<#
.SYNOPSIS
dist\SCSKiller\ ve dist\SCSKiller.zip dosyalarını derler (build eder):

  SCSKiller.exe            WinUI uygulaması, bağımsız (.NET + Windows App SDK), + oodle-data-shared.dll, zlib-ng2.dll
  cli\scskiller.exe        komut satırı (CLI), bağımsız, aynı Core (Çekirdek) projesi ve commit, + iki codec DLL'i
  cli\scskillerw.exe       Aynı CLI'nin GUI-alt sistemi exe'si olarak işaretlenmiş hali: zamanlanmış görevin çalıştırdığı (konsol penceresi yok)
  native\                  scskiller_warm.exe + proxy d3d12.dll (uygulama native\ içine, CLI ..\native\ içine bakar)
                           + scskiller_ags.dll (AMD AGS sarıcısı olarak aynı kaydedici, oyunun amd_ags_x64.dll'i olarak yüklenir)
                           + amd_ags_x64.dll (AMD AGS, proxy'nin CMake yapılandırması tarafından indirilir)
                           + segheap\scskiller_warm.exe (segment yığınında aynısı: NVIDIA'nın ısıtması (warm))
  THIRD-PARTY-NOTICES.md   her üçüncü taraf bileşen ve lisansı; notices\ klasörü Microsoft paketlerinin kendi bildirimlerini tutar

CLI uygulamanın yanında duramaz: scskiller.exe ve SCSKiller.exe NTFS'de aynı dosya adıdır.

-NoOodle, oodle-data-shared.dll'i dışarıda bırakır (CI ve yayınlanan her şey: Oodle tescillidir (proprietary), yeniden dağıtım
belirlenmemiştir, THIRD-PARTY-NOTICES.md); UnrealReader ve FromSoft okuyucusu bunu ilk kullanımda CUE4Parse aracılığıyla indirir.

-Version X.Y.Z[-pre] (baştaki 'v' atılır), her exe ve DLL'yi aynı ProductVersion (SignPath) ile damgalar.
Kullanılmazsa: yalnızca bir tane varsa HEAD'deki v* etiketi, aksi takdirde dev varsayılanı olan 0.0.0-internal.0+<sha> kullanılır.
#>
param([string]$Configuration = "Release", [switch]$NoZip, [switch]$NoOodle, [string]$Version)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent

# 0. Sürüm (version): -Version, aksi takdirde HEAD'deki tek v* etiketi (terfiler (promotions) bir commit'i birkaç kez etiketler: bu durumda hangisi olduğunu belirtin)
if (-not $Version) {
    $tags = @(git -C $repo tag --points-at HEAD -l "v*")
    if ($tags.Count -gt 1) { throw "HEAD'de birden fazla etiket var ($($tags -join ', ')): -Version parametresini geçin" }
    if ($tags.Count -eq 1) { $Version = $tags[0] }
}
$Version = $Version -replace '^v', ''
if ($Version -and $Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$') { throw "SemVer formatında bir X.Y.Z[-pre] değil: $Version" }
# sürüm numaralı bir derleme, sunucunun imzalı listelerini doğrular: kurallar anahtarı sabitlenmelidir (sürüm öncesinde tools/release-sign keygen --for rules)
if ($Version -and (Get-Content (Join-Path $repo "src\SCSKiller.Core\App\ContentTrust.cs") -Raw) -notmatch '\["rules-a"\]\s*=\s*"[A-Za-z0-9+/]{43}="') {
    throw "ContentTrust.RulesKeys dosyasında rules-a anahtarı yok: sürüm yayınlamadan önce tools/release-sign keygen --for rules komutuyla ortak anahtarı (public key) sabitleyin"
}
$versionArgs = if ($Version) { @("-p:Version=$Version", "-p:IncludeSourceRevisionInInformationalVersion=false") } else { @() }
# Yayımlanan hata ayıklama sembolleri (debug symbols), özel ödeme (checkout) yolunu ve depo URL'sini gömer.
$versionArgs += "-p:DebugType=None", "-p:DebugSymbols=false"
Write-Host "sürüm: $(if ($Version) { $Version } else { '0.0.0-internal.0 (geliştirici-dev)' })"
$dotnet = Join-Path $env:LOCALAPPDATA "Microsoft\dotnet\dotnet.exe"   # .NET 10 SDK kullanıcı başınadır; PATH'de daha eski bir sürüm olabilir
if (-not (Test-Path $dotnet)) { $dotnet = "dotnet" }
$dist = Join-Path $repo "dist"
$out = Join-Path $dist "SCSKiller"
$cli = Join-Path $out "cli"
$native = Join-Path $out "native"

function Run([string]$exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$exe $argv, $LASTEXITCODE çıkış koduyla başarısız oldu" }
}

if (Test-Path $out) { Remove-Item $out -Recurse -Force }

# 1. proxy CMake derlemesinden gelen native (yerel) araçlar
$build = Join-Path $repo "proxy\build"
$scskVersion = if ($Version) { $Version } else { "0.0.0-internal.0" }   # her seferinde: önbelleğe alınmış bir sürüm bir sonrakine taşınmamalıdır
Run cmake @("-S", (Join-Path $repo "proxy"), "-B", $build, "-A", "x64", "-DSCSK_VERSION=$scskVersion")
Run cmake @("--build", $build, "--config", "Release")
New-Item -ItemType Directory -Force $native | Out-Null
Copy-Item (Join-Path $build "Release\scskiller_warm.exe"), (Join-Path $build "Release\d3d12.dll"), (Join-Path $build "Release\amd_ags_x64.dll"), (Join-Path $build "Release\scskiller_ags.dll") $native
New-Item -ItemType Directory -Force (Join-Path $native "segheap") | Out-Null
Copy-Item (Join-Path $build "Release\segheap\scskiller_warm.exe") (Join-Path $native "segheap")

# 2. app (uygulama) + CLI
# SDK, obj\...\apphost.exe dosyasını assembly değişene kadar yeniden kullanır, bu nedenle tek başına bir CETCompat değişikliği eski bayrağı gönderebilir
Get-ChildItem (Join-Path $repo "src") -Recurse -Filter apphost.exe | Where-Object FullName -like "*\obj\*" | Remove-Item
Run $dotnet (@("publish", (Join-Path $repo "src\SCSKiller.App\SCSKiller.App.csproj"), "-c", $Configuration, "-r", "win-x64",
    "--self-contained", "true", "-p:Platform=x64", "-p:EnableMsixTooling=true", "-o", $out) + $versionArgs)   # MSIX tooling: aksi takdirde uygulamanın .pri/.xbf'si yayınlanmaz ve XAML başlangıçta çöker
Run $dotnet (@("publish", (Join-Path $repo "src\SCSKiller.Cli\SCSKiller.Cli.csproj"), "-c", $Configuration, "-r", "win-x64",
    "--self-contained", "true", "-o", $cli) + $versionArgs)

# 3. scskillerw.exe: isteğe bağlı başlık Alt Sistem (Subsystem) (e_lfanew + 24 + 68) 3 = konsol -> 2 = GUI, SDK'nın WinExe için yaptığı gibi.
#    Apphost hala aynı klasörden scskiller.dll'i çalıştırır.
$bytes = [IO.File]::ReadAllBytes((Join-Path $cli "scskiller.exe"))
$subsystem = [BitConverter]::ToInt32($bytes, 0x3C) + 24 + 68
if ($bytes[$subsystem] -ne 3) { throw "scskiller.exe: beklenmeyen PE alt sistemi $($bytes[$subsystem])" }
$bytes[$subsystem] = 2
[IO.File]::WriteAllBytes((Join-Path $cli "scskillerw.exe"), $bytes)

# 3b. hiçbir .NET exe CET uyumlu olamaz (Directory.Build.props): bayrak, tip 20 (IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS) olan debug dizini girişi verisinin 0. bitidir. PE32+: debug dizini 6. veri dizinidir, isteğe bağlı başlık + 160'tadır.
foreach ($exe in "SCSKiller.exe", "cli\scskiller.exe", "cli\scskillerw.exe") {
    $b = [IO.File]::ReadAllBytes((Join-Path $out $exe))
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    $opt = $pe + 24
    $sections = $opt + [BitConverter]::ToUInt16($b, $pe + 20)
    $rva = [BitConverter]::ToUInt32($b, $opt + 160)
    $size = [BitConverter]::ToUInt32($b, $opt + 164)
    $dir = $null
    for ($i = 0; $i -lt [BitConverter]::ToUInt16($b, $pe + 6); $i++) {
        $s = $sections + 40 * $i
        $va = [BitConverter]::ToUInt32($b, $s + 12)
        if ($rva -ge $va -and $rva -lt $va + [BitConverter]::ToUInt32($b, $s + 16)) { $dir = $rva - $va + [BitConverter]::ToUInt32($b, $s + 20) }
    }
    if ($null -eq $dir) { throw "${exe}: hata ayıklama (debug) dizini yok" }
    for ($e = $dir; $e -lt $dir + $size; $e += 28) {
        if ([BitConverter]::ToUInt32($b, $e + 12) -eq 20 -and ([BitConverter]::ToUInt32($b, [BitConverter]::ToUInt32($b, $e + 24)) -band 1)) {
            throw "$exe CET uyumludur: .NET, CET özel APC'leri olmayan Windows 10 yapılarında başlangıçta anında hata verir (fail-fast)"
        }
    }
}

# 4. her iki yürütülebilir (executable) dosyanın yanındaki codec'ler (UnrealReader bunları AppContext.BaseDirectory'den yükler), uygulamanın veri klasöründen asla alınmaz, derlemeye ait (build-owned) bir önbellekten gelir: fetch-codecs sabitlenmiş kopyaları orada tutar (Codecs.Pinned), eksik veya eşleşmeyen birini indirir ve hiçbir kopya eşleşmediğinde derlemeyi başarısız yapar
$codecs = @(if (-not $NoOodle) { "oodle-data-shared.dll" }) + "zlib-ng2.dll"
$cache = Join-Path ([Environment]::GetFolderPath("LocalApplicationData")) "SCSKiller-build\codecs"
Run (Join-Path $cli "scskiller.exe") @("fetch-codecs", $cache)
foreach ($c in $codecs) { Copy-Item (Join-Path $cache $c) $cli; Copy-Item (Join-Path $cache $c) $out }

# 4b. lisanslar: bildirimlerimiz ve lisansımız (GPL-3.0-or-later + bölüm 7 izni), artı sevk edildiği şekliyle Microsoft paketlerinin büyük bildirim (notice) dosyaları
Copy-Item (Join-Path $repo "THIRD-PARTY-NOTICES.md") $out
foreach ($l in "LICENSE", "LICENSE-EXCEPTION.txt") { Copy-Item (Join-Path $repo $l) $out }
$nuget = if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else { Join-Path $env:USERPROFILE ".nuget\packages" }
New-Item -ItemType Directory -Force (Join-Path $out "notices") | Out-Null
# sürümler gerçekte yayınlanan şeyden gelir (bu majör veya daha yeni bir SDK/çalışma zamanı onu derler)
$runtime = ((Get-Content (Join-Path $out "SCSKiller.runtimeconfig.json") -Raw | ConvertFrom-Json).runtimeOptions.includedFrameworks |
    Where-Object name -eq "Microsoft.NETCore.App").version
$libs = (Get-Content (Join-Path $repo "src\SCSKiller.App\obj\project.assets.json") -Raw | ConvertFrom-Json).libraries.PSObject.Properties.Name   # onu oluşturan restore işlemi
function PackageVersion($id) {
    $v = $libs | Where-Object { $_ -like "$id/*" } | Select-Object -First 1
    if (-not $v) { throw "$id, uygulamanın geri yüklenen (restored) paketleri arasında değil" }
    $v.Split("/")[1]
}
foreach ($n in @(
        @("microsoft.netcore.app.runtime.win-x64\$runtime\THIRD-PARTY-NOTICES.TXT", "dotnet-THIRD-PARTY-NOTICES.txt"),
        @("microsoft.windowsappsdk.runtime\$(PackageVersion 'Microsoft.WindowsAppSDK.Runtime')\NOTICE.txt", "WindowsAppSDK-NOTICE.txt"),
        @("microsoft.windowsappsdk.winui\$(PackageVersion 'Microsoft.WindowsAppSDK.WinUI')\NOTICE.txt", "WinUI-NOTICE.txt"),
        @("microsoft.web.webview2\$(PackageVersion 'Microsoft.Web.WebView2')\NOTICE.txt", "WebView2-NOTICE.txt"),
        @("communitytoolkit.highperformance\$(PackageVersion 'CommunityToolkit.HighPerformance')\ThirdPartyNotices.txt", "CommunityToolkit-ThirdPartyNotices.txt"))) {
    Copy-Item (Join-Path $nuget $n[0].ToLowerInvariant()) (Join-Path $out "notices\$($n[1])")   # THIRD-PARTY-NOTICES.md sevk edilen (shipped) sürümleri listeler
}

# 5. bağımsız (self-contained) .NET (".NET yükle" istemi yok) dahil düzen (layout) kontrolü: her exe'nin yanındaki çalışma zamanı (runtime) ve paylaşılan bir çerçeve (shared framework) yerine includedFrameworks'ü listeleyen runtimeconfig.json
$expected = "SCSKiller.exe", "oodle-data-shared.dll", "zlib-ng2.dll", "cli\scskiller.exe", "cli\scskillerw.exe",
    "cli\oodle-data-shared.dll", "cli\zlib-ng2.dll", "native\scskiller_warm.exe", "native\segheap\scskiller_warm.exe", "native\d3d12.dll", "native\amd_ags_x64.dll", "native\scskiller_ags.dll",
    "hostfxr.dll", "coreclr.dll", "cli\hostfxr.dll", "cli\coreclr.dll", "Microsoft.WindowsAppRuntime.dll", "SCSKiller.pri",
    "THIRD-PARTY-NOTICES.md", "LICENSE", "LICENSE-EXCEPTION.txt", "notices\dotnet-THIRD-PARTY-NOTICES.txt"
if ($NoOodle) { $expected = $expected | Where-Object { $_ -notlike "*oodle-data-shared.dll" } }
$missing = $expected | Where-Object { -not (Test-Path (Join-Path $out $_)) }
if ($missing) { throw "dist\SCSKiller klasöründe şunlar eksik: $($missing -join ', ')" }
if ($NoOodle -and (Get-ChildItem $out -Recurse -Filter "oodle-data-shared.dll")) { throw "-NoOodle kullanıldı, ancak oodle-data-shared.dll dist\SCSKiller içinde bulunuyor" }
if (Get-ChildItem $out -Recurse -Filter "*.pdb") { throw "Hata ayıklama sembolleri (debug symbols) yayınlanmamalıdır" }
foreach ($rc in "SCSKiller.runtimeconfig.json", "cli\scskiller.runtimeconfig.json") {
    if ((Get-Content (Join-Path $out $rc) -Raw) -notmatch '"includedFrameworks"') { throw "$rc framework'e bağımlıdır (framework-dependent)" }
}

# 6. zip işlemi (kökünde SCSKiller\ klasörü ile)
if (-not $NoZip) {
    $zip = Join-Path $dist "SCSKiller.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($out, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
}
$msg = "dist\SCSKiller klasörü: {0:N0} MB" -f ((Get-ChildItem $out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
if (-not $NoZip) { $msg += ", dist\SCSKiller.zip arşivi: {0:N0} MB" -f ((Get-Item $zip).Length / 1MB) }
Write-Host $msg
