// main.cpp — Win32 原生窗口：profile 列表 + 启动/关闭/新建 + 目录修改 + DEBUG 日志窗
#include "SunLauncher.h"

static AppState g;

enum {
    IDC_LIST = 100, IDC_START, IDC_STOP, IDC_REFRESH, IDC_NEWNAME, IDC_CREATE,
    IDC_DATADIR, IDC_BROWSERDIR, IDC_SAVEDIR, IDC_LOG, IDC_CLEARLOG, IDC_OPENDIR,
    TIMER_POLL = 1,
};

static std::wstring GetEdit(HWND h) {
    int n = ::GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    ::GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
static void AppendLog(const std::wstring& s) {
    if (!g.hLog) return;
    int n = ::GetWindowTextLengthW(g.hLog);
    ::SendMessageW(g.hLog, EM_SETSEL, n, n);
    std::wstring line = s + L"\r\n";
    ::SendMessageW(g.hLog, EM_REPLACESEL, 0, (LPARAM)line.c_str());
}
static void SetStatus(const std::wstring& s) {
    if (g.hStatus) ::SetWindowTextW(g.hStatus, s.c_str());
}

// 从 debug.log 尾部刷新日志窗（避免跨线程写控件）
static void RefreshLogView() {
    std::wstring path = DebugLog::Instance().Path();
    if (path.empty() || !g.hLog) return;
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    ::GetFileSizeEx(h, &sz);
    const long long tail = 64 * 1024;
    long long off = sz.QuadPart > tail ? sz.QuadPart - tail : 0;
    LARGE_INTEGER li{}; li.QuadPart = off;
    ::SetFilePointerEx(h, li, NULL, FILE_BEGIN);
    DWORD left = (DWORD)(sz.QuadPart - off);
    std::string buf(left, 0);
    DWORD got = 0;
    ::ReadFile(h, buf.data(), left, &got, NULL);
    ::CloseHandle(h);
    buf.resize(got);
    std::wstring w = W(buf);
    // 只取最后 ~40 行
    int lines = 0;
    size_t p = w.size();
    while (p > 0 && lines < 40) { if (w[--p] == L'\n') lines++; }
    if (p > 0) w = w.substr(p + 1);
    ::SetWindowTextW(g.hLog, w.c_str());
    ::SendMessageW(g.hLog, EM_SETSEL, -1, -1);
    ::SendMessageW(g.hLog, EM_SCROLLCARET, 0, 0);
}

static void RefreshList() {
    std::lock_guard<std::mutex> lk(g.mu);
    auto profiles = ScanProfiles(g.cfg, g.procs, g.ports);
    ::SendMessageW(g.hList, LB_RESETCONTENT, 0, 0);
    for (auto& p : profiles) {
        std::wstring item = p.name;
        if (p.running) item += L"  [运行 pid=" + std::to_wstring(p.pid) +
            L" port=" + std::to_wstring(p.port) + L"]";
        else if (p.port) item += L"  [停止 port=" + std::to_wstring(p.port) + L"]";
        else item += L"  [停止]";
        int idx = (int)::SendMessageW(g.hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
        // 用 item data 存序号，取值时再扫一遍（简单可靠）
        (void)idx;
    }
}

static int AllocPortLocked(const std::wstring& name) {
    auto it = g.ports.find(name);
    if (it != g.ports.end() && it->second > 0 && PortFree(it->second)) return it->second;
    std::map<int, bool> used;
    for (auto& kv : g.ports) used[kv.second] = true;
    for (int p = g.cfg.portBase; p < g.cfg.portBase + 1000; p++) {
        if (!used[p] && PortFree(p)) {
            g.ports[name] = p;
            SavePorts(g.ports);
            return p;
        }
    }
    return 0;
}

static std::wstring SelectedProfile() {
    int idx = (int)::SendMessageW(g.hList, LB_GETCURSEL, 0, 0);
    if (idx < 0) return L"";
    std::lock_guard<std::mutex> lk(g.mu);
    auto profiles = ScanProfiles(g.cfg, g.procs, g.ports);
    if (idx >= (int)profiles.size()) return L"";
    return profiles[idx].name;
}

static void OnStart() {
    std::wstring name = SelectedProfile();
    if (name.empty()) { SetStatus(L"请先在列表中选中一个 profile"); return; }
    std::lock_guard<std::mutex> lk(g.mu);
    auto it = g.procs.find(name);
    if (it != g.procs.end() && it->second.hProcess) {
        DWORD code = 0;
        if (::GetExitCodeProcess(it->second.hProcess, &code) && code == STILL_ACTIVE) {
            SetStatus(L"已在运行 pid=" + std::to_wstring(it->second.pid));
            return;
        }
        ::CloseHandle(it->second.hProcess);
        g.procs.erase(it);
    }
    std::wstring exe = g.cfg.sunBrowserDir + L"\\SunBrowser.exe";
    if (::GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring m = L"找不到 SunBrowser.exe：" + exe;
        LOG(m); SetStatus(m); return;
    }
    std::wstring dataDir = g.cfg.dataDir + L"\\" + name;
    ::CreateDirectoryW(g.cfg.dataDir.c_str(), NULL);
    ::CreateDirectoryW(dataDir.c_str(), NULL);
    ::CreateDirectoryW((dataDir + L"\\Default").c_str(), NULL);

    int port = AllocPortLocked(name);
    if (port == 0) {
        std::wstring m = L"无可用调试端口（" + std::to_wstring(g.cfg.portBase) + L" 起 1000 个全占）";
        LOG(m); SetStatus(m); return;
    }
    std::wstring args = L"--user-data-dir=\"" + dataDir +
        L"\" --profile-directory=Default --remote-debugging-port=" + std::to_wstring(port) +
        L" --no-first-run --no-default-browser-check about:blank";

    HANDLE hProc = NULL; DWORD pid = 0, err = 0;
    LOG(L"---- 启动 " + name + L" ----");
    if (!LaunchSunBrowser(exe, g.cfg.sunBrowserDir, args, &hProc, &pid, &err)) {
        std::wstring m = L"CreateProcess 失败 err=" + std::to_wstring(err) + L"，见 debug.log";
        LOG(m); SetStatus(m); return;
    }
    // 3 秒存活检查：Chromium 启动器静默退出分支会在 2 秒内结束
    ::Sleep(3000);
    DWORD code = 0;
    if (::GetExitCodeProcess(hProc, &code) && code != STILL_ACTIVE) {
        std::wstring m = L"SunBrowser 3 秒内退出 exit=" + std::to_wstring(code) +
            L"（版本子目录/参数问题），完整命令行见 debug.log";
        LOG(m + L" pid=" + std::to_wstring(pid));
        ::CloseHandle(hProc);
        SetStatus(m);
        return;
    }
    g.procs[name] = { hProc, pid };
    std::wstring m = L"已启动 " + name + L" pid=" + std::to_wstring(pid) +
        L" port=" + std::to_wstring(port);
    LOG(m); SetStatus(m);
}

static void OnStop() {
    std::wstring name = SelectedProfile();
    if (name.empty()) { SetStatus(L"请先在列表中选中一个 profile"); return; }
    std::lock_guard<std::mutex> lk(g.mu);
    auto it = g.procs.find(name);
    if (it == g.procs.end()) { SetStatus(name + L" 未在运行"); return; }
    ::TerminateProcess(it->second.hProcess, 0);
    ::CloseHandle(it->second.hProcess);
    g.procs.erase(it);
    LOG(L"已关闭 " + name);
    SetStatus(L"已关闭 " + name);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;
        auto mkBtn = [&](int id, const wchar_t* t, int x, int y, int w) {
            return ::CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, y, w, 30, h, (HMENU)(INT_PTR)id, hi, NULL);
        };
        auto mkEdit = [&](int id, int x, int y, int w) {
            return ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                x, y, w, 26, h, (HMENU)(INT_PTR)id, hi, NULL);
        };
        ::CreateWindowW(L"STATIC", L"数据目录:", WS_CHILD | WS_VISIBLE, 12, 12, 70, 22, h, NULL, hi, NULL);
        g.hDataDir = mkEdit(IDC_DATADIR, 86, 10, 480);
        ::CreateWindowW(L"STATIC", L"浏览器目录:", WS_CHILD | WS_VISIBLE, 12, 42, 70, 22, h, NULL, hi, NULL);
        g.hBrowserDir = mkEdit(IDC_BROWSERDIR, 86, 40, 480);
        mkBtn(IDC_SAVEDIR, L"保存目录", 576, 10, 100);
        mkBtn(IDC_OPENDIR, L"打开日志目录", 576, 42, 100);
        g.hList = ::CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
            12, 76, 420, 300, h, (HMENU)(INT_PTR)IDC_LIST, hi, NULL);
        mkBtn(IDC_START, L"启动", 444, 76, 100);
        mkBtn(IDC_STOP, L"关闭", 444, 114, 100);
        mkBtn(IDC_REFRESH, L"刷新", 444, 152, 100);
        ::CreateWindowW(L"STATIC", L"新建 profile:", WS_CHILD | WS_VISIBLE, 444, 200, 100, 22, h, NULL, hi, NULL);
        ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            444, 224, 232, 26, h, (HMENU)(INT_PTR)IDC_NEWNAME, hi, NULL);
        mkBtn(IDC_CREATE, L"新建", 444, 256, 100);
        ::CreateWindowW(L"STATIC", L"DEBUG 日志（debug.log 尾部，启动命令行/退出码都在里面）:",
            WS_CHILD | WS_VISIBLE, 12, 384, 500, 22, h, NULL, hi, NULL);
        mkBtn(IDC_CLEARLOG, L"清空日志窗", 576, 380, 100);
        g.hLog = ::CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            12, 408, 664, 150, h, (HMENU)(INT_PTR)IDC_LOG, hi, NULL);
        g.hStatus = ::CreateWindowW(L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE, 12, 566, 664, 22, h, NULL, hi, NULL);
        ::SetWindowTextW(g.hDataDir, g.cfg.dataDir.c_str());
        ::SetWindowTextW(g.hBrowserDir, g.cfg.sunBrowserDir.c_str());
        ::SetTimer(h, TIMER_POLL, 2000, NULL);
        RefreshList(); RefreshLogView();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_START) { OnStart(); RefreshList(); RefreshLogView(); }
        else if (id == IDC_STOP) { OnStop(); RefreshList(); RefreshLogView(); }
        else if (id == IDC_REFRESH) { RefreshList(); RefreshLogView(); SetStatus(L"已刷新"); }
        else if (id == IDC_CREATE) {
            std::wstring name = GetEdit(::GetDlgItem(h, IDC_NEWNAME));
            // 去首尾空格
            name.erase(0, name.find_first_not_of(L" \t"));
            name.erase(name.find_last_not_of(L" \t") + 1);
            if (name.empty() || name.find_first_of(L"\\/ :*?\"<>|") != std::wstring::npos) {
                SetStatus(L"非法 profile 名"); break;
            }
            ::CreateDirectoryW(g.cfg.dataDir.c_str(), NULL);
            ::CreateDirectoryW((g.cfg.dataDir + L"\\" + name).c_str(), NULL);
            ::CreateDirectoryW((g.cfg.dataDir + L"\\" + name + L"\\Default").c_str(), NULL);
            LOG(L"新建 profile " + name);
            ::SetWindowTextW(::GetDlgItem(h, IDC_NEWNAME), L"");
            RefreshList(); SetStatus(L"已新建 " + name);
        }
        else if (id == IDC_SAVEDIR) {
            std::lock_guard<std::mutex> lk(g.mu);
            g.cfg.dataDir = GetEdit(g.hDataDir);
            g.cfg.sunBrowserDir = GetEdit(g.hBrowserDir);
            if (SaveConfig(g.cfg)) { LOG(L"目录已保存 data=" + g.cfg.dataDir + L" browser=" + g.cfg.sunBrowserDir); SetStatus(L"目录已保存"); }
            else SetStatus(L"保存 sunlauncher.json 失败");
            RefreshList();
        }
        else if (id == IDC_CLEARLOG) { ::SetWindowTextW(g.hLog, L""); }
        else if (id == IDC_OPENDIR) {
            std::wstring d = AppDir();
            ::ShellExecuteW(NULL, L"open", d.c_str(), NULL, NULL, SW_SHOW);
        }
        return 0;
    }
    case WM_TIMER:
        RefreshList(); RefreshLogView();
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int show) {
    ::InitCommonControls();
    g.cfg = LoadConfig();
    g.ports = LoadPorts();
    DebugLog::Instance().Init(AppDir());
    LOG(L"config dataDir=" + g.cfg.dataDir);
    LOG(L"config browserDir=" + g.cfg.sunBrowserDir);
    LOG(L"config listen=" + g.cfg.listen);

    const wchar_t* cls = L"SunLauncherCls";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = cls;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    ::RegisterClassW(&wc);

    g.hMain = ::CreateWindowW(cls, L"SunLauncher（SunBrowser 启动器）",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 704, 630,
        NULL, NULL, hi, NULL);
    ::ShowWindow(g.hMain, show);
    ::UpdateWindow(g.hMain);
    AppendLog(L"日志文件：" + DebugLog::Instance().Path());

    // 轻量 HTTP 状态接口（给 RPA 用），失败不影响主窗口
    // 注意：此线程读取 g.cfg.listen / g.mu，与 UI 线程共享，已加锁保护。
    std::thread([]() {
        WSADATA wd{};
        if (::WSAStartup(MAKEWORD(2, 2), &wd) != 0) return;
        // 解析 listen（只支持 127.0.0.1:port 形式）
        int port = kDefaultPortBase + 178; // 默认 18900 兜底
        size_t c = g.cfg.listen.find(L":");
        if (c != std::wstring::npos) port = _wtoi(g.cfg.listen.substr(c + 1).c_str());
        if (port <= 0) port = 18900;
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) { LOG(L"HTTP 接口 socket 失败"); return; }
        int opt = 1;
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((u_short)port);
        if (::bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
            LOG(L"HTTP 接口 bind 失败 port=" + std::to_wstring(port));
            ::closesocket(s); return;
        }
        ::listen(s, 5);
        LOG(L"HTTP 状态接口 http://127.0.0.1:" + std::to_wstring(port) + L"/api/profiles");
        for (;;) {
            SOCKET c2 = ::accept(s, NULL, NULL);
            if (c2 == INVALID_SOCKET) break;
            char req[1024]{}; ::recv(c2, req, sizeof(req) - 1, 0);
            std::string body;
            {
                std::lock_guard<std::mutex> lk(g.mu);
                auto ps = ScanProfiles(g.cfg, g.procs, g.ports);
                body = "[";
                for (size_t i = 0; i < ps.size(); i++) {
                    if (i) body += ",";
                    body += "{\"name\":\"" + N(ps[i].name) + "\",\"running\":" +
                        (ps[i].running ? "true" : "false") + ",\"pid\":" +
                        std::to_string(ps[i].pid) + ",\"port\":" + std::to_string(ps[i].port) + "}";
                }
                body += "]";
            }
            std::string res = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\n\r\n" + body;
            ::send(c2, res.c_str(), (int)res.size(), 0);
            ::closesocket(c2);
        }
    }).detach();

    MSG m{};
    while (::GetMessageW(&m, NULL, 0, 0)) {
        ::TranslateMessage(&m);
        ::DispatchMessageW(&m);
    }
    return 0;
}
