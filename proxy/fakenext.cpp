// Proxy'nin zincirlendiği bir modun d3d12.dll'i (scskiller.ini next=) için test kopyası: D3D12CreateDevice'ı sistem 
// dll'ine yönlendirir ve çağrıları sayar. Sadece D3D12CreateDevice, birçok sarmalayıcı (wrapper) gibi: proxy geri kalanı sistem dll'inden alır.
// FakeNext_SwapPs'den sonra, shader değiştiren bir sarmalayıcı (RenoDX eklentili ReShade) gibi davranır: oyun, 
// CreateGraphicsPipelineState fonksiyonu desc'i oyununki yerine o piksel shader'ı ile aktaran bir sarmalayıcı cihazı (wrapper device) alır.
// FakeNext_Layer'dan sonra ise dxgi.dll olarak bir ReShade katmanı (layer) gibidir: Oraya bakın.
// Asla yayınlanmaz (build/publish.ps1 yalnızca adlandırılmış dosyaları kopyalar); `selftest chain` bunu yükler.
#include <windows.h>
#define D3D12CreateDevice D3D12CreateDevice_h  // d3d12.h dllexport olmadan bildirir
#include <d3d12.h>
#include <dxgi1_2.h>
#undef D3D12CreateDevice
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static LONG g_calls;
static std::string g_ps;
static bool g_unwrap, g_rewrite_rs;
static const GUID kUnwrapped = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};  // ReShade 6.8+

extern "C" __declspec(dllexport) LONG WINAPI FakeNext_Calls() { return g_calls; }
extern "C" __declspec(dllexport) void WINAPI FakeNext_SwapPs(const void* p, SIZE_T n) { g_ps.assign((const char*)p, n); }
// Bir oyunun D3D12\D3D12Core.dll'i olarak kopyalandı: Cihaz (device) oluşturmayan, herhangi bir sisteminkinden daha yeni bir Agility çalışma zamanı (WarmerTests)
extern "C" __declspec(dllexport) const UINT D3D12SDKVersion = 100000;

// Sarmalayıcı (wrapper): QueryInterface ve CreateGraphicsPipelineState dışında her yuva, sarmalanan aygıtın aynı yuvasına yönlendirir 
// (bir thunk `this`i değiştirir). 128 yuva ID3D12Device14'ü kapsar.
struct Wrap {
    void** vt;
    ID3D12Device* real;
};

static HRESULT STDMETHODCALLTYPE wrap_qi(Wrap* w, REFIID riid, void** pp) {
    if (g_unwrap && riid == kUnwrapped) return w->real->AddRef(), *pp = w->real, S_OK;
    HRESULT hr = w->real->QueryInterface(riid, pp);
    if (SUCCEEDED(hr) && *pp == w->real) *pp = w;  // ReShade'de olduğu gibi her cihaz arayüzü sarmalayıcıda kalır
    return hr;
}

static HRESULT STDMETHODCALLTYPE wrap_gfx(Wrap* w, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, REFIID riid, void** pp) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC c = *d;
    if (c.PS.pShaderBytecode) c.PS = {g_ps.data(), g_ps.size()};
    return w->real->CreateGraphicsPipelineState(&c, riid, pp);
}

static HMODULE system_d3d12() {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    return LoadLibraryW((std::wstring(sys) + L"\\d3d12.dll").c_str());
}

