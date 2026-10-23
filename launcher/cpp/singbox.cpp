#include "SunLauncher.h"
#include "singbox.h"
#include "fingerprint.h"

#include <winhttp.h>
#include <objbase.h>
#include <array>
#include <climits>
#include <mutex>

static bool SbEnsureWinsock() {
    static std::once_flag once;
    static bool ready = false;
    std::call_once(once, []() {
        WSADATA data{};
        ready = (::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    });
    return ready;
}

static std::string SbWideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return {};
    std::string result((size_t)bytes, '\0');
    if (!::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], bytes, NULL, NULL))
        return {};
    result.resize((size_t)bytes - 1);
    return result;
}

static bool SbWriteUtf8File(const std::wstring& path, const std::string& content) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t written = 0;
    bool ok = true;
    DWORD failure = ERROR_SUCCESS;
    while (written < content.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(content.size() - written, 1u << 20);
        DWORD done = 0;
        if (!::WriteFile(file, content.data() + written, chunk, &done, NULL) || done == 0) {
            ok = false;
            failure = ::GetLastError();
            break;
        }
        written += done;
    }
    if (ok && !::FlushFileBuffers(file)) { ok = false; failure = ::GetLastError(); }
    ::CloseHandle(file);
    if (!ok) {
        ::DeleteFileW(path.c_str());
        ::SetLastError(failure);
    }
    return ok;
}

static bool SbReplaceUtf8File(const std::wstring& path, const std::string& content,
    DWORD& errorOut) {
    std::wstring temporary = path + L".sunlauncher." +
        std::to_wstring(::GetCurrentProcessId()) + L"." +
        std::to_wstring(::GetTickCount64()) + L".tmp";
    if (!SbWriteUtf8File(temporary, content)) {
        errorOut = ::GetLastError();
        return false;
    }
    if (!::MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        errorOut = ::GetLastError();
        ::DeleteFileW(temporary.c_str());
        return false;
    }
    errorOut = ERROR_SUCCESS;
    return true;
}

static std::string SbNewApiSecret() {
    GUID guid{};
    wchar_t wide[40]{};
    if (SUCCEEDED(::CoCreateGuid(&guid)) && ::StringFromGUID2(guid, wide, 40) > 0) {
        std::string utf8 = SbWideToUtf8(wide);
        std::string secret;
        for (char c : utf8)
            if (c != '{' && c != '}' && c != '-') secret.push_back(c);
        if (!secret.empty()) return secret;
    }
    return std::to_string(::GetCurrentProcessId()) + std::to_string(::GetTickCount64());
}

static bool SbPortCanBind(uint16_t port) {
    if (!SbEnsureWinsock()) return false;
    SOCKET socketHandle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) return false;
    BOOL exclusive = TRUE;
    ::setsockopt(socketHandle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (const char*)&exclusive, sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    const bool ok = (::bind(socketHandle, (sockaddr*)&address, sizeof(address)) == 0);
    ::closesocket(socketHandle);
    return ok;
}

static bool SbFindFreePort(uint16_t start, uint16_t avoid, uint16_t& portOut) {
    const unsigned base = start >= 1024 ? start : 1024;
    for (unsigned offset = 0; offset < 64512; offset++) {
        unsigned candidate = base + offset;
        if (candidate > 65535) candidate = 1024 + (candidate - 65536);
        const uint16_t port = (uint16_t)candidate;
        if (port == avoid) continue;
        if (SbPortCanBind(port)) { portOut = port; return true; }
    }
    return false;
}

static uint16_t SbControllerPort(const std::string& controller) {
    const size_t colon = controller.find_last_of(':');
    if (colon == std::string::npos || colon + 1 >= controller.size()) return 0;
    unsigned value = 0;
    for (size_t i = colon + 1; i < controller.size(); i++) {
        if (controller[i] < '0' || controller[i] > '9') return 0;
        value = value * 10 + (unsigned)(controller[i] - '0');
        if (value > 65535) return 0;
    }
    return value ? (uint16_t)value : 0;
}

