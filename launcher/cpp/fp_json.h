#pragma once

#include <string>

// Top-level JSON helpers used by the fingerprint form and its focused tests.
std::string FpJsonGet(const std::string& json, const std::string& key);
std::string FpJsonSet(const std::string& json, const std::string& key,
    const std::string& valueRaw);

struct FpProxyJsonPair {
    std::string uiJson;
    std::string staticJson;
};

// Updates only the persisted proxy fields in ui_fingerprint.json and static ProxyChain.
// sing-box remains the UI mode, but its static ProxyChain scheme is SOCKS5.
bool FpBuildProxySaveJson(const std::string& existingUiJson,
    const std::string& existingStaticJson, const std::string& proxyType,
    const std::string& proxyHost, const std::string& proxyPort,
    const std::string& proxyUser, const std::string& proxyPass,
    FpProxyJsonPair& result, std::string& error);