// RenoDX'in enjeksiyonu: Her kök imzasına eklenen, yeniden serileştirilen bir kök sabiti (root constant) (b13, space 50).
static HRESULT STDMETHODCALLTYPE wrap_rs(Wrap* w, UINT mask, const void* blob, SIZE_T n, REFIID riid, void** pp) {
    if (!g_rewrite_rs) return w->real->CreateRootSignature(mask, blob, n, riid, pp);
    static HMODULE sys = system_d3d12();
    auto deser = (PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER)GetProcAddress(sys, "D3D12CreateRootSignatureDeserializer");
    auto ser = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(sys, "D3D12SerializeRootSignature");
    ID3D12RootSignatureDeserializer* ds = nullptr;
    if (!deser || !ser || FAILED(deser(blob, n, IID_PPV_ARGS(&ds)))) return E_FAIL;
    auto d = *ds->GetRootSignatureDesc();
    std::vector<D3D12_ROOT_PARAMETER> p(d.pParameters, d.pParameters + d.NumParameters);
    D3D12_ROOT_PARAMETER c = {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS};
    c.Constants = {13, 50, 4};
    p.push_back(c);
    d.NumParameters = (UINT)p.size(), d.pParameters = p.data();
    ID3DBlob *b = nullptr, *err = nullptr;
    HRESULT hr = ser(&d, D3D_ROOT_SIGNATURE_VERSION_1, &b, &err);
    if (SUCCEEDED(hr)) hr = w->real->CreateRootSignature(mask, b->GetBufferPointer(), b->GetBufferSize(), riid, pp);
    if (b) b->Release();
    if (err) err->Release();
    ds->Release();
    return hr;
}

// Her yuvası, ofset 8'deki nesnenin aynı yuvasına yönlendiren bir vtable.
static void** forward_vtable() {
    const int n = 128;
    auto code = (uint8_t*)VirtualAlloc(nullptr, n * 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    auto t = new void*[n];
    for (int i = 0; i < n; ++i) {
        uint8_t* c = code + i * 16;
        const uint8_t op[] = {0x48, 0x8B, 0x49, 0x08, 0x48, 0x8B, 0x01, 0xFF, 0xA0};  // mov rcx,[rcx+8]; mov rax,[rcx]; jmp [rax+disp32]
        int32_t disp = i * 8;
        memcpy(c, op, sizeof op), memcpy(c + sizeof op, &disp, 4);
        t[i] = c;
    }
    FlushInstructionCache(GetCurrentProcess(), code, n * 16);
    return t;
}

static void** wrapper_vtable() {
    static void** vt = [] {
        void** t = forward_vtable();
        t[0] = (void*)wrap_qi, t[10] = (void*)wrap_gfx, t[16] = (void*)wrap_rs;
        return t;
    }();
    return vt;
}

// selftest.exe'nin yanında dxgi.dll olarak kopyalanır (`selftest framesmod`): ReShade gibi System32'ninkine yönlendiren
// bir modun dxgi.dll'i. CreateDXGIFactory1'in fabrikası (factory) System32'nin (yönlendirilen her arama, QueryInterface'den gelen
// arayüzler dahil, böylece sarmalayıcı olarak kalır) etrafındaki kendi nesnesidir. Sarmalayıcının (wrapper) kendisi sızdırılmıştır (leaked): bir test süreci.
static HMODULE system_dxgi() {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    return LoadLibraryW((std::wstring(sys) + L"\\dxgi.dll").c_str());
}
struct FactoryWrap {
    void** vt;
    IUnknown* real;
};
static HRESULT STDMETHODCALLTYPE factory_qi(FactoryWrap* w, REFIID riid, void** pp) {
    HRESULT hr = w->real->QueryInterface(riid, pp);
    if (SUCCEEDED(hr) && *pp == w->real) *pp = w;
    return hr;
}
extern "C" HRESULT WINAPI fake_dxgi_create1(REFIID riid, void** pp) {
    static auto real = (HRESULT(WINAPI*)(REFIID, void**))GetProcAddress(system_dxgi(), "CreateDXGIFactory1");
    static void** vt = [] {
        void** t = forward_vtable();
        t[0] = (void*)factory_qi;
        return t;
    }();
    IUnknown* f = nullptr;
    HRESULT hr = real ? real(riid, (void**)&f) : E_FAIL;
    if (SUCCEEDED(hr)) *pp = new FactoryWrap{vt, f};
    return hr;
}
extern "C" HRESULT WINAPI fake_dxgi_create2(UINT flags, REFIID riid, void** pp) {
    static auto real = (HRESULT(WINAPI*)(UINT, REFIID, void**))GetProcAddress(system_dxgi(), "CreateDXGIFactory2");
    return real ? real(flags, riid, pp) : E_FAIL;
}
#pragma comment(linker, "/EXPORT:CreateDXGIFactory1=fake_dxgi_create1")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory2=fake_dxgi_create2")

extern "C" __declspec(dllexport) HRESULT WINAPI D3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL fl, REFIID riid, void** pp) {
    static auto real = (decltype(&D3D12CreateDevice_h))GetProcAddress(system_d3d12(), "D3D12CreateDevice");
    InterlockedIncrement(&g_calls);
    if (!real) return E_FAIL;
    if (g_ps.empty() || !pp) return real(adapter, fl, riid, pp);
    ID3D12Device* dev = nullptr;
    HRESULT hr = real(adapter, fl, IID_PPV_ARGS(&dev));
    if (FAILED(hr)) return hr;
    auto w = new Wrap{wrapper_vtable(), dev};  // sızdırıldı (leaked): bir test süreci
    hr = wrap_qi(w, riid, pp);
    dev->Release();
    return hr;
}

