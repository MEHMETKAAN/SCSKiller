// D3D11 sürücü-shader-önbelleği ölçüm probu. SCSKiller shim'i dahil edilmemiştir: sürücünün kendi kendine neyi önbelleğe
// aldığını görmek için doğrudan sistem d3d11.dll'i ile konuşur.
//
// Alt işlem (Child) modları:
//   cold seed        - yeni (tohumlanmış) ağır VS/PS/CS derler, her Create'i zamanlar, ardından Flush + olay-
//                       sorgusu beklemesini içeren ilk çizimi (VS+PS) ve ilk dağıtımı (CS) zamanlar.
//   state seed       - aynı ağır VS/PS'i derler (zaten bir kez ısıtılmış, zamanlanmamış), ardından çeşitli 
//                       sabit işlevli durum varyantları (varsayılan/blend/rtformat/layout/depth/msaa) altında 
//                       İLK çizimi zamanlar.
//   pair1 vsSeed psSeed         - iki tamamen yeni shader ile (her ikisi de hiçbir yerde görülmemiş) bir kez çizer.
//   pair2 vsSeed psSeed vsSeed2 psSeed2 - vsSeed/psSeed, pair1'in bytecode'unu yeniden üretir (süreçler arası
//                       bu exe adı altında sürücü tarafından zaten görülmüştür); vsSeed2/psSeed2 yenidir, daha önce hiç görülmemiştir.
//                       Çizimler: görülmüş-VS+yeni-PS, yeni-VS+görülmüş-PS, her-ikisi-görülmüş (varsayılan düzen),
//                       her-ikisi-görülmüş (yeni düzen). Sürücünün shader aşaması başına mı yoksa VS+PS çifti başına mı önbelleklediği sorusunu yanıtlar.
//   compile <dosya.hlsl> <hedef> <cikti.bin> - Bir dosyayı D3DCompile eder ("main"), bytecode'u yazar.
//   first <vs.bin> <ps.bin> - verilen bytecode'un ilk çizimini zamanlar (POSITION float3 + TEXCOORD0 float2 girişli
//                       bir VS, en fazla 4 doku, 1 örnekleyici, 1 float4 cbuffer'lı bir PS). Soğukken ~17 ms, 
//                       sürücü önbelleği her ikisini de (scskiller_warm tarafından ısıtılmış) zaten tuttuğunda ~1 ms.
//   firsttess <vs.bin> <hs.bin> <ds.bin> <ps.bin> - bir hull ve domain shader aracılığıyla aynı çizim (3 noktalı yamalar).
//   firstps <ps.bin>   - bir PS'in tek başına ilk çizimini zamanlar (burada derlenen giriş düzeni olmayan bir VS'nin arkasında, önce çizilir).
// Parametre yok: orkestratör. Alt işlemi kendi altında ve kendi kopyaları altında (aynı/farklı exe adı,
// aynı/farklı klasör), her birini yeni tohumlarla 3 kez çalıştırır ve medyan değerlerini yazdırır.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "probe_util.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <functional>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Soğuk (cold) bir sürücü derlemesinin net bir şekilde ölçülebileceği kadar "ağır" shader'lar (açılmış (unrolled) transandantal döngüler, 4 doku).
// `tohum` (seed), gömülü sabitleri değiştirir, böylece bytecode yeni (fresh) olur; aynı tohumla iki çağrı 
// baytları tamamen aynı olan bytecode üretir (derleyici aynı metinde deterministiktir), süreçler arası "daha önce görülmüş" testleri bu şekilde çalışır.
static std::string heavy_vsrc(unsigned seed) {
    std::string k = std::to_string(seed) + ".0";
    return "cbuffer C:register(b0){float4 kc;};"
           "float4 main(float3 p:POSITION,float2 uv:TEXCOORD0,out float2 ouv:TEXCOORD0):SV_Position{"
           "float3 pp=p;[unroll]for(int i=0;i<32;++i){pp=sin(pp*(" + k + "+i))+cos(pp);}"
           "ouv=uv; return float4(pp,1)+kc;}";
}
static std::string heavy_psrc(unsigned seed) {
    std::string k = std::to_string(seed) + ".0";
    return "Texture2D t0:register(t0);Texture2D t1:register(t1);Texture2D t2:register(t2);Texture2D t3:register(t3);"
           "SamplerState ss:register(s0);cbuffer C:register(b0){float4 kc;};"
           "float4 main(float4 pos:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
           "float4 c=t0.Sample(ss,uv)+t1.Sample(ss,uv*2)+t2.Sample(ss,uv*3)+t3.Sample(ss,uv*4);"
           "[unroll]for(int i=0;i<48;++i){c=sin(c*(" + k + "+i))+pow(abs(c)+0.001,1.3);} return c+kc;}";
}
static std::string heavy_csrc(unsigned seed) {
    std::string k = std::to_string(seed) + ".0";
    return "RWBuffer<float> b:register(u0);[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){"
           "float v=" + k + ";[unroll]for(int i=0;i<300;++i){v=sin(v*1.001+i)+cos(v)*exp(-abs(v)*0.01)+sqrt(abs(v)+0.001);} b[id.x]=v;}";
}

