// selftest.cpp ve probe11.cpp tarafından paylaşılır: check makrosu, duvar saati (wall-clock) zamanlayıcısı ve fxc sarmalayıcısı.
#pragma once
#include <d3dcompiler.h>
#include <chrono>
#include <cstdio>
#include <string>

#define CHECK(x) do { if (!(x)) { printf("BASARISIZ %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

template <class F> static double ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static ID3DBlob* compile(const std::string& src, const char* target) {
    ID3DBlob *code = nullptr, *err = nullptr;
    D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr, "main", target, 0, 0, &code, &err);
    if (err) { printf("%s\n", (const char*)err->GetBufferPointer()); err->Release(); }
    return code;
}
