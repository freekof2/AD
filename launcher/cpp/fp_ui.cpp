// fp_ui.cpp — 指纹配置原生窗口实现（part 1/4：JSON 互转 + 随机库）
#include "fp_ui.h"
#include "fingerprint.h"
#include <ctime>

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
    o += ",\"browserDir\":\"" + JEsc(f.browserDir) + "\",\"os\":\"" + JEsc(f.os) + "\"";
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
    std::string sp = N(f.webrtc), tz = (f.timezoneMode == L"ip") ? "1" : "0";
    std::string o = "{\"webrtc\":\"" + sp + "\",\"automatic_timezone\":\"" + tz + "\"";
    if (tz == "0") {
        std::string tzn = N(f.timezone);
        for (auto& c : tzn) if (c == ' ') c = '_';
        o += ",\"timezone\":\"" + tzn + "\"";
    } else o += ",\"timezone\":\"\"";
    std::string loc = N(f.geoMode);
    o += ",\"location\":\"" + loc + "\",\"location_switch\":\"" + std::string(f.geoIp == L"ip" ? "1" : "0") + "\"";
    if (f.geoIp != L"ip")
        o += ",\"latitude\":\"" + N(f.lat) + "\",\"longitude\":\"" + N(f.lng) + "\",\"accuracy\":\"" + N(f.accuracy) + "\"";
    else o += ",\"latitude\":\"\",\"longitude\":\"\",\"accuracy\":\"\"";
    std::string lang = N(f.langList);
    int cnt = 1;
    // 与 web-ui collectFp 一致：按逗号/分号/换行切分计数（单行 EDIT 无换行，但兼容粘贴值）
    { size_t p = 0; cnt = 0; while (p <= lang.size()) { size_t e = lang.find_first_of(",;\n", p); cnt++; if (e == std::string::npos) break; p = e + 1; } }
    o += ",\"language\":\"" + lang + "\",\"language_switch\":\"" + std::string(cnt <= 1 ? "1" : "0") + "\"";
    std::string res = N(f.resolution);
    if (f.resMode == L"custom" && !f.resW.empty() && !f.resH.empty())
        res = N(f.resW) + "_" + N(f.resH);
    o += ",\"screen_resolution\":\"" + res + "\"";
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
static const int kFpContentH = 1720;  // 内容总高（底 1564+110=1674 + 46 边距，24 行单页）