static bool SbConnectLoopback(uint16_t port) {
    SOCKET socketHandle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const bool connected = (::connect(socketHandle, (sockaddr*)&address, sizeof(address)) == 0);
    ::closesocket(socketHandle);
    return connected;
}

static bool SbWaitForInbound(SingBoxInstance* instance, std::wstring& error) {
    for (int waited = 0; waited <= 8000; waited += 100) {
        DWORD code = 0;
        if (!::GetExitCodeProcess(instance->process, &code) || code != STILL_ACTIVE) {
            error = L"sing-box 已退出，配置可能不兼容或端口无法监听";
            return false;
        }
        if (SbConnectLoopback(instance->socksPort)) return true;
        if (waited < 8000) ::Sleep(100);
    }
    error = L"等待 sing-box SOCKS 监听端口超时";
    return false;
}

static bool SbReadJsonRate(const std::string& json, const char* name,
    unsigned long long& value) {
    const std::string key = std::string("\"") + name + "\"";
    size_t position = json.find(key);
    if (position == std::string::npos) return false;
    position = json.find(':', position + key.size());
    if (position == std::string::npos) return false;
    position++;
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t')) position++;
    if (position >= json.size() || json[position] < '0' || json[position] > '9') return false;
    unsigned long long parsed = 0;
    while (position < json.size() && json[position] >= '0' && json[position] <= '9') {
        const unsigned digit = (unsigned)(json[position++] - '0');
        if (parsed > (ULLONG_MAX - digit) / 10) parsed = ULLONG_MAX;
        else parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

static void SbPublishRate(SingBoxInstance* instance, unsigned long long up,
    unsigned long long down) {
    instance->uploadBytesPerSecond.store(up, std::memory_order_relaxed);
    instance->downloadBytesPerSecond.store(down, std::memory_order_relaxed);
    if (instance->notifyWindow && ::IsWindow(instance->notifyWindow))
        ::PostMessageW(instance->notifyWindow, kSbRateUpdateMessage, 0, 0);
}

static bool SbReadTrafficStream(SingBoxInstance* instance) {
    HINTERNET session = ::WinHttpOpen(L"SunLauncher sing-box stats/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    ::WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000);
    HINTERNET connection = ::WinHttpConnect(session, L"127.0.0.1", instance->apiPort, 0);
    if (!connection) { ::WinHttpCloseHandle(session); return false; }
    HINTERNET request = ::WinHttpOpenRequest(connection, L"GET", L"/traffic", NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) {
        ::WinHttpCloseHandle(connection);
        ::WinHttpCloseHandle(session);
        return false;
    }
    bool ok = (::WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
        NULL, 0) != FALSE);
    if (ok && !instance->apiSecret.empty()) {
        std::wstring header = L"Authorization: Bearer " + W(instance->apiSecret) + L"\r\n";
        ok = (::WinHttpAddRequestHeaders(request, header.c_str(), (DWORD)-1,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != FALSE);
    }
    if (ok) ok = (::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE);
    if (ok) ok = (::WinHttpReceiveResponse(request, NULL) != FALSE);
    DWORD status = 0, statusSize = sizeof(status);
    if (ok) ok = (::WinHttpQueryHeaders(request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX(0), &status, &statusSize,
        WINHTTP_NO_HEADER_INDEX) != FALSE && status == 101);
    if (!ok) {
        ::WinHttpCloseHandle(request);
        ::WinHttpCloseHandle(connection);
        ::WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET webSocket = ::WinHttpWebSocketCompleteUpgrade(request, 0);
    ::WinHttpCloseHandle(request);
    if (!webSocket) {
        ::WinHttpCloseHandle(connection);
        ::WinHttpCloseHandle(session);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(instance->webSocketMutex);
        if (instance->stopping.load(std::memory_order_relaxed)) {
            ::WinHttpCloseHandle(webSocket);
            ::WinHttpCloseHandle(connection);
            ::WinHttpCloseHandle(session);
            return false;
        }
        instance->activeWebSocket = webSocket;
    }

    std::array<char, 4096> buffer{};
    std::string message;
    while (!instance->stopping.load(std::memory_order_relaxed)) {
        DWORD received = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        const DWORD result = ::WinHttpWebSocketReceive(webSocket, buffer.data(),
            (DWORD)buffer.size(), &received, &type);
        if (result != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            message.append(buffer.data(), received);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                unsigned long long up = 0, down = 0;
                if (SbReadJsonRate(message, "up", up) && SbReadJsonRate(message, "down", down))
                    SbPublishRate(instance, up, down);
                message.clear();
            }
        } else {
            message.clear();
        }
    }

    bool closeHandle = false;
    {
        std::lock_guard<std::mutex> lock(instance->webSocketMutex);
        if (instance->activeWebSocket == webSocket) {
            instance->activeWebSocket = nullptr;
            closeHandle = true;
        }
    }
    if (closeHandle) ::WinHttpCloseHandle(webSocket);
    ::WinHttpCloseHandle(connection);
    ::WinHttpCloseHandle(session);
    return false; // A traffic stream ending means the worker should reconnect.
}

static void SbStatsWorker(SingBoxInstance* instance) {
    while (!instance->stopping.load(std::memory_order_relaxed)) {
        const bool connected = SbReadTrafficStream(instance);
        if (!connected) SbPublishRate(instance, 0, 0);
        for (int i = 0; i < 10 && !instance->stopping.load(std::memory_order_relaxed); i++)
            ::Sleep(100);
    }
    SbPublishRate(instance, 0, 0);
}

static std::wstring SbRuntimeConfigName(const std::wstring& profileDir, unsigned attempt) {
    wchar_t leaf[128]{};
    swprintf_s(leaf, L"sing-box.runtime.%lu.%llu.%u.json",
        ::GetCurrentProcessId(), ::GetTickCount64(), attempt);
    return profileDir + L"\\" + leaf;
}

bool SbReadProfileConfig(const std::wstring& profileDir, SingBoxConfigInfo& info,
    std::string& error) {
    const std::wstring path = profileDir + L"\\config.json";
    std::string json;
    if (!FpReadTextFile(path, json)) {
        error = "profile sing-box config not found: " + SbWideToUtf8(path);
        return false;
    }
    if (json.size() >= 3 && (unsigned char)json[0] == 0xef &&
        (unsigned char)json[1] == 0xbb && (unsigned char)json[2] == 0xbf)
        json.erase(0, 3);
    return SbParseConfig(json, info, error);
}

bool SbStartInstance(const std::wstring& appDir, const std::wstring& profileDir,
    HWND notifyWindow, std::unique_ptr<SingBoxInstance>& instanceOut, std::wstring& error) {
    instanceOut.reset();
    if (!SbEnsureWinsock()) {
        error = L"Winsock 初始化失败，无法启动 sing-box";
        return false;
    }
    SingBoxConfigInfo sourceInfo{};
    std::string detail;
    const std::wstring sourceConfigPath = profileDir + L"\\config.json";
    std::string sourceJson;
    if (!FpReadTextFile(sourceConfigPath, sourceJson)) {
        error = L"profile sing-box config not found：" + sourceConfigPath;
        return false;
    }
    if (sourceJson.size() >= 3 && (unsigned char)sourceJson[0] == 0xef &&
        (unsigned char)sourceJson[1] == 0xbb && (unsigned char)sourceJson[2] == 0xbf)
        sourceJson.erase(0, 3);
    std::string normalizedConfig, cachePath;
    bool cachePathChanged = false;
    if (!SbNormalizeCacheFilePath(sourceJson, SbWideToUtf8(sourceConfigPath),
        normalizedConfig, cachePath, cachePathChanged, detail)) {
        error = L"检查 sing-box cache_file.path 失败：" + W(detail);
        return false;
    }
    if (cachePathChanged) {
        DWORD writeError = 0;
        if (!SbReplaceUtf8File(sourceConfigPath, normalizedConfig, writeError)) {
            error = L"修正 sing-box config.json 的 cache_file.path 失败，错误码=" +
                std::to_wstring(writeError);
            return false;
        }
        LOG(L"sing-box cache_file.path 已修正到指纹目录 " + W(cachePath));
    }
    sourceJson = normalizedConfig;
    if (!SbParseConfig(sourceJson, sourceInfo, detail)) {
        error = L"sing-box 配置无效：" + W(detail);
        return false;
    }
    const std::wstring executable = appDir + L"\\sing-box\\sing-box.exe";
    if (::GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"找不到 sing-box 程序：" + executable;
        return false;
    }

    uint16_t socksPort = 0, apiPort = 0;
    if (!SbFindFreePort(sourceInfo.listenPort, 0, socksPort)) {
        error = L"没有可用的 sing-box SOCKS 监听端口";
        return false;
    }
    uint16_t apiStart = SbControllerPort(sourceInfo.controller);
    if (!apiStart) apiStart = socksPort == 65535 ? 20000 : (uint16_t)(socksPort + 1);
    if (!SbFindFreePort(apiStart, socksPort, apiPort)) {
        error = L"没有可用的 sing-box Clash API 端口";
        return false;
    }

    const std::string secret = SbNewApiSecret();
    std::string runtimeJson;
    std::string buildError;
    if (!SbBuildRuntimeConfig(sourceJson, socksPort, apiPort, cachePath,
        secret, runtimeJson, buildError)) {
        error = L"sing-box 配置处理失败：" + W(buildError);
        return false;
    }

    std::wstring runtimeConfig;
    bool wroteConfig = false;
    for (unsigned attempt = 0; attempt < 16; attempt++) {
        runtimeConfig = SbRuntimeConfigName(profileDir, attempt);
        if (SbWriteUtf8File(runtimeConfig, runtimeJson)) { wroteConfig = true; break; }
        if (::GetLastError() != ERROR_FILE_EXISTS && ::GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    if (!wroteConfig) {
        error = L"无法写入 sing-box 临时配置：" + profileDir;
        return false;
    }

    auto instance = std::make_unique<SingBoxInstance>();
    instance->socksPort = socksPort;
    instance->apiPort = apiPort;
    instance->profileDir = profileDir;
    instance->runtimeConfigPath = runtimeConfig;
    instance->apiSecret = secret;
    instance->notifyWindow = notifyWindow;

    std::wstring command = L"\"" + executable + L"\" run -c \"" + runtimeConfig + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(executable.c_str(), &command[0], NULL, NULL, FALSE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL, profileDir.c_str(),
        &startup, &process)) {
        const DWORD createError = ::GetLastError();
        ::DeleteFileW(runtimeConfig.c_str());
        error = L"启动 sing-box 失败，错误码=" + std::to_wstring(createError);
        return false;
    }
    ::CloseHandle(process.hThread);
    instance->process = process.hProcess;
    instance->pid = process.dwProcessId;

    if (!SbWaitForInbound(instance.get(), error)) {
        SbStopInstance(instance);
        return false;
    }
    try {
        instance->statsThread = std::thread(SbStatsWorker, instance.get());
    } catch (...) {
        error = L"无法启动 sing-box 流量统计线程";
        SbStopInstance(instance);
        return false;
    }
    instanceOut = std::move(instance);
    return true;
}

void SbStopInstance(std::unique_ptr<SingBoxInstance>& instance) {
    if (!instance) return;
    instance->stopping.store(true, std::memory_order_relaxed);
    void* webSocket = nullptr;
    {
        std::lock_guard<std::mutex> lock(instance->webSocketMutex);
        webSocket = instance->activeWebSocket;
        instance->activeWebSocket = nullptr;
    }
    if (webSocket) ::WinHttpCloseHandle((HINTERNET)webSocket);
    if (instance->statsThread.joinable()) instance->statsThread.join();
    if (instance->process) {
        DWORD code = 0;
        if (::GetExitCodeProcess(instance->process, &code) && code == STILL_ACTIVE) {
            ::TerminateProcess(instance->process, 0);
            ::WaitForSingleObject(instance->process, 2000);
        }
        ::CloseHandle(instance->process);
        instance->process = NULL;
    }
    if (!instance->runtimeConfigPath.empty())
        ::DeleteFileW(instance->runtimeConfigPath.c_str());
    instance.reset();
}
