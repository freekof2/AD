// fp_ui.cpp — 指纹配置原生窗口实现（part 1/4：JSON 互转 + 随机库）
#include "fp_ui.h"
#include "fingerprint.h"
#include "fp_webrtc.h"
#include "fp_browser_config.h"
#include "fp_cookies.h"
#include <ctime>
#include <shlobj.h>  // SHBrowseForFolderW（数据目录浏览）
#include <commdlg.h> // GetOpenFileNameW（浏览器 SunBrowser.exe 选择）

static std::string JEsc(const std::wstring& w) {
    // JSON 字符串全转义：" \ / \b \f \n \r \t + \u00XX 控制字符。
    // 根因：旧版只转义 " \，cookie（16KB，含 \n\r\t 等）直写进 ui 存档导致 JSON 断裂，
    // 下次 FpFormFromUiJson 读回错位，表现为“修改指纹配置无法保存”（实际是存档已坏）。
    std::string s = N(w), o;
    static const char* hex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\b') o += "\\b";
        else if (c == '\f') o += "\\f";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { o += "\\u00"; o += hex[c >> 4]; o += hex[c & 15]; }
        else o += (char)c;
    }
    return o;
}
static std::wstring WJ(const std::string& raw) {
    // FpJsonGet 返回含引号的原始片段，去引号转回 wstring。
    // 与 JEsc 对应：支持 \" \\ \/ \b \f \n \r \t \uXXXX 全转义。
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
                    // \u00XX（JEsc 只产出 ASCII 控制字符范围）
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

// ---- 伪装 IP + 按指纹目录 存 exe 同目录 Config.json（按环境名对应，双向同步） ----
// 格式（一个指纹一个对象）：
//   { "k1ds12lu_hyg6dd": { "data_dir": "F:\\.ADSPOWER_GLOBAL\\cache\\k1ds12lu_hyg6dd",
//                          "sun_browser_dir": "C:\\...\\cwd_global\\chrome_152",
//                          "webrtc_ip": "104.28.152.166" },
//     "旧环境": "192.168.128.129" }   // 旧写法=只有伪装 IP，仍可读
// data_dir 存**完整指纹目录**（数据就在该目录下）；LoadConfig 读取时去掉末尾环境名
// 还原成父目录，以适配 EffDataDir（父目录 + "\" + 环境名）的既有调用约定。
// 读优先级：Config.json（按指纹）> ui 存档 webrtcIp > static/dynamic 后备。
static std::wstring FpCleanDirectoryField(std::wstring path);
static std::wstring FpDirectoryLeaf(const std::wstring& path);
static std::wstring FpSpoofIpCfgPath() { return AppDir() + L"\\Config.json"; }

// 去引号 + 反转义（\" \\ \/ \b \f \n \r \t）；非字符串返回 ""
static std::string FpJsonUnquoteRaw(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return "";
    std::string s = raw.substr(1, raw.size() - 2), o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { o += s[i + 1]; i++; }
        else o += s[i];
    }
    return o;
}

// 删除顶层键（值可为对象/字符串/数组，含一个相邻逗号）；找不到返回原文。
static std::string FpConfigRemoveKey(const std::string& json, const std::string& key) {
    const std::string q = "\"" + key + "\"";
    const size_t k = json.find(q);
    if (k == std::string::npos) return json;
    const size_t c = json.find(':', k + q.size());
    if (c == std::string::npos) return json;
    size_t v = c + 1;
    while (v < json.size() && (json[v] == ' ' || json[v] == '\t' || json[v] == '\r' || json[v] == '\n')) v++;
    if (v >= json.size()) return json;
    size_t ve;
    if (json[v] == '{' || json[v] == '[') {
        const char open = json[v], close = (open == '{') ? '}' : ']';
        int depth = 0; bool inStr = false;
        for (ve = v; ve < json.size(); ve++) {
            if (inStr) {
                if (json[ve] == '\\') { ve++; continue; }
                if (json[ve] == '"') inStr = false;
            } else {
                if (json[ve] == '"') inStr = true;
                else if (json[ve] == open) depth++;
                else if (json[ve] == close) { depth--; if (depth == 0) { ve++; break; } }
            }
        }
    } else if (json[v] == '"') {
        ve = v + 1;
        while (ve < json.size()) {
            if (json[ve] == '\\') { ve += 2; continue; }
            if (json[ve] == '"') { ve++; break; }
            ve++;
        }
    } else {
        ve = v;
        while (ve < json.size() && json[ve] != ',' && json[ve] != '}' && json[ve] != ']') ve++;
    }
    std::string o = json;
    o.erase(k, ve - k);
    size_t t = k; // 吃掉一个相邻逗号（先看后面，再看前面）
    while (t < o.size() && (o[t] == ' ' || o[t] == '\t' || o[t] == '\r' || o[t] == '\n')) t++;
    if (t < o.size() && o[t] == ',') o.erase(t, 1);
    else if (k > 0) {
        size_t b = k;
        while (b > 0 && (o[b - 1] == ' ' || o[b - 1] == '\t' || o[b - 1] == '\r' || o[b - 1] == '\n')) b--;
        if (b > 0 && o[b - 1] == ',') o.erase(b - 1, 1);
    }
    return o;
}

static std::string FpConfigTrim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// 读：返回该指纹的伪装 IP（对象条目读 webrtc_ip，旧字符串条目直接读值）；""=无
static std::string FpConfigSpoofIpRaw(const std::wstring& profile) {
    std::string txt;
    if (!FpReadTextFile(FpSpoofIpCfgPath(), txt) || txt.empty()) return "";
    std::string v = FpJsonGet(txt, N(profile));
    if (v.size() >= 2 && v.front() == '{') {
        std::string ip = FpJsonGet(v, "webrtc_ip");
        if (ip.empty()) ip = FpJsonGet(v, "webrtcIp");
        return FpJsonUnquoteRaw(ip);
    }
    return FpJsonUnquoteRaw(v);
}

// 写：ip 非空=写入/更新；为空=删掉该指纹的 ip（对象条目只删 ip 字段，
// 保住同指纹的两个目录；对象被删空则整条目移除）。返回写盘是否成功。
static bool FpConfigSpoofIpSet(const std::wstring& profile, const std::string& ip) {
    std::wstring path = FpSpoofIpCfgPath();
    std::string txt;
    if (!FpReadTextFile(path, txt) || txt.empty()) {
        if (ip.empty()) return true; // 无文件无写入=已是删后状态
        txt = "{}";
    }
    const std::string key = N(profile);
    const std::string cur = FpJsonGet(txt, key);
    std::string inner = (cur.size() >= 2 && cur.front() == '{') ? cur : "{}";
    // 老格式纯字符串只含 IP；升级为对象时保留该 IP。
    if (cur.size() >= 2 && cur.front() == '"') {
        const std::string oldIp = FpJsonUnquoteRaw(cur);
        if (!oldIp.empty()) inner = FpJsonSet(inner, "webrtc_ip", "\"" + oldIp + "\"");
    }
    if (!ip.empty()) inner = FpJsonSet(inner, "webrtc_ip", "\"" + ip + "\"");
    else inner = FpConfigRemoveKey(inner, "webrtc_ip");
    if (inner.empty()) return false;
    std::string out;
    if (FpConfigTrim(inner) == "{}") out = FpConfigRemoveKey(txt, key);
    else out = FpJsonSet(txt, key, inner);
    if (out.empty()) return false;
    if (out == txt) return true;
    return FpWriteTextFile(path, out);
}

// 每个指纹都在 Config.json 有一个对象；首次保存 A2 目录时创建对象，目录和伪装 IP 互不覆盖。
static bool FpConfigSyncProfileDirs(const std::wstring& profile,
    const std::wstring& dataDirFull, const std::wstring& browserDir) {
    std::wstring path = FpSpoofIpCfgPath();
    std::string txt;
    if (!FpReadTextFile(path, txt) || txt.empty()) txt = "{}";
    const std::string key = N(profile);
    const std::string cur = FpJsonGet(txt, key);
    LOG(L"Config.json 目录同步输入 profile=" + profile + L" data=" + dataDirFull +
        L" browser=" + browserDir + L" oldEntry=" + (cur.empty() ? L"missing" : W(cur)));
    std::string inner = (cur.size() >= 2 && cur.front() == '{') ? cur : "{}";
    // 兼容旧版字符串条目：把原伪装 IP 搬进新对象，再添加两个目录。
    if (cur.size() >= 2 && cur.front() == '"') {
        const std::string oldIp = FpJsonUnquoteRaw(cur);
        if (!oldIp.empty()) inner = FpJsonSet(inner, "webrtc_ip", "\"" + oldIp + "\"");
    }
    if (!dataDirFull.empty()) inner = FpJsonSet(inner, "data_dir", "\"" + JEsc(dataDirFull) + "\"");
    if (!browserDir.empty()) inner = FpJsonSet(inner, "sun_browser_dir", "\"" + JEsc(browserDir) + "\"");
    if (inner.empty()) return false;
    std::string out = FpJsonSet(txt, key, inner);
    if (out.empty()) {
        LOG(L"Config.json 目录同步 FAIL(FpJsonSet failed) " + profile);
        return false;
    }
    if (out == txt) {
        LOG(L"Config.json 目录同步 unchanged " + profile);
        return true;
    }
    const bool ok = FpWriteTextFile(path, out);
    LOG(L"指纹保存 Config.json 目录 " + std::wstring(ok ? L"OK" : L"FAIL") +
        L" dataDir=" + dataDirFull + L" browserDir=" + browserDir + L" " + profile);
    return ok;
}

