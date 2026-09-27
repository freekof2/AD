#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cwctype>
#include <string>

struct FpWebRtcResolution {
    bool disableWebRtc = true;
    bool disableUdp = false;
    bool proxyIpMissing = false;
    bool proxyIpIgnored = false;
    std::wstring address;
};

inline std::wstring FpTrimWebRtcIp(std::wstring ip) {
    size_t first = 0;
    while (first < ip.size() && std::iswspace(ip[first])) first++;
    size_t end = ip.size();
    while (end > first && std::iswspace(ip[end - 1])) end--;
    return ip.substr(first, end - first);
}

inline bool FpIsValidWebRtcIp(const std::wstring& ip) {
    if (ip.empty()) return false;
    IN_ADDR addr4{};
    IN6_ADDR addr6{};
    return ::InetPtonW(AF_INET, ip.c_str(), &addr4) == 1 ||
           ::InetPtonW(AF_INET6, ip.c_str(), &addr6) == 1;
}

inline FpWebRtcResolution FpResolveWebRtc(const std::wstring& mode,
    const std::wstring& proxyIp) {
    FpWebRtcResolution result;
    if (mode == L"forward") {
        result.disableWebRtc = false;
        result.proxyIpIgnored = !FpTrimWebRtcIp(proxyIp).empty();
        return result;
    }
    if (mode == L"disable_udp") {
        result.disableWebRtc = false;
        result.disableUdp = true;
        return result;
    }
    if (mode == L"proxy") {
        result.address = FpTrimWebRtcIp(proxyIp);
        if (FpIsValidWebRtcIp(result.address)) {
            result.disableWebRtc = false;
        } else {
            result.address.clear();
            result.proxyIpMissing = true;
        }
        return result;
    }
    // disabled 及未知值都采用安全默认值：禁用 WebRTC。
    return result;
}

inline std::string FpBuildWebRtcSunParams(const FpWebRtcResolution& rtc) {
    std::string json = "{\"DisableWebRTC\":";
    json += rtc.disableWebRtc ? "true" : "false";
    if (!rtc.address.empty()) {
        json += ",\"WebRTCAddress\":\"";
        for (wchar_t c : rtc.address) {
            if (c < 0x21 || c > 0x7e || c == L'"' || c == L'\\')
                return "{\"DisableWebRTC\":true}";
            json += static_cast<char>(c);
        }
        json += '"';
    }
    json += '}';
    return json;
}
