// D3D11 derleme-anahtarı (compile-key) probu: Hangi giriş-düzeni alanlarının, ortak aşamaların ve çıktı-birleştirici (output-merger)
// durumunun sürücünün bir shader'ı yeniden derlemesine yol açtığı, bu derlemelerin sürücünün disk önbelleğine ulaşıp ulaşmadığı
// ve dolaylı bir sıfır-sayılı çizimin (zero-count indirect draw) (warm11.cpp'nin yayınladığı) derlenip derlenmediği incelenir. 
// Bağımsız: Tek dosya, sistem d3d11.dll'i, hiçbir SCSKiller kodu içermez.
//   cl /std:c++17 /O2 /EHsc layout11.cpp d3d11.lib dxgi.lib d3dcompiler.lib
// Parametre yok: Orkestratör (yönetici). Kendini yeni exe adlarıyla kopyalar (yeni bir ad = boş bir sürücü önbelleği),
// alt işlemleri (children) taze tohumlarla (seeds) 3 kez çalıştırır ve medyan değerleri yazdırır. Alt İşlemler (Children):
//   vs <tohum> [again]  ağır VS + basit PS: ilk çizim, ardından varyant (giriş alanı, topoloji, partner PS) başına bir çizim. "again" = aynı isimde ikinci bir süreç: varyantlar ters sırada.
//   ps <tohum> [again]  ağır PS + basit VS (giriş düzeni yok): işleme-hedefi formatları, harmanlama (blend), derinlik, MSAA, SRV formatları, örnekleyiciler, partner VS.
//   warm <tohum> <gen|exact|direct>  ağır VS, PS ve CS birer kez kullanılır: gen = warm11'in yolu (eleman başına float4, PS için doğrudan geçen (pass-through) VS, RGBA16F + D24S8, sıfır-sayılı dolaylı çizim); exact = oyunun düzeni ve PS'i, hala dolaylı; direct = gerçek bir çizim ile exact.
//   game <tohum> <sıra> "oyun": kendi düzeniyle ağır VS, ağır PS, ağır CS, her biri kendi başına (child_game) zamanlanır.
//   real <vs.bin> [again]  bir oyunun VS'i: giriş imzasından bir düzen (32 bit formatlar), ardından 16 veya 8 bit formatında her seferinde bir eleman.
//   geo <tohum> [again] diğer VS'lerin arkasında ve diğer PS'lerin önünde ağır bir GS ve ağır bir HS+DS çifti.
//   real-dir <dizin>    dizindeki her *.bin dosyası için orkestratör (dosya başına iki kez, yeni bir isim altında).
//   pspair <tohum> <warm|game> <l|t> <a|b> giriş düzeni olan veya olmayan bir VS'nin arkasındaki ağır PS (child_pspair).
//   only <vs|ps|geo|pspair|warmps|warm> sadece bir bölüm için orkestratör.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static ID3D11Device* dev;
static ID3D11DeviceContext* ctx;

template <class F> static double ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static ID3DBlob* compile(const std::string& src, const char* target) {
    ID3DBlob *code = nullptr, *err = nullptr;
    D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr, "main", target, 0, 0, &code, &err);
    if (err) printf("# fxc: %s\n", (const char*)err->GetBufferPointer()), err->Release();
    return code;
}

// Yayım + Temizleme (Flush) + bir olay sorgusunda bekleme: Sürücünün ilk çizimde tembel (lazy) bir şekilde yaptığı derlemeyi içerir.
static double timed(const std::function<void()>& issue) {
    ID3D11Query* q = nullptr;
    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    dev->CreateQuery(&qd, &q);
    double t = ms([&] {
        issue();
        ctx->End(q);
        ctx->Flush();
        while (ctx->GetData(q, nullptr, 0, 0) == S_FALSE) Sleep(0);
    });
    q->Release();
    return t;
}