// İş yayınlama + Temizleme (Flush) + bir olay sorgusunda beklemeyi zamanlar, böylece sonuç sadece CPU tarafındaki Create/Draw çağrısı 
// yükünü değil, ilk kullanımda tembel (lazily) olarak gerçekleşen herhangi bir sürücü tarafı derlemesini de içerir.
static double timed_gpu(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::function<void()>& issue) {
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

static ID3D11ShaderResourceView* make_texture(ID3D11Device* dev) {
    BYTE px[4 * 4 * 4] = {};
    D3D11_TEXTURE2D_DESC td = {4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE};
    D3D11_SUBRESOURCE_DATA sd = {px, 16, 0};
    ID3D11Texture2D* tex = nullptr;
    dev->CreateTexture2D(&td, &sd, &tex);
    ID3D11ShaderResourceView* srv = nullptr;
    if (tex) dev->CreateShaderResourceView(tex, nullptr, &srv), tex->Release();
    return srv;
}

static ID3D11Buffer* make_vb(ID3D11Device* dev, const void* data, UINT bytes) {
    D3D11_BUFFER_DESC bd = {bytes, D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER};
    D3D11_SUBRESOURCE_DATA sd = {data, 0, 0};
    ID3D11Buffer* b = nullptr;
    dev->CreateBuffer(&bd, &sd, &b);
    return b;
}

struct Variant { const wchar_t* name; DXGI_FORMAT rtv_fmt; UINT samples; bool blend; bool depth; bool split_layout; };

// Verilen sabit işlev durumu (fixed-function state) altında uçtan uca zamanlanmış bir çizim çağrısı (bkz. timed_gpu).
static double draw_variant(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                            ID3DBlob* vsblob, ID3D11Buffer* cb, ID3D11ShaderResourceView* srv, ID3D11SamplerState* smp,
                            const Variant& v, ID3D11HullShader* hs = nullptr, ID3D11DomainShader* dom = nullptr) {
    // Yalnızca "layout" varyantı için iki arabelleğe (buffer) bölünmüş Konumlar (Positions) ve UV'ler.
    float posuv[3][5] = {{-1, -1, 0, 0, 0}, {0, 1, 0, 0.5f, 1}, {1, -1, 0, 1, 0}};
    float pos[3][3], uv[3][2];
    for (int i = 0; i < 3; ++i) { memcpy(pos[i], posuv[i], 12); memcpy(uv[i], posuv[i] + 3, 8); }

    ID3D11InputLayout* il = nullptr;
    ID3D11Buffer *vb0 = nullptr, *vb1 = nullptr;
    UINT strides[2], offsets[2] = {0, 0};
    ID3D11Buffer* vbs[2];
    UINT nbufs;
    if (v.split_layout) {
        D3D11_INPUT_ELEMENT_DESC ie[2] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        dev->CreateInputLayout(ie, 2, vsblob->GetBufferPointer(), vsblob->GetBufferSize(), &il);
        vb0 = make_vb(dev, pos, sizeof pos);
        vb1 = make_vb(dev, uv, sizeof uv);
        vbs[0] = vb0; vbs[1] = vb1; strides[0] = 12; strides[1] = 8; nbufs = 2;
    } else {
        D3D11_INPUT_ELEMENT_DESC ie[2] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        dev->CreateInputLayout(ie, 2, vsblob->GetBufferPointer(), vsblob->GetBufferSize(), &il);
        vb0 = make_vb(dev, posuv, sizeof posuv);
        vbs[0] = vb0; strides[0] = 20; nbufs = 1;
    }

    D3D11_TEXTURE2D_DESC rtd = {64, 64, 1, 1, v.rtv_fmt, {v.samples, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET};
    ID3D11Texture2D* rt = nullptr;
    dev->CreateTexture2D(&rtd, nullptr, &rt);
    ID3D11RenderTargetView* rtv = nullptr;
    dev->CreateRenderTargetView(rt, nullptr, &rtv);

    ID3D11Texture2D* ds = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    if (v.depth) {
        D3D11_TEXTURE2D_DESC dd = {64, 64, 1, 1, DXGI_FORMAT_D32_FLOAT, {v.samples, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL};
        dev->CreateTexture2D(&dd, nullptr, &ds);
        dev->CreateDepthStencilView(ds, nullptr, &dsv);
        D3D11_DEPTH_STENCIL_DESC dsd = {TRUE, D3D11_DEPTH_WRITE_MASK_ALL, D3D11_COMPARISON_LESS_EQUAL};
        dev->CreateDepthStencilState(&dsd, &dss);
    }

    ID3D11BlendState* bs = nullptr;
    if (v.blend) {
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0] = {TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
                               D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
        dev->CreateBlendState(&bd, &bs);
    }

    D3D11_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->OMSetBlendState(bs, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(dss, 0);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(il);
    ctx->IASetPrimitiveTopology(hs ? D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, nbufs, vbs, strides, offsets);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->HSSetShader(hs, nullptr, 0);
    ctx->DSSetShader(dom, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ID3D11ShaderResourceView* srvs[4] = {srv, srv, srv, srv};
    ctx->PSSetShaderResources(0, 4, srvs);
    ctx->PSSetSamplers(0, 1, &smp);
    if (hs) ctx->HSSetConstantBuffers(0, 1, &cb), ctx->DSSetConstantBuffers(0, 1, &cb), ctx->DSSetShaderResources(0, 4, srvs), ctx->DSSetSamplers(0, 1, &smp);

    double t = timed_gpu(dev, ctx, [&] { ctx->Draw(3, 0); });

    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (bs) bs->Release();
    if (dss) dss->Release();
    if (dsv) dsv->Release();
    if (ds) ds->Release();
    rtv->Release(); rt->Release();
    il->Release();
    if (vb0) vb0->Release();
    if (vb1) vb1->Release();
    return t;
}

// Her çizim tarafından paylaşılan Doku/örnekleyici/cbuffer: Yalnızca shader'lar ve sabit-işlevli durumlar değişir.
struct Common { ID3D11ShaderResourceView* srv; ID3D11SamplerState* smp; ID3D11Buffer* cb; };
static Common make_common(ID3D11Device* dev) {
    Common c{};
    c.srv = make_texture(dev);
    D3D11_SAMPLER_DESC smd = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP};
    smd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&smd, &c.smp);
    float zero[4] = {};
    D3D11_BUFFER_DESC cbd = {16, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER};
    D3D11_SUBRESOURCE_DATA cbi = {zero, 0, 0};
    dev->CreateBuffer(&cbd, &cbi, &c.cb);
    return c;
}
// scskiller_warm'ın varsayılan adaptörü: en çok ayrılmış VRAM'e sahip donanımsal olanı
static ID3D11Device* make_device(ID3D11DeviceContext** ctx) {
    IDXGIFactory1* f = nullptr;
    IDXGIAdapter1 *best = nullptr, *a;
    DXGI_ADAPTER_DESC1 bd{}, d;
    CreateDXGIFactory1(IID_PPV_ARGS(&f));
    for (UINT i = 0; f && f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        a->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (!best || d.DedicatedVideoMemory > bd.DedicatedVideoMemory)) std::swap(best, a), bd = d;
        if (a) a->Release();
    }
    ID3D11Device* dev = nullptr;
    D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0}, got;
    if (best) D3D11CreateDevice(best, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &dev, &got, ctx), best->Release();
    if (f) f->Release();
    return dev;
}
static ID3D11VertexShader* make_vs(ID3D11Device* dev, ID3DBlob* b) {
    ID3D11VertexShader* s = nullptr;
    dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s);
    return s;
}
static ID3D11PixelShader* make_ps(ID3D11Device* dev, ID3DBlob* b) {
    ID3D11PixelShader* s = nullptr;
    dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s);
    return s;
}

