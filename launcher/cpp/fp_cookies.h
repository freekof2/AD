#pragma once
// ==== Cookie：指纹编辑框 / 三件套 cookies 文件 / Chromium cookie 库 三处共用 ====
// 本头文件只放纯字符串函数（inline，单测直接 include，不需要 windows 系统 API）；
// 读写 Chromium 库与 DPAPI/AES-GCM 在 fp_cookies.cpp。
//
// 官方 cookie 对象形态（main.min.js parseCookies / parseTxtCookies / getSameSite）：
//   {"name","value","domain","path","httpOnly","secure","session",
//    "expires"(unix 秒),"sameSite"[,"hostOnly"]}
// sameSite 字符串 <-> 库里 samesite 整数（main.min.js h={"-1":"unspecified",
//   0:"no_restriction",1:"lax",2:"strict"}）
// 库里时间列（expires_utc/creation_utc/last_access_utc/last_update_utc）：
//   自 1601-01-01 UTC 起的微秒（Chrome 专用），与 unix 秒按此互转。

#include <string>
#include <vector>
#include <utility>
#include <ctime>

// ---------------- JSON 小工具（够用即可，不引第三方解析器） ----------------
inline std::string FpCookieEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    static const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\b': o += "\\b";  break;
        case '\f': o += "\\f";  break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (c < 0x20) { o += "\\u00"; o += h[c >> 4]; o += h[c & 0xf]; }
            else o += static_cast<char>(c);
        }
    }
    return o;
}

inline std::string FpCookieUnescape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
        char c = s[++i];
        switch (c) {
        case 'n': o += '\n'; break;
        case 'r': o += '\r'; break;
        case 't': o += '\t'; break;
        case 'b': o += '\b'; break;
        case 'f': o += '\f'; break;
        case 'u': {
            // \uXXXX（BMP）-> UTF-8；超出或残缺按原样丢弃该转义
            if (i + 4 < s.size()) {
                unsigned v = 0;
                bool ok = true;
                for (int k = 1; k <= 4; k++) {
                    char d = s[i + k];
                    unsigned dv;
                    if (d >= '0' && d <= '9') dv = d - '0';
                    else if (d >= 'a' && d <= 'f') dv = d - 'a' + 10;
                    else if (d >= 'A' && d <= 'F') dv = d - 'A' + 10;
                    else { ok = false; break; }
                    v = (v << 4) | dv;
                }
                if (ok) {
                    i += 4;
                    if (v < 0x80) o += static_cast<char>(v);
                    else if (v < 0x800) {
                        o += static_cast<char>(0xC0 | (v >> 6));
                        o += static_cast<char>(0x80 | (v & 0x3F));
                    } else {
                        o += static_cast<char>(0xE0 | (v >> 12));
                        o += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
                        o += static_cast<char>(0x80 | (v & 0x3F));
                    }
                } else o += c;
            } else o += c;
            break;
        }
        default: o += c; break;
        }
    }
    return o;
}

// 从 '{' 起找配对的 '}'（跳过字符串与转义），返回结束位置后一个下标。
// 直接 find('}') 会在 value 含 '}' 时截断，故必须扫字符串状态。
inline bool FpCookieScanObject(const std::string& s, size_t openPos, size_t& endOut) {
    if (openPos >= s.size() || s[openPos] != '{') return false;
    int depth = 0;
    bool inStr = false;
    for (size_t i = openPos; i < s.size(); i++) {
        char c = s[i];
        if (inStr) {
            if (c == '\\') { i++; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) { endOut = i + 1; return true; }
        }
    }
    return false;
}

// 顶层数组切元素：返回每个 {...} 的原文（非对象元素忽略，截断数组返回空）
inline std::vector<std::string> FpCookieArraySplit(const std::string& arr) {
    std::vector<std::string> out;
    size_t i = 0, n = arr.size();
    while (i < n && (arr[i] == ' ' || arr[i] == '\t' || arr[i] == '\r' || arr[i] == '\n')) i++;
    if (i >= n || arr[i] != '[') return out;
    i++;
    while (i < n) {
        while (i < n && (arr[i] == ' ' || arr[i] == '\t' || arr[i] == '\r' ||
                         arr[i] == '\n' || arr[i] == ',')) i++;
        if (i >= n) break;
        if (arr[i] == ']') break;
        if (arr[i] == '{') {
            size_t end = 0;
            if (!FpCookieScanObject(arr, i, end)) { out.clear(); return out; } // 截断
            out.push_back(arr.substr(i, end - i));
            i = end;
        } else {
            // 原始值元素（数字/字符串/true…）：跳到下一个 ',' 或 ']'
            bool inStr = false;
            for (; i < n; i++) {
                char c = arr[i];
                if (inStr) {
                    if (c == '\\') i++;
                    else if (c == '"') inStr = false;
                } else if (c == '"') inStr = true;
                else if (c == ',' || c == ']') break;
            }
        }
    }
    return out;
}

