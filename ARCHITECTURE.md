# SCSKiller Mimarisi

SCSKiller, bir oyun çalışmadan önce GPU sürücüsünün shader önbelleğini doldurarak shader derleme takılmalarını ortadan kaldırır. Oyunun kurulu dosyalarından shader'larını okur, oyunun oluşturacağı veri yollarını (pipelines) planlar ve bunları sürücünün oyun sandığı ayrı bir süreçte yeniden oynatır. Dosyaların yeterli bilgi vermediği durumlarda (çalışma zamanında oluşturulan kök imzaları, AMD'nin duruma bağlı önbelleği, ışın izleme), isteğe bağlı bir kaydedici oyun oynanırken ne oluşturulduğunu yakalar ve bir sonraki plan bunu da yeniden oynatır.

Satıcıya veya oyun motoruna özgü her şey `src/SCSKiller.Core/Contracts.cs` içindeki bir arayüzün arkasında durur: bir mağaza `IGameSource`, bir oyun motoru `IEngineReader`, bir GPU satıcısı kendi `VendorCaps` bilgileriyle bir `IGpuVendorBackend`'dir. Planlayıcı (planner) ve ısıtıcı (warmer) yalnızca bu kapsama verilerini okur.

## Bileşenler

**App (Uygulama)** (`src/SCSKiller.App/`). WinUI 3, paketlenmemiş: kütüphane, oyun sayfaları, kuyruk, önbellek, sistem tepsisi, ayarlar. Yalnızca View modelleri (`ViewModels.cs`) ve Velopack entegrasyonu (`Updater.cs`). Tüm mantık Core (Çekirdek) içinde, `IScsKiller` arayüzünün arkasındadır.

**Core (Çekirdek)** (`src/SCSKiller.Core/`). `ScsKiller` cephesi (`App/ScsKiller.cs`) oyuna özel durumu (`AppStore`), derleme kuyruğunu ve kaydedicinin yaşam döngüsünü yönetir.

- *Tarama (Scan)* (`Games/`): Her mağaza için bir `IGameSource` ve kullanıcının kendi eklediği veya oyun klasörlerinde bulduğu oyunlar için `ManualSource`. `GameFiles` asıl exe'yi seçer ve tek anti-hile dedektörüdür. `Vac`, Valve Anti-Cheat'e karar verir. `ReShade`, shader modlarını sınıflandırır.
- *Motor okuyucuları (Engine readers)* (`Unreal/`, `Unity/`, `FromSoft/`, `ReEngine/`, `RedEngine/`, `Northlight/`, `Dagor/`, `Rage/`, `Source2/`, `NaughtyDog/`, `SantaMonica/`, `Nixxes/`, `Tank/`, `Decima/`, `Luminous/`, `ForzaTech/` ve ham DXBC/DXIL içeren herhangi bir oyun için `Carved/`): Her biri, yapının shader'larını bir arada çizilebilen shader haritaları halinde gruplandırılmış bir `ShaderIndex` olarak listeler ve istendiğinde baytlarını okur. Bir shader, kendi konteynerinin SHA-1 değeridir (`ShaderContainer.Parse`).
- *Planlayıcı (Planner)* (`Planning/`): `Planner` ve `PlanBuilder`, indeks, kayıt ve kapsama verilerini bir plana dönüştürür. `RootSig`, `plan-rules.json` dosyasından (`RulesFile`) motorların çalışma zamanında oluşturduğu kök imzalarını yeniden oluşturur. `UnitCover`, aşama başına önbellek birimini kapsayan en az sayıdaki pipeline'ı seçer, `RtCollections` ışın izleme (ray tracing) koleksiyonlarını sentezler ve `Middleware`, FidelityFX ve XeSS gibi DLL'lere gömülü pipeline'ları kapsar. Statik örnekleyiciler (samplers), doku filtreleme (texture filtering) ayarını takip eder (`SamplerVariants`).
- *Isıtma (Warm)* (`Warming/`): `Warmer`, `scskiller_warm.exe`'yi çalıştırır, çıktısını ayrıştırır (parse), sürücü çökmelerini yeniden dener ve izole eder.
- *Satıcılar (Vendors)* (`Vendors/`): `NvidiaBackend`, `AmdBackend`, bunların kapasiteleri ve bir oyunun sürücü önbellek dosyalarını bulan ve öğrenen `NvidiaAppCache` / `AmdAppCache`.
- *Kaydedici yönetimi (Recorder management)* (`App/ScsKiller.cs`, `App/Recordings.cs`): Kurulum, damgalama (stamp), içe aktarma, anahtarlar dosyası.
- *Topluluk (Community)* (`App/Community.cs`, `App/Sharing.cs`, `Planning/HashOnly.cs`, `Planning/Rehydrate.cs`).
- *Güncellemeler ve içerik (Updates and content)* (`App/Updates.cs`, `App/ContentTrust.cs`, `App/Account.cs`, `App/RouteFailover.cs`).

