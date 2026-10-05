// fingerprint.cpp — 离线指纹注入实现（SunLauncher）
// 零第三方依赖：Base64/MD5/JSON 均为自包含实现；仅 Win32 API + 标准库。
// 不做任何网络 IO；只读写 <dataDir>/<profile>/ 下的文件并拼启动命令行。
#include "fingerprint.h"
#include "fp_webrtc.h"
#include "fp_browser_config.h"
#include "fp_cookies.h"
#include <tlhelp32.h>

const wchar_t* FP_C1 = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const wchar_t* FP_C2 = L"hTy1bfRJz4nLPcBCO7WtmNIaGvVeul5Zo8kq32UxrYw_-0gsjp96SDFXQiEMKdHA";

// ================= 标准 Base64 =================
static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string FpStdBase64Encode(const std::string& raw) {
    std::string o;
    o.reserve(((raw.size() + 2) / 3) * 4);
    for (size_t i = 0; i < raw.size(); i += 3) {
        unsigned v = (unsigned char)raw[i] << 16;
        int n = 1;
        if (i + 1 < raw.size()) { v |= (unsigned char)raw[i + 1] << 8; n++; }
        if (i + 2 < raw.size()) { v |= (unsigned char)raw[i + 2]; n++; }
        o += kB64[(v >> 18) & 63];
        o += kB64[(v >> 12) & 63];
        o += (n > 1) ? kB64[(v >> 6) & 63] : '=';
        o += (n > 2) ? kB64[v & 63] : '=';
    }
    return o;
}
static int B64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
std::string FpStdBase64Decode(const std::string& b64) {
    std::string o;
    o.reserve((b64.size() / 4) * 3);
    for (size_t i = 0; i + 3 < b64.size() + 1; i += 4) {
        int a = B64Val(b64[i]), b = B64Val(b64[i + 1]);
        int c = (b64[i + 2] == '=') ? 0 : B64Val(b64[i + 2]);
        int d = (b64[i + 3] == '=') ? 0 : B64Val(b64[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return "";
        unsigned v = (a << 18) | (b << 12) | (c << 6) | d;
        o += (char)((v >> 16) & 255);
        if (b64[i + 2] != '=') o += (char)((v >> 8) & 255);
        if (b64[i + 3] != '=') o += (char)(v & 255);
    }
    return o;
}

// ================= 换表 =================
// main.min.js: encode 时标准表字符 -> 自定义表同下标字符；decode 反之；不在表中的字符原样保留
static std::string MapTable(const std::string& s, const wchar_t* from, const wchar_t* to) {
    char f[128], t[128];
    for (int i = 0; i < 64; i++) { f[i] = (char)from[i]; t[i] = (char)to[i]; }
    std::string o = s;
    for (size_t k = 0; k < o.size(); k++) {
        for (int i = 0; i < 64; i++) {
            if (o[k] == f[i]) { o[k] = t[i]; break; }
        }
    }
    return o;
}
std::string FpEncode(const std::string& rawJson) {
    return MapTable(FpStdBase64Encode(rawJson), FP_C1, FP_C2);
}
std::string FpDecode(const std::string& mapped) {
    return FpStdBase64Decode(MapTable(mapped, FP_C2, FP_C1));
}

// ================= MD5（RFC1321 公共实现，精简为单函数） =================
struct Md5Ctx {
    uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
    uint64_t total = 0;
    unsigned char buf[64] = {};
    size_t buflen = 0;
};
static inline uint32_t Rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static void Md5Block(Md5Ctx& x, const unsigned char* p) {
    static const uint32_t S[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
    uint32_t M[16];
    for (int i = 0; i < 16; i++)
        M[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) |
               ((uint32_t)p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);
    uint32_t A = x.a, B = x.b, C = x.c, D = x.d, F;
    int g;
    for (int i = 0; i < 64; i++) {
        if (i < 16) { F = (B & C) | (~B & D); g = i; }
        else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
        else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
        else { F = C ^ (B | ~D); g = (7 * i) % 16; }
        F = F + A + K[i] + M[g];
        A = D; D = C; C = B;
        B = B + Rol(F, S[i]);
    }
    x.a += A; x.b += B; x.c += C; x.d += D;
}
static void Md5Update(Md5Ctx& x, const unsigned char* d, size_t n) {
    x.total += n;
    while (n > 0) {
        size_t take = 64 - x.buflen;
        if (take > n) take = n;
        memcpy(x.buf + x.buflen, d, take);
        x.buflen += take; d += take; n -= take;
        if (x.buflen == 64) { Md5Block(x, x.buf); x.buflen = 0; }
    }
}
static std::string Md5Final(Md5Ctx& x) {
    uint64_t bits = x.total * 8;
    unsigned char pad = 0x80;
    Md5Update(x, &pad, 1);
    unsigned char z = 0;
    while (x.buflen != 56) Md5Update(x, &z, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; i++) len[i] = (unsigned char)((bits >> (i * 8)) & 255);
    Md5Update(x, len, 8);
    unsigned char dg[16];
    uint32_t h[4] = { x.a, x.b, x.c, x.d };
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) dg[i * 4 + j] = (unsigned char)((h[i] >> (j * 8)) & 255);
    static const char* hex = "0123456789abcdef";
    std::string o;
    for (int i = 0; i < 16; i++) { o += hex[dg[i] >> 4]; o += hex[dg[i] & 15]; }
    return o;
}
std::string FpMd5Hex(const std::string& s) {
    Md5Ctx x;
    Md5Update(x, (const unsigned char*)s.data(), s.size());
    return Md5Final(x);
}
std::string FpStaticName(const std::string& fbccId) { return FpMd5Hex(fbccId + "_static"); }
std::string FpDynamicName(const std::string& fbccId) { return FpMd5Hex(fbccId + "_webrtc"); }
std::string FpCookiesName(const std::string& fbccId) { return FpMd5Hex(fbccId + "_cookies"); }

// ================= 文件读写 =================
std::string FpFbccIdOf(const std::wstring& profileDirName) {
    size_t p = profileDirName.find(L'_');
    std::wstring fb = (p == std::wstring::npos) ? profileDirName : profileDirName.substr(0, p);
    return N(fb);
}
bool FpReadTextFile(const std::wstring& path, std::string& out) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    ::GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 64 * 1024 * 1024) { ::CloseHandle(h); return false; }
    std::string buf((size_t)sz.QuadPart, 0);
    DWORD got = 0, off = 0;
    while (off < buf.size()) {
        DWORD r = 0;
        if (!::ReadFile(h, &buf[off], (DWORD)(buf.size() - off), &r, NULL) || r == 0) break;
        off += r; got = off;
    }
    ::CloseHandle(h);
    buf.resize(got);
    // 去首尾空白（含换行），与官方 readFile 后行为一致
    size_t a = buf.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { out.clear(); return true; }
    size_t b = buf.find_last_not_of(" \t\r\n");
    out = buf.substr(a, b - a + 1);
    return true;
}
bool FpWriteTextFile(const std::wstring& path, const std::string& data) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD done = 0;
    ::WriteFile(h, data.data(), (DWORD)data.size(), &done, NULL);
    ::CloseHandle(h);
    return done == data.size();
}