// FakeNext_Layer: RenoDX eklentisiyle dxgi.dll olarak bir ReShade gibi katman (layer). Proxy'den önce yüklendiğinde, sistem 
// d3d12.dll'inin D3D12CreateDevice dışa aktarımını buraya işaret eder (dışa aktarım tablosu girişi: ReShade bunun yerine kodu yamalar, 
// ve proxy her iki şekilde de ulaşır), ve aygıt (device) kök imzaları yeniden yazılmış halde sarmalanmış olarak gelir (wrap_rs). 
// unwrap (açma): sarmalayıcı IID_UnwrappedObject öğesini ReShade 6.8'in yaptığı gibi gerçek aygıtla yanıtlar; 
// bu olmadan proxy gerçek cihazı başka bir yoldan bulur.
static decltype(&D3D12CreateDevice_h) g_sys_create;

static HRESULT WINAPI layer_create(IUnknown* adapter, D3D_FEATURE_LEVEL fl, REFIID riid, void** pp) {
    if (!pp) return g_sys_create(adapter, fl, riid, pp);
    ID3D12Device* dev = nullptr;
    HRESULT hr = g_sys_create(adapter, fl, IID_PPV_ARGS(&dev));
    if (FAILED(hr)) return hr;
    auto w = new Wrap{wrapper_vtable(), dev};  // sızdırıldı (leaked): bir test süreci
    hr = wrap_qi(w, riid, pp);
    dev->Release();
    return hr;
}

extern "C" __declspec(dllexport) BOOL WINAPI FakeNext_Layer(BOOL unwrap) {
    g_unwrap = unwrap, g_rewrite_rs = true;
    auto base = (uint8_t*)system_d3d12();
    if (!base) return FALSE;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto ex = (IMAGE_EXPORT_DIRECTORY*)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    auto names = (DWORD*)(base + ex->AddressOfNames);
    auto ords = (WORD*)(base + ex->AddressOfNameOrdinals);
    auto fns = (DWORD*)(base + ex->AddressOfFunctions);
    for (DWORD i = 0; i < ex->NumberOfNames; ++i) {
        if (strcmp((const char*)base + names[i], "D3D12CreateDevice")) continue;
        DWORD* slot = &fns[ords[i]];
        g_sys_create = (decltype(g_sys_create))(base + *slot);
        // tablo, imaj (image) tabanından 32 bit ofsetleri tutar: imajı geçen bir atlama parçası (jump stub), ulaşılabilir durumda
        uint8_t* stub = nullptr;
        for (uintptr_t a = ((uintptr_t)base + nt->OptionalHeader.SizeOfImage + 0xFFFF) & ~(uintptr_t)0xFFFF; !stub && a < (uintptr_t)base + 0x7FFF0000; a += 0x10000)
            stub = (uint8_t*)VirtualAlloc((void*)a, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!stub) return FALSE;
        void* to = (void*)layer_create;
        const uint8_t jmp[] = {0xFF, 0x25, 0, 0, 0, 0};  // jmp [rip]
        memcpy(stub, jmp, sizeof jmp), memcpy(stub + sizeof jmp, &to, 8);
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        *slot = (DWORD)(stub - base);
        VirtualProtect(slot, 4, old, &old);
        return TRUE;
    }
    return FALSE;
}

