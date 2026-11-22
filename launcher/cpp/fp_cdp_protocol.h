#pragma once

#include "fp_browser_config.h"

#include <charconv>
#include <system_error>
#include <string>

// Return a canonical HTTP(S) origin or an empty string for opaque/invalid URLs.
inline std::string FpCdpOriginFromUrl(const std::string& url) {
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return "";

    std::string scheme = url.substr(0, schemeEnd);
    for (char& c : scheme) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (scheme != "http" && scheme != "https") return "";

    const size_t authorityStart = schemeEnd + 3;
    const size_t authorityEnd = url.find_first_of("/?#", authorityStart);
    const std::string authority = url.substr(authorityStart,
        authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        authority.find('\\') != std::string::npos) return "";
    for (unsigned char c : authority) {
        if (c <= 0x20 || c == 0x7f) return "";
    }

    std::string host;
    std::string portText;
    if (authority[0] == '[') {
        const size_t close = authority.find(']');
        if (close == std::string::npos || close == 1) return "";
        host = authority.substr(0, close + 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return "";
            portText = authority.substr(close + 2);
        }
        for (size_t i = 1; i < host.size() - 1; ++i) {
            char& hostChar = host[i];
            if (hostChar >= 'A' && hostChar <= 'Z')
                hostChar = static_cast<char>(hostChar - 'A' + 'a');
            const char c = hostChar;
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F');
            if (!hex && c != ':' && c != '.') return "";
        }
    } else {
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            if (authority.find(':') != colon) return ""; // IPv6 must be bracketed.
            host = authority.substr(0, colon);
            portText = authority.substr(colon + 1);
        } else {
            host = authority;
        }
        if (host.empty()) return "";
        for (char& c : host) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '-';
            if (!valid) return "";
        }
    }

    unsigned port = 0;
    if (!portText.empty()) {
        for (char c : portText) {
            if (c < '0' || c > '9') return "";
            const unsigned digit = static_cast<unsigned>(c - '0');
            if (port > (65535U - digit) / 10U) return "";
            port = port * 10U + digit;
        }
        if (port == 0) return "";
    } else if (authority.back() == ':') {
        return "";
    }

    std::string origin = scheme + "://" + host;
    const unsigned defaultPort = scheme == "https" ? 443 : 80;
    if (port != 0 && port != defaultPort) origin += ":" + std::to_string(port);
    return origin;
}

// Only accept loopback WebSocket URLs associated with this profile's CDP port.
inline std::string FpCdpWebSocketPath(const std::string& url, int expectedPort) {
    static const std::string prefix = "ws://";
    if (expectedPort <= 0 || url.compare(0, prefix.size(), prefix) != 0) return "";
    const size_t authorityStart = prefix.size();
    const size_t pathStart = url.find('/', authorityStart);
    if (pathStart == std::string::npos) return "";
    const std::string authority = url.substr(authorityStart, pathStart - authorityStart);
    if (authority.find('@') != std::string::npos) return "";

    const size_t colon = authority.rfind(':');
    if (colon == std::string::npos || colon + 1 == authority.size()) return "";
    std::string host = authority.substr(0, colon);
    unsigned port = 0;
    for (size_t i = colon + 1; i < authority.size(); ++i) {
        const char c = authority[i];
        if (c < '0' || c > '9') return "";
        const unsigned digit = static_cast<unsigned>(c - '0');
        if (port > (65535U - digit) / 10U) return "";
        port = port * 10U + digit;
    }
    if (port != static_cast<unsigned>(expectedPort)) return "";
    if (host != "127.0.0.1" && host != "localhost" && host != "[::1]") return "";

    const std::string path = url.substr(pathStart);
    if (path.compare(0, 10, "/devtools/") != 0 || path.find('#') != std::string::npos)
        return "";
    return path;
}

inline std::string FpCdpPermissionSettingForMode(const std::string& mode) {
    if (mode == "allow") return "granted";
    if (mode == "block") return "denied";
    // Do not use Browser.setPermission("prompt") for ask: DevTools' ASK
    // override bypasses Chromium's normal permission-request UI path.
    return "";
}

inline std::string FpCdpBuildPermissionCommand(int id, const std::string& origin,
    const std::string& setting) {
    if (id <= 0 || origin.empty() || FpCdpOriginFromUrl(origin) != origin ||
        (setting != "granted" && setting != "denied")) return "";
    return "{\"id\":" + std::to_string(id) +
        ",\"method\":\"Browser.setPermission\",\"params\":{" +
        "\"permission\":{\"name\":\"geolocation\"}," +
        "\"setting\":" + FpBrowserConfigJsonQuote(setting) +
        ",\"origin\":" + FpBrowserConfigJsonQuote(origin) + "}}";
}

inline bool FpCdpJsonNumber(const std::string& value, std::string& result) {
    double parsed = 0.0;
    if (!FpParseFiniteNumber(value, parsed)) return false;
    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), parsed,
        std::chars_format::general);
    if (converted.ec != std::errc()) return false;
    result.assign(buffer, converted.ptr);
    return true;
}

inline std::string FpCdpBuildGeolocationCommand(int id, const std::string& geoposition) {
    if (id <= 0) return "";
    const std::string normalized = FpNormalizeGeoposition(geoposition);
    if (normalized.empty()) return "";
    const size_t first = normalized.find(',');
    const size_t second = normalized.find(',', first + 1);
    std::string latitude, longitude, accuracy;
    if (!FpCdpJsonNumber(normalized.substr(0, first), latitude) ||
        !FpCdpJsonNumber(normalized.substr(first + 1, second - first - 1), longitude) ||
        !FpCdpJsonNumber(normalized.substr(second + 1), accuracy)) return "";
    return "{\"id\":" + std::to_string(id) +
        ",\"method\":\"Emulation.setGeolocationOverride\",\"params\":{" +
        "\"latitude\":" + latitude +
        ",\"longitude\":" + longitude +
        ",\"accuracy\":" + accuracy + "}}";
}
