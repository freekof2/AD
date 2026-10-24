#pragma once

#include <cstdint>
#include <string>

// sing-box config.json subset used by SunLauncher. Only cache_file.path may be normalized
// in the original file; per-instance ports are written to a separate runtime copy.
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

// Rewrites cache_file.path only when its resolved parent is not the directory
// containing configPath. A relative cache path is resolved from that directory.
bool SbNormalizeCacheFilePath(const std::string& json, const std::string& configPath,
    std::string& normalizedJson, std::string& cachePath, bool& changed,
    std::string& error);

// Validate a decimal port and update the only supported SOCKS-compatible inbound.
bool SbUpdateListenPortConfig(const std::string& sourceJson,
    const std::string& requestedPort, std::string& updatedJson,
    uint16_t& portOut, bool& changed, std::string& error);

// Makes a per-profile runtime copy with loopback-only listeners and isolated ports/cache.
bool SbBuildRuntimeConfig(const std::string& sourceJson, uint16_t listenPort,
    uint16_t apiPort, const std::string& cachePath, const std::string& apiSecret,
    std::string& runtimeJson, std::string& error);
