# Güvenlik politikası

## Bir güvenlik açığını bildirme

Lütfen herkese açık bir issue (sorun) oluşturmayın. Açığı bu deponun **Security (Güvenlik)** sekmesi üzerinden, **Report a vulnerability (Bir güvenlik açığı bildir)** seçeneğini kullanarak bildirin.
Bulduğunuz açığı, SCSKiller sürümünü (Hakkında sayfasında yazar) ve açığın nasıl yeniden oluşturulabileceğini adım adım ekleyin. En geç 7 gün içinde ilk yanıtı alacaksınız. Lütfen açığı kamuya açıklamadan önce düzeltilmesi için makul bir süre tanıyın.

## Kapsam

- Uygulama (app), `scskiller` komut satırı ve yerel (native) araçlar (`scskiller_warm.exe`, kaydedici `d3d12.dll`).
- Kaydedici: Bir oyunun sürecinde (process) yaptığı her şey ve SCSKiller'ın kendi klasörleri ya da kurulu olduğu oyunun klasörü dışında yazdığı herhangi bir şey.
- Güncelleyici ve güncelleme akışı imzası: Uygulamanın kabul edeceği tahrif edilmiş bir akış veya paket.
- Oturum açma akışı ve uygulamanın depoladığı token'lar.

Yalnızca en son çıkan kararlı sürüm (stable release) desteklenmektedir.