static int child_cold(unsigned seed) {
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = make_device(&ctx);
    CHECK(dev);

    ID3DBlob* vsb = compile(heavy_vsrc(seed), "vs_5_0");
    ID3DBlob* psb = compile(heavy_psrc(seed), "ps_5_0");
    ID3DBlob* csb = compile(heavy_csrc(seed), "cs_5_0");
    CHECK(vsb && psb && csb);

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11ComputeShader* cs = nullptr;
    double t_vs = ms([&] { vs = make_vs(dev, vsb); });
    double t_ps = ms([&] { ps = make_ps(dev, psb); });
    double t_cs = ms([&] { dev->CreateComputeShader(csb->GetBufferPointer(), csb->GetBufferSize(), nullptr, &cs); });
    CHECK(vs && ps && cs);

    Common c = make_common(dev);
    Variant def = {L"default", DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, false, false};
    double t_draw = draw_variant(dev, ctx, vs, ps, vsb, c.cb, c.srv, c.smp, def);

    UINT zero32[64] = {};
    D3D11_BUFFER_DESC ubd = {64 * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS};
    D3D11_SUBRESOURCE_DATA ubi = {zero32, 0, 0};
    ID3D11Buffer* ub = nullptr;
    dev->CreateBuffer(&ubd, &ubi, &ub);
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {DXGI_FORMAT_R32_FLOAT, D3D11_UAV_DIMENSION_BUFFER};
    uavd.Buffer.NumElements = 64;
    ID3D11UnorderedAccessView* uav = nullptr;
    dev->CreateUnorderedAccessView(ub, &uavd, &uav);
    ctx->CSSetShader(cs, nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    double t_disp = timed_gpu(dev, ctx, [&] { ctx->Dispatch(1, 1, 1); });

    printf("COLD seed=%u create_vs=%.3f create_ps=%.3f create_cs=%.3f draw=%.3f dispatch=%.3f\n", seed, t_vs, t_ps, t_cs, t_draw, t_disp);
    return 0;
}

static int child_state(unsigned seed) {
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = make_device(&ctx);
    CHECK(dev);

    ID3DBlob* vsb = compile(heavy_vsrc(seed), "vs_5_0");
    ID3DBlob* psb = compile(heavy_psrc(seed), "ps_5_0");
    CHECK(vsb && psb);
    ID3D11VertexShader* vs = make_vs(dev, vsb);
    ID3D11PixelShader* ps = make_ps(dev, psb);
    CHECK(vs && ps);

    Common c = make_common(dev);
    ID3D11ShaderResourceView* srv = c.srv;
    ID3D11SamplerState* smp = c.smp;
    ID3D11Buffer* cb = c.cb;

    Variant variants[] = {
        {L"default", DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, false, false},  // zamanlanmamış-eşdeğer temel çizgi (baseline), bu da gösterilir
        {L"blend",   DXGI_FORMAT_R8G8B8A8_UNORM, 1, true,  false, false},
        {L"rtformat",DXGI_FORMAT_R16G16B16A16_FLOAT, 1, false, false, false},
        {L"layout",  DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, false, true},
        {L"depth",   DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, true,  false},
        {L"msaa",    DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, false, false},
    };
    // VS/PS bytecode seviyesi derlemesinin (eğer tembelse) bedeli zaten ödenmiş olsun diye önce zamanlanmamış bir ısınma (warm) çizimi.
    draw_variant(dev, ctx, vs, ps, vsb, cb, srv, smp, variants[0]);
    for (auto& v : variants) {
        double t = draw_variant(dev, ctx, vs, ps, vsb, cb, srv, smp, v);
        printf("STATE seed=%u variant=%ls draw=%.3f\n", seed, v.name, t);
    }
    return 0;
}

// shader-başına veya çift-başına (per-shader-vs-per-pair) testinin 1. Süreci: iki tamamen yeni (fresh) shader ile bir kez çizin.
static int child_pair1(unsigned vs_seed, unsigned ps_seed) {
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = make_device(&ctx);
    CHECK(dev);
    ID3DBlob* vsb = compile(heavy_vsrc(vs_seed), "vs_5_0");
    ID3DBlob* psb = compile(heavy_psrc(ps_seed), "ps_5_0");
    CHECK(vsb && psb);
    ID3D11VertexShader* vs = make_vs(dev, vsb);
    ID3D11PixelShader* ps = make_ps(dev, psb);
    CHECK(vs && ps);
    Common c = make_common(dev);
    Variant def = {L"default", DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, false, false};
    double t = draw_variant(dev, ctx, vs, ps, vsb, c.cb, c.srv, c.smp, def);
    printf("PAIR tag=both_fresh vs_seed=%u ps_seed=%u draw=%.3f\n", vs_seed, ps_seed, t);
    return 0;
}

// Süreç 2: vs_seed/ps_seed, 1. sürecin bytecode'unu yeniden üretir (bayt-özdeşi, yani sürücü tarafından bu exe adı altında
// süreçler arası zaten görülmüştür); vs_seed2/ps_seed2 yenidir, daha önce hiç görülmemiştir.
static int child_pair2(unsigned vs_seed, unsigned ps_seed, unsigned vs_seed2, unsigned ps_seed2) {
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = make_device(&ctx);
    CHECK(dev);
    ID3DBlob* vsb_seen = compile(heavy_vsrc(vs_seed), "vs_5_0");
    ID3DBlob* psb_seen = compile(heavy_psrc(ps_seed), "ps_5_0");
    ID3DBlob* vsb_fresh = compile(heavy_vsrc(vs_seed2), "vs_5_0");
    ID3DBlob* psb_fresh = compile(heavy_psrc(ps_seed2), "ps_5_0");
    CHECK(vsb_seen && psb_seen && vsb_fresh && psb_fresh);
    ID3D11VertexShader* vs_seen = make_vs(dev, vsb_seen);
    ID3D11PixelShader* ps_seen = make_ps(dev, psb_seen);
    ID3D11VertexShader* vs_fresh = make_vs(dev, vsb_fresh);
    ID3D11PixelShader* ps_fresh = make_ps(dev, psb_fresh);
    CHECK(vs_seen && ps_seen && vs_fresh && ps_fresh);
    Common c = make_common(dev);
    Variant def = {L"default", DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, false, false};
    Variant layout = {L"layout", DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, false, true};

    double t1 = draw_variant(dev, ctx, vs_seen, ps_fresh, vsb_seen, c.cb, c.srv, c.smp, def);
    double t2 = draw_variant(dev, ctx, vs_fresh, ps_seen, vsb_fresh, c.cb, c.srv, c.smp, def);
    double t3 = draw_variant(dev, ctx, vs_seen, ps_seen, vsb_seen, c.cb, c.srv, c.smp, def);
    double t4 = draw_variant(dev, ctx, vs_seen, ps_seen, vsb_seen, c.cb, c.srv, c.smp, layout);
    printf("PAIR tag=vs_seen_ps_fresh vs_seed=%u ps_seed=%u draw=%.3f\n", vs_seed, ps_seed2, t1);
    printf("PAIR tag=vs_fresh_ps_seen vs_seed=%u ps_seed=%u draw=%.3f\n", vs_seed2, ps_seed, t2);
    printf("PAIR tag=both_seen vs_seed=%u ps_seed=%u draw=%.3f\n", vs_seed, ps_seed, t3);
    printf("PAIR tag=both_seen_layout vs_seed=%u ps_seed=%u draw=%.3f\n", vs_seed, ps_seed, t4);
    return 0;
}

static std::string read_file(const wchar_t* path) {
    std::string s;
    if (FILE* f = _wfopen(path, L"rb")) {
        char buf[65536];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f));) s.append(buf, n);
        fclose(f);
    }
    return s;
}

