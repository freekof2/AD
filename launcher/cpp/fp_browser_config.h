#pragma once

#include <cctype>
#include <string>

inline std::string FpBrowserConfigJsonQuote(const std::string& value) {
    std::string out = "\"";
    static const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 0x0f];
        } else out += static_cast<char>(c);
    }
    out += '"';
    return out;
}

inline std::string FpNormalizeTimezone(const std::string& value) {
    std::string out;
    bool pendingSpace = false;
    for (unsigned char c : value) {
        if (std::isspace(c)) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) out += '_';
        out += static_cast<char>(c);
        pendingSpace = false;
    }
    return out;
}

inline std::string FpBuildTimeZoneSunParam(const std::string& timezone) {
    const std::string normalized = FpNormalizeTimezone(timezone);
    if (normalized.empty()) return "";
    return "\"TimeZone\":" + FpBrowserConfigJsonQuote(normalized);
}

inline std::string FpBrowserPlatformTag(const std::string& platform) {
    if (platform == "iPhone") return "iPhone";
    if (platform == "Linux armv7I" || platform == "Linux armv8I" || platform == "Linux armv81")
        return "Android";
    if (platform == "MacIntel") return "MacOS";
    return "Other";
}

inline std::string FpBuildWebGlConfigJson(const std::string& vendor,
    const std::string& renderer, const std::string& webgpuSwitch,
    const std::string& gpuVendor, const std::string& gpuArchitecture) {
    if (vendor.empty() || renderer.empty()) return "";
    std::string json = "{\"UNMASKED_VENDOR_WEBGL\":" + FpBrowserConfigJsonQuote(vendor) +
        ",\"UNMASKED_RENDERER_WEBGL\":" + FpBrowserConfigJsonQuote(renderer);
    if (webgpuSwitch == "1" && !gpuVendor.empty()) {
        json += ",\"GPUAdapterInfo\":{\"vendor\":" + FpBrowserConfigJsonQuote(gpuVendor);
        if (!gpuArchitecture.empty())
            json += ",\"architecture\":" + FpBrowserConfigJsonQuote(gpuArchitecture);
        json += "}";
    }
    json += ",\"SUPPORTED_EXTENSIONS\":[]}";
    return json;
}

struct FpImportSourceResolution {
    std::wstring profilePath;
    bool directProfilePath = false;
};

inline std::wstring FpImportTrimDirectory(std::wstring path) {
    size_t first = path.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    path = path.substr(first, path.find_last_not_of(L" \t\r\n") - first + 1);
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    return path;
}

// The A2 field accepts either a cache parent (append currentProfile) or an actual
// profile directory (use it directly). Never infer profile-ness from underscores:
// ordinary parent directories such as "user_cache" also contain underscores.
inline FpImportSourceResolution FpResolveImportSourcePath(const std::wstring& raw,
    const std::wstring& currentProfile, bool directoryExists, bool hasFingerprintFiles) {
    const std::wstring path = FpImportTrimDirectory(raw);
    FpImportSourceResolution out;
    if (path.empty()) return out;
    size_t slash = path.find_last_of(L"\\/");
    const std::wstring leaf = slash == std::wstring::npos ? path : path.substr(slash + 1);
    if (directoryExists && (hasFingerprintFiles || leaf == currentProfile)) {
        out.profilePath = path;
        out.directProfilePath = true;
        return out;
    }
    out.profilePath = path + L"\\" + currentProfile;
    return out;
}

// ==== 指纹“系统”字段 + 噪声开关（读 / 存 / 应用共用，纯字符串函数便于单测） ====
// OS 取值与指纹页 F_OS 一致：win | mac | linux | android | ios。
inline bool FpOsIsMobile(const std::string& os) { return os == "android" || os == "ios"; }
inline bool FpOsIsAndroid(const std::string& os) { return os == "android"; }
inline bool FpOsIsIos(const std::string& os) { return os == "ios"; }
// official setFlash：t.flash && "linux" !== process.platform -> PepperFlash 只在桌面 Chromium 存在
inline bool FpOsSupportsFlash(const std::string& os) { return os == "win" || os == "mac"; }
// official setMaxTouchPoints / setGyroscope / setNetworkInformationType 都限定移动平台
inline bool FpOsSupportsMobileExtras(const std::string& os) { return FpOsIsMobile(os); }

