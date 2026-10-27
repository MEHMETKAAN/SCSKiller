// proxy.cpp ve selftest.cpp tarafından paylaşılır: System32'nin dxgi.dll'i ve bir adresin içinde yaşadığı modül. Bir oyun klasörünün
// dxgi.dll'i (OptiScaler, ReShade) aynı ada sahiptir, bu nedenle GetModuleHandleW(L"dxgi.dll") o olabilir.
#pragma once
#include <windows.h>
#include <string>

// yükleyici (loader) tam yol (full path) için başka bir dosya geri döndürdüğünde (DLL yönlendirmesi: exe'nin yanındaki .local klasörü) null döner
inline HMODULE system_dxgi() {
    wchar_t p[MAX_PATH], got[MAX_PATH];
    UINT n = GetSystemDirectoryW(p, MAX_PATH);
    if (!n || n >= MAX_PATH) return nullptr;
    const std::wstring path = std::wstring(p, n) + L"\\dxgi.dll";
    HMODULE m = LoadLibraryW(path.c_str());
    return m && GetModuleFileNameW(m, got, MAX_PATH) && !_wcsicmp(got, path.c_str()) ? m : nullptr;
}

// yüklü bir modülde değilse null (bir vtable'ın yığın (heap) kopyası)
inline HMODULE module_of(const void* p) {
    HMODULE m = nullptr;
    if (p) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &m);
    return m;
}