static int child_compile(const wchar_t* src, const wchar_t* target, const wchar_t* out) {
    char t[16];
    WideCharToMultiByte(CP_ACP, 0, target, -1, t, sizeof t, nullptr, nullptr);
    ID3DBlob* b = compile(read_file(src), t);
    FILE* f = b ? _wfopen(out, L"wb") : nullptr;
    CHECK(f && fwrite(b->GetBufferPointer(), 1, b->GetBufferSize(), f) == b->GetBufferSize());
    fclose(f);
    return 0;
}

static int child_first(const wchar_t* vs_path, const wchar_t* ps_path) {
    ID3DBlob *vsb = nullptr, *psb = nullptr;
    std::string v = read_file(vs_path), p = read_file(ps_path);
    CHECK(!v.empty() && !p.empty() && SUCCEEDED(D3DCreateBlob(v.size(), &vsb)) && SUCCEEDED(D3DCreateBlob(p.size(), &psb)));
    memcpy(vsb->GetBufferPointer(), v.data(), v.size());
    memcpy(psb->GetBufferPointer(), p.data(), p.size());
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = make_device(&ctx);
    CHECK(dev);
    ID3D11VertexShader* vs = make_vs(dev, vsb);
    ID3D11PixelShader* ps = make_ps(dev, psb);
    CHECK(vs && ps);
    Common c = make_common(dev);
    Variant def = {L"default", DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, false, false};
    printf("FIRST draw=%.3f\n", draw_variant(dev, ctx, vs, ps, vsb, c.cb, c.srv, c.smp, def));
    return 0;
}