// En yüksek ayrılmış VRAM'e sahip donanım bağdaştırıcısı (scskiller_warm'ın seçtiği gibi).
static bool make_device() {
    IDXGIFactory1* f = nullptr;
    IDXGIAdapter1 *best = nullptr, *a;
    DXGI_ADAPTER_DESC1 bd{}, d;
    CreateDXGIFactory1(IID_PPV_ARGS(&f));
    for (UINT i = 0; f && f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        a->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (!best || d.DedicatedVideoMemory > bd.DedicatedVideoMemory)) std::swap(best, a), bd = d;
        if (a) a->Release();
    }
    D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0}, got;
    if (best) D3D11CreateDevice(best, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, fl, 2, D3D11_SDK_VERSION, &dev, &got, &ctx), best->Release();
    if (f) f->Release();
    if (dev) printf("# secilen_adaptor %ls\n", bd.Description);
    return dev != nullptr;
}

template <class T> static void rel(T*& p) { if (p) p->Release(), p = nullptr; }

static ID3D11Buffer *g_cb, *g_vb, *g_args, *g_ub;
static ID3D11UnorderedAccessView* g_uav;
static ID3D11SamplerState* g_smp;

static void common() {
    std::vector<char> zero(65536);
    D3D11_SUBRESOURCE_DATA init = {zero.data()};
    D3D11_BUFFER_DESC cbd = {65536, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER};
    dev->CreateBuffer(&cbd, &init, &g_cb);
    D3D11_BUFFER_DESC vbd = {4096, D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER};
    dev->CreateBuffer(&vbd, &init, &g_vb);
    D3D11_BUFFER_DESC abd = {32, D3D11_USAGE_DEFAULT, 0, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS};
    dev->CreateBuffer(&abd, &init, &g_args);
    D3D11_BUFFER_DESC ubd = {256, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS};
    dev->CreateBuffer(&ubd, &init, &g_ub);
    D3D11_UNORDERED_ACCESS_VIEW_DESC uvd = {DXGI_FORMAT_R32_FLOAT, D3D11_UAV_DIMENSION_BUFFER};
    uvd.Buffer.NumElements = 64;
    if (g_ub) dev->CreateUnorderedAccessView(g_ub, &uvd, &g_uav);
    D3D11_SAMPLER_DESC sd = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP};
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, &g_smp);
    ID3D11Buffer* cbs[14];
    for (auto& b : cbs) b = g_cb;
    ctx->VSSetConstantBuffers(0, 14, cbs), ctx->PSSetConstantBuffers(0, 14, cbs), ctx->CSSetConstantBuffers(0, 14, cbs);
    D3D11_VIEWPORT vp = {0, 0, 16, 16, 0, 1};
    ctx->RSSetViewports(1, &vp);
}

static ID3D11Texture2D* tex2d(DXGI_FORMAT f, UINT bind, UINT samples = 1, UINT size = 16) {
    D3D11_TEXTURE2D_DESC td = {size, size, 1, 1, f, {samples, 0}, D3D11_USAGE_DEFAULT, bind};
    ID3D11Texture2D* t = nullptr;
    dev->CreateTexture2D(&td, nullptr, &t);
    return t;
}
static ID3D11RenderTargetView* make_rtv(DXGI_FORMAT f, UINT samples = 1) {
    ID3D11Texture2D* t = tex2d(f, D3D11_BIND_RENDER_TARGET, samples);
    ID3D11RenderTargetView* v = nullptr;
    if (t) dev->CreateRenderTargetView(t, nullptr, &v), t->Release();
    return v;
}
static ID3D11DepthStencilView* make_dsv(DXGI_FORMAT f, UINT samples = 1) {
    ID3D11Texture2D* t = tex2d(f, D3D11_BIND_DEPTH_STENCIL, samples);
    ID3D11DepthStencilView* v = nullptr;
    if (t) dev->CreateDepthStencilView(t, nullptr, &v), t->Release();
    return v;
}
static ID3D11ShaderResourceView* make_srv(DXGI_FORMAT f) {
    ID3D11Texture2D* t = tex2d(f, D3D11_BIND_SHADER_RESOURCE);
    ID3D11ShaderResourceView* v = nullptr;
    if (t) dev->CreateShaderResourceView(t, nullptr, &v), t->Release();
    return v;
}

