#pragma once

#include <cstdint>
#include <string>

// sing-box config.json subset used by SunLauncher. The original config is never rewritten.
struct SingBoxConfigInfo {
    std::string inboundType;
    std::string inboundListen;
    std::string controller;
    std::string apiSecret;
    std::string cachePath;
    uint16_t listenPort = 0;
    bool cacheEnabled = false;
};

// Accepts exactly one SOCKS-compatible inbound ("socks" or "mixed").
bool SbParseConfig(const std::string& json, SingBoxConfigInfo& info, std::string& error);

// Makes a per-profile runtime copy with loopback-only listeners and isolated ports/cache.
bool SbBuildRuntimeConfig(const std::string& sourceJson, uint16_t listenPort,
    uint16_t apiPort, const std::string& cachePath, const std::string& apiSecret,
    std::string& runtimeJson, std::string& error);
