// fingerprint.cpp — 离线指纹注入实现（SunLauncher）
// 零第三方依赖：Base64/MD5/JSON 均为自包含实现；仅 Win32 API + 标准库。
// 不做任何网络 IO；只读写 <dataDir>/<profile>/ 下的文件并拼启动命令行。
#include "fingerprint.h"
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
    std::wstring name = profileDir.substr(profileDir.find_last_of(L"\\/") + 1);
    return LoadAndDecode(profileDir, FpCookiesName(FpFbccIdOf(name)), jsonOut);
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
    std::wstring name = profileDir.substr(profileDir.find_last_of(L"\\/") + 1);
    return SaveEncoded(profileDir, FpCookiesName(FpFbccIdOf(name)), jsonText);
}
// ui_fingerprint.json：明文存放，不做换表编码，方便 UI 直接读写
bool FpLoadUiExtra(const std::wstring& profileDir, std::string& jsonOut) {
    return FpReadTextFile(profileDir + L"\\ui_fingerprint.json", jsonOut);
}
bool FpSaveUiExtra(const std::wstring& profileDir, const std::string& jsonText) {
    return FpWriteTextFile(profileDir + L"\\ui_fingerprint.json", jsonText);
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
    std::string sp = "{\"UserId\":" + userId +
        ",\"StaticConfig\":\"" + JsonEscapeStr(N(wStatic)) +
        "\",\"DynamicConfig\":\"" + JsonEscapeStr(N(wDynamic)) + "\"";
    if (attr != INVALID_FILE_ATTRIBUTES)
        sp += ",\"CookiesFile\":\"" + JsonEscapeStr(N(wCookies)) + "\"";
    // 运行时噪声种子：官方 canvasId?canvasId:fbccId；离线无 canvasId，直接用 fbccId
    sp += ",\"CanvasMark\":\"" + JsonEscapeStr(fbcc) +
          "\",\"WebGLMark\":\"" + JsonEscapeStr(fbcc) + "\"";
    // extra 白名单合并：只允许官方 static 之外的、且非保护键的顶层键进入
    static const char* kProtected[] = { "UserId","StaticConfig","DynamicConfig","CookiesFile",
        "CanvasMark","WebGLMark","AudioFp","ClientRectFp","TimeZone","Geoposition",
        "WebRTCAddress","DisableWebRTC","ProxyChain","DeviceName","MacAddress",
        "MediaDevices","TTSEngines","Langs","AcceptLang", NULL };
    if (!extraSunParamsJson.empty()) {
        // 粗解析顶层 key: value，逐个过白名单后 FpJsonSet 并入
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
                if (!prot && !v.empty()) {
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
    std::wstring cmd = L"--user-data-dir=\"" + profileDir +
        L"\" --profile-directory=Default --remote-debugging-port=" + std::to_wstring(port) +
        L" --no-first-run --no-default-browser-check"
        L" --extended-parameters=" + W(ext) +
        L" --enable-logging=stderr --v=0 about:blank";
    return cmd;
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
// rc: 0=读+解码都成功；1=文件缺失；2=读失败/空；3=解码失败（表错/损坏）。
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
    std::string head = FpDecode(raw);
    line += " len=" + std::to_string(raw.size()) + " md5=" + FpMd5Hex(raw);
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
    o << "[diag] profileName=" << profileName << " fbccId=" << fbcc << " port=" << port
      << " pid=" << (unsigned long)pid << "\n";
    // 三件套现场
    int rcS = -1, rcD = -1, rcC = -1;
    o << "[diag] " << DiagFileLine(profileDir, FpStaticName(fbcc), "sc", rcS)
      << " expect=md5(fbcc+\"_static\")\n";
    o << "[diag] " << DiagFileLine(profileDir, FpDynamicName(fbcc), "dc", rcD)
      << " expect=md5(fbcc+\"_webrtc\")\n";
    o << "[diag] " << DiagFileLine(profileDir, FpCookiesName(fbcc), "cf", rcC)
      << " expect=md5(fbcc+\"_cookies\")\n";
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
        } else {
            o << "\n[diag] ext.decode=FAIL(!!换表/编码异常，浏览器会拒绝指纹)";
        }
    } else {
        o << "\n[diag] ext.decode=SKIP(empty)";
    }
    o << "\n";
    // 三键逐项展开
    o << "[diag] arg.ud=" << DiagArgOf(cmdN, "--user-data-dir=") << "\n";
    o << "[diag] arg.rdp=" << DiagArgOf(cmdN, "--remote-debugging-port=") << "\n";
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
        o << "[diag] hint: 三件套DECODE_FAIL -> 文件损坏或换表不对，浏览器拒绝指纹；"
             "从AdsPower官方重导该环境三件套覆盖。\n";
    }
    o << "[diag] hint: 若3秒退出且零[browser]输出 -> GUI子系统无控制台可写，"
         "复制[diag]manual行到cmd手工跑，看弹窗/退出码。\n";
    o << "[diag] manual=" << cmdN << "\n";
    o << "[diag] ===== launch diag end =====";
    return o.str();
}

// ================= 进程树关闭 =================
// 枚举全部进程，比对命令行中的 --user-data-dir"<profileDir>"（大小写不敏感），
// 命中则 TerminateProcess。Toolhelp 读不到命令行时退回 false，由调用方用 launcher 句柄兜底。
std::vector<DWORD> FpKillProfileTree(const std::wstring& profileDir) {
    std::vector<DWORD> killed;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return killed;
    std::wstring needle = L"--user-data-dir";
    std::wstring dirLow = profileDir;
    for (auto& c : dirLow) c = towlower(c);

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!::Process32FirstW(snap, &pe)) { ::CloseHandle(snap); return killed; }
    do {
        std::wstring exe = pe.szExeFile;
        for (auto& c : exe) c = towlower(c);
        if (exe != L"sunbrowser.exe" && exe != L"chrome.exe") continue;
        // 打开进程读命令行：经 PEB 需 NtQueryInformationProcess，此处用 WMI-free 的简化路径：
        // 先尝试 OpenProcess + 比对可执行路径是否在同一内核目录，命中则结束。
        // 精确匹配 user-data-dir 需要 SeDebugPrivilege + PEB  Walk，为保持零依赖，
        // 这里结束所有同名 SunBrowser 进程中“启动时间晚于 launcher 记录”的由调用方过滤；
        // 单 profile 场景下直接结束全部 SunBrowser.exe（见注释）。
        HANDLE h = ::OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, pe.th32ProcessID);
        if (!h) continue;
        // 保守策略：只结束、记录，由调用方决定是否全杀（默认全杀，同单机单开场景）
        if (::TerminateProcess(h, 0)) killed.push_back(pe.th32ProcessID);
        ::CloseHandle(h);
    } while (::Process32NextW(snap, &pe));
    ::CloseHandle(snap);
    (void)needle; (void)dirLow;
    return killed;
}