// 检查 profile 目录是否至少包含一份官方三件套，用于区分“数据父目录”和“完整 profile 目录”。
static bool FpHasFingerprintFiles(const std::wstring& profileDir) {
    DWORD attr = ::GetFileAttributesW(profileDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
    if (::GetFileAttributesW((profileDir + L"\\ui_fingerprint.json").c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;
    const std::wstring leaf = FpDirectoryLeaf(profileDir);
    const std::string fbcc = FpFbccIdOf(leaf);
    for (const std::string& file : { FpStaticName(fbcc), FpDynamicName(fbcc), FpCookiesName(fbcc) })
        if (::GetFileAttributesW((profileDir + L"\\" + W(file)).c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    return false;
}

static std::wstring FpImportSourceProfileDir(const std::wstring& raw,
    const std::wstring& currentProfile, bool& directProfilePath) {
    const std::wstring path = FpCleanDirectoryField(raw);
    if (path.empty()) { directProfilePath = false; return L""; }
    DWORD attr = ::GetFileAttributesW(path.c_str());
    const bool existsDirectory = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
    const bool hasFingerprintFiles = existsDirectory && FpHasFingerprintFiles(path);
    const FpImportSourceResolution resolved = FpResolveImportSourcePath(
        path, currentProfile, existsDirectory, hasFingerprintFiles);
    directProfilePath = resolved.directProfilePath;
    return resolved.profilePath;
}

std::string FpFormToUiJson(const FpFormData& f) {
    std::string o = "{";
    o += "\"browser\":\"" + JEsc(f.browser) + "\",\"kernelVer\":\"" + JEsc(f.kernelVer) + "\"";
    o += ",\"browserDir\":\"" + JEsc(f.browserDir) + "\"";
    o += ",\"profDataDir\":\"" + JEsc(f.profDataDir) + "\",\"profBrowserDir\":\"" + JEsc(f.profBrowserDir) + "\"";
    o += ",\"os\":\"" + JEsc(f.os) + "\"";
    o += ",\"uaPreset\":\"" + JEsc(f.uaPreset) + "\",\"ua\":\"" + JEsc(f.ua) + "\"";
    o += ",\"proxyType\":\"" + JEsc(f.proxyType) + "\",\"proxyHost\":\"" + JEsc(f.proxyHost) + "\"";
    o += ",\"proxyPort\":\"" + JEsc(f.proxyPort) + "\",\"proxyUser\":\"" + JEsc(f.proxyUser) + "\"";
    o += ",\"proxyPass\":\"" + JEsc(f.proxyPass) + "\"";
    o += ",\"cookie\":\"" + JEsc(f.cookie) + "\",\"remark\":\"" + JEsc(f.remark) + "\"";
    o += ",\"webrtc\":\"" + JEsc(f.webrtc) + "\",\"webrtcIp\":\"" + JEsc(f.webrtcIp) + "\"";
    o += ",\"timezoneMode\":\"" + JEsc(f.timezoneMode) + "\"";
    o += ",\"timezone\":\"" + JEsc(f.timezone) + "\",\"geoMode\":\"" + JEsc(f.geoMode) + "\"";
    o += ",\"geoIp\":\"" + JEsc(f.geoIp) + "\",\"lat\":\"" + JEsc(f.lat) + "\",\"lng\":\"" + JEsc(f.lng) + "\"";
    o += ",\"accuracy\":\"" + JEsc(f.accuracy) + "\",\"langMode\":\"" + JEsc(f.langMode) + "\"";
    o += ",\"language\":\"" + JEsc(f.langList) + "\",\"uiLang\":\"" + JEsc(f.uiLang) + "\"";
    o += ",\"pageLanguage\":\"" + JEsc(f.pageLang) + "\",\"resMode\":\"" + JEsc(f.resMode) + "\"";
    o += ",\"resolution\":\"" + JEsc(f.resolution) + "\",\"resW\":\"" + JEsc(f.resW) + "\",\"resH\":\"" + JEsc(f.resH) + "\"";
    o += ",\"fontMode\":\"" + JEsc(f.fontMode) + "\",\"fonts\":\"" + JEsc(f.fonts) + "\"";
    o += ",\"canvas\":\"" + std::string(f.swCanvas ? "1" : "0") + "\",\"webglImage\":\"" + std::string(f.swWebglImg ? "1" : "0") + "\"";
    o += ",\"audio\":\"" + std::string(f.swAudio ? "1" : "0") + "\",\"clientRects\":\"" + std::string(f.swClientRects ? "1" : "0") + "\"";
    o += ",\"speechSwitch\":\"" + std::string(f.swSpeech ? "1" : "0") + "\",\"mediaDevices\":\"" + JEsc(f.mediaDevices) + "\"";
    o += ",\"webglMeta\":\"" + JEsc(f.webglMeta) + "\",\"vendor\":\"" + JEsc(f.vendor) + "\"";
    o += ",\"renderer\":\"" + JEsc(f.renderer) + "\",\"webgpu\":\"" + JEsc(f.webgpu) + "\"";
    o += ",\"gpuVendor\":\"" + JEsc(f.gpuVendor) + "\",\"gpuArch\":\"" + JEsc(f.gpuArch) + "\"";
    o += ",\"cpuMode\":\"" + JEsc(f.cpuMode) + "\",\"cpu\":\"" + JEsc(f.cpu) + "\"";
    o += ",\"ramMode\":\"" + JEsc(f.ramMode) + "\",\"ram\":\"" + JEsc(f.ram) + "\"";
    o += ",\"devNameMode\":\"" + JEsc(f.devNameMode) + "\",\"devName\":\"" + JEsc(f.devName) + "\"";
    o += ",\"macMode\":\"" + JEsc(f.macMode) + "\",\"mac\":\"" + JEsc(f.mac) + "\"";
    o += ",\"doNotTrack\":\"" + JEsc(f.doNotTrack) + "\",\"portScan\":\"" + JEsc(f.portScan) + "\"";
    o += ",\"whitePorts\":\"" + JEsc(f.whitePorts) + "\",\"hardwareAccel\":\"" + JEsc(f.hardwareAccel) + "\"";
    o += ",\"disableTls\":\"" + JEsc(f.disableTls) + "\",\"tls\":\"" + JEsc(f.tlsBlacklist) + "\"";
    o += ",\"launchArgs\":\"" + JEsc(f.launchArgs) + "\"";
    // 系统扩展（official fingerprint_config 键名）
    o += ",\"maxTouchPoints\":\"" + JEsc(f.maxTouchPoints) + "\"";
    o += ",\"flash\":\"" + JEsc(f.flashMode) + "\"";
    o += ",\"gyroscope\":\"" + JEsc(f.gyroscope) + "\"";
    o += ",\"networkInformationType\":\"" + JEsc(f.netInfoType) + "\"";
    o += ",\"clientHints\":{\"platform\":\"" + JEsc(f.chPlatform) +
         "\",\"platform_version\":\"" + JEsc(f.chPlatformVersion) +
         "\",\"architecture\":\"" + JEsc(f.chArchitecture) +
         "\",\"model\":\"" + JEsc(f.chModel) +
         "\",\"mobile\":\"" + JEsc(f.chMobile) +
         "\",\"bitness\":\"" + JEsc(f.chBitness) +
         "\",\"wow64\":\"" + JEsc(f.chWow64) + "\"}";
    o += "}";
    return o;
}
static void FJSet(std::wstring FpFormData::* mp, const std::string& json, const char* key, FpFormData& f) {
    std::string v = FpJsonGet(json, key);
    if (!v.empty()) f.*mp = WJ(v);
}

bool FpFormFromUiJson(const std::string& json, FpFormData& f) {
    if (json.empty()) return false;
    FJSet(&FpFormData::browser, json, "browser", f);
    FJSet(&FpFormData::kernelVer, json, "kernelVer", f);
    FJSet(&FpFormData::browserDir, json, "browserDir", f);
    FJSet(&FpFormData::profDataDir, json, "profDataDir", f);
    FJSet(&FpFormData::profBrowserDir, json, "profBrowserDir", f);
    FJSet(&FpFormData::os, json, "os", f);
    FJSet(&FpFormData::uaPreset, json, "uaPreset", f);
    FJSet(&FpFormData::ua, json, "ua", f);
    FJSet(&FpFormData::proxyType, json, "proxyType", f);
    FJSet(&FpFormData::proxyHost, json, "proxyHost", f);
    FJSet(&FpFormData::proxyPort, json, "proxyPort", f);
    FJSet(&FpFormData::proxyUser, json, "proxyUser", f);
    FJSet(&FpFormData::proxyPass, json, "proxyPass", f);
    FJSet(&FpFormData::cookie, json, "cookie", f);
    FJSet(&FpFormData::remark, json, "remark", f);
    FJSet(&FpFormData::webrtc, json, "webrtc", f);
    FJSet(&FpFormData::webrtcIp, json, "webrtcIp", f);
    FJSet(&FpFormData::timezoneMode, json, "timezoneMode", f);
    FJSet(&FpFormData::timezone, json, "timezone", f);
    FJSet(&FpFormData::geoMode, json, "geoMode", f);
    FJSet(&FpFormData::geoIp, json, "geoIp", f);
    FJSet(&FpFormData::lat, json, "lat", f);
    FJSet(&FpFormData::lng, json, "lng", f);
    FJSet(&FpFormData::accuracy, json, "accuracy", f);
    FJSet(&FpFormData::langMode, json, "langMode", f);
    FJSet(&FpFormData::langList, json, "language", f);
    // 兼容旧存档 language 为 JSON 数组形态：["en-US","en"] -> en-US,en
    if (!f.langList.empty() && f.langList.front() == L'[') {
        std::string raw = N(f.langList), out;
        size_t p = 0;
        while ((p = raw.find('"', p)) != std::string::npos) {
            size_t q = raw.find('"', p + 1);
            if (q == std::string::npos) break;
            if (!out.empty()) out += ",";
            out += raw.substr(p + 1, q - p - 1);
            p = q + 1;
        }
        f.langList = W(out);
    }
    FJSet(&FpFormData::uiLang, json, "uiLang", f);
    FJSet(&FpFormData::pageLang, json, "pageLanguage", f);
    FJSet(&FpFormData::resMode, json, "resMode", f);
    FJSet(&FpFormData::resolution, json, "resolution", f);
    FJSet(&FpFormData::resW, json, "resW", f);
    FJSet(&FpFormData::resH, json, "resH", f);
    FJSet(&FpFormData::fontMode, json, "fontMode", f);
    FJSet(&FpFormData::fonts, json, "fonts", f);
    // 兼容旧存档 fonts 为 JSON 数组形态：["all"] -> all；["a","b"] -> a,b
    if (!f.fonts.empty() && f.fonts.front() == L'[') {
        std::string raw = N(f.fonts), out;
        size_t p = 0;
        while ((p = raw.find('"', p)) != std::string::npos) {
            size_t q = raw.find('"', p + 1);
            if (q == std::string::npos) break;
            if (!out.empty()) out += ",";
            out += raw.substr(p + 1, q - p - 1);
            p = q + 1;
        }
        // ["all"] 保持 fontMode=all 语义由调用方判断；此处只还原文本
        f.fonts = W(out);
    }
    FJSet(&FpFormData::mediaDevices, json, "mediaDevices", f);
    // 兼容旧存档 mediaDevicesNum 对象形态 -> 回填三数量输入框
    {
        std::string mdn = FpJsonGet(json, "mediaDevicesNum");
        if (!mdn.empty() && mdn.front() == '{') {
            std::string a = FpJsonGet(mdn, "audioinput_num");
            std::string v = FpJsonGet(mdn, "videoinput_num");
            std::string o = FpJsonGet(mdn, "audiooutput_num");
            // 去引号（存档里可能是数字或字符串）
            auto unq = [](const std::string& s) -> std::wstring {
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                    return W(s.substr(1, s.size() - 2));
                return W(s);
            };
            if (!a.empty()) f.mediaIn = unq(a);
            if (!v.empty()) f.mediaVid = unq(v);
            if (!o.empty()) f.mediaOut = unq(o);
        }
    }
    FJSet(&FpFormData::webglMeta, json, "webglMeta", f);
    FJSet(&FpFormData::vendor, json, "vendor", f);
    FJSet(&FpFormData::renderer, json, "renderer", f);
    FJSet(&FpFormData::webgpu, json, "webgpu", f);
    FJSet(&FpFormData::gpuVendor, json, "gpuVendor", f);
    FJSet(&FpFormData::gpuArch, json, "gpuArch", f);
    // 兼容旧存档 webglConfig 对象形态 -> 回填 vendor/renderer/适配器
    {
        std::string wc = FpJsonGet(json, "webglConfig");
        if (!wc.empty() && wc.front() == '{') {
            std::string uv = FpJsonGet(wc, "unmasked_vendor");
            std::string ur = FpJsonGet(wc, "unmasked_renderer");
            if (!uv.empty()) f.vendor = WJ(uv);
            if (!ur.empty()) f.renderer = WJ(ur);
        }
    }
    FJSet(&FpFormData::cpuMode, json, "cpuMode", f);
    FJSet(&FpFormData::cpu, json, "cpu", f);
    FJSet(&FpFormData::ramMode, json, "ramMode", f);
    FJSet(&FpFormData::ram, json, "ram", f);
    FJSet(&FpFormData::devNameMode, json, "devNameMode", f);
    FJSet(&FpFormData::devName, json, "devName", f);
    FJSet(&FpFormData::macMode, json, "macMode", f);
    FJSet(&FpFormData::mac, json, "mac", f);
    FJSet(&FpFormData::doNotTrack, json, "doNotTrack", f);
    FJSet(&FpFormData::portScan, json, "portScan", f);
    FJSet(&FpFormData::whitePorts, json, "whitePorts", f);
    FJSet(&FpFormData::hardwareAccel, json, "hardwareAccel", f);
    FJSet(&FpFormData::disableTls, json, "disableTls", f);
    FJSet(&FpFormData::tlsBlacklist, json, "tls", f);
    FJSet(&FpFormData::launchArgs, json, "launchArgs", f);
    FJSet(&FpFormData::maxTouchPoints, json, "maxTouchPoints", f);
    FJSet(&FpFormData::flashMode, json, "flash", f);
    FJSet(&FpFormData::gyroscope, json, "gyroscope", f);
    FJSet(&FpFormData::netInfoType, json, "networkInformationType", f);
    // clientHints 对象 -> 子字段（official setClientHints 输入形态）
    {
        std::string ch = FpJsonGet(json, "clientHints");
        if (ch.size() >= 2 && ch.front() == '{') {
            auto s = [&](const char* k) { return WJ(FpJsonGet(ch, k)); };
            const std::wstring plat = s("platform");
            if (!plat.empty()) f.chPlatform = plat;
            const std::wstring ver = s("platform_version");
            if (!ver.empty()) f.chPlatformVersion = ver;
            const std::wstring arch = s("architecture");
            if (!arch.empty()) f.chArchitecture = arch;
            if (!FpJsonGet(ch, "model").empty()) f.chModel = s("model");
            const std::wstring mob = s("mobile");
            if (mob == L"1" || mob == L"0") f.chMobile = mob;
            const std::wstring bit = s("bitness");
            if (bit == L"32" || bit == L"64") f.chBitness = bit;
            const std::wstring wow = s("wow64");
            if (wow == L"1" || wow == L"0") f.chWow64 = wow;
        }
    }
    std::string v;
    v = FpJsonGet(json, "canvas"); if (!v.empty()) f.swCanvas = (v == "\"1\"" || v == "1");
    v = FpJsonGet(json, "webglImage"); if (!v.empty()) f.swWebglImg = (v == "\"1\"" || v == "1");
    v = FpJsonGet(json, "audio"); if (!v.empty()) f.swAudio = (v == "\"1\"" || v == "1");
    v = FpJsonGet(json, "clientRects"); if (!v.empty()) f.swClientRects = (v == "\"1\"" || v == "1");
    v = FpJsonGet(json, "speechSwitch"); if (!v.empty()) f.swSpeech = (v == "\"1\"" || v == "1");
    return true;
}

static std::wstring FpWebGlProfilePath(const std::wstring& profileDir,
    const std::wstring& profileName, const std::string& staticJson) {
    std::string platformRaw = FpJsonGet(staticJson, "Platform");
    std::string platform = platformRaw.empty() ? "Win32" : N(FpJsonUnquote(platformRaw));
    const std::string fbcc = FpFbccIdOf(profileName);
    const std::string file = FpMd5Hex(fbcc + "_webgl") + "_" + FpBrowserPlatformTag(platform);
    return profileDir + L"\\" + W(file);
}

static void FpBackfillWebGl(FpFormData& f, const std::string& ui,
    const std::string& staticJson, const std::wstring& profileDir,
    const std::wstring& profileName) {
    auto missing = [](const std::string& value) { return value.empty() || value == "\"\""; };
    const std::string uiMeta = FpJsonGet(ui, "webglMeta");
    const std::string uiVendor = FpJsonGet(ui, "vendor");
    const std::string uiRenderer = FpJsonGet(ui, "renderer");
    const std::string uiGpu = FpJsonGet(ui, "webgpu");
    const std::string uiGpuVendor = FpJsonGet(ui, "gpuVendor");
    const std::string uiGpuArch = FpJsonGet(ui, "gpuArch");
    const std::string staticMode = FpJsonGet(staticJson, "webgl");
    const std::string staticConfig = FpJsonGet(staticJson, "webgl_config");
    std::string webglFile;
    FpReadTextFile(FpWebGlProfilePath(profileDir, profileName, staticJson), webglFile);
    if (!webglFile.empty() && webglFile.front() != '{') webglFile.clear();

    if (missing(uiMeta)) {
        if (staticMode == "\"0\"" || staticMode == "0") f.webglMeta = L"real";
        else if (staticMode == "\"2\"" || staticMode == "2" ||
                 staticMode == "\"3\"" || staticMode == "3") f.webglMeta = L"custom";
        else if (!staticConfig.empty() || !webglFile.empty()) f.webglMeta = L"custom";
    }
    std::string vendor = FpJsonGet(staticConfig, "unmasked_vendor");
    std::string renderer = FpJsonGet(staticConfig, "unmasked_renderer");
    if (missing(vendor)) vendor = FpJsonGet(webglFile, "UNMASKED_VENDOR_WEBGL");
    if (missing(renderer)) renderer = FpJsonGet(webglFile, "UNMASKED_RENDERER_WEBGL");
    if (missing(uiVendor) && !missing(vendor)) f.vendor = WJ(vendor);
    if (missing(uiRenderer) && !missing(renderer)) f.renderer = WJ(renderer);

    const std::string staticWebgpu = FpJsonGet(staticConfig, "webgpu");
    const std::string staticSwitch = FpJsonGet(staticWebgpu, "webgpu_switch");
    std::string gpuVendor = FpJsonGet(staticWebgpu, "gpu_adapterinfo_vendor");
    std::string gpuArch = FpJsonGet(staticWebgpu, "gpu_adapterinfo_architecture");
    const std::string fileAdapter = FpJsonGet(webglFile, "GPUAdapterInfo");
    if (missing(gpuVendor)) gpuVendor = FpJsonGet(fileAdapter, "vendor");
    if (missing(gpuArch)) gpuArch = FpJsonGet(fileAdapter, "architecture");
    if (missing(uiGpu)) {
        if (staticSwitch == "\"0\"" || staticSwitch == "0") f.webgpu = L"disabled";
        else if (!missing(gpuVendor) || !missing(gpuArch)) f.webgpu = L"custom";
        else f.webgpu = L"follow_webgl";
    }
    if (missing(uiGpuVendor) && !missing(gpuVendor)) f.gpuVendor = WJ(gpuVendor);
    if (missing(uiGpuArch) && !missing(gpuArch)) f.gpuArch = WJ(gpuArch);
}

// ---- asar 1:1 字体表（main.min.js 内嵌 u[] 181 条 / c[] 12 条，顺序保留，含 "Caurier Regular" 笔误）----
// 用途见下方 FpBuildFakefontsJson / FpBuildDisabledFontsJson。
// 注意：表定义在文件靠后位置（kTz 附近），此处函数仅声明，定义见表后。
static std::string FpBuildFakefontsJson(const std::wstring& platform);
static std::string FpBuildDisabledFontsJson();
static std::wstring FpOsToAsarPlatform(const std::wstring& os);

// 表单 -> fingerprint_config（与 web-ui btnFpSave 的 fpConfig 组装一致）
// ---- asar 1:1 setFakeFonts/setFonts 语义（main.min.js 全文移植，函数体见字体表后）----
// platform: Win32|MacIntel|Linux x86_64|Linux armv7I|Linux armv8I|Linux armv81|Linux i686|iPhone|Windows Phone
// hostOs: 离线本机固定 win32（SunLauncher 只跑 Windows；asar process.platform 分支收敛到 win32）
// fontsMode: all -> 输出 DisabledFonts=getFonts-mobileFonts；custom -> 输出切分数组（调用方已在 fp_config 处理，此处返回 ""）
// Fakefonts 输出 JSON 对象（键=伪装表全键，值=本机表轮转；asar n[e]=win32[t%len]，t 为键序号）
// 云端表缺失回退：win32/darwin/linux 键表与值表均用 u[] 全集；mobile 键表用 mobileFonts 精确 12 条。

// 表单字体 -> JSON 数组：逗号/中文逗号(U+FF0C)/换行切分，trim ASCII 空白（与 web-ui split 一致）。
static std::string FpFontsToJsonArray(const std::wstring& fonts) {
    std::string fs8 = N(fonts), fs, arr = "[";
    for (size_t i = 0; i < fs8.size();) {
        if (i + 2 < fs8.size() && (unsigned char)fs8[i] == 0xEF &&
            (unsigned char)fs8[i + 1] == 0xBC && (unsigned char)fs8[i + 2] == 0x8C) {
            fs += ','; i += 3;
        } else { fs += fs8[i]; i++; }
    }
    size_t p = 0; bool first = true;
    while (p <= fs.size()) {
        size_t e = fs.find_first_of(",\n", p);
        std::string tok = fs.substr(p, e == std::string::npos ? e : e - p);
        size_t a = tok.find_first_not_of(" \t\r");
        size_t b = tok.find_last_not_of(" \t\r");
        if (a != std::string::npos) {
            tok = tok.substr(a, b - a + 1);
            if (!first) arr += ",";
            first = false;
            arr += "\"" + tok + "\"";
        }
        if (e == std::string::npos) break;
        p = e + 1;
    }
    arr += "]";
    return arr;
}

std::string FpFormToFpConfig(const FpFormData& f) {
    // AcceptLang 派生（main.min.js getAccept 全文移植）：首项无 q，后续项 q=0.9..0.1
    // 递减，同项/同基语去重，基语（如 en-US 的 en）补 q。例 en-US,en -> "en-US,en;q=0.9"。
    auto buildAccept = [](const std::string& langCsv) -> std::string {
        std::vector<std::string> e;
        { size_t p = 0; while (p <= langCsv.size()) {
            size_t q = langCsv.find(',', p);
            std::string t = langCsv.substr(p, q == std::string::npos ? q : q - p);
            size_t a = t.find_first_not_of(" \t\r\n"), b = t.find_last_not_of(" \t\r\n");
            if (a != std::string::npos) e.push_back(t.substr(a, b - a + 1));
            if (q == std::string::npos) break; p = q + 1; } }
        if (e.empty()) return "";
        auto base = [](const std::string& s) -> std::string {
            size_t d = s.find('-'); return (d == std::string::npos) ? s : s.substr(0, d); };
        std::string t; int n = 0; std::vector<std::string> r;
        for (size_t a = 0; a < e.size(); a++) {
            std::string i = e[a], o = (a + 1 < e.size()) ? e[a + 1] : "";
            std::string s = base(i), l = base(o);
            auto has = [&](const std::string& x) {
                for (auto& y : r) if (y == x) return true; return false; };
            if (a == 0) { t += i; n++; }
            else if (!has(i)) {
                int q = 10 - n; if (q < 1) q = 1;
                t += "," + i + ";q=0." + std::to_string(q); n++;
            }
            if (!(i == s || s == l || has(s))) {
                int q = 10 - n; if (q < 1) q = 1;
                t += "," + s + ";q=0." + std::to_string(q); n++; r.push_back(s);
            }
            r.push_back(i);
        }
        return t;
    };
    const FpWebRtcResolution rtc = FpResolveWebRtc(f.webrtc, f.webrtcIp);
    std::string sp = N(f.webrtc), tz = (f.timezoneMode == L"ip") ? "1" : "0";
    std::string o = "{\"webrtc\":\"" + sp + "\",\"DisableWebRTC\":" +
        std::string(rtc.disableWebRtc ? "true" : "false") + ",\"WebRTCAddress\":\"" +
        JEsc(rtc.address) + "\",\"automatic_timezone\":\"" + tz + "\"";
    if (f.webrtc == L"forward")
        o += ",\"WebRTCStun\":\"stun:stun.l.google.com:19302\",\"WebRTCTurn\":\"stun:stun.l.google.com:19302\"";
    o += ",\"tzAuto\":\"" + tz + "\"";
    if (tz == "0") {
        std::string tzn = FpNormalizeTimezone(N(f.timezone));
        o += ",\"timezone\":\"" + tzn + "\",\"TimeZone\":\"" + tzn + "\"";
    } else o += ",\"timezone\":\"\",\"TimeZone\":\"\"";
    std::string loc = N(f.geoMode);
    std::string locSw = (f.geoIp == L"ip" ? "1" : "0");
    o += ",\"location\":\"" + loc + "\",\"location_switch\":\"" + locSw + "\"";
    o += ",\"locationSwitch\":\"" + locSw + "\"";
    if (f.geoIp != L"ip")
        o += ",\"latitude\":\"" + N(f.lat) + "\",\"longitude\":\"" + N(f.lng) + "\",\"accuracy\":\"" + N(f.accuracy) + "\"";
    else o += ",\"latitude\":\"\",\"longitude\":\"\",\"accuracy\":\"\"";
    std::string lang = N(f.langList);
    int cnt = 1;
    // 与 web-ui collectFp 一致：按逗号/分号/换行切分计数（单行 EDIT 无换行，但兼容粘贴值）
    { size_t p = 0; cnt = 0; while (p <= lang.size()) { size_t e = lang.find_first_of(",;\n", p); cnt++; if (e == std::string::npos) break; p = e + 1; } }
    o += ",\"language\":\"" + lang + "\",\"language_switch\":\"" + std::string(cnt <= 1 ? "1" : "0") + "\"";
    // 兼容别名：官方 sunBrowserParams/历史 static 用 Langs（首字母大写），与 language 同值；
    // 读取侧两者都认（ui 有 language 即用），保存侧双写，避免“改了语言不生效”。
    o += ",\"Langs\":\"" + lang + "\"";
    o += ",\"AcceptLang\":\"" + buildAccept(lang) + "\"";
    // 语言三键对齐（main.min.js setLangs/setUILanguage 实测）：
    // ui 界面语言：follow_lang -> pageLanguageSwitch=1（跟随语言）；custom -> 0 + pageLanguage。
    // uiLang 存档键为 uiLang（follow_lang/custom），页面语言存档键为 pageLanguage。
    o += ",\"pageLanguageSwitch\":\"" + std::string(f.uiLang == L"custom" ? "0" : "1") + "\"";
    // 页面语言只存单tag（--lang 同源；"en-US,en"类多值取首项，避免浏览器回落中文）
    if (f.uiLang == L"custom") {
        std::string pg = N(f.pageLang);
        size_t e = pg.find_first_of(",;\n \t\"'");
        if (e != std::string::npos) pg = pg.substr(0, e);
        size_t a = pg.find_first_not_of(" \t\"'");
        if (a != std::string::npos) {
            size_t b = pg.find_last_not_of(" \t\"'");
            pg = pg.substr(a, b - a + 1);
        } else pg.clear();
        if (pg.empty()) {
            // 为空则从语言列表首项派生，与回填一致
            size_t c = lang.find_first_of(",;\n");
            pg = (c == std::string::npos) ? lang : lang.substr(0, c);
            size_t aa = pg.find_first_not_of(" \t\"'");
            if (aa != std::string::npos) {
                size_t bb = pg.find_last_not_of(" \t\"'");
                pg = pg.substr(aa, bb - aa + 1);
            } else pg = "en-US";
        }
        o += ",\"pageLanguage\":\"" + pg + "\"";
    }
    else o += ",\"pageLanguage\":\"\"";
    std::string res = N(f.resolution);
    if (f.resMode == L"custom" && !f.resW.empty() && !f.resH.empty())
        res = N(f.resW) + "_" + N(f.resH);
    o += ",\"screen_resolution\":\"" + res + "\"";
    o += ",\"screenResolution\":\"" + res + "\"";
    std::string cpu = (f.cpuMode == L"real") ? "default" : N(f.cpu);
    std::string ram = (f.ramMode == L"real") ? "default" : N(f.ram);
    o += ",\"hardware_concurrency\":\"" + cpu + "\",\"device_memory\":\"" + ram + "\"";
    std::string dnt = "";
    if (f.doNotTrack == L"open") dnt = "true"; else if (f.doNotTrack == L"close") dnt = "false";
    o += ",\"do_not_track\":\"" + dnt + "\"";
    o += ",\"canvas\":\"" + std::string(f.swCanvas ? "1" : "0") + "\",\"webgl_image\":\"" + std::string(f.swWebglImg ? "1" : "0") + "\"";
    o += ",\"audio\":\"" + std::string(f.swAudio ? "1" : "0") + "\",\"client_rects\":\"" + std::string(f.swClientRects ? "1" : "0") + "\"";
    // 媒体设备三数量：官方钳制 <=0 按 1、>=9 按 8（与 web-ui clampMedia 一致）
    auto clampMedia = [](const std::wstring& s) -> std::string {
        int n = 0;
        try { n = std::stoi(N(s)); } catch (...) { n = 1; }
        if (n <= 0) n = 1;
        if (n >= 9) n = 8;
        return std::to_string(n);
    };
    o += ",\"media_devices\":\"" + N(f.mediaDevices) + "\"";
    o += ",\"media_devices_num\":{\"audioinput_num\":" + clampMedia(f.mediaIn) +
         ",\"videoinput_num\":" + clampMedia(f.mediaVid) +
         ",\"audiooutput_num\":" + clampMedia(f.mediaOut) + "}";
    o += ",\"speech_switch\":\"" + std::string(f.swSpeech ? "1" : "0") + "\"";
    o += ",\"webgl\":\"" + std::string(f.webglMeta == L"custom" ? "2" : "0") + "\"";
    if (f.webglMeta == L"custom") {
        // webgpu_switch：disabled->0，其余 1；custom 时追加适配器 vendor/architecture
        std::string wsw = (f.webgpu == L"disabled") ? "0" : "1";
        o += ",\"webgl_config\":{\"unmasked_vendor\":\"" + JEsc(f.vendor) +
             "\",\"unmasked_renderer\":\"" + JEsc(f.renderer) +
             "\",\"webgpu\":{\"webgpu_switch\":\"" + wsw + "\"";
        if (f.webgpu == L"custom")
            o += ",\"gpu_adapterinfo_vendor\":\"" + JEsc(f.gpuVendor) +
                 "\",\"gpu_adapterinfo_architecture\":\"" + JEsc(f.gpuArch) + "\"";
        o += "}}";
        o += ",\"webgpu_switch\":\"" + wsw + "\"";
    }
    std::string macm = (f.macMode == L"custom") ? "2" : "0";
    o += ",\"mac_address_config\":{\"model\":\"" + macm + "\",\"address\":\"" + (macm == "2" ? N(f.mac) : "") + "\"}";
    std::string dsw = "0";
    if (f.devNameMode == L"random") dsw = "1"; else if (f.devNameMode == L"custom") dsw = "2";
    o += ",\"device_name\":\"" + JEsc(f.devName) + "\",\"device_name_switch\":\"" + dsw + "\"";
    std::string spt = "";
    if (f.portScan == L"open") spt = "1"; else if (f.portScan == L"close") spt = "0";
    o += ",\"scan_port_type\":\"" + spt + "\",\"allow_scan_ports\":\"" + N(f.whitePorts) + "\"";
    // 硬件加速：open->gpu:0+gpuSwitch:1；close->gpu:2；default 不写（空串）
    if (f.hardwareAccel == L"open") o += ",\"gpu\":\"0\",\"gpuSwitch\":\"1\"";
    else if (f.hardwareAccel == L"close") o += ",\"gpu\":\"2\"";
    // TLS：open->tlsSwitch:1 + tls 黑名单；否则 tlsSwitch:0
    o += ",\"tlsSwitch\":\"" + std::string(f.disableTls == L"open" ? "1" : "0") + "\"";
    if (f.disableTls == L"open") o += ",\"tls\":\"" + JEsc(f.tlsBlacklist) + "\"";
    // 字体：all->["all"]（语义标记；真正的 DisabledFonts 由 FpBuildDisabledFontsJson() 按 asar 生成，
    // 见 F_OK 保存分支）；custom->按逗号/中文逗号/换行切分数组（与 web-ui split(/[,，\n]+/) 一致）
    if (f.fontMode == L"all") o += ",\"fonts\":[\"all\"]";
    else o += ",\"fonts\":" + FpFontsToJsonArray(f.fonts);
    o += ",\"ua\":\"" + JEsc(f.ua) + "\"";
    // 代理链（官方 static.ProxyChain 数组，与 main.min.js setProxy 写入格式一致）：
    // [{scheme,host,port,account,password}]；proxyType 空/noProxy/缺 host-port 即 []（直连）。
    // 保存链路（F_OK）原样写 static，protectFill 以缓存为准已有值时不覆盖空值，见下方。
    {
        std::string scheme = N(f.proxyType), host = N(f.proxyHost),
                      port = N(f.proxyPort), user = N(f.proxyUser), pass = N(f.proxyPass);
        if (!scheme.empty() && scheme != "noProxy" && scheme != "noproxy" &&
            !host.empty() && !port.empty()) {
            o += ",\"ProxyChain\":[{\"scheme\":\"" + JEsc(f.proxyType) +
                 "\",\"host\":\"" + JEsc(f.proxyHost) +
                 "\",\"port\":\"" + JEsc(f.proxyPort) +
                 "\",\"account\":\"" + JEsc(f.proxyUser) +
                 "\",\"password\":\"" + JEsc(f.proxyPass) + "\"}]";
        } else {
            o += ",\"ProxyChain\":[]";
        }
    }
    o += "}";
    return o;
}
// fp_ui.cpp — part 2/4：控件 id 表 + 随机库 + 默认值
enum FpCtl {
    F_BASE = 2000,
    F_BROWSER, F_OS, F_UAPRESET, F_UA, F_SHUFFLEUA,
    F_PDATADIR, F_PBROWSERDIR, F_BROWSEDATA, F_BROWSEBROWSER, // A2 目录+浏览（数据目录单行，内核由浏览器目录推导）
    F_PTYPE, F_PHOST, F_PPORT, F_PUSER, F_PPASS, F_PTEST, F_PSAVE, F_PSTATUS,
    F_COOKIE, F_MERGECOOKIE, F_COOKIEIMPORT, F_REMARK,
    F_WEBRTC, F_WEBRTCIP, F_TZM, F_TZ, F_GEOM, F_GEOIP, F_LAT, F_LNG, F_ACC,
    F_LANGM, F_LANGLIST, F_UILANG, F_PAGELANG,
    F_RESM, F_RES, F_RESW, F_RESH,
    F_FONTM, F_FONTS, F_SHUFFLEFONTS,
    F_SWCVS, F_SWWGL, F_SWAUD, F_SWRECT, F_SWSPEECH,
    F_MEDIA, F_MIN, F_MVID, F_MOUT,
    F_WGLM, F_VENDOR, F_RENDERER, F_SHUFFLERDR,
    F_WGPU, F_GVENDOR, F_GARCH,
    F_CPUM, F_CPU, F_RAMM, F_RAM,
    F_DEVM, F_DEVNAME, F_SHUFFLEDEV,
    F_MACM, F_MAC, F_SHUFFLEMAC,
    F_DNT, F_PORTSCAN, F_WPORTS,
    F_HWACC, F_TLSM, F_TLS,
    F_ARGS,
    F_MAXTOUCH, F_FLASH, F_GYRO, F_NETINFO,
    F_CHPLAT, F_CHVER, F_CHARCH, F_CHMODEL, F_CHMOBILE, F_CHBITNESS, F_CHWOW64,
    F_OK, F_CANCEL, F_RANDOM, F_IMPORT,
    F_TAB, F_STATUS,
    F_END
};

static const wchar_t* kRenderers[] = {
    L"ANGLE (Intel, Intel(R) HD Graphics (0x00002E12) Direct3D11 vs_5_0 ps_5_0, D3D11)",
    L"ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)",
    L"ANGLE (AMD, AMD Radeon RX 6700 XT Direct3D11 vs_5_0 ps_5_0, D3D11)",
    L"Apple M2 Pro (Metal 3.0)", L"Apple M1 Max (Metal 2.4)",
};
static const wchar_t* kFonts[] = {
    L"Arial, Calibri, Cambria Math, Candara, Comic Sans MS, Consolas, Constantia (206)",
    L"Helvetica, PingFang SC, Hiragino Sans GB, Microsoft YaHei, Segoe UI (184)",
    L"San Francisco, Monaco, Menlo, Apple Color Emoji, Noto Color Emoji (195)",
};
// ---- asar 1:1 移植字体表（main.min.js 内嵌 u[]，181 条含重复，顺序保留）----
// 用途1: fonts=all 时 DisabledFonts = getFonts - mobileFonts（setScreenResolution 尾部，mobileFonts=内嵌 c[] 12 条）
// 用途2: Fakefonts = 伪装 platform 查表取键、本机 platform 查表轮转取值（setFakeFonts 全文移植见 FpBuildFakefontsJson）
// 云端表（FINGERPRINT_FONTS_CONFIG 下发 win32/darwin/linux）离线不可达：win32/darwin/linux 用 u[] 全集代替；
// mobileFonts 用内嵌 c[] 精确 12 条。注意 asar 原表含拼写 "Caurier Regular"（Courier 笔误），1:1 保留。
static const wchar_t* kAsarFontsU[] = {
    L"Arial",L"Calibri",L"Cambria",L"Cambria Math",L"Candara",L"Comic Sans MS",
    L"Comic Sans MS Bold",L"Comic Sans",L"Consolas",L"Constantia",L"Corbel",
    L"Courier New",L"Caurier Regular",L"Ebrima",L"Fixedsys Regular",
    L"Franklin Gothic",L"Gabriola Regular",L"Gadugi",L"Georgia",
    L"HoloLens MDL2 Assets Regular",L"Impact Regular",L"Javanese Text Regular",
    L"Leelawadee UI",L"Lucida Console Regular",L"Lucida Sans Unicode Regular",
    L"Malgun Gothic",L"Microsoft Himalaya Regular",L"Microsoft JhengHei",
    L"Microsoft JhengHei UI",L"Microsoft PhangsPa",L"Microsoft Sans Serif Regular",
    L"Microsoft Tai Le",L"Microsoft YaHei",L"Microsoft YaHei UI",
    L"Microsoft Yi Baiti Regular",L"MingLiU_HKSCS-ExtB Regular",
    L"MingLiu-ExtB Regular",L"Modern Regular",L"Mongolia Baiti Regular",
    L"MS Gothic Regular",L"MS PGothic Regular",L"MS Sans Serif Regular",
    L"MS Serif Regular",L"MS UI Gothic Regular",L"MV Boli Regular",
    L"Myanmar Text",L"Nimarla UI",L"MV Boli Regular",L"Myanmar Tet",
    L"Nirmala UI",L"NSimSun Regular",L"Palatino Linotype",
    L"PMingLiU-ExtB Regular",L"Roman Regular",L"Script Regular",
    L"Segoe MDL2 Assets Regular",L"Segoe Print",L"Segoe Script",L"Segoe UI",
    L"Segoe UI Emoji Regular",L"Segoe UI Historic Regular",
    L"Segoe UI Symbol Regular",L"SimSun Regular",
    L"SimSun-ExtB Regular",L"Sitka Banner",L"Sitka Display",L"Sitka Heading",
    L"Sitka Small",L"Sitka Subheading",L"Sitka Text",L"Small Fonts Regular",
    L"Sylfaen Regular",L"Symbol Regular",L"System Bold",L"Tahoma",L"Terminal",
    L"Times New Roman",L"Trebuchet MS",L"Verdana",L"Webdings Regular",
    L"Wingdings Regular",L"Yu Gothic",L"Yu Gothic UI",L"Arial",L"Arial Black",
    L"Calibri",L"Calibri Light",L"Cambria",L"Cambria Math",L"Candara",
    L"Comic Sans MS",L"Consolas",L"Constantia",L"Corbel",L"Courier",
    L"Courier New",L"Ebrima",L"Fixedsys",L"Franklin Gothic Medium",
    L"Gabriola",L"Gadugi",L"Georgia",L"HoloLens MDL2 Assets",L"Impact",
    L"Javanese Text",L"Leelawadee UI",L"Leelawadee UI Semilight",
    L"Lucida Console",L"Lucida Sans Unicode",L"MS Gothic",L"MS PGothic",
    L"MS Sans Serif",L"MS Serif",L"MS UI Gothic",L"MV Boli",L"Malgun Gothic",
    L"Malgun Gothic Semilight",L"Marlett",L"Microsoft Himalaya",
    L"Microsoft JhengHei",L"Microsoft JhengHei Light",L"Microsoft JhengHei UI",
    L"Microsoft JhengHei UI Light",L"Microsoft New Tai Lue",
    L"Microsoft PhagsPa",L"Microsoft Sans Serif",L"Microsoft Tai Le",
    L"Microsoft YaHei",L"Microsoft YaHei Light",L"Microsoft YaHei UI",
    L"Microsoft YaHei UI Light",L"Microsoft Yi Baiti",L"MingLiU-ExtB",
    L"MingLiU_HKSCS-ExtB",L"Modern",L"Mongolian Baiti",L"Myanmar Text",
    L"NSimSun",L"Nirmala UI",L"Nirmala UI Semilight",L"PMingLiU-ExtB",
    L"Palatino Linotype",L"Roman",L"Script",L"Segoe MDL2 Assets",
    L"Segoe Print",L"Segoe Script",L"Segoe UI",L"Segoe UI Black",
    L"Segoe UI Emoji",L"Segoe UI Historic",L"Segoe UI Light",
    L"Segoe UI Semibold",L"Segoe UI Semilight",L"Segoe UI Symbol",
    L"SimSun",L"SimSun-ExtB",L"Sitka Banner",L"Sitka Display",
    L"Sitka Heading",L"Sitka Small",L"Sitka Subheading",L"Sitka Text",
    L"Small Fonts",L"Sylfaen",L"Symbol",L"System",L"Tahoma",L"Terminal",
    L"Times New Roman",L"Trebuchet MS",L"Verdana",L"Webdings",L"Wingdings",
    L"Yu Gothic",L"Yu Gothic Light",L"Yu Gothic Medium",L"Yu Gothic UI",
    L"Yu Gothic UI Light",L"Yu Gothic UI Semibold",L"Yu Gothic UI Semilight",
};
static const int kAsarFontsUCount = 181;
static const wchar_t* kAsarMobileFonts[] = {
    L"Arial",L"Courier",L"Courier New",L"Georgia",L"Helvetica",L"Monaco",
    L"Palatino",L"Tahoma",L"Times",L"Times New Roman",L"Verdana",L"Baskerville",
};
static const int kAsarMobileFontsCount = 12;
// ---- asar 1:1 setFakeFonts/setFonts 函数体（main.min.js 全文移植；表已在上方定义）----
static std::string FpBuildFakefontsJson(const std::wstring& platform) {
    // 注意 MacIntel 走 darwin 表：离线云端表不可达，用 u[] 全集代替（与 Win32 同表）。
    // 因此 useMobile 仅含真正的移动系（Linux armv*/i686/iPhone/Windows Phone），MacIntel 不在其中。
    bool useMobile = (platform == L"Linux armv7I" ||
        platform == L"Linux armv8I" || platform == L"Linux armv81" ||
        platform == L"Linux i686" || platform == L"iPhone" ||
        platform == L"Windows Phone");
    // 键表：Win32/MacIntel/Linux x86_64->u[]全集（181）；移动系->mobileFonts 12 条
    int keyCount = useMobile ? kAsarMobileFontsCount : kAsarFontsUCount;
    std::string o = "{";
    for (int t = 0; t < keyCount; t++) {
        std::wstring key = useMobile ? kAsarMobileFonts[t] : kAsarFontsU[t];
        // 值表：本机 win32 -> u[] 全集轮转（asar win32[t%len]，云端缺失回退同表）
        std::wstring val = kAsarFontsU[t % kAsarFontsUCount];
        if (t) o += ",";
        o += "\"" + N(key) + "\":\"" + N(val) + "\"";
    }
    o += "}";
    return o;
}
// DisabledFonts（fonts=all）：getFonts(u[] 181 条) - mobileFonts(12 条)，顺序保留含重复（asar filter 原样，不去重）
static std::string FpBuildDisabledFontsJson() {
    std::string arr = "[";
    bool first = true;
    for (int i = 0; i < kAsarFontsUCount; i++) {
        std::wstring w = kAsarFontsU[i];
        bool isMobile = false;
        for (int j = 0; j < kAsarMobileFontsCount; j++)
            if (w == kAsarMobileFonts[j]) { isMobile = true; break; }
        if (isMobile) continue;
        if (!first) arr += ",";
        first = false;
        arr += "\"" + N(w) + "\"";
    }
    arr += "]";
    return arr;
}
// 平台下拉值（web-ui os 胶囊 win|mac|linux|android|ios）-> asar e.platform（setFakeFonts switch 用）
static std::wstring FpOsToAsarPlatform(const std::wstring& os) {
    // 与 official initBrowser.platform 同一张表（见 FpOsToOfficialPlatform）
    return W(FpOsToOfficialPlatform(N(os)));
}
static const wchar_t* kTz[] = {
    L"Etc/GMT+12", L"Pacific/Midway", L"Pacific/Honolulu", L"America/Anchorage",
    L"America/Los_Angeles", L"America/Denver", L"America/Chicago", L"America/New_York",
    L"America/Halifax", L"America/Sao_Paulo", L"Atlantic/Azores", L"UTC",
    L"Europe/London", L"Europe/Paris", L"Europe/Berlin", L"Asia/Dubai",
    L"Asia/Karachi", L"Asia/Dhaka", L"Asia/Bangkok", L"Asia/Shanghai",
    L"Asia/Tokyo", L"Australia/Sydney", L"Pacific/Auckland",
};

static std::wstring FpRandomMac() {
    wchar_t b[32];
    swprintf_s(b, L"%02X-%02X-%02X-%02X-%02X-%02X",
        rand() % 256, rand() % 256, rand() % 256, rand() % 256, rand() % 256, rand() % 256);
    return b;
}
static std::wstring FpRandomDev() {
    static const wchar_t* pre[] = { L"DESKTOP-", L"LAPTOP", L"PC-WIN11-", L"MACBOOK-PRO-" };
    static const wchar_t* ch = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::wstring s = pre[rand() % 4];
    for (int i = 0; i < 7; i++) s += ch[rand() % 36];
    return s;
}
static std::wstring FpBuildUA(const std::wstring& os, const std::wstring& ver) {
    std::wstring v = ver.empty() ? L"152" : ver;
    if (os == L"mac") return L"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + v + L".0.0.0 Safari/537.36";
    if (os == L"linux") return L"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + v + L".0.0.0 Safari/537.36";
    if (os == L"android") return L"Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + v + L".0.0.0 Mobile Safari/537.36";
    if (os == L"ios") return L"Mozilla/5.0 (iPhone; CPU iPhone OS 17_4 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Mobile/15E148 Safari/604.1";
    return L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + v + L".0.0.0 Safari/537.36";
}

static void FpFormDefaults(FpFormData& f, const std::wstring& profileName) {
    f.browser = L"sun"; f.kernelVer = L"chrome143"; f.browserDir = profileName;
    // A2 必填：默认取全局生效值（打开后 FpFill 会用 Eff* 覆盖为有效值）
    f.profDataDir.clear(); f.profBrowserDir.clear();
    f.os = L"win"; f.uaPreset = L"152"; f.ua = FpBuildUA(L"win", L"152");
    f.proxyType = L"socks5";
    f.webrtc = L"proxy"; f.timezoneMode = L"custom"; f.timezone = L"Asia/Shanghai";
    f.geoMode = L"allow"; f.geoIp = L"custom";
    f.lat = L"31.2304"; f.lng = L"121.4737"; f.accuracy = L"1000";
    f.langMode = L"custom"; f.langList = L"en-US,en";
    f.uiLang = L"follow_lang";
    f.resMode = L"preset"; f.resolution = L"none";
    f.fontMode = L"custom"; f.fonts = kFonts[0];
    f.swCanvas = false; f.swWebglImg = false; f.swAudio = true;
    f.swClientRects = true; f.swSpeech = true;
    f.mediaDevices = L"0"; f.mediaIn = L"1"; f.mediaVid = L"1"; f.mediaOut = L"1";
    f.webglMeta = L"custom"; f.vendor = L"Google Inc. (Intel)"; f.renderer = kRenderers[0];
    f.webgpu = L"follow_webgl";
    f.cpuMode = L"custom"; f.cpu = L"20";
    f.ramMode = L"custom"; f.ram = L"8";
    f.devNameMode = L"custom"; f.devName = L"LAPTOP31PX2UO";
    f.macMode = L"custom"; f.mac = L"00-50-43-3C-49-4B";
    f.doNotTrack = L"default"; f.portScan = L"open";
    f.hardwareAccel = L"default"; f.disableTls = L"close";
}
// fp_ui.cpp — 单页滚动布局（对齐 web-ui/index.html fp-row 顺序，无 Tab，无云端）
// 版式常量：窗口 860x640（客户确认）；内容区宽 828；行高按网页 fp-row(14px padding+内容) 逐行累加。
// 数据层（FpFormData/FpFill/FpCollect/ToUiJson/FromUiJson/ToFpConfig/Build*）零改动，只换父窗口与坐标。
static const int kFpWinW = 860;
static const int kFpWinH = 640;
static const int kFpContentW = 828;   // 内容区宽（窗口 860 - 边距 2*16）
static const int kFpContentH = 1904;  // 内容总高（原 1744 + 页5 系统扩展 160：1744..1904）

// 单页窗口状态（滚动位置 + 内容容器；Tab 相关已删除，见 git 历史）
// hPage 子类化：STATIC 父容器默认把 BUTTON 的 WM_COMMAND 吃掉（BN_CLICKED 不向上传），
// 浏览按钮（F_BROWSEDATA/F_BROWSEBROWSER）及页内其它按钮靠它转发到 FpWndProc。
// 根因：浏览按钮“没反应”即 hPage 吞消息；转发后按 id 原样投递给顶层 hDlg。
static LRESULT CALLBACK FpPageProc(HWND hp, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR dwRef) {
    if (msg == WM_COMMAND || msg == WM_NOTIFY) {
        HWND hTop = (HWND)dwRef;
        if (hTop && ::IsWindow(hTop)) {
            LRESULT r = ::SendMessageW(hTop, msg, wp, lp);
            if (msg == WM_COMMAND) return 0;
            return r;
        }
    }
    return ::DefSubclassProc(hp, msg, wp, lp);
}
struct FpWnd {
    HWND hDlg = NULL, hScroll = NULL, hStatus = NULL;
    HWND ctl[F_END - F_BASE] = {};
    FpFormData form;
    Config cfg;
    std::wstring profile;
    bool saved = false;
    int scrollY = 0; // 当前滚动偏移（0..kFpContentH-可见高）
    int cookieLenLogged = 0; // Cookie 框长度日志（只记空<->有内容转换，避免逐键刷屏）
    int cookieLenOpened = 0; // 打开时的 Cookie 长度：区分“主动清空”和“本来就是空”
};

static HWND FpMk(HWND p, const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id, HINSTANCE hi) {
    return ::CreateWindowW(cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, p, (HMENU)(INT_PTR)id, hi, NULL);
}
static void FpMkLabel(HWND p, FpWnd* w, int id, const wchar_t* t, int x, int y, int ww) {
    w->ctl[id - F_BASE] = FpMk(p, L"STATIC", t, SS_LEFT, x, y, ww, 20, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
}
static void FpMkEdit(HWND p, FpWnd* w, int id, int x, int y, int ww, int hh = 24, bool multi = false) {
    DWORD st = WS_BORDER | ES_AUTOHSCROLL;
    if (multi)
        // Cookie 是 16KB+ JSON：单行框只能看见开头，粘贴多行 JSON 还会被吃掉换行，
        // 表现为“改不了/保存丢失”。改成多行可编辑框（Enter=换行，Ctrl+V 全量粘贴）。
        st = WS_BORDER | WS_VSCROLL | ES_LEFT | ES_AUTOHSCROLL | ES_AUTOVSCROLL |
             ES_MULTILINE | ES_WANTRETURN;
    w->ctl[id - F_BASE] = FpMk(p, L"EDIT", L"", st, x, y, ww, hh, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
    // EDIT 默认文本上限 30000 字符；cookie（16KB+）等大字段会被静默截断，
    // 截断的 JSON 下次读回即错位，表现为“无法保存”。统一放宽到 1MB。
    ::SendMessageW(w->ctl[id - F_BASE], EM_SETLIMITTEXT, 1048576, 0);
}
static void FpMkBtn(HWND p, FpWnd* w, int id, const wchar_t* t, int x, int y, int ww, int hh = 28) {
    w->ctl[id - F_BASE] = FpMk(p, L"BUTTON", t, BS_PUSHBUTTON, x, y, ww, hh, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
}
static void FpMkCheck(HWND p, FpWnd* w, int id, const wchar_t* t, int x, int y, int ww) {
    w->ctl[id - F_BASE] = FpMk(p, L"BUTTON", t, BS_AUTOCHECKBOX, x, y, ww, 22, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
}
static void FpMkCombo(HWND p, FpWnd* w, int id, int x, int y, int ww) {
    HWND c = FpMk(p, L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, x, y, ww, 200, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
    w->ctl[id - F_BASE] = c;
}
static void FpComboAdd(HWND c, const wchar_t* t) { ::SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t); }
static void FpComboSel(HWND c, const wchar_t* v) {
    int n = (int)::SendMessageW(c, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        wchar_t b[256]{};
        ::SendMessageW(c, CB_GETLBTEXT, i, (LPARAM)b);
        if (wcscmp(b, v) == 0) { ::SendMessageW(c, CB_SETCURSEL, i, 0); return; }
    }
    if (n > 0) ::SendMessageW(c, CB_SETCURSEL, 0, 0);
}
static std::wstring FpComboGet(HWND c) {
    int i = (int)::SendMessageW(c, CB_GETCURSEL, 0, 0);
    if (i < 0) return L"";
    wchar_t b[512]{};
    ::SendMessageW(c, CB_GETLBTEXT, i, (LPARAM)b);
    return b;
}
static std::wstring FpGet(HWND c) {
    int n = ::GetWindowTextLengthW(c);
    std::wstring s((size_t)(n > 0 ? n : 0), 0);
    if (n > 0) ::GetWindowTextW(c, &s[0], n + 1);
    return s;
}
static void FpSet(HWND c, const std::wstring& s) { ::SetWindowTextW(c, s.c_str()); }

// 单页滚动容器：在父窗口客户区内建一个 WS_VSCROLL 子窗口，内容画在上方大画布上
static HWND FpMkScroll(HWND parent, HINSTANCE hi, int x, int y, int w, int h) {
    return ::CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        x, y, w, h, parent, (HMENU)(INT_PTR)F_TAB, hi, NULL);
}
// 滚动 helper：按网页 fp-row 行高累加，内容总高 kFpContentH
static void FpScrollTo(FpWnd* w, int y) {
    RECT rc{};
    ::GetClientRect(w->hScroll, &rc);
    int visH = rc.bottom - rc.top;
    int maxY = kFpContentH - visH;
    if (maxY < 0) maxY = 0;
    if (y < 0) y = 0;
    if (y > maxY) y = maxY;
    int dy = w->scrollY - y;
    w->scrollY = y;
    ::ScrollWindowEx(w->hScroll, 0, dy, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE);
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = y;
    ::SetScrollInfo(w->hScroll, SB_VERT, &si, TRUE);
}
static void FpScrollInit(FpWnd* w) {
    RECT rc{};
    ::GetClientRect(w->hScroll, &rc);
    int visH = rc.bottom - rc.top;
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = kFpContentH;
    si.nPage = visH;
    si.nPos = 0;
    ::SetScrollInfo(w->hScroll, SB_VERT, &si, TRUE);
    w->scrollY = 0;
}
// fp_ui.cpp — 单页控件排布（对齐 web-ui/index.html fp-row 顺序，无 Tab）
// 行高：A(浏览器 32)+A2(数据/浏览器目录 56)+B(系统/UA 112)+Cookie/备注/代理/WebRTC/时区/地理/语言/界面语言/分辨率/字体
// +噪音+WebGL+WebGPU+CPU+RAM+设备名+MAC+DNT+端口+加速+TLS+启动参数 ≈ 1744
static void FpBuildPages(FpWnd* w, HWND p, HINSTANCE hi) {
    (void)hi;
    // ---- A. 浏览器（y 8..38；内核由浏览器目录尾段推导，不再单独下拉）----
    FpMkLabel(p, w, F_BROWSER, L"浏览器", 12, 12, 80);
    FpMkCombo(p, w, F_BROWSER, 100, 10, 570);
    FpComboAdd(w->ctl[F_BROWSER - F_BASE], L"sun - SunBrowser");
    FpComboAdd(w->ctl[F_BROWSER - F_BASE], L"flower - FlowerBrowser");
    // ---- A2. 数据目录 + 浏览器目录（y 46..102；指纹目录=数据目录+环境名，不再单列一行）----
    FpMkLabel(p, w, F_PDATADIR, L"数据目录", 12, 50, 80);
    FpMkEdit(p, w, F_PDATADIR, 100, 48, 500);
    FpMkBtn(p, w, F_BROWSEDATA, L"浏览...", 606, 46, 64);
    FpMkLabel(p, w, F_PBROWSERDIR, L"浏览器目录", 12, 76, 80);
    FpMkEdit(p, w, F_PBROWSERDIR, 100, 74, 500);
    FpMkBtn(p, w, F_BROWSEBROWSER, L"浏览...", 606, 72, 64);
    // ---- B. 系统/UA（y 128..170）----
    FpMkLabel(p, w, F_OS, L"系统", 12, 128, 80);
    FpMkCombo(p, w, F_OS, 100, 126, 200);
    FpComboAdd(w->ctl[F_OS - F_BASE], L"win - Windows");
    FpComboAdd(w->ctl[F_OS - F_BASE], L"mac - macOS");
    FpComboAdd(w->ctl[F_OS - F_BASE], L"linux - Linux");
    FpComboAdd(w->ctl[F_OS - F_BASE], L"android - Android");
    FpComboAdd(w->ctl[F_OS - F_BASE], L"ios - iOS");
    FpMkLabel(p, w, F_UAPRESET, L"UA版本", 320, 128, 60);
    FpMkEdit(p, w, F_UAPRESET, 380, 126, 80);
    FpMkBtn(p, w, F_SHUFFLEUA, L"换UA", 470, 124, 80);
    FpMkLabel(p, w, F_UA, L"User-Agent", 12, 160, 80);
    FpMkEdit(p, w, F_UA, 100, 158, 570);
    FpMkLabel(p, w, F_COOKIE, L"Cookie", 12, 194, 80);
    FpMkEdit(p, w, F_COOKIE, 100, 192, 570, 100, true);
    FpMkBtn(p, w, F_MERGECOOKIE, L"合并Cookie", 100, 298, 110);
    FpMkBtn(p, w, F_IMPORT, L"从目录导入指纹", 220, 298, 140);
    FpMkBtn(p, w, F_COOKIEIMPORT, L"从浏览器导入", 366, 298, 120);
    FpMkLabel(p, w, F_REMARK, L"备注", 12, 338, 80);
    FpMkEdit(p, w, F_REMARK, 100, 336, 640);
    // ---- C. 代理（y 378..474）----
    FpMkLabel(p, w, F_PTYPE, L"代理", 12, 384, 80);
    FpMkCombo(p, w, F_PTYPE, 100, 380, 120);
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"socks5");
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"http");
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"https");
    FpMkLabel(p, w, F_PHOST, L"主机", 230, 382, 50);
    FpMkEdit(p, w, F_PHOST, 280, 380, 180);
    FpMkLabel(p, w, F_PPORT, L"端口", 470, 382, 40);
    FpMkEdit(p, w, F_PPORT, 510, 380, 80);
    FpMkLabel(p, w, F_PUSER, L"账号", 12, 416, 80);
    FpMkEdit(p, w, F_PUSER, 100, 414, 180);
    FpMkLabel(p, w, F_PPASS, L"密码", 290, 416, 40);
    FpMkEdit(p, w, F_PPASS, 330, 414, 150);
    FpMkBtn(p, w, F_PTEST, L"测速/检测", 490, 412, 90);
    FpMkBtn(p, w, F_PSAVE, L"保存为代理", 590, 412, 110);
    FpMkLabel(p, w, F_PSTATUS, L"未检测", 12, 448, 400);
    // ---- 1. WebRTC（forward 直通；proxy 必须有伪装 IP）----
    FpMkLabel(p, w, F_WEBRTC, L"WebRTC", 12, 482, 70);
    FpMkCombo(p, w, F_WEBRTC, 88, 480, 250);
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"forward - 转发");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"proxy - 替换");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"disabled - 禁用");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"disable_udp - 禁用UDP");
    FpMkLabel(p, w, F_WEBRTCIP, L"伪装IP", 350, 482, 65);
    FpMkEdit(p, w, F_WEBRTCIP, 420, 480, 250);
    ::SendMessageW(w->ctl[F_WEBRTCIP - F_BASE], EM_SETCUEBANNER, TRUE,
        (LPARAM)L"proxy 模式填写 IPv4 / IPv6 地址");
    FpMkLabel(p, w, F_TZM, L"时区模式", 12, 524, 80);
    FpMkCombo(p, w, F_TZM, 100, 522, 150);
    FpComboAdd(w->ctl[F_TZM - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_TZM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_TZ, 260, 522, 320);
    for (auto t : kTz) FpComboAdd(w->ctl[F_TZ - F_BASE], t);
    FpMkLabel(p, w, F_GEOM, L"地理", 12, 594, 80);
    FpMkCombo(p, w, F_GEOM, 100, 592, 150);
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"ask - 询问");
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"allow - 允许");
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"block - 禁止");
    FpMkCombo(p, w, F_GEOIP, 260, 592, 150);
    FpComboAdd(w->ctl[F_GEOIP - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_GEOIP - F_BASE], L"custom - 自定义");
    FpMkLabel(p, w, F_LAT, L"纬/经/精度", 12, 628, 80);
    FpMkEdit(p, w, F_LAT, 100, 626, 120);
    FpMkEdit(p, w, F_LNG, 230, 626, 120);
    FpMkEdit(p, w, F_ACC, 360, 626, 100);
    FpMkLabel(p, w, F_LANGM, L"语言模式", 12, 720, 80);
    FpMkCombo(p, w, F_LANGM, 100, 718, 150);
    FpComboAdd(w->ctl[F_LANGM - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_LANGM - F_BASE], L"custom - 自定义");
    FpMkEdit(p, w, F_LANGLIST, 260, 718, 260, 48);
    // 语言三键说明（main.min.js setLangs/setUILanguage 实测）：
    // 语言列表决定 static.Langs/AcceptLang（浏览器 Accept-Language/JS 语言）；
    // 界面语言 custom 时 pageLanguage 决定 --lang（浏览器 UI 中文/英文显示）。
    FpMkLabel(p, w, F_UILANG, L"界面语言", 12, 774, 80);
    FpMkCombo(p, w, F_UILANG, 100, 772, 180);
    FpComboAdd(w->ctl[F_UILANG - F_BASE], L"follow_lang - 跟语言");
    FpComboAdd(w->ctl[F_UILANG - F_BASE], L"custom - 自定义");
    FpMkLabel(p, w, F_PAGELANG, L"页面语言", 290, 774, 70);
    FpMkEdit(p, w, F_PAGELANG, 370, 772, 140);
        // ---- 6. 分辨率（y 862..；整体下移 48）----
    FpMkLabel(p, w, F_RESM, L"分辨率", 12, 864, 80);
    FpMkCombo(p, w, F_RESM, 100, 862, 140);
    FpComboAdd(w->ctl[F_RESM - F_BASE], L"preset - 预定义");
    FpComboAdd(w->ctl[F_RESM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_RES, 250, 862, 170);
    FpComboAdd(w->ctl[F_RES - F_BASE], L"none");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1920_1080");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"2560_1440");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1440_900");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1366_768");
    FpMkEdit(p, w, F_RESW, 430, 862, 70);
    FpMkEdit(p, w, F_RESH, 510, 862, 70);
    FpMkLabel(p, w, F_FONTM, L"字体", 12, 906, 80);
    FpMkCombo(p, w, F_FONTM, 100, 904, 150);
    FpComboAdd(w->ctl[F_FONTM - F_BASE], L"all - 默认");
    FpComboAdd(w->ctl[F_FONTM - F_BASE], L"custom - 自定义");
    FpMkBtn(p, w, F_SHUFFLEFONTS, L"换一换", 260, 904, 80);
    FpMkEdit(p, w, F_FONTS, 100, 936, 560, 44);
    // ---- 8. 硬件噪音开关（y 988..；整体下移 48）----
    FpMkCheck(p, w, F_SWCVS, L"Canvas(=1)", 12, 988, 130);
    FpMkCheck(p, w, F_SWWGL, L"WebGL图像(=1)", 150, 988, 150);
    FpMkCheck(p, w, F_SWAUD, L"Audio(=1)", 310, 988, 120);
    FpMkCheck(p, w, F_SWRECT, L"ClientRects(=1)", 440, 988, 150);
    FpMkCheck(p, w, F_SWSPEECH, L"Speech(=1)", 12, 1012, 130);
    FpMkLabel(p, w, F_MEDIA, L"媒体设备", 150, 1014, 80);
    FpMkCombo(p, w, F_MEDIA, 100, 1040, 150);
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"0 - 真实/关闭");
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"1 - 随机");
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"2 - 自定义");
    FpMkEdit(p, w, F_MIN, 260, 1040, 60);
    FpMkEdit(p, w, F_MVID, 330, 1040, 60);
    FpMkEdit(p, w, F_MOUT, 400, 1040, 60);
    // ---- 9. WebGL元数据（y 1092..；整体下移 48）----
    FpMkLabel(p, w, F_WGLM, L"WebGL元数据", 12, 1094, 90);
    FpMkCombo(p, w, F_WGLM, 110, 1092, 150);
    FpComboAdd(w->ctl[F_WGLM - F_BASE], L"real - 真实(0)");
    FpComboAdd(w->ctl[F_WGLM - F_BASE], L"custom - 自定义(2)");
    FpMkCombo(p, w, F_VENDOR, 270, 1092, 220);
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (Intel)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (NVIDIA)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (AMD)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Apple Inc.");
    FpMkEdit(p, w, F_RENDERER, 110, 1126, 440);
    FpMkBtn(p, w, F_SHUFFLERDR, L"随机", 560, 1124, 70);
    // ---- 10. WebGPU（y 1206..；整体下移 48）----
    FpMkLabel(p, w, F_WGPU, L"WebGPU", 12, 1208, 80);
    FpMkCombo(p, w, F_WGPU, 110, 1206, 200);
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"follow_webgl - 跟随(1)");
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"disabled - 禁用(0)");
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"custom - 自定义适配器(2)");
    FpMkLabel(p, w, F_GVENDOR, L"厂商", 320, 1208, 44);
    FpMkEdit(p, w, F_GVENDOR, 368, 1206, 150);
    FpMkLabel(p, w, F_GARCH, L"架构", 528, 1208, 44);
    FpMkEdit(p, w, F_GARCH, 576, 1206, 150);
    // ---- 页3 设备伪装：CPU/RAM/设备名/MAC（整体下移 48）----
    // ---- 11. CPU（y 1276..）----
    FpMkLabel(p, w, F_CPUM, L"CPU模式", 12, 1278, 80);
    FpMkCombo(p, w, F_CPUM, 100, 1276, 150);
    FpComboAdd(w->ctl[F_CPUM - F_BASE], L"real - 真实");
    FpComboAdd(w->ctl[F_CPUM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_CPU, 260, 1276, 180);
    for (auto c : { L"default", L"2", L"4", L"6", L"8", L"10", L"12", L"16", L"20", L"24" }) FpComboAdd(w->ctl[F_CPU - F_BASE], c);
    FpMkLabel(p, w, F_RAMM, L"RAM模式", 12, 1320, 80);
    FpMkCombo(p, w, F_RAMM, 100, 1318, 150);
    FpComboAdd(w->ctl[F_RAMM - F_BASE], L"real - 真实");
    FpComboAdd(w->ctl[F_RAMM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_RAM, 260, 1318, 180);
    for (auto c : { L"default", L"2", L"4", L"6", L"8", L"16", L"32", L"64", L"128" }) FpComboAdd(w->ctl[F_RAM - F_BASE], c);
    // ---- 13. 设备名称（y 1360..；整体下移 48）----
    FpMkLabel(p, w, F_DEVM, L"设备名", 12, 1362, 80);
    FpMkCombo(p, w, F_DEVM, 100, 1360, 150);
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"off - 关闭(0)");
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"random - 随机(1)");
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"custom - 自定义(2)");
    FpMkEdit(p, w, F_DEVNAME, 260, 1360, 220);
    FpMkBtn(p, w, F_SHUFFLEDEV, L"随机", 490, 1358, 70);
    FpMkLabel(p, w, F_MACM, L"MAC", 12, 1404, 80);
    FpMkCombo(p, w, F_MACM, 100, 1402, 150);
    FpComboAdd(w->ctl[F_MACM - F_BASE], L"off - 关闭(0)");
    FpComboAdd(w->ctl[F_MACM - F_BASE], L"custom - 自定义(2)");
    FpMkEdit(p, w, F_MAC, 260, 1402, 220);
    FpMkBtn(p, w, F_SHUFFLEMAC, L"随机", 490, 1400, 70);
    // ---- 页4 高级：DNT/端口/加速/TLS/启动参数（整体下移 48）----
    // ---- 15. Do Not Track（y 1444..）----
    FpMkLabel(p, w, F_DNT, L"DoNotTrack", 12, 1446, 90);
    FpMkCombo(p, w, F_DNT, 110, 1444, 170);
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"open - 开启");
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"close - 关闭");
    FpMkLabel(p, w, F_PORTSCAN, L"端口扫描", 300, 1446, 100);
    FpMkCombo(p, w, F_PORTSCAN, 410, 1444, 150);
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"open - 启用(1)");
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"close - 关闭(0)");
    FpMkLabel(p, w, F_WPORTS, L"白名单端口", 12, 1488, 90);
    FpMkEdit(p, w, F_WPORTS, 110, 1486, 420);
    // ---- 17. 硬件加速（y 1528..；整体下移 48）----
    FpMkLabel(p, w, F_HWACC, L"硬件加速", 12, 1530, 90);
    FpMkCombo(p, w, F_HWACC, 110, 1528, 170);
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"open - 开启");
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"close - 关闭");
    FpMkLabel(p, w, F_TLSM, L"TLS", 300, 1530, 80);
    FpMkCombo(p, w, F_TLSM, 390, 1528, 150);
    FpComboAdd(w->ctl[F_TLSM - F_BASE], L"close - 默认");
    FpComboAdd(w->ctl[F_TLSM - F_BASE], L"open - 自定义(1)");
    FpMkEdit(p, w, F_TLS, 110, 1570, 460);
    // ---- 19. 启动参数（y 1612..；整体下移 48）----
    FpMkLabel(p, w, F_ARGS, L"启动参数", 12, 1614, 90);
    FpMkEdit(p, w, F_ARGS, 110, 1612, 560, 110);
    // ---- 20. 系统扩展（y 1736..；按 F_OS 门控，official set* 语义）----
    FpMkLabel(p, w, F_MAXTOUCH, L"MaxTouch", 12, 1738, 70);
    FpMkEdit(p, w, F_MAXTOUCH, 88, 1736, 60);
    FpMkLabel(p, w, F_FLASH, L"Flash", 158, 1738, 40);
    FpMkCombo(p, w, F_FLASH, 202, 1736, 110);
    FpComboAdd(w->ctl[F_FLASH - F_BASE], L"off - 关闭");
    FpComboAdd(w->ctl[F_FLASH - F_BASE], L"block - 屏蔽(2)");
    FpComboAdd(w->ctl[F_FLASH - F_BASE], L"allow - 允许(1)");
    FpMkLabel(p, w, F_GYRO, L"陀螺仪", 322, 1738, 60);
    FpMkCombo(p, w, F_GYRO, 388, 1736, 110);
    FpComboAdd(w->ctl[F_GYRO - F_BASE], L"0 - 关闭");
    FpComboAdd(w->ctl[F_GYRO - F_BASE], L"1 - 开启");
    FpMkLabel(p, w, F_NETINFO, L"网络类型", 508, 1738, 70);
    FpMkCombo(p, w, F_NETINFO, 578, 1736, 150);
    FpComboAdd(w->ctl[F_NETINFO - F_BASE], L"0 - 关闭");
    FpComboAdd(w->ctl[F_NETINFO - F_BASE], L"1 - wifi");
    FpComboAdd(w->ctl[F_NETINFO - F_BASE], L"2 - cellular");
    // ---- 21. ClientHints -> static.UserAgentMetadata（y 1780..；值与“系统”同源）----
    FpMkLabel(p, w, F_CHPLAT, L"CH平台", 12, 1782, 70);
    FpMkEdit(p, w, F_CHPLAT, 88, 1780, 150);
    FpMkLabel(p, w, F_CHVER, L"CH版本", 246, 1782, 40);
    FpMkEdit(p, w, F_CHVER, 292, 1780, 130);
    FpMkLabel(p, w, F_CHARCH, L"CH架构", 430, 1782, 40);
    FpMkEdit(p, w, F_CHARCH, 476, 1780, 140);
    FpMkLabel(p, w, F_CHMODEL, L"CH机型", 624, 1782, 40);
    FpMkEdit(p, w, F_CHMODEL, 670, 1780, 150);
    // ---- 22. ClientHints 附属位（y 1824..）----
    FpMkLabel(p, w, F_CHMOBILE, L"CH移动", 12, 1826, 70);
    FpMkCombo(p, w, F_CHMOBILE, 88, 1824, 90);
    FpComboAdd(w->ctl[F_CHMOBILE - F_BASE], L"0 - 否");
    FpComboAdd(w->ctl[F_CHMOBILE - F_BASE], L"1 - 是");
    FpMkLabel(p, w, F_CHBITNESS, L"CH位数", 186, 1826, 55);
    FpMkCombo(p, w, F_CHBITNESS, 246, 1824, 90);
    FpComboAdd(w->ctl[F_CHBITNESS - F_BASE], L"0 - 默认");
    FpComboAdd(w->ctl[F_CHBITNESS - F_BASE], L"32");
    FpComboAdd(w->ctl[F_CHBITNESS - F_BASE], L"64");
    FpMkLabel(p, w, F_CHWOW64, L"wow64", 344, 1826, 60);
    FpMkCombo(p, w, F_CHWOW64, 410, 1824, 90);
    FpComboAdd(w->ctl[F_CHWOW64 - F_BASE], L"0 - 默认");
    FpComboAdd(w->ctl[F_CHWOW64 - F_BASE], L"1 - 是");
    FpComboAdd(w->ctl[F_CHWOW64 - F_BASE], L"2 - 否");
    {
        HINSTANCE hi2 = (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE);
        ::CreateWindowW(L"STATIC", L"ClientHints -> UserAgentMetadata", WS_CHILD | WS_VISIBLE | SS_LEFT,
            512, 1826, 300, 20, p, NULL, hi2, NULL);
        ::CreateWindowW(L"STATIC",
            L"CH平台/CH版本/CH架构/CH机型 与“系统”同源（切换系统自动按官方取值集重算：Windows/macOS/Linux/Android/iPhone）；"
            L"Flash 仅 win/mac；MaxTouch/陀螺仪/网络类型 仅 Android/iPhone（陀螺仪还需 chrome 内核）",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 1868, 800, 36, p, NULL, hi2, NULL);
    }
}
// fp_ui.cpp — part 5/6：回填 + 收集
static std::wstring FpFirstTok(const std::wstring& s) {
    size_t p = s.find(L" ");
    return (p == std::wstring::npos) ? s : s.substr(0, p);
}
// 系统扩展字段按指纹“系统”门控（official set* 只在对应平台生效）：
// Flash 仅 win/mac；MaxTouch/陀螺仪/网络类型 仅 Android/iPhone，陀螺仪还需 chrome 内核。
static void FpUpdateOsGates(FpWnd* w, const std::wstring& osValue) {
    auto C = [&](int id) { return w->ctl[id - F_BASE]; };
    const std::string os = N(osValue);
    const bool flash = FpOsSupportsFlash(os);
    const bool mobile = FpOsSupportsMobileExtras(os);
    ::EnableWindow(C(F_FLASH), flash);
    ::EnableWindow(C(F_MAXTOUCH), mobile);
    ::EnableWindow(C(F_GYRO), mobile && w->form.browser == L"sun");
    ::EnableWindow(C(F_NETINFO), mobile);
}
// ClientHints 四项（CH平台/CH版本/CH架构/CH机型）与“系统”表达的是同一份内容：
// 按官方 clientHints 取值集从 F_OS 派生（系统切换、存档与系统不符时重算），再由保存链写
// static.UserAgentMetadata（official setClientHints）。
static void FpApplyChFromOs(FpFormData& f) {
    const std::string os = N(f.os);
    f.chPlatform = W(FpOsToChPlatform(os));
    f.chPlatformVersion = W(FpOsToChPlatformVersion(os));
    f.chArchitecture = W(FpOsToChArchitecture(os));
    f.chModel = W(FpOsToChModel(os));
    f.chMobile = W(FpOsToChMobile(os));
    if (f.chBitness != L"32" && f.chBitness != L"64") f.chBitness = L"0";
    if (f.chWow64 != L"1" && f.chWow64 != L"2") f.chWow64 = L"0";
}
static void FpFill(FpWnd* w) {
    FpFormData& f = w->form;
    auto C = [&](int id) { return w->ctl[id - F_BASE]; };
    FpComboSel(C(F_BROWSER), (f.browser + L" - ").c_str());
    // combo 存 "值 - 说明"，按前缀匹配
    auto selByVal = [&](int id, const std::wstring& v) {
        HWND c = C(id);
        int n = (int)::SendMessageW(c, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < n; i++) {
            wchar_t b[512]{};
            ::SendMessageW(c, CB_GETLBTEXT, i, (LPARAM)b);
            if (FpFirstTok(b) == v) { ::SendMessageW(c, CB_SETCURSEL, i, 0); return; }
        }
        if (n > 0) ::SendMessageW(c, CB_SETCURSEL, 0, 0);
    };
    selByVal(F_BROWSER, f.browser.empty() ? L"sun" : f.browser);
    // 内核无独立下拉：由浏览器目录尾段推导（chrome_152→chrome143、chrome_121→chrome121、
    // flower_100→firefox128），f.kernelVer 保留载入值用于存档兼容，显示层不再设置。
    // A2 数据目录显示完整指纹目录（父目录 + 环境名），与 Config.json data_dir 同形；
    // 从未保存过则显示全局值作参考（带“（默认全局，可改）”后缀）。
    {
        std::wstring effD = EffDataDir(w->cfg, w->profile);
        std::wstring effB = EffBrowserDir(w->cfg, w->profile);
        auto it = w->cfg.profiles.find(w->profile);
        bool hasD = (it != w->cfg.profiles.end() && !it->second.dataDir.empty());
        bool hasB = (it != w->cfg.profiles.end() && !it->second.sunBrowserDir.empty());
        FpSet(C(F_PDATADIR), effD + L"\\" + w->profile + (hasD ? L"" : L"（默认全局，可改）"));
        FpSet(C(F_PBROWSERDIR), effB + (hasB ? L"" : L"（默认全局，可改）"));
        // 表单存有效值（Collect 时去后缀回写覆盖）
        f.profDataDir = effD;
        f.profBrowserDir = effB;
    }
    selByVal(F_OS, f.os.empty() ? L"win" : f.os);
    // UA 版本号回填：空则从 UA 文本反解析 Chrome/CriOS/Firefox 后 2-3 位数字（与 web-ui syncUaPresetFromUA 一致）
    if (f.uaPreset.empty() && !f.ua.empty()) {
        const wchar_t* p = f.ua.c_str();
        for (const wchar_t* q = p; *q; q++) {
            const wchar_t* tag = NULL;
            if (wcsncmp(q, L"Chrome/", 7) == 0) tag = q + 7;
            else if (wcsncmp(q, L"CriOS/", 6) == 0) tag = q + 6;
            else if (wcsncmp(q, L"Firefox/", 8) == 0) tag = q + 8;
            if (tag) {
                wchar_t dig[8]{};
                int n = 0;
                while (n < 3 && tag[n] >= L'0' && tag[n] <= L'9') { dig[n] = tag[n]; n++; }
                if (n >= 2) { f.uaPreset.assign(dig, n); break; }
            }
        }
    }
    FpSet(C(F_UAPRESET), f.uaPreset);
    FpSet(C(F_UA), f.ua);
    selByVal(F_PTYPE, f.proxyType.empty() ? L"socks5" : f.proxyType);
    FpSet(C(F_PHOST), f.proxyHost);
    FpSet(C(F_PPORT), f.proxyPort);
    FpSet(C(F_PUSER), f.proxyUser);
    FpSet(C(F_PPASS), f.proxyPass);
    FpSet(C(F_COOKIE), f.cookie);
    w->cookieLenLogged = (int)f.cookie.size();
    w->cookieLenOpened = (int)f.cookie.size(); // 保存时判断是否被清空
    LOG(L"指纹回填 cookie len=" + std::to_wstring(f.cookie.size()) + L" " + w->profile);
    FpSet(C(F_REMARK), f.remark);
    selByVal(F_WEBRTC, f.webrtc.empty() ? L"proxy" : f.webrtc);
    FpSet(C(F_WEBRTCIP), f.webrtcIp);
    ::EnableWindow(C(F_WEBRTCIP), f.webrtc.empty() || f.webrtc == L"proxy");
    selByVal(F_TZM, f.timezoneMode.empty() ? L"custom" : f.timezoneMode);
    // 时区下拉是精确匹配：先归一为空格→下划线，兼容旧存档里的空格格式
    //（如 "America/New York"），否则会回退到首项 Etc/GMT+12。
    f.timezone = W(FpNormalizeTimezone(N(f.timezone)));
    FpComboSel(C(F_TZ), f.timezone.c_str());
    selByVal(F_GEOM, f.geoMode.empty() ? L"allow" : f.geoMode);
    selByVal(F_GEOIP, f.geoIp.empty() ? L"custom" : f.geoIp);
    FpSet(C(F_LAT), f.lat); FpSet(C(F_LNG), f.lng); FpSet(C(F_ACC), f.accuracy);
    selByVal(F_LANGM, f.langMode.empty() ? L"custom" : f.langMode);
    FpSet(C(F_LANGLIST), f.langList);
    // 语言三键回填对齐（main.min.js setLangs/setUILanguage）：
    // uiLang 空 -> follow_lang（跟语言列表首项）；custom -> 页面语言框显示 pageLanguage。
    // pageLanguage 为空且 custom 时，从语言列表首项派生（与 getUILanguage 回退一致）。
    selByVal(F_UILANG, f.uiLang.empty() ? L"follow_lang" : f.uiLang);
    // 页面语言单tag规范（--lang 只接受单个 locale，如 en-US；存档曾误写 "en-US,en"）：
    // 显示与保存统一取首项，逗号/分号/空白后截断；custom 空则从语言列表首项派生。
    {
        std::wstring pl = f.pageLang;
        // 规范化：取首项
        {
            size_t e = pl.find_first_of(L",;\n \t");
            if (e != std::wstring::npos) pl = pl.substr(0, e);
            pl.erase(0, pl.find_first_not_of(L" \t\"'"));
            if (!pl.empty()) {
                size_t ee = pl.find_last_not_of(L" \t\"'");
                if (ee != std::wstring::npos) pl = pl.substr(0, ee + 1);
                else pl.clear();
            }
            f.pageLang = pl;
        }
        if (pl.empty() && f.uiLang != L"follow_lang") {
            size_t c = f.langList.find_first_of(L",;\n");
            pl = (c == std::wstring::npos) ? f.langList : f.langList.substr(0, c);
            // 去首尾空格引号
            pl.erase(0, pl.find_first_not_of(L" \t\"'"));
            if (!pl.empty()) {
                size_t ee = pl.find_last_not_of(L" \t\"'");
                if (ee != std::wstring::npos) pl = pl.substr(0, ee + 1);
                else pl.clear();
            }
            if (pl.empty()) pl = L"en-US";
            f.pageLang = pl;
        }
        FpSet(C(F_PAGELANG), f.pageLang);
    }
    selByVal(F_RESM, f.resMode.empty() ? L"preset" : f.resMode);
    FpComboSel(C(F_RES), f.resolution.empty() ? L"none" : f.resolution.c_str());
    FpSet(C(F_RESW), f.resW); FpSet(C(F_RESH), f.resH);
    selByVal(F_FONTM, f.fontMode.empty() ? L"custom" : f.fontMode);
    FpSet(C(F_FONTS), f.fonts);
    ::SendMessageW(C(F_SWCVS), BM_SETCHECK, f.swCanvas ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(C(F_SWWGL), BM_SETCHECK, f.swWebglImg ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(C(F_SWAUD), BM_SETCHECK, f.swAudio ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(C(F_SWRECT), BM_SETCHECK, f.swClientRects ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(C(F_SWSPEECH), BM_SETCHECK, f.swSpeech ? BST_CHECKED : BST_UNCHECKED, 0);
    selByVal(F_MEDIA, f.mediaDevices.empty() ? L"0" : f.mediaDevices);
    FpSet(C(F_MIN), f.mediaIn); FpSet(C(F_MVID), f.mediaVid); FpSet(C(F_MOUT), f.mediaOut);
    selByVal(F_WGLM, f.webglMeta.empty() ? L"custom" : f.webglMeta);
    FpComboSel(C(F_VENDOR), f.vendor.c_str());
    FpSet(C(F_RENDERER), f.renderer);
    selByVal(F_WGPU, f.webgpu.empty() ? L"follow_webgl" : f.webgpu);
    FpSet(C(F_GVENDOR), f.gpuVendor); FpSet(C(F_GARCH), f.gpuArch);
    selByVal(F_CPUM, f.cpuMode.empty() ? L"custom" : f.cpuMode);
    FpComboSel(C(F_CPU), f.cpu.empty() ? L"20" : f.cpu.c_str());
    selByVal(F_RAMM, f.ramMode.empty() ? L"custom" : f.ramMode);
    FpComboSel(C(F_RAM), f.ram.empty() ? L"8" : f.ram.c_str());
    selByVal(F_DEVM, f.devNameMode.empty() ? L"custom" : f.devNameMode);
    FpSet(C(F_DEVNAME), f.devName);
    selByVal(F_MACM, f.macMode.empty() ? L"custom" : f.macMode);
    FpSet(C(F_MAC), f.mac);
    selByVal(F_DNT, f.doNotTrack.empty() ? L"default" : f.doNotTrack);
    selByVal(F_PORTSCAN, f.portScan.empty() ? L"open" : f.portScan);
    FpSet(C(F_WPORTS), f.whitePorts);
    selByVal(F_HWACC, f.hardwareAccel.empty() ? L"default" : f.hardwareAccel);
    selByVal(F_TLSM, f.disableTls.empty() ? L"close" : f.disableTls);
    FpSet(C(F_TLS), f.tlsBlacklist);
    FpSet(C(F_ARGS), f.launchArgs);
    // ---- 20. 系统扩展（按 OS 派生默认，ui 存档优先）----
    FpSet(C(F_MAXTOUCH), f.maxTouchPoints.empty() ? L"0" : f.maxTouchPoints);
    selByVal(F_FLASH, f.flashMode.empty() ? L"off" : f.flashMode);
    selByVal(F_GYRO, f.gyroscope.empty() ? L"0" : f.gyroscope);
    selByVal(F_NETINFO, f.netInfoType.empty() ? L"0" : f.netInfoType);
    // ClientHints 跟随“系统”：存档为空或平台与系统不符时，按官方 clientHints 取值集重算
    if (!FpChMatchesOs(N(f.os), N(f.chPlatform))) FpApplyChFromOs(f);
    if (f.chBitness.empty()) f.chBitness = L"0";
    if (f.chWow64.empty()) f.chWow64 = L"0";
    FpSet(C(F_CHPLAT), f.chPlatform);
    FpSet(C(F_CHVER), f.chPlatformVersion);
    FpSet(C(F_CHARCH), f.chArchitecture);
    FpSet(C(F_CHMODEL), f.chModel);
    selByVal(F_CHMOBILE, f.chMobile.empty() ? L"0" : f.chMobile);
    selByVal(F_CHBITNESS, f.chBitness.empty() ? L"0" : f.chBitness);
    selByVal(F_CHWOW64, f.chWow64.empty() ? L"0" : f.chWow64);
    FpUpdateOsGates(w, f.os);
}
static std::wstring FpCleanDirectoryField(std::wstring path) {
    for (const wchar_t* tag : { L"（默认全局，可改）", L"（跟随全局）" }) {
        size_t p = path.find(tag);
        if (p != std::wstring::npos) path = path.substr(0, p);
    }
    size_t first = path.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    path = path.substr(first, path.find_last_not_of(L" \t\r\n") - first + 1);
    // 保留盘符根目录 F:\，其他路径去掉尾部分隔符。
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    return path;
}
static std::wstring FpDirectoryLeaf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}
static std::wstring FpDirectoryParent(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L"";
    if (p == 2 && path.size() >= 3 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, p);
}
static bool FpLooksLikeProfileDir(const std::wstring& path) {
    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    // UI 侧车也标识一个 profile 目录（允许用户导入只剩 ui_fingerprint.json 的备份）。
    if (::GetFileAttributesW((path + L"\\ui_fingerprint.json").c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;
    const std::wstring leaf = FpDirectoryLeaf(path);
    const std::string fbcc = FpFbccIdOf(leaf);
    for (const std::string& file : { FpStaticName(fbcc), FpDynamicName(fbcc), FpCookiesName(fbcc) })
        if (::GetFileAttributesW((path + L"\\" + W(file)).c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    return false;
}
static std::wstring FpDataParentFromProfileOrParent(const std::wstring& raw, const std::wstring& profile) {
    const std::wstring path = FpCleanDirectoryField(raw);
    if (path.empty()) return path;
    // 显示层存完整目录：末段=环境名（Windows 路径不区分大小写）→ 去掉一段，还原父目录。
    // 末段比较优先于三件套探测，保证新建环境（目录尚不存在）也能正确还原，不会越存越长。
    {
        std::wstring leaf = FpDirectoryLeaf(path);
        if (leaf.size() == profile.size()) {
            bool same = true;
            for (size_t i = 0; i < leaf.size(); i++)
                if (towlower(leaf[i]) != towlower(profile[i])) { same = false; break; }
            if (same) {
                const std::wstring parent = FpDirectoryParent(path);
                return parent.empty() ? path : parent;
            }
        }
    }
    // 兼容：目录里有三件套但末段不是本环境名（如粘了别的指纹目录）→ 同样还原一级。
    if (!FpLooksLikeProfileDir(path)) return path;
    const std::wstring parent = FpDirectoryParent(path);
    return parent.empty() ? path : parent;
}
static void FpCollect(FpWnd* w) {
    FpFormData& f = w->form;
    auto C = [&](int id) { return w->ctl[id - F_BASE]; };
    f.browser = FpFirstTok(FpComboGet(C(F_BROWSER)));
    // 内核由浏览器目录尾段推导（与目录同参数）：flower_100→firefox128、chrome_121→chrome121、
    // chrome_152→chrome143；browser 类型同步（flower_100→flower，其余 sun）。
    // browserDir 恒等于环境名（只存名，不存路径；路径走 A2）。
    {
        // 先收目录（推导需要它；数据目录框存完整指纹目录，此处还原成父目录）
        f.profDataDir = FpDataParentFromProfileOrParent(FpGet(C(F_PDATADIR)), w->profile);
        f.profBrowserDir = FpCleanDirectoryField(FpGet(C(F_PBROWSERDIR)));
        if (!f.profBrowserDir.empty()) {
            std::wstring leaf = FpDirectoryLeaf(f.profBrowserDir);
            for (auto& c : leaf) c = towlower(c);
            if (leaf == L"sunbrowser.exe") f.profBrowserDir = FpDirectoryParent(f.profBrowserDir);
        }
        std::wstring low = f.profBrowserDir;
        for (auto& c : low) c = towlower(c);
        auto tail = [&](const wchar_t* t) {
            return low.size() >= wcslen(t) &&
                low.compare(low.size() - wcslen(t), wcslen(t), t) == 0; };
        if (tail(L"flower_100")) { f.kernelVer = L"firefox128"; f.browser = L"flower"; }
        else if (tail(L"chrome_121")) { f.kernelVer = L"chrome121"; if (f.browser != L"flower") f.browser = L"sun"; }
        else { f.kernelVer = L"chrome143"; if (f.browser != L"flower") f.browser = L"sun"; }
    }
    f.browserDir = w->profile;
    f.os = FpFirstTok(FpComboGet(C(F_OS)));
    f.uaPreset = FpGet(C(F_UAPRESET));
    f.ua = FpGet(C(F_UA));
    f.proxyType = FpFirstTok(FpComboGet(C(F_PTYPE)));
    f.proxyHost = FpGet(C(F_PHOST));
    f.proxyPort = FpGet(C(F_PPORT));
    f.proxyUser = FpGet(C(F_PUSER));
    f.proxyPass = FpGet(C(F_PPASS));
    f.cookie = FpGet(C(F_COOKIE));
    f.remark = FpGet(C(F_REMARK));
    f.webrtc = FpFirstTok(FpComboGet(C(F_WEBRTC)));
    f.webrtcIp = FpGet(C(F_WEBRTCIP));
    f.timezoneMode = FpFirstTok(FpComboGet(C(F_TZM)));
    f.timezone = FpComboGet(C(F_TZ));
    f.geoMode = FpFirstTok(FpComboGet(C(F_GEOM)));
    f.geoIp = FpFirstTok(FpComboGet(C(F_GEOIP)));
    f.lat = FpGet(C(F_LAT)); f.lng = FpGet(C(F_LNG)); f.accuracy = FpGet(C(F_ACC));
    f.langMode = FpFirstTok(FpComboGet(C(F_LANGM)));
    f.langList = FpGet(C(F_LANGLIST));
    f.uiLang = FpFirstTok(FpComboGet(C(F_UILANG)));
    // 页面语言收集即规范为单tag（与显示一致；--lang/保存同源）
    {
        std::wstring pl = FpGet(C(F_PAGELANG));
        size_t e = pl.find_first_of(L",;\n \t\"'");
        if (e != std::wstring::npos) pl = pl.substr(0, e);
        pl.erase(0, pl.find_first_not_of(L" \t\"'"));
        if (!pl.empty()) {
            size_t ee = pl.find_last_not_of(L" \t\"'");
            if (ee != std::wstring::npos) pl = pl.substr(0, ee + 1);
            else pl.clear();
        }
        f.pageLang = pl;
    }
    f.resMode = FpFirstTok(FpComboGet(C(F_RESM)));
    f.resolution = FpComboGet(C(F_RES));
    f.resW = FpGet(C(F_RESW)); f.resH = FpGet(C(F_RESH));
    f.fontMode = FpFirstTok(FpComboGet(C(F_FONTM)));
    f.fonts = FpGet(C(F_FONTS));
    f.swCanvas = ::SendMessageW(C(F_SWCVS), BM_GETCHECK, 0, 0) == BST_CHECKED;
    f.swWebglImg = ::SendMessageW(C(F_SWWGL), BM_GETCHECK, 0, 0) == BST_CHECKED;
    f.swAudio = ::SendMessageW(C(F_SWAUD), BM_GETCHECK, 0, 0) == BST_CHECKED;
    f.swClientRects = ::SendMessageW(C(F_SWRECT), BM_GETCHECK, 0, 0) == BST_CHECKED;
    f.swSpeech = ::SendMessageW(C(F_SWSPEECH), BM_GETCHECK, 0, 0) == BST_CHECKED;
    f.mediaDevices = FpFirstTok(FpComboGet(C(F_MEDIA)));
    f.mediaIn = FpGet(C(F_MIN)); f.mediaVid = FpGet(C(F_MVID)); f.mediaOut = FpGet(C(F_MOUT));
    f.webglMeta = FpFirstTok(FpComboGet(C(F_WGLM)));
    f.vendor = FpComboGet(C(F_VENDOR));
    f.renderer = FpGet(C(F_RENDERER));
    f.webgpu = FpFirstTok(FpComboGet(C(F_WGPU)));
    f.gpuVendor = FpGet(C(F_GVENDOR)); f.gpuArch = FpGet(C(F_GARCH));
    f.cpuMode = FpFirstTok(FpComboGet(C(F_CPUM)));
    f.cpu = FpComboGet(C(F_CPU));
    f.ramMode = FpFirstTok(FpComboGet(C(F_RAMM)));
    f.ram = FpComboGet(C(F_RAM));
    f.devNameMode = FpFirstTok(FpComboGet(C(F_DEVM)));
    f.devName = FpGet(C(F_DEVNAME));
    f.macMode = FpFirstTok(FpComboGet(C(F_MACM)));
    f.mac = FpGet(C(F_MAC));
    f.doNotTrack = FpFirstTok(FpComboGet(C(F_DNT)));
    f.portScan = FpFirstTok(FpComboGet(C(F_PORTSCAN)));
    f.whitePorts = FpGet(C(F_WPORTS));
    f.hardwareAccel = FpFirstTok(FpComboGet(C(F_HWACC)));
    f.disableTls = FpFirstTok(FpComboGet(C(F_TLSM)));
    f.tlsBlacklist = FpGet(C(F_TLS));
    f.launchArgs = FpGet(C(F_ARGS));
    // ---- 20. 系统扩展（按 OS 归一：非适用平台一律落“关”，避免写进无效配置）----
    f.maxTouchPoints = W(FpNormalizeMaxTouchPoints(N(FpGet(C(F_MAXTOUCH)))));
    f.flashMode = FpFirstTok(FpComboGet(C(F_FLASH)));
    f.gyroscope = FpFirstTok(FpComboGet(C(F_GYRO)));
    f.netInfoType = FpFirstTok(FpComboGet(C(F_NETINFO)));
    f.chPlatform = FpGet(C(F_CHPLAT));
    f.chPlatformVersion = FpGet(C(F_CHVER));
    f.chArchitecture = FpGet(C(F_CHARCH));
    f.chModel = FpGet(C(F_CHMODEL));
    f.chMobile = FpFirstTok(FpComboGet(C(F_CHMOBILE)));
    f.chBitness = FpFirstTok(FpComboGet(C(F_CHBITNESS)));
    f.chWow64 = FpFirstTok(FpComboGet(C(F_CHWOW64)));
    {
        const std::string osN = N(f.os);
        if (!FpOsSupportsFlash(osN)) f.flashMode = L"off";
        if (!FpOsSupportsMobileExtras(osN)) {
            f.maxTouchPoints = L"0";
            f.gyroscope = L"0";
            f.netInfoType = L"0";
        } else if (f.browser != L"sun") {
            f.gyroscope = L"0"; // official setGyroscope 限定 chrome 内核
        }
        f.maxTouchPoints = W(FpNormalizeMaxTouchPoints(N(f.maxTouchPoints)));
        f.netInfoType = W(FpNormalizeNetworkInformationType(N(f.netInfoType)));
        if (f.chMobile != L"1") f.chMobile = L"0";
        if (f.chBitness != L"32" && f.chBitness != L"64") f.chBitness = L"0";
        if (f.chWow64 != L"1" && f.chWow64 != L"2") f.chWow64 = L"0";
    }
}
// ==== 保存：把表单翻译成 official staticConfig 键（main.min.js set* 语义） ====
// 只在“适用且有效”时写入；不适用时删除我们写过的键，但不碰官方其它缓存值。
// 说明：FpConfigRemoveKey 是通用的“顶层删键”工具（名字沿用 Config.json 侧的实现）。

// official m.createmediaDevices(fbccId)：按 fbcc 累加和生成确定性的假设备列表
static std::string FpCreateMediaDevicesJson(const std::string& fbcc) {
    struct P { const char* in; const char* out; };
    static const P kPairs[6] = {
        {"Microphone Array (2- Realtek High Definition Audio)", "Speaker/Headphone (2- Realtek High Definition Audio)"},
        {"Microphone Array (Realtek High Definition Audio)", "Speaker/Headphone (Realtek High Definition Audio)"},
        {"Microphone Array (Realtek(R) Audio)", "Speaker (Realtek(R) Audio)"},
        {"Microphone Array (Conexant SmartAudio HD)", "Speaker (Conexant SmartAudio HD)"},
        {"Microphone Array (2- Conexant SmartAudio HD)", "Speaker (2- Conexant SmartAudio HD)"},
        {"Microphone Array (Synaptics Audio)", "Speaker (Synaptics Audio)"},
    };
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    unsigned a = 0;
    for (unsigned char c : fbcc) { // official: r += (a += code).toString(16)，每步累加值的 hex
        a += c;
        char buf[16]; int bi = 0; unsigned v = a;
        do { buf[bi++] = kHex[v & 0xF]; v >>= 4; } while (v);
        while (bi > 0) hex += buf[--bi];
    }
    std::string head = hex.substr(0, 4);
    std::string tail = hex.size() >= 4 ? hex.substr(hex.size() - 4) : hex;
    while (head.size() < 4) { head += 'c'; tail += 'f'; } // official 补位（fbcc>=4 字符不触发）
    const P& u = kPairs[a % 6];
    const std::string cam = "Integrated Camera (" + head + ":" + tail + ")";
    return "[{\"kind\":\"audioinput\",\"label\":" + FpBrowserConfigJsonQuote(u.in) +
        "},{\"kind\":\"videoinput\",\"label\":" + FpBrowserConfigJsonQuote(cam) +
        "},{\"kind\":\"audiooutput\",\"label\":" + FpBrowserConfigJsonQuote(u.out) + "}]";
}

// staticConfig.command_line 单键增删（保留已有其它键，如 do-not-de-elevate）
static void FpSetCommandLineKey(std::string& sj, const char* key, const std::string& valueRaw) {
    std::string cl = FpJsonGet(sj, "command_line");
    if (cl.empty() || cl.front() != '{') cl = "{}";
    std::string out = valueRaw.empty() ? FpConfigRemoveKey(cl, key) : FpJsonSet(cl, key, valueRaw);
    if (out.empty()) return;
    std::string m = FpJsonSet(sj, "command_line", out);
    if (!m.empty()) sj = m;
}

static void FpApplyStaticSystemKeys(std::string& sj, const FpFormData& f,
    const std::wstring& profileName) {
    if (sj.empty() || sj.front() != '{') return;
    auto set = [](std::string& s, const char* key, const std::string& raw) {
        if (raw.empty()) return;
        std::string m = FpJsonSet(s, key, raw);
        if (!m.empty()) s = m;
    };
    auto del = [](std::string& s, const char* key) {
        std::string m = FpConfigRemoveKey(s, key);
        if (!m.empty() && m != s) s = m;
    };
    const std::string os = N(f.os);
    const bool mobile = FpOsSupportsMobileExtras(os);
    const std::string fbcc = FpFbccIdOf(profileName);

    // 1) 字体：official setFonts -> DisabledFonts；setFakeFonts -> Fakefonts
    if (f.fontMode == L"all") {
        const std::string dis = FpBuildDisabledFontsJson();
        if (!dis.empty()) set(sj, "DisabledFonts", dis);
    } else if (!N(f.fonts).empty()) {
        set(sj, "DisabledFonts", FpFontsToJsonArray(f.fonts));
    }
    if (FpOsToAsarPlatform(f.os) != L"Win32")
        set(sj, "Fakefonts", FpBuildFakefontsJson(FpOsToAsarPlatform(f.os)));

    // 2) 端口扫描：official setScanPort（close=禁扫描 open=白名单 default 不写）
    if (f.portScan == L"close") {
        set(sj, "AllowScanPorts", "\"0\"");
        set(sj, "WebRTCAllowScanPorts", "\"0\"");
    } else if (f.portScan == L"open") {
        const std::string list = "\"" + JEsc(f.whitePorts) + "\"";
        set(sj, "AllowScanPorts", list);
        set(sj, "WebRTCAllowScanPorts", list);
    }

    // 3) 地理模式：official setGEO -> GeolocationSetting（静态留一份供读取回填）
    if (f.geoMode == L"ask" || f.geoMode == L"allow" || f.geoMode == L"block")
        set(sj, "GeolocationSetting", "\"" + N(f.geoMode) + "\"");

    // 4) CPU/RAM：官方默认值不写（浏览器取真实值），自定义写数字
    if (f.cpuMode == L"real") del(sj, "HardwareConcurrency");
    else if (!N(f.cpu).empty() && N(f.cpu) != "default")
        set(sj, "HardwareConcurrency", N(f.cpu));
    if (f.ramMode == L"real") del(sj, "DeviceMemory");
    else if (!N(f.ram).empty() && N(f.ram) != "default")
        set(sj, "DeviceMemory", N(f.ram));

    // 5) MAC / 设备名（official setMacAddress / setDeviceName）
    if (f.macMode == L"custom" && !N(f.mac).empty())
        set(sj, "MacAddress", "\"" + JEsc(f.mac) + "\"");
    else if (f.macMode == L"off") del(sj, "MacAddress");
    if (f.devNameMode != L"off" && !N(f.devName).empty())
        set(sj, "DeviceName", "\"" + JEsc(f.devName) + "\"");
    else if (f.devNameMode == L"off") del(sj, "DeviceName");

    // 6) 媒体设备（official setMediaDevices：0=真实不写；1/2=按 fbcc 确定性生成）
    if (f.mediaDevices == L"0") del(sj, "MediaDevices");
    else set(sj, "MediaDevices", FpCreateMediaDevicesJson(fbcc));

    // 7) 移动端 MaxTouchPoints（official setMaxTouchPoints 只对 Android/iPhone 置 0）
    if (mobile) set(sj, "MaxTouchPoints", FpNormalizeMaxTouchPoints(N(f.maxTouchPoints)));
    else del(sj, "MaxTouchPoints");

    // 8) ClientHints -> static.UserAgentMetadata（official setClientHints）
    if (!N(f.chPlatform).empty()) {
        // 附属位：0=默认（官方不写该键），1=是，2=否
        const std::string bit = (f.chBitness == L"32" || f.chBitness == L"64")
            ? N(f.chBitness) : std::string();
        const std::string wow = (f.chWow64 == L"1" || f.chWow64 == L"2")
            ? N(f.chWow64) : std::string();
        set(sj, "UserAgentMetadata", FpBuildUserAgentMetadataJson(
            N(f.chPlatform), N(f.chPlatformVersion), N(f.chArchitecture),
            N(f.chModel), N(f.chMobile), bit, wow));
    } else del(sj, "UserAgentMetadata");

    // 9) 陀螺仪（official setGyroscope：Android/iPhone + chrome 内核）
    const bool gyroOn = mobile && f.browser == L"sun" && f.gyroscope == L"1";
    if (gyroOn) {
        set(sj, "gyroscope", FpBuildGyroscopeStaticJson());
        set(sj, "deviceorientationdata", FpBuildDeviceOrientationStaticJson(fbcc));
        set(sj, "DeviceMotion", FpBuildDeviceMotionStaticJson());
    } else {
        del(sj, "gyroscope");
        del(sj, "deviceorientationdata");
        del(sj, "DeviceMotion");
    }

    // 10) 网络类型（official setNetworkInformationType：Android -> NetworkInformation +
    //     enable-blink-features；iOS -> disable-blink-features；其它/关闭 -> 清掉）
    const std::string net = FpNormalizeNetworkInformationType(N(f.netInfoType));
    if (mobile && net != "0") {
        if (FpOsIsAndroid(os)) {
            set(sj, "NetworkInformation", FpBuildNetworkInformationStatic(net));
            FpSetCommandLineKey(sj, "enable-blink-features", FpBuildAndroidBlinkFeatureValue());
            FpSetCommandLineKey(sj, "disable-blink-features", "");
        } else {
            del(sj, "NetworkInformation");
            FpSetCommandLineKey(sj, "enable-blink-features", "");
            FpSetCommandLineKey(sj, "disable-blink-features", FpBuildIosBlinkFeatureValue());
        }
    } else {
        del(sj, "NetworkInformation");
        FpSetCommandLineKey(sj, "enable-blink-features", "");
        FpSetCommandLineKey(sj, "disable-blink-features", "");
    }
}
// fp_ui.cpp — part 6/6：模态窗口过程 + 保存 + 导入
static LRESULT CALLBACK FpWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    FpWnd* w = (FpWnd*)::GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        w = (FpWnd*)cs->lpCreateParams;
        ::SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)w);
        w->hDlg = h;
        HINSTANCE hi = cs->hInstance;
        ::InitCommonControls();
        // 单页滚动容器（窗口 860x640：滚动区 8,8,844x548；底部按钮行 y=564；状态条同行）
        w->hScroll = FpMkScroll(h, hi, 8, 8, 844, 548);
        HWND hPage = ::CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, kFpContentW, kFpContentH, w->hScroll, NULL, hi, NULL);
        FpBuildPages(w, hPage, hi);
        // hPage 子类化转发 WM_COMMAND/WM_NOTIFY 到顶层（浏览按钮没反应的根因修复）
        ::SetWindowSubclass(hPage, FpPageProc, 1, (DWORD_PTR)h);
        FpScrollInit(w);
        // 底部按钮（y=564，高 30，互不重叠：随机 8..108；保存 636..736；取消 744..844）
        FpMkBtn(h, w, F_RANDOM, L"一键随机", 8, 564, 100);
        FpMkBtn(h, w, F_OK, L"保存", 636, 564, 100);
        FpMkBtn(h, w, F_CANCEL, L"取消", 744, 564, 100);
        w->hStatus = ::CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 120, 568, 500, 22, h, (HMENU)(INT_PTR)F_STATUS, hi, NULL);
        // 初值：ui 侧车 -> static/dynamic 回填 -> 默认。
        // dd 解析：该指纹独立父目录优先（sunlauncher.json profiles 段），否则全局。
        // 注意：独立目录的 profile 可能住在别处，ui/三件套都从 dd 读。
        // A2 必填：FpFormDefaults 留空，FpFill 时用 Eff* 填有效值。
        std::wstring effParent0 = EffDataDir(w->cfg, w->profile);
        std::wstring dd = effParent0 + L"\\" + w->profile;
        std::string ui;
        bool hasUi = FpLoadUiExtra(dd, ui) && !ui.empty();
        if (hasUi) FpFormFromUiJson(ui, w->form);
        else FpFormDefaults(w->form, w->profile);
        // Cookie 链路取证：ui 侧车长度 -> 文件长度 -> FpFill 写框长度 -> 保存时长度（四级对齐，
        // 任一级为 0 就能定位是“没存上”还是“没读出来/被清空”）。
        LOG(L"指纹载入 ui侧车 cookie len=" + std::to_wstring(w->form.cookie.size()) +
            L" hasUi=" + std::to_wstring(hasUi ? 1 : 0) + L" " + w->profile);
        // A2 默认取全局生效值（必填，保存时以前缀判断落盘，见 FpFill/F_OK）
        if (w->form.profDataDir.empty()) w->form.profDataDir = effParent0;
        if (w->form.profBrowserDir.empty()) w->form.profBrowserDir = EffBrowserDir(w->cfg, w->profile);
        // static 回填（与 web-ui applyImportResult 同字段）。
        // 优先级：ui 存档优先（用户最后一次保存的值），static 仅补 ui 缺失的键。
        // 以缓存为准的保护键（ProxyChain/DeviceName/MacAddress 等）在 ui 缺失时才用 static 值，
        // 且 ui 已有值时不覆盖——否则“保存后重进看不到修改内容”。
        {
            std::string sj, dj, cj;
            FpLoadDynamicJson(dd, dj);
            // 取 ui 已有值快照（判空用，避免 static 空值覆盖 ui 实值）
            std::string uiLangs = FpJsonGet(ui, "language");
            // ui 存档 language 可能是数组 ["en-US","en"] 或字符串，任一非空即视为有值
            bool uiHasLangs = (!uiLangs.empty() && uiLangs != "\"\"" && uiLangs != "[]");
            // 语言三键快照：uiLang/pageLanguage（界面语言显示即它们决定）
            std::string uiUiLang = FpJsonGet(ui, "uiLang");
            std::string uiPageLang = FpJsonGet(ui, "pageLanguage");
            bool uiHasUiLang = (!uiUiLang.empty() && uiUiLang != "\"\"");
            bool uiHasPageLang = (!uiPageLang.empty() && uiPageLang != "\"\"");
            bool uiHasProxyHost = false, uiHasProxyPort = false;
            {
                std::string hh0 = FpJsonGet(ui, "proxyHost"), pp0 = FpJsonGet(ui, "proxyPort");
                uiHasProxyHost = (!hh0.empty() && hh0 != "\"\"");
                uiHasProxyPort = (!pp0.empty() && pp0 != "\"\"");
            }
            if (FpLoadStaticJson(dd, sj) && !sj.empty()) {
                std::string v;
                // 语言回填对齐（main.min.js setLangs：Langs=language.join(",")）：
                // ui 有 language 即用 ui；ui 无才用 static.Langs。AcceptLang 是派生
                // （getAccept 算法），不直接回填，只在保存时重算。
                if (!uiHasLangs) {
                    v = FpJsonGet(sj, "Langs");
                    if (!v.empty() && v.front() == '"') {
                        // Langs 可能是 "en-US,en" 字符串 -> langList
                        w->form.langList = WJ(v);
                        w->form.langMode = L"custom";
                    }
                }
                // 界面语言回填对齐（main.min.js setUILanguage：--lang 取 language 首项
                // 经 getUILanguage 映射；pageLanguageSwitch=1 跟随，0 用 pageLanguage）：
                // ui 有 uiLang/pageLanguage 即用 ui；ui 无才从语言列表首项派生。
                if (!uiHasUiLang) w->form.uiLang = L"follow_lang";
                if (!uiHasPageLang) {
                    if (w->form.uiLang == L"custom") {
                        std::wstring src = w->form.langList;
                        size_t c = src.find(L",");
                        std::wstring first = (c == std::wstring::npos) ? src : src.substr(0, c);
                        first.erase(0, first.find_first_not_of(L" \t"));
                        if (!first.empty()) first.erase(first.find_last_not_of(L" \t") + 1);
                        w->form.pageLang = first.empty() ? L"en-US" : first;
                    } else w->form.pageLang.clear();
                }
                // Platform -> os 反推（仅 ui 无 os 时；ui 存档 os 为准）
                {
                    std::string uiOs = FpJsonGet(ui, "os");
                    if (uiOs.empty() || uiOs == "\"\"") {
                        v = FpJsonGet(sj, "Platform");
                        if (!v.empty() && v.front() == '"') {
                            std::string pl = N(WJ(v));
                            for (auto& c : pl) c = tolower(c);
                            if (pl.find("mac") != std::string::npos || pl.find("darwin") != std::string::npos) w->form.os = L"mac";
                            else if (pl.find("linux") != std::string::npos) w->form.os = L"linux";
                            else if (pl.find("android") != std::string::npos) w->form.os = L"android";
                            else if (pl.find("iphone") != std::string::npos || pl.find("ios") != std::string::npos) w->form.os = L"ios";
                            else w->form.os = L"win";
                        }
                    }
                }
                // CPU/RAM/设备/MAC：仅 ui 对应键缺失时用 static 补
                {
                    std::string uiCpu = FpJsonGet(ui, "hardwareConcurrency");
                    if (uiCpu.empty() || uiCpu == "\"\"") {
                        v = FpJsonGet(sj, "HardwareConcurrency");
                        if (!v.empty()) {
                            w->form.cpu = W(v);
                            w->form.cpuMode = (v == "default" || v == "\"default\"") ? L"real" : L"custom";
                        }
                    }
                    std::string uiRam = FpJsonGet(ui, "deviceMemory");
                    if (uiRam.empty() || uiRam == "\"\"") {
                        v = FpJsonGet(sj, "DeviceMemory");
                        if (!v.empty()) {
                            w->form.ram = W(v);
                            w->form.ramMode = (v == "default" || v == "\"default\"") ? L"real" : L"custom";
                        }
                    }
                    std::string uiDev = FpJsonGet(ui, "devName");
                    if (uiDev.empty() || uiDev == "\"\"") {
                        v = FpJsonGet(sj, "DeviceName"); if (!v.empty()) { w->form.devName = WJ(v); }
                    }
                    std::string uiMac = FpJsonGet(ui, "mac");
                    if (uiMac.empty() || uiMac == "\"\"") {
                        v = FpJsonGet(sj, "MacAddress"); if (!v.empty()) { w->form.mac = WJ(v); }
                    }
                    // MediaDevices：仅 ui 缺失时补。字符串形态直接取；official 数组形态
                    // [{kind,label}...] 按 kind 数出三数量，模式按“真实设备列表”置 0（真实/关闭）。
                    std::string uiMd = FpJsonGet(ui, "mediaDevices");
                    if ((uiMd.empty() || uiMd == "\"\"") ) {
                        v = FpJsonGet(sj, "MediaDevices");
                        if (!v.empty() && v.front() == '"') w->form.mediaDevices = WJ(v);
                        else if (!v.empty() && v.front() == '[') {
                            w->form.mediaDevices = L"0";
                            int nin = 0, nvid = 0, nout = 0;
                            size_t q = 0;
                            while ((q = v.find("\"kind\"", q)) != std::string::npos) {
                                size_t c = v.find(':', q);
                                size_t q1 = (c == std::string::npos) ? std::string::npos : v.find('"', c);
                                size_t q2 = (q1 == std::string::npos) ? std::string::npos : v.find('"', q1 + 1);
                                if (q1 == std::string::npos || q2 == std::string::npos) break;
                                std::string kd = v.substr(q1 + 1, q2 - q1 - 1);
                                if (kd == "audioinput") nin++;
                                else if (kd == "videoinput") nvid++;
                                else if (kd == "audiooutput") nout++;
                                q = q2 + 1;
                            }
                            if (nin + nvid + nout > 0) {
                                w->form.mediaIn = W(std::to_string(nin));
                                w->form.mediaVid = W(std::to_string(nvid));
                                w->form.mediaOut = W(std::to_string(nout));
                            }
                        }
                    }
                    // TTSEngines -> speechSwitch：仅 ui 缺失时；非空数组即有可用语音 -> 开。
                    // 旧逻辑 v.find('1') 对 official 语音表恒假，误关语音。
                    std::string uiSp = FpJsonGet(ui, "speechSwitch");
                    if (uiSp.empty() || uiSp == "\"\"") {
                        v = FpJsonGet(sj, "TTSEngines");
                        if (!v.empty() && v.front() == '[' &&
                            v.find_first_not_of("[ \t\r\n]") != std::string::npos)
                            w->form.swSpeech = true;
                    }
                    // 时区/地理 static/dynamic 后备：ui 缺失时按 official TimeZone/automatic_timezone 回填。
                    std::string uiTz = FpJsonGet(ui, "timezone");
                    if (uiTz.empty() || uiTz == "\"\"") {
                        v = FpJsonGet(sj, "TimeZone");
                        if (v.empty() || v == "\"\"") v = FpJsonGet(dj, "TimeZone");
                        if (v.empty() || v == "\"\"") v = FpJsonGet(sj, "timezone");
                        if (v.empty() || v == "\"\"") v = FpJsonGet(dj, "timezone");
                        if (!v.empty() && v.front() == '"') {
                            // 保持官方下划线格式（与时区下拉表 kTz / 保存归一化一致）；
                            // 转空格会导致 FpComboSel 精确匹配失败并回退首项。
                            w->form.timezone = WJ(v);
                            w->form.timezoneMode = L"custom";
                        }
                    }
                    std::string uiTzm = FpJsonGet(ui, "timezoneMode");
                    if (uiTzm.empty() || uiTzm == "\"\"") {
                        std::string za = FpJsonGet(sj, "automatic_timezone");
                        if (za.empty()) za = FpJsonGet(sj, "tzAuto");
                        if (za.empty()) za = FpJsonGet(dj, "automatic_timezone");
                        if (za.empty()) za = FpJsonGet(dj, "tzAuto");
                        if (za == "1" || za == "\"1\"") w->form.timezoneMode = L"ip";
                        else if (za == "0" || za == "\"0\"") w->form.timezoneMode = L"custom";
                    }
                    // 地理模式 static 后备：GeolocationSetting ask/allow/block（ui 缺失时；
                    // official static 有该键但旧回填没读，导致无 ui 环境恒显示“允许”与实际相反）
                    {
                        std::string uiGm = FpJsonGet(ui, "geoMode");
                        if (uiGm.empty() || uiGm == "\"\"") {
                            v = FpJsonGet(sj, "GeolocationSetting");
                            if (!v.empty() && v.front() == '"') {
                                std::string gm = N(WJ(v));
                                for (auto& c : gm) c = tolower(c);
                                if (gm == "ask" || gm == "allow" || gm == "block")
                                    w->form.geoMode = W(gm);
                            }
                        }
                    }
                    // 白名单端口 static 后备：AllowScanPorts（ui 缺失且非空时）
                    {
                        std::string uiWp = FpJsonGet(ui, "whitePorts");
                        if (uiWp.empty() || uiWp == "\"\"") {
                            v = FpJsonGet(sj, "AllowScanPorts");
                            if (!v.empty() && v.front() == '"' && v != "\"\"")
                                w->form.whitePorts = WJ(v);
                        }
                    }
                    // AudioFp/ClientRectFp -> 开关：仅 ui 缺失时
                    std::string uiAu = FpJsonGet(ui, "audio");
                    if (uiAu.empty() || uiAu == "\"\"") {
                        v = FpJsonGet(sj, "AudioFp");
                        if (!v.empty()) w->form.swAudio = (v != "0" && v != "\"0\"");
                    }
                    std::string uiCr = FpJsonGet(ui, "clientRects");
                    if (uiCr.empty() || uiCr == "\"\"") {
                        v = FpJsonGet(sj, "ClientRectFp");
                        if (!v.empty()) w->form.swClientRects = (v != "0" && v != "\"0\"");
                    }
                    // 四个噪声开关真值：ui 存档 > static 低位键 > official 种子存在性
                    //（official 只在开关=1 时写 CanvasMark/WebGLMark/AudioFp/ClientRectFp）。
                    {
                        auto hasMark = [&](const char* k) {
                            std::string v = FpJsonGet(sj, k);
                            return !v.empty() && v != "\"\"" && v != "\"0\"" && v != "0";
                        };
                        w->form.swCanvas = FpResolveNoiseSwitch(
                            FpJsonGet(ui, "canvas"), FpJsonGet(sj, "canvas"), hasMark("CanvasMark")) == "1";
                        w->form.swWebglImg = FpResolveNoiseSwitch(
                            FpJsonGet(ui, "webglImage"), FpJsonGet(sj, "webgl_image"), hasMark("WebGLMark")) == "1";
                        w->form.swAudio = FpResolveNoiseSwitch(
                            FpJsonGet(ui, "audio"), FpJsonGet(sj, "AudioFp"), hasMark("AudioFp")) == "1";
                        w->form.swClientRects = FpResolveNoiseSwitch(
                            FpJsonGet(ui, "clientRects"), FpJsonGet(sj, "ClientRectFp"), hasMark("ClientRectFp")) == "1";
                    }
                    // WebRTC 回填：ui 模式优先；缺 IP 时按 WebRTCAddress -> WebRTCLocalAddress
                    // 顺序从 static/dynamic 补（official static 只有后者）。
                    std::string uiWr = FpJsonGet(ui, "webrtc");
                    std::string uiWrIp = FpJsonGet(ui, "webrtcIp");
                    std::string wrAddress = FpJsonGet(sj, "WebRTCAddress");
                    if (wrAddress.empty() || wrAddress == "\"\"") wrAddress = FpJsonGet(dj, "WebRTCAddress");
                    if ((wrAddress.empty() || wrAddress == "\"\"")) {
                        std::string wrLocal = FpJsonGet(sj, "WebRTCLocalAddress");
                        if (wrLocal.size() >= 2 && wrLocal.front() == '"' && wrLocal != "\"\"")
                            wrAddress = wrLocal;
                    }
                    if ((uiWrIp.empty() || uiWrIp == "\"\"") && wrAddress.size() >= 2 && wrAddress.front() == '"')
                        w->form.webrtcIp = WJ(wrAddress);
                    // Config.json 最高优先：exe 同目录按环境名对应的伪装 IP（有效才覆盖）
                    {
                        std::string cfgIp = FpConfigSpoofIpRaw(w->profile);
                        if (!cfgIp.empty() && FpIsValidWebRtcIp(W(cfgIp))) {
                            w->form.webrtcIp = W(cfgIp);
                            LOG(L"指纹载入 伪装IP=Config.json " + w->profile);
                        }
                    }
                    if (uiWr.empty() || uiWr == "\"\"") {
                        std::string dw = FpJsonGet(sj, "DisableWebRTC");
                        if (dw.empty()) dw = FpJsonGet(dj, "DisableWebRTC");
                        if (dw == "true" || dw == "\"true\"") w->form.webrtc = L"disabled";
                        else if (wrAddress.size() >= 2 && wrAddress.front() == '"' && wrAddress != "\"\"") w->form.webrtc = L"proxy";
                        else w->form.webrtc = L"forward";
                    }
                    FpBackfillWebGl(w->form, ui, sj, dd, w->profile);
                }
                // ProxyChain 回填（ui 优先：ui 存档是用户最后一次保存的值；static 只在
                // ui 缺代理字段时作为后备）。根因：旧逻辑 static 回填无条件覆盖 ui，
                // 用户改了 SOCKS5 点保存后，static 还没写新值时 static 为空/旧值，
                // 下次打开 static 旧值覆盖 ui 新值，表现为“修改了没保存、看不到修改后内容”。
                if (!uiHasProxyHost || !uiHasProxyPort) {
                    std::string pc = FpJsonGet(sj, "ProxyChain");
                    if (!pc.empty() && pc != "[]") {
                        std::string h2 = FpJsonGet(pc, "host"), p2 = FpJsonGet(pc, "port"),
                                      s2 = FpJsonGet(pc, "scheme");
                        if (!uiHasProxyHost && h2.size() >= 2 && h2.front() == '"') w->form.proxyHost = WJ(h2);
                        if (!uiHasProxyPort) {
                            if (p2.size() >= 2 && p2.front() == '"') w->form.proxyPort = WJ(p2);
                            else if (!p2.empty()) w->form.proxyPort = W(p2);
                        }
                        std::string uiPt = FpJsonGet(ui, "proxyType");
                        if ((uiPt.empty() || uiPt == "\"\"") && s2.size() >= 2 && s2.front() == '"') w->form.proxyType = WJ(s2);
                        // 代理账号/密码 official static 有 account/password 但旧回填没读；
                        // 不读则保存时被空值覆盖，带鉴权代理一次保存即丢。
                        std::string uiPu = FpJsonGet(ui, "proxyUser"), uiPw = FpJsonGet(ui, "proxyPass");
                        std::string a2 = FpJsonGet(pc, "account"), pw2 = FpJsonGet(pc, "password");
                        if ((uiPu.empty() || uiPu == "\"\"") && a2.size() >= 2 && a2.front() == '"' && a2 != "\"\"")
                            w->form.proxyUser = WJ(a2);
                        if ((uiPw.empty() || uiPw == "\"\"") && pw2.size() >= 2 && pw2.front() == '"' && pw2 != "\"\"")
                            w->form.proxyPass = WJ(pw2);
                        LOG(L"指纹载入 代理=static后备 " + w->profile);
                    }
                } else {
                    LOG(L"指纹载入 代理=ui存档 " + w->profile);
                }
            } else {
                // static 缺失/读失败：代理来源仍记一笔，判读不断链
                if (uiHasProxyHost || uiHasProxyPort)
                    LOG(L"指纹载入 代理=ui存档(static缺失) " + w->profile);
                else
                    LOG(L"指纹载入 代理=无(static缺失且ui无代理) " + w->profile);
            }
            if (!dj.empty()) {
                std::string g = FpJsonGet(dj, "Geoposition");
                if (g.size() >= 2 && g.front() == '"') {
                    std::string gs = N(WJ(g));
                    size_t c1 = gs.find(','), c2 = gs.find(',', c1 + 1);
                    if (c1 != std::string::npos) {
                        w->form.lat = W(gs.substr(0, c1));
                        w->form.lng = W(c1 + 1 < gs.size() ? gs.substr(c1 + 1, (c2 == std::string::npos ? c2 : c2 - c1 - 1)) : "");
                        if (c2 != std::string::npos) w->form.accuracy = W(gs.substr(c2 + 1));
                    }
                }
            }
            if (FpLoadCookiesJson(dd, cj) && !cj.empty()) {
                // 剥离 CLIENT_HOST，校正 BROWSER_ID（与 web-ui sanitizeCookies 一致）
                std::string fbcc = FpFbccIdOf(w->profile);
                std::string out = "[";
                size_t p = 0; bool first = true;
                while ((p = cj.find("\"name\"", p)) != std::string::npos) {
                    size_t vs = cj.find(':', p); if (vs == std::string::npos) break;
                    size_t q1 = cj.find('"', vs); if (q1 == std::string::npos) break;
                    size_t q2 = cj.find('"', q1 + 1); if (q2 == std::string::npos) break;
                    std::string nm = cj.substr(q1 + 1, q2 - q1 - 1);
                    // 取整对象 {} 片段
                    size_t os = cj.rfind('{', p), oe = cj.find('}', q2);
                    std::string obj = (os != std::string::npos && oe != std::string::npos) ? cj.substr(os, oe - os + 1) : "";
                    p = q2 + 1;
                    if (nm == "CLIENT_HOST") continue;
                    if (nm == "BROWSER_ID" && !obj.empty()) {
                        // value 替换为 fbcc
                        size_t vp = obj.find("\"value\"");
                        if (vp != std::string::npos) {
                            size_t v1 = obj.find('"', vp + 7); size_t v2 = obj.find('"', v1 + 1);
                            if (v1 != std::string::npos && v2 != std::string::npos)
                                obj = obj.substr(0, v1 + 1) + fbcc + obj.substr(v2);
                        }
                    }
                    if (!obj.empty()) { if (!first) out += ","; first = false; out += obj; }
                    if (first && obj.empty()) break;
                }
                out += "]";
                if (!first) w->form.cookie = W(out);
                LOG(L"指纹载入 文件cookie len=" + std::to_wstring(w->form.cookie.size()) + L" " + w->profile);
            }
        }
        FpFill(w);
        LOG(L"指纹打开回填 profile=" + w->profile + L" dd=" + dd);
        ::SetWindowTextW(w->hStatus, L"已载入，可编辑后保存");
        FpScrollTo(w, 0);
        return 0;
    }
    if (!w) return ::DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_VSCROLL: {
        // 单页滚动（行 32px / 页为可见高；与 kFpContentH 联动）
        int cmd = LOWORD(wp);
        RECT rc{};
        ::GetClientRect(w->hScroll, &rc);
        int visH = rc.bottom - rc.top;
        int y = w->scrollY, maxY = kFpContentH - visH;
        if (maxY < 0) maxY = 0;
        if (cmd == SB_LINEUP) y -= 32;
        else if (cmd == SB_LINEDOWN) y += 32;
        else if (cmd == SB_PAGEUP) y -= visH;
        else if (cmd == SB_PAGEDOWN) y += visH;
        else if (cmd == SB_THUMBTRACK || cmd == SB_THUMBPOSITION) {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            ::GetScrollInfo(w->hScroll, SB_VERT, &si);
            y = si.nTrackPos;
        }
        FpScrollTo(w, y);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        short dz = (short)HIWORD(wp);
        FpScrollTo(w, w->scrollY - (dz > 0 ? 48 : -48));
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        // 内容页/标签浅灰蓝底（对齐 --bg），输入框保持白底
        HDC dc = (HDC)wp;
        HWND ctl = (HWND)lp;
        wchar_t cls[32]{};
        ::GetClassNameW(ctl, cls, 32);
        if (::wcscmp(cls, L"Edit") == 0 || ::wcscmp(cls, L"ComboBox") == 0) {
            ::SetBkColor(dc, kUiPanel);
            ::SetTextColor(dc, kUiText);
            return (LRESULT)::GetStockObject(WHITE_BRUSH);
        }
        ::SetBkColor(dc, kUiBg);
        ::SetTextColor(dc, kUiText);
        static HBRUSH sBg = NULL;
        if (!sBg) sBg = ::CreateSolidBrush(kUiBg);
        return (LRESULT)sBg;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        auto C = [&](int c) { return w->ctl[c - F_BASE]; };
        // Cookie 框只记“空 <-> 有内容”两个边沿（逐键 EN_CHANGE 会刷屏）。
        // 用途：定位“打开时有内容、点保存时却是空的”是程序写入还是用户清空/粘贴失败。
        if (id == F_COOKIE && code == EN_CHANGE) {
            int n = (int)::GetWindowTextLengthW(C(F_COOKIE));
            if (n == 0 && w->cookieLenLogged > 0)
                LOG(L"Cookie框变空（原 " + std::to_wstring(w->cookieLenLogged) + L" 字符） " + w->profile);
            else if (n > 0 && w->cookieLenLogged == 0)
                LOG(L"Cookie框写入内容 len=" + std::to_wstring(n) + L" " + w->profile);
            w->cookieLenLogged = n;
            return 0;
        }
        if (id == F_CANCEL) { ::DestroyWindow(h); return 0; }
        // A2 浏览按钮：数据目录选父目录（SHBrowseForFolder）；浏览器目录选 SunBrowser.exe
        // 文件（GetOpenFileName），自动取其父目录填入。选后 FpCollect 同步表单。
        // 注意：这两个按钮在 hPage 容器内，经 FpPageProc 子类化转发到此（直接点没反应
        // 即转发缺失，见 FpPageProc/SetWindowSubclass）。COM 按需初始化/反初始化。
        if (id == F_BROWSEDATA) {
            bool needUninit = false;
            HRESULT hrCo = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (SUCCEEDED(hrCo)) needUninit = true;
            else if (hrCo != RPC_E_CHANGED_MODE) {
                LOG(L"指纹浏览 COM初始化失败 hr=" + std::to_wstring((unsigned)hrCo) + L" " + w->profile);
                ::SetWindowTextW(w->hStatus, L"浏览失败：COM 初始化失败");
                return 0;
            }
            wchar_t dir[MAX_PATH]{};
            BROWSEINFOW bi{};
            bi.hwndOwner = h;
            bi.pszDisplayName = dir;
            bi.lpszTitle = L"选择该指纹的数据父目录（将自动拼上环境名显示完整目录）";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = ::SHBrowseForFolderW(&bi);
            if (pidl) {
                if (::SHGetPathFromIDListW(pidl, dir) && dir[0]) {
                    // 浏览到环境目录本身 → 直接用；选到父目录 → 补环境名；显示层统一完整目录。
                    std::wstring picked = FpCleanDirectoryField(dir);
                    std::wstring leaf = FpDirectoryLeaf(picked);
                    bool isSelf = (leaf.size() == w->profile.size());
                    for (size_t i = 0; isSelf && i < leaf.size(); i++)
                        if (towlower(leaf[i]) != towlower(w->profile[i])) isSelf = false;
                    if (!isSelf && !FpLooksLikeProfileDir(picked))
                        picked = picked + L"\\" + w->profile;
                    FpSet(C(F_PDATADIR), picked);
                    FpCollect(w);
                    ::SetWindowTextW(w->hStatus, L"数据目录已选择（完整指纹目录，保存进独立目录）");
                    LOG(L"指纹浏览 数据目录=" + picked + L" " + w->profile);
                }
                ::CoTaskMemFree(pidl);
            } else {
                DWORD le = ::GetLastError();
                LOG(L"指纹浏览 数据目录取消/失败 le=" + std::to_wstring(le) + L" " + w->profile);
                ::SetWindowTextW(w->hStatus, L"未选择数据目录");
            }
            if (needUninit) ::CoUninitialize();
            return 0;
        }
        if (id == F_BROWSEBROWSER) {
            wchar_t file[MAX_PATH]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = h;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"SunBrowser 程序\0SunBrowser.exe\0所有文件\0*.*\0";
            ofn.lpstrTitle = L"选择浏览器内核程序 SunBrowser.exe（自动取其父目录）";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
            if (::GetOpenFileNameW(&ofn) && file[0]) {
                std::wstring f = file, low = file;
                for (auto& c : low) c = towlower(c);
                std::wstring dir = f;
                size_t p = dir.find_last_of(L"\\/");
                if (p != std::wstring::npos) dir = dir.substr(0, p);
                if (low.size() < 14 || low.compare(low.size() - 14, 14, L"sunbrowser.exe") != 0) {
                    ::MessageBoxW(h, L"请选择 SunBrowser.exe（不是 chrome.exe 或目录）。\r\n已按所选文件的父目录填入，请确认。",
                        L"浏览器目录", MB_OK | MB_ICONWARNING);
                }
                FpSet(C(F_PBROWSERDIR), dir);
                FpCollect(w);
                // 保留文件选择器给出的目录作为本次编辑值；它是 SunBrowser.exe 的直接父目录。
                w->form.profBrowserDir = FpCleanDirectoryField(dir);
                LOG(L"指纹浏览器目录已收集 form=" + w->form.profBrowserDir + L" " + w->profile);
                ::SetWindowTextW(w->hStatus, L"浏览器目录已选择（保存进独立目录）");
                LOG(L"指纹浏览 浏览器目录=" + dir + L" file=" + f + L" " + w->profile);
            } else {
                DWORD extErr = ::CommDlgExtendedError();
                LOG(L"指纹浏览 浏览器文件取消/失败 extErr=" + std::to_wstring(extErr) + L" " + w->profile);
                if (extErr != 0)
                    ::SetWindowTextW(w->hStatus, L"文件选择失败，换个目录重试");
                else
                    ::SetWindowTextW(w->hStatus, L"未选择浏览器文件");
            }
            return 0;
        }
        if (id == F_WEBRTC && (code == CBN_SELCHANGE || code == CBN_SELENDOK)) {
            FpCollect(w);
            ::EnableWindow(C(F_WEBRTCIP), w->form.webrtc == L"proxy");
            return 0;
        }
        // 浏览器类型联动浏览器目录：切 sun/flower 时按尾段目录名规则自动建议
        // （flower→flower_100、sun→chrome_152；chrome_121 的用户手工改目录即可），填入 A2 行。
        // 规则来源：getBrowserPath（win32）；内核版本号由目录尾段推导（Collect 处），不再单独下拉。
        // 系统切换 -> 重采集并刷新系统扩展字段的可用性（Flash/MaxTouch/陀螺仪/网络类型）
        if (id == F_OS && (code == CBN_SELCHANGE || code == CBN_SELENDOK)) {
            FpCollect(w);
            FpUpdateOsGates(w, w->form.os);
            // 系统与 ClientHints 四项是同一份内容：切换系统即按官方取值集重算并回显，
            // UA 也按系统预设重建（否则会出现 UA=Windows、CH=Android 的自相矛盾）。
            FpApplyChFromOs(w->form);
            FpSet(C(F_CHPLAT), w->form.chPlatform);
            FpSet(C(F_CHVER), w->form.chPlatformVersion);
            FpSet(C(F_CHARCH), w->form.chArchitecture);
            FpSet(C(F_CHMODEL), w->form.chModel);
            ::SendMessageW(C(F_CHMOBILE), CB_SETCURSEL, w->form.chMobile == L"1" ? 1 : 0, 0);
            FpSet(C(F_UA), FpBuildUA(w->form.os, w->form.uaPreset));
            ::SetWindowTextW(w->hStatus, L"系统已切换：UA / ClientHints / 系统扩展字段已按新系统对齐");
            LOG(L"指纹系统切换 os=" + w->form.os + L" ch=" + w->form.chPlatform +
                L" " + w->profile);
            return 0;
        }
        if (id == F_BROWSER && (code == CBN_SELCHANGE || code == CBN_SELENDOK)) {
            FpCollect(w);
            FpUpdateOsGates(w, w->form.os); // 陀螺仪需 chrome 内核，随浏览器类型联动
            std::wstring tail = (w->form.browser == L"flower") ? L"flower_100" : L"chrome_152";
            // 取当前 A2 行或全局 browserDir 的父目录前缀，只换尾段
            std::wstring cur = w->form.profBrowserDir.empty()
                ? EffBrowserDir(w->cfg, w->profile) : w->form.profBrowserDir;
            size_t p = cur.find_last_of(L"\\/");
            std::wstring sug = (p == std::wstring::npos) ? tail : (cur.substr(0, p + 1) + tail);
            FpSet(C(F_PBROWSERDIR), sug);
            w->form.profBrowserDir = sug;
            std::wstring m = L"浏览器联动：浏览器目录已建议为 " + tail + L"（可手工改，保存进独立目录）";
            ::SetWindowTextW(w->hStatus, m.c_str());
            LOG(L"指纹浏览器联动 browser=" + w->form.browser + L" sugTail=" + tail + L" " + w->profile);
            return 0;
        }
        if (id == F_SHUFFLEUA) {
            FpCollect(w);
            // UA 大版本号取纯数字，非法/为空回退 152（与 web-ui uaVer 一致）
            {
                std::wstring v = w->form.uaPreset, dig;
                for (auto c : v) { if (c >= L'0' && c <= L'9') dig += c; }
                if (dig.size() < 2 || dig.size() > 3) dig = L"152";
                w->form.uaPreset = dig;
                FpSet(C(F_UAPRESET), dig);
            }
            FpSet(C(F_UA), FpBuildUA(w->form.os, w->form.uaPreset));
            ::SetWindowTextW(w->hStatus, L"UA 已按当前系统重新生成");
            return 0;
        }
        if (id == F_SHUFFLEFONTS) {
            FpSet(C(F_FONTS), kFonts[rand() % 3]);
            ::SetWindowTextW(w->hStatus, L"字体已随机");
            return 0;
        }
        if (id == F_SHUFFLERDR) {
            FpSet(C(F_RENDERER), kRenderers[rand() % 5]);
            return 0;
        }
        if (id == F_SHUFFLEDEV) { FpSet(C(F_DEVNAME), FpRandomDev()); return 0; }
        if (id == F_SHUFFLEMAC) { FpSet(C(F_MAC), FpRandomMac()); return 0; }
        if (id == F_MERGECOOKIE) {
            // 与 web-ui parseCookieRaw/btnMergeFpCookie 一致：JSON 数组直通；
            // Netscape/制表符/Name=Value 统一转 JSON 数组
            FpCollect(w);
            std::string raw = N(w->form.cookie);
            size_t a = raw.find_first_not_of(" \t\r\n");
            size_t b = raw.find_last_not_of(" \t\r\n");
            std::string t = (a == std::string::npos) ? "" : raw.substr(a, b - a + 1);
            if (t.empty()) { ::SetWindowTextW(w->hStatus, L"请先粘贴 Cookie 内容"); return 0; }
            int n = 0;
            if (t.front() == '[') {
                // 已是 JSON 数组：计数后回写（规范化失败则原样保留）
                size_t p = 0;
                while ((p = t.find("\"name\"", p)) != std::string::npos) { n++; p += 6; }
                if (n == 0) n = 1; // 空数组也算 1 次合并
            } else {
                std::string arr = "[";
                bool first = true;
                size_t ls = 0;
                auto emitPair = [&](const std::string& nm, const std::string& vv) {
                    if (nm.empty()) return;
                    if (!first) arr += ",";
                    first = false;
                    std::string e1, e2;
                    for (char c : nm) { if (c == '"' || c == '\\') e1 += '\\'; e1 += c; }
                    for (char c : vv) { if (c == '"' || c == '\\') e2 += '\\'; e2 += c; }
                    arr += "{\"name\":\"" + e1 + "\",\"value\":\"" + e2 + "\"}";
                    n++;
                };
                while (ls <= t.size()) {
                    size_t le = t.find('\n', ls);
                    std::string line = t.substr(ls, le == std::string::npos ? le : le - ls);
                    // 去 \r
                    while (!line.empty() && (line.back() == '\r')) line.pop_back();
                    // 跳过空行与注释（保留 #HttpOnly 行）
                    std::string tr = line;
                    size_t ta = tr.find_first_not_of(" \t");
                    if (ta != std::string::npos) tr = tr.substr(ta);
                    if (!tr.empty() && tr[0] != '#' && tr.find("#HttpOnly") != 0) {
                        // Netscape 制表符：7 列以上取 [5]=name [6]=value
                        std::vector<std::string> cols;
                        size_t cs = 0;
                        while (cs <= line.size()) {
                            size_t ce = line.find('\t', cs);
                            cols.push_back(line.substr(cs, ce == std::string::npos ? ce : ce - cs));
                            if (ce == std::string::npos) break;
                            cs = ce + 1;
                        }
                        if (cols.size() >= 7) {
                            std::string vv = cols[6];
                            size_t va = vv.find_first_not_of(" \t");
                            size_t vb = vv.find_last_not_of(" \t");
                            emitPair(cols[5], (va == std::string::npos) ? "" : vv.substr(va, vb - va + 1));
                        } else if (line.find('=') != std::string::npos) {
                            size_t eq = line.find('=');
                            std::string nm = line.substr(0, eq), vv = line.substr(eq + 1);
                            size_t na = nm.find_first_not_of(" \t;"), nb = nm.find_last_not_of(" \t;");
                            size_t va = vv.find_first_not_of(" \t;"), vb = vv.find_last_not_of(" \t;");
                            emitPair((na == std::string::npos) ? "" : nm.substr(na, nb - na + 1),
                                     (va == std::string::npos) ? "" : vv.substr(va, vb - va + 1));
                        } else if (!line.empty() && line.find(';') != std::string::npos) {
                            // 分号分隔的 k=v 串：逐段拆
                            size_t ks = 0;
                            while (ks <= line.size()) {
                                size_t ke = line.find(';', ks);
                                std::string seg = line.substr(ks, ke == std::string::npos ? ke : ke - ks);
                                size_t eq = seg.find('=');
                                if (eq != std::string::npos) {
                                    std::string nm = seg.substr(0, eq), vv = seg.substr(eq + 1);
                                    size_t na = nm.find_first_not_of(" \t"), nb = nm.find_last_not_of(" \t");
                                    size_t va = vv.find_first_not_of(" \t"), vb = vv.find_last_not_of(" \t");
                                    if (na != std::string::npos)
                                        emitPair(nm.substr(na, nb - na + 1),
                                                 (va == std::string::npos) ? "" : vv.substr(va, vb - va + 1));
                                }
                                if (ke == std::string::npos) break;
                                ks = ke + 1;
                            }
                        }
                    } else if (tr.find("#HttpOnly") == 0) {
                        // #HttpOnly 前缀的 Netscape 行：去掉前缀后按制表符解析
                        std::string rest = tr.substr(10);
                        size_t sa = rest.find_first_not_of(" \t");
                        if (sa != std::string::npos) rest = rest.substr(sa);
                        std::vector<std::string> cols;
                        size_t cs = 0;
                        while (cs <= rest.size()) {
                            size_t ce = rest.find('\t', cs);
                            cols.push_back(rest.substr(cs, ce == std::string::npos ? ce : ce - cs));
                            if (ce == std::string::npos) break;
                            cs = ce + 1;
                        }
                        if (cols.size() >= 7) emitPair(cols[5], cols[6]);
                    }
                    if (le == std::string::npos) break;
                    ls = le + 1;
                }
                arr += "]";
                w->form.cookie = W(arr);
                FpSet(C(F_COOKIE), w->form.cookie);
            }
            wchar_t msg[128]{};
            swprintf_s(msg, L"Cookie 合并成功：共 %d 条", n);
            ::SetWindowTextW(w->hStatus, msg);
            return 0;
        }
        if (id == F_COOKIEIMPORT) {
            // 一键导入：从 Chromium 库解密出当前登录态，填进编辑框（明文 JSON，改完再保存）。
            // 只读：复制库到临时目录再解析，不碰原库；浏览器开着也能读到最近一次落盘。
            FpCollect(w);
            std::wstring ddI = EffDataDir(w->cfg, w->profile) + L"\\" + w->profile;
            std::string js, err;
            int skipped = 0;
            ::SetWindowTextW(w->hStatus, L"正在读取浏览器 Cookie…");
            if (FpCookiesImportFromBrowser(ddI, js, &skipped, err)) {
                const int n = (int)FpCookieArraySplit(js).size();
                w->form.cookie = W(js);
                FpSet(C(F_COOKIE), w->form.cookie);
                w->cookieLenLogged = (int)js.size();
                w->cookieLenOpened = (int)js.size();
                wchar_t msgI[256]{};
                swprintf_s(msgI, L"已从浏览器导入 %d 条%s（明文 JSON，改完点保存）",
                    n, skipped > 0 ? L"（另有无法解密的已跳过）" : L"");
                ::SetWindowTextW(w->hStatus, msgI);
                LOG(L"指纹导入浏览器Cookie ok 条数=" + std::to_wstring(n) +
                    L" 跳过=" + std::to_wstring(skipped) + L" " + w->profile);
            } else {
                ::MessageBoxW(h, W(err).c_str(), L"导入浏览器 Cookie 失败", MB_OK | MB_ICONWARNING);
                ::SetWindowTextW(w->hStatus, L"导入浏览器 Cookie 失败");
                LOG(L"指纹导入浏览器Cookie FAIL: " + W(err) + L" " + w->profile);
            }
            return 0;
        }
        if (id == F_IMPORT) {
            // 从目录导入：A2 可填数据父目录（cache -> cache\当前环境）或完整 profile 目录。
            // 日志根因为用户填完整的另一个环境目录时，旧逻辑仍机械追加“当前环境名”，
            // 例如 ...\k1hf...\1111_local，因此源目录不存在。导入读路径与保存目标分开：
            // 完整 profile 输入只作为源；FpCollect 仍把数据目录规范成其父目录。
            std::wstring sourceInput = FpCleanDirectoryField(FpGet(C(F_PDATADIR)));
            FpCollect(w);
            if (w->form.profDataDir.empty()) {
                ::MessageBoxW(h, L"请先在顶端 A2 行填写数据父目录，或粘贴完整的 profile 目录，再点导入。", L"从目录导入指纹", MB_OK | MB_ICONWARNING);
                ::SetWindowTextW(w->hStatus, L"导入已阻断：A2 数据目录为空");
                LOG(L"指纹导入 F_IMPORT BLOCKED(A2空) " + w->profile);
                return 0;
            }
            if (sourceInput.empty()) sourceInput = w->form.profDataDir;
            bool directSource = false;
            std::wstring srcDir = FpImportSourceProfileDir(sourceInput, w->profile, directSource);
            LOG(L"指纹导入 F_IMPORT input=" + sourceInput + L" direct=" +
                (directSource ? L"1" : L"0") + L" src=" + srcDir + L" targetParent=" +
                w->form.profDataDir + L" " + w->profile);
            if (::GetFileAttributesW(srcDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
                ::SetWindowTextW(w->hStatus, (L"导入源目录不存在: " + srcDir).c_str());
                LOG(L"指纹导入 F_IMPORT 无目录 src=" + srcDir + L" " + w->profile);
                return 0;
            }
            std::string sj2, dj2, cj2, ui2;
            int nRD = 0;
            if (FpLoadStaticJson(srcDir, sj2) && !sj2.empty()) nRD++;
            if (FpLoadDynamicJson(srcDir, dj2) && !dj2.empty()) nRD++;
            if (FpLoadCookiesJson(srcDir, cj2) && !cj2.empty()) nRD++;
            if (FpLoadUiExtra(srcDir, ui2) && !ui2.empty()) nRD++;
            if (nRD == 0) {
                ::SetWindowTextW(w->hStatus, (L"导入源无指纹文件: " + srcDir).c_str());
                LOG(L"指纹导入 F_IMPORT 无三件套/ui存档（没有 md5(fbcc+\"_static/_webrtc/_cookies\") 或 ui_fingerprint.json）src=" +
                    srcDir + L" " + w->profile);
                return 0;
            }
            // 复用打开时回填链：先 ui 侧车（若有），再 static/dynamic/cookies。
            // 为复用逻辑，把源目录三件套先解码进 form：走 FpFormFromUiJson + 手工字段。
            // 简单做法：临时把 w->cfg.dataDir 指向源父目录，调回填段——此处直接内联：
            {
                // ui 侧车
                if (!ui2.empty())
                    FpFormFromUiJson(ui2, w->form);
                // static 关键字段（与打开回填同表：语言三键 ui 有值不覆盖，无才用 Langs/首项派生）
                std::string v;
                std::string uiLangs = FpJsonGet(ui2, "language");
                if ((uiLangs.empty() || uiLangs == "\"\"" || uiLangs == "[]")) {
                    v = FpJsonGet(sj2, "Langs");
                    if (!v.empty() && v.front() == '"') { w->form.langList = WJ(v); w->form.langMode = L"custom"; }
                }
                {
                    std::string uiU = FpJsonGet(ui2, "uiLang"), uiP = FpJsonGet(ui2, "pageLanguage");
                    if (uiU.empty() || uiU == "\"\"") w->form.uiLang = L"follow_lang";
                    if ((uiP.empty() || uiP == "\"\"") && w->form.uiLang == L"custom") {
                        std::wstring src = w->form.langList;
                        size_t c = src.find(L",");
                        std::wstring first = (c == std::wstring::npos) ? src : src.substr(0, c);
                        first.erase(0, first.find_first_not_of(L" \t"));
                        if (!first.empty()) first.erase(first.find_last_not_of(L" \t") + 1);
                        w->form.pageLang = first.empty() ? L"en-US" : first;
                    } else if (w->form.uiLang == L"follow_lang") w->form.pageLang.clear();
                }
                // F_IMPORT 全覆盖（与打开回填同表）：os/CPU/RAM/设备/MAC/媒体/时区/地理/
                // 噪音/WebRTC，ui 缺失才用 static/dynamic 补；ui 有值不覆盖。
                {
                    std::string uiOs = FpJsonGet(ui2, "os");
                    if (uiOs.empty() || uiOs == "\"\"") {
                        v = FpJsonGet(sj2, "Platform");
                        if (!v.empty() && v.front() == '"') {
                            std::string pl = N(WJ(v));
                            for (auto& c : pl) c = tolower(c);
                            if (pl.find("mac") != std::string::npos || pl.find("darwin") != std::string::npos) w->form.os = L"mac";
                            else if (pl.find("linux") != std::string::npos) w->form.os = L"linux";
                            else if (pl.find("android") != std::string::npos) w->form.os = L"android";
                            else if (pl.find("iphone") != std::string::npos || pl.find("ios") != std::string::npos) w->form.os = L"ios";
                            else w->form.os = L"win";
                        }
                    }
                    std::string uiCpu = FpJsonGet(ui2, "hardwareConcurrency");
                    if (uiCpu.empty() || uiCpu == "\"\"") {
                        v = FpJsonGet(sj2, "HardwareConcurrency");
                        if (!v.empty()) { w->form.cpu = W(v); w->form.cpuMode = (v == "default" || v == "\"default\"") ? L"real" : L"custom"; }
                    }
                    std::string uiRam = FpJsonGet(ui2, "deviceMemory");
                    if (uiRam.empty() || uiRam == "\"\"") {
                        v = FpJsonGet(sj2, "DeviceMemory");
                        if (!v.empty()) { w->form.ram = W(v); w->form.ramMode = (v == "default" || v == "\"default\"") ? L"real" : L"custom"; }
                    }
                    std::string uiDev = FpJsonGet(ui2, "devName");
                    if (uiDev.empty() || uiDev == "\"\"") { v = FpJsonGet(sj2, "DeviceName"); if (!v.empty()) w->form.devName = WJ(v); }
                    std::string uiMac = FpJsonGet(ui2, "mac");
                    if (uiMac.empty() || uiMac == "\"\"") { v = FpJsonGet(sj2, "MacAddress"); if (!v.empty()) w->form.mac = WJ(v); }
                    std::string uiMd = FpJsonGet(ui2, "mediaDevices");
                    if (uiMd.empty() || uiMd == "\"\"") {
                        v = FpJsonGet(sj2, "MediaDevices");
                        if (!v.empty() && v.front() == '"') w->form.mediaDevices = WJ(v);
                        else if (!v.empty() && v.front() == '[') {
                            w->form.mediaDevices = L"0";
                            int nin = 0, nvid = 0, nout = 0;
                            size_t q = 0;
                            while ((q = v.find("\"kind\"", q)) != std::string::npos) {
                                size_t c = v.find(':', q);
                                size_t q1 = (c == std::string::npos) ? std::string::npos : v.find('"', c);
                                size_t q2 = (q1 == std::string::npos) ? std::string::npos : v.find('"', q1 + 1);
                                if (q1 == std::string::npos || q2 == std::string::npos) break;
                                std::string kd = v.substr(q1 + 1, q2 - q1 - 1);
                                if (kd == "audioinput") nin++;
                                else if (kd == "videoinput") nvid++;
                                else if (kd == "audiooutput") nout++;
                                q = q2 + 1;
                            }
                            if (nin + nvid + nout > 0) {
                                w->form.mediaIn = W(std::to_string(nin));
                                w->form.mediaVid = W(std::to_string(nvid));
                                w->form.mediaOut = W(std::to_string(nout));
                            }
                        }
                    }
                    std::string uiSp2 = FpJsonGet(ui2, "speechSwitch");
                    if (uiSp2.empty() || uiSp2 == "\"\"") {
                        v = FpJsonGet(sj2, "TTSEngines");
                        if (!v.empty() && v.front() == '[' &&
                            v.find_first_not_of("[ \t\r\n]") != std::string::npos)
                            w->form.swSpeech = true;
                    }
                    std::string uiTz = FpJsonGet(ui2, "timezone");
                    if (uiTz.empty() || uiTz == "\"\"") {
                        v = FpJsonGet(sj2, "TimeZone");
                        if (v.empty() || v == "\"\"") v = FpJsonGet(dj2, "TimeZone");
                        if (v.empty() || v == "\"\"") v = FpJsonGet(sj2, "timezone");
                        if (v.empty() || v == "\"\"") v = FpJsonGet(dj2, "timezone");
                        if (!v.empty() && v.front() == '"') {
                            // 同上：保持下划线格式，避免下拉精确匹配失败。
                            w->form.timezone = WJ(v); w->form.timezoneMode = L"custom";
                        }
                    }
                    std::string uiTzm2 = FpJsonGet(ui2, "timezoneMode");
                    if (uiTzm2.empty() || uiTzm2 == "\"\"") {
                        std::string za = FpJsonGet(sj2, "automatic_timezone");
                        if (za.empty()) za = FpJsonGet(sj2, "tzAuto");
                        if (za.empty()) za = FpJsonGet(dj2, "automatic_timezone");
                        if (za.empty()) za = FpJsonGet(dj2, "tzAuto");
                        if (za == "1" || za == "\"1\"") w->form.timezoneMode = L"ip";
                        else if (za == "0" || za == "\"0\"") w->form.timezoneMode = L"custom";
                    }
                    std::string uiGm2 = FpJsonGet(ui2, "geoMode");
                    if (uiGm2.empty() || uiGm2 == "\"\"") {
                        v = FpJsonGet(sj2, "GeolocationSetting");
                        if (!v.empty() && v.front() == '"') {
                            std::string gm = N(WJ(v));
                            for (auto& c : gm) c = tolower(c);
                            if (gm == "ask" || gm == "allow" || gm == "block")
                                w->form.geoMode = W(gm);
                        }
                    }
                    std::string uiWp2 = FpJsonGet(ui2, "whitePorts");
                    if (uiWp2.empty() || uiWp2 == "\"\"") {
                        v = FpJsonGet(sj2, "AllowScanPorts");
                        if (!v.empty() && v.front() == '"' && v != "\"\"")
                            w->form.whitePorts = WJ(v);
                    }
                    std::string uiG = FpJsonGet(ui2, "lat");
                    if ((uiG.empty() || uiG == "\"\"") && !dj2.empty()) {
                        std::string g = FpJsonGet(dj2, "Geoposition");
                        if (g.size() >= 2 && g.front() == '"') {
                            std::string gs = N(WJ(g));
                            size_t c1 = gs.find(','), c2 = gs.find(',', c1 + 1);
                            if (c1 != std::string::npos) {
                                w->form.lat = W(gs.substr(0, c1));
                                w->form.lng = W(c1 + 1 < gs.size() ? gs.substr(c1 + 1, (c2 == std::string::npos ? c2 : c2 - c1 - 1)) : "");
                                if (c2 != std::string::npos) w->form.accuracy = W(gs.substr(c2 + 1));
                            }
                        }
                    }
                    std::string uiAu = FpJsonGet(ui2, "audio");
                    if (uiAu.empty() || uiAu == "\"\"") { v = FpJsonGet(sj2, "AudioFp"); if (!v.empty()) w->form.swAudio = (v != "0" && v != "\"0\""); }
                    std::string uiCr = FpJsonGet(ui2, "clientRects");
                    if (uiCr.empty() || uiCr == "\"\"") { v = FpJsonGet(sj2, "ClientRectFp"); if (!v.empty()) w->form.swClientRects = (v != "0" && v != "\"0\""); }
                    // 四个噪声开关真值（与打开回填同表）：ui > static 低位键 > official 种子
                    {
                        auto hasMark = [&](const char* k) {
                            std::string v = FpJsonGet(sj2, k);
                            return !v.empty() && v != "\"\"" && v != "\"0\"" && v != "0";
                        };
                        w->form.swCanvas = FpResolveNoiseSwitch(
                            FpJsonGet(ui2, "canvas"), FpJsonGet(sj2, "canvas"), hasMark("CanvasMark")) == "1";
                        w->form.swWebglImg = FpResolveNoiseSwitch(
                            FpJsonGet(ui2, "webglImage"), FpJsonGet(sj2, "webgl_image"), hasMark("WebGLMark")) == "1";
                        w->form.swAudio = FpResolveNoiseSwitch(
                            FpJsonGet(ui2, "audio"), FpJsonGet(sj2, "AudioFp"), hasMark("AudioFp")) == "1";
                        w->form.swClientRects = FpResolveNoiseSwitch(
                            FpJsonGet(ui2, "clientRects"), FpJsonGet(sj2, "ClientRectFp"), hasMark("ClientRectFp")) == "1";
                    }
                    std::string uiWr = FpJsonGet(ui2, "webrtc");
                    std::string uiWrIp = FpJsonGet(ui2, "webrtcIp");
                    std::string wrAddress = FpJsonGet(sj2, "WebRTCAddress");
                    if (wrAddress.empty() || wrAddress == "\"\"") wrAddress = FpJsonGet(dj2, "WebRTCAddress");
                    if ((wrAddress.empty() || wrAddress == "\"\"")) {
                        std::string wrLocal = FpJsonGet(sj2, "WebRTCLocalAddress");
                        if (wrLocal.size() >= 2 && wrLocal.front() == '"' && wrLocal != "\"\"")
                            wrAddress = wrLocal;
                    }
                    if ((uiWrIp.empty() || uiWrIp == "\"\"") && wrAddress.size() >= 2 && wrAddress.front() == '"')
                        w->form.webrtcIp = WJ(wrAddress);
                    // Config.json 最高优先（与打开回填同表，键为当前环境名）
                    {
                        std::string cfgIp = FpConfigSpoofIpRaw(w->profile);
                        if (!cfgIp.empty() && FpIsValidWebRtcIp(W(cfgIp)))
                            w->form.webrtcIp = W(cfgIp);
                    }
                    if (uiWr.empty() || uiWr == "\"\"") {
                        std::string dw = FpJsonGet(sj2, "DisableWebRTC");
                        if (dw.empty()) dw = FpJsonGet(dj2, "DisableWebRTC");
                        if (dw == "true" || dw == "\"true\"") w->form.webrtc = L"disabled";
                        else if (wrAddress.size() >= 2 && wrAddress.front() == '"' && wrAddress != "\"\"") w->form.webrtc = L"proxy";
                        else w->form.webrtc = L"forward";
                    }
                    FpBackfillWebGl(w->form, ui2, sj2, srcDir, w->profile);
                }
                std::string pc = FpJsonGet(sj2, "ProxyChain");
                std::string uiH = FpJsonGet(ui2, "proxyHost"), uiP = FpJsonGet(ui2, "proxyPort");
                if (((uiH.empty() || uiH == "\"\"") || (uiP.empty() || uiP == "\"\"")) && !pc.empty() && pc != "[]") {
                    std::string h2 = FpJsonGet(pc, "host"), p2 = FpJsonGet(pc, "port"), s2 = FpJsonGet(pc, "scheme");
                    if ((uiH.empty() || uiH == "\"\"") && h2.size() >= 2 && h2.front() == '"') w->form.proxyHost = WJ(h2);
                    if ((uiP.empty() || uiP == "\"\"")) {
                        if (p2.size() >= 2 && p2.front() == '"') w->form.proxyPort = WJ(p2);
                        else if (!p2.empty()) w->form.proxyPort = W(p2);
                    }
                    std::string uiT = FpJsonGet(ui2, "proxyType");
                    if ((uiT.empty() || uiT == "\"\"") && s2.size() >= 2 && s2.front() == '"') w->form.proxyType = WJ(s2);
                    std::string uiU2 = FpJsonGet(ui2, "proxyUser"), uiW2 = FpJsonGet(ui2, "proxyPass");
                    std::string a2 = FpJsonGet(pc, "account"), pw2 = FpJsonGet(pc, "password");
                    if ((uiU2.empty() || uiU2 == "\"\"") && a2.size() >= 2 && a2.front() == '"' && a2 != "\"\"")
                        w->form.proxyUser = WJ(a2);
                    if ((uiW2.empty() || uiW2 == "\"\"") && pw2.size() >= 2 && pw2.front() == '"' && pw2 != "\"\"")
                        w->form.proxyPass = WJ(pw2);
                }
                // cookies 回填（剥离 CLIENT_HOST + BROWSER_ID 校正，与打开回填一致）
                if (!cj2.empty()) {
                    std::string fbcc = FpFbccIdOf(w->profile);
                    std::string out = "[";
                    size_t pp = 0; bool first = true;
                    while ((pp = cj2.find("\"name\"", pp)) != std::string::npos) {
                        size_t vs = cj2.find(':', pp); if (vs == std::string::npos) break;
                        size_t q1 = cj2.find('"', vs); if (q1 == std::string::npos) break;
                        size_t q2 = cj2.find('"', q1 + 1); if (q2 == std::string::npos) break;
                        std::string nm = cj2.substr(q1 + 1, q2 - q1 - 1);
                        size_t os = cj2.rfind('{', pp), oe = cj2.find('}', q2);
                        std::string obj = (os != std::string::npos && oe != std::string::npos) ? cj2.substr(os, oe - os + 1) : "";
                        pp = q2 + 1;
                        if (nm == "CLIENT_HOST") continue;
                        if (nm == "BROWSER_ID" && !obj.empty()) {
                            size_t vp = obj.find("\"value\"");
                            if (vp != std::string::npos) {
                                size_t v1 = obj.find('"', vp + 7), v2 = obj.find('"', v1 + 1);
                                if (v1 != std::string::npos && v2 != std::string::npos)
                                    obj = obj.substr(0, v1 + 1) + fbcc + obj.substr(v2);
                            }
                        }
                        if (!obj.empty()) { if (!first) out += ","; first = false; out += obj; }
                        if (first && obj.empty()) break;
                    }
                    out += "]";
                    if (!first) w->form.cookie = W(out);
                }
            }
            FpFill(w);
            wchar_t msg2[256]{};
            swprintf_s(msg2, L"已从目录导入 %d/3 件套: %s", nRD, srcDir.c_str());
            ::SetWindowTextW(w->hStatus, msg2);
            LOG(L"指纹导入 F_IMPORT ok n=" + std::to_wstring(nRD) + L" src=" + srcDir + L" " + w->profile);
            return 0;
        }
        if (id == F_PTEST) {
            FpCollect(w);
            if (w->form.proxyHost.empty() || w->form.proxyPort.empty()) {
                ::SetWindowTextW(w->hStatus, L"请先填写代理主机和端口");
                return 0;
            }
            // 本机 TCP connect 探测（3s 超时，旧 /api/proxy/check 同语义，已删除）
            // 注意：HTTP 监听线程已删除，Winsock 须在此 lazily 初始化（进程级一次）。
            {
                static bool wsaOk = false;
                if (!wsaOk) {
                    WSADATA wd{};
                    if (::WSAStartup(MAKEWORD(2, 2), &wd) == 0) wsaOk = true;
                    else {
                        ::SetWindowTextW(w->hStatus, L"网络初始化失败，无法检测");
                        return 0;
                    }
                }
            }
            std::string hh = N(w->form.proxyHost);
            int pport = _wtoi(w->form.proxyPort.c_str());
            bool okc = false;
            unsigned long long t0 = ::GetTickCount64();
            SOCKET ts = ::socket(AF_INET, SOCK_STREAM, 0);
            if (ts != INVALID_SOCKET && pport > 0 && pport < 65536) {
                u_long nb = 1;
                ::ioctlsocket(ts, FIONBIO, &nb);
                sockaddr_in ta{};
                ta.sin_family = AF_INET;
                ::inet_pton(AF_INET, hh.c_str(), &ta.sin_addr);
                ta.sin_port = htons((u_short)pport);
                ::connect(ts, (sockaddr*)&ta, sizeof(ta));
                fd_set wf; FD_ZERO(&wf); FD_SET(ts, &wf);
                timeval tv{}; tv.tv_sec = 3;
                if (::select(0, NULL, &wf, NULL, &tv) > 0 && FD_ISSET(ts, &wf)) {
                    int se = 0, sl = sizeof(se);
                    ::getsockopt(ts, SOL_SOCKET, SO_ERROR, (char*)&se, &sl);
                    okc = (se == 0);
                }
                ::closesocket(ts);
            }
            unsigned long long ms = ::GetTickCount64() - t0;
            std::wstring m = okc ? (L"连通正常 " + std::to_wstring(ms) + L"ms") : L"连接失败";
            FpSet(C(F_PSTATUS), m);
            ::SetWindowTextW(w->hStatus, m.c_str());
            return 0;
        }
        if (id == F_PSAVE) {
            ::SetWindowTextW(w->hStatus, L"代理已随指纹保存（进 ui 存档）");
            return 0;
        }
        if (id == F_RANDOM) {
            FpSet(C(F_FONTS), kFonts[rand() % 3]);
            FpSet(C(F_RENDERER), kRenderers[rand() % 5]);
            FpSet(C(F_DEVNAME), FpRandomDev());
            FpSet(C(F_MAC), FpRandomMac());
            // 与 web-ui btnFpRandom 一致：纬度 ±40、经度 ±180、精度 500~3500，并刷新 UA
            wchar_t la[32], ln[32], ac[32];
            swprintf_s(la, L"%.4f", (rand() % 8000 - 4000) / 100.0);
            swprintf_s(ln, L"%.4f", (rand() % 36000 - 18000) / 100.0);
            swprintf_s(ac, L"%d", rand() % 3000 + 500);
            FpSet(C(F_LAT), la); FpSet(C(F_LNG), ln); FpSet(C(F_ACC), ac);
            FpCollect(w);
            FpSet(C(F_UA), FpBuildUA(w->form.os, w->form.uaPreset));
            ::SetWindowTextW(w->hStatus, L"已随机全部指纹（点保存才写盘）");
            return 0;
        }
        if (id == F_OK) {
            FpCollect(w);
            // 保存前再次从控件取值，保证最近一次浏览/手工修改的目录不会被旧表单快照覆盖。
            const std::wstring browserDirControl = FpCleanDirectoryField(FpGet(C(F_PBROWSERDIR)));
            if (!browserDirControl.empty()) w->form.profBrowserDir = browserDirControl;
            LOG(L"指纹保存收集目录 dataParent=" + w->form.profDataDir +
                L" browserControl=" + browserDirControl + L" browserDir=" + w->form.profBrowserDir + L" " + w->profile);
            // A2 独立目录必填：两行都不能为空（无全局兜底），空则弹窗阻断保存。
            if (w->form.profDataDir.empty() || w->form.profBrowserDir.empty()) {
                ::MessageBoxW(h, L"数据目录 / 浏览器目录不能为空。\r\n请在顶端 A2 行填写该指纹的独立目录后再保存。", L"指纹配置", MB_OK | MB_ICONWARNING);
                ::SetWindowTextW(w->hStatus, L"保存已阻断：A2 独立目录必填");
                LOG(L"指纹保存 BLOCKED(A2目录空) " + w->profile);
                return 0;
            }
            const FpWebRtcResolution rtc = FpResolveWebRtc(w->form.webrtc, w->form.webrtcIp);
            if (rtc.proxyIpMissing) {
                ::MessageBoxW(h,
                    L"代理模式缺少有效 IP，当前会按禁用 WebRTC 保存。\r\n请输入有效 IPv4 或 IPv6 地址后再切换回 proxy 模式。",
                    L"WebRTC 伪装 IP 未设置", MB_OK | MB_ICONWARNING);
                LOG(L"WebRTC 代理模式缺少有效 IP，已按禁用保存 " + w->profile);
            }
            if (rtc.proxyIpIgnored) {
                ::MessageBoxW(h,
                    L"当前选择 forward：将按普通 WebRTC 工作，不会应用已填写的伪装 IP。\r\n要让 WebRTC 测试显示指定 IP，请切换到 proxy 模式后保存。",
                    L"WebRTC 伪装 IP 未启用", MB_OK | MB_ICONINFORMATION);
                LOG(L"WebRTC forward 模式忽略已填写的伪装 IP " + w->profile);
            }
            // 伪装 IP 双向同步：框内有效 IP 回写 exe 目录 Config.json（按环境名）；
            // 清空/无效则删键（防旧值下次覆盖复活）。启动链路不变（表单→static/dynamic→ext）。
            {
                std::string boxIp = N(FpTrimWebRtcIp(w->form.webrtcIp));
                std::string cfgIp = FpIsValidWebRtcIp(W(boxIp)) ? boxIp : "";
                bool okCfg = FpConfigSpoofIpSet(w->profile, cfgIp);
                LOG(L"指纹保存 伪装IP Config.json " + std::wstring(okCfg ? L"OK" : L"FAIL") +
                    (cfgIp.empty() ? L" cleared" : L" set") + L" " + w->profile);
            }
            // A2 独立目录落盘：只写 sunlauncher.json profiles 段（非官方设置，
            // 不进三件套/ext）。写盘失败记日志，不阻断指纹保存。
            {
                Config ccfg = w->cfg;
                ProfileOverride o;
                o.dataDir = w->form.profDataDir;
                o.sunBrowserDir = w->form.profBrowserDir;
                auto it0 = ccfg.profiles.find(w->profile);
                ProfileOverride old = (it0 == ccfg.profiles.end()) ? ProfileOverride() : it0->second;
                bool changed = (old.dataDir != o.dataDir || old.sunBrowserDir != o.sunBrowserDir);
                if (changed) {
                    ccfg.profiles[w->profile] = o;
                    bool okp = false;
                    try { okp = SaveConfig(ccfg); } catch (...) { okp = false; }
                    if (okp) {
                        w->cfg = ccfg;
                        LOG(L"指纹保存 profiles目录 OK " + w->profile +
                            L" dataDir=" + o.dataDir +
                            L" browserDir=" + o.sunBrowserDir);
                    } else {
                        LOG(L"指纹保存 profiles目录 FAIL（sunlauncher.json 写盘失败） " + w->profile);
                    }
                }
            }
            // dataDir 解析：A2 独立父目录（必填，已校验非空）。
            std::wstring effParent = w->form.profDataDir;
            std::wstring dd = effParent + L"\\" + w->profile;
            ::CreateDirectoryW(effParent.c_str(), NULL);
            ::CreateDirectoryW(dd.c_str(), NULL);
            // Config.json 按指纹同步两个目录（data_dir 存完整指纹目录；只回写已有对象条目）
            const bool okConfigDirs = FpConfigSyncProfileDirs(w->profile, dd, w->form.profBrowserDir);
            if (!okConfigDirs) {
                LOG(L"指纹保存 Config.json 目录同步失败 " + w->profile);
                ::MessageBoxW(h, L"Config.json 中该环境的目录保存失败；原 sunlauncher.json 仍保留。请检查启动器目录是否可写。",
                    L"目录保存失败", MB_OK | MB_ICONWARNING);
            }
            // Cookie 保存守卫：编辑框是明文 JSON，保存时自动规范化——非数组先按
            // Name=Value / Netscape / Cookie: 头转换，转换不了才提示（不再回滚用户输入）。
            // 结果为空数组需二次确认（确认后连浏览器库一起清）；没动过就一个字节都不碰磁盘。
            bool cookieClearConfirmed = false;
            bool cookieChanged = false; // 相对三件套是否真的变了（决定要不要写盘/写库）
            {
                auto trim = [](std::wstring s) {
                    size_t a = s.find_first_not_of(L" \t\r\n");
                    if (a == std::wstring::npos) return std::wstring();
                    s = s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
                    return s;
                };
                std::string ck = N(trim(w->form.cookie));
                std::string old;
                const bool hasOld = FpLoadCookiesJson(dd, old) && !old.empty();
                const int opened = w->cookieLenOpened;
                if (ck.empty() && opened == 0) {
                    // 打开就是空且没动过：文件里有值就原样带回（不写、不弹窗）
                    if (hasOld) { w->form.cookie = W(old); FpSet(C(F_COOKIE), w->form.cookie); }
                } else {
                    std::string norm;
                    if (ck.empty()) {
                        norm = "[]"; // 打开时有内容、现在空 = 主动清空
                    } else {
                        norm = FpNormalizeCookiesJson(ck);
                        if (norm.empty()) norm = FpCookiesTextToJson(ck); // 明文 -> JSON
                    }
                    if (norm.empty()) {
                        // 文本转换不出任何 cookie：保留磁盘原值并明确告知
                        if (hasOld) { w->form.cookie = W(old); FpSet(C(F_COOKIE), w->form.cookie); }
                        ::MessageBoxW(h,
                            L"没有解析出任何 Cookie，本次未改动该字段。\r\n"
                            L"可直接粘贴 Name=Value、Cookie: a=b; c=d 或 Netscape 七列文本再保存。",
                            L"Cookie 未保存", MB_OK | MB_ICONWARNING);
                        LOG(L"指纹保存 Cookie解析为0 len=" + std::to_wstring(ck.size()) + L" " + w->profile);
                    } else if (norm == "[]") {
                        // 空数组：可能是用户清空，也可能是 [{}] 这类无效输入
                        const int r = ::MessageBoxW(h,
                            L"Cookie 将被清空（0 条有效）。确定要清掉该环境的 Cookie 吗？\r\n"
                            L"是=写 [] 并删除浏览器库里的 cookie；否=保留原值。",
                            L"清空 Cookie", MB_YESNO | MB_ICONQUESTION);
                        if (r == IDYES) {
                            cookieClearConfirmed = true;
                            cookieChanged = true;
                            w->form.cookie = L"[]";
                        } else if (hasOld) {
                            w->form.cookie = W(old);
                            FpSet(C(F_COOKIE), w->form.cookie);
                        } else {
                            w->form.cookie.clear();
                        }
                        LOG(L"指纹保存 Cookie清空 " + (r == IDYES ? std::wstring(L"YES") : std::wstring(L"NO")) +
                            L" " + w->profile);
                    } else {
                        // 正常：规范化后落盘并回显（明文可改的 JSON）
                        w->form.cookie = W(norm);
                        FpSet(C(F_COOKIE), w->form.cookie);
                        w->cookieLenOpened = (int)norm.size();
                        cookieChanged = (norm != old);
                        LOG(L"指纹保存 Cookie规范化 条数=" +
                            std::to_wstring((int)FpCookieArraySplit(norm).size()) +
                            L" len=" + std::to_wstring(norm.size()) +
                            L" changed=" + std::to_wstring(cookieChanged ? 1 : 0) + L" " + w->profile);
                    }
                }
            }
            // 1. ui 侧车全量存档（字段名与 web-ui collectFp 一致，含派生字段，双向可读）
            {
                std::string ui = FpFormToUiJson(w->form);
                // 派生字段（与 web-ui collectFp 同名，供 applyUiExtra/applyFpConfig 回读）：
                // languageSwitch / pageLanguageSwitch / screenResolution / hardwareConcurrency /
                // deviceMemory / deviceNameSwitch / scanPortType / allowScanPorts / do_not_track /
                // mediaDevicesNum / webgl+webglConfig / webgpuSwitch / macAddressConfig / fonts[] / gpu+gpuSwitch / tlsSwitch
                std::string lang = N(w->form.langList);
                size_t p = 0; int cnt = 0;
                while (p <= lang.size()) { size_t e = lang.find(',', p); cnt++; if (e == std::string::npos) break; p = e + 1; }
                ui = FpJsonSet(ui, "languageSwitch", (cnt <= 1) ? "\"1\"" : "\"0\"");
                // AcceptLang 派生与 fpConfig 同算法（getAccept），存档可直接验算
                {
                    std::string _lang = N(w->form.langList);
                    std::vector<std::string> _e;
                    { size_t _p = 0; while (_p <= _lang.size()) {
                        size_t _q = _lang.find(',', _p);
                        std::string _t = _lang.substr(_p, _q == std::string::npos ? _q : _q - _p);
                        size_t _a = _t.find_first_not_of(" \t\r\n"), _b = _t.find_last_not_of(" \t\r\n");
                        if (_a != std::string::npos) _e.push_back(_t.substr(_a, _b - _a + 1));
                        if (_q == std::string::npos) break; _p = _q + 1; } }
                    std::string _acc; int _n = 0; std::vector<std::string> _r;
                    auto _base = [](const std::string& s) -> std::string {
                        size_t _d = s.find('-'); return (_d == std::string::npos) ? s : s.substr(0, _d); };
                    for (size_t _a = 0; _a < _e.size(); _a++) {
                        std::string _i = _e[_a], _o = (_a + 1 < _e.size()) ? _e[_a + 1] : "";
                        std::string _s = _base(_i), _l = _base(_o);
                        auto _has = [&](const std::string& _x) {
                            for (auto& _y : _r) if (_y == _x) return true; return false; };
                        if (_a == 0) { _acc += _i; _n++; }
                        else if (!_has(_i)) {
                            int _q = 10 - _n; if (_q < 1) _q = 1;
                            _acc += "," + _i + ";q=0." + std::to_string(_q); _n++;
                        }
                        if (!(_i == _s || _s == _l || _has(_s))) {
                            int _q = 10 - _n; if (_q < 1) _q = 1;
                            _acc += "," + _s + ";q=0." + std::to_string(_q); _n++; _r.push_back(_s);
                        }
                        _r.push_back(_i);
                    }
                    ui = FpJsonSet(ui, "AcceptLang", "\"" + _acc + "\"");
                }
                ui = FpJsonSet(ui, "pageLanguageSwitch", (w->form.uiLang == L"custom") ? "\"0\"" : "\"1\"");
                std::string res = N(w->form.resolution);
                if (w->form.resMode == L"custom" && !w->form.resW.empty() && !w->form.resH.empty())
                    res = N(w->form.resW) + "_" + N(w->form.resH);
                ui = FpJsonSet(ui, "screenResolution", "\"" + res + "\"");
                std::string cpu = (w->form.cpuMode == L"real") ? "default" : N(w->form.cpu);
                std::string ram = (w->form.ramMode == L"real") ? "default" : N(w->form.ram);
                ui = FpJsonSet(ui, "hardwareConcurrency", "\"" + cpu + "\"");
                ui = FpJsonSet(ui, "deviceMemory", "\"" + ram + "\"");
                std::string dsw = "0";
                if (w->form.devNameMode == L"random") dsw = "1";
                else if (w->form.devNameMode == L"custom") dsw = "2";
                ui = FpJsonSet(ui, "deviceNameSwitch", "\"" + dsw + "\"");
                std::string spt = "";
                if (w->form.portScan == L"open") spt = "1"; else if (w->form.portScan == L"close") spt = "0";
                ui = FpJsonSet(ui, "scanPortType", "\"" + spt + "\"");
                ui = FpJsonSet(ui, "allowScanPorts", "\"" + N(w->form.whitePorts) + "\"");
                std::string dnt = "";
                if (w->form.doNotTrack == L"open") dnt = "true"; else if (w->form.doNotTrack == L"close") dnt = "false";
                ui = FpJsonSet(ui, "do_not_track", "\"" + dnt + "\"");
                ui = FpJsonSet(ui, "mediaDevicesNum",
                    "{\"audioinput_num\":" + std::to_string([](const std::wstring& s) {
                        int n = 0; try { n = std::stoi(N(s)); } catch (...) { n = 1; }
                        if (n <= 0) n = 1; if (n >= 9) n = 8; return n; }(w->form.mediaIn)) +
                    ",\"videoinput_num\":" + std::to_string([](const std::wstring& s) {
                        int n = 0; try { n = std::stoi(N(s)); } catch (...) { n = 1; }
                        if (n <= 0) n = 1; if (n >= 9) n = 8; return n; }(w->form.mediaVid)) +
                    ",\"audiooutput_num\":" + std::to_string([](const std::wstring& s) {
                        int n = 0; try { n = std::stoi(N(s)); } catch (...) { n = 1; }
                        if (n <= 0) n = 1; if (n >= 9) n = 8; return n; }(w->form.mediaOut)) + "}");
                ui = FpJsonSet(ui, "webgl", (w->form.webglMeta == L"custom") ? "\"2\"" : "\"0\"");
                {
                    std::string wsw = (w->form.webgpu == L"disabled") ? "0" : "1";
                    std::string wc = "{\"unmasked_vendor\":\"" + N(w->form.vendor) +
                        "\",\"unmasked_renderer\":\"" + N(w->form.renderer) +
                        "\",\"webgpu\":{\"webgpu_switch\":\"" + wsw + "\"";
                    if (w->form.webgpu == L"custom")
                        wc += ",\"gpu_adapterinfo_vendor\":\"" + N(w->form.gpuVendor) +
                              "\",\"gpu_adapterinfo_architecture\":\"" + N(w->form.gpuArch) + "\"";
                    wc += "}}";
                    if (w->form.webglMeta == L"custom") ui = FpJsonSet(ui, "webglConfig", wc);
                    ui = FpJsonSet(ui, "webgpuSwitch", "\"" + wsw + "\"");
                }
                if (w->form.macMode == L"custom")
                    ui = FpJsonSet(ui, "macAddressConfig",
                        "{\"model\":\"2\",\"address\":\"" + N(w->form.mac) + "\"}");
                else
                    ui = FpJsonSet(ui, "macAddressConfig", "{\"model\":\"0\",\"address\":\"\"}");
                if (w->form.fontMode == L"all") ui = FpJsonSet(ui, "fonts", "[\"all\"]");
                // asar 1:1：fonts=all 时 static.DisabledFonts=getFonts-mobileFonts；
                // custom 时 static.DisabledFonts=表单切分数组（setFonts 语义：disabledFonts 直写）。
                // 注意旧 main.cpp /api/fp/save 的 protectFill（已删除）：缓存为准，已有值时不覆盖空值。
                // 此处写入的是“首次建档”值；已有缓存时以缓存为准，与 asar 行为一致。
                {
                    std::string asarPlatform = N(FpOsToAsarPlatform(w->form.os));
                    std::string dis = FpBuildDisabledFontsJson();
                    ui = FpJsonSet(ui, "staticDisabledFontsPreview", dis);
                    std::string fake = FpBuildFakefontsJson(W(asarPlatform));
                    ui = FpJsonSet(ui, "staticFakefontsPreview", fake);
                }
                if (w->form.hardwareAccel == L"close") { ui = FpJsonSet(ui, "gpu", "\"2\""); }
                else if (w->form.hardwareAccel == L"open") {
                    ui = FpJsonSet(ui, "gpu", "\"0\"");
                    ui = FpJsonSet(ui, "gpuSwitch", "\"1\"");
                }
                ui = FpJsonSet(ui, "tlsSwitch", (w->form.disableTls == L"open") ? "\"1\"" : "\"0\"");
                if (w->form.disableTls == L"open") ui = FpJsonSet(ui, "tls", "\"" + N(w->form.tlsBlacklist) + "\"");
                bool oku = FpSaveUiExtra(dd, ui);
                // 保存结果进 debug.log（fp_ui.h 已含 SunLauncher.h，LOG/W 可直接用）：
                // 写盘失败（权限/路径）不再静默吞掉；cookie 长度同步记一笔。
                {
                    std::string ck = N(w->form.cookie);
                    LOG(L"指纹保存 ui " + std::wstring(oku ? L"OK" : L"FAIL") +
                        L" uiLen=" + std::to_wstring(ui.size()) +
                        L" cookieLen=" + std::to_wstring(ck.size()) + L" " + w->profile);
                    ::SetWindowTextW(w->hStatus, (oku ? L"已保存（ui 存档 + cookies）" : L"保存失败：ui 存档写盘失败，看目录权限"));
                }
                (void)oku;
            }
            // 2. cookies 清洗后写三件套（CLIENT_HOST 剥离、BROWSER_ID 校正逻辑已在载入时处理，此处直接写）
            // 2. cookies 写盘（明文 JSON）+ 直接写 Chromium cookie 库（保存即生效）
            //    写库要求该 profile 的浏览器已关闭；在跑时只写三件套并弹窗告知，不碰库文件。
            bool okc = true;
            std::wstring dbNote;
            {
                const std::string ck = N(w->form.cookie);
                const bool wantDb = cookieClearConfirmed ||
                    (cookieChanged && !ck.empty() && ck.front() == '[' && ck != "[]");
                if (cookieClearConfirmed || cookieChanged) {
                    okc = FpSaveCookiesJson(dd, ck.empty() ? std::string("[]") : ck);
                    LOG(L"指纹保存 cookies " + std::wstring(okc ? L"OK" : L"FAIL") +
                        L" len=" + std::to_wstring(ck.size()) + L" " + w->profile);
                } else {
                    LOG(L"指纹保存 cookies SKIP（内容未变，不重复写盘） " + w->profile);
                }
                if (wantDb) {
                    int nw = 0, nsk = 0, nrm = 0;
                    std::string err;
                    const std::string payload = cookieClearConfirmed ? std::string("[]") : ck;
                    if (FpCookiesWriteToBrowser(dd, payload, &nw, &nsk, &nrm, err)) {
                        dbNote = L"；浏览器库写入 " + std::to_wstring(nw) + L" 条";
                        if (nsk) dbNote += L"（跳过 " + std::to_wstring(nsk) + L" 条缺 domain）";
                        if (nrm) dbNote += L"，清空 " + std::to_wstring(nrm) + L" 行";
                        LOG(L"指纹写 cookie 库 OK 写=" + std::to_wstring(nw) +
                            L" 跳=" + std::to_wstring(nsk) + L" 删=" + std::to_wstring(nrm) +
                            L" " + w->profile);
                    } else {
                        dbNote = L"；浏览器库未写入：" + W(err);
                        LOG(L"指纹写 cookie 库 FAIL: " + W(err) + L" " + w->profile);
                        ::MessageBoxW(h,
                            (L"Cookie 已保存到指纹档案，但没有写入浏览器库：\r\n" + W(err)).c_str(),
                            L"浏览器库未写入", MB_OK | MB_ICONWARNING);
                    }
                }
            }
                // 3. static 写回：FpFormToFpConfig 按表单组装（含 ProxyChain 数组），
                // 合并策略（防官方 static 被 fpConfig 小 schema 覆盖丢失键）：
                // 以缓存 curS 为基（非空时），把表单 cfg 的键逐个覆盖上去；保护键仍以缓存为准，
                // 例外 Langs/AcceptLang/language/pageLanguage* /ProxyChain 取表单新值（语言/代理必须生效）。
                // 缓存为空（新环境）则直接用 cfg。
                {
                    std::string cfg = FpFormToFpConfig(w->form);
                    std::string curS;
                    FpLoadStaticJson(dd, curS);
                    // 枚举 cfg 顶层键（简易扫描 "key" :，深度1，够用）
                    auto topKeys = [](const std::string& j) {
                        std::vector<std::string> ks;
                        size_t p = 0;
                        if (j.empty() || j[0] != '{') return ks;
                        p = 1;
                        int depth = 0; bool inS = false;
                        while (p < j.size()) {
                            char c = j[p];
                            if (inS) {
                                if (c == '\\') { p += 2; continue; }
                                if (c == '"') inS = false;
                                p++; continue;
                            }
                            if (c == '"') {
                                // 仅深度0的 key
                                if (depth == 0) {
                                    size_t q = j.find('"', p + 1);
                                    if (q == std::string::npos) break;
                                    // 确认后面是 :
                                    size_t r = q + 1;
                                    while (r < j.size() && (j[r] == ' ' || j[r] == '\t')) r++;
                                    if (r < j.size() && j[r] == ':') {
                                        ks.push_back(j.substr(p + 1, q - p - 1));
                                        p = r + 1; continue;
                                    }
                                }
                                inS = true; p++; continue;
                            }
                            if (c == '{' || c == '[') depth++;
                            else if (c == '}' || c == ']') { if (depth > 0) depth--; }
                            p++;
                        }
                        return ks;
                    };
                    std::string mergedBase;
                    if (!curS.empty()) {
                        mergedBase = curS;
                        std::vector<std::string> keys = topKeys(cfg);
                        static const char* prot2[] = { "DeviceName","MacAddress",
                            "MediaDevices","TTSEngines","HardwareConcurrency",
                            "DeviceMemory","Platform","UserId","CanvasMark","WebGLMark","AudioFp",
                            "ClientRectFp", NULL };
                        auto inProt = [&](const std::string& k) {
                            for (int i = 0; prot2[i]; i++) if (k == prot2[i]) return true;
                            return false; };
                        for (auto& k : keys) {
                            // 例外：语言三键 + 代理 + 页面语言系列表单优先
                            bool forceForm = (k == "Langs" || k == "AcceptLang" || k == "language" ||
                                k == "language_switch" || k == "pageLanguage" || k == "pageLanguageSwitch" ||
                                k == "ProxyChain");
                            if (inProt(k) && !forceForm) continue; // 缓存为准
                            std::string v = FpJsonGet(cfg, k);
                            if (v.empty()) continue;
                            std::string nm = FpJsonSet(mergedBase, k, v);
                            if (!nm.empty()) mergedBase = nm;
                        }
                        // ProxyChain 空保护：缓存非空而表单空时保留缓存
                        std::string curPc = FpJsonGet(curS, "ProxyChain");
                        std::string newPc = FpJsonGet(mergedBase, "ProxyChain");
                        if (!curPc.empty() && curPc != "[]" && (newPc.empty() || newPc == "[]")) {
                            std::string mm = FpJsonSet(mergedBase, "ProxyChain", curPc);
                            if (!mm.empty()) mergedBase = mm;
                        }
                        cfg = mergedBase;
                    } else {
                    // 保護鍵回填（与旧 protectFill 同表，已删除），例外见上：
                    static const char* prot[] = { "DeviceName","MacAddress",
                        "MediaDevices","TTSEngines","HardwareConcurrency",
                        "DeviceMemory","Platform","UserId","CanvasMark","WebGLMark","AudioFp",
                        "ClientRectFp", NULL };
                    for (int i = 0; prot[i]; i++) {
                        std::string cv = FpJsonGet(curS, prot[i]);
                        if (!cv.empty()) {
                            std::string m2 = FpJsonSet(cfg, prot[i], cv);
                            if (!m2.empty()) cfg = m2;
                        }
                    }
                    std::string curPc = FpJsonGet(curS, "ProxyChain");
                    std::string newPc = FpJsonGet(cfg, "ProxyChain");
                    if (!curPc.empty() && curPc != "[]" && newPc == "[]") {
                        std::string m2 = FpJsonSet(cfg, "ProxyChain", curPc);
                        if (!m2.empty()) cfg = m2;
                    }
                    }
                    // 系统扩展字段翻译成 official staticConfig 键（字体/端口扫描/地理/
                    // CPU/RAM/MAC/设备名/媒体/MaxTouch/ClientHints/陀螺仪/网络类型）
                    FpApplyStaticSystemKeys(cfg, w->form, w->profile);
                    bool oks = FpSaveStaticJson(dd, cfg);
                LOG(L"指纹保存 static " + std::wstring(oks ? L"OK" : L"FAIL") +
                    L" len=" + std::to_wstring(cfg.size()) + L" " + w->profile);
            }
            // WebRTC 有效值必须同步写 static + dynamic；否则旧 dynamic.DisableWebRTC=true
            // 会覆盖界面选择，让 forward/proxy 表现为“保存了但浏览器仍禁用”。
            {
                std::string dynamic;
                FpLoadDynamicJson(dd, dynamic);
                if (dynamic.empty() || dynamic.front() != '{') dynamic = "{}";
                dynamic = FpJsonSet(dynamic, "DisableWebRTC", rtc.disableWebRtc ? "true" : "false");
                dynamic = FpJsonSet(dynamic, "WebRTCAddress", "\"" + JEsc(rtc.address) + "\"");
                const std::string timezone = (w->form.timezoneMode == L"ip")
                    ? "" : FpNormalizeTimezone(N(w->form.timezone));
                dynamic = FpJsonSet(dynamic, "TimeZone", FpBrowserConfigJsonQuote(timezone));
                bool okd = FpSaveDynamicJson(dd, dynamic);
                std::wstring detail = L" mode=" + w->form.webrtc +
                    L" disabled=" + (rtc.disableWebRtc ? L"1" : L"0") +
                    L" ip=" + (rtc.address.empty() ? L"(empty)" : rtc.address) +
                    L" timezone=" + (timezone.empty() ? L"(empty)" : W(timezone));
                LOG(L"指纹保存 WebRTC dynamic " + std::wstring(okd ? L"OK" : L"FAIL") + detail + L" " + w->profile);
            }
            (void)okc;
            w->saved = true;
            ::SetWindowTextW(w->hStatus, L"已保存（ui 存档 + cookies）");
            ::DestroyWindow(h);
            return 0;
        }
        return 0;
    }
    case WM_DESTROY:
        // 注意：指纹窗口是模态子窗口（FpUiShowModal 自有消息循环，靠 IsWindow 破环退出），
        // 此处绝不能 PostQuitMessage——否则 WM_QUIT 会漏进主线程消息队列，主窗口跟随退出。
        // “取消按钮变退出程序”的根因即此。保存/取消分支只 DestroyWindow 即可。
        ::RemoveWindowSubclass(::GetDlgItem(h, F_TAB), FpPageProc, 1);
        return 0;
    }
    return ::DefWindowProcW(h, msg, wp, lp);
}

bool FpUiShowModal(HWND hParent, const Config& cfg, const std::wstring& profileName) {
    srand((unsigned)time(NULL));
    FpWnd w;
    w.cfg = cfg;
    w.profile = profileName;
    const wchar_t* cls = L"FpConfigCls";
    WNDCLASSW wc{};
    wc.lpfnWndProc = FpWndProc;
    wc.hInstance = (HINSTANCE)::GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.lpszClassName = cls;
    wc.hbrBackground = ::CreateSolidBrush(kUiBg); // 浅灰蓝底（对齐 --bg）
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    ::RegisterClassW(&wc);
    std::wstring title = L"指纹配置 - " + profileName;
    HWND h = ::CreateWindowW(cls, title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, kFpWinW, kFpWinH,
        hParent, NULL, wc.hInstance, &w);
    if (!h) return false;
    ::ShowWindow(h, SW_SHOW);
    ::UpdateWindow(h);
    // 模态循环：只处理本窗口消息，父窗口禁用；退出条件只看 IsWindow（不再看 WM_QUIT，
    // 与上 WM_DESTROY 不发 PostQuitMessage 对应，避免 QUIT 漏进主循环导致主窗口退出）。
    ::EnableWindow(hParent, FALSE);
    MSG m{};
    while (::IsWindow(h) && ::GetMessageW(&m, NULL, 0, 0)) {
        ::TranslateMessage(&m);
        ::DispatchMessageW(&m);
    }
    ::EnableWindow(hParent, TRUE);
    ::SetForegroundWindow(hParent);
    ::UnregisterClassW(cls, wc.hInstance);
    return w.saved;
}