// `selftest framesfg ffx` (test bu dll'i amd_fidelityfx_loader_dx12.dll olarak kopyalar) için FidelityFX'in ffxCreateContext öğesi:
// bir FOR_HWND kare oluşturma (frame generation) takas zinciri tanımlayıcısı (tip 0x30006) bir vekil takas zinciri (stand-in swap chain) alır,
// kendi vtable salt okunur verileri buradadır, bunun Present metodu çağıran iş parçacığında gerçek olanı sunar (menülerde olduğu gibi FSR 3 hiçbir şey üretmez),
// ardından başka bir iş parçacığından iki kez daha sunar (oluşturulmuş ve işlenmiş bir kare).
struct FgChain {
    const void* const* vt;
    IDXGISwapChain1* real;
};
static HRESULT STDMETHODCALLTYPE fg_qi(FgChain*, REFIID, void** pp) { return *pp = nullptr, E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE fg_ref(FgChain*) { return 1; }
static HRESULT STDMETHODCALLTYPE fg_none(FgChain*) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE fg_present(FgChain* c, UINT sync, UINT flags) {
    HRESULT hr = c->real->Present(sync, flags);
    std::thread([&] { for (int i = 0; i < 2 && SUCCEEDED(hr); ++i) hr = c->real->Present(sync, flags); }).join();
    return hr;
}
static const void* const kFgVt[23] = {(const void*)fg_qi, (const void*)fg_ref, (const void*)fg_ref, (const void*)fg_none, (const void*)fg_none,
    (const void*)fg_none, (const void*)fg_none, (const void*)fg_none, (const void*)fg_present, (const void*)fg_none, (const void*)fg_none,
    (const void*)fg_none, (const void*)fg_none, (const void*)fg_none, (const void*)fg_none, (const void*)fg_none, (const void*)fg_none,
    (const void*)fg_none, (const void*)fg_none, (const void*)fg_none, (const void*)fg_none, (const void*)fg_none, (const void*)fg_none};
struct FgForHwnd {
    uint64_t type;
    void* next;
    IUnknown** swapchain;
    HWND hwnd;
    DXGI_SWAP_CHAIN_DESC1* desc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen;
    IDXGIFactory* factory;
    ID3D12CommandQueue* queue;
};
extern "C" uint32_t fake_ffx_create(void** ctx, FgForHwnd* d, const void*) {  // ffxCreateContext (fakenext.asm) buraya atlar
    if (!d || (uint32_t)d->type != 0x30006) return 1;
    ID3D12Device* dev = nullptr;
    IDXGIFactory2* f2 = nullptr;
    ID3D12CommandQueue* q = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd = {};
    auto c = new FgChain{kFgVt, nullptr};
    bool ok = SUCCEEDED(d->queue->GetDevice(IID_PPV_ARGS(&dev))) && SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) &&
              SUCCEEDED(q->SetName(L"AMD FSR PresentQueue")) && SUCCEEDED(d->factory->QueryInterface(IID_PPV_ARGS(&f2))) &&
              SUCCEEDED(f2->CreateSwapChainForHwnd(q, d->hwnd, d->desc, d->fullscreen, nullptr, &c->real));
    if (!ok) return delete c, 1;
    *d->swapchain = (IUnknown*)c;
    *ctx = c;
    return 0;
}