// firstps: Sadece PS, burada derlenen giriş düzeni (input layout) olmayan bir VS'nin arkasında (SV_Position + TEXCOORD0 çıkışı);
// çiziminden hemen önce oluşturulur: AMD, oluşturmadan (create) itibaren arka planda derler.
static int child_firstps(const wchar_t* ps_path) {
    std::string p = read_file(ps_path);
    ID3DBlob* vsb = compile("struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};O main(uint i:SV_VertexID){O o;o.uv=float2(i&1,i>>1);o.p=float4(o.uv*2-1,0.5,1);return o;}", "vs_5_0");
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = make_device(&ctx);
    CHECK(dev && vsb && !p.empty());
    ID3D11VertexShader* vs = make_vs(dev, vsb);
    Common c = make_common(dev);
    ID3D11RenderTargetView* rtv = nullptr;
    D3D11_TEXTURE2D_DESC rtd = {64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET};
    ID3D11Texture2D* rt = nullptr;
    dev->CreateTexture2D(&rtd, nullptr, &rt);
    dev->CreateRenderTargetView(rt, nullptr, &rtv);
    D3D11_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ID3D11ShaderResourceView* srvs[4] = {c.srv, c.srv, c.srv, c.srv};
    ctx->PSSetShaderResources(0, 4, srvs);
    ctx->PSSetSamplers(0, 1, &c.smp);
    ctx->PSSetConstantBuffers(0, 1, &c.cb);
    timed_gpu(dev, ctx, [&] { ctx->Draw(3, 0); });  // VS'nin kendi derlemesi
    ID3D11PixelShader* ps = nullptr;
    dev->CreatePixelShader(p.data(), p.size(), nullptr, &ps);
    CHECK(vs && ps);
    ctx->PSSetShader(ps, nullptr, 0);
    printf("FIRST draw=%.3f\n", timed_gpu(dev, ctx, [&] { ctx->Draw(3, 0); }));
    return 0;
}

