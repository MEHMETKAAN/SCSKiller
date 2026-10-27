// Proxy ve uygulamanın (ScsKiller.FileKey, ScsKiller.Signed) scskiller.ini'deki kaydedici damgası hakkında paylaştıkları.
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <string>

// Kaydediciyi <dir> (sondaki ters eğik çizgi) içine yerleştiren SCSKiller kopyasının veri klasörü:
// scskiller.ledger'ın adlandırdığı armed\ yolunun ebeveyni (UTF-8, mutlak, tek satır; taşınabilir veya test kopyasınınki, ScsKiller.LedgerStamp). False:
// hiçbiri yok (kurulu kopyanınki). Orada ama kullanılamaz: "" ile true.
inline bool stamped_recorder_dir(const std::wstring& dir, std::wstring& out) {
    out.clear();
    const std::wstring file = dir + L"scskiller.ledger";
    const DWORD a = GetFileAttributesW(file.c_str());   // bir bağlantının kendi nitelikleri: sarkan (dangling) bir bağlantı oradadır
    if (a == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        return e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND;
    }
    if (a & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) return true;   // uygulama düz bir dosya yazar
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return true;
    FILE_ATTRIBUTE_TAG_INFO tag{};
    char b[4 * MAX_PATH];
    DWORD n = 0;
    const BOOL read = GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof tag) && !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                      && ReadFile(h, b, sizeof b, &n, nullptr);
    CloseHandle(h);
    if (!read || n == sizeof b) return true;
    while (n && (b[n - 1] == '\r' || b[n - 1] == '\n' || b[n - 1] == ' ')) --n;
    const int m = n ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b, (int)n, nullptr, 0) : 0;
    if (m <= 3 || m >= MAX_PATH) return true;
    std::wstring ledger(m, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b, (int)n, ledger.data(), m);
    // Path.GetFullPath'in yazdığı gibi: X:\ veya \\sunucu\paylasim, ardından boş olmayan veya . veya boşlukla bitmeyen segmentler (Windows
    // bunları atar: .. ve "x." diğer klasörleri hedefler), cihaz yolu (device path), akış (stream), joker karakter (wildcard) veya NUL yok
    const bool drive = (ledger[0] | 0x20) >= L'a' && (ledger[0] | 0x20) <= L'z' && ledger[1] == L':' && ledger[2] == L'\\';
    bool ok = (drive || (ledger[0] == L'\\' && ledger[1] == L'\\')) && ledger.find_first_of(L"/?*<>|\"") == std::wstring::npos
              && ledger.find(L':', 2) == std::wstring::npos && ledger.find(L'\0') == std::wstring::npos;
    size_t parts = 0;
    for (size_t at = drive ? 3 : 2; ok && at <= ledger.size(); ++parts) {
        size_t end = ledger.find(L'\\', at);
        if (end == std::wstring::npos) end = ledger.size();
        ok = end > at && ledger[end - 1] != L'.' && ledger[end - 1] != L' ';
        at = end + 1;
    }
    const size_t cut = ledger.find_last_of(L'\\');
    if (ok && parts >= (drive ? 2u : 3u) && !_wcsicmp(ledger.c_str() + cut + 1, L"armed")) out = ledger.substr(0, cut);
    return true;
}

// Kaydedicisi <dir> (sondaki ters eğik çizgi) içinde olan kopyanın kaydedici klasörü (recorder.key, recorder.revoked, refused\):
// stamped_recorder_dir'inki, aksi takdirde kurulu kopyanın %LOCALAPPDATA%\SCSKiller klasörü; alınamıyorsa "".
// SCSKILLER_TEST_RECORDER_DIR, onu yalnızca SCSKiller_WarmHost'u dışa aktaran bir süreçte (selftest, scskiller_warm) değiştirir, 
// asla bir oyunda değiştirmez: bir başlatıcının veya başka bir programın aktardığı bir ortam, bir oyunun kabulünün okuduğu yeri taşıyamaz. Orada ayarlanmış 
// ancak kullanılamaz: "", asla gerçeği değil.
inline std::wstring recorder_dir(const std::wstring& dir) {
    if (GetProcAddress(GetModuleHandleW(nullptr), "SCSKiller_WarmHost")) {
        wchar_t v[MAX_PATH];
        if (DWORD n = GetEnvironmentVariableW(L"SCSKILLER_TEST_RECORDER_DIR", v, MAX_PATH)) return n < MAX_PATH ? v : L"";
    }
    if (dir.empty()) return L"";
    std::wstring stamped;
    if (stamped_recorder_dir(dir, stamped)) return stamped;
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return CoTaskMemFree(local), L"";
    std::wstring installed = local;
    CoTaskMemFree(local);
    return installed + L"\\SCSKiller";
}

inline std::wstring key_hex(const void* p, size_t n) {
    UCHAR h[20];
    if (BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, (PUCHAR)p, (ULONG)n, h, 20)) return L"";
    wchar_t hex[41] = {};
    for (int i = 0; i < 20; ++i) swprintf(hex + 2 * i, 3, L"%02x", h[i]);
    return hex;
}

inline std::wstring path_key(std::wstring path) {
    for (auto& c : path)
        if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return key_hex(path.data(), path.size() * sizeof(wchar_t));
}

