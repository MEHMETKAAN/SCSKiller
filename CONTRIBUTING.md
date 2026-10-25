# Katkıda Bulunma

Hatalar ve oyun istekleri, mevcut şablonlar kullanılarak Issues (Sorunlar) sekmesine yazılmalıdır. Güvenlik sorunları asla herkese açık bir sorun olarak değil, doğrudan [SECURITY.md](SECURITY.md) üzerinden özel olarak iletilmelidir.

## Anti-hile Oyunları

SCSKiller, kullanıcı tarafından başlatılan özel bir oturum dışında, anti-hile içeren bir oyuna asla dokunmaz ve sadece dosyalarını okur: kaydedici yüklemez, enjeksiyon yapmaz, süreçlerini (process) açmaz. Bir oturum (Bkz. ARCHITECTURE.md, Kurallar) her oyun için özel olarak seçilir, her başlatmada onaylanır, SCSKiller tarafından başlatılır, yalnızca o süreçte kayıt yapar ve çıkışta kendini temizler. (Örn: `src/SCSKiller.Core/Games/offline-eac.json` listesindeki oyunlar için EasyAntiCheat olmadan veya kullanıcının Ayarlar'dan izin vermesi durumunda). Kaydedicinin yetkilerini bu sınırların ötesine genişleten veya kuralların etrafından dolanan kod değişiklikleri (Pull Request) kabul edilmeyecektir.

## Derleme Aşaması (Build)

C++ masaüstü iş yükü (MSVC x64, CMake) içeren Visual Studio 2022 ve .NET 10 SDK gereklidir:

      cmake -S proxy -B proxy/build -A x64
      cmake --build proxy/build --config Release
      dotnet build SCSKiller.slnx -c Release

CMake yapılandırması, AMD'nin `amd_ags_x64.dll` dosyasını indirir. Bu dosya olmadan, AGS kullanan oyunların AMD tarafındaki ön derlemeleri düz bir aygıt kullanır.

## Kod Stili

- Yaptığınız değişikliği etrafındaki kodun mimarisine uydurun. Her PR için yalnızca bir konu belirleyin ve küçük tutun.
- Sadece kodun tek başına anlatamadığı şeyleri yorum satırı yapın: doğrudan anlaşılamayan bir sebep, sürücü/motor geçici çözümleri veya dosya biçimi detayları gibi. Yorum satırına alınmış (kullanılmayan) kodlar bırakmayın.
- Kullanıcının fark edeceği her türlü değişiklik için [CHANGELOG.md](CHANGELOG.md) dosyasına `## [Unreleased]` başlığı altına bir satır ekleyin.
- Oyun dosyalarını, shader dökümlerini, kayıtları (`*.db`) veya üçüncü taraf derlenmiş dosyaları (binaries) projenin içine dahil etmeyin (commit'lemeyin).