// ---- shader'lar. `K` (tohum/seed), ağır olanları hiçbir sürücü önbelleğinin görmediği bytecode'a dönüştürür.
static std::string K(unsigned seed) { return std::to_string(seed) + ".0"; }
static std::string heavy_vs(unsigned s) {  // TANGENT bildirilir ancak asla okunmaz
    return "cbuffer C:register(b0){float4 kc;};"
           "struct I{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:COLOR0;float4 t:TANGENT0;};"
           "struct O{float4 pos:SV_Position;float4 a:TEXCOORD0;float4 b:TEXCOORD1;};"
           "O main(I i){O o;float3 pp=i.p+i.n;[unroll]for(int k=0;k<64;++k){pp=sin(pp*(" + K(s) + "+k))+cos(pp.yzx)*i.c.rgb;}"
           "o.pos=float4(pp,1)+kc;o.a=float4(pp*i.uv.x,i.uv.y);o.b=i.c*kc;return o;}";
}
static std::string heavy_ps(unsigned s) {
    return "Texture2D t0:register(t0);Texture2D t1:register(t1);Texture2D t2:register(t2);Texture2D t3:register(t3);"
           "SamplerState ss:register(s0);cbuffer C:register(b0){float4 kc;};"
           "float4 main(float4 pos:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
           "float4 c=t0.Sample(ss,uv)+t1.Sample(ss,uv*2)+t2.Sample(ss,uv*3)+t3.Sample(ss,uv*4);"
           "[unroll]for(int i=0;i<64;++i){c=sin(c*(" + K(s) + "+i))+pow(abs(c)+0.001,1.3);} return c+kc;}";
}
static std::string heavy_cs(unsigned s) {
    return "RWBuffer<float> b:register(u0);[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){"
           "float4 v=float4(id.x," + K(s) + ",2,3);[unroll]for(int i=0;i<160;++i){v=sin(v*(" + K(s) + "+i))+pow(abs(v.yzwx)+0.001,1.3);} b[id.x]=dot(v,1);}";
}
// Ağır VS'nin Partner PS'leri (index = VsVar::ps; 5 = PS yok).
static const char* kPartnerPs[] = {
    "float4 main(float4 p:SV_Position,float4 a:TEXCOORD0,float4 b:TEXCOORD1):SV_Target{return a+b;}",
    "float4 main(float4 p:SV_Position,float4 a:TEXCOORD0):SV_Target{return a.x;}",
    "float4 main(float4 p:SV_Position,nointerpolation float4 a:TEXCOORD0,nointerpolation float4 b:TEXCOORD1):SV_Target{return a+b;}",
    "float4 main(float4 p:SV_Position,float4 a:TEXCOORD0,float4 b:TEXCOORD1):SV_Target{return (a+b)*2;}",
    "float4 main():SV_Target{return 0;}",  // warm11'in null_ps'i
};
// Ağır PS'nin Partner VS'leri (index = PsVar::vs): giriş düzeni (input layout) yok.
static const char* kPartnerVs[] = {
    "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};O main(uint i:SV_VertexID){O o;o.uv=float2(i&1,i>>1);o.p=float4(o.uv*2-1,0.5,1);return o;}",
    "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;float4 c:COLOR0;};O main(uint i:SV_VertexID){O o;o.uv=float2(i>>1,i&1);o.p=float4(o.uv*4-1,0.5,1);o.c=i;return o;}",
    "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};O main(uint i:SV_VertexID){O o;o.uv=float2(i,i*0.5);o.p=float4(o.uv,0,1);return o;}",
};