// Açılacak yol, uzun-yol farkındalığı (long-path aware) olmayan bir işlemde MAX_PATH'i geçtiğinde: mutlak yolun önünde \\?\ (paylaşım için \\?\UNC\).
// \\?\, normalleştirmeyi kapatır, böylece / veya . ya da .. segmentli bir yol olduğu gibi bırakılır (açılamaz).
inline std::wstring long_path(const std::wstring& p) {
    if (p.size() < MAX_PATH || p.rfind(L"\\\\?\\", 0) == 0 || p.find(L'/') != std::wstring::npos) return p;
    for (const wchar_t* dots : {L"\\.\\", L"\\..\\"})
        if (p.find(dots) != std::wstring::npos) return p;
    if (p.ends_with(L"\\.") || p.ends_with(L"\\..")) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    return p.size() > 2 && p[1] == L':' && p[2] == L'\\' ? L"\\\\?\\" + p : p;
}

// Yoldaki bir klasör adı nokta veya boşlukla biter: \\?\ ile veya onsuz açıldığında iki klasörü adlandırabilir, bu yüzden 
// bir kontrolün neyi listelediği yolun uzunluğuna bağlı olabilir.
inline bool odd_name(const std::wstring& p) {
    for (size_t at = p.find(L'\\', 1); at != std::wstring::npos; at = p.find(L'\\', at + 1))
        if (p[at - 1] == L'.' || p[at - 1] == L' ') return true;
    return !p.empty() && (p.back() == L'.' || p.back() == L' ');
}

// Bir dosyanın veya klasörün anahtarı: tam dosya konumu (FILE_ID_INFO, 24 bayt: paketlenmiş bir uygulamanın sürecinin kendi 
// exe'sini paketin WindowsApps bağlantısıyla adlandırdığı herhangi bir bağlantı, birleşim noktası (junction) veya 8.3 adı 
// üzerinden aynıdır), aksi takdirde nihai yolun (final path) (\\?\ olmadan), aksi takdirde verilen yolun 
// (UTF-16LE, A-Z küçültülmüş) SHA-1 hex değeri.
inline std::wstring file_key(const std::wstring& path) {
    HANDLE f = CreateFileW(long_path(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        FILE_ID_INFO id;
        std::wstring key;
        if (GetFileInformationByHandleEx(f, FileIdInfo, &id, sizeof id)) key = key_hex(&id, sizeof id);
        std::wstring final(32768, L'\0');
        DWORD m = key.empty() ? GetFinalPathNameByHandleW(f, final.data(), (DWORD)final.size(), 0) : 0;
        CloseHandle(f);
        if (!key.empty()) return key;
        if (m && m < final.size()) {
            final.resize(m);
            return path_key(final.rfind(L"\\\\?\\UNC\\", 0) == 0 ? L"\\\\" + final.substr(8) : final.rfind(L"\\\\?\\", 0) == 0 ? final.substr(4) : final);
        }
    }
    return path_key(path);
}

// Klasörün nihai yolu (bağlantılar, birleşim noktaları ve 8.3 adları çözümlenmiş; \\?\ yok) sondaki ters eğik çizgi ile;
// alınamıyorsa "" döner (örneğin sürücü harfi hacmi (volume) ile eşleşmiyorsa).
inline std::wstring final_dir(const std::wstring& dir) {
    HANDLE f = CreateFileW(long_path(dir).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    std::wstring final(32768, L'\0');
    DWORD m = GetFinalPathNameByHandleW(f, final.data(), (DWORD)final.size(), 0);
    CloseHandle(f);
    if (!m || m >= final.size()) return L"";
    final.resize(m);
    final = final.rfind(L"\\\\?\\UNC\\", 0) == 0 ? L"\\\\" + final.substr(8) : final.rfind(L"\\\\?\\", 0) == 0 ? final.substr(4) : final;
    return final.back() == L'\\' ? final : final + L'\\';
}

// sig=: anahtar altındaki "<exe'nin dosya_anahtarı>\n<klasörünün dosya_anahtarı>\n" + damga +
// "root=<kök>\n<kökün dosya_anahtarı>\n" HMAC-SHA1 değeri (küçük harfli hex), UTF-16LE, burada damga admission.h'nin signed_stamp'idir 
// ve kök, exe'nin final_dir'inden çözümlenen root= değeridir. Yapılamadığında "".
inline std::wstring recorder_sig(const std::string& key, const std::wstring& exe_key, const std::wstring& dir_key, const std::wstring& stamp, const std::wstring& root,
                                 const std::wstring& root_key) {
    const std::wstring msg = exe_key + L"\n" + dir_key + L"\n" + stamp + L"root=" + root + L"\n" + root_key + L"\n";
    UCHAR mac[20];
    if (key.empty() || BCryptHash(BCRYPT_HMAC_SHA1_ALG_HANDLE, (PUCHAR)key.data(), (ULONG)key.size(), (PUCHAR)msg.data(), (ULONG)(msg.size() * sizeof(wchar_t)), mac, 20))
        return L"";
    wchar_t hex[41] = {};
    for (int i = 0; i < 20; ++i) swprintf(hex + 2 * i, 3, L"%02x", mac[i]);
    return hex;
}