// 单页窗口状态（滚动位置 + 内容容器；Tab 相关已删除，见 git 历史）
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
// 行高：A(浏览器/内核/目录 64)+B(系统/UA 64)+C(代理 96)+D(Cookie 132/备注 32)
// +WebRTC(36)+时区(64)+地理(120)+语言(96)+界面语言(36)+分辨率(96)+字体(80)
// +噪音(96)+WebGL(108)+WebGPU(64)+CPU(36)+RAM(36)+设备名(36)+MAC(36)
// +DNT(36)+端口(64)+加速(36)+TLS(64)+启动参数(120) ≈ 2050
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
    FpMkLabel(p, w, F_BDIR, L"浏览器目录", 12, 46, 80);
    FpMkEdit(p, w, F_BDIR, 100, 44, 570);
    FpMkLabel(p, w, F_OS, L"系统", 12, 80, 80);
    FpMkCombo(p, w, F_OS, 100, 78, 200);
    FpComboAdd(w->ctl[F_OS - F_BASE], L"win - Windows");
    FpComboAdd(w->ctl[F_OS - F_BASE], L"mac - macOS");
    FpComboAdd(w->ctl[F_OS - F_BASE], L"linux - Linux");
    FpComboAdd(w->ctl[F_OS - F_BASE], L"android - Android");
    FpComboAdd(w->ctl[F_OS - F_BASE], L"ios - iOS");
    FpMkLabel(p, w, F_UAPRESET, L"UA版本", 320, 80, 60);
    FpMkEdit(p, w, F_UAPRESET, 380, 78, 80);
    FpMkBtn(p, w, F_SHUFFLEUA, L"换UA", 470, 76, 80);
    FpMkLabel(p, w, F_UA, L"User-Agent", 12, 112, 80);
    FpMkEdit(p, w, F_UA, 100, 110, 570);
    FpMkLabel(p, w, F_COOKIE, L"Cookie", 12, 146, 80);
    FpMkEdit(p, w, F_COOKIE, 100, 144, 570, 100);
    FpMkBtn(p, w, F_MERGECOOKIE, L"合并Cookie", 100, 250, 110);
    FpMkBtn(p, w, F_IMPORT, L"从目录导入指纹", 220, 250, 140);
    FpMkLabel(p, w, F_REMARK, L"备注", 12, 290, 80);
    FpMkEdit(p, w, F_REMARK, 100, 288, 640);
    // ---- C. 代理（y 330..426，h=96；网页 C 行两行输入+状态）----
    FpMkLabel(p, w, F_PTYPE, L"代理", 12, 336, 80);
    FpMkCombo(p, w, F_PTYPE, 100, 332, 120);
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"socks5");
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"http");
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"https");
    FpMkLabel(p, w, F_PHOST, L"主机", 230, 334, 50);
    FpMkEdit(p, w, F_PHOST, 280, 332, 180);
    FpMkLabel(p, w, F_PPORT, L"端口", 470, 334, 40);
    FpMkEdit(p, w, F_PPORT, 510, 332, 80);
    FpMkLabel(p, w, F_PUSER, L"账号", 12, 368, 80);
    FpMkEdit(p, w, F_PUSER, 100, 366, 180);
    FpMkLabel(p, w, F_PPASS, L"密码", 290, 368, 40);
    FpMkEdit(p, w, F_PPASS, 330, 366, 150);
    FpMkBtn(p, w, F_PTEST, L"测速/检测", 490, 364, 90);
    FpMkBtn(p, w, F_PSAVE, L"保存为代理", 590, 364, 110);
    FpMkLabel(p, w, F_PSTATUS, L"未检测", 12, 400, 400);
    // ---- 1. WebRTC（y 432..468，h=36）----
    FpMkLabel(p, w, F_WEBRTC, L"WebRTC", 12, 434, 80);
    FpMkCombo(p, w, F_WEBRTC, 100, 432, 260);
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"forward - 转发");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"proxy - 替换");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"disabled - 禁用");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"disable_udp - 禁用UDP");
    FpMkLabel(p, w, F_TZM, L"时区模式", 12, 476, 80);
    FpMkCombo(p, w, F_TZM, 100, 474, 150);
    FpComboAdd(w->ctl[F_TZM - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_TZM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_TZ, 260, 474, 320);
    for (auto t : kTz) FpComboAdd(w->ctl[F_TZ - F_BASE], t);
    FpMkLabel(p, w, F_GEOM, L"地理", 12, 546, 80);
    FpMkCombo(p, w, F_GEOM, 100, 544, 150);
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"ask - 询问");
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"allow - 允许");
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"block - 禁止");
    FpMkCombo(p, w, F_GEOIP, 260, 544, 150);
    FpComboAdd(w->ctl[F_GEOIP - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_GEOIP - F_BASE], L"custom - 自定义");
    FpMkLabel(p, w, F_LAT, L"纬/经/精度", 12, 580, 80);
    FpMkEdit(p, w, F_LAT, 100, 578, 120);
    FpMkEdit(p, w, F_LNG, 230, 578, 120);
    FpMkEdit(p, w, F_ACC, 360, 578, 100);
    FpMkLabel(p, w, F_LANGM, L"语言模式", 12, 672, 80);
    FpMkCombo(p, w, F_LANGM, 100, 670, 150);
    FpComboAdd(w->ctl[F_LANGM - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_LANGM - F_BASE], L"custom - 自定义");
    FpMkEdit(p, w, F_LANGLIST, 260, 670, 260, 48);
    FpMkLabel(p, w, F_UILANG, L"界面语言", 12, 774, 80);
    FpMkCombo(p, w, F_UILANG, 100, 772, 180);
    FpComboAdd(w->ctl[F_UILANG - F_BASE], L"follow_lang - 基于语言");
    FpComboAdd(w->ctl[F_UILANG - F_BASE], L"custom - 自定义");
    FpMkEdit(p, w, F_PAGELANG, 290, 772, 220);
        // ---- 6. 分辨率（y 814..910，h=96）----
    FpMkLabel(p, w, F_RESM, L"分辨率", 12, 816, 80);
    FpMkCombo(p, w, F_RESM, 100, 814, 140);
    FpComboAdd(w->ctl[F_RESM - F_BASE], L"preset - 预定义");
    FpComboAdd(w->ctl[F_RESM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_RES, 250, 814, 170);
    FpComboAdd(w->ctl[F_RES - F_BASE], L"none");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1920_1080");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"2560_1440");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1440_900");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1366_768");
    FpMkEdit(p, w, F_RESW, 430, 814, 70);
    FpMkEdit(p, w, F_RESH, 510, 814, 70);
    FpMkLabel(p, w, F_FONTM, L"字体", 12, 858, 80);
    FpMkCombo(p, w, F_FONTM, 100, 856, 150);
    FpComboAdd(w->ctl[F_FONTM - F_BASE], L"all - 默认");
    FpComboAdd(w->ctl[F_FONTM - F_BASE], L"custom - 自定义");
    FpMkBtn(p, w, F_SHUFFLEFONTS, L"换一换", 260, 856, 80);
    FpMkEdit(p, w, F_FONTS, 100, 888, 560, 44);
    // ---- 8. 硬件噪音开关（y 940..990：两行复选框，避开字体区 888..932 与媒体行 970）----
    FpMkCheck(p, w, F_SWCVS, L"Canvas(=1)", 12, 940, 130);
    FpMkCheck(p, w, F_SWWGL, L"WebGL图像(=1)", 150, 940, 150);
    FpMkCheck(p, w, F_SWAUD, L"Audio(=1)", 310, 940, 120);
    FpMkCheck(p, w, F_SWRECT, L"ClientRects(=1)", 440, 940, 150);
    FpMkCheck(p, w, F_SWSPEECH, L"Speech(=1)", 12, 964, 130);
    FpMkLabel(p, w, F_MEDIA, L"媒体设备", 150, 966, 80);
    FpMkCombo(p, w, F_MEDIA, 100, 992, 150);
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"0 - 真实/关闭");
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"1 - 随机");
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"2 - 自定义");
    FpMkEdit(p, w, F_MIN, 260, 992, 60);
    FpMkEdit(p, w, F_MVID, 330, 992, 60);
    FpMkEdit(p, w, F_MOUT, 400, 992, 60);
    // ---- 9. WebGL元数据（y 1044..1152，h=108）----
    FpMkLabel(p, w, F_WGLM, L"WebGL元数据", 12, 1046, 90);
    FpMkCombo(p, w, F_WGLM, 110, 1044, 150);
    FpComboAdd(w->ctl[F_WGLM - F_BASE], L"real - 真实(0)");
    FpComboAdd(w->ctl[F_WGLM - F_BASE], L"custom - 自定义(2)");
    FpMkCombo(p, w, F_VENDOR, 270, 1044, 220);
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (Intel)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (NVIDIA)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (AMD)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Apple Inc.");
    FpMkEdit(p, w, F_RENDERER, 110, 1078, 440);
    FpMkBtn(p, w, F_SHUFFLERDR, L"随机", 560, 1076, 70);
    // ---- 10. WebGPU（y 1158..1222，h=64）----
    FpMkLabel(p, w, F_WGPU, L"WebGPU", 12, 1160, 80);
    FpMkCombo(p, w, F_WGPU, 110, 1158, 200);
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"follow_webgl - 跟随(1)");
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"disabled - 禁用(0)");
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"custom - 自定义适配器(2)");
    FpMkLabel(p, w, F_GVENDOR, L"厂商", 320, 1160, 44);
    FpMkEdit(p, w, F_GVENDOR, 368, 1158, 150);
    FpMkLabel(p, w, F_GARCH, L"架构", 528, 1160, 44);
    FpMkEdit(p, w, F_GARCH, 576, 1158, 150);
    // ---- 页3 设备伪装：CPU/RAM/设备名/MAC ----
    // ---- 11. CPU（y 1228..1264，h=36）----
    FpMkLabel(p, w, F_CPUM, L"CPU模式", 12, 1230, 80);
    FpMkCombo(p, w, F_CPUM, 100, 1228, 150);
    FpComboAdd(w->ctl[F_CPUM - F_BASE], L"real - 真实");
    FpComboAdd(w->ctl[F_CPUM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_CPU, 260, 1228, 180);
    for (auto c : { L"default", L"2", L"4", L"6", L"8", L"10", L"12", L"16", L"20", L"24" }) FpComboAdd(w->ctl[F_CPU - F_BASE], c);
    FpMkLabel(p, w, F_RAMM, L"RAM模式", 12, 1272, 80);
    FpMkCombo(p, w, F_RAMM, 100, 1270, 150);
    FpComboAdd(w->ctl[F_RAMM - F_BASE], L"real - 真实");
    FpComboAdd(w->ctl[F_RAMM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_RAM, 260, 1270, 180);
    for (auto c : { L"default", L"2", L"4", L"6", L"8", L"16", L"32", L"64", L"128" }) FpComboAdd(w->ctl[F_RAM - F_BASE], c);
    // ---- 13. 设备名称（y 1312..1348，h=36）----
    FpMkLabel(p, w, F_DEVM, L"设备名", 12, 1314, 80);
    FpMkCombo(p, w, F_DEVM, 100, 1312, 150);
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"off - 关闭(0)");
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"random - 随机(1)");
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"custom - 自定义(2)");
    FpMkEdit(p, w, F_DEVNAME, 260, 1312, 220);
    FpMkBtn(p, w, F_SHUFFLEDEV, L"随机", 490, 1310, 70);
    FpMkLabel(p, w, F_MACM, L"MAC", 12, 1356, 80);
    FpMkCombo(p, w, F_MACM, 100, 1354, 150);
    FpComboAdd(w->ctl[F_MACM - F_BASE], L"off - 关闭(0)");
    FpComboAdd(w->ctl[F_MACM - F_BASE], L"custom - 自定义(2)");
    FpMkEdit(p, w, F_MAC, 260, 1354, 220);
    FpMkBtn(p, w, F_SHUFFLEMAC, L"随机", 490, 1352, 70);
    // ---- 页4 高级：DNT/端口/加速/TLS/启动参数 ----
    // ---- 15. Do Not Track（y 1396..1432，h=36）----
    FpMkLabel(p, w, F_DNT, L"DoNotTrack", 12, 1398, 90);
    FpMkCombo(p, w, F_DNT, 110, 1396, 170);
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"open - 开启");
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"close - 关闭");
    FpMkLabel(p, w, F_PORTSCAN, L"端口扫描", 300, 1398, 100);
    FpMkCombo(p, w, F_PORTSCAN, 410, 1396, 150);
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"open - 启用(1)");
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"close - 关闭(0)");
    FpMkLabel(p, w, F_WPORTS, L"白名单端口", 12, 1440, 90);
    FpMkEdit(p, w, F_WPORTS, 110, 1438, 420);
    // ---- 17. 硬件加速（y 1480..1516，h=36）----
    FpMkLabel(p, w, F_HWACC, L"硬件加速", 12, 1482, 90);
    FpMkCombo(p, w, F_HWACC, 110, 1480, 170);
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"open - 开启");
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"close - 关闭");
    FpMkLabel(p, w, F_TLSM, L"TLS", 300, 1482, 80);
    FpMkCombo(p, w, F_TLSM, 390, 1480, 150);
    FpComboAdd(w->ctl[F_TLSM - F_BASE], L"close - 默认");
    FpComboAdd(w->ctl[F_TLSM - F_BASE], L"open - 自定义(1)");
    FpMkEdit(p, w, F_TLS, 110, 1522, 460);
    // ---- 19. 启动参数（y 1564..1684，h=120）----
    FpMkLabel(p, w, F_ARGS, L"启动参数", 12, 1566, 90);
    FpMkEdit(p, w, F_ARGS, 110, 1564, 560, 110);
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
    FpSet(C(F_BDIR), f.browserDir);
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
    selByVal(F_UILANG, f.uiLang.empty() ? L"follow_lang" : f.uiLang);
    FpSet(C(F_PAGELANG), f.pageLang);
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
    f.browserDir = FpGet(C(F_BDIR));
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
    f.pageLang = FpGet(C(F_PAGELANG));
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
        FpScrollInit(w);
        // 底部按钮（y=564，高 30，互不重叠：随机 8..108；保存 636..736；取消 744..844）
        FpMkBtn(h, w, F_RANDOM, L"一键随机", 8, 564, 100);
        FpMkBtn(h, w, F_OK, L"保存", 636, 564, 100);
        FpMkBtn(h, w, F_CANCEL, L"取消", 744, 564, 100);
        w->hStatus = ::CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 120, 568, 500, 22, h, (HMENU)(INT_PTR)F_STATUS, hi, NULL);
        // 初值：ui 侧车 -> static/dynamic 回填 -> 默认
        std::wstring dd = w->cfg.dataDir + L"\\" + w->profile;
        std::string ui;
        bool hasUi = FpLoadUiExtra(dd, ui) && !ui.empty();
        if (hasUi) FpFormFromUiJson(ui, w->form);
        else FpFormDefaults(w->form, w->profile);
        // static 回填（与 web-ui applyImportResult 同字段：Langs/Platform/CPU/RAM/设备/MAC/代理）
        {
            std::string sj, dj, cj;
            if (FpLoadStaticJson(dd, sj) && !sj.empty()) {
                std::string v;
                v = FpJsonGet(sj, "Langs");
                if (!v.empty() && v.front() == '"') {
                    // Langs 可能是 "en-US,en" 字符串 -> langList
                    w->form.langList = WJ(v);
                    w->form.langMode = L"custom";
                }
                // Platform -> os 反推（Win32->win；Darwin/mac->mac；Linux->linux）
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
                v = FpJsonGet(sj, "HardwareConcurrency");
                if (!v.empty()) {
                    w->form.cpu = W(v);
                    w->form.cpuMode = (v == "default" || v == "\"default\"") ? L"real" : L"custom";
                }
                v = FpJsonGet(sj, "DeviceMemory");
                if (!v.empty()) {
                    w->form.ram = W(v);
                    w->form.ramMode = (v == "default" || v == "\"default\"") ? L"real" : L"custom";
                }
                v = FpJsonGet(sj, "DeviceName"); if (!v.empty()) { w->form.devName = WJ(v); }
                v = FpJsonGet(sj, "MacAddress"); if (!v.empty()) { w->form.mac = WJ(v); }
                // ProxyChain 对象粗取 host/port/scheme（与 web-ui applyImportResult 一致）
                std::string pc = FpJsonGet(sj, "ProxyChain");
                if (!pc.empty()) {
                    std::string hh = FpJsonGet(pc, "host"), pp = FpJsonGet(pc, "port"), ss = FpJsonGet(pc, "scheme");
                    if (hh.size() >= 2 && hh.front() == '"') w->form.proxyHost = WJ(hh);
                    if (pp.size() >= 2 && pp.front() == '"') w->form.proxyPort = WJ(pp); else w->form.proxyPort = W(pp);
                    if (ss.size() >= 2 && ss.front() == '"') w->form.proxyType = WJ(ss);
                }
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
        auto C = [&](int c) { return w->ctl[c - F_BASE]; };
        if (id == F_CANCEL) { ::DestroyWindow(h); return 0; }
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
            ::SetWindowTextW(w->hStatus, L"当前目录即导入源：三件套已在打开时回填");
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
            std::wstring dd = w->cfg.dataDir + L"\\" + w->profile;
            ::CreateDirectoryW(w->cfg.dataDir.c_str(), NULL);
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
            // protectFill 语义：已有缓存的保护键以缓存为准，但 ProxyChain 为空数组时
            // 允许表单新值覆盖（否则代理永远写不进去——本次 SOCKS5 不生效根因）。
            {
                std::string cfg = FpFormToFpConfig(w->form);
                std::string curS;
                FpLoadStaticJson(dd, curS);
                // 保護鍵回填（与 main.cpp /api/fp/save protectFill 同表），但 ProxyChain 例外：
                // 缓存 ProxyChain 为空/缺失时用表单新值；非空时以缓存为准。
                static const char* prot[] = { "DeviceName","MacAddress",
                    "MediaDevices","TTSEngines","Langs","AcceptLang","HardwareConcurrency",
                    "DeviceMemory","Platform","UserId","CanvasMark","WebGLMark","AudioFp",
                    "ClientRectFp", NULL };
                for (int i = 0; prot[i]; i++) {
                    std::string cv = FpJsonGet(curS, prot[i]);
                    if (!cv.empty()) {
                        std::string merged = FpJsonSet(cfg, prot[i], cv);
                        if (!merged.empty()) cfg = merged;
                    }
                }
                std::string curPc = FpJsonGet(curS, "ProxyChain");
                std::string newPc = FpJsonGet(cfg, "ProxyChain");
                if (!curPc.empty() && curPc != "[]" && newPc == "[]") {
                    std::string merged = FpJsonSet(cfg, "ProxyChain", curPc);
                    if (!merged.empty()) cfg = merged;
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

