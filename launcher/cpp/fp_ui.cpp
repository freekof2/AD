// fp_ui.cpp — 指纹配置原生窗口实现（part 1/4：JSON 互转 + 随机库）
#include "fp_ui.h"
#include "fingerprint.h"
#include <ctime>

static std::string JEsc(const std::wstring& w) {
    std::string s = N(w), o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}
static std::wstring WJ(const std::string& raw) {
    // FpJsonGet 返回含引号的原始片段，去引号转回 wstring
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        std::string s = raw.substr(1, raw.size() - 2), o;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) { o += s[i + 1]; i++; }
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
    o += ",\"canvas\":\"" + std::string(f.swCanvas ? "1" : "0") + "\",\"webglImage\":\"" + (f.swWebglImg ? "1" : "0") + "\"";
    o += ",\"audio\":\"" + std::string(f.swAudio ? "1" : "0") + "\",\"clientRects\":\"" + (f.swClientRects ? "1" : "0") + "\"";
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

// 表单 -> fingerprint_config（与 web-ui btnFpSave 的 fpConfig 组装一致）
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
    std::string lang = N(f.langList), cnt = 1;
    { size_t p = 0; cnt = 0; while (p <= lang.size()) { size_t e = lang.find(',', p); cnt++; if (e == std::string::npos) break; p = e + 1; } }
    o += ",\"language\":\"" + lang + "\",\"language_switch\":\"" + (cnt <= 1 ? "1" : "0") + "\"";
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
    o += ",\"canvas\":\"" + std::string(f.swCanvas ? "1" : "0") + "\",\"webgl_image\":\"" + (f.swWebglImg ? "1" : "0") + "\"";
    o += ",\"audio\":\"" + std::string(f.swAudio ? "1" : "0") + "\",\"client_rects\":\"" + (f.swClientRects ? "1" : "0") + "\"";
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
    o += ",\"speech_switch\":\"" + (f.swSpeech ? "1" : "0") + "\"";
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
    // 字体：all->["all"]；custom->按逗号/中文逗号/换行切分数组
    if (f.fontMode == L"all") o += ",\"fonts\":[\"all\"]";
    else {
        std::string fs = N(f.fonts), arr = "[";
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
    o += ",\"ua\":\"" + JEsc(f.ua) + "\"}";
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
// fp_ui.cpp — part 3/4：窗口骨架（Tab 5 页 + 底部按钮）
struct FpWnd {
    HWND hDlg = NULL, hTab = NULL, hStatus = NULL;
    HWND ctl[F_END - F_BASE] = {};
    FpFormData form;
    Config cfg;
    std::wstring profile;
    bool saved = false;
};

static const wchar_t* kTabNames[] = {
    L"基础", L"网络指纹", L"硬件指纹", L"设备伪装", L"高级",
};

static HWND FpMk(HWND p, const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id, HINSTANCE hi) {
    return ::CreateWindowW(cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, p, (HMENU)(INT_PTR)id, hi, NULL);
}
static void FpMkLabel(HWND p, FpWnd* w, int id, const wchar_t* t, int x, int y, int ww) {
    w->ctl[id - F_BASE] = FpMk(p, L"STATIC", t, SS_LEFT, x, y, ww, 20, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
}
static void FpMkEdit(HWND p, FpWnd* w, int id, int x, int y, int ww, int hh = 24) {
    w->ctl[id - F_BASE] = FpMk(p, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, x, y, ww, hh, id, (HINSTANCE)::GetWindowLongPtrW(p, GWLP_HINSTANCE));
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

// Tab 页容器：5 个 child panel，切换时 show/hide
static HWND FpMkPanel(HWND tab, HINSTANCE hi) {
    RECT rc{};
    ::GetClientRect(tab, &rc);
    ::MapWindowPoints(tab, ::GetParent(tab), (POINT*)&rc, 2);
    HWND p = ::CreateWindowW(L"STATIC", L"", WS_CHILD, rc.left + 8, rc.top + 30, 740, 440,
        ::GetParent(tab), NULL, hi, NULL);
    return p;
}
// fp_ui.cpp — part 4/4：5 页控件排布 + 回填/收集 + 保存 + 模态循环
static void FpBuildPages(FpWnd* w, HWND panels[5], HINSTANCE hi) {
    // ---- 页0 基础：浏览器/内核/目录 + 系统/UA + Cookie + 备注 ----
    HWND p = panels[0];
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
    FpMkEdit(p, w, F_REMARK, 100, 288, 570);
    // ---- 页1 网络指纹：代理 + WebRTC + 时区 + 地理 + 语言 ----
    p = panels[1];
    FpMkLabel(p, w, F_PTYPE, L"代理类型", 12, 12, 80);
    FpMkCombo(p, w, F_PTYPE, 100, 10, 120);
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"socks5");
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"http");
    FpComboAdd(w->ctl[F_PTYPE - F_BASE], L"https");
    FpMkLabel(p, w, F_PHOST, L"主机", 230, 12, 50);
    FpMkEdit(p, w, F_PHOST, 280, 10, 180);
    FpMkLabel(p, w, F_PPORT, L"端口", 470, 12, 40);
    FpMkEdit(p, w, F_PPORT, 510, 10, 80);
    FpMkLabel(p, w, F_PUSER, L"账号", 12, 46, 80);
    FpMkEdit(p, w, F_PUSER, 100, 44, 180);
    FpMkLabel(p, w, F_PPASS, L"密码", 290, 46, 40);
    FpMkEdit(p, w, F_PPASS, 330, 44, 150);
    FpMkBtn(p, w, F_PTEST, L"测速/检测", 490, 42, 90);
    FpMkBtn(p, w, F_PSAVE, L"保存为代理", 590, 42, 90);
    FpMkLabel(p, w, F_PSTATUS, L"未检测", 12, 78, 400);
    FpMkLabel(p, w, F_WEBRTC, L"WebRTC", 12, 110, 80);
    FpMkCombo(p, w, F_WEBRTC, 100, 108, 260);
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"forward - 转发");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"proxy - 替换");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"disabled - 禁用");
    FpComboAdd(w->ctl[F_WEBRTC - F_BASE], L"disable_udp - 禁用UDP");
    FpMkLabel(p, w, F_TZM, L"时区模式", 12, 144, 80);
    FpMkCombo(p, w, F_TZM, 100, 142, 150);
    FpComboAdd(w->ctl[F_TZM - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_TZM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_TZ, 260, 142, 260);
    for (auto t : kTz) FpComboAdd(w->ctl[F_TZ - F_BASE], t);
    FpMkLabel(p, w, F_GEOM, L"地理", 12, 178, 80);
    FpMkCombo(p, w, F_GEOM, 100, 176, 150);
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"ask - 询问");
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"allow - 允许");
    FpComboAdd(w->ctl[F_GEOM - F_BASE], L"block - 禁止");
    FpMkCombo(p, w, F_GEOIP, 260, 176, 150);
    FpComboAdd(w->ctl[F_GEOIP - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_GEOIP - F_BASE], L"custom - 自定义");
    FpMkLabel(p, w, F_LAT, L"纬/经/精度", 12, 212, 80);
    FpMkEdit(p, w, F_LAT, 100, 210, 120);
    FpMkEdit(p, w, F_LNG, 230, 210, 120);
    FpMkEdit(p, w, F_ACC, 360, 210, 100);
    FpMkLabel(p, w, F_LANGM, L"语言模式", 12, 246, 80);
    FpMkCombo(p, w, F_LANGM, 100, 244, 150);
    FpComboAdd(w->ctl[F_LANGM - F_BASE], L"ip - 基于IP");
    FpComboAdd(w->ctl[F_LANGM - F_BASE], L"custom - 自定义");
    FpMkEdit(p, w, F_LANGLIST, 260, 244, 200);
    FpMkLabel(p, w, F_UILANG, L"界面语言", 12, 280, 80);
    FpMkCombo(p, w, F_UILANG, 100, 278, 180);
    FpComboAdd(w->ctl[F_UILANG - F_BASE], L"follow_lang - 基于语言");
    FpComboAdd(w->ctl[F_UILANG - F_BASE], L"custom - 自定义");
    FpMkEdit(p, w, F_PAGELANG, 290, 278, 170);
    // ---- 页2 硬件指纹：分辨率 + 字体 + 噪音开关 + 媒体 + WebGL/WebGPU ----
    p = panels[2];
    FpMkLabel(p, w, F_RESM, L"分辨率", 12, 12, 80);
    FpMkCombo(p, w, F_RESM, 100, 10, 140);
    FpComboAdd(w->ctl[F_RESM - F_BASE], L"preset - 预定义");
    FpComboAdd(w->ctl[F_RESM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_RES, 250, 10, 170);
    FpComboAdd(w->ctl[F_RES - F_BASE], L"none");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1920_1080");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"2560_1440");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1440_900");
    FpComboAdd(w->ctl[F_RES - F_BASE], L"1366_768");
    FpMkEdit(p, w, F_RESW, 430, 10, 70);
    FpMkEdit(p, w, F_RESH, 510, 10, 70);
    FpMkLabel(p, w, F_FONTM, L"字体", 12, 46, 80);
    FpMkCombo(p, w, F_FONTM, 100, 44, 150);
    FpComboAdd(w->ctl[F_FONTM - F_BASE], L"all - 默认");
    FpComboAdd(w->ctl[F_FONTM - F_BASE], L"custom - 自定义");
    FpMkBtn(p, w, F_SHUFFLEFONTS, L"换一换", 260, 42, 80);
    FpMkEdit(p, w, F_FONTS, 100, 76, 480, 44);
    FpMkCheck(p, w, F_SWCVS, L"Canvas(=1)", 12, 130, 130);
    FpMkCheck(p, w, F_SWWGL, L"WebGL图像(=1)", 150, 130, 150);
    FpMkCheck(p, w, F_SWAUD, L"Audio(=1)", 310, 130, 120);
    FpMkCheck(p, w, F_SWRECT, L"ClientRects(=1)", 440, 130, 150);
    FpMkCheck(p, w, F_SWSPEECH, L"Speech(=1)", 12, 156, 130);
    FpMkLabel(p, w, F_MEDIA, L"媒体设备", 160, 156, 80);
    FpMkCombo(p, w, F_MEDIA, 240, 154, 140);
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"0 - 真实/关闭");
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"1 - 随机");
    FpComboAdd(w->ctl[F_MEDIA - F_BASE], L"2 - 自定义");
    FpMkEdit(p, w, F_MIN, 390, 154, 50);
    FpMkEdit(p, w, F_MVID, 450, 154, 50);
    FpMkEdit(p, w, F_MOUT, 510, 154, 50);
    FpMkLabel(p, w, F_WGLM, L"WebGL元数据", 12, 190, 90);
    FpMkCombo(p, w, F_WGLM, 110, 188, 150);
    FpComboAdd(w->ctl[F_WGLM - F_BASE], L"real - 真实(0)");
    FpComboAdd(w->ctl[F_WGLM - F_BASE], L"custom - 自定义(2)");
    FpMkCombo(p, w, F_VENDOR, 270, 188, 200);
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (Intel)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (NVIDIA)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Google Inc. (AMD)");
    FpComboAdd(w->ctl[F_VENDOR - F_BASE], L"Apple Inc.");
    FpMkEdit(p, w, F_RENDERER, 110, 220, 360);
    FpMkBtn(p, w, F_SHUFFLERDR, L"随机", 480, 218, 70);
    FpMkLabel(p, w, F_WGPU, L"WebGPU", 12, 254, 80);
    FpMkCombo(p, w, F_WGPU, 110, 252, 200);
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"follow_webgl - 跟随(1)");
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"disabled - 禁用(0)");
    FpComboAdd(w->ctl[F_WGPU - F_BASE], L"custom - 自定义适配器(2)");
    FpMkEdit(p, w, F_GVENDOR, 320, 252, 120);
    FpMkEdit(p, w, F_GARCH, 450, 252, 120);
    // ---- 页3 设备伪装：CPU/RAM/设备名/MAC ----
    p = panels[3];
    FpMkLabel(p, w, F_CPUM, L"CPU模式", 12, 12, 80);
    FpMkCombo(p, w, F_CPUM, 100, 10, 150);
    FpComboAdd(w->ctl[F_CPUM - F_BASE], L"real - 真实");
    FpComboAdd(w->ctl[F_CPUM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_CPU, 260, 10, 150);
    for (auto c : { L"default", L"2", L"4", L"6", L"8", L"10", L"12", L"16", L"20", L"24" }) FpComboAdd(w->ctl[F_CPU - F_BASE], c);
    FpMkLabel(p, w, F_RAMM, L"RAM模式", 12, 46, 80);
    FpMkCombo(p, w, F_RAMM, 100, 44, 150);
    FpComboAdd(w->ctl[F_RAMM - F_BASE], L"real - 真实");
    FpComboAdd(w->ctl[F_RAMM - F_BASE], L"custom - 自定义");
    FpMkCombo(p, w, F_RAM, 260, 44, 150);
    for (auto c : { L"default", L"2", L"4", L"6", L"8", L"16", L"32", L"64", L"128" }) FpComboAdd(w->ctl[F_RAM - F_BASE], c);
    FpMkLabel(p, w, F_DEVM, L"设备名", 12, 80, 80);
    FpMkCombo(p, w, F_DEVM, 100, 78, 150);
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"off - 关闭(0)");
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"random - 随机(1)");
    FpComboAdd(w->ctl[F_DEVM - F_BASE], L"custom - 自定义(2)");
    FpMkEdit(p, w, F_DEVNAME, 260, 78, 200);
    FpMkBtn(p, w, F_SHUFFLEDEV, L"随机", 470, 76, 70);
    FpMkLabel(p, w, F_MACM, L"MAC", 12, 114, 80);
    FpMkCombo(p, w, F_MACM, 100, 112, 150);
    FpComboAdd(w->ctl[F_MACM - F_BASE], L"off - 关闭(0)");
    FpComboAdd(w->ctl[F_MACM - F_BASE], L"custom - 自定义(2)");
    FpMkEdit(p, w, F_MAC, 260, 112, 200);
    FpMkBtn(p, w, F_SHUFFLEMAC, L"随机", 470, 110, 70);
    // ---- 页4 高级：DNT/端口/加速/TLS/启动参数 ----
    p = panels[4];
    FpMkLabel(p, w, F_DNT, L"DoNotTrack", 12, 12, 90);
    FpMkCombo(p, w, F_DNT, 110, 10, 170);
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"open - 开启");
    FpComboAdd(w->ctl[F_DNT - F_BASE], L"close - 关闭");
    FpMkLabel(p, w, F_PORTSCAN, L"端口扫描", 300, 12, 80);
    FpMkCombo(p, w, F_PORTSCAN, 380, 10, 150);
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"open - 启用(1)");
    FpComboAdd(w->ctl[F_PORTSCAN - F_BASE], L"close - 关闭(0)");
    FpMkLabel(p, w, F_WPORTS, L"白名单端口", 12, 46, 90);
    FpMkEdit(p, w, F_WPORTS, 110, 44, 420);
    FpMkLabel(p, w, F_HWACC, L"硬件加速", 12, 80, 90);
    FpMkCombo(p, w, F_HWACC, 110, 78, 170);
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"default - 默认");
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"open - 开启");
    FpComboAdd(w->ctl[F_HWACC - F_BASE], L"close - 关闭");
    FpMkLabel(p, w, F_TLSM, L"TLS", 300, 80, 60);
    FpMkCombo(p, w, F_TLSM, 380, 78, 150);
    FpComboAdd(w->ctl[F_TLSM - F_BASE], L"close - 默认");
    FpComboAdd(w->ctl[F_TLSM - F_BASE], L"open - 自定义(1)");
    FpMkEdit(p, w, F_TLS, 110, 112, 420);
    FpMkLabel(p, w, F_ARGS, L"启动参数", 12, 146, 90);
    FpMkEdit(p, w, F_ARGS, 110, 144, 420, 100);
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
    static HWND sPanels[5];
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        w = (FpWnd*)cs->lpCreateParams;
        ::SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)w);
        w->hDlg = h;
        HINSTANCE hi = cs->hInstance;
        ::InitCommonControls();
        w->hTab = ::CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            8, 8, 760, 500, h, (HMENU)(INT_PTR)F_TAB, hi, NULL);
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        for (int i = 0; i < 5; i++) {
            ti.pszText = (LPWSTR)kTabNames[i];
            ::SendMessageW(w->hTab, TCM_INSERTITEMW, i, (LPARAM)&ti);
            sPanels[i] = FpMkPanel(w->hTab, hi);
        }
        FpBuildPages(w, sPanels, hi);
        // 底部按钮
        FpMkBtn(h, w, F_RANDOM, L"一键随机", 8, 516, 100);
        FpMkBtn(h, w, F_OK, L"保存", 560, 516, 100);
        FpMkBtn(h, w, F_CANCEL, L"取消", 668, 516, 100);
        w->hStatus = ::CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 120, 520, 430, 20, h, (HMENU)(INT_PTR)F_STATUS, hi, NULL);
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
        for (int i = 1; i < 5; i++) ::ShowWindow(sPanels[i], SW_HIDE);
        return 0;
    }
    if (!w) return ::DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->idFrom == F_TAB && nm->code == TCN_SELCHANGE) {
            int sel = (int)::SendMessageW(w->hTab, TCM_GETCURSEL, 0, 0);
            for (int i = 0; i < 5; i++) ::ShowWindow(sPanels[i], i == sel ? SW_SHOW : SW_HIDE);
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        auto C = [&](int c) { return w->ctl[c - F_BASE]; };
        if (id == F_CANCEL) { ::DestroyWindow(h); return 0; }
        if (id == F_SHUFFLEUA) {
            FpCollect(w);
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
            ::SetWindowTextW(w->hStatus, L"Cookie 已为 JSON 数组形态（保存时清洗后写入）");
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
            wchar_t la[32], ln[32];
            swprintf_s(la, L"%.4f", (rand() % 8000 - 4000) / 100.0);
            swprintf_s(ln, L"%.4f", (rand() % 36000 - 18000) / 100.0);
            FpSet(C(F_LAT), la); FpSet(C(F_LNG), ln);
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
                if (w->form.hardwareAccel == L"close") { ui = FpJsonSet(ui, "gpu", "\"2\""); }
                else if (w->form.hardwareAccel == L"open") {
                    ui = FpJsonSet(ui, "gpu", "\"0\"");
                    ui = FpJsonSet(ui, "gpuSwitch", "\"1\"");
                }
                ui = FpJsonSet(ui, "tlsSwitch", (w->form.disableTls == L"open") ? "\"1\"" : "\"0\"");
                if (w->form.disableTls == L"open") ui = FpJsonSet(ui, "tls", "\"" + N(w->form.tlsBlacklist) + "\"");
                bool oku = FpSaveUiExtra(dd, ui);
                (void)oku;
            }
            // 2. cookies 清洗后写三件套（CLIENT_HOST 剥离、BROWSER_ID 校正逻辑已在载入时处理，此处直接写）
            bool okc = true;
            if (!w->form.cookie.empty()) {
                std::string ck = N(w->form.cookie);
                // 非 JSON 则跳过 cookies 写盘（与 web-ui 一致）
                if (!ck.empty() && ck.front() == '[') okc = FpSaveCookiesJson(dd, ck);
            }
            // 3. fingerprint_config 供参考：如 static 缺失则按表单建最小 static（含保护键回填见 fp/save）
            (void)okc;
            w->saved = true;
            ::SetWindowTextW(w->hStatus, L"已保存（ui 存档 + cookies）");
            ::DestroyWindow(h);
            return 0;
        }
        return 0;
    }
    case WM_DESTROY:
        ::PostQuitMessage(0);
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
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    ::RegisterClassW(&wc);
    std::wstring title = L"指纹配置 - " + profileName;
    HWND h = ::CreateWindowW(cls, title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 792, 600,
        hParent, NULL, wc.hInstance, &w);
    if (!h) return false;
    ::ShowWindow(h, SW_SHOW);
    ::UpdateWindow(h);
    // 模态循环：只处理本窗口消息，父窗口禁用
    ::EnableWindow(hParent, FALSE);
    MSG m{};
    while (::GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_QUIT) break;
        ::TranslateMessage(&m);
        ::DispatchMessageW(&m);
        if (!::IsWindow(h)) break;
    }
    ::EnableWindow(hParent, TRUE);
    ::SetForegroundWindow(hParent);
    ::UnregisterClassW(cls, wc.hInstance);
    return w.saved;
}

