// Kayıt öncesi ve sonrası çalıştırılan oyun içi panel, saf kısımları: proxy.cpp onu çizer, selftest.cpp onları kontrol eder.
// Panel CPU üzerinde bir tuvale (canvas) boyanır (SDR ekranının gösterdiği gibi sRGB baytları), arka arabelleğin (back buffer) 
// formatına dönüştürülür ve kopyalanır: shader, kök imza (root signature) veya PSO yoktur, bu nedenle kaydedicinin cihaz 
// kancalarından (device hooks) veya sürücü önbelleğine hiçbir şey gitmez.
#pragma once
#include <windows.h>
#include <dxgiformat.h>
#include <dxgicommon.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include "overlay_font.h"

namespace ov {

// Panelin ölçeği: 720p'de 1, 1080p'de 2, 1440p'de 3, 4K ve yukarısında 4 (yazı tipinin boyutları, arabellekler buna göre boyutlandırılır).
constexpr int kMaxScale = 4;
inline int scale_for(uint64_t height) { return height < 960 ? 1 : (int)std::min<uint64_t>(height / 480, kMaxScale); }
constexpr const ovfont::Size& font(int s) { return ovfont::kSizes[std::clamp(s, 1, kMaxScale) - 1]; }
constexpr const ovfont::Glyph* glyph(const ovfont::Size& f, char c) {
    for (int i = 0; i < f.n; ++i)
        if (f.glyphs[i].c == c) return &f.glyphs[i];
    return nullptr;  // bir boşluk veya yazı tipinin sahip olmadığı bir karakter: boş (blank)
}
constexpr long text_width(const char* t, int s) {
    const auto& f = font(s);
    long w = 0;
    for (; *t; ++t) w += *t == ' ' ? f.space : glyph(f, *t) ? glyph(f, *t)->adv : f.space;
    return w;
}

inline constexpr char kDone[] = "Bitti, oyunu kapatabilirsiniz";
inline constexpr char kStutter[] = "SHADER DERLEME TAKILMASI!";
// "SCSKiller öncesi 3:42": kalan saniyeler yukarı yuvarlanır, böylece 0:00 yalnızca süre dolduğunda gösterilir. b'ye yazılır.
inline const char* countdown(bool after, double left_s, char (&b)[48]) {
    long s = left_s <= 0 ? 0 : (long)std::ceil(left_s);
    snprintf(b, sizeof b, "SCSKiller %s %ld:%02ld", after ? "sonrasi" : "oncesi", s / 60, s % 60);
    return b;
}
inline std::string countdown(bool after, double left_s) {
    char b[48];
    return countdown(after, left_s, b);
}

// 100 ms'lik sütunlarda son 5 saniyenin kare süreleri (frame times), her biri onunla örtüşen en uzun kare; shader derleme takılmasının 
// (shader compilation stutter) örtüştüğü sütun işaretlenir.
struct Graph {
    static constexpr int kCols = 50;
    static constexpr double kColMs = 100;
    float ms[kCols] = {};
    bool shader[kCols] = {};
    long newest = -1;  // son karenin bitiş sütunu
    void advance(long b) {
        for (long k = std::max(newest + 1, b - kCols + 1); k <= b; ++k) ms[k % kCols] = 0, shader[k % kCols] = false;
        newest = std::max(newest, b);
    }
    template <class F> void each(double s, double e, F f) {
        for (long k = std::max((long)(s / kColMs), newest - kCols + 1); k <= (long)(e / kColMs) && k <= newest; ++k) f(k % kCols);
    }
    void frame(double s, double e) {
        if (e < 0 || s > e) return;
        advance((long)(e / kColMs));
        each(std::max(s, 0.0), e, [&](long i) { ms[i] = std::max(ms[i], float(e - s)); });
    }
    void mark(double s, double e) { each(std::max(s, 0.0), e, [&](long i) { shader[i] = true; }); }
};

// G x Y arka arabelleğinin (back buffer) sağ üst kısmındaki panel: bir metin satırı, ince bir çubuk, 50 ms çizgisiyle grafik, 
// ve (stutters) shader derleme takılması parlaması (flash) için bir satır. Konumlar panelin sol üstünden itibarendir.
struct Layout {
    int s = 1;
    long w = 0, h = 0, pad = 0, text_y = 0, track_y = 0, track_h = 0, graph_y = 0, graph_h = 0, flash_y = 0;
    RECT at = {};   // arabellek üzerinde, ona kırpılmış; boş: sol == sağ
    long sx = 0, sy = 0;  // at'in sol üst köşesinin paneldeki konumu
};
inline void layout(uint64_t bw, uint64_t bh, const char* widest, bool stutters, Layout& L) {
    int s = L.s = scale_for(bh);
    const auto& f = font(s);
    long gap = 3 * s, margin = 8 * s;
    L.pad = 4 * s;
    long tw = std::max({text_width(widest, s), text_width(kDone, s), stutters ? text_width(kStutter, s) : 0L});
    L.w = tw + 2 * L.pad;
    L.text_y = L.pad;
    L.track_y = L.text_y + f.height + gap, L.track_h = s;
    L.graph_y = L.track_y + L.track_h + gap, L.graph_h = 24 * s;
    L.flash_y = L.graph_y + L.graph_h + gap;
    L.h = (stutters ? L.flash_y + f.height : L.graph_y + L.graph_h) + L.pad;
    long r = (long)bw - margin, l = r - L.w, t = margin;
    L.at = {std::max(l, 0L), std::max(t, 0L), std::min(r, (long)bw), std::min(t + L.h, (long)bh)};
    if (L.at.right <= L.at.left || L.at.bottom <= L.at.top) L.at.right = L.at.left, L.at.bottom = L.at.top;
    L.sx = L.at.left - l, L.sy = L.at.top - t;
}

// Bir SDR ekranında gösterildiği gibi renkler, 0xAABBGGRR.
constexpr uint32_t rgb(uint32_t r, uint32_t g, uint32_t b) { return r | g << 8 | b << 16 | 0xFF000000u; }
inline constexpr uint32_t kPanel = rgb(5, 5, 5), kTrack = rgb(51, 51, 51), kFill = rgb(64, 166, 255), kText = rgb(255, 255, 255),
                          kGraphBg = rgb(22, 22, 22), kLine = rgb(150, 150, 150), kSlow = rgb(255, 184, 0), kShader = rgb(255, 64, 64);

// Kapsama alanı (coverage a) 0-255 üzerinden bg üzerinde fg, ekranın gösterdiği şekilde bayt başına.
inline uint32_t mix(uint32_t fg, uint32_t bg, uint32_t a) {
    uint32_t o = 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8) o |= (((fg >> sh & 255) * a + (bg >> sh & 255) * (255 - a) + 127) / 255) << sh;
    return o;
}
inline void fill(uint32_t* img, long pitch, long x0, long y0, long x1, long y1, uint32_t c) {
    for (long y = y0; y < y1; ++y) std::fill(img + y * pitch + x0, img + y * pitch + x1, c);
}
// Satırın üstü y'de, x'ten başlayan, altındaki katı bg (arkaplan) üzerinde c renginde t metni.
inline void text(uint32_t* img, long pitch, long x, long y, const char* t, int s, uint32_t c, uint32_t bg) {
    const auto& f = font(s);
    for (; *t; ++t) {
        const ovfont::Glyph* g = *t == ' ' ? nullptr : glyph(f, *t);
        if (!g) { x += f.space; continue; }
        for (int r = 0; r < g->h; ++r)
            for (int k = 0; k < g->w; ++k) {
                uint32_t i = g->at + r * ((g->w + 1) & ~1) + k, a = (ovfont::kPixels[i / 2] >> (i % 2 * 4) & 15) * 17;
                if (a) img[(y + g->y + r) * pitch + x + g->x + k] = mix(c, bg, a);
            }
        x += g->adv;
    }
}
// img (L.w x L.h, bir satırda 'pitch' kadar piksel) içindeki tüm panel: metin, frac kısmına kadar doldurulmuş çubuk, grafik, ve 
// flash (parlama) ise parlama çizgisi.
inline void paint(const Layout& L, const char* t, double frac, const Graph& g, bool flash, uint32_t* img, long pitch) {
    int s = L.s;
    long p = L.pad, iw = L.w - 2 * p;
    fill(img, pitch, 0, 0, L.w, L.h, kPanel);
    text(img, pitch, p, L.text_y, t, s, kText, kPanel);
    fill(img, pitch, p, L.track_y, p + iw, L.track_y + L.track_h, kTrack);
    fill(img, pitch, p, L.track_y, p + std::lround(iw * std::clamp(frac, 0.0, 1.0)), L.track_y + L.track_h, kFill);
    long gy = L.graph_y, gh = L.graph_h;
    fill(img, pitch, p, gy, p + iw, gy + gh, kGraphBg);
    for (int i = 0; i < Graph::kCols; ++i) {  // en eskisi solda, aşağıdan yukarıya 0-100 ms
        long k = g.newest - Graph::kCols + 1 + i;
        if (k < 0) continue;
        float ms = g.ms[k % Graph::kCols];
        long x0 = p + iw * i / Graph::kCols, x1 = p + iw * (i + 1) / Graph::kCols - (iw >= 3 * Graph::kCols);
        long bar = std::lround(gh * std::min(ms, 100.0f) / 100);
        uint32_t c = g.shader[k % Graph::kCols] ? kShader : ms >= 50 ? kSlow : kFill;
        if (bar > 0) fill(img, pitch, x0, gy + gh - bar, std::max(x1, x0 + 1), gy + gh, c);
    }
    long line = gy + gh / 2;  // 50 ms
    for (long x = p; x < p + iw; x += 2 * s) fill(img, pitch, x, line, std::min(x + s, p + iw), line + std::max(1, s / 2), kLine);
    if (flash) text(img, pitch, p, L.flash_y, kStutter, s, kShader, kPanel);
}

