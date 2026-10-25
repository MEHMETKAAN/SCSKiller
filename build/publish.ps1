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
param([string]$Configuration = "Release", [switch]$NoZip, [switch]$NoOodle, [string]$Version)$ErrorActionPreference = "Stop"
$repo = Split-Path$PSScriptRoot -Parent

# 0. Sürüm (version): -Version, aksi takdirde HEAD'deki tek v* etiketi (terfiler (promotions) bir commit'i birkaç kez etiketler: bu durumda hangisi olduğunu belirtin)
if (-not $Version) {
    $tags = @(git -C$repo tag --points-at HEAD -l "v*")
    if ($tags.Count -gt 1) { throw "HEAD'de birden fazla etiket var ($($tags -join ', ')): -Version parametresini geçin" }
    if ($tags.Count -eq 1) { $Version =$tags[0] }
}
$Version =$Version -replace '^v', ''
if ($Version -and $Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$') { throw "SemVer formatında bir X.Y.Z[-pre] değil: $Version" }
# sürüm numaralı bir derleme, sunucunun imzalı listelerini doğrular: kurallar anahtarı sabitlenmelidir (sürüm öncesinde tools/release-sign keygen --for rules)
if ($Version -and (Get-Content (Join-Path$repo "src\SCSKiller.Core\App\ContentTrust.cs") -Raw) -notmatch '["rules-a"]\s*=\s*"[A-Za-z0-9+/]{43}="') {
    throw "ContentTrust.RulesKeys dosyasında rules-a anahtarı yok: sürüm yayınlamadan önce tools/release-sign keygen --for rules komutuyla ortak anahtarı (public key) sabitleyin"
}
$versionArgs = if ($Version) { @("-p:Version=$Version", "-p:IncludeSourceRevisionInInformationalVersion=false") } else { @() }
# Yayımlanan hata ayıklama sembolleri (debug symbols), özel ödeme (checkout) yolunu ve depo URL'sini gömer.
$versionArgs += "-p:DebugType=None", "-p:DebugSymbols=false"
Write-Host "sürüm: $(if ($Version) {$Version } else { '0.0.0-internal.0 (geliştirici-dev)' })"
$dotnet = Join-Path$env:LOCALAPPDATA "Microsoft\dotnet\dotnet.exe"   # .NET 10 SDK kullanıcı başınadır; PATH'de daha eski bir sürüm olabilir
if (-not (Test-Path $dotnet)) {$dotnet = "dotnet" }
$dist = Join-Path$repo "dist"
$out = Join-Path$dist "SCSKiller"
$cli = Join-Path$out "cli"
$native = Join-Path$out "native"

function Run([string]$exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$exe $argv,$LASTEXITCODE çıkış koduyla başarısız oldu" }
}

if (Test-Path $out) { Remove-Item$out -Recurse -Force }

# 1. proxy CMake derlemesinden gelen native (yerel) araçlar
$build = Join-Path$repo "proxy\build"
$scskVersion = if ($Version) {$Version } else { "0.0.0-internal.0" }   # her seferinde: önbelleğe alınmış bir sürüm bir sonrakine taşınmamalıdır
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
    "--self-contained", "true", "-p:Platform=x64", "-p:EnableMsixTooling=true", "-o", $out) +$versionArgs)   # MSIX tooling: aksi takdirde uygulamanın .pri/.xbf'si yayınlanmaz ve XAML başlangıçta
