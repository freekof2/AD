#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <cwctype>

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

inline std::string FpResolveGeolocationSetting(const std::string& uiMode,
    const std::string& staticMode, const std::string& legacyLocation) {
    auto supported = [](const std::string& value) {
        return value == "ask" || value == "allow" || value == "block";
    };
    if (supported(uiMode)) return uiMode;
    if (supported(staticMode)) return staticMode;
    if (supported(legacyLocation)) return legacyLocation;
    return "";
}

inline std::string FpTrimAsciiWhitespace(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline bool FpParseFiniteNumber(const std::string& value, double& result) {
    const std::string trimmed = FpTrimAsciiWhitespace(value);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
    result = parsed;
    return true;
}

// Builds the native Geoposition value from the profile's local latitude,
// longitude and accuracy fields. The custom parameter is a comma-separated
// string because that is the format consumed by the browser kernel.
inline std::string FpBuildGeopositionValue(const std::string& latitude,
    const std::string& longitude, const std::string& accuracy) {
    const std::string lat = FpTrimAsciiWhitespace(latitude);
    const std::string lon = FpTrimAsciiWhitespace(longitude);
    const std::string acc = FpTrimAsciiWhitespace(accuracy);
    double latValue = 0.0, lonValue = 0.0, accuracyValue = 0.0;
    if (!FpParseFiniteNumber(lat, latValue) || !FpParseFiniteNumber(lon, lonValue) ||
        !FpParseFiniteNumber(acc, accuracyValue)) return "";
    if (latValue < -90.0 || latValue > 90.0 || lonValue < -180.0 ||
        lonValue > 180.0 || accuracyValue < 0.0) return "";
    return lat + "," + lon + "," + acc;
}

inline std::string FpNormalizeGeoposition(const std::string& value) {
    const size_t first = value.find(',');
    if (first == std::string::npos) return "";
    const size_t second = value.find(',', first + 1);
    if (second == std::string::npos || value.find(',', second + 1) != std::string::npos)
        return "";
    return FpBuildGeopositionValue(value.substr(0, first),
        value.substr(first + 1, second - first - 1), value.substr(second + 1));
}

inline std::string FpResolveGeoposition(const std::string& uiLatitude,
    const std::string& uiLongitude, const std::string& uiAccuracy,
    const std::string& dynamicGeoposition, const std::string& staticGeoposition) {
    std::string value = FpBuildGeopositionValue(uiLatitude, uiLongitude, uiAccuracy);
    if (!value.empty()) return value;
    value = FpNormalizeGeoposition(dynamicGeoposition);
    if (!value.empty()) return value;
    return FpNormalizeGeoposition(staticGeoposition);
}

inline std::wstring FpQuoteWindowsCommandLineArg(const std::wstring& arg) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(ch);
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

inline std::wstring FpBuildUserAgentArg(const std::wstring& userAgent) {
    size_t first = 0;
    while (first < userAgent.size() && std::iswspace(userAgent[first])) ++first;
    size_t last = userAgent.size();
    while (last > first && std::iswspace(userAgent[last - 1])) --last;
    if (first == last) return L"";
    return FpQuoteWindowsCommandLineArg(L"--user-agent=" + userAgent.substr(first, last - first));
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
inline std::string FpBuildEmptyMediaDevicesJson() {
    return "[{\"kind\":\"audioinput\",\"label\":\"\"},"
           "{\"kind\":\"videoinput\",\"label\":\"\"},"
           "{\"kind\":\"audiooutput\",\"label\":\"\"}]";
}
// Initial offline profile-save fallback; the launcher refreshes this value from
// the local system RNG on each enabled mobile Chrome launch to match the
// official per-launch randomization.
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

inline bool FpShouldUseDeviceOrientationRanges(int kernelVersion, int browserBuildDate) {
    return kernelVersion >= 148 && browserBuildDate > 20260701;
}

inline std::string FpBuildDeviceOrientationStaticJson(double alpha, double beta, double gamma) {
    auto number = [](double value) {
        if (value == 0.0) value = 0.0; // avoid serializing negative zero
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(1) << value;
        return out.str();
    };
    return "{\"alpha\":" + number(alpha) + ",\"beta\":" + number(beta) +
        ",\"gamma\":" + number(gamma) + ",\"absolute\":false}";
}

inline std::string FpBuildDeviceOrientationRangeJson(double alphaA, double alphaB,
    double betaA, double betaB, double gammaA, double gammaB) {
    auto range = [](double a, double b) {
        if (b < a) { const double temp = a; a = b; b = temp; }
        auto number = [](double value) {
            if (value == 0.0) value = 0.0;
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::fixed << std::setprecision(1) << value;
            return out.str();
        };
        return "[" + number(a) + "," + number(b) + "]";
    };
    return "{\"alpha\":" + range(alphaA, alphaB) + ",\"beta\":" +
        range(betaA, betaB) + ",\"gamma\":" + range(gammaA, gammaB) +
        ",\"absolute\":false}";
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

// ---- WebRTC：official setWebRTC 只在 forward 写的 STUN/TURN（main.min.js 133639 常量）----
// 两个键同值且都是 stun:（不是 turn:），所以「转发」不是中继，只是显式挂公网 STUN。
inline const char* FpWebRtcStunServer() { return "stun:stun.l.google.com:19302"; }

// ---- WebRTC：disable_udp 的官方二选一（main.min.js setWebRTC 59832）----
// 官方条件：chrome 内核 && kernel>=145 && browserVersion>=20260422 && proxyType==socks5
//   命中 -> staticConfig.WebRTCSocks5UdpProxy{host,port,account,password}（UDP 走 SOCKS5，不禁用）
//   未命中 -> args += --webrtc-ip-handling-policy=disable_non_proxied_udp
//            且 staticConfig.command_line["webrtc-ip-handling-policy"]=同值
// browserVersion 是 AdsPower 的发布日期：chrome_152/update_version_key = 20260831。
inline int FpBrowserKernelFromDirLeaf(const std::string& leaf) {
    // "chrome_152" -> 152；"flower_100" -> 100；其它返回 0
    size_t p = leaf.rfind('_');
    if (p == std::string::npos || p + 1 >= leaf.size()) return 0;
    int v = 0;
    bool any = false;
    for (size_t i = p + 1; i < leaf.size(); i++) {
        if (leaf[i] < '0' || leaf[i] > '9') return 0;
        v = v * 10 + (leaf[i] - '0');
        any = true;
        if (v > 9999) return 0;
    }
    return any ? v : 0;
}
inline bool FpUdpSocks5PathApplies(bool isChrome, int kernelVersion, int browserBuildDate) {
    return isChrome && kernelVersion >= 145 && browserBuildDate >= 20260422;
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

// AdsPower's setMediaDevices is a no-op for mode 0 and uses an existing
// profile-generated list when one is present for modes 1/2. Keep that local
// preset intact; only fall back to the offline fbcc generator when missing.
inline std::string FpSelectMediaDevicesPreset(const std::string& mode,
    const std::string& cachedJson, const std::string& generatedJson) {
    if (mode == "0") return cachedJson;
    if (!cachedJson.empty() && cachedJson != "[]" && cachedJson != "\"\"")
        return cachedJson;
    return generatedJson;
}