// Bir arka arabelleğin piksellerinin aldığı şey: SDR 8-bit (RGBA veya BGRA) veya 10-bit, HDR10 (PQ, BT.2020) veya scRGB (FP16, doğrusal).
enum Out { kNone, kRgba8, kBgra8, kRgb10, kPq10, kScRgb };
// cs_known (renk alanı biliniyor): Renk alanı bizim SetColorSpace1 kancamıza (hook) aittir, bu nedenle 10 bitlik bir arabelleğin 
// ne olduğu söylenebilir. FP16 bir takas zinciri scRGB'dir.
inline Out out_of(DXGI_FORMAT f, int cs, bool cs_known) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return kRgba8;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return kBgra8;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return !cs_known ? kNone : cs == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? kRgb10 : cs == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? kPq10 : kNone;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return cs == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 || cs == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? kScRgb : kNone;
    default: return kNone;
    }
}
inline bool drawn_format(DXGI_FORMAT f) { return out_of(f, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, true) != kNone; }
inline int bytes(Out o) { return o == kScRgb ? 8 : 4; }

// HDR'de SDR beyazı: BT.2408'in referans beyazı.
constexpr double kWhiteNits = 203;
struct Luts {
    float lin[256];       // doğrusal ışık (linear light) olarak bir sRGB baytı, SDR beyaz 1
    uint16_t ten[256];    // 10 bit SDR kodu olarak
    uint16_t half[256];   // scRGB (1 = 80 nit) FP16 olarak
    uint16_t pq[4096];    // doğrusal ışık 0-1 (SDR beyazı), 10 bit PQ kodu olarak
};
inline Luts make_luts() {
    Luts l;
    for (int v = 0; v < 256; ++v) {
        double c = v / 255.0, x = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        l.lin[v] = (float)x, l.ten[v] = (uint16_t)std::lround(c * 1023);
        l.half[v] = DirectX::PackedVector::XMConvertFloatToHalf((float)(x * kWhiteNits / 80));
    }
    for (int i = 0; i < 4096; ++i) {
        double y = std::pow(i / 4095.0 * kWhiteNits / 10000, 0.1593017578125);
        l.pq[i] = (uint16_t)std::lround(std::pow((0.8359375 + 18.8515625 * y) / (1 + 18.6875 * y), 78.84375) * 1023);
    }
    return l;
}
inline const Luts& luts() {  // herhangi bir sunumdan (present) önceki ilk çağrı onları oluşturur
    static const Luts l = make_luts();
    return l;
}
// src'den gelen w x h boyutlarındaki tuval (canvas) pikselleri, o formatında pikseller olarak dst'ye (dpitch baytlık satırlar).
inline void convert(const uint32_t* src, long spitch, long w, long h, Out o, uint8_t* dst, long dpitch) {
    const Luts& L = luts();
    for (long y = 0; y < h; ++y) {
        const uint32_t* s = src + y * spitch;
        uint8_t* d = dst + y * dpitch;
        if (o == kRgba8) { memcpy(d, s, w * 4); continue; }
        for (long x = 0; x < w; ++x) {
            uint32_t p = s[x], r = p & 255, g = p >> 8 & 255, b = p >> 16 & 255;
            if (o == kBgra8) ((uint32_t*)d)[x] = b | g << 8 | r << 16 | 0xFF000000u;
            else if (o == kRgb10) ((uint32_t*)d)[x] = L.ten[r] | L.ten[g] << 10 | L.ten[b] << 20 | 3u << 30;
            else if (o == kScRgb) ((uint64_t*)d)[x] = L.half[r] | (uint64_t)L.half[g] << 16 | (uint64_t)L.half[b] << 32 | (uint64_t)0x3C00 << 48;
            else {  // BT.709'dan BT.2020 ana renklerine, sonra PQ
                float R = L.lin[r], G = L.lin[g], B = L.lin[b];
                auto q = [&](float v) { return (uint32_t)L.pq[std::lround(std::clamp(v, 0.0f, 1.0f) * 4095)]; };
                ((uint32_t*)d)[x] = q(0.6274f * R + 0.3293f * G + 0.0433f * B) | q(0.0691f * R + 0.9195f * G + 0.0114f * B) << 10 |
                                    q(0.0164f * R + 0.0880f * G + 0.9056f * B) << 20 | 3u << 30;
            }
        }
    }
}