// 解析对象 -> (key, 原始值)；字符串值保留引号，由取值函数决定是否去引号
inline bool FpCookieObjFields(const std::string& obj,
    std::vector<std::pair<std::string, std::string>>& out) {
    out.clear();
    if (obj.empty() || obj[0] != '{') return false;
    size_t i = 1, n = obj.size();
    auto ws = [&]() {
        while (i < n && (obj[i] == ' ' || obj[i] == '\t' || obj[i] == '\r' || obj[i] == '\n')) i++;
    };
    while (i < n) {
        ws();
        if (i < n && obj[i] == '}') break;
        if (i < n && obj[i] == ',') { i++; continue; }
        if (i >= n || obj[i] != '"') return false;
        // key（含转义）
        std::string keyRaw;
        i++;
        bool closed = false;
        for (; i < n; i++) {
            if (obj[i] == '\\') { keyRaw += obj[i]; if (i + 1 < n) keyRaw += obj[++i]; continue; }
            if (obj[i] == '"') { closed = true; i++; break; }
            keyRaw += obj[i];
        }
        if (!closed) return false;
        std::string key = FpCookieUnescape(keyRaw);
        ws();
        if (i >= n || obj[i] != ':') return false;
        i++;
        ws();
        if (i >= n) return false;
        std::string val;
        if (obj[i] == '"') {
            val += '"';
            i++;
            bool end = false;
            for (; i < n; i++) {
                if (obj[i] == '\\') { val += obj[i]; if (i + 1 < n) val += obj[++i]; continue; }
                val += obj[i];
                if (obj[i] == '"') { end = true; i++; break; }
            }
            if (!end) return false;
        } else if (obj[i] == '{' || obj[i] == '[') {
            // 嵌套结构按原文保留（cookie 不用，但不误解析）
            size_t end = 0;
            if (obj[i] == '{') { if (!FpCookieScanObject(obj, i, end)) return false; }
            else {
                // 扫数组
                int depth = 0; bool inStr = false; end = 0;
                for (size_t k = i; k < n; k++) {
                    char c = obj[k];
                    if (inStr) { if (c == '\\') k++; else if (c == '"') inStr = false; continue; }
                    if (c == '"') inStr = true;
                    else if (c == '[') depth++;
                    else if (c == ']') { depth--; if (depth == 0) { end = k + 1; break; } }
                }
                if (!end) return false;
            }
            val = obj.substr(i, end - i);
            i = end;
        } else {
            for (; i < n; i++) {
                if (obj[i] == ',' || obj[i] == '}') break;
                val += obj[i];
            }
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' ||
                                    val.back() == '\r' || val.back() == '\n')) val.pop_back();
        }
        out.push_back(std::make_pair(key, val));
    }
    return true;
}

inline std::string FpCookieGetStr(const std::string& obj, const char* key) {
    std::vector<std::pair<std::string, std::string>> fs;
    if (!FpCookieObjFields(obj, fs)) return std::string();
    for (const auto& kv : fs) {
        if (kv.first != key) continue;
        const std::string& v = kv.second;
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            return FpCookieUnescape(v.substr(1, v.size() - 2));
        return std::string();
    }
    return std::string();
}

inline bool FpCookieGetBool(const std::string& obj, const char* key, bool def) {
    std::vector<std::pair<std::string, std::string>> fs;
    if (!FpCookieObjFields(obj, fs)) return def;
    for (const auto& kv : fs) {
        if (kv.first != key) continue;
        if (kv.second == "true") return true;
        if (kv.second == "false") return false;
        if (kv.second.size() >= 2 && kv.second.front() == '"' && kv.second.back() == '"') {
            std::string s = FpCookieUnescape(kv.second.substr(1, kv.second.size() - 2));
            return s == "1" || s == "true" || s == "True" || s == "TRUE";
        }
        return def;
    }
    return def;
}

inline long long FpCookieGetInt(const std::string& obj, const char* key, long long def) {
    std::vector<std::pair<std::string, std::string>> fs;
    if (!FpCookieObjFields(obj, fs)) return def;
    for (const auto& kv : fs) {
        if (kv.first != key) continue;
        std::string v = kv.second;
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            v = FpCookieUnescape(v.substr(1, v.size() - 2));
        bool neg = false;
        size_t p = 0;
        if (!v.empty() && (v[0] == '-' || v[0] == '+')) { neg = (v[0] == '-'); p = 1; }
        if (p >= v.size()) return def;
        long long acc = 0;
        for (; p < v.size(); p++) {
            if (v[p] < '0' || v[p] > '9') return def;
            acc = acc * 10 + (v[p] - '0');
            if (acc > 4611686018427387900LL) break;
        }
        return neg ? -acc : acc;
    }
    return def;
}