// ---- Bir oyunun VS'si için düzen oluşturmaya yetecek kadar DXBC'nin giriş imzası (ISGN/ISG1)
static uint32_t u32(const std::string& s, size_t o) { uint32_t v = 0; if (o + 4 <= s.size()) memcpy(&v, s.data() + o, 4); return v; }
struct Sig { std::string name; uint32_t index, sv, type, reg; uint8_t mask, rw; };
static std::vector<Sig> isgn(const std::string& c) {
    std::vector<Sig> v;
    if (c.size() < 32 || c.compare(0, 4, "DXBC")) return v;
    for (uint32_t i = 0, n = u32(c, 28); i < n && i < 64; ++i) {
        size_t o = u32(c, 32 + 4 * i), len = u32(c, o + 4);
        bool g1 = !c.compare(o, 4, "ISG1");
        if (!g1 && c.compare(o, 4, "ISGN")) continue;
        std::string d = c.substr(o + 8, len);
        size_t size = g1 ? 32 : 24, lead = g1 ? 4 : 0;
        for (uint32_t e = 0, ne = u32(d, 0); e < ne && e < 64; ++e) {
            size_t b = u32(d, 4) + e * size + lead, nm = u32(d, b);
            if (b + size - lead > d.size() || nm >= d.size()) break;
            v.push_back({std::string(d.c_str() + nm), u32(d, b + 4), u32(d, b + 8), u32(d, b + 12), u32(d, b + 16), (uint8_t)d[b + 20], (uint8_t)d[b + 21]});
        }
    }
    return v;
}

// ---- VS varyantları
struct El { std::string sem; UINT idx; DXGI_FORMAT f; UINT slot, off; UINT step; };  // step 0 = tepe noktası (vertex) başına
struct VsVar { std::string name; std::vector<El> el; UINT stride[3] = {64, 16, 16}; UINT vboff = 0;
               D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; int ps = 0; };

static std::vector<El> base_layout() {
    return {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, 0},  {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, 0},    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, 0},
            {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, 0}};
}
static El& el(VsVar& v, const char* sem) { for (auto& e : v.el) if (_stricmp(e.sem.c_str(), sem) == 0) return e; return v.el[0]; }

static std::vector<VsVar> vs_variants() {
    std::vector<VsVar> r;
    auto add = [&](const char* name, const std::function<void(VsVar&)>& f) { VsVar v{name, base_layout()}; f(v); r.push_back(v); };
    add("base_again", [](VsVar&) {});  // aynı desc, yeni bir yerleşim nesnesi
    add("order", [](VsVar& v) { std::reverse(v.el.begin(), v.el.end()); });
    add("extra_elem", [](VsVar& v) { v.el.push_back({"BLENDWEIGHT", 0, DXGI_FORMAT_R32_FLOAT, 1, 0, 0}); });
    add("sem_case", [](VsVar& v) { v.el[0].sem = "position"; v.el[1].sem = "Normal"; });
    add("stride", [](VsVar& v) { v.stride[0] = 80; });
    add("vb_offset", [](VsVar& v) { v.vboff = 16; });
    add("off_read", [](VsVar& v) { el(v, "COLOR").off = 24; el(v, "TEXCOORD").off = 28; });
    add("off_unread", [](VsVar& v) { el(v, "TANGENT").off = 48; });
    add("fmt_unread", [](VsVar& v) { el(v, "TANGENT").f = DXGI_FORMAT_R16G16B16A16_FLOAT; });
    add("pos_f32x4", [](VsVar& v) { el(v, "POSITION").f = DXGI_FORMAT_R32G32B32A32_FLOAT; el(v, "POSITION").off = 48; });
    add("pos_f16x4", [](VsVar& v) { el(v, "POSITION").f = DXGI_FORMAT_R16G16B16A16_FLOAT; el(v, "POSITION").off = 52; });
    add("pos_snorm16x4", [](VsVar& v) { el(v, "POSITION").f = DXGI_FORMAT_R16G16B16A16_SNORM; el(v, "POSITION").off = 52; });
    add("nrm_unorm8x4", [](VsVar& v) { el(v, "NORMAL").f = DXGI_FORMAT_R8G8B8A8_UNORM; });
    add("nrm_snorm8x4", [](VsVar& v) { el(v, "NORMAL").f = DXGI_FORMAT_R8G8B8A8_SNORM; });
    add("nrm_rgb10a2", [](VsVar& v) { el(v, "NORMAL").f = DXGI_FORMAT_R10G10B10A2_UNORM; });
    add("nrm_f16x4", [](VsVar& v) { el(v, "NORMAL").f = DXGI_FORMAT_R16G16B16A16_FLOAT; el(v, "NORMAL").off = 52; });
    add("uv_f16x2", [](VsVar& v) { el(v, "TEXCOORD").f = DXGI_FORMAT_R16G16_FLOAT; });
    add("uv_unorm16x2", [](VsVar& v) { el(v, "TEXCOORD").f = DXGI_FORMAT_R16G16_UNORM; });
    add("uv_snorm16x2", [](VsVar& v) { el(v, "TEXCOORD").f = DXGI_FORMAT_R16G16_SNORM; });
    add("col_bgra8", [](VsVar& v) { el(v, "COLOR").f = DXGI_FORMAT_B8G8R8A8_UNORM; });
    add("col_f32x4", [](VsVar& v) { el(v, "COLOR").f = DXGI_FORMAT_R32G32B32A32_FLOAT; el(v, "COLOR").off = 48; });
    add("slot_read", [](VsVar& v) { el(v, "TEXCOORD").slot = 1; el(v, "TEXCOORD").off = 0; });
    add("slot_unread", [](VsVar& v) { el(v, "TANGENT").slot = 2; el(v, "TANGENT").off = 0; });
    add("slot_all1", [](VsVar& v) { for (auto& e : v.el) e.slot = 1; v.stride[1] = 64; });
    add("inst_read", [](VsVar& v) { el(v, "COLOR").slot = 1; el(v, "COLOR").off = 0; el(v, "COLOR").step = 1; });
    add("inst_rate2", [](VsVar& v) { el(v, "COLOR").slot = 1; el(v, "COLOR").off = 0; el(v, "COLOR").step = 2; });
    add("inst_unread", [](VsVar& v) { el(v, "TANGENT").slot = 2; el(v, "TANGENT").off = 0; el(v, "TANGENT").step = 1; });
    add("topo_strip", [](VsVar& v) { v.topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; });
    add("topo_line", [](VsVar& v) { v.topo = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; });
    add("topo_point", [](VsVar& v) { v.topo = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; });
    add("ps_reads_x", [](VsVar& v) { v.ps = 1; });
    add("ps_flat", [](VsVar& v) { v.ps = 2; });
    add("ps_other_same_reads", [](VsVar& v) { v.ps = 3; });
    add("ps_warm11_null", [](VsVar& v) { v.ps = 4; });
    add("ps_none", [](VsVar& v) { v.ps = 5; });
    add("base_end", [](VsVar&) {});
    return r;
}