// Piksel cinsinden, layout()'un oluşturduğu şekliyle kMaxScale'daki panel: En geniş satır (bir geri sayımda en fazla 5 basamak vardır) ve her sıra.
constexpr long kMaxW = std::max({text_width(kDone, kMaxScale), text_width(kStutter, kMaxScale), text_width("SCSKiller oncesi 00:00", kMaxScale)}) + 2 * 4 * kMaxScale;
constexpr long kMaxH = 2 * 4 * kMaxScale + 2 * font(kMaxScale).height + 3 * 3 * kMaxScale + kMaxScale + 24 * kMaxScale;

// Bir kare oluşturmanın yaptığı present (sunum) kuyruğu (FSR 3'ün "AMD FSR PresentQueue", XeSS'nin "XefgInterpolationSwapChain::present_queue_").
inline bool fg_queue(const std::wstring& name) {
    std::wstring n = name;
    for (auto& c : n) c = towlower(c);
    for (const wchar_t* k : {L"fsr", L"xefg", L"interpolat", L"framegen", L"frame gen", L"dlssg"})
        if (n.find(k) != std::wstring::npos) return true;
    return false;
}
// Yüklendiğinde, bunların bulunması kare oluşturmanın istendiği anlamına gelir (DLSS-G'ninki, FSR 3 alternatifleri, XeSS'ninki).
inline const wchar_t* const kFgModules[] = {L"nvngx_dlssg.dll", L"dlssg_to_fsr3_amd_is_better.dll", L"libxess_fg.dll"};

