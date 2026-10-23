#pragma once

#include "singbox_config.h"
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

static constexpr UINT kSbRateUpdateMessage = WM_APP + 37;

struct SingBoxInstance {
    HANDLE process = NULL;
    DWORD pid = 0;
    uint16_t socksPort = 0;
    uint16_t apiPort = 0;
    std::wstring profileDir;
    std::wstring runtimeConfigPath;
    std::string apiSecret;
    HWND notifyWindow = NULL;
    std::atomic<bool> stopping{ false };
    std::atomic<unsigned long long> uploadBytesPerSecond{ 0 };
    std::atomic<unsigned long long> downloadBytesPerSecond{ 0 };
    std::thread statsThread;
    std::mutex webSocketMutex;
    void* activeWebSocket = nullptr;
};

// Reads <profileDir>\config.json (UTF-8) and validates its SOCKS-compatible inbound.
bool SbReadProfileConfig(const std::wstring& profileDir, SingBoxConfigInfo& info,
    std::string& error);

// Starts a hidden per-profile sing-box process and its traffic statistics worker.
bool SbStartInstance(const std::wstring& appDir, const std::wstring& profileDir,
    HWND notifyWindow, std::unique_ptr<SingBoxInstance>& instance, std::wstring& error);

// Stops and joins only this profile's instance, then removes its generated config.
void SbStopInstance(std::unique_ptr<SingBoxInstance>& instance);