**Kaydedici (Recorder)** (`proxy/proxy.cpp`, `d3d12.dll` olarak derlenir). Her dışa aktarımı (export) sistem DLL'sine iletir. Yalnızca izin verilen (admitted) bir başlatmada aygıtı kancalar (hook) ve oyunun ne yarattığını kaydeder. Bir DirectX 11 oyununda AMD üzerinde `d3d11.dll` olarak giriş düzenlerini (input layouts) kaydeder. Aksi takdirde, oyun klasörüne hiçbir şey yazmayan saf bir geçiştir.

**scskiller_warm.exe** (`proxy/warm.cpp`, `proxy/warm11.cpp`). Oyunun exe'si ile aynı adı taşıyan kendi kopyasını, kaydedici DLL'sini ve çalışma klasörünün veritabanlarını yeni bir klasöre yerleştirir ve çalıştırır.

**Selftest (Kendi kendini test etme)** (`proxy/selftest.cpp`, `probe11.cpp`, `layout11.cpp`). Kaydediciyi gerçek GPU üzerinde uçtan uca kontrol eder ve sürücü önbellek davranışlarını inceler.

**Sunucu (Server).** API'nin arkasında barındırılan hizmet. Hesapları ve Patreon oturumlarını tutar, erişim belirteçlerini (tokens) imzalar, bildirimlere (manifest) yüklemeleri alır ve kotalarla birlikte sunar.

**Komut Satırı (CLI)** (`src/SCSKiller.Cli/`). Komut satırından Core işlevleri (`scan`, `status`, `compile`, `record`, `cache`, `task`). `scskillerw` bunun penceresiz kopyasıdır ve sürücü güncelleme görevini `rewarm-stale --if-driver-changed` olarak çalıştırır (`ScheduledTask`).

## Temel Akışlar

### Derleme (Compile)