// firsttess: Bir hull ve domain shader ile aynısı (3-kontrol-noktalı yamalar; HS/DS'de b0, DS'de ayrıca t0-t3 + s0).
static int child_firsttess(const wchar_t* vs_path, const wchar_t* hs_path, const wchar_t* ds_path, const wchar_t* ps_path) {
    std::string b[4] = {read_file(vs_path), read_file(hs_path), read_file(ds_path), read_file(ps_path)};
    ID3DBlob* vsb = nullptr;
    CHECK(!b[0].empty() && !b[1].empty() && !b[2].empty() && !b[3].empty() && SUCCEEDED(D3DCreateBlob(b[0].size(), &vsb)));
    memcpy(vsb->GetBufferPointer(), b[0].data(), b[0].size());
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = make_device(&ctx);
    CHECK(dev);
    ID3D11VertexShader* vs = make_vs(dev, vsb);
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    ID3D11PixelShader* ps = nullptr;
    dev->CreateHullShader(b[1].data(), b[1].size(), nullptr, &hs);
    dev->CreateDomainShader(b[2].data(), b[2].size(), nullptr, &ds);
    dev->CreatePixelShader(b[3].data(), b[3].size(), nullptr, &ps);
    CHECK(vs && hs && ds && ps);
    Common c = make_common(dev);
    Variant def = {L"default", DXGI_FORMAT_R8G8B8A8_UNORM, 1, false, false, false};
    printf("FIRST draw=%.3f\n", draw_variant(dev, ctx, vs, ps, vsb, c.cb, c.srv, c.smp, def, hs, ds));
    return 0;
}

static std::string run_capture(const std::wstring& exe, const std::wstring& args) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE r = nullptr, w = nullptr;
    if (!CreatePipe(&r, &w, &sa, 0)) return {};
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = w;
    si.hStdError = w;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::string out;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(w);
        w = nullptr;
        char buf[4096];
        DWORD n;
        while (ReadFile(r, buf, sizeof buf, &n, nullptr) && n) out.append(buf, n);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (w) CloseHandle(w);
    CloseHandle(r);
    return out;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

static double parse_after(const std::string& line, const char* key) {
    size_t p = line.find(key);
    return p == std::string::npos ? -1.0 : atof(line.c_str() + p + strlen(key));
}

static void parse_cold(const std::string& out, double* create_vs, double* create_ps, double* create_cs, double* draw, double* dispatch) {
    *create_vs = parse_after(out, "create_vs=");
    *create_ps = parse_after(out, "create_ps=");
    *create_cs = parse_after(out, "create_cs=");
    *draw = parse_after(out, "draw=");
    *dispatch = parse_after(out, "dispatch=");
}

static void parse_state(const std::string& out, std::vector<std::pair<std::string, double>>& variants) {
    size_t pos = 0;
    while ((pos = out.find("variant=", pos)) != std::string::npos) {
        size_t nstart = pos + 8, nend = out.find(' ', nstart);
        std::string name = out.substr(nstart, nend - nstart);
        double t = parse_after(out.substr(nend), "draw=");
        variants.emplace_back(name, t);
        pos = nend;
    }
}

// Her "PAIR tag=<name> ... draw=<ms>" satırı için bir giriş (entry).
static void parse_pair(const std::string& out, std::vector<std::pair<std::string, double>>& tags) {
    size_t pos = 0;
    while ((pos = out.find("tag=", pos)) != std::string::npos) {
        size_t nstart = pos + 4, nend = out.find(' ', nstart);
        std::string name = out.substr(nstart, nend - nstart);
        double t = parse_after(out.substr(nend), "draw=");
        tags.emplace_back(name, t);
        pos = nend;
    }
}

