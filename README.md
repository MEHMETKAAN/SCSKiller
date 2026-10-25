<p align="center">
  <img width="128" src=".github/assets/logo.png" alt="SCSKiller logo">
</p>
<h1 align="center">SCSKiller</h1>
<p align="center">
  <strong>Shader Derleme Takılmasını Azaltıcı.</strong> SCSKiller, siz oyuna başlamadan önce oyunlarınızın shader'larını GPU sürücünüzün önbelleğine derler, böylece oyun ortasında derleme yapmak için duraklamaz. Windows için ücretsiz ve açık kaynaklıdır.
</p>

<p align="center">
  <a href="https://github.com/BlueHeisenberg/SCSKiller/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/BlueHeisenberg/SCSKiller"></a>
  <a href="https://github.com/BlueHeisenberg/SCSKiller/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/BlueHeisenberg/SCSKiller/total"></a>
  <a href="LICENSE"><img alt="Licence: GPL-3.0-or-later" src="https://img.shields.io/badge/licence-GPL--3.0--or--later-blue"></a>
  <a href="https://www.patreon.com/SCSKiller"><img alt="Patreon" src="https://img.shields.io/badge/Patreon-support-f96854?logo=patreon&logoColor=white"></a>
  <a href="https://discord.gg/st7C4yCTcN"><img alt="Discord" src="https://img.shields.io/badge/Discord-join-5865f2?logo=discord&logoColor=white"></a>
</p>

<p align="center">
  <a href="https://scskiller.com">Web Sitesi</a>
  ·
  <a href="https://github.com/BlueHeisenberg/SCSKiller/releases/latest">İndir</a>
  ·
  <a href="https://www.patreon.com/SCSKiller">Patreon</a>
  ·
  <a href="https://discord.gg/st7C4yCTcN">Discord</a>
  ·
  <a href="https://x.com/SCSKiller">X</a>
  ·
  <a href="https://github.com/BlueHeisenberg/SCSKiller/issues/new?template=bug.yml">Hata Bildir</a>
  ·
  <a href="https://github.com/BlueHeisenberg/SCSKiller/issues/new?template=game-request.yml">Oyun İsteğinde Bulun</a>
</p>

