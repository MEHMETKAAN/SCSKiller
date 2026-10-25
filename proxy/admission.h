// Kaydedicinin kabulünün (admission) yalnızca değerleri okuyan kısımları, `selftest admission` ile paylaşılır: 
// sig= damgasının kapsadığı yerler ve kontrollerin bulduklarından çıkan karar.
#pragma once
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

// sig= damgasının kapsadığı, ScsKiller.Signed tarafından yazılan satırlar: exe_size=, exe_time=, ardından pid= varsa
// pid= ve pid_time=, risk= varsa risk=.
inline std::wstring signed_stamp(const std::wstring& size, const std::wstring& time, const std::wstring& pid, const std::wstring& pid_time, const std::wstring& risk,
                                 const std::wstring& vendor = L"", const std::wstring& overlay = L"", const std::wstring& play_s = L"", const std::wstring& tag = L"") {
    std::wstring s = L"exe_size=" + size + L"\r\nexe_time=" + time + L"\r\n";
    if (!pid.empty()) s += L"pid=" + pid + L"\r\npid_time=" + pid_time + L"\r\n";
    if (!risk.empty()) s += L"risk=" + risk + L"\r\n";
    if (!vendor.empty()) s += L"vendor=" + vendor + L"\r\n";
    if (!overlay.empty()) s += L"overlay=" + overlay + L"\r\n";
    if (!play_s.empty()) s += L"ab_play_s=" + play_s + L"\r\n";
    if (!tag.empty()) s += L"ab_tag=" + tag + L"\r\n";
    return s;
}

// risk= ya eksiktir, ya da pid= ile birlikte 1'dir.
inline bool risk_well_formed(const std::wstring& pid, const std::wstring& risk) { return risk.empty() || (!pid.empty() && risk == L"1"); }

// Kontrollerin bulduklarından çıkan karar, kendi sıralarına göre: Riot Games kurulumu, kurulum köküne kadar 
// klasörlerdeki işaretçiler ("isim (nerede)"; biri tam listelenemediyse {"?"}) ve ilkinin nedeni olarak found_why, 
// yüklenen anti-hile istemci modülleri, vac_now'ın nedeni (Steam'in dosyaları söyleyemediyse "VAC: ..."). 
// Risk olmadan ilk bulgu reddeder. Risk altında kesin bir bulgu kabul eder ve logun listelediği gibi seen (görüldü)
// değişkenine geçer; incelenemeyenler risk olmadan reddedildiği gibi reddedilir.
inline const char* decide(bool risk, bool riot, const std::vector<std::string>& found, const char* found_why, const std::vector<std::string>& loaded, const char* vac,
                          std::string& seen) {
    if (!risk) return riot ? "Riot Games kurulumu" : !found.empty() ? found_why : !loaded.empty() ? "anti-hile istemcisi yüklendi" : vac;
    if (!found.empty() && found[0] == "?") return found_why;
    if (vac && !strncmp(vac, "VAC:", 4)) return vac;
    for (auto& m : found) seen += (seen.empty() ? "" : ", ") + m;
    for (auto& m : loaded) seen += (seen.empty() ? "" : ", ") + m + " yüklendi";
    if (riot) seen += (seen.empty() ? "" : ", ") + std::string("Riot Games kurulumu");
    if (vac) seen += (seen.empty() ? "" : ", ") + std::string("VAC");
    if (seen.empty()) seen = "ilk cihazda hiçbir şey yok";
    return nullptr;
}

// scskiller.ini'nin baytlarındaki anahtarın değeri, GetPrivateProfileStringW'nin bir ANSI dosyasını okuduğu gibi okunur:
// ilk [scskiller] bölümü, anahtarın ilk satırı, kırpılmış, tırnak işaretleri çıkarılmış halde. Satırların bu ikisini farklı 
// bölebileceği bir dosya (LF olmadan bir CR, bir NUL, tam olarak [scskiller] dışında bir bölüm başlığı veya bunlardan 
// ikisi) boş olarak okunur: imzalanmamıştır.
inline std::wstring ini_value(const std::string& bytes, const wchar_t* key) {
    if (bytes.find('\0') != std::string::npos) return L"";
    for (size_t cr = bytes.find('\r'); cr != std::string::npos; cr = bytes.find('\r', cr + 1))
        if (cr + 1 == bytes.size() || bytes[cr + 1] != '\n') return L"";
    std::wstring text(bytes.size(), L'\0');
    const int n = bytes.empty() ? 0 : MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)bytes.size(), text.data(), (int)text.size());
    text.resize(n > 0 ? n : 0);
    const auto trim = [](const std::wstring& s) {
        const size_t a = s.find_first_not_of(L" \t\r"), b = s.find_last_not_of(L" \t\r");
        return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
    };
    int sections = 0;
    for (size_t at = 0; at <= text.size();) {
        const size_t end = text.find(L'\n', at);
        const std::wstring line = trim(text.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at));
        at = end == std::wstring::npos ? text.size() + 1 : end + 1;
        if (!line.empty() && line[0] == L'[' && (_wcsicmp(line.c_str(), L"[scskiller]") || ++sections > 1)) return L"";
    }
    bool in = false;
    for (size_t at = 0; at <= text.size();) {
        const size_t end = text.find(L'\n', at);
        std::wstring line = text.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
        at = end == std::wstring::npos ? text.size() + 1 : end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        line = trim(line);
        if (!line.empty() && line[0] == L'[') {
            if (in) break;
            in = !_wcsicmp(line.c_str(), L"[scskiller]");
            continue;
        }
        const size_t eq = line.find(L'=');
        if (!in || line.empty() || line[0] == L';' || eq == std::wstring::npos || _wcsicmp(trim(line.substr(0, eq)).c_str(), key)) continue;
        std::wstring v = trim(line.substr(eq + 1));
        if (v.size() >= 2 && (v[0] == L'"' || v[0] == L'\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
        return v;
    }
    return L"";
}