static ID3D11InputLayout* make_layout(const std::vector<El>& el, ID3DBlob* vsb) {
    std::vector<D3D11_INPUT_ELEMENT_DESC> d;
    for (auto& e : el) d.push_back({e.sem.c_str(), e.idx, e.f, e.slot, e.off, e.step ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA, e.step});
    ID3D11InputLayout* il = nullptr;
    dev->CreateInputLayout(d.data(), (UINT)d.size(), vsb->GetBufferPointer(), vsb->GetBufferSize(), &il);
    return il;
}

static ID3D11RenderTargetView* g_rt;

// Bu varyantın düzeni ve durumuyla bir VS (+ps) çizimi; düzen (layout) oluşturulamazsa -1 döndürür.
static double draw_vs(ID3D11VertexShader* vs, ID3DBlob* vsb, ID3D11PixelShader* const* ps, const VsVar& v, bool indirect = false) {
    ID3D11InputLayout* il = make_layout(v.el, vsb);
    if (!il) return -1;
    ID3D11Buffer* vbs[3] = {g_vb, g_vb, g_vb};
    UINT off[3] = {v.vboff, 0, 0};
    ctx->IASetInputLayout(il);
    ctx->IASetVertexBuffers(0, 3, vbs, v.stride, off);
    ctx->IASetPrimitiveTopology(v.topo);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(v.ps < 5 ? ps[v.ps] : nullptr, nullptr, 0);
    ctx->OMSetRenderTargets(1, &g_rt, nullptr);
    double t = timed([&] { indirect ? ctx->DrawInstancedIndirect(g_args, 0) : ctx->DrawInstanced(3, 2, 0, 0); });
    il->Release();
    return t;
}

static void print_isgn(ID3DBlob* b) {
    printf("# isgn:");
    for (auto& s : isgn(std::string((const char*)b->GetBufferPointer(), b->GetBufferSize())))
        printf(" %s%u(r%u mask=%x read=%x)", s.name.c_str(), s.index, s.reg, s.mask, s.rw);
    printf("\n");
}