// Present'lardan çizimi yapan iş parçacığına doğru, sırayla kare bitişleri. Present'lar her seferinde bir itme (push) yapar:
// başka birinin itme yaptığını gören veya kuyruk (ring) dolu (N kare boyunca hiçbir şey çizilmedi) bulan bir itme işlemi, 
// beklemek yerine karesini düşürür.
struct Feed {
    static constexpr uint64_t N = 1024;
    std::atomic<double> t[N];
    std::atomic<uint64_t> head{0}, tail{0};
    std::atomic_flag pushing;
    void push(double v) {
        if (pushing.test_and_set(std::memory_order_acquire)) return;
        uint64_t h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) < N) t[h % N].store(v, std::memory_order_relaxed), head.store(h + 1, std::memory_order_release);
        pushing.clear(std::memory_order_release);
    }
    template <class F> void drain(F f) {  // her seferinde tek iş parçacığı (thread)
        uint64_t h = head.load(std::memory_order_acquire), i = tail.load(std::memory_order_relaxed);
        for (; i < h; ++i) f(t[i % N].load(std::memory_order_relaxed));
        tail.store(i, std::memory_order_release);
    }
};

// FrameLog'un canlı anlık takılma nedenleri (hitch causes) için kaydedicinin en son gördüğü oluşturmalar (creates):
// Her seferinde tek iş parçacığı tarafından (kaydedicinin kilidi) yazılır, çizim tarafından biri olmadan okunur (yazılmakta olan bir yuva atlanır).
struct Creates {
    static constexpr uint32_t N = 2048;
    enum : uint8_t { kCompile = 1, kBlocks = 2, kPresents = 4 };
    struct Slot { std::atomic<uint32_t> seq{0}, tid{0}; std::atomic<double> end{0}, ms{0}; std::atomic<uint8_t> flags{0}; } s[N];
    std::atomic<uint32_t> head{0};
    void add(double end, double ms, uint32_t tid, uint8_t flags) {
        uint32_t i = head.load(std::memory_order_relaxed);
        Slot& x = s[i % N];
        uint32_t q = x.seq.load(std::memory_order_relaxed);
        x.seq.store(q + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        x.end.store(end, std::memory_order_relaxed), x.ms.store(ms, std::memory_order_relaxed);
        x.tid.store(tid, std::memory_order_relaxed), x.flags.store(flags, std::memory_order_relaxed);
        x.seq.store(q + 2, std::memory_order_release);
        head.store(i + 1, std::memory_order_release);
    }
    // s'den e'ye kadar oynanan, 50 ms veya daha uzun süren bir karenin FrameLog nedeni, içinde biten (bu yüzden her biri
    // en fazla bir kare için sayılır) oluşturmalar (creates) ile birlikte: Bir shader derleme takılması (yarısından fazlası için 
    // 100 ms veya daha fazla süren, içinde sayılan derlemeler veya bloke eden herhangi bir derleme) için true, bir yükleme 
    // (8 veya daha fazla iş parçacığında derleme, hiçbiri sunumcuya (presenter) ait değil, veya 100 oluşturma) veya başka bir
    // takılma için false. Kare'den 5 saniye önce biten oluşturmalara doğru, en yenisi ilk sırada.
    bool shader_stutter(double s, double e) const {
        double cold = 0;
        int n = 0, nt = 0;
        uint32_t tids[8];
        bool compile = false, blocks = false, presenter = false;
        for (uint32_t h = head.load(std::memory_order_acquire), k = 0; k < N && k < h; ++k) {
            const Slot& x = this->s[(h - 1 - k) % N];
            uint32_t q = x.seq.load(std::memory_order_acquire);
            if (!q || q & 1) continue;
            double end = x.end.load(std::memory_order_relaxed), ms = x.ms.load(std::memory_order_relaxed);
            uint32_t tid = x.tid.load(std::memory_order_relaxed);
            uint8_t f = x.flags.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (x.seq.load(std::memory_order_relaxed) != q) continue;
            if (end < s - 5000) break;  // sıranın biraz dışında günlüğe kaydedilir: bir oluşturma döndükten sonra kaydedicinin kilidini bekler
            if (end <= s || end > e) continue;
            ++n, presenter |= (f & kPresents) != 0;
            if (nt < 8 && std::find(tids, tids + nt, tid) == tids + nt) tids[nt++] = tid;
            if (!(f & kCompile)) continue;
            compile = true, blocks |= (f & kBlocks) != 0;
            if (ms >= 100) cold += end - std::max(end - ms, s);
        }
        if (compile && nt >= 8 && !presenter) return false;
        if (cold >= (e - s) / 2) return true;
        return n < 100 && blocks;
    }
};
// Satıriçi ışın izleme (RayQuery) yapan DXIL: SCSKiller'ın Dxbc.InlineRayTracing'in okuduğu gibi SFI0 parçasının özellik bayrağı.
inline bool inline_rt(const char* p, size_t n) {
    auto u32 = [&](size_t at) { uint32_t v; memcpy(&v, p + at, 4); return v; };
    if (n < 32 || memcmp(p, "DXBC", 4)) return false;
    for (uint32_t i = 0, parts = u32(28); i < parts && 32 + 4 * (size_t)i + 4 <= n; ++i) {
        size_t at = u32(32 + 4 * i);
        if (at + 16 <= n && !memcmp(p + at, "SFI0", 4) && u32(at + 4) >= 8) return (u32(at + 8) & 0x100000) != 0;
    }
    return false;
}

// 50 ms veya daha uzun süren kareler, oyunun başladığı bilindiğinde ve oluşturmaları eklendiğinde bir kez değerlendirilir
// (kWaitMs: bir oluşturma, kaydedicinin kilidi boşaldığında günlüğe kaydedilir). Bir shader derleme takılması kendi
// sütunlarını işaretler ve eğer yeniyse kFlashMs süresince kStutter parlar (flash).
struct Slow {
    static constexpr double kWaitMs = 1000, kFlashMs = 2000;
    static constexpr int kMax = 32;  // bir başlangıç (startup) sırasındaki yükleme kareleri de oyunun başladığı bilinene kadar burada bekler: en eskiler ilk gider
    struct F { double s, e; } f[kMax];
    int n = 0;
    double flash_until = -1;
    uint32_t found = 0;
    void frame(double s, double e) {
        if (e - s < 50) return;
        if (n == kMax) std::copy(f + 1, f + kMax, f), --n;
        f[n++] = {s, e};
    }
    double checked = -1e18;
    void check(const Creates& c, double now, double startup, Graph& g) {
        if (startup < 0 || now - checked < 100) return;  // en fazla saniyede on kez: her kontrol oluşturmaları (creates) tarar
        checked = now;
        for (int i = 0; i < n;)
            if (bool shader = f[i].e > startup && c.shader_stutter(f[i].s, f[i].e); shader || f[i].e <= startup || now > f[i].e + kWaitMs) {
                if (shader) {
                    g.mark(f[i].s, f[i].e), ++found;
                    if (now - f[i].e < kFlashMs) flash_until = now + kFlashMs;
                }
                std::copy(f + i + 1, f + n, f + i), --n;
            } else ++i;
    }
    bool flashing(double now) const { return now < flash_until; }
};

// FrameLog'un bir "öncesi ve sonrası" çalıştırması (before and after run) için oynanışı canlı olarak saydığı gibi: başlatmanın
// ilk sunumundan (present) itibaren, yüklemeler ve duraklamalar dahil her kare. shown() fonksiyonu, başlatma şimdi bitseydi 
// FrameLog'un bırakma (quitting) olarak değerlendireceği (son 10 saniyesi içinde başlayanlar) kareleri dışarıda bırakır; 
// bu nedenle uygulamanın o anda kare günlüğünden (frame log) okuduğu ve raporun sayacağı oynanış süresidir.
// Süreler kaydedicinin saatinde (csv'nin t_ms'si) ms cinsindendir. Her seferinde tek iş parçacığı (thread).
struct Clock {
    static constexpr double kQuitMs = 10000;  // FrameLog.QuitMs
    double startup = -1, last = -1, played = 0;  // startup: ilk present (sunum)
    double recent[16] = {};  // son 16 saniye, karelerin başlangıcının saniyesi başına ms cinsinden oynanış
    long recent_s[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    // t zamanında bir present. İlk olduğunda true döner: oynama oradan sayılır.
    bool frame(double t) {
        bool first = startup < 0;
        if (first) startup = t;
        else if (t > last) count(last, t - last);
        last = t;
        return first;
    }
    void count(double start, double ms) {
        played += ms;
        recently(start, ms);
    }
    void recently(double start, double ms) {
        long s = (long)(start / 1000);
        auto& at = recent_s[s & 15];
        if (s < at) return;  // son 16 saniyeden daha eski
        if (s > at) at = s, recent[s & 15] = 0;
        recent[s & 15] += ms;
    }
    double shown(double now) const {
        double v = played;
        for (int k = 0; k < 16; ++k)
            if (recent_s[k] >= 0 && (recent_s[k] + 1) * 1000.0 > now - kQuitMs) v -= recent[k];
        return std::max(v, 0.0);
    }
};

}  // namespace ov