static bool LoadAndDecode(const std::wstring& profileDir, const std::string& fileName, std::string& jsonOut) {
    std::wstring path = profileDir + L"\\" + W(fileName);
    std::string raw;
    if (!FpReadTextFile(path, raw) || raw.empty()) { jsonOut.clear(); return false; }
    jsonOut = FpDecode(raw);
    return !jsonOut.empty();
}
bool FpLoadStaticJson(const std::wstring& profileDir, std::string& jsonOut) {
    std::wstring name = profileDir.substr(profileDir.find_last_of(L"\\/") + 1);
    return LoadAndDecode(profileDir, FpStaticName(FpFbccIdOf(name)), jsonOut);
}
bool FpLoadDynamicJson(const std::wstring& profileDir, std::string& jsonOut) {
    std::wstring name = profileDir.substr(profileDir.find_last_of(L"\\/") + 1);
    return LoadAndDecode(profileDir, FpDynamicName(FpFbccIdOf(name)), jsonOut);
}
bool FpLoadCookiesJson(const std::wstring& profileDir, std::string& jsonOut) {
    // 官方 main.min.js setCookie 写 cookies 文件是 writeFile 明文（x(n,JSON.stringify(t))）；
    // 实测 k1c6pr18 的 cookies 文件即明文 JSON 数组。读侧兼容双格式：
    // 换表编码能解出“像 JSON”的结果才按解码用（防把乱码解成垃圾），否则原文是 JSON 即明文返回。
    // 读出后统一规范化（丢 [{}] 这类无 name 元素、补官方默认字段），保证编辑框里是明文可改的 JSON。
    std::wstring name = profileDir.substr(profileDir.find_last_of(L"\\/") + 1);
    std::wstring path = profileDir + L"\\" + W(FpCookiesName(FpFbccIdOf(name)));
    std::string raw;
    if (!FpReadTextFile(path, raw) || raw.empty()) { jsonOut.clear(); return false; }
    auto looksJson = [](const std::string& s) {
        size_t p = s.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
        return p != std::string::npos && (s[p] == '[' || s[p] == '{');
    };
    std::string src;
    std::string dec = FpDecode(raw);
    if (!dec.empty() && looksJson(dec)) src = dec;
    else if (looksJson(raw)) {
        size_t nz = raw.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
        src = raw.substr(nz);
        size_t tail = src.find_last_not_of(" \t\r\n");
        if (tail != std::string::npos) src.resize(tail + 1);
    }
    if (src.empty()) { jsonOut.clear(); return false; }
    std::string norm = FpNormalizeCookiesJson(src);
    jsonOut = norm.empty() ? src : norm; // 对象等异常结构先原样返回，由调用方决定
    return !jsonOut.empty();
}
// 写前比对 md5(文件原文) vs md5(新编码)，一致跳过（官方 FinalizeTask 逻辑）
static bool SaveEncoded(const std::wstring& profileDir, const std::string& fileName, const std::string& jsonText) {
    std::string enc = FpEncode(jsonText);
    if (enc.empty()) return false;
    std::wstring path = profileDir + L"\\" + W(fileName);
    std::string old;
    if (FpReadTextFile(path, old) && !old.empty()) {
        if (FpMd5Hex(old) == FpMd5Hex(enc)) return true;  // 内容一致，跳过写盘
    }
    return FpWriteTextFile(path, enc);
}
bool FpSaveStaticJson(const std::wstring& profileDir, const std::string& jsonText) {
    std::wstring name = profileDir.substr(profileDir.find_last_of(L"\\/") + 1);
    return SaveEncoded(profileDir, FpStaticName(FpFbccIdOf(name)), jsonText);
}
bool FpSaveDynamicJson(const std::wstring& profileDir, const std::string& jsonText) {
    std::wstring name = profileDir.substr(profileDir.find_last_of(L"\\/") + 1);
    return SaveEncoded(profileDir, FpDynamicName(FpFbccIdOf(name)), jsonText);
}
bool FpSaveCookiesJson(const std::wstring& profileDir, const std::string& jsonText) {
    // 官方 main.min.js setCookie：x(n,JSON.stringify(t)) 即 writeFile 明文，无 encodeBase64。
    // 写前规范化（丢无效元素/补官方默认字段），不是 JSON 数组则拒绝落盘（编辑框守卫已在保存链转换）。
    // 写前比对 md5(原文)，一致跳过。读侧 FpLoadCookiesJson 兼容双格式。
    std::string norm = FpNormalizeCookiesJson(jsonText);
    if (norm.empty()) return false;
    std::wstring name = profileDir.substr(profileDir.find_last_of(L"\\/") + 1);
    std::string fileName = FpCookiesName(FpFbccIdOf(name));
    std::wstring path = profileDir + L"\\" + W(fileName);
    std::string old;
    if (FpReadTextFile(path, old) && !old.empty()) {
        if (FpMd5Hex(old) == FpMd5Hex(norm)) return true;  // 内容一致，跳过写盘
    }
    return FpWriteTextFile(path, norm);
}
// ui_fingerprint.json：明文存放，不做换表编码，方便 UI 直接读写
bool FpLoadUiExtra(const std::wstring& profileDir, std::string& jsonOut) {
    return FpReadTextFile(profileDir + L"\\ui_fingerprint.json", jsonOut);
}
bool FpSaveUiExtra(const std::wstring& profileDir, const std::string& jsonText) {
    return FpWriteTextFile(profileDir + L"\\ui_fingerprint.json", jsonText);
}
// FpJsonGet 返回含引号的原始片段，去引号并还原转义（与 fp_ui.cpp JEsc 对应）。
std::wstring FpJsonUnquote(const std::string& raw) {
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        std::string s = raw.substr(1, raw.size() - 2), o;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char n = s[i + 1];
                if (n == 'b') { o += '\b'; i++; }
                else if (n == 'f') { o += '\f'; i++; }
                else if (n == 'n') { o += '\n'; i++; }
                else if (n == 'r') { o += '\r'; i++; }
                else if (n == 't') { o += '\t'; i++; }
                else if (n == 'u' && i + 5 < s.size()) {
                    auto hv = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                        return -1;
                    };
                    int h1 = hv(s[i + 2]), h2 = hv(s[i + 3]), h3 = hv(s[i + 4]), h4 = hv(s[i + 5]);
                    if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0 && h1 == 0 && h2 == 0) {
                        o += (char)((h3 << 4) | h4);
                        i += 5;
                    } else { o += n; i++; }
                } else { o += n; i++; }
            }
            else o += s[i];
        }
        return W(o);
    }
    return W(raw);
}
// 主窗口环境列表备注列：读 ui 存档 remark，换行/制表压成空格并去首尾空白。
std::wstring FpProfileRemark(const std::wstring& profileDir) {
    std::string ui;
    if (!FpLoadUiExtra(profileDir, ui) || ui.empty()) return L"";
    std::wstring r = FpJsonUnquote(FpJsonGet(ui, "remark"));
    for (auto& c : r) { if (c == L'\r' || c == L'\n' || c == L'\t') c = L' '; }
    size_t a = r.find_first_not_of(L" ");
    if (a == std::wstring::npos) return L"";
    size_t b = r.find_last_not_of(L" ");
    return r.substr(a, b - a + 1);
}

