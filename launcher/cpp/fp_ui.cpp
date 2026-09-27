// fp_ui.cpp — 指纹配置原生窗口实现（part 1/4：JSON 互转 + 随机库）
#include "fp_ui.h"
#include "fingerprint.h"
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
    o += ",\"webrtc\":\"" + JEsc(f.webrtc) + "\",\"timezoneMode\":\"" + JEsc(f.timezoneMode) + "\"";
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
    std::string v;
    v = FpJsonGet(json, "canvas"); if (!v.empty()) f.swCanvas = (v == "\"1\"" || v == "1");
    v = FpJsonGet(json, "webglImage"); if (!v.empty()) f.swWebglImg = (v == "\"1\"" || v == "1");
    v = FpJsonGet(json, "audio"); if (!v.empty()) f.swAudio = (v == "\"1\"" || v == "1");
    v = FpJsonGet(json, "clientRects"); if (!v.empty()) f.swClientRects = (v == "\"1\"" || v == "1");
    v = FpJsonGet(json, "speechSwitch"); if (!v.empty()) f.swSpeech = (v == "\"1\"" || v == "1");
    return true;
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
    std::string sp = N(f.webrtc), tz = (f.timezoneMode == L"ip") ? "1" : "0";
    std::string o = "{\"webrtc\":\"" + sp + "\",\"automatic_timezone\":\"" + tz + "\"";
    o += ",\"tzAuto\":\"" + tz + "\"";
    if (tz == "0") {
        std::string tzn = N(f.timezone);
        for (auto& c : tzn) if (c == ' ') c = '_';
        o += ",\"timezone\":\"" + tzn + "\"";
    } else o += ",\"timezone\":\"\"";
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
    else {
        std::string fs8 = N(f.fonts), fs, arr = "[";
        // UTF-8 中文逗号 U+FF0C = EF BC 8C，先替换为 ASCII 逗号再切分
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
            // 去首尾空白（含中文逗号已在 UI 侧按逗号切，这里只 trim ASCII 空白）
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
        o += ",\"fonts\":" + arr;
    }
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
    F_BROWSER, F_KERNEL, F_BDIR, F_OS, F_UAPRESET, F_UA, F_SHUFFLEUA,
    F_PDATADIR, F_PBROWSERDIR, F_BROWSEDATA, F_BROWSEBROWSER, F_BROWSEFP, // A2 目录+浏览，指纹目录浏览同数据源
    F_PTYPE, F_PHOST, F_PPORT, F_PUSER, F_PPASS, F_PTEST, F_PSAVE, F_PSTATUS,
    F_COOKIE, F_MERGECOOKIE, F_REMARK,
    F_WEBRTC, F_TZM, F_TZ, F_GEOM, F_GEOIP, F_LAT, F_LNG, F_ACC,
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
    if (os == L"mac") return L"MacIntel";
    if (os == L"linux") return L"Linux x86_64";
    if (os == L"android") return L"Linux armv8I";
    if (os == L"ios") return L"iPhone";
    return L"Win32";
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
static const int kFpContentH = 1770;  // 内容总高（A2 新增 52 + 整体下移 48：1674+96=1770）

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
};