1. **Tarama (Scan).** Mağazalar oyunları listeler. `GameFiles.GameExe` asıl exe'yi seçer (Unreal için asla bir başlatıcı değil, `-Shipping.exe`). Bir oyunun motor ve anti-hile kararları, exe veya sürüm değişene kadar `scan.json` içinde önbelleğe alınır.
2. **Okuma (Read).** Motor okuyucusu `ShaderIndex`'i oluşturur. Arşiv anahtarları kullanıcıdan veya sabitlenmiş bir indirmeden gelir ve yerel kalır. 
3. **Kontrol (Check).** `Planner.Check`, `Unsupported` (Desteklenmiyor), `NeedsRecording` (Kayıt Gerekiyor) veya `Ready` (Hazır) durumunu verir.
4. **Hazırlık (Prepare).** Topluluk indirmesiyle birleştirilen `recording.db`, hash (karma) ile adlandırdığı shader'ları kurulumdan geri okur (`Rehydrate.Run`).
5. **Planlama (Plan).** `Planner.Build`, planlayıcı sürümü ve eşleşen kuralların karmasıyla damgalanmış `plan.bin` dosyasını yazar.
6. **Somutlaştırma (Materialize).** `Planner.Materialize`, planın shader baytlarını kurulumdan `work\` içine kopyalar.
7. **Isıtma (Warm).** `scskiller_warm.exe`, oyunun exe adı altında `work\`'ü yeniden oynatır, ardından `work\` silinir. Tam bir ısıtma işlemi `scskiller.warmed` dosyasını yazar. Sürücünün reddettiği kısım çok fazlaysa oyun "Kısmen derlendi" (PartlyCompiledShare) olarak işaretlenir. 
8. **İzleme (Track).** Isıtma girdileri `warm-<hash>.keys` dosyasına gider. Sürücü veya oyun güncellemesi, yeni bir planlayıcı veya yeni kurallar oyunu `Stale` (Eski/Güncellenmeli) yapar ve boşta (idle) yeniden ısıtma işlemi bunu algılar.

### Kaydedici (Recorder)

1. **Kurulum (Install)** Yalnızca uyumlu oyunlara kurulur: desteklenen, anti-hile olmayan, VAC olmayan, yükseltme olmadan yazılabilen, çalışmayan ve NVIDIA'da `d3d12.dll` dosyası Streamline ile birlikte gelmeyen oyunlar.
2. **Damgalama (Stamp)** Temiz bir tam anti-hile kontrolünden sonra gerçekleşir (`ScsKiller.Attest`). İndeksi olmayan bir yapı önce arka planda indekslenir ve o bitmeden başlayan bir başlatma her shader'ın baytlarını kaydeder.
3. **Kabul etme (Admit)** Oyunun ilk cihazında (device) bir kez kabul edilir.
4. **Yakalama (Capture)** Pipeline'ları, kök imzaları, ışın izleme (RT) durum nesnelerini ve eklentilerini yakalar. Gelen shader'lar hash ile kaydedilir, bu nedenle yalnızca çalışma zamanı (run-time) ve mod shader'ları bayt taşır.
5. **İçe aktarma (Import)** Exe'nin yanındaki `scskiller.db` bir gelen kutusudur, kayıt anahtarına göre `recording.db` ile birleştirilir ve ardından boşaltılır.
6. **Yükleme (Upload)** (İsteğe bağlı): Oyunun kendi kaydı anonim bir yükleme cihazı belirteci altında yalnızca hash biçiminde yüklenir.
7. **Değerlendirme (Judge)** (AMD): Tam bir ısıtmadan sonraki ilk başlatma, oyunun tamamen mi yoksa kısmen mi ısıtıldığına karar verir.

### Öncesi ve sonrası (Before and after)

`ScsKiller.BeforeAfter`. Anti-hile olmayan, kaydedilmiş bir D3D12 veya D3D11 oyununda: oyunun önbelleklerini temizler, kullanıcı oynar, tekrar temizler, o çalıştırmanın kaydıyla derler ve kullanıcı tekrar oynar. Oyun içi bir panel (`proxy/overlay.h`) kalan süreyi, bir ilerleme çubuğunu, son 5 saniyelik kare sürelerini (frame times) ve shader derleme takılmalarını gösterir.

### Topluluk indirmesi (Community download)

1. Her kurulum, hiçbir oyun bilgisi olmadan manifest'i (bildirim) getirir ve oyunlarını yerel olarak eşleştirir.
2. Bir destekçinin belirteci (`db` yetkisi), yapının birleştirilmiş kaydını `community.db` dosyasına indirir.
3. Paylaşılan ara katman paketleri, DLL sürümüne ve GPU satıcısına göre anahtarlanmış olarak her kurulum için ücretsizdir.
4. İndirilen her şey yalnızca hash biçimindedir ve yerel kurulumdan yeniden oluşturulur (rehydrate).

## Kurallar

**Kaydedici, bir oturum dışında asla bir anti-hile veya VAC oyunu içinde çalışmaz.** Bu katmanlar halinde zorlanır:
- *Tespit.* `GameFiles.DetectAntiCheat`, kurulumun herhangi bir yerindeki isimlere göre işaretçileri eşleştirir. Gizli bir anti-hile, exe adı veya mağaza kimliğine göre listelenir.
- *Kurulum* kopyalamadan önce, kopyalamadan sonra ve son adım olarak kontrol eder.
- *Kabul etme* (`proxy/proxy.cpp` içindeki `admitted()`), yalnızca `scskiller.ini` güncel exe için geçerli bir damga (stamp) taşıdığında ve hiçbir anti-hile modülü yüklenmediğinde kaydeder. Kullanıcının kendi riskiyle girdiği bir oturum, `risk=1` imzasıyla bu kuralı aşan tek yoldur.
- *Oturumlar (sadece uygulama):* Kaydedici, yalnızca kullanıcının o oyun için izin verdiği ve onayladığı bir oturumda anti-hileli bir oyuna girer (Çevrimdışı oturumlar dahil). Oyun kapandığında veya sistem yeniden başlatıldığında dosyalar anında silinir.

**Damga (The stamp).** `scskiller.ini` içindeki `exe_size=` ve `exe_time=`, tam bir anti-hile kontrolünden önce alınır ve `sig=` ile yalnızca temizse yazılır. Damga bozuk veya kullanıcı tarafından manuel olarak düzenlenmişse geçersiz sayılır.

**Oyun klasörleri.** Oyun klasörüne kaydedicinin dosyaları dışında hiçbir şey yazılmaz ve yalnızca oyun çalışmıyorken yazılır (`GameFolderWrite`). Oyun dosyaları salt okunur olarak açılır ve asla değiştirilmez.

**Oyun içeriği PC'de kalır.** Oyun shader baytları yalnızca `work\` içinde tutulur ve her ısıtmadan sonra silinir. Paylaşılan her şey yalnızca hash'ten ibarettir.

**Bir ısıtma işlemi ve oyunu asla aynı anda çalışmaz.** NVIDIA, aynı adlı ikinci bir sürece kendi önbellek dosyalarını verir, bu nedenle oyun çalışırken kuyruk ısıtmayı bekletir.

## Veri Konumları

| Nerede | Ne |
|---|---|
| `%LOCALAPPDATA%\SCSKiller\` | Ayarlar, tarama önbellekleri, kullanıcı klasörleri, kaydedici logları, uygulamanın log'u. |
| `...\games\<id>\` | Durum, plan dosyası, `recording.db`, çalışma ve derleme günlükleri, oyun için anahtarlar. |
| `...\games\<id>\sessions\` | Bir başlatmanın kare raporları. |
| `...\packs\`, `...\community\` | Ara katman paketleri, topluluk manifest'i ve paylaşılan paketler. |
| Oyunun exe'si yanında | `d3d12.dll` veya `d3d11.dll`, `scskiller.ini`, kayıt veri tabanları ve CSV raporu. |
| `%LOCALAPPDATA%\SCSKiller.App\` | Velopack kurulum klasörü. |

## Sürücü Önbellekleri (Driver caches)

### NVIDIA, D3D12
- **Anahtar (Key):** Exe dosya adı, büyük/küçük harf duyarsız (`CacheKeyedByExeName`). NVAPI shader uzantı yuvası anahtarın bir parçasıdır ve kaydedilir.
- **Ayrıntı düzeyi:** Sabit işlev durumu, biçimler ve girdi düzeni anahtarda yoktur.
- **Dosyalar:** `NVIDIA\DXCache\*.nvph`. SCSKiller yalnızca adlarını, boyutlarını ve hangi işlemlerin onları tuttuğunu okur, içeriklerini asla okumaz.
- **Ulaşılamayan (Unreached):** Shader hata ayıklama bilgisine sahip NVIDIA Aftermath, sürücünün her pipeline'ı başka bir anahtar altında derlemesine neden olur. Oyun Aftermath kullanıyorsa, SCSKiller da aynısını dener.

### AMD, D3D12
- **Anahtar (Key):** Exe adının UTF-16LE baytlarının FNV-1a-32 değeri, büyük/küçük harfe duyarlı.
- **Geçersiz Kılmalar (Overrides):** Sürücü uygulama profilleri anahtarı değiştirir. AGS aracılığıyla oluşturulan bir aygıt, uygulama adıyla anahtarlanır (`AmdAppCache.AgsKey`). 
- **Ayrıntı düzeyi:** Aşamaya göredir, durumdan bağımsız değildir. Bir VS-PS eşleştirmesi, biçim, MSAA veya harmanlama değişiklikleri yeniden derleme gerektirir.
- **Sınır (Cap):** Tüm DxcCache klasörü 16 GiB tutar. En az erişilen dosyalar çıkarılır.
- **Bütçe:** Bir oyunun planı, kaydedilen PSO'ları her zaman tutar, ardından sınır dahilinde önceliklendirir.

### Intel, D3D12
- **Anahtar:** Exe dosya adı, yoldan bağımsız. 
- **Sınır:** Exe adı başına 512 MiB (`IntelBackend.PerGameCap`).

### D3D11
Her satıcıda exe adına sabitlenir, shader başına önbelleğe alınır, ilk kullanımda derlenir.

### Işın İzleme (Ray tracing)

Her satıcı durum nesnelerini (state objects) exe adına göre önbelleğe alır ve tam bir tekrarda isabet (hit) elde eder (`VendorCaps.RtCacheGranularity`).

- **NVIDIA (`Collection`):** Koleksiyonlar kendi başlarına önbelleğe alınır ve ucuza bağlanır (link), bu nedenle planlayıcı her DXIL kütüphanesi için bir tane sentezler. Dışa aktarma (export) adları anahtarda (key) bulunmaz.
- **AMD (`WholeObject`):** Bağlanan tüm nesne anahtardır. Sadece kaydedilmiş nesneler ısıtılabilir (warmed).
- **Intel (`WholeObject`):** Koleksiyonlar kendi başlarına önbelleğe alınır, ancak düz durum nesneleri onlarla hiçbir şey paylaşmaz ve `AddToStateObject` bunları kullanmaz, bu nedenle yalnızca kaydedilmiş nesneler ısıtılır.

### Wine Altında NVIDIA (Proton, Linux)

Proton altındaki bir D3D12 oyunu, her PSO için bir Vulkan pipeline'ı oluşturan vkd3d-proton üzerinde çalışır; NVIDIA'nın Linux sürücüsü bunu önbelleğe alır.

- **Anahtar (Key):** Süreç görüntüsünün (process image) dosya adı, her oyunun her Windows süreci için Wine'ın `wine64-preloader`'ı ve önbelleği tutan süreç başına bir sayı: Bir süreç bir sonraki dosyayı alır. `__GL_SHADER_DISK_CACHE_APP_NAME` adı değiştirir, `..._READ_ONLY_APP_NAME` (`;` ile ayrılmış dosya adları) sürecin yalnızca okuduğu dosyaları ekler, `..._PATH` klasörü (var olması gerekir) taşır ve `..._SIZE` onu sınırlar: Bu sınırı geçtiğinde sürücü tüm dosyaları siler. Sürücü bunları Wine'ın başladığı Linux ortamından okur; bir Windows sürecinin ayarladığı değişken, onun başlattığı sürece ulaşmaz.
- **Ayrıntı Düzeyi (Granularity):** Aşama başına, durumdan bağımsız değil: Harmanlama etkinleştirmesi (blend enable) bir parça (fragment) shader'ını yeniden derler; formatlar ve derinlik (depth) formatı çoğunlukla derlemez. Dosyalardaki işlem (compute) pipeline'ları isabet (hit) sayılır.
- **vkd3d-proton'un kendi önbelleği**, DXIL->SPIR-V çevirisini tutar: `VKD3D_SHADER_CACHE_PATH` altında `vkd3d-proton.<exe name>.cache`. SPIR-V yapısına (build) bağlı olduğundan, bir ısıtma işlemi oyunun Proton'unda çalışır.
- **DXGI, DXVK'ye aittir**, sürücü sürümü de DXVK'nin kendisidir: Wine altında `GpuBackends`, ana bilgisayar sürücüsünün (host driver) sürümünü Vulkan'dan alır.
- Proton sarıcı betiği (wrapper script), oyunun prefix'i (dizini) içinde CLI'yi kendi Proton'unda çalıştırır ve derlemenin Wine oturumunu `proton-env --warm` değişkenleriyle başlatır: `work\` yanındaki oyunun `proton-cache\` klasörü ve ısıtmaya özgü bir uygulama adı. Oyunun başlatılması, `proton-env`'in kendi uygulama adını ve ısıtmanın her dosyasını salt okunur (`Wine.WarmFiles`: Isıtmanın numarası, önbelleği tutan Wine'ın başka ne olduğuna bağlıdır) olarak alır. Bir Steam oyunu için, önek ve Proton'u Steam'in dosyalarından alır ve başlatma seçeneklerinin değişkenleri, Steam'in belirlediği NVIDIA önbellek değişkenlerinin yerini alır. CLI, Steam'in verileri için aktif Steam istemcisinin klasörünü alır (`steam-client`: Çalışan istemcinin, yoksa `~/.steam/root`, Flatpak ve Snap altındaki tek istemci klasörü), asla bir kütüphane klasörünü almaz ve bundan emin olunamadığında hiçbir şey almaz.

## Dosya Formatları

**Kaydedici veritabanı (Recorder database)** (`scskiller.db`, `recording.db`, `scskiller_gen.db`, paketler). `u8 tag, u32 length, payload` formatında kayıtlar içerir; etiket (tag) ve yükün (payload) SHA-1'ine göre anahtarlanır. Kaydedici ve planlayıcı tarafından yazılır, ısıtıcı (warm) tarafından yeniden oynatılır. Etiketler: `proxy/proxy.cpp` başlığı, `write_so` konumundaki durum nesneleri. Okuyucu: `PsoDb`.

**Plan dosyası (Plan file)** (`plan.bin`). Şablonlar (Templates), üretilen kök imzalar ve shader'ları asla shader baytlarıyla değil SHA-1 ile isimlendiren öğelerdir. `Planner.Build` tarafından yazılır, somutlaştırma (materialize) aşamasında ve yeni pipeline sayımında okunur. Tür: `PlanFile` (`Planning/Planner.cs`).

**Anahtarlar dosyası (Keys file)** (`scskiller.keys`). Kaydedicinin yanına `Recordings` tarafından yazılır: Kaydedicinin ilkini atlaması ve ikincisini hash ile kaydetmesi için halihazırda kaydedilmiş hash'ler ve yapının (build) birlikte geldiği (ships) shader'lardır.

**Isıtılmış dosya (Warmed file)** (`scskiller.warmed`). Tam bir derlemeden sonra `Recordings.Warmed` tarafından yazılır, oyunun veri klasöründe tutulur ve anahtarlar dosyasının yanına kopyalanır: Derlemenin oluşturduğu pipeline'ların ve durum nesnelerinin sıralanmış anahtarları, ardından ısıtmanın `scskiller_failed.keys` içinde listelediği kısımlar çıkarılarak elde edilen sıralanmış (aşamalar + kök imza) demet (tuple) hash'leri.

### scskiller_warm.exe protokolü

Seçenekler (Options): `proxy/warm.cpp` başlığı. Standart çıktı (Stdout) yalnızca JSON satırlarından oluşur (`stage`, `start`, `progress`, `done`, `retry`, `error`); bu satırlar `Warming/Warmer.cs` içindeki `WarmEvent` tarafından ayrıştırılır. Oluşturmadığı öğeler (başarısız, iptal edilmiş veya çökme olarak atlanmış olanlar), kayıt anahtarına göre `scskiller_failed.keys` dosyasına gider. Çıkış kodu (Exit) 0 tamamlandığını veya durdurulduğunu, 1 hatayı, 3 ise yeniden denemeyi gösterir. Yeniden oynatma (Replay) dosya sırasını takip eder, bu yüzden durdurulan bir işlem `--start <done>` ile devam eder. Durdurma adlandırılmış bir olaydır, asla doğrudan işlemi öldürme (kill) değildir. Bellek bütçesi: `proxy/proxy.cpp` içinde `SCSKiller_WarmMemory`.

### scskiller_creates.csv

Kaydedici tarafından exe'nin yanına yazılır; oluşturma (create) başına bir satır (`t_ms,kind,known,tuple_known,ms,key,proxy_ms,tid,presents`) içerir. Başlatma başına `#session`, `#clock` ve `#end` işaretçileri (markers); kaydediciden `d3d11.dll` olarak `#api,d3d11`; her takas zinciri (swap chain) için `#swapchain,<w>,<h>,<DXGI_FORMAT>`; oyun içi panel çizildiğinde bir kez `#overlay,1` ve bir kare oluşturma (frame generation) modülü (DLSS-G'nin `nvngx_dlssg.dll`, XeSS) yüklendiğinde veya kare oluşturma kendi takas zincirini oluşturduğunda bir kez `#framegen,1`; sonrasında hiçbir kanca (hook) oyunun karelerini zamanlamıyorsa `#frames,stopped` gelir. `#session`, exe adını başlatıldığı gibi, AMD'nin anahtar oluşturduğu büyük/küçük harf durumuyla (case) tutar. Bir `scskiller.warmed` dosyası mevcutsa, `known` ve `tuple_known` son derlemenin o anahtarı veya demeti (tuple) oluşturduğu anlamına gelir; yoksa, `known` veritabanının veya anahtarlar dosyasının başlangıçta tuttuğu bir anahtardır. `SessionLog` tarafından okunur.