// `selftest nvqi` için NVAPI'nin nvapi_QueryInterface'i (test bir kopyasını nvapi64.dll olarak yükler): 0x5EE000nn kendi işlevine çözümlenir, 
// 0x5EE100nn, 0x5EE000nn'i bu çözümlemenin içinden dışa aktarılan girdi yoluyla (girdiyi kancalayan (hooked) her neyse onun üzerinden 
// çözümleyen bir katman) çözümleyerek aynı işleve bağlanır, diğer tüm kimlikler boştur.
static char g_nv_fns[16];
extern "C" void* nvapi_QueryInterface(uint32_t id);
#pragma comment(linker, "/EXPORT:nvapi_QueryInterface")
#pragma comment(linker, "/EXPORT:ffxCreateContext")
// `selftest nvopts`: NvAPI_Initialize ve SetCreatePipelineStateOptions; FakeNv_Opts için kabul ettiği her çağrının bayraklarını (flags) tutar.
static std::mutex g_nv_mx;
static std::vector<uint32_t> g_nv_opts;
extern "C" void FakeNv_SetOpts();
extern "C" void FakeNv_OneByte();
extern "C" void FakeNv_ShortPad();
extern "C" void FakeNv_LivePad();
extern "C" void FakeNv_Raced();
extern "C" int FakeNv_OnOpts(void*, const uint32_t* p) {
    if (!p || (p[0] & 0xFFFF) < 8) return -9;  // NVAPI_INCOMPATIBLE_STRUCT_VERSION
    std::lock_guard l(g_nv_mx);
    g_nv_opts.push_back(p[1]);
    return 0;
}
static int nv_init() { return 0; }
extern "C" __declspec(dllexport) int WINAPI FakeNv_Opts(uint32_t* out, int max) {
    std::lock_guard l(g_nv_mx);
    int n = (int)std::min<size_t>(max, g_nv_opts.size());
    std::copy_n(g_nv_opts.begin(), n, out);
    return n;
}

// `selftest nvqi`: nvapi_QueryInterface içinde, proxy onu kancalarken ilk komutunu geçmiş bir iş parçacığı (thread).
static std::atomic<int> g_nv_inside;
extern "C" void fake_nv_qi_inside() {
    g_nv_inside = 1;
    g_nv_inside.notify_all();
    g_nv_inside.wait(1);
}
#pragma comment(linker, "/EXPORT:FakeNext_NvQiInside")
extern "C" __declspec(dllexport) void WINAPI FakeNext_NvQiHold(bool release) {  // false: bir iş parçacığı içeri girene kadar; true: devam eder
    if (!release) g_nv_inside.wait(0);
    else g_nv_inside = 2, g_nv_inside.notify_all();
}

extern "C" void* fake_nv_qi(uint32_t id) {
    if (id == 0x0150E828) return (void*)nv_init;
    if (id == 0x5C607A27) return (void*)FakeNv_SetOpts;
    if (id == 0xAC2DFEB5) return (void*)FakeNv_OneByte;   // SetNvShaderExtnSlotSpace
    if (id == 0x43D867C0) return (void*)FakeNv_ShortPad;  // SetNvShaderExtnSlotSpaceLocalThread
    if (id == 0x2FC28856) return (void*)FakeNv_LivePad;   // CreateGraphicsPipelineState
    if (id == 0x2762DEAC) return (void*)FakeNv_Raced;     // CreateComputePipelineState
    if ((id & ~0xFu) == 0x5EE00000) return &g_nv_fns[id & 0xF];
    if ((id & ~0xFu) == 0x5EE10000) return nvapi_QueryInterface(0x5EE00000 | (id & 0xF));
    return nullptr;
}

// FakeNext_NvAtExit'ten sonra, bu dll'nin süreç ayrılması (process detach), daha önce hiç çözülmemiş 16 kimliği (id) ve
// bilinen 16 kimliği dışa aktarılan (exported) girdi aracılığıyla (proxy'den önce yüklenen, işlem çıkışında NVAPI'yi çağıran
// ve proxy'den sonra ayrılan bir dll) çözer, ardından kendisinin yanına nv_at_exit.done yapar.
static bool g_nv_at_exit;
extern "C" __declspec(dllexport) void WINAPI FakeNext_NvAtExit() { g_nv_at_exit = true; }
BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
    if (reason != DLL_PROCESS_DETACH || !reserved || !g_nv_at_exit) return TRUE;
    for (uint32_t i = 0; i < 16; ++i) nvapi_QueryInterface(0x5EE20000 | i), nvapi_QueryInterface(0x5EE00000 | i);
    wchar_t p[MAX_PATH];
    if (DWORD n = GetModuleFileNameW(self, p, MAX_PATH); n && n < MAX_PATH - 16 && wcsrchr(p, L'\\')) {
        wcscpy(wcsrchr(p, L'\\') + 1, L"nv_at_exit.done");
        CloseHandle(CreateFileW(p, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    }
    return TRUE;
}