static int child_vs(unsigned seed, bool again) {
    if (!make_device()) return 1;
    common();
    g_rt = make_rtv(DXGI_FORMAT_R8G8B8A8_UNORM);
    ID3DBlob* vsb = compile(heavy_vs(seed), "vs_5_0");
    if (!vsb) return 1;
    print_isgn(vsb);
    ID3D11VertexShader* vs = nullptr;
    printf("VS create=%.3f\n", ms([&] { dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs); }));
    ID3D11PixelShader* ps[5] = {};
    for (int i = 0; i < 5; ++i)
        if (ID3DBlob* b = compile(kPartnerPs[i], "ps_5_0")) dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps[i]), b->Release();
    // Partner PS'lerin kendi derlemeleri; çöpe atılacak bir VS arkasında çalışır, böylece varyantın süresi ağır VS'nin süresi olur
    if (ID3DBlob* b = compile("struct O{float4 p:SV_Position;float4 a:TEXCOORD0;float4 b:TEXCOORD1;};O main(uint i:SV_VertexID){O o=(O)i;o.p=float4(i,0,0,1);return o;}", "vs_5_0")) {
        ID3D11VertexShader* t = nullptr;
        dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &t);
        ctx->IASetInputLayout(nullptr), ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST), ctx->VSSetShader(t, nullptr, 0);
        ctx->OMSetRenderTargets(1, &g_rt, nullptr);
        for (auto* p : ps) ctx->PSSetShader(p, nullptr, 0), timed([&] { ctx->Draw(3, 0); });
        rel(t), b->Release();
    }
    auto vars = vs_variants();
    if (again) std::reverse(vars.begin(), vars.end());
    else printf("VS v=base ms=%.3f\n", draw_vs(vs, vsb, ps, VsVar{"base", base_layout()}));
    for (auto& v : vars) printf("VS v=%s ms=%.3f\n", v.name.c_str(), draw_vs(vs, vsb, ps, v));
    return 0;
}

// ---- PS varyantları
struct PsVar { std::string name; DXGI_FORMAT rt = DXGI_FORMAT_R8G8B8A8_UNORM; UINT samples = 1; int nrt = 1; bool extra_rt = false;
               int blend = 0; UINT8 wmask = D3D11_COLOR_WRITE_ENABLE_ALL; bool a2c = false; DXGI_FORMAT ds = DXGI_FORMAT_UNKNOWN; bool stencil = false;
               DXGI_FORMAT srv = DXGI_FORMAT_R8G8B8A8_UNORM; int smp = 0; int vs = 0; };