// official setMaxTouchPoints -> sunBrowserParams.Platform = initBrowser.platform
// 取值集（main.min.js setFakeFonts/setMediaDevices + asar 侧注释）：
//   Win32 | MacIntel | Linux x86_64 | Linux armv7I/armv8I/armv81 | Linux i686 | iPhone | Windows Phone
inline std::string FpOsToOfficialPlatform(const std::string& os) {
    if (os == "mac") return "MacIntel";
    if (os == "linux") return "Linux x86_64";
    if (os == "android") return "Linux armv8I";
    if (os == "ios") return "iPhone";
    return "Win32";
}
// official clientHints.platform 判定集（main.min.js configureNavigatorEmulation 的 a=clientHints.platform）：
//   Windows | macOS | Android | iPhone | Linux
inline std::string FpOsToChPlatform(const std::string& os) {
    if (os == "mac") return "macOS";
    if (os == "linux") return "Linux";
    if (os == "android") return "Android";
    if (os == "ios") return "iPhone";
    return "Windows";
}
// CH架构：Chrome UA-CH architecture 只有 x86 / arm（位数走 bitness）
inline std::string FpOsToChArchitecture(const std::string& os) {
    return (os == "android" || os == "ios") ? "arm" : "x86";
}
// CH版本（platformVersion）：与指纹页 UA 预设的系统版本同源（补到三段；Linux 官方留空）
inline std::string FpOsToChPlatformVersion(const std::string& os) {
    if (os == "mac") return "10.15.7";    // UA: Intel Mac OS X 10_15_7
    if (os == "android") return "14.0.0"; // UA: Android 14
    if (os == "ios") return "17.4.0";     // UA: iPhone OS 17_4
    if (os == "linux") return "";         // official: platform_version 缺省即 ""
    return "10.0.0";                      // UA: Windows NT 10.0
}
// CH机型：仅 Android 暴露，且必须与 UA 里的机型一致；桌面/iOS 官方 model 为空
inline std::string FpOsToChModel(const std::string& os) {
    if (os == "android") return "Pixel 8"; // UA: Android 14; Pixel 8
    return "";
}
// official setClientHints 的 mobile 是布尔（"1"===t.mobile）
inline std::string FpOsToChMobile(const std::string& os) {
    return (os == "android" || os == "ios") ? "1" : "0";
}
// 存档里的 CH 平台是否与当前“系统”一致（不一致/为空 -> 按系统重算）
inline bool FpChMatchesOs(const std::string& os, const std::string& chPlatform) {
    return !chPlatform.empty() && chPlatform == FpOsToChPlatform(os);
}

// 四个噪声开关统一真值：ui 侧车 > static 低位键 > official 种子存在性。
// official setCanvasAndWebGL/setAudio/setClientRects 只在开关=1 时写
// CanvasMark/WebGLMark/AudioFp/ClientRectFp，因此“种子存在”即代表开启。
// uiRaw/staticRaw 传入 FpJsonGet 的原始片段（含引号或裸值），此处统一去引号。
inline std::string FpResolveNoiseSwitch(const std::string& uiRaw,
    const std::string& staticRaw, bool markPresent) {
    auto val = [](const std::string& raw) -> std::string {
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
            return raw.substr(1, raw.size() - 2);
        return raw;
    };
    const std::string u = val(uiRaw), s = val(staticRaw);
    if (u == "1" || u == "0") return u;
    if (s == "1" || s == "0") return s;
    return markPresent ? "1" : "0";
}

// ---- NetworkInformationType（official normalize/get/apply 全文移植） ----
inline std::string FpNormalizeNetworkInformationType(const std::string& v) {
    std::string s;
    for (unsigned char c : v) if (!std::isspace(c)) s += static_cast<char>(c);
    if (s == "1") return "1";
    if (s == "2") return "2";
    return "0";
}
inline std::string FpNetworkChromeType(const std::string& v) {
    const std::string n = FpNormalizeNetworkInformationType(v);
    if (n == "1") return "wifi";
    if (n == "2") return "cellular";
    return "";
}
inline std::string FpBuildNetworkInformationStatic(const std::string& v) {
    const std::string type = FpNetworkChromeType(v);
    if (type.empty()) return "";
    return "{\"enabled\":true,\"type\":" + FpBrowserConfigJsonQuote(type) + "}";
}
// 返回 command_line 里单个键的 JSON 值（带引号），空串表示该键应被移除
inline std::string FpBuildAndroidBlinkFeatureValue() {
    return "\"NetworkInformation,NetInfoDownlinkMax\"";
}
inline std::string FpBuildIosBlinkFeatureValue() {
    return "\"BatteryStatus,WebBluetooth,NetworkInformation,NetInfoDownlinkMax,"
           "WebkitTemporaryStorage,WebkitPersistentStorage\"";
}

