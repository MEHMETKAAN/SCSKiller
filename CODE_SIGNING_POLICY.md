# Kod imzalama politikası

## Durum

SCSKiller, [SignPath Foundation](https://signpath.org) üzerinden ücretsiz kod imzalama başvurusunda bulunmuştur. Onaylanana kadar sürümler **imzasızdır** ve Windows "Bilinmeyen yayıncı" uyarısı verir. Her sürüm, indirmelerinin SHA-256 değerini listeler.

## Neler imzalanır

Yalnızca bu deponun kaynak kodundan GitHub Actions sürüm iş akışı (workflow) tarafından, kararlı bir sürüm etiketinden (`vX.Y.Z`) derlenen ikili dosyalar (binaries):

- `SCSKiller.exe` (uygulama)
- `cli\scskiller.exe` ve `cli\scskillerw.exe` (komut satırı)
- `native\scskiller_warm.exe` ve `native\segheap\scskiller_warm.exe` (oyun dışında çalışan derleyici)
- `native\d3d12.dll` (isteğe bağlı kaydedici)
- `SCSKiller-Setup.exe` ve `Update.exe` (Velopack)

Paketteki yukarı akış ikili dosyaları ve çalışma zamanları (.NET runtime, Windows App SDK, Velopack'in kendi bileşenleri, AMD'nin `native\amd_ags_x64.dll` dosyası) bu proje tarafından imzalanmaz. Alfa ve beta yapıları burada derlenmez ve bu politika kapsamında imzalanmaz. İmzalanan her dosya `SCSKiller` ürün adını ve sürüm numarasını taşır.

## Roller

| Rol | Kim |
|---|---|
| Yazar (kaynak depoya commit yapar) | [Proje Sorumlusu (maintainer)](https://github.com/BlueHeisenberg) |
| İnceleyici (birleştirilmeden önce her dış katkıyı inceler) | Proje Sorumlusu |
| Onaylayıcı (her imzalama isteğini onaylar) | Proje Sorumlusu |

Katkılar, birleştirilmeden önce herkese açık pull request'lerde (PR) incelenir ([CONTRIBUTING.md](CONTRIBUTING.md)). Her imzalama isteği manuel olarak onaylanır. Tüm üyeler GitHub ve SignPath üzerinde çok faktörlü kimlik doğrulama (MFA) kullanır.

## Gizlilik

SCSKiller, siz istemediğiniz sürece sizinle, bilgisayarınızla veya oyunlarınızla ilgili hiçbir şey göndermez. Şuralara bağlanır:

- SCSKiller hizmeti (`api.scskiller.com`, yedek `api.scskiller.io`):
  - karşılama metni, takılma yaptığı bilinen oyunların listesi ve topluluk veritabanının dizini için (hakkınızda hiçbir şey gönderilmez)
  - Patreon ile giriş yapmak için
  - giriş yapmış destekçiler için, oyunlarınızın yapılarının topluluk kayıtlarını indirmek ve alfa/beta kanalındaysa bu güncellemeleri kontrol etmek için
  - yalnızca "Shader hash değerlerimi paylaş" açıksa, kayıtlarınızın yalnızca hash halini (shader kodu olmadan), hesabınızla bağlantılı olmayan anonim bir cihaz altında yüklemek için
  - yalnızca bir oyunun "Sorun bildir" kısmında Gönder'e bastığınızda, size gösterdiği raporu göndermek için (90 gün saklanır)
- `dl.scskiller.io`, alfa ve beta güncellemeleri için
- GitHub Releases, kararlı güncellemeler için
- Herkese açık GitHub barındırmalı kaynaklar, bir sıkıştırma kütüphanesi (Oodle) veya oyunun ihtiyaç duyduğu bir arşiv anahtarı ilk kez gerektiğinde.

Siz paylaşımı açmadığınız sürece shader'lar ve kayıtlar PC'nizde kalır. Kare süreleri (Frame times) her zaman PC'nizde kalır.

## Bildirim

Kötü amaçlı veya yanlış kullanıldığına inandığınız imzalı bir ikili dosyayı [SECURITY.md](SECURITY.md) aracılığıyla bildirin.