// ---------------- official 语义 ----------------
inline long long FpCookiesNowUnix() {
    return static_cast<long long>(std::time(nullptr));
}
// unix 秒 -> Chrome 微秒（自 1601-01-01 UTC）；越界返回 0
inline long long FpCookiesChromeFromUnixSec(long long unixSec) {
    const long long kEpochDiff = 11644473600LL;
    if (unixSec <= 0) return 0;
    return (unixSec - kEpochDiff) * 1000000LL;
}
inline long long FpCookiesUnixFromChromeUs(long long chromeUs) {
    const long long kEpochDiff = 11644473600LL;
    if (chromeUs <= 0) return 0;
    return chromeUs / 1000000LL + kEpochDiff;
}
inline std::string FpCookiesSameSiteToStr(int v) {
    if (v == 0) return "no_restriction";
    if (v == 1) return "lax";
    if (v == 2) return "strict";
    return "unspecified";
}
inline int FpCookiesSameSiteToInt(const std::string& s) {
    std::string v;
    for (char c : s) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') v += c;
    for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (v == "no_restriction" || v == "none" || v == "0") return 0;
    if (v == "lax" || v == "1") return 1;
    if (v == "strict" || v == "2") return 2;
    return -1; // unspecified / 缺失
}

// 规范化：丢无 name 的对象（[{}] -> []）、补齐官方默认字段、输出规范 JSON 数组。
// 输入不是数组返回 ""（调用方据此判断是否需要先做明文转换）。droppedOut = 被丢弃的元素数。
inline std::string FpNormalizeCookiesJson(const std::string& in, int* droppedOut = nullptr) {
    int dropped = 0;
    if (droppedOut) *droppedOut = 0;
    size_t s0 = 0;
    while (s0 < in.size() && (in[s0] == ' ' || in[s0] == '\t' || in[s0] == '\r' || in[s0] == '\n')) s0++;
    if (s0 >= in.size() || in[s0] != '[') return std::string();

    const long long now = FpCookiesNowUnix();
    const long long kYear = 365LL * 24 * 3600;
    std::string out = "[";
    bool first = true;
    for (const std::string& obj : FpCookieArraySplit(in)) {
        const std::string name = FpCookieGetStr(obj, "name");
        if (name.empty()) { dropped++; continue; }
        const std::string value = FpCookieGetStr(obj, "value");
        const std::string domain = FpCookieGetStr(obj, "domain");
        std::string path = FpCookieGetStr(obj, "path");
        if (path.empty()) path = "/";
        const bool httpOnly = FpCookieGetBool(obj, "httpOnly", false);
        const bool secure = FpCookieGetBool(obj, "secure", false);
        long long expires = FpCookieGetInt(obj, "expires", 0);
        const bool session = FpCookieGetBool(obj, "session", expires <= 0);
        if (session) expires = 0;
        else if (expires <= 0) expires = now + kYear;

        // sameSite：官方字符串优先，数字回退映射
        std::string sameSite = FpCookieGetStr(obj, "sameSite");
        if (sameSite.empty()) {
            long long si = FpCookieGetInt(obj, "sameSite", -99);
            sameSite = (si == -99) ? "unspecified" : FpCookiesSameSiteToStr(static_cast<int>(si));
        } else {
            std::string t;
            for (char c : sameSite) if (c != ' ') t += c;
            if (t == "Unspecified" || t == "unspecified") sameSite = "unspecified";
            else if (t == "None" || t == "none") sameSite = "no_restriction";
            else if (t == "Lax" || t == "lax") sameSite = "lax";
            else if (t == "Strict" || t == "strict") sameSite = "strict";
            else sameSite = "unspecified";
        }

        bool hostOnly = false;
        bool hasHost = false;
        std::vector<std::pair<std::string, std::string>> fs;
        if (FpCookieObjFields(obj, fs)) {
            for (const auto& kv : fs) {
                if (kv.first == "hostOnly") { hasHost = true; hostOnly = FpCookieGetBool(obj, "hostOnly", false); }
            }
        }
        if (!hasHost && !domain.empty()) hostOnly = (domain[0] != '.');

        if (!first) out += ",";
        first = false;
        out += "{\"name\":\"" + FpCookieEscape(name) + "\"";
        out += ",\"value\":\"" + FpCookieEscape(value) + "\"";
        if (!domain.empty()) out += ",\"domain\":\"" + FpCookieEscape(domain) + "\"";
        out += ",\"path\":\"" + FpCookieEscape(path) + "\"";
        out += std::string(",\"httpOnly\":") + (httpOnly ? "true" : "false");
        out += std::string(",\"secure\":") + (secure ? "true" : "false");
        out += std::string(",\"session\":") + (session ? "true" : "false");
        if (!session) out += ",\"expires\":" + std::to_string(expires);
        out += ",\"sameSite\":\"" + sameSite + "\"";
        if (!domain.empty()) out += std::string(",\"hostOnly\":") + (hostOnly ? "true" : "false");
        out += "}";
    }
    out += "]";
    if (droppedOut) *droppedOut = dropped;
    return out;
}