static HWND FpMk(HWND p, const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id, HINSTANCE hi) {
    return ::CreateWindowW(cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, p, (HMENU)(INT_PTR)id, hi, NULL);
}
static void FpMkLabel(HWND p, FpWnd* w, int id, const wchar_t* t, int x, int y, int ww) {
    w->ctl[id - F_BASE] = FpMk(p, L"STATIC", t, SS_LEFT, x, y, ww, 20, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
}
static void FpMkEdit(HWND p, FpWnd* w, int id, int x, int y, int ww, int hh = 24) {
    w->ctl[id - F_BASE] = FpMk(p, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, x, y, ww, hh, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
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
// 行高：A(浏览器/内核/目录 64)+A2(独立目录 52)+B(系统/UA 112)+Cookie/备注/代理/WebRTC/时区/地理/语言/界面语言/分辨率/字体
// +噪音+WebGL+WebGPU+CPU+RAM+设备名+MAC+DNT+端口+加速+TLS+启动参数 ≈ 1770
static void FpBuildPages(FpWnd* w, HWND p, HINSTANCE hi) {
    (void)hi;
    // ---- A. 浏览器 / 内核 / 目录（y 8..72）----
    FpMkLabel(p, w, F_BROWSER, L"浏览器", 12, 12, 80);
    FpMkCombo(p, w, F_BROWSER, 100, 10, 200);
    FpComboAdd(w->ctl[F_BROWSER - F_BASE], L"sun - SunBrowser");
    FpComboAdd(w->ctl[F_BROWSER - F_BASE], L"flower - FlowerBrowser");
    FpMkLabel(p, w, F_KERNEL, L"内核", 320, 12, 50);
    FpMkCombo(p, w, F_KERNEL, 370, 10, 300);
    FpComboAdd(w->ctl[F_KERNEL - F_BASE], L"chrome143 - Chrome 143 (SunBrowser 150)");
    FpComboAdd(w->ctl[F_KERNEL - F_BASE], L"chrome121 - Chrome 121 (SunBrowser 121)");
    FpComboAdd(w->ctl[F_KERNEL - F_BASE], L"firefox128 - Firefox 128 (FlowerBrowser)");
    // F_BDIR 是指纹目录完整路径（= 数据父目录 + 环境名），只读展示，与数据目录同源：
    // 数据目录改，联动刷新；浏览按钮选父目录，两行同步。真正落盘只用 A2 两行。
    FpMkLabel(p, w, F_BDIR, L"指纹目录", 12, 46, 80);
    FpMkEdit(p, w, F_BDIR, 100, 44, 500);
    ::SendMessageW(w->ctl[F_BDIR - F_BASE], EM_SETREADONLY, TRUE, 0);
    FpMkBtn(p, w, F_BROWSEFP, L"浏览...", 606, 42, 64);
    // ---- A2. 本指纹独立目录（y 72..124；必填；二合一：指纹目录=数据父目录+环境名，
    // 浏览按钮选父目录；浏览器目录浏览按钮直接选 SunBrowser.exe，自动取其父目录）----
    FpMkLabel(p, w, F_PDATADIR, L"数据目录", 12, 76, 80);
    FpMkEdit(p, w, F_PDATADIR, 100, 74, 500);
    FpMkBtn(p, w, F_BROWSEDATA, L"浏览...", 606, 72, 64);
    FpMkLabel(p, w, F_PBROWSERDIR, L"浏览器目录", 12, 102, 80);
    FpMkEdit(p, w, F_PBROWSERDIR, 100, 100, 500);
    FpMkBtn(p, w, F_BROWSEBROWSER, L"浏览...", 606, 98, 64);
    // ---- B. 系统/UA（y 128..170；整体下移 48）----
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
    FpMkEdit(p, w, F_COOKIE, 100, 192, 570, 100);
    FpMkBtn(p, w, F_MERGECOOKIE, L"合并Cookie", 100, 298, 110);
    FpMkBtn(p, w, F_IMPORT, L"从目录导入指纹", 220, 298, 140);
    FpMkLabel(p, w, F_REMARK, L"备注", 12, 338, 80);
    FpMkEdit(p, w, F_REMARK, 100, 336, 640);
    // ---- C. 代理（y 378..474；整体下移 48）----
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
    // ---- 1. WebRTC（y 480..；整体下移 48）----
    FpMkLabel(p, w, F_WEBRTC, L"WebRTC", 12, 482, 80);
    FpMkCombo(p, w, F_WEBRTC, 100, 480, 260);
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"forward - 转发");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"proxy - 替换");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"disabled - 禁用");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"disable_udp - 禁用UDP");
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
}
// fp_ui.cpp — part 5/6：回填 + 收集
static std::wstring FpFirstTok(const std::wstring& s) {
    size_t p = s.find(L" ");
    return (p == std::wstring::npos) ? s : s.substr(0, p);
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
    selByVal(F_KERNEL, f.kernelVer.empty() ? L"chrome143" : f.kernelVer);
    // 指纹目录与数据目录同源显示：指纹目录 = 数据父目录 + 环境名（只读），数据目录改即联动。
    // A2 独立目录回填：必填（无全局兜底）。显示已保存的独立值；从未保存过则
    // 显示全局值作参考（带“（默认全局，可改）”后缀），保存时以前缀判断落盘。
    {
        std::wstring effD = EffDataDir(w->cfg, w->profile);
        std::wstring effB = EffBrowserDir(w->cfg, w->profile);
        FpSet(C(F_BDIR), effD + L"\\" + w->profile);
        auto it = w->cfg.profiles.find(w->profile);
        bool hasD = (it != w->cfg.profiles.end() && !it->second.dataDir.empty());
        bool hasB = (it != w->cfg.profiles.end() && !it->second.sunBrowserDir.empty());
        FpSet(C(F_PDATADIR), effD + (hasD ? L"" : L"（默认全局，可改）"));
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
    FpSet(C(F_REMARK), f.remark);
    selByVal(F_WEBRTC, f.webrtc.empty() ? L"proxy" : f.webrtc);
    selByVal(F_TZM, f.timezoneMode.empty() ? L"custom" : f.timezoneMode);
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
}
static void FpCollect(FpWnd* w) {
    FpFormData& f = w->form;
    auto C = [&](int id) { return w->ctl[id - F_BASE]; };
    f.browser = FpFirstTok(FpComboGet(C(F_BROWSER)));
    f.kernelVer = FpFirstTok(FpComboGet(C(F_KERNEL)));
    // browserDir 恒等于环境名（只存名，不存路径；路径走 A2），显示层 F_BDIR 为完整路径只读。
    f.browserDir = w->profile;
    // A2 独立目录收集：必填。去“（默认全局，可改）”后缀；为空弹窗阻断保存。
    {
        auto stripTag = [](std::wstring s) -> std::wstring {
            size_t p = s.find(L"（默认全局，可改）");
            if (p != std::wstring::npos) s = s.substr(0, p);
            p = s.find(L"（跟随全局）"); // 兼容旧版后缀
            if (p != std::wstring::npos) s = s.substr(0, p);
            // 去首尾空格
            s.erase(0, s.find_first_not_of(L" \t"));
            if (!s.empty()) s.erase(s.find_last_not_of(L" \t") + 1);
            return s;
        };
        f.profDataDir = stripTag(FpGet(C(F_PDATADIR)));
        f.profBrowserDir = stripTag(FpGet(C(F_PBROWSERDIR)));
    }
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
        // A2 默认取全局生效值（必填，保存时以前缀判断落盘，见 FpFill/F_OK）
        if (w->form.profDataDir.empty()) w->form.profDataDir = effParent0;
        if (w->form.profBrowserDir.empty()) w->form.profBrowserDir = EffBrowserDir(w->cfg, w->profile);
        // static 回填（与 web-ui applyImportResult 同字段）。
        // 优先级：ui 存档优先（用户最后一次保存的值），static 仅补 ui 缺失的键。
        // 以缓存为准的保护键（ProxyChain/DeviceName/MacAddress 等）在 ui 缺失时才用 static 值，
        // 且 ui 已有值时不覆盖——否则“保存后重进看不到修改内容”。
        {
            std::string sj, dj, cj;
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
                    // MediaDevices：仅 ui 缺失时补（ui 存档 mediaDevices 为准）
                    std::string uiMd = FpJsonGet(ui, "mediaDevices");
                    if ((uiMd.empty() || uiMd == "\"\"") ) {
                        v = FpJsonGet(sj, "MediaDevices");
                        if (!v.empty() && v.front() == '"') w->form.mediaDevices = WJ(v);
                    }
                    // TTSEngines -> speechSwitch：仅 ui 缺失时
                    std::string uiSp = FpJsonGet(ui, "speechSwitch");
                    if (uiSp.empty() || uiSp == "\"\"") {
                        v = FpJsonGet(sj, "TTSEngines");
                        if (!v.empty()) w->form.swSpeech = (v.find('1') != std::string::npos);
                    }
                    // 时区/地理 static 后备：仅 ui 对应键缺失时（timezone/timezoneMode/geoMode/lat/lng/accuracy）
                    std::string uiTz = FpJsonGet(ui, "timezone");
                    if (uiTz.empty() || uiTz == "\"\"") {
                        v = FpJsonGet(sj, "TimeZone");
                        if (!v.empty() && v.front() == '"') {
                            std::string t = N(WJ(v));
                            for (auto& c : t) if (c == '_') c = ' ';
                            w->form.timezone = W(t);
                            w->form.timezoneMode = L"custom";
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
                    // CanvasMark/WebGLMark -> 开关+种子显示：仅 ui 缺失时（ui canvas/webglImage 为准）
                    std::string uiCv = FpJsonGet(ui, "canvas");
                    if (uiCv.empty() || uiCv == "\"\"") {
                        v = FpJsonGet(sj, "CanvasMark");
                        if (!v.empty()) w->form.swCanvas = true;
                    }
                    std::string uiWg = FpJsonGet(ui, "webglImage");
                    if (uiWg.empty() || uiWg == "\"\"") {
                        v = FpJsonGet(sj, "WebGLMark");
                        if (!v.empty()) w->form.swWebglImg = true;
                    }
                    // WebRTC/代理 static 后备（ui 缺失时）
                    std::string uiWr = FpJsonGet(ui, "webrtc");
                    if (uiWr.empty() || uiWr == "\"\"") {
                        v = FpJsonGet(sj, "WebRTCAddress");
                        std::string dw = FpJsonGet(sj, "DisableWebRTC");
                        if (dw == "true" || dw == "\"true\"") w->form.webrtc = L"disabled";
                    }
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
            if (FpLoadDynamicJson(dd, dj) && !dj.empty()) {
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
            bi.lpszTitle = L"选择该指纹的数据父目录（指纹目录=父目录+环境名）";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = ::SHBrowseForFolderW(&bi);
            if (pidl) {
                if (::SHGetPathFromIDListW(pidl, dir) && dir[0]) {
                    FpSet(C(F_PDATADIR), dir);
                    // 同源联动：指纹目录 = 数据父目录 + 环境名，只读刷新
                    FpSet(C(F_BDIR), std::wstring(dir) + L"\\" + w->profile);
                    FpCollect(w);
                    ::SetWindowTextW(w->hStatus, L"数据目录已选择（指纹目录已同步，保存进独立目录）");
                    LOG(L"指纹浏览 数据目录=" + std::wstring(dir) + L" " + w->profile);
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
        // 指纹目录浏览：与数据目录同源（选父目录，两行同步）。指纹目录本身只读，不直接编辑。
        if (id == F_BROWSEFP) {
            bool needUninit = false;
            HRESULT hrCo = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (SUCCEEDED(hrCo)) needUninit = true;
            else if (hrCo != RPC_E_CHANGED_MODE) {
                ::SetWindowTextW(w->hStatus, L"浏览失败：COM 初始化失败");
                return 0;
            }
            wchar_t dir[MAX_PATH]{};
            BROWSEINFOW bi{};
            bi.hwndOwner = h;
            bi.pszDisplayName = dir;
            bi.lpszTitle = L"选择指纹目录的父目录（指纹目录=父目录+环境名，与数据目录相同）";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = ::SHBrowseForFolderW(&bi);
            if (pidl) {
                if (::SHGetPathFromIDListW(pidl, dir) && dir[0]) {
                    FpSet(C(F_PDATADIR), dir);
                    FpSet(C(F_BDIR), std::wstring(dir) + L"\\" + w->profile);
                    FpCollect(w);
                    ::SetWindowTextW(w->hStatus, L"指纹目录已选择（已同步数据目录）");
                    LOG(L"指纹浏览 指纹目录=" + std::wstring(dir) + L"\\" + w->profile + L" " + w->profile);
                }
                ::CoTaskMemFree(pidl);
            } else {
                ::SetWindowTextW(w->hStatus, L"未选择指纹目录");
            }
            if (needUninit) ::CoUninitialize();
            return 0;
        }
        // 内核联动浏览器目录：内核下拉切换时，按“浏览器类型→内核版本→目录名”规则
        // 自动重算浏览器目录建议值（chrome143→chrome_152、chrome121→chrome_121、
        // firefox128→flower_100；全局前缀不变，只换尾段目录名），填入 A2 行。
        // 规则来源：getBrowserPath（win32）+ DATA_FLODER 形态；用户仍可手工改。
        if ((id == F_KERNEL && (code == CBN_SELCHANGE || code == CBN_SELENDOK)) ||
            (id == F_BROWSER && (code == CBN_SELCHANGE || code == CBN_SELENDOK))) {
            FpCollect(w);
            std::wstring tail;
            if (w->form.browser == L"flower" || w->form.kernelVer == L"firefox128")
                tail = L"flower_100";
            else if (w->form.kernelVer == L"chrome121")
                tail = L"chrome_121";
            else
                tail = L"chrome_152"; // chrome143 默认
            // 取当前 A2 行或全局 browserDir 的父目录前缀，只换尾段
            std::wstring cur = w->form.profBrowserDir.empty()
                ? EffBrowserDir(w->cfg, w->profile) : w->form.profBrowserDir;
            size_t p = cur.find_last_of(L"\\/");
            std::wstring sug = (p == std::wstring::npos) ? tail : (cur.substr(0, p + 1) + tail);
            FpSet(C(F_PBROWSERDIR), sug);
            w->form.profBrowserDir = sug;
            std::wstring m = L"内核联动：浏览器目录已建议为 " + tail + L"（可手工改，保存进独立目录）";
            ::SetWindowTextW(w->hStatus, m.c_str());
            LOG(L"指纹内核联动 kernel=" + w->form.kernelVer + L" browser=" + w->form.browser + L" sugTail=" + tail + L" " + w->profile);
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
            swprintf_s(msg, L"Cookie 合并成功，共 %d 个", n);
            ::SetWindowTextW(w->hStatus, msg);
            return 0;
        }
        if (id == F_IMPORT) {
            // 从目录导入指纹：导入源 = A2 行“数据目录”输入框里的父目录 + 环境目录名。
            // 逻辑：A2 必填（F_OK 同规则），先 FpCollect 取 A2 当前输入，源目录不存在/
            // 无三件套则状态条报错并记日志；导入成功回填表单（覆盖当前编辑）+ FpFill。
            // 注意：改了 A2 再点导入 = 换源目录导入；导入不改 A2 本身，不写盘。
            FpCollect(w);
            if (w->form.profDataDir.empty()) {
                ::MessageBoxW(h, L"请先在顶端 A2 行填写数据目录（导入源父目录），再点导入。", L"从目录导入指纹", MB_OK | MB_ICONWARNING);
                ::SetWindowTextW(w->hStatus, L"导入已阻断：A2 数据目录为空");
                LOG(L"指纹导入 F_IMPORT BLOCKED(A2空) " + w->profile);
                return 0;
            }
            std::wstring srcParent = w->form.profDataDir;
            std::wstring srcDir = srcParent + L"\\" + w->profile;
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
            if (nRD == 0) {
                ::SetWindowTextW(w->hStatus, (L"导入源无三件套: " + srcDir).c_str());
                return 0;
            }
            // 复用打开时回填链：先 ui 侧车（若有），再 static/dynamic/cookies。
            // 为复用逻辑，把源目录三件套先解码进 form：走 FpFormFromUiJson + 手工字段。
            // 简单做法：临时把 w->cfg.dataDir 指向源父目录，调回填段——此处直接内联：
            {
                // ui 侧车
                if (FpLoadUiExtra(srcDir, ui2) && !ui2.empty())
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
                    if (uiMd.empty() || uiMd == "\"\"") { v = FpJsonGet(sj2, "MediaDevices"); if (!v.empty() && v.front() == '"') w->form.mediaDevices = WJ(v); }
                    std::string uiTz = FpJsonGet(ui2, "timezone");
                    if (uiTz.empty() || uiTz == "\"\"") {
                        v = FpJsonGet(sj2, "TimeZone");
                        if (!v.empty() && v.front() == '"') {
                            std::string t = N(WJ(v)); for (auto& c : t) if (c == '_') c = ' ';
                            w->form.timezone = W(t); w->form.timezoneMode = L"custom";
                        }
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
                    std::string uiCv = FpJsonGet(ui2, "canvas");
                    if (uiCv.empty() || uiCv == "\"\"") { v = FpJsonGet(sj2, "CanvasMark"); if (!v.empty()) w->form.swCanvas = true; }
                    std::string uiWg = FpJsonGet(ui2, "webglImage");
                    if (uiWg.empty() || uiWg == "\"\"") { v = FpJsonGet(sj2, "WebGLMark"); if (!v.empty()) w->form.swWebglImg = true; }
                    std::string uiWr = FpJsonGet(ui2, "webrtc");
                    if (uiWr.empty() || uiWr == "\"\"") {
                        v = FpJsonGet(sj2, "WebRTCAddress"); std::string dw = FpJsonGet(sj2, "DisableWebRTC");
                        if (dw == "true" || dw == "\"true\"") w->form.webrtc = L"disabled";
                    }
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
            // 本机 TCP connect 探测（与 /api/proxy/check 一致，3s 超时）
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
            // A2 独立目录必填：两行都不能为空（无全局兜底），空则弹窗阻断保存。
            if (w->form.profDataDir.empty() || w->form.profBrowserDir.empty()) {
                ::MessageBoxW(h, L"数据目录 / 浏览器目录不能为空。\r\n请在顶端 A2 行填写该指纹的独立目录后再保存。", L"指纹配置", MB_OK | MB_ICONWARNING);
                ::SetWindowTextW(w->hStatus, L"保存已阻断：A2 独立目录必填");
                LOG(L"指纹保存 BLOCKED(A2目录空) " + w->profile);
                return 0;
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
                // 注意 main.cpp /api/fp/save 的 protectFill 会用缓存值覆盖这 3 个键（以缓存为准），
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
            // 保存结果同样进 debug.log：cookies 写盘失败不再静默。
            bool okc = true;
            if (!w->form.cookie.empty()) {
                std::string ck = N(w->form.cookie);
                // 非 JSON 则跳过 cookies 写盘（与 web-ui 一致）
                if (!ck.empty() && ck.front() == '[') {
                    okc = FpSaveCookiesJson(dd, ck);
                    LOG(L"指纹保存 cookies " + std::wstring(okc ? L"OK" : L"FAIL") +
                        L" len=" + std::to_wstring(ck.size()) + L" " + w->profile);
                } else {
                    LOG(L"指纹保存 cookies SKIP（非JSON数组，不写盘） " + w->profile);
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
                    // 保護鍵回填（与 main.cpp /api/fp/save protectFill 同表），例外见上：
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
                    bool oks = FpSaveStaticJson(dd, cfg);
                LOG(L"指纹保存 static " + std::wstring(oks ? L"OK" : L"FAIL") +
                    L" len=" + std::to_wstring(cfg.size()) + L" " + w->profile);
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