int wmain(int argc, wchar_t** argv) {
    std::wstring mode = argc > 1 ? argv[1] : L"";
    if (mode == L"pair1") return argc == 4 ? child_pair1((unsigned)_wtoi(argv[2]), (unsigned)_wtoi(argv[3])) : 1;
    if (mode == L"pair2")
        return argc == 6 ? child_pair2((unsigned)_wtoi(argv[2]), (unsigned)_wtoi(argv[3]), (unsigned)_wtoi(argv[4]), (unsigned)_wtoi(argv[5])) : 1;
    if (argc > 2) {
        if (mode == L"compile" && argc == 5) return child_compile(argv[2], argv[3], argv[4]);
        if (mode == L"first" && argc == 4) return child_first(argv[2], argv[3]);
        if (mode == L"firstps" && argc == 3) return child_firstps(argv[2]);
        if (mode == L"firsttess" && argc == 6) return child_firsttess(argv[2], argv[3], argv[4], argv[5]);
        unsigned seed = (unsigned)_wtoi(argv[2]);
        return mode == L"state" ? child_state(seed) : child_cold(seed);
    }

    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring self = p;
    // her kopya bu çalıştırmanın (run) yeni bir klasörüne gider, sonunda içindeki her şeyle birlikte kaldırılır
    std::wstring dir = self.substr(0, self.find_last_of(L'\\') + 1) + L"probe11-" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    if (!CreateDirectoryW(dir.c_str(), nullptr)) return printf("yeni bir klasor %ls olusturulamadi\n", dir.c_str()), 1;
    std::wstring exeA = dir + L"probe11.exe";          // orijinal isim
    std::wstring exeC = dir + L"probe11_altname.exe";  // farkli isim, ayni klasor
    std::wstring subdir = dir + L"probe11_sub\\";
    CreateDirectoryW(subdir.c_str(), nullptr);
    std::wstring exeB = subdir + L"probe11.exe";        // ayni isim, farkli klasor
    CopyFileW(self.c_str(), exeA.c_str(), TRUE);
    CopyFileW(self.c_str(), exeC.c_str(), TRUE);
    CopyFileW(self.c_str(), exeB.c_str(), TRUE);

    unsigned base = GetTickCount() % 1000000;

    printf("=== S1 (Q1): surecler arasi (cross-process) onbellek, ayni exe (surec1 soguk (cold) vs surec2 ayni-isim-isabeti) ===\n");
    std::vector<double> c1_vs, c1_ps, c1_cs, c1_draw, c1_disp, c2_vs, c2_ps, c2_cs, c2_draw, c2_disp;
    for (int i = 0; i < 3; ++i) {
        unsigned seed = base + i * 7 + 1;
        std::wstring args = L"cold " + std::to_wstring(seed);
        auto o1 = run_capture(exeA, args);
        auto o2 = run_capture(exeA, args);
        double v1, p1, c1, d1, s1, v2, p2, c2, d2, s2;
        parse_cold(o1, &v1, &p1, &c1, &d1, &s1);
        parse_cold(o2, &v2, &p2, &c2, &d2, &s2);
        printf("  tohum(seed)=%u  surec(proc)1: vs=%.2f ps=%.2f cs=%.2f draw=%.2f disp=%.2f  |  surec(proc)2: vs=%.2f ps=%.2f cs=%.2f draw=%.2f disp=%.2f\n",
               seed, v1, p1, c1, d1, s1, v2, p2, c2, d2, s2);
        c1_vs.push_back(v1); c1_ps.push_back(p1); c1_cs.push_back(c1); c1_draw.push_back(d1); c1_disp.push_back(s1);
        c2_vs.push_back(v2); c2_ps.push_back(p2); c2_cs.push_back(c2); c2_draw.push_back(d2); c2_disp.push_back(s2);
    }
    printf("  MEDYAN surec1(soguk): create_vs=%.2f create_ps=%.2f create_cs=%.2f draw=%.2f dispatch=%.2f\n",
           median(c1_vs), median(c1_ps), median(c1_cs), median(c1_draw), median(c1_disp));
    printf("  MEDYAN surec2(ayni exe, ayni bytecode): create_vs=%.2f create_ps=%.2f create_cs=%.2f draw=%.2f dispatch=%.2f\n",
           median(c2_vs), median(c2_ps), median(c2_cs), median(c2_draw), median(c2_disp));

    printf("=== S2 (Q2): exe ismine gore mi anahtarlaniyor? A 'probe11.exe'yi isitiyor, C=farkli isim ayni tohum, B=ayni isim farkli klasor ayni tohum ===\n");
    std::vector<double> qa_draw, qc_draw, qb_draw, qa_cs, qc_cs, qb_cs;
    for (int i = 0; i < 3; ++i) {
        unsigned seed = base + i * 7 + 101;
        std::wstring args = L"cold " + std::to_wstring(seed);
        auto oa = run_capture(exeA, args);
        auto oc = run_capture(exeC, args);
        auto ob = run_capture(exeB, args);
        double av, ap, ac, ad, as, cv, cp, cc, cd, cs_, bv, bp, bc, bd, bs;
        parse_cold(oa, &av, &ap, &ac, &ad, &as);
        parse_cold(oc, &cv, &cp, &cc, &cd, &cs_);
        parse_cold(ob, &bv, &bp, &bc, &bd, &bs);
        printf("  tohum(seed)=%u  A(warm-name): draw=%.2f cs=%.2f | C(farkli isim): draw=%.2f cs=%.2f | B(farkli klasor, ayni isim): draw=%.2f cs=%.2f\n",
               seed, ad, ac, cd, cc, bd, bc);
        qa_draw.push_back(ad); qa_cs.push_back(ac);
        qc_draw.push_back(cd); qc_cs.push_back(cc);
        qb_draw.push_back(bd); qb_cs.push_back(bc);
    }
    printf("  MEDYAN A(isim onbellegi isitiyor): draw=%.2f create_cs=%.2f\n", median(qa_draw), median(qa_cs));
    printf("  MEDYAN C(farkli exe ismi, ayni tohum): draw=%.2f create_cs=%.2f\n", median(qc_draw), median(qc_cs));
    printf("  MEDYAN B(ayni exe ismi, farkli klasor): draw=%.2f create_cs=%.2f\n", median(qb_draw), median(qb_cs));

    printf("=== S3 (Q3): duruma bagli yeniden derlemeler (recompiles), shader'lar zaten onbellekte; surec1 soguk-durum vs surec2 ayni-durum ===\n");
    std::vector<std::string> names;
    std::vector<std::vector<double>> p1_times, p2_times;
    for (int i = 0; i < 3; ++i) {
        unsigned seed = base + i * 7 + 201;
        std::wstring args = L"state " + std::to_wstring(seed);
        auto o1 = run_capture(exeA, args);
        auto o2 = run_capture(exeA, args);
        std::vector<std::pair<std::string, double>> v1, v2;
        parse_state(o1, v1);
        parse_state(o2, v2);
        if (names.empty()) { for (auto& kv : v1) names.push_back(kv.first); p1_times.resize(names.size()); p2_times.resize(names.size()); }
        for (size_t k = 0; k < v1.size() && k < names.size(); ++k) p1_times[k].push_back(v1[k].second);
        for (size_t k = 0; k < v2.size() && k < names.size(); ++k) p2_times[k].push_back(v2[k].second);
        printf("  tohum(seed)=%u  surec(proc)1: ", seed);
        for (auto& kv : v1) printf("%s=%.2f ", kv.first.c_str(), kv.second);
        printf(" | surec(proc)2: ");
        for (auto& kv : v2) printf("%s=%.2f ", kv.first.c_str(), kv.second);
        printf("\n");
    }
    for (size_t k = 0; k < names.size(); ++k)
        printf("  MEDYAN varyant=%-9s surec1(ilkez_gorulen)=%.2fms  surec2(2.surec, ayni durum)=%.2fms\n",
               names[k].c_str(), median(p1_times[k]), median(p2_times[k]));

    printf("=== S5 (Q5): shader basina veya cift(pair) basina onbellek? surec1 VS_a+PS_a cizer yeni(fresh); surec2 (ayni exe) cizer:\n");
    printf("    VS_a(gorulen)+PS_b(yeni), VS_c(yeni)+PS_a(gorulen), VS_a+PS_a(gorulen,varsayilan), VS_a+PS_a(gorulen,yeni_duzen) ===\n");
    std::vector<std::string> ptags;
    std::vector<std::vector<double>> ptimes;
    for (int i = 0; i < 3; ++i) {
        unsigned vs_a = base + i * 11 + 301, ps_a = base + i * 11 + 302, vs_c = base + i * 11 + 303, ps_b = base + i * 11 + 304;
        run_capture(exeA, L"pair1 " + std::to_wstring(vs_a) + L" " + std::to_wstring(ps_a));  // VS_a+PS_a'yı exeA adı altında ısıtır
        auto o2 = run_capture(exeA, L"pair2 " + std::to_wstring(vs_a) + L" " + std::to_wstring(ps_a) + L" " +
                                         std::to_wstring(vs_c) + L" " + std::to_wstring(ps_b));
        std::vector<std::pair<std::string, double>> v2;
        parse_pair(o2, v2);
        if (ptags.empty()) { for (auto& kv : v2) ptags.push_back(kv.first); ptimes.resize(ptags.size()); }
        for (size_t k = 0; k < v2.size() && k < ptags.size(); ++k) ptimes[k].push_back(v2[k].second);
        printf("  tekrar(rep)=%d  ", i);
        for (auto& kv : v2) printf("%s=%.2f ", kv.first.c_str(), kv.second);
        printf("\n");
    }
    for (size_t k = 0; k < ptags.size(); ++k)
        printf("  MEDYAN %-18s cizim(draw)=%.2fms\n", ptags[k].c_str(), median(ptimes[k]));

    for (auto& f : {exeB, exeC, exeA}) DeleteFileW(f.c_str());
    RemoveDirectoryW(subdir.c_str()), RemoveDirectoryW(dir.c_str());
    return 0;
}