// ---- Gyroscope（official setGyroscope：仅 Android/iPhone + chrome 内核写 static） ----
inline std::string FpBuildGyroscopeStaticJson() {
    return "{\"x\":[-0.15,0.15],\"y\":[-0.15,0.15],\"z\":[-0.15,0.15]}";
}
inline std::string FpBuildDeviceMotionStaticJson() {
    return "{\"acceleration\":{\"x\":[-0.05,0.05],\"y\":[-0.05,0.05],\"z\":[-0.05,0.05]},"
           "\"accelerationIncludingGravity\":{\"x\":[-0.2,0.2],\"y\":[-0.2,0.2],"
           "\"z\":[9.78,9.81]}}";
}
// official deviceorientationdata 每次启动随机 alpha/beta/gamma；离线用 fbcc 种子做确定性值，
// 避免全 0 被检测为注入。
inline std::string FpBuildDeviceOrientationStaticJson(const std::string& fbcc) {
    unsigned h = 2166136261u;
    for (unsigned char c : fbcc) { h ^= c; h *= 16777619u; }
    const int alpha = (int)(h % 360u);
    const int beta = (int)((h >> 8) % 21u) - 10;
    const int gamma = (int)((h >> 16) % 11u) - 5;
    return "{\"alpha\":" + std::to_string(alpha) +
        ",\"beta\":" + std::to_string(beta) +
        ",\"gamma\":" + std::to_string(gamma) + ",\"absolute\":false}";
}

// ---- ClientHints -> official staticConfig.UserAgentMetadata（setClientHints） ----
inline std::string FpBuildUserAgentMetadataJson(const std::string& platform,
    const std::string& platformVersion, const std::string& architecture,
    const std::string& model, const std::string& mobile /* "0"/"1" */,
    const std::string& bitness, const std::string& wow64) {
    std::string json = "{\"platform\":" + FpBrowserConfigJsonQuote(platform) +
        ",\"platformVersion\":" + FpBrowserConfigJsonQuote(platformVersion) +
        ",\"architecture\":" + FpBrowserConfigJsonQuote(architecture) +
        ",\"model\":" + FpBrowserConfigJsonQuote(model) +
        ",\"mobile\":" + std::string(mobile == "1" ? "true" : "false");
    if (!bitness.empty()) json += std::string(",\"bitness\":") + FpBrowserConfigJsonQuote(bitness);
    if (!wow64.empty()) json += std::string(",\"wow64\":") + (wow64 == "1" ? "true" : "false");
    json += "}";
    return json;
}

// ---- Flash（official setFlash：ext FlashPluginSetting = "allow"|"block"） ----
inline std::string FpBuildFlashSettingSunParam(const std::string& mode) {
    if (mode == "allow") return "\"FlashPluginSetting\":\"allow\"";
    if (mode == "block") return "\"FlashPluginSetting\":\"block\"";
    return ""; // off：官方 t.flash 为空即不注入
}

// ---- MaxTouchPoints：official 对移动端 staticConfig.MaxTouchPoints = 0 ----
inline std::string FpNormalizeMaxTouchPoints(const std::string& v) {
    std::string d;
    for (unsigned char c : v) if (std::isdigit(c)) d += static_cast<char>(c);
    if (d.empty() || d.size() > 3) return "0";
    return d;
}

// fbcc -> 确定性种子（官方 canvasId/clientRectsId/audioId 缺失时的离线回退）
inline int FpSeedFromFbcc(const std::string& fbcc, int min, int max) {
    unsigned h = 2166136261u;
    for (unsigned char c : fbcc) { h ^= static_cast<unsigned>(c); h *= 16777619u; }
    const unsigned span = static_cast<unsigned>(max - min + 1);
    return min + static_cast<int>(h % span);
}

// ---- 噪声开关 -> ext（sunBrowserParams）片段：只在开关=1 时输出 ----
inline std::string FpBuildNoiseSunParams(bool canvas, bool webglImage, bool audio,
    bool clientRects, const std::string& fbcc, const std::string& audioSeed,
    const std::string& clientRectSeed) {
    std::string json = "{";
    bool first = true;
    auto add = [&](const std::string& frag) {
        if (!first) json += ",";
        first = false;
        json += frag;
    };
    if (canvas) add("\"CanvasMark\":" + FpBrowserConfigJsonQuote(fbcc));
    if (webglImage) add("\"WebGLMark\":" + FpBrowserConfigJsonQuote(fbcc));
    if (audio && !audioSeed.empty()) add("\"AudioFp\":" + audioSeed);
    if (clientRects && !clientRectSeed.empty()) add("\"ClientRectFp\":" + clientRectSeed);
    if (first) return "";
    json += "}";
    return json;
}
