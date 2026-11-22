#include "SunLauncher.h"
#include "fp_cdp.h"
#include "fp_cdp_protocol.h"
#include "fp_cookies.h"
#include "fp_json.h"
#include "fingerprint.h"

#include <winhttp.h>
#include <map>
#include <set>
#include <utility>

namespace {

constexpr DWORD kCdpHttpTimeoutMs = 1000;
constexpr DWORD kCdpMaxResponseBytes = 1024 * 1024;

static std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int chars = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), NULL, 0);
    if (chars <= 0) return {};
    std::wstring result(static_cast<size_t>(chars), L'\0');
    if (!::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), &result[0], chars)) return {};
    return result;
}

static std::string JsonString(const std::string& object, const std::string& key) {
    const std::string raw = FpJsonGet(object, key);
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return {};
    return FpCookieUnescape(raw.substr(1, raw.size() - 2));
}

static bool IsProcessRunning(HANDLE process) {
    return process && ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

static bool HttpGetLocal(int port, const wchar_t* path, std::string& body) {
    body.clear();
    HINTERNET session = ::WinHttpOpen(L"SunLauncher local geolocation CDP",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    ::WinHttpSetTimeouts(session, kCdpHttpTimeoutMs, kCdpHttpTimeoutMs,
        kCdpHttpTimeoutMs, kCdpHttpTimeoutMs);
    HINTERNET connection = ::WinHttpConnect(session, L"127.0.0.1",
        static_cast<INTERNET_PORT>(port), 0);
    HINTERNET request = connection ? ::WinHttpOpenRequest(connection, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : NULL;
    DWORD disableRedirects = WINHTTP_DISABLE_REDIRECTS;
    const bool redirectsDisabled = request && ::WinHttpSetOption(request,
        WINHTTP_OPTION_DISABLE_FEATURE, &disableRedirects, sizeof(disableRedirects));
    bool ok = redirectsDisabled && ::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS,
        0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && ::WinHttpReceiveResponse(request, NULL);
    DWORD status = 0;
    DWORD statusBytes = sizeof(status);
    if (ok) {
        ok = ::WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes,
            WINHTTP_NO_HEADER_INDEX) && status == 200;
    }
    while (ok) {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
        if (available == 0) break;
        if (body.size() + available > kCdpMaxResponseBytes) { ok = false; break; }
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!::WinHttpReadData(request, &body[offset], available, &read) || read == 0) {
            body.resize(offset);
            ok = false;
            break;
        }
        body.resize(offset + read);
    }
    if (request) ::WinHttpCloseHandle(request);
    if (connection) ::WinHttpCloseHandle(connection);
    ::WinHttpCloseHandle(session);
    if (!ok) body.clear();
    return ok;
}

class CdpWebSocket {
public:
    CdpWebSocket() = default;
    CdpWebSocket(const CdpWebSocket&) = delete;
    CdpWebSocket& operator=(const CdpWebSocket&) = delete;
    ~CdpWebSocket() { Close(); }

    bool Connect(int port, const std::string& url) {
        Close();
        const std::string path = FpCdpWebSocketPath(url, port);
        const std::wstring widePath = Utf8ToWide(path);
        if (path.empty() || widePath.empty()) return false;

        session_ = ::WinHttpOpen(L"SunLauncher local geolocation CDP",
            WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) { Close(); return false; }
        ::WinHttpSetTimeouts(session_, kCdpHttpTimeoutMs, kCdpHttpTimeoutMs,
            kCdpHttpTimeoutMs, kCdpHttpTimeoutMs);
        connection_ = ::WinHttpConnect(session_, L"127.0.0.1",
            static_cast<INTERNET_PORT>(port), 0);
        if (!connection_) { Close(); return false; }
        request_ = ::WinHttpOpenRequest(connection_, L"GET", widePath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        DWORD disableRedirects = WINHTTP_DISABLE_REDIRECTS;
        if (!request_ || !::WinHttpSetOption(request_,
                WINHTTP_OPTION_DISABLE_FEATURE, &disableRedirects, sizeof(disableRedirects)) ||
            !::WinHttpSetOption(request_,
                WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) ||
            !::WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !::WinHttpReceiveResponse(request_, NULL)) {
            Close();
            return false;
        }
        DWORD status = 0;
        DWORD statusBytes = sizeof(status);
        if (!::WinHttpQueryHeaders(request_,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes,
                WINHTTP_NO_HEADER_INDEX) || status != 101) {
            Close();
            return false;
        }
        socket_ = ::WinHttpWebSocketCompleteUpgrade(request_, 0);
        ::WinHttpCloseHandle(request_);
        request_ = NULL;
        if (!socket_) { Close(); return false; }
        return true;
    }

    bool IsConnected() const { return socket_ != NULL; }

    bool SendCommand(int id, const std::string& command) {
        if (!socket_ || command.empty()) return false;
        DWORD error = ::WinHttpWebSocketSend(socket_,
            WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            const_cast<char*>(command.data()), static_cast<DWORD>(command.size()));
        if (error != NO_ERROR) { Close(); return false; }

        for (;;) {
            std::string response;
            if (!ReceiveMessage(response)) { Close(); return false; }
            if (FpJsonGet(response, "id") != std::to_string(id)) continue;
            return FpJsonGet(response, "error").empty();
        }
    }

    void Close() {
        if (socket_) {
            ::WinHttpWebSocketClose(socket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                NULL, 0);
            ::WinHttpCloseHandle(socket_);
            socket_ = NULL;
        }
        if (request_) { ::WinHttpCloseHandle(request_); request_ = NULL; }
        if (connection_) { ::WinHttpCloseHandle(connection_); connection_ = NULL; }
        if (session_) { ::WinHttpCloseHandle(session_); session_ = NULL; }
    }

private:
    bool ReceiveMessage(std::string& message) {
        message.clear();
        char buffer[8192];
        for (;;) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD error = ::WinHttpWebSocketReceive(socket_, buffer,
                static_cast<DWORD>(sizeof(buffer)), &read, &type);
            if (error != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                return false;
            if (message.size() + read > kCdpMaxResponseBytes) return false;
            message.append(buffer, read);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) return true;
            if (type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE &&
                type != WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) return false;
        }
    }

    HINTERNET session_ = NULL;
    HINTERNET connection_ = NULL;
    HINTERNET request_ = NULL;
    HINTERNET socket_ = NULL;
};

struct PageConnection {
    std::string webSocketUrl;
    std::string origin;
    std::unique_ptr<CdpWebSocket> socket;
};

class GeolocationCdpWorker {
public:
    GeolocationCdpWorker(std::wstring profileDir, std::string geoposition,
        std::string permissionMode, HANDLE process)
        : profileDir_(std::move(profileDir)), geoposition_(std::move(geoposition)),
          permissionSetting_(FpCdpPermissionSettingForMode(permissionMode)), process_(process) {}

    void Run() {
        while (IsProcessRunning(process_)) {
            if (port_ == 0) {
                port_ = ReadDevToolsPort(profileDir_, 0);
                if (port_ == 0) { ::Sleep(250); continue; }
            }

            std::string targetsJson;
            if (!HttpGetLocal(port_, L"/json/list", targetsJson)) {
                ResetConnections();
                ::Sleep(500);
                continue;
            }
            const std::vector<std::string> targets = FpCookieArraySplit(targetsJson);
            std::set<std::string> liveTargets;
            for (const std::string& target : targets) {
                if (JsonString(target, "type") != "page") continue;
                const std::string targetId = JsonString(target, "id");
                const std::string webSocketUrl = JsonString(target, "webSocketDebuggerUrl");
                if (targetId.empty() || webSocketUrl.empty()) continue;
                liveTargets.insert(targetId);

                const std::string origin = FpCdpOriginFromUrl(JsonString(target, "url"));
                PageConnection& page = pages_[targetId];
                if (!page.socket || !page.socket->IsConnected() ||
                    page.webSocketUrl != webSocketUrl) {
                    page.socket.reset();
                    page.webSocketUrl = webSocketUrl;
                    page.origin.clear();
                    auto socket = std::make_unique<CdpWebSocket>();
                    const int commandId = nextCommandId_++;
                    const std::string command = FpCdpBuildGeolocationCommand(commandId, geoposition_);
                    if (socket->Connect(port_, webSocketUrl) &&
                        socket->SendCommand(commandId, command)) {
                        page.socket = std::move(socket);
                        if (!loggedOverride_) {
                            LOG(L"diag CDP 固定地理位置覆盖已应用（坐标仅取 profile 本地数据）");
                            loggedOverride_ = true;
                        }
                    }
                }
                if (!origin.empty() && page.origin != origin) {
                    if (permissionSetting_.empty() || ApplyOriginPermission(origin))
                        page.origin = origin;
                }
            }

            for (auto it = pages_.begin(); it != pages_.end();) {
                if (liveTargets.find(it->first) != liveTargets.end()) { ++it; continue; }
                it = pages_.erase(it);
            }
            ::Sleep(500);
        }
        ResetConnections();
    }

private:
    bool EnsureBrowserSocket() {
        if (browserSocket_ && browserSocket_->IsConnected()) return true;
        browserSocket_.reset();
        std::string versionJson;
        if (!HttpGetLocal(port_, L"/json/version", versionJson)) return false;
        const std::string browserUrl = JsonString(versionJson, "webSocketDebuggerUrl");
        auto socket = std::make_unique<CdpWebSocket>();
        if (!socket->Connect(port_, browserUrl)) return false;
        browserSocket_ = std::move(socket);
        appliedOrigins_.clear();
        return true;
    }

    bool ApplyOriginPermission(const std::string& origin) {
        if (appliedOrigins_.find(origin) != appliedOrigins_.end()) return true;
        if (!EnsureBrowserSocket()) return false;
        const int id = nextCommandId_++;
        if (!browserSocket_->SendCommand(id,
                FpCdpBuildPermissionCommand(id, origin, permissionSetting_))) {
            ResetPermissionState();
            return false;
        }
        appliedOrigins_.insert(origin);
        return true;
    }

    void ResetConnections() {
        pages_.clear();
        browserSocket_.reset();
        appliedOrigins_.clear();
    }

    void ResetPermissionState() {
        browserSocket_.reset();
        appliedOrigins_.clear();
        for (auto& page : pages_) page.second.origin.clear();
    }

    std::wstring profileDir_;
    std::string geoposition_;
    std::string permissionSetting_;
    HANDLE process_ = NULL;
    int port_ = 0;
    int nextCommandId_ = 1;
    bool loggedOverride_ = false;
    std::map<std::string, PageConnection> pages_;
    std::unique_ptr<CdpWebSocket> browserSocket_;
    std::set<std::string> appliedOrigins_;
};

} // namespace

void FpStartGeolocationCdpWorker(const std::wstring& profileDir,
    const std::string& geoposition, const std::string& permissionMode,
    HANDLE browserProcess) {
    const std::string normalized = FpNormalizeGeoposition(geoposition);
    if (profileDir.empty() || normalized.empty() || !browserProcess) return;

    HANDLE workerProcess = NULL;
    if (!::DuplicateHandle(::GetCurrentProcess(), browserProcess,
            ::GetCurrentProcess(), &workerProcess, SYNCHRONIZE, FALSE, 0)) {
        LOG(L"diag CDP 地理 worker 无法复制浏览器进程句柄");
        return;
    }
    try {
        std::thread([profileDir, normalized, permissionMode, workerProcess]() {
            try {
                GeolocationCdpWorker worker(profileDir, normalized, permissionMode, workerProcess);
                worker.Run();
            } catch (...) {
                LOG(L"diag CDP 地理 worker 异常退出");
            }
            ::CloseHandle(workerProcess);
        }).detach();
    } catch (...) {
        ::CloseHandle(workerProcess);
        LOG(L"diag CDP 地理 worker 启动失败");
    }
}
