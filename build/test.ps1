# Test paketi, her biri testlerin kendine düşen kısmını çalıştıran birden fazla test işlemi olarak aynı anda çalıştırılır; seri Timing koleksiyonu dahildir. 
# Bir testin diğer bir testle paylaştığı her şey tek bir işlemde (process) çalışır (statik durum, işlem adları, defter (ledger) ve mutex kapsamı, geçici klasörlerin tümü işlem başınadır), böylece bir işlemin testlerinin yapılacağı yerlerde işlemler çakışamaz.
# AppTests, test adlarının ilk harflerine göre, yaklaşık olarak aynı sürelik paylara bölünür; son pay ise geri kalanı alır.
# $Alone: yük altında kendi kendisiyle yarışan (race) testler, yanlarında hiçbir şey olmadan paylardan sonra çalıştırılır.
param(
    [string]$Filter = 'Needs!=Gpu&Needs!=Game',
    [string]$Configuration = 'Release',
    [switch]$List   # çalıştırmak yerine her payın (share) test sayısını yazdır
)
$ErrorActionPreference = 'Stop'

$dll = Join-Path $PSScriptRoot "..\tests\SCSKiller.Tests\bin\$Configuration\net10.0-windows\SCSKiller.Tests.dll"
if (-not (Test-Path $dll)) { throw "Derlenmemiş (not built): $dll" }

$app = 'FullyQualifiedName~Platform.AppTests.'
$appShares = @(
    @('T'),
    @('An'),
    @('A_r', 'O'),
    @('A_c', 'A_g'),
    @('B', 'A_p'),
    @('A_s', 'R', 'A_m'),
    @('A_d', 'Am', 'S', 'A_h', 'A_f', 'W', 'A_w')
)
function Any($prefixes) { '(' + (($prefixes | ForEach-Object { "$app$_" }) -join '|') + ')' }
$taken = $appShares | ForEach-Object { $_ } | ForEach-Object { "FullyQualifiedName!~Platform.AppTests.$_" }
$shares = [ordered]@{}
for ($i = 0; $i -lt $appShares.Count; $i++) { $shares["app$($i + 1)"] = Any $appShares[$i] }
$shares['app-rest'] = (@($app) + $taken) -join '&'
$shares['overlay'] = 'FullyQualifiedName~Platform.OverlayIniTests.'
$shares['rest'] = 'FullyQualifiedName!~Platform.AppTests.&FullyQualifiedName!~Platform.OverlayIniTests.'
$alone = @('Platform.AppTests.A_fresh_copy_with_its_own_data_folder_changes_nothing_of_the_installed_copys')
foreach ($name in @($shares.Keys)) { $shares[$name] = (@("($($shares[$name]))") + ($alone | ForEach-Object { "FullyQualifiedName!~$_" })) -join '&' }

$out = Join-Path ([IO.Path]::GetTempPath()) ("scskiller-test-shares-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory $out | Out-Null
function Run($names) {
    $runs = foreach ($name in $names) {
        $a = @('test', "`"$dll`"", '--filter', "`"($($shares[$name]))&($Filter)`"")
        $a += if ($List) { '--list-tests' } else { @('--logger', '"console;verbosity=normal"', '--', 'RunConfiguration.TreatNoTestsAsError=true') }
        $p = Start-Process dotnet -ArgumentList $a -NoNewWindow -PassThru `
            -RedirectStandardOutput (Join-Path $out "$name.log") -RedirectStandardError (Join-Path $out "$name.err")
        $null = $p.Handle   # işlem sona erdikten sonra çıkış kodunu okunabilir tutar
        [pscustomobject]@{ Name = $name; Process = $p }
    }
    foreach ($r in $runs) {
        $r.Process.WaitForExit()
        $log = Get-Content (Join-Path $out "$($r.Name).log")
        if ($List) { "$($r.Name): $(@($log | Where-Object { $_ -match '^\s{4}\S' }).Count) test, $($r.Process.ExitCode) koduyla çıkış yaptı" }
        else {
            "::group::$($r.Name)"
            $log
            '::endgroup::'
            "$($r.Name): $([int]($r.Process.ExitTime - $r.Process.StartTime).TotalSeconds) saniye içinde $($r.Process.ExitCode) koduyla çıkış yaptı"
        }
        Get-Content (Join-Path $out "$($r.Name).err")
        if ($r.Process.ExitCode -ne 0) { $script:failed += $r.Name }
    }
}
$failed = @()
Run @($shares.Keys)
$shares['alone'] = '(' + (($alone | ForEach-Object { "FullyQualifiedName~$_" }) -join '|') + ')'
Run @('alone')
Remove-Item -Recurse -Force $out
if ($failed) { "Başarısız oldu: $($failed -join ', ')"; exit 1 }