> [!WARNING]
> Tek resmi indirme kaynağı bu deponun [Releases (Sürümler)](https://github.com/BlueHeisenberg/SCSKiller/releases) sayfasıdır. SCSKiller'a ait hesaplar ve siteler için [resmi bağlantılara](#resmi-bağlantılar) bakın.

<p align="center">
  <img src=".github/assets/library.webp" alt="SCSKiller's library: games grouped by store, with shader and pipeline counts, cache size, compile time, a status such as Needs rebuilding or Warmed, and Play buttons.">
</p>

## Neden Gerekli?

Bir oyun yeni bir görsel efektle karşılaştığında, GPU sürücüsünün o an o efekti (shader) derlemesi gerekir ve ekrandaki kare bu işlem bitene kadar bekler; işte bu duruma shader derleme takılması (stutter) denir. SCSKiller, oyunun yüklü dosyalarındaki shader'ları okur, oyunun oluşturacağı veri yollarını (pipeline) hesaplar ve oyuna girmeden önce bunları GPU sürücüsünün önbelleğine derler.

## Sonuçlar

Soğuk (boş) bir sürücü önbelleği ile SCSKiller'ın derlemesinden sonra oyun başına yaklaşık 5 dakikalık oynanış verileridir. Takılma (stutter), oyunun shader derlemek için 20 ms veya daha fazla beklediği anları ifade eder.

<table>
  <tr><th>Oyun</th><th>GPU</th><th>Ölçüm</th><th>Soğuk (İlk Açılış)</th><th>SCSKiller İle</th></tr>
  <tr><td rowspan="4">Final Fantasy VII Rebirth</td><td rowspan="2">NVIDIA<br>RTX 5090</td><td>Takılmalar (≥ 20 ms)</td><td>19</td><td><b>0</b></td></tr>
  <tr><td>En kötü duraklama</td><td>76 ms</td><td><b>5 ms</b></td></tr>
  <tr><td rowspan="2">AMD<br>Ryzen AI Max+ 395 (Strix Halo)</td><td>Takılmalar (≥ 20 ms)</td><td>162</td><td><b>0</b></td></tr>
  <tr><td>En kötü duraklama</td><td>291 ms</td><td><b>7 ms</b></td></tr>
  <tr><td rowspan="2">Silent Hill: Townfall</td><td rowspan="2">NVIDIA<br>RTX 5090</td><td>Takılmalar (≥ 20 ms)</td><td>24</td><td><b>0</b></td></tr>
  <tr><td>En kötü duraklama</td><td>347 ms</td><td><b>8 ms</b></td></tr>
  <tr><td rowspan="4">Hogwarts Legacy</td><td rowspan="2">NVIDIA<br>RTX 5090</td><td>Takılmalar (≥ 20 ms)</td><td>1,014</td><td><b>87</b></td></tr>
  <tr><td>En kötü duraklama</td><td>2,400 ms</td><td><b>100 ms</b></td></tr>
  <tr><td rowspan="2">AMD<br>Ryzen AI Max+ 395 (Strix Halo)</td><td>Takılmalar (≥ 20 ms)</td><td>27,743</td><td><b>206</b></td></tr>
  <tr><td>En kötü duraklama</td><td>2,146 ms</td><td><b>104 ms</b></td></tr>
  <tr><td rowspan="2">Tiny Tina's Wonderlands</td><td rowspan="2">NVIDIA<br>RTX 5090</td><td>Takılmalar (≥ 20 ms)</td><td>583</td><td><b>0</b></td></tr>
  <tr><td>En kötü duraklama</td><td>315 ms</td><td><b>13 ms</b></td></tr>
  <tr><td>Star Wars Jedi: Survivor</td><td>NVIDIA<br>RTX 5090</td><td>Takılmalar (≥ 20 ms)</td><td>9,038</td><td><b>11</b></td></tr>
</table>

SCSKiller'ın kendi kaydedicisiyle ölçülmüştür. Rakamlarınız oyuna, GPU'ya ve sürücünüze bağlı olarak değişiklik gösterir. SCSKiller sadece shader derleme takılmalarını çözer; harita geçişi veya kaplama yükleme kaynaklı takılmaları çözemez. Bulamadığı veya kaydedemediği bir shader oyun içinde yine takılmaya yol açabilir.

## Özellikler

- **NVIDIA ve AMD**, DirectX 12 oyunları ve NVIDIA üzerinde DirectX 11 oyunları desteklenir.
- Steam, Epic Games, EA app, GOG, Ubisoft Connect, Xbox (PC), Battle.net, PURPLE, HoYoPlay, Gaijin ve Riot Client'taki **oyunlarınızı otomatik bulur**, kendi eklediğiniz oyunları da destekler.
- **Takılma yaptığı bilinen oyunları en üste koyar**, sebebini belirtir ve kendi shader'larını kendisi derleyen oyun motorlarını işaretler.
- Sürücü güncellemelerinden sonra temizlenen önbellekleri **yeniden derler** (izin verirseniz kendi kendine yapar).
- Dosyaların yeterli bilgiyi vermediği oyunlar için **isteğe bağlı kaydedici**: açıkken birkaç dakika oynayın, ardından derleyin.
- **Oturum başına kare süreleri (Frame times)**: Kaydedici açıkken oyunun sayfası son oturumunuzun grafiğini çizer ve shader takılmalarını diğer donmalardan ayırır. Veriler tamamen PC'nizde kalır.
- Oyunu doğrudan kendi mağazası üzerinden başlatan **Oyna butonu**.
- Patreon destekçileri için **topluluk shader hash veritabanı**: Başka oyuncuların kayıtları sayesinde oyunlarınızı kendiniz kaydetmeden derleyebilirsiniz. Kendi kayıtlarınızı paylaşmak tamamen isteğe bağlı, anonim ve sadece hash (şifrelenmiş veri) boyutundadır.
- **Anti-hile oyunlarına dokunmaz**: Dosyalarını okumak dışında başlatma yapmaz, içine enjekte olmaz, kaydediciyi kullanmaz. İstisna olarak, her açılışta bizzat onayladığınız riskli oturumlarda çalışabilir. (Örneğin; ELDEN RING ve ARMORED CORE VI'yı anti-hilesiz çevrimdışı başlatmak veya Ayarlar'dan risk alarak anti-hileli oyunlara izin vermek gibi).

## Kurulum

Sürüm [Releases](https://github.com/BlueHeisenberg/SCSKiller/releases/latest) sayfasından `SCSKiller-Setup.exe` dosyasını indirin ve çalıştırın. Yalnızca kendi kullanıcınız için kurulur, yönetici hakları gerektirmez ve kendini otomatik günceller.

Taşınabilir sürüm istiyorsanız `SCSKiller-<versiyon>-Portable.zip` dosyasını istediğiniz bir klasöre çıkartıp `SCSKiller.exe`'yi çalıştırın.

## Nasıl Kullanılır?

1. **SCSKiller'ı açın.** Oyunlarınızı ve sahip oldukları shader'ları bulacaktır.
2. **Oyunları sıraya ekleyin ve Derle'ye basın.** "Önerilenlerin tümünü ekle" seçeneği iyi bir başlangıçtır. Derleme sırasında oyunu kapalı tutun.
3. **Oynayın.** Kendi mağazasından veya Oyna butonuyla oyuna girebilirsiniz.

Eğer bir oyun kayıt gerektiriyorsa **Kaydet (Record)** butonunu açın, birkaç dakika oynayın, ardından derleyin.