**Kareler dosyası (Frames file)** (`scskiller_frames.bin`). CSV'nin saati üzerinden `u32` kayıtları olarak son başlatmanın kare süreleri (frame times). Format `proxy/proxy.cpp` içindeki kare yazıcısındadır. Yükleme ve diğer takılmaları shader takılmasından ayıran `FrameLog` tarafından okunur.
**Etiketli kayıtlar (Tagged records)**, yazıcının asla kullanmadığı boşluk değerlerine yerleştirilir: En üstteki 4 bit 15 ve düşük 28 bit 268,435 (`(1 << 28) / 1000`, `FrameLog.TagEnd`) altındadır; çünkü bir boşluk yalnızca 2^28 µs veya üzeri için yazılır. Bir etiket bir `u32`'dir; etiketleri olmayan bir okuyucu bunu o kadar milisaniyelik bir boşluk olarak algılar. 0-14 etiketleri (`0xF0000000`-`0xF000000E`) o takas zincirini kare oluşturmaya ait olarak işaretler, sunumları oyunun işlediği (rendered) karelerdir; diğer tüm takas zincirleri ise görüntülenenlerdir. 15-268,434 etiketleri (`0xF000000F`-`0xF0041892`) yeni kayıt türleri için serbesttir, en düşük olanlar önceliklidir (küçük bir etiket, eski bir okuyucunun kaydırmasını (shift) küçük tutar); okuyucular bilmedikleri etiketleri atlar ve verilere ihtiyaç duyan bir kayıt bunu daha fazla etikette kodlar.

## Detaylar Nerede Bulunur?

- Önce kod: Yukarıdaki isimler kurallara götürür ve bunlardaki yorum satırları sebepleri ve eşikleri barındırır. `Contracts.cs` arayüzlerin (interfaces) haritasıdır.
- Her sunucu alışverişinin istemci (client) tarafı: `App/Community.cs`, `App/Sharing.cs`, `App/Account.cs`, `App/Updates.cs` ve `App/ContentTrust.cs`.
- İmzalı akışlar (feeds) ve içerik dosyaları: `tools/release-sign` (`verify`, `verify-content`). Kod imzalama: `CODE_SIGNING_POLICY.md`.
- Derleme (Building) ve test etme: `CONTRIBUTING.md`. Bir güvenlik açığı bildirme: `SECURITY.md`.