// 明文 -> 官方 JSON 数组（再规范化）。支持：
//   1) 已是 JSON 数组 -> 透传
//   2) Netscape 7 列（含 #HttpOnly 前缀、'#' 注释行）
//   3) Cookie: a=b; c=d 头（或任意 k=v; 分隔）
//   4) 逐行 Name=Value
// 返回规范化后的 JSON；一行都没解析出对象返回 "[]"。parsedOut = 解析出的条数。
inline std::string FpCookiesTextToJson(const std::string& raw, int* parsedOut = nullptr) {
    if (parsedOut) *parsedOut = 0;
    size_t a = raw.find_first_not_of(" \t\r\n");
    std::string t = (a == std::string::npos) ? std::string() : raw.substr(a);
    if (!t.empty()) {
        size_t b = t.find_last_not_of(" \t\r\n");
        t.resize(b + 1);
    }
    if (t.empty()) return "[]";
    if (t.front() == '[') return FpNormalizeCookiesJson(t);

    auto esc = [](const std::string& s) { return FpCookieEscape(s); };
    int n = 0;
    std::string arr = "[";
    bool first = true;
    // expires 传 0 表示“未给”，由规范化补 now+1y（与官方 parseCookies 一致）
    auto emitPair = [&](const std::string& nm, const std::string& vv, const std::string& dom,
                        bool httpOnly, bool secure, long long expires) {
        if (nm.empty()) return;
        if (!first) arr += ",";
        first = false;
        arr += "{\"name\":\"" + esc(nm) + "\",\"value\":\"" + esc(vv) + "\"";
        if (!dom.empty()) arr += ",\"domain\":\"" + esc(dom) + "\"";
        arr += std::string(",\"httpOnly\":") + (httpOnly ? "true" : "false");
        arr += std::string(",\"secure\":") + (secure ? "true" : "false");
        if (expires > 0) arr += ",\"expires\":" + std::to_string(expires);
        n++;
    };
    auto toLL = [](const std::string& s) -> long long {
        long long v = 0;
        bool any = false;
        for (char c : s) {
            if (c < '0' || c > '9') break;
            v = v * 10 + (c - '0');
            any = true;
        }
        return any ? v : 0;
    };

    size_t ls = 0;
    while (ls <= t.size()) {
        size_t le = t.find('\n', ls);
        std::string line = (le == std::string::npos) ? t.substr(ls) : t.substr(ls, le - ls);
        while (!line.empty() && (line.back() == '\r')) line.pop_back();

        std::string tr = line;
        size_t ta = tr.find_first_not_of(" \t");
        if (ta != std::string::npos) tr = tr.substr(ta);

        const bool comment = !tr.empty() && tr[0] == '#';
        if (comment && tr.rfind("#HttpOnly", 0) == 0) {
            // #HttpOnly<domain>\tflag\tpath\tsecure\texpiry\tname\tvalue
            // 兼容两种导出：#HttpOnly 紧贴域名（curl 风格）与 #HttpOnly + 制表符
            std::string rest = tr.substr(9);
            if (!rest.empty() && rest[0] == '\t') rest.erase(0, 1);
            std::vector<std::string> cols;
            size_t cs = 0;
            while (cs <= rest.size()) {
                size_t ce = rest.find('\t', cs);
                cols.push_back((ce == std::string::npos) ? rest.substr(cs) : rest.substr(cs, ce - cs));
                if (ce == std::string::npos) break;
                cs = ce + 1;
            }
            if (cols.size() >= 7) {
                bool sec = (cols.size() >= 4 && (cols[3] == "TRUE" || cols[3] == "true"));
                emitPair(cols[5], cols[6], cols[0], true, sec, toLL(cols[4]));
            }
        } else if (comment || tr.empty()) {
            // '#' 注释 / 空行：跳过
        } else {
            size_t nCols = 0;
            {
                size_t cs = 0;
                bool hasTab = false;
                for (size_t k = 0; k < line.size(); k++) if (line[k] == '\t') hasTab = true;
                if (hasTab) {
                    while (cs <= line.size()) {
                        size_t ce = line.find('\t', cs);
                        nCols++;
                        if (ce == std::string::npos) break;
                        cs = ce + 1;
                    }
                }
            }
            if (nCols >= 7) {
                std::vector<std::string> cols;
                size_t cs = 0;
                while (cs <= line.size()) {
                    size_t ce = line.find('\t', cs);
                    cols.push_back((ce == std::string::npos) ? line.substr(cs) : line.substr(cs, ce - cs));
                    if (ce == std::string::npos) break;
                    cs = ce + 1;
                }
                bool sec = (cols.size() >= 4 && (cols[3] == "TRUE" || cols[3] == "true"));
                emitPair(cols[5], cols[6], cols[0], true, sec, toLL(cols[4]));
            } else if (line.find('=') != std::string::npos) {
                // Cookie: a=b; c=d  /  任意 k=v; 分隔  /  单行 Name=Value
                std::string work = line;
                size_t cks = work.find_first_not_of(" \t");
                if (cks != std::string::npos && work.size() - cks >= 7) {
                    std::string tag = work.substr(cks, 7);
                    for (auto& ch : tag) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                    if (tag == "cookie:") work = work.substr(cks + 7);
                }
                auto splitKV = [](const std::string& s, size_t eq, std::string& nm, std::string& vv) {
                    size_t nStart = 0;
                    while (nStart < eq && (s[nStart] == ' ' || s[nStart] == '\t' ||
                                           s[nStart] == ';' || s[nStart] == ':')) nStart++;
                    size_t nEnd = eq;
                    while (nEnd > nStart && (s[nEnd - 1] == ' ' || s[nEnd - 1] == '\t')) nEnd--;
                    size_t vStart = eq + 1;
                    while (vStart < s.size() && (s[vStart] == ' ' || s[vStart] == '\t')) vStart++;
                    size_t vEnd = s.size();
                    while (vEnd > vStart && (s[vEnd - 1] == ' ' || s[vEnd - 1] == '\t')) vEnd--;
                    nm = (nStart < nEnd) ? s.substr(nStart, nEnd - nStart) : std::string();
                    vv = (vStart < vEnd) ? s.substr(vStart, vEnd - vStart) : std::string();
                };
                if (work.find(';') != std::string::npos) {
                    size_t ks = 0;
                    while (ks <= work.size()) {
                        size_t ke = work.find(';', ks);
                        std::string seg = (ke == std::string::npos) ? work.substr(ks)
                                                                    : work.substr(ks, ke - ks);
                        size_t eq = seg.find('=');
                        if (eq != std::string::npos) {
                            std::string nm, vv;
                            splitKV(seg, eq, nm, vv);
                            emitPair(nm, vv, std::string(), false, false, 0);
                        }
                        if (ke == std::string::npos) break;
                        ks = ke + 1;
                    }
                } else {
                    size_t eq = work.find('=');
                    std::string nm, vv;
                    splitKV(work, eq, nm, vv);
                    emitPair(nm, vv, std::string(), false, false, 0);
                }
            }
        }
        if (le == std::string::npos) break;
        ls = le + 1;
    }
    arr += "]";
    if (parsedOut) *parsedOut = n;
    return FpNormalizeCookiesJson(arr);
}

// ---------------- 需要 IO / 系统 API 的部分（fp_cookies.cpp） ----------------
// 该 profile 的浏览器进程是否在跑（命令行 --user-data-dir 指向该目录）
bool FpCookiesBrowserRunning(const std::wstring& profileDir);
// 从浏览器 cookie 库导入明文 JSON（DPAPI 解密 v10；值解不出的计入 skippedOut）
bool FpCookiesImportFromBrowser(const std::wstring& profileDir, std::string& jsonOut,
    int* skippedOut, std::string& errOut);
// 把规范化 JSON 写进 Chromium cookie 库（同 host_key+name+path 覆盖）。
//   writtenOut=写入条数 skippedOut=缺 domain 等跳过条数 removedOut=清空时删除的行数
// 浏览器运行中/无库/无密钥 -> 返回 false 并写 errOut（不半途改库，先备份）
bool FpCookiesWriteToBrowser(const std::wstring& profileDir, const std::string& jsonArr,
    int* writtenOut, int* skippedOut, int* removedOut, std::string& errOut);