// ================= 语言三键工具（对齐 main.min.js LanguageTask 全文） =================
static std::string TrimTag(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::vector<std::string> FpParseLangList(const std::string& raw) {
    std::vector<std::string> out;
    std::string t = TrimTag(raw);
    if (t.empty() || t == "\"\"" || t == "[]") return out;
    // JSON 数组形态：["en-US","en"] -> 逐项取引号内
    if (t.front() == '[') {
        size_t p = 0;
        while ((p = t.find('"', p)) != std::string::npos) {
            size_t q = t.find('"', p + 1);
            if (q == std::string::npos) break;
            std::string tok = TrimTag(t.substr(p + 1, q - p - 1));
            if (!tok.empty()) out.push_back(tok);
            p = q + 1;
        }
        return out;
    }
    // 去一层引号 "en-US,en"
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
        t = t.substr(1, t.size() - 2);
    size_t p = 0;
    while (p <= t.size()) {
        size_t e = t.find_first_of(",;\n", p);
        std::string tok = TrimTag(t.substr(p, e == std::string::npos ? e : e - p));
        if (!tok.empty()) out.push_back(tok);
        if (e == std::string::npos) break;
        p = e + 1;
    }
    return out;
}
void FpCompatiLangs(std::vector<std::string>& e) {
    if (e.empty()) return;
    auto has = [&](const std::string& x) {
        for (auto& y : e) if (y == x) return true; return false; };
    auto base = [](const std::string& s) -> std::string {
        size_t d = s.find_first_of("-_"); return (d == std::string::npos) ? s : s.substr(0, d); };
    // i 表：[zh-HK->zh-TW][en->en-US][pt->pt-BR][es->es-ES]
    const char* pairs[][2] = { {"zh-HK","zh-TW"},{"en","en-US"},{"pt","pt-BR"},{"es","es-ES"} };
    for (auto& pr : pairs) {
        if (has(pr[0]) && !has(pr[1])) e.push_back(pr[1]);
    }
    // o 表：[/en-(?!US)/->en-GB][/pt-(?!PT)/->pt-BR][/es-(?!ES)/->es-MX]
    struct ReMap { const char* pat; const char* val; };
    // 简化为前缀判断（与官方正则等价，覆盖常见情形）
    auto matchEn = [](const std::string& s) {
        return (s.compare(0, 3, "en-") == 0 && s != "en-US"); };
    auto matchPt = [](const std::string& s) {
        return (s.compare(0, 3, "pt-") == 0 && s != "pt-PT" && s != "pt-BR"); };
    auto matchEs = [](const std::string& s) {
        return (s.compare(0, 3, "es-") == 0 && s != "es-ES"); };
    bool needGB = false, needBR = false, needMX = false;
    for (auto& s : e) {
        if (matchEn(s)) needGB = true;
        if (matchPt(s)) needBR = true;
        if (matchEs(s)) needMX = true;
    }
    if (needGB && !has("en-GB")) e.push_back("en-GB");
    if (needBR && !has("pt-BR")) e.push_back("pt-BR");
    if (needMX && !has("es-MX")) e.push_back("es-MX");
    // 基语补齐：首项基语若不在列表则追加
    std::string b0 = base(e[0]);
    if (!has(b0)) e.push_back(b0);
}
std::string FpGetUILanguage(std::vector<std::string>& langs) {
    static const char* kSup[] = {
        "ar","am","et","bg","pl","fa","da","de","de-AT","de-DE","de-LI","de-CH","ru",
        "fr","fr-FR","fr-CA","fr-CH","fil","fi","gu","ko","nl","ca","cs","kn","hr",
        "lv","lt","ro","mr","ml","ms","bn","af","pt","pt-BR","pt-PT","ja","sv","sr",
        "nb","sk","sl","sw","te","ta","th","tr","ur","uk","es","es-AR","es-CO","es-CR",
        "es-HN","es-419","es-US","es-PE","es-MX","es-VE","es-UY","es-ES","es-CL","he",
        "el","hu","it","it-CH","it-IT","hi","id","en","en-IE","en-AU","en-CA","en-US",
        "en-ZA","en-NZ","en-IN","en-GB-oxendict","en-GB","vi","zh-TW","zh-CN"
    };
    for (auto& cur : langs) {
        for (auto s : kSup) { if (cur == s) return cur; }
    }
    langs.push_back("en-US");
    return "en-US";
}
std::string FpSingleLangTag(const std::string& raw) {
    std::string t = TrimTag(raw);
    if (t.empty() || t == "\"\"" || t == "[]") return "";
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
        t = t.substr(1, t.size() - 2);
    size_t e = t.find_first_of(",;\n \t");
    std::string first = TrimTag(e == std::string::npos ? t : t.substr(0, e));
    // 兼容数组残留引号
    if (first.size() >= 2 && first.front() == '"' && first.back() == '"')
        first = first.substr(1, first.size() - 2);
    return TrimTag(first);
}
std::string FpResolveLangArg(const std::string& extraSunParamsJson, const std::string& staticJson) {
    // 语言列表：uiExtra.language 优先，否则 static.Langs
    std::vector<std::string> langs = FpParseLangList(FpJsonGet(extraSunParamsJson, "language"));
    if (langs.empty())
        langs = FpParseLangList(FpJsonGet(staticJson, "Langs"));
    if (langs.empty()) { langs.push_back("en-US"); }
    FpCompatiLangs(langs);
    std::string uiDefault = FpGetUILanguage(langs);
    // pageLanguageSwitch：uiExtra 优先（"0"=自定义，"1"=跟随），缺失看 uiLang
    std::string sw = FpSingleLangTag(FpJsonGet(extraSunParamsJson, "pageLanguageSwitch"));
    if (sw.empty()) {
        std::string ul = FpSingleLangTag(FpJsonGet(extraSunParamsJson, "uiLang"));
        if (ul == "custom") sw = "0";
        else if (ul == "follow_lang" || ul == "follow-lang") sw = "1";
    }
    if (sw == "0") {
        std::string pg = FpSingleLangTag(FpJsonGet(extraSunParamsJson, "pageLanguage"));
        if (pg.empty() || pg == "native") return uiDefault;
        return pg;
    }
    return uiDefault;
}

// ================= 极简顶层 JSON 键值 =================
// 跳过字符串/嵌套找顶层逗号与括号，够用即可；解析失败返回 "" 不抛异常。
static size_t SkipWs(const std::string& j, size_t p) {
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\r' || j[p] == '\n')) p++;
    return p;
}
static size_t SkipValue(const std::string& j, size_t p) {
    p = SkipWs(j, p);
    if (p >= j.size()) return p;
    if (j[p] == '"') {
        p++;
        while (p < j.size()) {
            if (j[p] == '\\') { p += 2; continue; }
            if (j[p] == '"') return p + 1;
            p++;
        }
        return p;
    }
    if (j[p] == '{' || j[p] == '[') {
        char open = j[p], close = (j[p] == '{') ? '}' : ']';
        int depth = 0;
        bool inStr = false;
        for (; p < j.size(); p++) {
            if (inStr) {
                if (j[p] == '\\') p++;
                else if (j[p] == '"') inStr = false;
            } else {
                if (j[p] == '"') inStr = true;
                else if (j[p] == open) depth++;
                else if (j[p] == close) { depth--; if (depth == 0) return p + 1; }
            }
        }
        return p;
    }
    while (p < j.size() && j[p] != ',' && j[p] != '}' && j[p] != ']') p++;
    // 去尾空白
    size_t e = p;
    while (e > 0 && (j[e - 1] == ' ' || j[e - 1] == '\t' || j[e - 1] == '\r' || j[e - 1] == '\n')) e--;
    return e;
}
std::string FpJsonGet(const std::string& json, const std::string& key) {
    size_t p = SkipWs(json, 0);
    if (p >= json.size() || json[p] != '{') return "";
    p++;
    std::string q = "\"" + key + "\"";
    while (p < json.size()) {
        p = SkipWs(json, p);
        if (p >= json.size() || json[p] == '}') break;
        if (json.compare(p, q.size(), q) == 0) {
            p += q.size();
            p = SkipWs(json, p);
            if (p < json.size() && json[p] == ':') {
                p++;
                size_t vs = SkipWs(json, p);
                size_t ve = SkipValue(json, vs);
                return json.substr(vs, ve - vs);
            }
            return "";
        }
        // 跳过 key: value
        p = SkipValue(json, p);                       // key
        p = SkipWs(json, p);
        if (p < json.size() && json[p] == ':') p = SkipValue(json, p + 1);  // value
        p = SkipWs(json, p);
        if (p < json.size() && json[p] == ',') p++;
    }
    return "";
}
static std::string JsonEscapeStr(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}
std::string FpJsonSet(const std::string& json, const std::string& key, const std::string& valueRaw) {
    if (valueRaw.empty()) return "";
    size_t p = SkipWs(json, 0);
    if (p >= json.size() || json[p] != '{') return "";
    std::string q = "\"" + key + "\"";
    p++;
    while (p < json.size()) {
        size_t sp = SkipWs(json, p);
        if (sp >= json.size() || json[sp] == '}') break;
        if (json.compare(sp, q.size(), q) == 0) {
            size_t vp = sp + q.size();
            vp = SkipWs(json, vp);
            if (vp < json.size() && json[vp] == ':') {
                size_t vs = SkipWs(json, vp + 1);
                size_t ve = SkipValue(json, vs);
                std::string o = json;
                o.replace(vs, ve - vs, valueRaw);
                return o;
            }
            return "";
        }
        p = SkipValue(json, sp);
        p = SkipWs(json, p);
        if (p < json.size() && json[p] == ':') p = SkipValue(json, p + 1);
        p = SkipWs(json, p);
        if (p < json.size() && json[p] == ',') p++;
    }
    // key 不存在：追加到 } 之前
    size_t end = json.find_last_of('}');
    if (end == std::string::npos) return "";
    // 判断是否空对象
    bool emptyObj = true;
    for (size_t i = 0; i < end; i++) {
        char c = json[i];
        if (c != '{' && c != ' ' && c != '\t' && c != '\r' && c != '\n') { emptyObj = false; break; }
    }
    std::string o = json;
    o.insert(end, (emptyObj ? "" : ",") + q + ":" + valueRaw);
    (void)JsonEscapeStr;
    return o;
}

static std::wstring FpWebGlProfilePath(const std::wstring& profileDir,
    const std::string& fbcc, const std::string& staticJson) {
    std::string platformRaw = FpJsonGet(staticJson, "Platform");
    std::string platform = platformRaw.empty() ? "Win32" : N(FpJsonUnquote(platformRaw));
    return profileDir + L"\\" + W(FpMd5Hex(fbcc + "_webgl") + "_" + FpBrowserPlatformTag(platform));
}

// 起始页：读 exe 同目录 Config.json 顶层 start_url（第二个顶层参数，手改即生效、全局）。
// 缺失/非 http(s)/含空白（空格会打断命令行 tokenization）一律回落 about:blank，
// 因此旧 Config.json 不加该键也能照常启动。
static std::wstring FpStartUrl() {
    std::string txt;
    if (!FpReadTextFile(AppDir() + L"\\Config.json", txt) || txt.empty()) return L"about:blank";
    const std::string raw = FpJsonGet(txt, "start_url");
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return L"about:blank";
    const std::string src = raw.substr(1, raw.size() - 2);
    std::string u;
    for (size_t i = 0; i < src.size(); i++) { // 反转义 \" \\（路径/查询串常见）
        if (src[i] == '\\' && i + 1 < src.size()) { u += src[i + 1]; i++; }
        else u += src[i];
    }
    size_t a = u.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return L"about:blank";
    u = u.substr(a, u.find_last_not_of(" \t\r\n") - a + 1);
    for (char c : u)
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return L"about:blank"; // 内部空白会断行
    std::string head = u.substr(0, 8);
    for (auto& c : head) c = static_cast<char>(towlower(static_cast<unsigned char>(c)));
    const bool ok = (head == "https://") ||
        (head.size() >= 7 && head.substr(0, 7) == "http://");
    if (!ok) return L"about:blank";
    return W(u);
}


// ================= 启动命令行组装 =================
// 冲突规则（以缓存为准，见 fingerprint.h）：
//  - UserId / ProxyChain / DeviceName / MacAddress / MediaDevices 等：只从 static 文件读，
//    extraSunParamsJson 里即使给了同名键也不覆盖（白名单之外的键才合并）。
//  - CanvasMark/WebGLMark 等运行时噪声：离线无云端 canvasId，固定回退 fbccId，不接受外部覆盖。
//  - CLIENT_HOST cookie（指向已死的 20725）：启动参数层面不处理，由 UI 导出/导入时剥离；
//    此处仅透传 CookiesFile 路径，SunBrowser 自己会重写 BROWSER_ID。
std::wstring FpBuildCmdline(const std::wstring& profileDir, int port,
    const std::string& extraSunParamsJson) {
    std::string profileName = N(profileDir.substr(profileDir.find_last_of(L"\\/") + 1));
    std::string fbcc = FpFbccIdOf(profileDir.substr(profileDir.find_last_of(L"\\/") + 1));

    std::string staticJson, dynamicJson;
    FpLoadStaticJson(profileDir, staticJson);
    FpLoadDynamicJson(profileDir, dynamicJson);

    // UserId：只从 static 取；取不到则用 fbcc hash 兜底（保证确定性，不用随机数污染指纹）
    std::string userId = FpJsonGet(staticJson, "UserId");
    if (userId.empty()) {
        unsigned h = 0;
        for (char c : fbcc) h = h * 131 + (unsigned char)c;
        userId = std::to_string(h % 900000 + 100000);
    }
    std::wstring wStatic = profileDir + L"\\" + W(FpStaticName(fbcc));
    std::wstring wDynamic = profileDir + L"\\" + W(FpDynamicName(fbcc));
    std::wstring wCookies = profileDir + L"\\" + W(FpCookiesName(fbcc));
    DWORD attr = ::GetFileAttributesW(wCookies.c_str());

    // 最小 sunBrowserParams：身份 + 三文件指针 + 确定性噪声种子（= fbccId，与官方回退一致）
    // 官方 setSunflowerBrowserHeader：IS_SUNFLOWER_BROWSER_BASE64 非 true 时另传
    //   --UserId=<browserHead> 明文开关；离线默认走 ext 内 UserId，不单独加该开关
    //   （browserHead 即 static.UserId，ext 内已含，见 diag ext.decode）。
    // 代理（官方 setProxy 语义，main.min.js 实测原文）：
    //   非 CANVAS 模式 -> 命令行追加 --proxy-server=<scheme>://<host>:<port>
    //   （socks5 且有账号时 user:pass@ 段按 C1/C2 换表解码；离线存明文，直拼）；
    //   CANVAS 模式 -> ProxyUser/ProxyPassword 进 ext（本函数 kExtraAllow 已放行）。
    //   static.ProxyChain 数组由保存链路写（FpFormToFpConfig 组装），此处不重复组装。
    std::string proxyArg;
    {
        std::string scheme, host, portStr, user, pass;
        if (!extraSunParamsJson.empty()) {
            std::string v;
            v = FpJsonGet(extraSunParamsJson, "proxyType");
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                scheme = v.substr(1, v.size() - 2);
            v = FpJsonGet(extraSunParamsJson, "proxyHost");
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                scheme.empty(), host = v.substr(1, v.size() - 2);
            else if (!v.empty() && v != "\"\"") host = v;
            v = FpJsonGet(extraSunParamsJson, "proxyPort");
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                portStr = v.substr(1, v.size() - 2);
            else if (!v.empty() && v != "\"\"") portStr = v;
            v = FpJsonGet(extraSunParamsJson, "proxyUser");
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                user = v.substr(1, v.size() - 2);
            v = FpJsonGet(extraSunParamsJson, "proxyPass");
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                pass = v.substr(1, v.size() - 2);
        }
        // 后备：uiExtra 为空但 static 有 ProxyChain（旧存档/官方同步目录）-> 取第一项
        if ((scheme.empty() || host.empty() || portStr.empty()) && !staticJson.empty()) {
            std::string pc = FpJsonGet(staticJson, "ProxyChain");
            if (!pc.empty() && pc.front() == '[') {
                size_t b = pc.find('{');
                size_t e = pc.find('}');
                if (b != std::string::npos && e != std::string::npos && e > b) {
                    std::string o = pc.substr(b, e - b + 1);
                    std::string v;
                    if (scheme.empty()) {
                        v = FpJsonGet(o, "scheme");
                        if (v.size() >= 2 && v.front() == '"') scheme = v.substr(1, v.size() - 2);
                    }
                    if (host.empty()) {
                        v = FpJsonGet(o, "host");
                        if (v.size() >= 2 && v.front() == '"') host = v.substr(1, v.size() - 2);
                    }
                    if (portStr.empty()) {
                        v = FpJsonGet(o, "port");
                        if (v.size() >= 2 && v.front() == '"') portStr = v.substr(1, v.size() - 2);
                        else if (!v.empty()) portStr = v;
                    }
                    if (user.empty()) {
                        v = FpJsonGet(o, "account");
                        if (v.size() >= 2 && v.front() == '"') user = v.substr(1, v.size() - 2);
                    }
                    if (pass.empty()) {
                        v = FpJsonGet(o, "password");
                        if (v.size() >= 2 && v.front() == '"') pass = v.substr(1, v.size() - 2);
                    }
                }
            }
        }
        // noProxy/空host/空port -> 直连，不拼开关（与官方 m!==noProxy 分支一致）
        if (!scheme.empty() && scheme != "noProxy" && scheme != "noproxy" &&
            !host.empty() && !portStr.empty()) {
            if (scheme == "socks5" && !user.empty()) {
                proxyArg = "--proxy-server=" + scheme + "://" + user + ":" + pass + "@" + host + ":" + portStr;
            } else {
                proxyArg = "--proxy-server=" + scheme + "://" + host + ":" + portStr;
            }
        }
    }
    std::string sp = "{\"UserId\":" + userId +
        ",\"StaticConfig\":\"" + JsonEscapeStr(N(wStatic)) +
        "\",\"DynamicConfig\":\"" + JsonEscapeStr(N(wDynamic)) + "\"";
    // 官方 WebRTCTask 同时写 sunBrowserParams.WebRTCAddress/DisableWebRTC 与 DynamicConfig。
    // 仅更新三件套文件不够：SunBrowser 的 ICE 伪装读取 ext 顶层这两个 sunBrowserParams 键。
    {
        auto unquote = [](std::string v) {
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                return v.substr(1, v.size() - 2);
            return v;
        };
        std::string mode = unquote(FpJsonGet(staticJson, "webrtc"));
        std::string ipRaw = FpJsonGet(staticJson, "WebRTCAddress");
        if (ipRaw.empty() || ipRaw == "\"\"") ipRaw = FpJsonGet(dynamicJson, "WebRTCAddress");
        std::string ip = unquote(ipRaw);
        if (mode.empty()) {
            std::string disabled = FpJsonGet(staticJson, "DisableWebRTC");
            if (disabled.empty()) disabled = FpJsonGet(dynamicJson, "DisableWebRTC");
            if (disabled == "true" || disabled == "\"true\"") mode = "disabled";
            else mode = ip.empty() ? "forward" : "proxy";
        }
        const FpWebRtcResolution rtc = FpResolveWebRtc(W(mode), W(ip));
        const std::string rtcParams = FpBuildWebRtcSunParams(rtc);
        if (rtcParams.size() >= 2)
            sp += "," + rtcParams.substr(1, rtcParams.size() - 2);
    }
    // official setTimezone 写 sunBrowserParams.TimeZone；static 中的 timezone 小写键
    // 单独存在不会让浏览器 timezone override 生效。
    {
        std::string autoMode = FpJsonGet(staticJson, "automatic_timezone");
        if (autoMode.empty()) autoMode = FpJsonGet(staticJson, "tzAuto");
        if (autoMode.empty()) autoMode = FpJsonGet(dynamicJson, "automatic_timezone");
        if (autoMode.empty()) autoMode = FpJsonGet(dynamicJson, "tzAuto");
        const bool timezoneByIp = (autoMode == "1" || autoMode == "\"1\"");
        std::string raw = FpJsonGet(staticJson, "TimeZone");
        if (raw.empty() || raw == "\"\"") raw = FpJsonGet(dynamicJson, "TimeZone");
        if ((raw.empty() || raw == "\"\"") && !timezoneByIp) {
            raw = FpJsonGet(staticJson, "timezone");
            if (raw.empty() || raw == "\"\"") raw = FpJsonGet(dynamicJson, "timezone");
        }
        if (!raw.empty() && raw != "\"\"") {
            const std::string timezone = FpNormalizeTimezone(N(FpJsonUnquote(raw)));
            const std::string fragment = FpBuildTimeZoneSunParam(timezone);
            if (!fragment.empty()) sp += "," + fragment;
        }
    }
    // official WebGLTask writes a per-profile WebGLFP JSON file and injects its path
    // in sunBrowserParams. A lowercase static webgl_config alone is not consumed by WebGL.
    {
        std::string mode = N(FpJsonUnquote(FpJsonGet(staticJson, "webgl")));
        std::wstring webglPath = FpWebGlProfilePath(profileDir, fbcc, staticJson);
        bool useWebglFile = false;
        if (mode == "2" || mode == "3") {
            std::string cfg = FpJsonGet(staticJson, "webgl_config");
            std::string webgpu = FpJsonGet(cfg, "webgpu");
            auto strVal = [](const std::string& j, const char* key) {
                return N(FpJsonUnquote(FpJsonGet(j, key))); };
            std::string json = FpBuildWebGlConfigJson(
                strVal(cfg, "unmasked_vendor"), strVal(cfg, "unmasked_renderer"),
                strVal(webgpu, "webgpu_switch"), strVal(webgpu, "gpu_adapterinfo_vendor"),
                strVal(webgpu, "gpu_adapterinfo_architecture"));
            if (!json.empty()) {
                if (FpWriteTextFile(webglPath, json)) {
                    useWebglFile = true;
                    LOG(L"WebGLFP 配置已生成 " + webglPath);
                } else {
                    LOG(L"WebGLFP 配置写入失败 " + webglPath);
                }
            }
        }
        // ui-less/official profiles may have only their already generated WebGLFP file.
        if (mode != "0" && !useWebglFile &&
            ::GetFileAttributesW(webglPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            useWebglFile = true;
        if (useWebglFile)
            sp += ",\"WebGLFP\":\"" + JsonEscapeStr(N(webglPath)) + "\"";
    }
    if (attr != INVALID_FILE_ATTRIBUTES)
        sp += ",\"CookiesFile\":\"" + JsonEscapeStr(N(wCookies)) + "\"";
    // ==== official set* 语义：按开关条件注入（此前无条件注入导致噪声关不掉、
    //      DNT/端口/地理/CPU/RAM/屏幕/平台/Flash 等从不进 ext） ====
    {
        auto raw = [&](const std::string& j, const char* k) { return FpJsonGet(j, k); };
        auto hasMark = [&](const char* k) {
            const std::string v = raw(staticJson, k);
            return !v.empty() && v != "\"\"" && v != "\"0\"" && v != "0";
        };
        auto unq = [](const std::string& v) { return N(FpJsonUnquote(v)); };
        const bool canvas = FpResolveNoiseSwitch(raw(extraSunParamsJson, "canvas"),
            raw(staticJson, "canvas"), hasMark("CanvasMark")) == "1";
        const bool webglImg = FpResolveNoiseSwitch(raw(extraSunParamsJson, "webglImage"),
            raw(staticJson, "webgl_image"), hasMark("WebGLMark")) == "1";
        const bool audio = FpResolveNoiseSwitch(raw(extraSunParamsJson, "audio"),
            raw(staticJson, "AudioFp"), hasMark("AudioFp")) == "1";
        const bool rect = FpResolveNoiseSwitch(raw(extraSunParamsJson, "clientRects"),
            raw(staticJson, "ClientRectFp"), hasMark("ClientRectFp")) == "1";
        // 种子优先沿用官方已有值；缺失才按 fbcc 派生（ClientRectFp 数值域与官方一致）
        std::string audioSeed = unq(raw(staticJson, "AudioFp"));
        if (audioSeed.empty() || audioSeed == "0" || audioSeed == "default")
            audioSeed = std::to_string(FpSeedFromFbcc(fbcc, 1, 9999));
        std::string rectSeed = unq(raw(staticJson, "ClientRectFp"));
        if (rectSeed.empty() || rectSeed == "default")
            rectSeed = std::to_string(FpSeedFromFbcc(fbcc, -10000, 9999));
        const std::string noise = FpBuildNoiseSunParams(canvas, webglImg, audio, rect,
            fbcc, audioSeed, rectSeed);
        if (noise.size() >= 2) sp += "," + noise.substr(1, noise.size() - 2);
        // DoNotTrack（official setDoNotTrack -> EnableDoNotTrack:true）
        std::string dnt = unq(raw(extraSunParamsJson, "do_not_track"));
        if (dnt.empty()) dnt = unq(raw(staticJson, "do_not_track"));
        if (dnt == "true") sp += ",\"EnableDoNotTrack\":true";
        // 端口扫描（official setScanPort：0=禁扫描；否则为白名单字符串）
        std::string allow = unq(raw(staticJson, "AllowScanPorts"));
        if (allow.empty()) allow = unq(raw(staticJson, "allow_scan_ports"));
        if (allow == "0") sp += ",\"AllowScanPorts\":\"0\"";
        else if (!allow.empty()) sp += ",\"AllowScanPorts\":\"" + JsonEscapeStr(allow) + "\"";
        // 地理（official setGEO -> GeolocationSetting）
        std::string geo = unq(raw(staticJson, "GeolocationSetting"));
        if (geo.empty()) geo = unq(raw(extraSunParamsJson, "location"));
        if (geo == "ask" || geo == "allow" || geo == "block")
            sp += ",\"GeolocationSetting\":\"" + geo + "\"";
        // CPU/RAM（official setDoNotTrack 内的两键，官方类型为数字；default=真实值不注入）
        const std::string cpu = unq(raw(staticJson, "HardwareConcurrency"));
        if (!cpu.empty() && cpu != "default") sp += ",\"HardwareConcurrency\":" + cpu;
        const std::string ram = unq(raw(staticJson, "DeviceMemory"));
        if (!ram.empty() && ram != "default") sp += ",\"DeviceMemory\":" + ram;
        // 屏幕（official setScreenResolution -> ScreenSize，"1920_1080" -> "1920,1080"）
        std::string res = unq(raw(extraSunParamsJson, "screenResolution"));
        if (res.empty()) res = unq(raw(staticJson, "ScreenSize"));
        if (!res.empty() && res != "none") {
            for (auto& c : res) if (c == '_') c = ',';
            sp += ",\"ScreenSize\":\"" + JsonEscapeStr(res) + "\"";
        }
        // 平台：official setMaxTouchPoints -> sunBrowserParams.Platform = initBrowser.platform
        // （取值集与指纹页“系统”同源：Win32/MacIntel/Linux x86_64/Linux armv8I/iPhone）
        const std::string osKey = unq(raw(extraSunParamsJson, "os"));
        const std::string platform = osKey.empty()
            ? unq(raw(staticJson, "Platform"))
            : FpOsToOfficialPlatform(osKey);
        if (!platform.empty())
            sp += ",\"Platform\":\"" + JsonEscapeStr(platform) + "\"";
        // Flash（official setFlash：桌面平台且非 off 才注入）
        const bool desktop = platform.empty() || platform == "Win32" || platform == "MacIntel";
        std::string flash = unq(raw(extraSunParamsJson, "flash"));
        if (flash.empty()) flash = unq(raw(staticJson, "FlashPluginSetting"));
        if (desktop && (flash == "allow" || flash == "block"))
            sp += ",\"FlashPluginSetting\":\"" + flash + "\"";
    }
    // extra 白名单合并：只允许官方 static 之外的、且非保护键的顶层键进入。
    // 白名单（与官方 sunBrowserParams 顶层键对齐，非 ui 存档全量字段）：
    // 仅 mergedExtra 显式允许的键可进 ext；ui_fingerprint.json 的 cookie/ua/lang/
    // webglConfig 等大字段一律不进 ext（ext 只传三文件路径指针，不内联指纹内容）。
    // 背景：err=206（命令行超 32767）根因即全量 uiExtra（cookie 16KB 等）被并入 ext；
    // k1c6pr18 保存后 ext 达 39KB 直接 CreateProcess 失败，k1gyly5t 未保存故 ext=468 正常。
    static const char* kProtected[] = { "UserId","StaticConfig","DynamicConfig","CookiesFile",
        "CanvasMark","WebGLMark","AudioFp","ClientRectFp","TimeZone","Geoposition",
        "WebRTCAddress","DisableWebRTC","ProxyChain","DeviceName","MacAddress",
        "MediaDevices","TTSEngines","Langs","AcceptLang","WebGLFP", NULL };
    static const char* kExtraAllow[] = {
        // 官方 sunBrowserParams 常用小标量（与 main.min.js set* 系列写入键对齐）
        "DisableContainer","LoadExtensionErrorBox","ForceProcessExit","StartTime",
        "DisableBackgroundMode",
        "Platform","Vendor","ScreenSize","HardwareConcurrency","DeviceMemory",
        "EnableDoNotTrack","FlashPluginSetting","FlashPluginPath","MaxTouchPoints",
        "NewMobileMode","MobileModeFixedResolution","DisabledFonts","AllowScanPorts",
        "GeolocationSetting","ProxyUser","ProxyPassword","WebRTCLocalAddress",
        "WebRTCStun","WebRTCTurn","ClientRectFp","AudioFp","TimeZone","Geoposition",
        "WebRTCAddress","DisableWebRTC", NULL };
    if (!extraSunParamsJson.empty()) {
        // 粗解析顶层 key: value，白名单命中且非保护键才 FpJsonSet 并入
        size_t q = SkipWs(extraSunParamsJson, 0);
        if (q < extraSunParamsJson.size() && extraSunParamsJson[q] == '{') {
            size_t r = q + 1;
            while (r < extraSunParamsJson.size()) {
                r = SkipWs(extraSunParamsJson, r);
                if (r >= extraSunParamsJson.size() || extraSunParamsJson[r] == '}') break;
                if (extraSunParamsJson[r] != '"') break;
                size_t ke = extraSunParamsJson.find('"', r + 1);
                if (ke == std::string::npos) break;
                std::string k = extraSunParamsJson.substr(r + 1, ke - r - 1);
                size_t c = SkipWs(extraSunParamsJson, ke + 1);
                if (c >= extraSunParamsJson.size() || extraSunParamsJson[c] != ':') break;
                size_t vs = SkipWs(extraSunParamsJson, c + 1);
                size_t ve = SkipValue(extraSunParamsJson, vs);
                std::string v = extraSunParamsJson.substr(vs, ve - vs);
                bool prot = false;
                for (int i = 0; kProtected[i]; i++) {
                    if (k == kProtected[i]) { prot = true; break; }
                }
                bool allowed = false;
                if (!prot) {
                    for (int i = 0; kExtraAllow[i]; i++) {
                        if (k == kExtraAllow[i]) { allowed = true; break; }
                    }
                }
                // 白名单外键直接丢弃（cookie/ua/language/webglConfig/uiLang 等大字段止于 ui 存档）
                if (allowed && !v.empty() && v.size() <= 512) {
                    std::string merged = FpJsonSet(sp + "}", k, v);
                    if (!merged.empty()) sp = merged.substr(0, merged.size() - 1);
                }
                r = SkipWs(extraSunParamsJson, ve);
                if (r < extraSunParamsJson.size() && extraSunParamsJson[r] == ',') r++;
            }
        }
    }
    sp += "}";
    (void)profileName; (void)dynamicJson;

    std::string ext = FpEncode(sp);
    // 官方启动开关（main.min.js 实测原文）：
    //  - buildLaunchOpt：p=["--protected-disable-safe-open","--remote-debugging-port=0"]，
    //    即官方用 --remote-debugging-port=0（随机端口）+ --protected-disable-safe-open；
    //  - setSandbox（IS_SUNFLOWER_BROWSER=true）：browserVersion>=20251127 且
    //    kernelSandboxMode 为空时追加 --no-sandbox --disable-setuid-sandbox；
    //  - setSunflowerBrowserHeader 另有 --disable-background-mode（仅非 BASE64 模式），
    //    ext 模式不加，保持与 diag ext.decode=OK 的注入体一致。
    // 离线等价行为：固定端口改随机 0（消端口占用竞态）+ 补 safe-open/sandbox 两组。
    // 注意：--enable-logging=stderr 是子进程控制台窗口的直接来源（stderr 建 console
    // 输出，主进程 SW_HIDE 压不住孙进程自建窗口）。官方 main.min.js 全文无此开关，
    // puppeteer 默认也不带。离线诊断需要时才加：uiExtra 含 "debugConsole":true 则保留，
    // 否则默认去掉，只留一个浏览器主窗口。
    bool wantConsole = (extraSunParamsJson.find("\"debugConsole\"") != std::string::npos);
    std::wstring cmd = L"--user-data-dir=\"" + profileDir +
        L"\" --profile-directory=Default --remote-debugging-port=0"
        L" --no-first-run --no-default-browser-check --no-sandbox --disable-setuid-sandbox"
        L" --protected-disable-safe-open";
    if (!proxyArg.empty())
        cmd += L" " + W(proxyArg); // 代理开关（官方 setProxy 非 CANVAS 分支；密码不记 diag）
    // 官方 setProxy 同步追加（main.min.js 实测原文）：代理直连云端域名不过代理，
    // C=["https://download.adspower.net","start.adspower.net","sys.adspower.net"]
    // (+ignoreAgentConfig/+*.fbcdn.net)。离线固定三项即可；缺了它会导致 localhost/
    // DevTools 走代理回环失败（127.0.0.1:1200 类本地代理最敏感）。
    if (!proxyArg.empty())
        cmd += L" --proxy-bypass-list=https://download.adspower.net;start.adspower.net;sys.adspower.net";
    // 语言：官方 LanguageTask.setUILanguage win32 分支必推 --lang=<单tag>。
    // 缺了它浏览器 UI 跟随系统（中文系统即显示中文），与指纹语言列表脱节。
    // 推导见 FpResolveLangArg：switch==1 用 getUILanguage(language)，==0 用 pageLanguage单tag。
    {
        std::string langArg = FpResolveLangArg(extraSunParamsJson, staticJson);
        if (!langArg.empty())
            cmd += L" --lang=" + W(langArg);
    }
    {
        std::string mode = FpJsonGet(staticJson, "webrtc");
        if (mode == "\"disable_udp\"" || mode == "disable_udp")
            cmd += L" --webrtc-ip-handling-policy=disable_non_proxied_udp";
    }
    // 系统开关的命令行等价实现（official setGPU / setTls / setWebGPU）：
    //  - setGPU: ("0"===gpu && 0==+gpuSwitch || "2"===gpu) -> --disable-gpu
    //  - setTls : tlsSwitch=="1" 且有黑名单 -> --cipher-suite-blacklist=<list>
    //  - setWebGPU: 关闭时追加 WebGPU,WebGPUService 到 --disable-features
    {
        auto uiStr = [&](const char* k) {
            return N(FpJsonUnquote(FpJsonGet(extraSunParamsJson, k)));
        };
        const std::string gpu = uiStr("gpu");
        const std::string gpuSwitch = uiStr("gpuSwitch");
        if (gpu == "2" || (gpu == "0" && gpuSwitch == "0")) cmd += L" --disable-gpu";
        const std::string tlsOn = uiStr("tlsSwitch");
        const std::string tlsList = uiStr("tls");
        if (tlsOn == "1" && !tlsList.empty())
            cmd += L" --cipher-suite-blacklist=" + W(tlsList);
        std::string webgl = N(FpJsonUnquote(FpJsonGet(staticJson, "webgl")));
        if (webgl.empty()) webgl = uiStr("webgl");
        std::string wgSwitch = (uiStr("webgpu") == "disabled") ? "0" : "1";
        std::string wgVendor = uiStr("gpu_adapterinfo_vendor");
        if (wgVendor.empty()) {
            const std::string cfg = FpJsonGet(staticJson, "webgl_config");
            wgVendor = N(FpJsonUnquote(FpJsonGet(FpJsonGet(cfg, "webgpu"),
                "gpu_adapterinfo_vendor")));
        }
        const bool webgpuConfigPresent = (!uiStr("webgpu").empty());
        if (webgpuConfigPresent && (webgl != "0" || wgSwitch == "1") &&
            (wgSwitch == "0" || (wgSwitch == "1" && wgVendor.empty())))
            cmd += L" --disable-features=WebGPU,WebGPUService";
    }
    cmd += L" --extended-parameters=" + W(ext);
    if (wantConsole)
        cmd += L" --enable-logging=stderr --v=0";
    // official mergeBrowserArgs(n.args, r.userArgs, true)：自定义启动参数追加在 URL 之前
    {
        const std::string userArgs = N(FpJsonUnquote(FpJsonGet(extraSunParamsJson, "launchArgs")));
        if (!userArgs.empty()) cmd += L" " + W(userArgs);
    }
    // 起始页：Config.json 顶层 start_url；未配/非法回落 about:blank
    cmd += L" " + FpStartUrl();
    (void)port; // 端口跟随官方：命令行只传 --remote-debugging-port=0，实际值由浏览器随机写 DevToolsActivePort
    return cmd;
}

// FpCmdTooLong：诊断用，ext/命令行是否超限（32767）。返回 true=超限，lenOut=ext 长度。
bool FpCmdTooLong(const std::wstring& cmdline, size_t& lenOut) {
    // 取 --extended-parameters= 值长度 + 固定开销估算
    std::string cmd = N(cmdline);
    const char* k = "--extended-parameters=";
    size_t p = cmd.find(k);
    size_t extLen = 0;
    if (p != std::string::npos) {
        size_t s = p + strlen(k);
        size_t e = cmd.find(' ', s);
        extLen = ((e == std::string::npos) ? cmd.size() : e) - s;
    }
    lenOut = extLen;
    // 整行 32767 上限；ext 超 24000 即预警（exe 路径+三文件路径另占约 1KB）
    return (cmd.size() > 30000) || (extLen > 24000);
}

// ================= 启动诊断（只写 debug.log，不做任何网络 IO） =================
// 缩写说明：diag=诊断块；ud=--user-data-dir；ext=--extended-parameters；
// rdp=--remote-debugging-port；sc=StaticConfig；dc=DynamicConfig；cf=CookiesFile。
bool FpDiagEnvAuth(std::string& detailOut) {
    bool hit = false;
    detailOut.clear();
    // GetEnvironmentVariableA：查当前进程环境，不触碰系统其它位置。
    char v[32768];
    DWORD n = ::GetEnvironmentVariableA("AUTH", v, sizeof(v));
    if (n > 0 && n < sizeof(v)) { hit = true; detailOut += "AUTH(len=" + std::to_string(n) + ") "; }
    n = ::GetEnvironmentVariableA("ELECTRON_RUN_AS_NODE", v, sizeof(v));
    if (n > 0 && n < sizeof(v)) { hit = true; detailOut += "ELECTRON_RUN_AS_NODE(len=" + std::to_string(n) + ") "; }
    if (!hit) detailOut = "none";
    return hit;
}

// 取三件套文件现场：文件名 + 长度 + md5(原文) + 换表解码头（最多 64 字符，截断标 ...）。
// rc: 0=读+解码都成功；1=文件缺失；2=读失败/空；3=解码失败（表错/损坏）；
//     4=明文 JSON（cookies 文件官方就是明文，见 main.min.js setCookie：x(n,JSON.stringify(t))）。
static std::string DiagFileLine(const std::wstring& profileDir,
    const std::string& fileName, const char* tag, int& rcOut) {
    std::wstring path = profileDir + L"\\" + W(fileName);
    DWORD attr = ::GetFileAttributesW(path.c_str());
    std::string line = std::string(tag) + " file=" + fileName;
    if (attr == INVALID_FILE_ATTRIBUTES) {
        rcOut = 1;
        return line + " MISSING";
    }
    std::string raw;
    if (!FpReadTextFile(path, raw) || raw.empty()) {
        rcOut = 2;
        return line + " READ_FAIL len=0";
    }
    line += " len=" + std::to_string(raw.size()) + " md5=" + FpMd5Hex(raw);
    // 明文 JSON 直存（cookies 官方行为）：[{/{" 开头即明文，不是 DECODE_FAIL。
    size_t nz = raw.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (nz != std::string::npos && (raw[nz] == '[' || raw[nz] == '{')) {
        rcOut = 4;
        std::string h = raw.substr(nz, raw.size() - nz > 64 ? 64 : raw.size() - nz);
        for (char& c : h) { if (c == '\r' || c == '\n' || c == '\t') c = ' '; }
        line += " PLAINTEXT(head)=" + h;
        if (raw.size() - nz > 64) line += "...";
        return line;
    }
    std::string head = FpDecode(raw);
    if (head.empty()) {
        rcOut = 3;
        std::string rh = raw.substr(0, raw.size() > 32 ? 32 : raw.size());
        return line + " DECODE_FAIL rawHead=" + rh;
    }
    rcOut = 0;
    std::string h = head.substr(0, head.size() > 64 ? 64 : head.size());
    // 去掉换行，避免诊断块断行。
    for (char& c : h) { if (c == '\r' || c == '\n' || c == '\t') c = ' '; }
    line += " head=" + h;
    if (head.size() > 64) line += "...";
    return line;
}

// 从 cmdline 里按 key= 取值（值到下一个空格或结尾；引号保留原样）。
static std::string DiagArgOf(const std::string& cmd, const char* key) {
    size_t p = cmd.find(key);
    if (p == std::string::npos) return "(missing)";
    size_t s = p + strlen(key);
    size_t e = s;
    if (e < cmd.size() && cmd[e] == '"') {
        e++;
        size_t q = cmd.find('"', e);
        e = (q == std::string::npos) ? cmd.size() : q + 1;
    } else {
        size_t q = cmd.find(' ', e);
        e = (q == std::string::npos) ? cmd.size() : q;
    }
    std::string v = cmd.substr(s, e - s);
    if (v.size() > 160) v = v.substr(0, 64) + "...[" + std::to_string(cmd.substr(s, e - s).size()) + " chars]..." + cmd.substr(e - 32 > s ? e - 32 : s, 32);
    return v;
}

std::string FpDiagDumpLaunch(const std::wstring& exe, const std::wstring& workDir,
    const std::wstring& profileDir, int port,
    const std::string& extraSunParamsJson,
    const std::wstring& cmdline, DWORD pid) {
    std::string profileName = N(profileDir.substr(profileDir.find_last_of(L"\\/") + 1));
    std::string fbcc = FpFbccIdOf(profileDir.substr(profileDir.find_last_of(L"\\/") + 1));
    std::ostringstream o;
    o << "[diag] ===== launch diag begin =====\n";
    o << "[diag] exe=" << N(exe) << "\n";
    o << "[diag] workDir=" << N(workDir) << "\n";
    o << "[diag] profileDir=" << N(profileDir) << "\n";
    o << "[diag] profileName=" << profileName << " fbccId=" << fbcc
      << " port=" << (port > 0 ? std::to_string(port)
                               : std::string("0(官方随机,启动后读DevToolsActivePort)"))
      << " pid=" << (unsigned long)pid << "\n";
    // 三件套现场
    int rcS = -1, rcD = -1, rcC = -1;
    o << "[diag] " << DiagFileLine(profileDir, FpStaticName(fbcc), "sc", rcS)
      << " expect=md5(fbcc+\"_static\")\n";
    o << "[diag] " << DiagFileLine(profileDir, FpDynamicName(fbcc), "dc", rcD)
      << " expect=md5(fbcc+\"_webrtc\")\n";
    o << "[diag] " << DiagFileLine(profileDir, FpCookiesName(fbcc), "cf", rcC)
      << " expect=md5(fbcc+\"_cookies\")\n";
    // cookies 内容现场：编辑框与写库用的就是这份规范化结果（条数一目了然）
    {
        std::string cj;
        if (FpLoadCookiesJson(profileDir, cj) && !cj.empty())
            o << "[diag] cf.json=ok items=" << FpCookieArraySplit(cj).size()
              << " len=" << cj.size() << "\n";
        else
            o << "[diag] cf.json=empty\n";
    }
    // sunBrowserParams 明文重建（与 FpBuildCmdline 同逻辑，只为展示，不替代 ext 真值）
    std::string staticJson, dynamicJson;
    FpLoadStaticJson(profileDir, staticJson);
    FpLoadDynamicJson(profileDir, dynamicJson);
    std::string userId = FpJsonGet(staticJson, "UserId");
    std::string userIdSrc = "static";
    if (userId.empty()) {
        unsigned h = 0;
        for (char c : fbcc) h = h * 131 + (unsigned char)c;
        userId = std::to_string(h % 900000 + 100000);
        userIdSrc = "fallback(hash fbcc)";
    }
    std::string cmdN = N(cmdline);
    // ext 真值：从 cmdline 里抠 --extended-parameters= 之后到空格的值
    std::string extVal;
    {
        const char* k = "--extended-parameters=";
        size_t p = cmdN.find(k);
        if (p != std::string::npos) {
            size_t s = p + strlen(k);
            size_t e = cmdN.find(' ', s);
            extVal = cmdN.substr(s, e == std::string::npos ? e : e - s);
        }
    }
    o << "[diag] sp.UserId=" << userId << " src=" << userIdSrc
      << " staticLoaded=" << (staticJson.empty() ? "no" : "yes")
      << " dynamicLoaded=" << (dynamicJson.empty() ? "no" : "yes")
      << " uiExtraLen=" << extraSunParamsJson.size() << "\n";
    o << "[diag] ext.len=" << extVal.size();
    if (!extVal.empty()) {
        o << " head=" << extVal.substr(0, extVal.size() > 64 ? 64 : extVal.size());
        if (extVal.size() > 128)
            o << " tail=" << extVal.substr(extVal.size() - 64);
        else if (extVal.size() > 64)
            o << "...";
    }
    // ext 解码校验：能解出 {"UserId": 即注入体合法
    if (!extVal.empty()) {
        std::string dec = FpDecode(extVal);
        if (dec.size() > 10 && dec[0] == '{' && dec.find("\"UserId\"") != std::string::npos) {
            std::string dh = dec.substr(0, dec.size() > 96 ? 96 : dec.size());
            for (char& c : dh) { if (c == '\r' || c == '\n' || c == '\t') c = ' '; }
            o << "\n[diag] ext.decode=OK head=" << dh << (dec.size() > 96 ? "..." : "");
            std::string wrDisabled = FpJsonGet(dec, "DisableWebRTC");
            std::string wrAddress = FpJsonGet(dec, "WebRTCAddress");
            o << "\n[diag] ext.webrtc.disabled=" << (wrDisabled.empty() ? "(absent)" : wrDisabled)
              << " address=" << (!wrAddress.empty() && wrAddress != "\"\"" ? "set" : "empty");
            std::string tz = FpJsonGet(dec, "TimeZone");
            std::string webglFp = FpJsonGet(dec, "WebGLFP");
            o << "\n[diag] ext.settings.timezone=" << (tz.empty() || tz == "\"\"" ? "empty" : tz)
              << " webglFP=" << (!webglFp.empty() && webglFp != "\"\"" ? "set" : "empty");
            // 系统/噪声开关的注入现场（开关对不对一眼可见；只回显状态不回显种子值）
            auto hasKey = [&](const char* k) {
                return dec.find(std::string("\"") + k + "\"") != std::string::npos;
            };
            o << "\n[diag] ext.settings canvas=" << (hasKey("CanvasMark") ? "on" : "off")
              << " webglImage=" << (hasKey("WebGLMark") ? "on" : "off")
              << " audio=" << (hasKey("AudioFp") ? "on" : "off")
              << " clientRects=" << (hasKey("ClientRectFp") ? "on" : "off")
              << " dnt=" << (hasKey("EnableDoNotTrack") ? "on" : "off")
              << " scan=" << (hasKey("AllowScanPorts") ? "set" : "-")
              << " geo=" << (hasKey("GeolocationSetting") ? "set" : "-")
              << " cpu=" << (hasKey("HardwareConcurrency") ? "set" : "-")
              << " ram=" << (hasKey("DeviceMemory") ? "set" : "-")
              << " screen=" << (hasKey("ScreenSize") ? "set" : "-")
              << " platform=" << (hasKey("Platform") ? "set" : "-")
              << " flash=" << (hasKey("FlashPluginSetting") ? "set" : "-");
            o << "\n[diag] cmdline gpu="
              << (cmdN.find("--disable-gpu") == std::string::npos ? "off" : "on")
              << " tls=" << (cmdN.find("--cipher-suite-blacklist=") == std::string::npos ? "off" : "on")
              << " webgpuDisabled=" << (cmdN.find("WebGPU,WebGPUService") == std::string::npos ? "no" : "yes")
              << " launchArgs=" << N(FpJsonUnquote(FpJsonGet(extraSunParamsJson, "launchArgs")));
        } else {
            o << "\n[diag] ext.decode=FAIL(!!换表/编码异常，浏览器会拒绝指纹)";
        }
    } else {
        o << "\n[diag] ext.decode=SKIP(empty)";
    }
    o << "\n";
    // 三键逐项展开
    o << "[diag] arg.ud=" << DiagArgOf(cmdN, "--user-data-dir=") << "\n";
    o << "[diag] arg.rdp=" << DiagArgOf(cmdN, "--remote-debugging-port=")
      << "（官方=0随机；若此处非0即偏离官方buildLaunchOpt）\n";
    o << "[diag] arg.safeopen=" << (cmdN.find("--protected-disable-safe-open") == std::string::npos ? "MISSING(偏离官方)" : "present") << "\n";
    o << "[diag] arg.nosandbox=" << (cmdN.find("--no-sandbox") == std::string::npos ? "MISSING" : "present") << "\n";
    o << "[diag] start.url=" << N(FpStartUrl()) << "\n";
    {
        std::string mode = FpJsonGet(staticJson, "webrtc");
        std::string staticDisable = FpJsonGet(staticJson, "DisableWebRTC");
        std::string dynamicDisable = FpJsonGet(dynamicJson, "DisableWebRTC");
        std::string staticAddress = FpJsonGet(staticJson, "WebRTCAddress");
        std::string dynamicAddress = FpJsonGet(dynamicJson, "WebRTCAddress");
        o << "[diag] webrtc.mode=" << (mode.empty() ? "(absent)" : mode)
          << " static.disabled=" << (staticDisable.empty() ? "(absent)" : staticDisable)
          << " dynamic.disabled=" << (dynamicDisable.empty() ? "(absent)" : dynamicDisable)
          << " static.address=" << (!staticAddress.empty() && staticAddress != "\"\"" ? "set" : "empty")
          << " dynamic.address=" << (!dynamicAddress.empty() && dynamicAddress != "\"\"" ? "set" : "empty")
          << " udpPolicy=" << (cmdN.find("--webrtc-ip-handling-policy=disable_non_proxied_udp") == std::string::npos ? "absent" : "set")
          << "\n";
    }
    // 代理现场：static.ProxyChain 第一项 + 命令行 --proxy-server 是否生效（密码打码）。
    // 若 uiExtra 有代理但 proxyArg=missing，说明表单值没进 static（保存链路问题）；
    // 若 proxyArg present 但浏览器仍直连，说明 static.ProxyChain 与命令行不一致或代理本身不通。
    {
        std::string pc = FpJsonGet(staticJson, "ProxyChain");
        std::string pcHead = pc.empty() ? "(absent)" : pc.substr(0, pc.size() > 96 ? 96 : pc.size());
        for (char& c : pcHead) { if (c == '\r' || c == '\n' || c == '\t') c = ' '; }
        std::string pa = DiagArgOf(cmdN, "--proxy-server=");
        if (pa != "(missing)") {
            size_t at = pa.find('@');
            if (at != std::string::npos) {
                size_t sc = pa.find("://");
                pa = pa.substr(0, (sc == std::string::npos ? 0 : sc + 3)) + "***@" +
                     pa.substr(at + 1);
            }
        }
        o << "[diag] proxy.static=" << pcHead << "\n";
        o << "[diag] proxy.arg=" << pa << "\n";
        // bypass 现场：缺了它，localhost/DevTools 会被迫走代理（127 类本地代理最敏感）
        o << "[diag] proxy.bypass=" << DiagArgOf(cmdN, "--proxy-bypass-list=") << "\n";
        // 语言现场：三键 + --lang 推导（浏览器中文/英文显示即它决定）
        {
            std::string langRaw = FpJsonGet(extraSunParamsJson, "language");
            if (langRaw.empty()) langRaw = FpJsonGet(staticJson, "Langs");
            std::string accRaw = FpJsonGet(staticJson, "AcceptLang");
            std::string pgSw = FpJsonGet(extraSunParamsJson, "pageLanguageSwitch");
            std::string pg = FpJsonGet(extraSunParamsJson, "pageLanguage");
            std::string langArg = FpResolveLangArg(extraSunParamsJson, staticJson);
            o << "[diag] lang.list=" << (langRaw.empty() ? "(absent)" : langRaw.substr(0, 96)) << "\n";
            o << "[diag] lang.accept=" << (accRaw.empty() ? "(absent)" : accRaw.substr(0, 96)) << "\n";
            o << "[diag] lang.uiSwitch=" << (pgSw.empty() ? "(absent)" : pgSw)
              << " page=" << (pg.empty() ? "(absent)" : pg.substr(0, 48)) << "\n";
            o << "[diag] lang.arg=" << DiagArgOf(cmdN, "--lang=") << " resolve=" << langArg << "\n";
        }
    }
    o << "[diag] arg.ext.present=" << (extVal.empty() ? "no" : "yes") << "\n";
    // 环境变量
    std::string envDetail;
    bool envHit = FpDiagEnvAuth(envDetail);
    o << "[diag] env.AUTH_ELECTRON=" << (envHit ? ("HIT " + envDetail + "(官方filterEnv会删，我方透传)") : "none") << "\n";
    // 失败建议
    if (rcS == 1 || rcD == 1) {
        o << "[diag] hint: 三件套缺失(sc/dc MISSING) -> UserId已回退hash，浏览器可能因StaticConfig路径无效秒退；"
             "先用指纹配置生成三件套再启动。\n";
    }
    if (rcS == 3 || rcD == 3) {
        o << "[diag] hint: static/dynamic DECODE_FAIL -> 文件损坏或换表不对，浏览器拒绝指纹；"
             "从AdsPower官方重导该环境三件套覆盖。\n";
    }
    if (rcC == 4) {
        o << "[diag] hint: cookies=PLAINTEXT(明文JSON) -> 与官方 setCookie 一致，正常，"
             "ext 里只传 CookiesFile 路径，不内联内容。\n";
    } else if (rcC == 3) {
        o << "[diag] hint: cookies DECODE_FAIL -> 既非换表编码也非明文JSON，文件损坏；"
             "删掉该 cookies 文件后重试（启动会跳过 CookiesFile）。\n";
    }
    o << "[diag] hint: 若3秒退出且零[browser]输出 -> GUI子系统无控制台可写，"
         "复制[diag]manual行到cmd手工跑，看弹窗/退出码。\n";
    o << "[diag] manual=" << cmdN << "\n";
    o << "[diag] ===== launch diag end =====";
    return o.str();
}

// ================= 进程树关闭 =================
// 精确匹配：只结束命令行含 --user-data-dir 指向该 profile 的 SunBrowser 进程，
// 不碰其它 profile（多开互不干扰）。实现：NtQueryInformationProcess 读 PEB 命令行，
// 零第三方依赖（ntdll 动态取函数地址，无导入表改动）。
// 回退：命令行读不到时，只结束“启动时间晚于 oldestAllowed”的同名进程兜底。
#include <winternl.h>
typedef NTSTATUS(NTAPI* PFN_NtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
static bool ProcCmdlineHasDir(DWORD pid, const std::wstring& dirLow) {
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    PFN_NtQueryInformationProcess q = (PFN_NtQueryInformationProcess)::GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (!q) return false;
    HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return false;
    bool hit = false;
    PROCESS_BASIC_INFORMATION pbi{};
    ULONG rl = 0;
    if (q(h, ProcessBasicInformation, &pbi, sizeof(pbi), &rl) == 0 && pbi.PebBaseAddress) {
        // PEB+0x20 = ProcessParameters（x64）；UNICODE_STRING CommandLine 在其 +0x70
        PVOID params = NULL;
        SIZE_T got = 0;
        if (::ReadProcessMemory(h, (LPCVOID)((char*)pbi.PebBaseAddress + 0x20), &params, sizeof(params), &got) && params) {
            struct US { USHORT Len, Max; PWSTR Buf; };
            US cmd{};
            if (::ReadProcessMemory(h, (LPCVOID)((char*)params + 0x70), &cmd, sizeof(cmd), &got) &&
                cmd.Buf && cmd.Len > 0 && cmd.Len < 32768) {
                std::wstring s((size_t)(cmd.Len / 2), 0);
                if (::ReadProcessMemory(h, cmd.Buf, &s[0], cmd.Len, &got)) {
                    for (auto& c : s) c = towlower(c);
                    if (s.find(L"--user-data-dir") != std::wstring::npos &&
                        s.find(dirLow) != std::wstring::npos)
                        hit = true;
                }
            }
        }
    }
    ::CloseHandle(h);
    return hit;
}
std::vector<DWORD> FpProfileBrowserProcesses(const std::wstring& profileDir) {
    std::vector<DWORD> found;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return found;
    std::wstring dirLow = profileDir;
    for (auto& c : dirLow) c = towlower(c);

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!::Process32FirstW(snap, &pe)) { ::CloseHandle(snap); return found; }
    do {
        std::wstring exe = pe.szExeFile;
        for (auto& c : exe) c = towlower(c);
        if (exe != L"sunbrowser.exe" && exe != L"chrome.exe") continue;
        if (pe.th32ProcessID <= 4) continue;
        if (ProcCmdlineHasDir(pe.th32ProcessID, dirLow)) found.push_back(pe.th32ProcessID);
    } while (::Process32NextW(snap, &pe));
    ::CloseHandle(snap);
    return found;
}
std::vector<DWORD> FpKillProfileTree(const std::wstring& profileDir) {
    std::vector<DWORD> killed;
    std::vector<DWORD> pids = FpProfileBrowserProcesses(profileDir);
    for (DWORD pid : pids) {
        // 读不到命令行的进程不会进列表（不全杀，避免误伤其它 profile）
        HANDLE h = ::OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, pid);
        if (!h) continue;
        if (::TerminateProcess(h, 0)) killed.push_back(pid);
        ::CloseHandle(h);
    }
    return killed;
}