static std::vector<PsVar> ps_variants() {
    std::vector<PsVar> r;
    auto add = [&](const char* name, const std::function<void(PsVar&)>& f) { PsVar v{name}; f(v); r.push_back(v); };
    add("base_again", [](PsVar&) {});
    add("rt_bgra8", [](PsVar& v) { v.rt = DXGI_FORMAT_B8G8R8A8_UNORM; });
    add("rt_srgb", [](PsVar& v) { v.rt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; });
    add("rt_rgba16f", [](PsVar& v) { v.rt = DXGI_FORMAT_R16G16B16A16_FLOAT; });
    add("rt_r11g11b10", [](PsVar& v) { v.rt = DXGI_FORMAT_R11G11B10_FLOAT; });
    add("rt_rgb10a2", [](PsVar& v) { v.rt = DXGI_FORMAT_R10G10B10A2_UNORM; });
    add("rt_rgba16unorm", [](PsVar& v) { v.rt = DXGI_FORMAT_R16G16B16A16_UNORM; });
    add("rt_rgba32f", [](PsVar& v) { v.rt = DXGI_FORMAT_R32G32B32A32_FLOAT; });
    add("rt_rg16f", [](PsVar& v) { v.rt = DXGI_FORMAT_R16G16_FLOAT; });
    add("rt_r32f", [](PsVar& v) { v.rt = DXGI_FORMAT_R32_FLOAT; });
    add("rt_r8", [](PsVar& v) { v.rt = DXGI_FORMAT_R8_UNORM; });
    add("rt_rg8", [](PsVar& v) { v.rt = DXGI_FORMAT_R8G8_UNORM; });
    add("rt_none", [](PsVar& v) { v.nrt = 0; });
    add("rt_extra_slot", [](PsVar& v) { v.extra_rt = true; });
    add("blend_alpha", [](PsVar& v) { v.blend = 1; });
    add("blend_add", [](PsVar& v) { v.blend = 2; });
    add("wmask_rgb", [](PsVar& v) { v.wmask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE; });
    add("wmask_0", [](PsVar& v) { v.wmask = 0; });
    add("alpha_to_coverage", [](PsVar& v) { v.a2c = true; });
    add("msaa4", [](PsVar& v) { v.samples = 4; });
    add("depth_d32", [](PsVar& v) { v.ds = DXGI_FORMAT_D32_FLOAT; });
    add("depth_d24s8_stencil", [](PsVar& v) { v.ds = DXGI_FORMAT_D24_UNORM_S8_UINT; v.stencil = true; });
    add("srv_rgba16f", [](PsVar& v) { v.srv = DXGI_FORMAT_R16G16B16A16_FLOAT; });
    add("srv_bc1", [](PsVar& v) { v.srv = DXGI_FORMAT_BC1_UNORM; });
    add("srv_r32f", [](PsVar& v) { v.srv = DXGI_FORMAT_R32_FLOAT; });
    add("smp_point", [](PsVar& v) { v.smp = 1; });
    add("smp_aniso", [](PsVar& v) { v.smp = 2; });
    add("vs_more_outputs", [](PsVar& v) { v.vs = 1; });
    add("vs_other_same_outputs", [](PsVar& v) { v.vs = 2; });
    add("base_end", [](PsVar&) {});
    return r;
}

static double draw_ps(ID3D11PixelShader* ps, ID3D11VertexShader* const* vss, const PsVar& v, bool indirect = false) {
    ID3D11RenderTargetView* rt[2] = {v.nrt ? make_rtv(v.rt, v.samples) : nullptr, v.extra_rt ? make_rtv(DXGI_FORMAT_R8G8B8A8_UNORM) : nullptr};
    if (v.nrt && !rt[0]) return -1;
    ID3D11DepthStencilView* dsv = v.ds ? make_dsv(v.ds, v.samples) : nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    if (v.ds) {
        D3D11_DEPTH_STENCIL_DESC d = {TRUE, D3D11_DEPTH_WRITE_MASK_ALL, D3D11_COMPARISON_LESS_EQUAL, v.stencil, 0xFF, 0xFF,
                                      {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS},
                                      {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS}};
        dev->CreateDepthStencilState(&d, &dss);
    }
    D3D11_BLEND_DESC bd = {v.a2c};
    bd.RenderTarget[0] = {v.blend != 0, v.blend == 1 ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_ONE, v.blend == 1 ? D3D11_BLEND_INV_SRC_ALPHA : v.blend ? D3D11_BLEND_ONE : D3D11_BLEND_ZERO,
                          D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, v.wmask};
    ID3D11BlendState* bs = nullptr;
    dev->CreateBlendState(&bd, &bs);
    ID3D11ShaderResourceView* srv = make_srv(v.srv);
    ID3D11ShaderResourceView* srvs[4] = {srv, srv, srv, srv};
    D3D11_SAMPLER_DESC sd = {v.smp == 1 ? D3D11_FILTER_MIN_MAG_MIP_POINT : v.smp == 2 ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                             D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0, 16};
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* smp = nullptr;
    dev->CreateSamplerState(&sd, &smp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vss[v.vs], nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 4, srvs);
    ctx->PSSetSamplers(0, 1, &smp);
    ctx->OMSetRenderTargets(v.extra_rt ? 2 : v.nrt, rt, dsv);
    ctx->OMSetDepthStencilState(dss, 1);
    ctx->OMSetBlendState(bs, nullptr, 0xFFFFFFFF);
    D3D11_VIEWPORT vp = {0, 0, 16, 16, 0, 1};
