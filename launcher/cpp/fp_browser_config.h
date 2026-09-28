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
