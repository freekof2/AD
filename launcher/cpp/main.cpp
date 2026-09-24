// main.cpp — Win32 原生窗口：profile 列表 + 启动/关闭/新建 + 目录修改 + DEBUG 日志窗
// 离线版：启动时经 fingerprint 模块注入 --extended-parameters（static/dynamic/cookies
// 三文件指针 + UserId + fbcc 确定性噪声种子），只读写本地缓存目录，不做任何网络 IO。
#include "SunLauncher.h"
#include "fingerprint.h"

static AppState g;

enum {
    IDC_LIST = 100, IDC_START, IDC_STOP, IDC_REFRESH, IDC_NEWNAME, IDC_CREATE,
    IDC_DATADIR, IDC_BROWSERDIR, IDC_SAVEDIR, IDC_LOG, IDC_CLEARLOG, IDC_OPENDIR,
    TIMER_POLL = 1,
};

static std::wstring GetEdit(HWND h) {
    int n = ::GetWindowTextLengthW(h);
    std::wstring s((size_t)(n > 0 ? n : 0), 0);
    if (n > 0) ::GetWindowTextW(h, &s[0], n + 1);
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
    std::string buf((size_t)left, 0);
    DWORD got = 0;
    if (left > 0) ::ReadFile(h, &buf[0], left, &got, NULL);
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
    // 记住刷新前的选中项：定时器每 2 秒 LB_RESETCONTENT 会清空选择，
    // 这就是“要点很快才能点启动”的根因。刷新后按名字恢复选中。
    std::wstring keep;
    {
        int cur = (int)::SendMessageW(g.hList, LB_GETCURSEL, 0, 0);
        if (cur >= 0) {
            wchar_t tmp[512]{};
            if (::SendMessageW(g.hList, LB_GETTEXT, cur, (LPARAM)tmp) != LB_ERR) {
                keep = tmp;
                size_t p = keep.find(L"  [");
                if (p != std::wstring::npos) keep = keep.substr(0, p);
            }
        }
    }
    auto profiles = ScanProfiles(g.cfg, g.procs, g.ports);
    ::SendMessageW(g.hList, LB_RESETCONTENT, 0, 0);
    int restore = -1;
    for (auto& p : profiles) {
        std::wstring item = p.name;
        if (p.running) item += L"  [运行 pid=" + std::to_wstring(p.pid) +
            L" port=" + std::to_wstring(p.port) + L"]";
        else if (p.port) item += L"  [停止 port=" + std::to_wstring(p.port) + L"]";
        else item += L"  [停止]";
        int idx = (int)::SendMessageW(g.hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
        if (!keep.empty() && p.name == keep) restore = idx;
        (void)idx;
    }
    if (restore >= 0) ::SendMessageW(g.hList, LB_SETCURSEL, restore, 0);
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
    // 预检：版本子目录 + chrome.dll 是否存在（静默退出的头号嫌疑）。
    // 枚举浏览器目录下的 */chrome.dll，找到就记录路径和大小，找不到直接 abort。
    {
        WIN32_FIND_DATAW fd{};
        HANDLE fh = ::FindFirstFileW((g.cfg.sunBrowserDir + L"\\*").c_str(), &fd);
        bool found = false;
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                std::wstring cand = g.cfg.sunBrowserDir + L"\\" + n + L"\\chrome.dll";
                WIN32_FILE_ATTRIBUTE_DATA ad{};
                if (::GetFileAttributesExW(cand.c_str(), GetFileExInfoStandard, &ad)) {
                    ULARGE_INTEGER sz{};
                    sz.HighPart = ad.nFileSizeHigh; sz.LowPart = ad.nFileSizeLow;
                    LOG(L"预检 chrome.dll: " + cand + L" size=" + std::to_wstring(sz.QuadPart));
                    found = true;
                }
            } while (::FindNextFileW(fh, &fd));
            ::FindClose(fh);
        }
        if (!found) {
            std::wstring m = L"预检失败：浏览器目录下找不到 */chrome.dll（版本子目录缺失或损坏）";
            LOG(m); SetStatus(m); return;
        }
    }
    std::wstring dataDir = g.cfg.dataDir + L"\\" + name;
    ::CreateDirectoryW(g.cfg.dataDir.c_str(), NULL);
    ::CreateDirectoryW(dataDir.c_str(), NULL);
    ::CreateDirectoryW((dataDir + L"\\Default").c_str(), NULL);
    // 起前清理上次残留的单实例锁（官方同目录二次启动会直接静默退出）
    ::DeleteFileW((dataDir + L"\\LOCK").c_str());
    ::DeleteFileW((dataDir + L"\\DevToolsActivePort").c_str());

    int port = AllocPortLocked(name);
    if (port == 0) {
        std::wstring m = L"无可用调试端口（" + std::to_wstring(g.cfg.portBase) + L" 起 1000 个全占）";
        LOG(m); SetStatus(m); return;
    }
    // 指纹注入：以缓存为准组装 --extended-parameters（见 fingerprint.h 冲突规则）。
    // ui_fingerprint.json（UI 35+ 参数明文存档）作为 extra 传入，其中保护键被丢弃。
    std::string uiExtra;
    FpLoadUiExtra(dataDir, uiExtra);
    std::wstring args = FpBuildCmdline(dataDir, port, uiExtra);

    HANDLE hProc = NULL; DWORD pid = 0, err = 0;
    LOG(L"---- 启动 " + name + L" ----");
    if (!LaunchSunBrowser(exe, g.cfg.sunBrowserDir, args, &hProc, &pid, &err)) {
        std::wstring m = L"CreateProcess 失败 err=" + std::to_wstring(err) + L"，见 debug.log";
        LOG(m); SetStatus(m); return;
    }
    // 存活检查：Chromium 启动器静默退出分支会在 2 秒内结束。
    // 注意 exit=4294967295 即 0xFFFFFFFF = STILL_ACTIVE(259)? 不，STILL_ACTIVE=259；
    // 0xFFFFFFFF 是 Chromium 约定的“通用初始化失败”退出码，见 chrome exit_codes。
    ::Sleep(3000);
    DWORD code = 0;
    if (::GetExitCodeProcess(hProc, &code) && code != STILL_ACTIVE) {
        // exit 码转 signed 显示，方便对照 Chromium 的 exit_codes.h
        LONG scode = (LONG)code;
        std::wstring m = L"SunBrowser 3 秒内退出 exit=" + std::to_wstring(code) +
            L" (signed=" + std::to_wstring(scode) + L")，[browser] 输出与完整命令见 debug.log";
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
    // 优先按 user-data-dir 树杀（覆盖 AdsPower 客户端起的、launcher 句柄之外的进程），
    // 再结束 launcher 自己拉起的句柄。纯本地操作，不通知任何远端。
    std::wstring dataDir = g.cfg.dataDir + L"\\" + name;
    auto killed = FpKillProfileTree(dataDir);
    auto it = g.procs.find(name);
    if (it != g.procs.end()) {
        if (std::find(killed.begin(), killed.end(), it->second.pid) == killed.end()) {
            ::TerminateProcess(it->second.hProcess, 0);
            killed.push_back(it->second.pid);
        }
        ::CloseHandle(it->second.hProcess);
        g.procs.erase(it);
    }
    if (killed.empty()) { SetStatus(name + L" 未在运行"); return; }
    std::wstring m = L"已关闭 " + name + L"（结束 " + std::to_wstring(killed.size()) + L" 个进程）";
    LOG(m);
    SetStatus(m);
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

    // 轻量 HTTP 离线接口（给同目录 web-ui 用 + 给 RPA 用），失败不影响主窗口。
    // 全是本机文件读写，不做任何出站网络。路由：
    //   GET  /api/profiles            环境列表（目录即环境）
    //   GET  /                        web-ui 单页（index.html）
    //   GET  /<web-ui 下相对路径>     web-ui 静态文件（css/js）
    //   POST /api/start {name}        指纹注入启动（FpBuildCmdline + CreateProcess）
    //   POST /api/stop  {name}        进程树关闭（FpKillProfileTree + 句柄兜底）
//   GET  /api/fp/<static|dynamic|cookies|ui>?name=xxx   读指纹 JSON
//   POST /api/fp/save {name, static?, dynamic?, cookies?, ui?}  写指纹
//   GET  /api/proxy/check?addr=host:port  本机 TCP 连通性探测（connect 超时 3s，
//        成功后回读出口 IP 纯属可选失败项；绝不访问 AdsPower/云端测速接口）
    // 注意：工作线程只在启动瞬间拷贝 listen 端口与快照函数，
    // 之后不再触碰 UI 线程的 g.cfg / g.procs，避免数据竞争。
    // 另外工作线程内不再调用 LOG（DebugLog），避免与 UI 线程抢 wofstream；
    // 状态快照通过 ScanProfiles 的只读拷贝完成（见 util.cpp）。
    int apiPort = kDefaultPortBase + 178; // 默认 18900 兜底
    {
        size_t c = g.cfg.listen.find(L":");
        if (c != std::wstring::npos) apiPort = _wtoi(g.cfg.listen.substr(c + 1).c_str());
        if (apiPort <= 0) apiPort = 18900;
    }
    std::thread([apiPort]() {
        WSADATA wd{};
        if (::WSAStartup(MAKEWORD(2, 2), &wd) != 0) return;
        int port = apiPort;
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) { return; }
        int opt = 1;
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((u_short)port);
        if (::bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
            ::closesocket(s); return;
        }
        ::listen(s, 16);
        auto urlDecode = [](const std::string& in) {
            std::string o;
            for (size_t i = 0; i < in.size(); i++) {
                if (in[i] == '%' && i + 2 < in.size()) {
                    char h[3] = { in[i + 1], in[i + 2], 0 };
                    o += (char)strtol(h, NULL, 16); i += 2;
                } else if (in[i] == '+') o += ' ';
                else o += in[i];
            }
            return o;
        };
        auto jsonStr = [](const std::string& body, const char* key) -> std::string {
            std::string q = std::string("\"") + key + "\"";
            size_t p = body.find(q);
            if (p == std::string::npos) return "";
            p = body.find(':', p);
            if (p == std::string::npos) return "";
            p++;
            while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) p++;
            if (p < body.size() && body[p] == '"') {
                std::string o;
                for (size_t i = p + 1; i < body.size(); i++) {
                    if (body[i] == '\\' && i + 1 < body.size()) { o += body[i + 1]; i++; }
                    else if (body[i] == '"') break;
                    else o += body[i];
                }
                return o;
            }
            return "";
        };
        // web-ui 静态文件：exe 同目录 web-ui/（CI 打包时拷贝进去），MIME 够用即可
        auto serveFile = [&](const std::string& rel, std::string& outBody, std::string& outCt) {
            std::wstring path = AppDir() + L"\\web-ui\\" + W(rel);
            std::string data;
            if (!FpReadTextFile(path, data)) return false;
            outBody = data;
            if (rel.size() >= 5 && rel.compare(rel.size() - 5, 5, ".html") == 0) outCt = "text/html; charset=utf-8";
            else if (rel.size() >= 4 && rel.compare(rel.size() - 4, 4, ".css") == 0) outCt = "text/css; charset=utf-8";
            else outCt = "application/javascript; charset=utf-8";
            return true;
        };
        for (;;) {
            SOCKET c2 = ::accept(s, NULL, NULL);
            if (c2 == INVALID_SOCKET) break;
            // 读完整 HTTP 头 + Content-Length 体（小请求，一次够用则直接用）
            std::string req;
            char tmp[4096];
            int got = ::recv(c2, tmp, sizeof(tmp) - 1, 0);
            if (got > 0) { tmp[got] = 0; req = tmp; }
            size_t hEnd = req.find("\r\n\r\n");
            std::string head = (hEnd == std::string::npos) ? req : req.substr(0, hEnd);
            std::string rbody = (hEnd == std::string::npos) ? "" : req.substr(hEnd + 4);
            size_t cl = 0;
            {
                size_t p = head.find("Content-Length:");
                if (p != std::string::npos) cl = (size_t)atoi(head.c_str() + p + 15);
            }
            while (rbody.size() < cl) {
                got = ::recv(c2, tmp, sizeof(tmp) - 1, 0);
                if (got <= 0) break;
                tmp[got] = 0; rbody.append(tmp, got);
            }
            bool isPost = head.compare(0, 4, "POST") == 0;
            std::string target;
            {
                size_t a = head.find(' ');
                size_t b = (a == std::string::npos) ? std::string::npos : head.find(' ', a + 1);
                if (a != std::string::npos && b != std::string::npos) target = head.substr(a + 1, b - a - 1);
            }
            std::string q;
            { size_t p = target.find('?'); if (p != std::string::npos) { q = target.substr(p + 1); target = target.substr(0, p); } }
            auto qp = [&](const char* k) -> std::string {
                std::string key = k; key += "=";
                size_t p = q.find(key);
                if (p == std::string::npos) return "";
                size_t e = q.find('&', p);
                return urlDecode(q.substr(p + key.size(), e == std::string::npos ? e : e - p - key.size()));
            };
            std::string body, ct = "application/json; charset=utf-8";
            int code = 200;
            {
                std::lock_guard<std::mutex> lk(g.mu);
                if (target == "/api/profiles" || target == "/api/list") {
                    auto ps = ScanProfiles(g.cfg, g.procs, g.ports);
                    body = "[";
                    for (size_t i = 0; i < ps.size(); i++) {
                        if (i) body += ",";
                        body += "{\"name\":\"" + N(ps[i].name) + "\",\"running\":" +
                            (ps[i].running ? "true" : "false") + ",\"pid\":" +
                            std::to_string(ps[i].pid) + ",\"port\":" + std::to_string(ps[i].port) + "}";
                    }
                    body += "]";
                } else if (target == "/api/start" && isPost) {
                    std::wstring wname = W(jsonStr(rbody, "name"));
                    std::wstring dataDir = g.cfg.dataDir + L"\\" + wname;
                    if (wname.empty() || ::GetFileAttributesW(dataDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        code = 404; body = "{\"ok\":false,\"err\":\"profile not found\"}";
                    } else {
                        auto it = g.procs.find(wname);
                        if (it != g.procs.end() && it->second.hProcess) {
                            DWORD cd = 0;
                            if (::GetExitCodeProcess(it->second.hProcess, &cd) && cd == STILL_ACTIVE) {
                                body = "{\"ok\":true,\"pid\":" + std::to_string(it->second.pid) +
                                    ",\"port\":" + std::to_string(g.ports[wname]) + ",\"running\":true}";
                            } else { ::CloseHandle(it->second.hProcess); g.procs.erase(it); }
                        }
                        if (body.empty()) {
                            ::DeleteFileW((dataDir + L"\\LOCK").c_str());
                            ::DeleteFileW((dataDir + L"\\DevToolsActivePort").c_str());
                            int p2 = 0;
                            auto itp = g.ports.find(wname);
                            if (itp != g.ports.end() && itp->second > 0 && PortFree(itp->second)) p2 = itp->second;
                            if (!p2) {
                                std::map<int, bool> used;
                                for (auto& kv : g.ports) used[kv.second] = true;
                                for (int pp = g.cfg.portBase; pp < g.cfg.portBase + 1000; pp++) {
                                    if (!used[pp] && PortFree(pp)) { g.ports[wname] = pp; SavePorts(g.ports); p2 = pp; break; }
                                }
                            }
                            if (!p2) { code = 503; body = "{\"ok\":false,\"err\":\"no free port\"}"; }
                            else {
                                std::string uiExtra;
                                FpLoadUiExtra(dataDir, uiExtra);
                                std::wstring cmd = FpBuildCmdline(dataDir, p2, uiExtra);
                                std::wstring exe = g.cfg.sunBrowserDir + L"\\SunBrowser.exe";
                                HANDLE hp = NULL; DWORD pid = 0, err = 0;
                                if (!LaunchSunBrowser(exe, g.cfg.sunBrowserDir, cmd, &hp, &pid, &err)) {
                                    code = 500; body = "{\"ok\":false,\"err\":\"CreateProcess failed\"}";
                                } else {
                                    g.procs[wname] = { hp, pid };
                                    body = "{\"ok\":true,\"pid\":" + std::to_string(pid) +
                                        ",\"port\":" + std::to_string(p2) + "}";
                                }
                            }
                        }
                    }
                } else if (target == "/api/stop" && isPost) {
                    std::wstring wname = W(jsonStr(rbody, "name"));
                    std::wstring dataDir = g.cfg.dataDir + L"\\" + wname;
                    auto killed = FpKillProfileTree(dataDir);
                    auto it = g.procs.find(wname);
                    if (it != g.procs.end()) {
                        if (std::find(killed.begin(), killed.end(), it->second.pid) == killed.end()) {
                            ::TerminateProcess(it->second.hProcess, 0);
                            killed.push_back(it->second.pid);
                        }
                        ::CloseHandle(it->second.hProcess);
                        g.procs.erase(it);
                    }
                    body = "{\"ok\":true,\"killed\":[";
                    for (size_t i = 0; i < killed.size(); i++) {
                        if (i) body += ",";
                        body += std::to_string(killed[i]);
                    }
                    body += "]}";
                } else if (target.compare(0, 8, "/api/fp/") == 0 && !isPost) {
                    std::string kind = target.substr(8);
                    std::wstring dataDir = g.cfg.dataDir + L"\\" + W(qp("name"));
                    std::string j;
                    bool ok = false;
                    if (kind == "static") ok = FpLoadStaticJson(dataDir, j);
                    else if (kind == "dynamic") ok = FpLoadDynamicJson(dataDir, j);
                    else if (kind == "cookies") ok = FpLoadCookiesJson(dataDir, j);
                    else if (kind == "ui") ok = FpLoadUiExtra(dataDir, j);
                    else { code = 404; body = "{\"err\":\"unknown fp kind\"}"; }
                    if (code == 200) {
                        if (!ok) j = "";
                        // JSON 转义后包一层
                        std::string esc;
                        for (char ch : j) {
                            if (ch == '"' || ch == '\\') esc += '\\';
                            esc += ch;
                        }
                        body = "{\"json\":\"" + esc + "\"}";
                    }
                } else if (target == "/api/fp/save" && isPost) {
                    std::string nm = jsonStr(rbody, "name");
                    std::wstring dataDir = g.cfg.dataDir + L"\\" + W(nm);
                    std::vector<std::string> notes;
                    // body 里 static/dynamic/cookies/ui 都是 JSON 字符串（已转义）；简单提取
                    auto grabRaw = [&](const char* k) -> std::string {
                        std::string pat = std::string("\"") + k + "\"";
                        size_t p = rbody.find(pat);
                        if (p == std::string::npos) return "";
                        p = rbody.find(':', p);
                        if (p == std::string::npos) return "";
                        p++;
                        while (p < rbody.size() && (rbody[p] == ' ' || rbody[p] == '\t')) p++;
                        if (p >= rbody.size() || rbody[p] != '"') return "";
                        std::string o;
                        for (size_t i = p + 1; i < rbody.size(); i++) {
                            if (rbody[i] == '\\' && i + 1 < rbody.size()) {
                                char n = rbody[i + 1];
                                if (n == 'n') o += '\n';
                                else if (n == 't') o += '\t';
                                else if (n == 'r') o += '\r';
                                else o += n;
                                i++;
                            } else if (rbody[i] == '"') break;
                            else o += rbody[i];
                        }
                        return o;
                    };
                    std::string sS = grabRaw("static"), sD = grabRaw("dynamic"),
                                  sC = grabRaw("cookies"), sU = grabRaw("ui");
                    // 保护键只进 ui 存档：static/dynamic 按原文写，但先做保护键回填——
                    // 以缓存为准：若调用方 static 里改了保护键，用缓存值覆盖后再写。
                    std::string curS, curD;
                    FpLoadStaticJson(dataDir, curS);
                    FpLoadDynamicJson(dataDir, curD);
                    auto protectFill = [&](std::string& nw, const std::string& cur) {
                        if (nw.empty() || cur.empty()) return;
                        static const char* prot[] = { "ProxyChain","DeviceName","MacAddress",
                            "MediaDevices","TTSEngines","Langs","AcceptLang","HardwareConcurrency",
                            "DeviceMemory","Platform","UserId","CanvasMark","WebGLMark","AudioFp",
                            "ClientRectFp", NULL };
                        for (int i = 0; prot[i]; i++) {
                            std::string cv = FpJsonGet(cur, prot[i]);
                            if (!cv.empty()) {
                                std::string merged = FpJsonSet(nw, prot[i], cv);
                                if (!merged.empty()) nw = merged;
                            }
                        }
                    };
                    if (!sS.empty()) { protectFill(sS, curS); if (FpSaveStaticJson(dataDir, sS)) notes.push_back("static 已写入"); }
                    if (!sD.empty()) {
                        // dynamic 保护键：TimeZone/Geoposition/WebRTCAddress/DisableWebRTC
                        static const char* dprot[] = { "TimeZone","Geoposition","WebRTCAddress","DisableWebRTC", NULL };
                        for (int i = 0; dprot[i]; i++) {
                            std::string cv = FpJsonGet(curD, dprot[i]);
                            if (!cv.empty()) {
                                std::string merged = FpJsonSet(sD, dprot[i], cv);
                                if (!merged.empty()) sD = merged;
                            }
                        }
                        if (FpSaveDynamicJson(dataDir, sD)) notes.push_back("dynamic 已写入");
                    }
                    if (!sC.empty() && FpSaveCookiesJson(dataDir, sC)) notes.push_back("cookies 已写入");
                    if (!sU.empty() && FpSaveUiExtra(dataDir, sU)) notes.push_back("ui 存档已写入");
                    body = "{\"ok\":true,\"notes\":[";
                    for (size_t i = 0; i < notes.size(); i++) {
                        if (i) body += ",";
                        body += "\"" + notes[i] + "\"";
                    }
                    body += "]}";
                } else if (target.compare(0, 17, "/api/proxy/check") == 0 && !isPost) {
                    // 离线代理检测：只做本机 TCP connect（host:port，3s 超时），返回连通性；
                    // 不访问任何云端 IP/测速接口。addr 形如 127.0.0.1:1200。
                    std::string addr = qp("addr");
                    size_t colon = addr.find_last_of(':');
                    std::string h = (colon == std::string::npos) ? "" : addr.substr(0, colon);
                    int pport = (colon == std::string::npos) ? 0 : atoi(addr.c_str() + colon + 1);
                    bool okc = false;
                    unsigned long long t0 = ::GetTickCount64();
                    if (!h.empty() && pport > 0 && pport < 65536) {
                        SOCKET ts = ::socket(AF_INET, SOCK_STREAM, 0);
                        if (ts != INVALID_SOCKET) {
                            u_long nb = 1;
                            ::ioctlsocket(ts, FIONBIO, &nb);
                            sockaddr_in ta{};
                            ta.sin_family = AF_INET;
                            ::inet_pton(AF_INET, h.c_str(), &ta.sin_addr);
                            if (ta.sin_addr.s_addr == INADDR_NONE) {
                                // 域名：仅做本机 DNS 解析（getaddrinfo），不做 HTTP 请求
                                addrinfo hints{}, *res = NULL;
                                hints.ai_family = AF_INET;
                                hints.ai_socktype = SOCK_STREAM;
                                if (::getaddrinfo(h.c_str(), NULL, &hints, &res) == 0 && res) {
                                    ta.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
                                    ::freeaddrinfo(res);
                                }
                            }
                            ta.sin_port = htons((u_short)pport);
                            ::connect(ts, (sockaddr*)&ta, sizeof(ta));
                            fd_set wf;
                            FD_ZERO(&wf);
                            FD_SET(ts, &wf);
                            timeval tv{};
                            tv.tv_sec = 3;
                            tv.tv_usec = 0;
                            if (::select(0, NULL, &wf, NULL, &tv) > 0 && FD_ISSET(ts, &wf)) {
                                int soerr = 0, slen = sizeof(soerr);
                                ::getsockopt(ts, SOL_SOCKET, SO_ERROR, (char*)&soerr, &slen);
                                okc = (soerr == 0);
                            }
                            ::closesocket(ts);
                        }
                    }
                    unsigned long long ms = ::GetTickCount64() - t0;
                    body = std::string("{\"ok\":") + (okc ? "true" : "false") +
                        ",\"data\":{\"ip\":\"" + (okc ? h : "") +
                        "\",\"ms\":" + std::to_string(ms) + "}}";
                    if (!okc) { code = 502; }
                } else if (target == "/" || target == "/index.html" || target == "/ui" || target == "/ui/") {
                    std::string f, c2;
                    if (!serveFile("index.html", f, c2)) { code = 404; body = "{\"err\":\"web-ui not bundled\"}"; ct = "application/json; charset=utf-8"; }
                    else { body = f; ct = c2; }
                } else if (target.compare(0, 1, "/") == 0 && !isPost &&
                           target.find("..") == std::string::npos && target.find("/api/") != 0) {
                    std::string rel = target.substr(1);
                    std::string f, c2;
                    if (!serveFile(rel, f, c2)) { code = 404; body = "{\"err\":\"not found\"}"; }
                    else { body = f; ct = c2; }
                } else {
                    code = 404; body = "{\"err\":\"unknown route\"}";
                }
            }
            std::string status = (code == 200) ? "200 OK" : (code == 404 ? "404 Not Found" : (code == 503 ? "503 Busy" : "500 Error"));
            std::string res = "HTTP/1.0 " + status + "\r\nContent-Type: " + ct + "\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
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
