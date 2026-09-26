// main.cpp — Win32 原生窗口：profile 列表 + 启动/关闭/新建 + 目录修改 + DEBUG 日志窗
// 离线版：启动时经 fingerprint 模块注入 --extended-parameters（static/dynamic/cookies
// 三文件指针 + UserId + fbcc 确定性噪声种子），只读写本地缓存目录，不做任何网络 IO。
// 指纹配置原生窗口见 fp_ui.h/cpp（web-ui/index.html 单页版 1:1 纯原生重写，Tab 5 页）。
#include "SunLauncher.h"
#include "fingerprint.h"
#include "fp_ui.h"
#include <tlhelp32.h> // diag-04 失败现场取证：CreateToolhelp32Snapshot 枚举残留进程

static AppState g;

enum {
    IDC_LIST = 100, IDC_START, IDC_STOP, IDC_REFRESH, IDC_NEWNAME, IDC_CREATE,
    IDC_DATADIR, IDC_BROWSERDIR, IDC_SAVEDIR, IDC_LOG, IDC_CLEARLOG, IDC_OPENDIR,
    IDC_SEARCH, IDC_CHECKALL, IDC_BSTART, IDC_BSTOP, IDC_BDEL, IDC_FPCONFIG,
    IDC_GROUPLBL,
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
    if (!g.hLog || !::IsWindow(g.hLog)) return;
    LOG(L"probe logview-enter");
    std::wstring path = DebugLog::Instance().Path();
    if (path.empty()) return;
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
    LOG(L"probe logview-done");
}

static void RefreshList() {
    HWND hList = g.hList;
    if (!hList || !::IsWindow(hList)) return; // 定时器/HTTP 线程早于 WM_CREATE 触发时直接返回
    // 快照模式：锁内只拷贝 POD 数据 + 短字符串，锁外再发 LVM 消息。
    // 背景：0xc0000409 定罪到 LVM_INSERTITEMW（row0-begin 后即崩）。INSERTITEM 在同线程
    // 同步触发 LVN_ITEMCHANGED -> WndProc -> ListNameOfRow(+lock g.mu) 及 NM_CUSTOMDRAW
    // 回调（也在 WndProc 内读 g.hList），若此时 RefreshList 持有 g.mu 就是“UI 线程自己
    // 锁自己 + 回调重入”的未定义行为：MSVC /GS 熔断即报 0xc0000409。锁外发消息消重入。
    struct RowSnap { std::wstring name; std::wstring st; std::wstring port; bool checked; };
    std::wstring keep;
    std::vector<RowSnap> rows;
    std::wstring filter;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        filter = g.searchFilter;
        int cur = (int)::SendMessageW(hList, LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
        if (cur >= 0) {
            wchar_t tmp[512]{};
            LVITEMW li{};
            li.mask = LVIF_TEXT;
            li.iItem = cur;
            li.iSubItem = 0;
            li.pszText = tmp;
            li.cchTextMax = 512;
            if (::SendMessageW(hList, LVM_GETITEMTEXTW, (WPARAM)cur, (LPARAM)&li))
                keep = tmp;
        }
        auto profiles = ScanProfiles(g.cfg, g.procs, g.ports);
        LOG(std::wstring(L"probe refresh scan-done n=") + std::to_wstring(profiles.size()));
        for (auto& p : profiles) {
            // 搜索过滤（对齐 web-ui globalSearch：按目录名子串，不区分大小写）
            if (!filter.empty()) {
                std::wstring n = p.name, f = filter;
                for (auto& c : n) c = towlower(c);
                for (auto& c : f) c = towlower(c);
                if (n.find(f) == std::wstring::npos) continue;
            }
            RowSnap r;
            r.name = p.name;
            r.st = p.running ? (L"运行中 pid=" + std::to_wstring(p.pid)) : L"已停止";
            r.port = p.port ? std::to_wstring(p.port) : L"-";
            auto ck = g.checked.find(p.name);
            r.checked = (ck != g.checked.end() && ck->second);
            rows.push_back(std::move(r));
        }
    } // 解锁：以下 LVM 消息同步触发 LVN_ITEMCHANGED/NM_CUSTOMDRAW 回调，不再持锁
    ::SendMessageW(hList, LVM_DELETEALLITEMS, 0, 0);
    LOG(L"probe refresh clear-done");
    int restore = -1;
    int nOpen = 0, nClosed = 0;
    int row = 0;
    for (auto& r : rows) {
        if (r.st[0] == L'运') nOpen++; else nClosed++;
        if (row == 0) LOG(std::wstring(L"probe refresh row0-begin name=") + r.name);
        LVITEMW li{};
        li.mask = LVIF_TEXT;
        li.iItem = row;
        li.iSubItem = 0;
        li.pszText = (LPWSTR)r.name.c_str();
        int idx = (int)::SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&li);
        if (row == 0) LOG(std::wstring(L"probe refresh row0-insert idx=") + std::to_wstring(idx));
        if (idx < 0) { row++; continue; } // 插入失败跳过本行，避免后续 SETITEM 用野 idx
        LVITEMW li1{};
        li1.mask = LVIF_TEXT;
        li1.iItem = idx;
        li1.iSubItem = 1;
        li1.pszText = (LPWSTR)r.st.c_str();
        ::SendMessageW(hList, LVM_SETITEMTEXTW, (WPARAM)idx, (LPARAM)&li1);
        if (row == 0) LOG(L"probe refresh row0-col1");
        LVITEMW li2{};
        li2.mask = LVIF_TEXT;
        li2.iItem = idx;
        li2.iSubItem = 2;
        li2.pszText = (LPWSTR)r.port.c_str();
        ::SendMessageW(hList, LVM_SETITEMTEXTW, (WPARAM)idx, (LPARAM)&li2);
        if (row == 0) LOG(L"probe refresh row0-col2");
        // 复选框镜像 g.checked（批量操作用；LVS_EX_CHECKBOXES 状态图：2=勾选，1=未勾选）
        LVITEMW liS{};
        liS.mask = LVIF_STATE;
        liS.iItem = idx;
        liS.stateMask = LVIS_STATEIMAGEMASK;
        liS.state = INDEXTOSTATEIMAGEMASK(r.checked ? 2 : 1);
        ::SendMessageW(hList, LVM_SETITEMSTATE, (WPARAM)idx, (LPARAM)&liS);
        if (row == 0) LOG(L"probe refresh row0-state");
        // 运行中行着 success 色由 CustomDraw 负责，此处只记 restore
        if (!keep.empty() && r.name == keep) restore = idx;
        row++;
    }
    if (restore >= 0) {
        LVITEMW li{};
        li.mask = LVIF_STATE;
        li.iItem = restore;
        li.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        li.state = LVIS_SELECTED | LVIS_FOCUSED;
        ::SendMessageW(hList, LVM_SETITEMSTATE, (WPARAM)restore, (LPARAM)&li);
    }
    LOG(L"probe refresh rows-done");
    // 状态栏尾部追加队列计数（对齐 web-ui queue-card）
    if (g.hStatus) {
        wchar_t cur[512]{};
        ::GetWindowTextW(g.hStatus, cur, 512);
        std::wstring s = cur;
        size_t q = s.find(L" | 队列");
        if (q != std::wstring::npos) s = s.substr(0, q);
        if (s.empty()) s = L"就绪";
        s += L" | 队列 等待" + std::to_wstring(nClosed) +
             L" 运行" + std::to_wstring(nOpen);
        ::SetWindowTextW(g.hStatus, s.c_str());
    }
    LOG(L"probe refresh status-done");
}

// 从 LISTVIEW 当前选中行取 profile 名（第 0 列文本即目录名，无需反解）
static std::wstring ListNameOfRow(int idx) {
    if (!g.hList || !::IsWindow(g.hList) || idx < 0) return L"";
    wchar_t tmp[512]{};
    LVITEMW li{};
    li.mask = LVIF_TEXT;
    li.iItem = idx;
    li.iSubItem = 0;
    li.pszText = tmp;
    li.cchTextMax = 512;
    if (!::SendMessageW(g.hList, LVM_GETITEMTEXTW, (WPARAM)idx, (LPARAM)&li)) return L"";
    return tmp;
}

// 从列表行文本反解 profile 名（旧 LISTBOX 兼容保留；LISTVIEW 下直接用 ListNameOfRow）
static std::wstring ListNameOf(const std::wstring& item) {
    std::wstring s = item;
    if (s.size() > 4 && s[0] == L'[' && s[2] == L']' && s[3] == L' ')
        s = s.substr(4);
    size_t p = s.find(L"  [");
    if (p != std::wstring::npos) s = s.substr(0, p);
    return s;
}

static void OnStopOneLocked(const std::wstring& name, std::vector<DWORD>& killedOut);
static bool StartOneLocked(const std::wstring& name);

// 批量操作（对齐 web-ui batch-start/batch-stop/batch-del：按勾选集）
static void OnBatchStart() {
    std::vector<std::wstring> names;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        for (auto& kv : g.checked)
            if (kv.second) names.push_back(kv.first);
    }
    if (names.empty()) { SetStatus(L"请先勾选需要操作的环境（双击列表行勾选/取消）"); return; }
    int ok = 0;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        for (auto& n : names)
            if (StartOneLocked(n)) ok++;
    }
    LOG(L"批量启动 " + std::to_wstring(ok) + L"/" + std::to_wstring(names.size()));
    SetStatus(L"批量启动完成 " + std::to_wstring(ok) + L"/" + std::to_wstring(names.size()));
}

static void OnBatchStop() {
    std::vector<std::wstring> names;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        for (auto& kv : g.checked)
            if (kv.second) names.push_back(kv.first);
    }
    if (names.empty()) { SetStatus(L"请先勾选需要操作的环境（双击列表行勾选/取消）"); return; }
    int cnt = 0;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        for (auto& n : names) {
            std::vector<DWORD> k;
            OnStopOneLocked(n, k);
            cnt += (int)k.size();
        }
    }
    LOG(L"批量关闭 " + std::to_wstring(names.size()) + L" 个环境，共结束 " + std::to_wstring(cnt) + L" 个进程");
    SetStatus(L"批量关闭完成（结束 " + std::to_wstring(cnt) + L" 个进程）");
}

static void OnBatchDel() {
    std::vector<std::wstring> names;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        for (auto& kv : g.checked)
            if (kv.second) names.push_back(kv.first);
    }
    if (names.empty()) { SetStatus(L"请先勾选需要删除的环境"); return; }
    // 运行中不删：先停再删
    {
        std::lock_guard<std::mutex> lk(g.mu);
        for (auto& n : names) {
            std::vector<DWORD> k;
            OnStopOneLocked(n, k);
        }
    }
    int del = 0;
    for (auto& n : names) {
        std::wstring dd = g.cfg.dataDir + L"\\" + n;
        // 递归删目录（与 /api/deleteCacheById 同逻辑的本地版）
        std::vector<std::wstring> stack;
        stack.push_back(dd);
        for (size_t si = 0; si < stack.size(); si++) {
            WIN32_FIND_DATAW fd{};
            HANDLE fh = ::FindFirstFileW((stack[si] + L"\\*").c_str(), &fd);
            if (fh == INVALID_HANDLE_VALUE) continue;
            do {
                std::wstring n2 = fd.cFileName;
                if (n2 == L"." || n2 == L"..") continue;
                std::wstring fp = stack[si] + L"\\" + n2;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) stack.push_back(fp);
                else ::DeleteFileW(fp.c_str());
            } while (::FindNextFileW(fh, &fd));
            ::FindClose(fh);
        }
        for (size_t si = stack.size(); si > 0; si--)
            ::RemoveDirectoryW(stack[si - 1].c_str());
        {
            std::lock_guard<std::mutex> lk(g.mu);
            g.checked.erase(n);
            g.ports.erase(n);
        }
        del++;
        LOG(L"删除环境 " + n);
    }
    {
        std::lock_guard<std::mutex> lk(g.mu);
        SavePorts(g.ports);
    }
    SetStatus(L"已删除 " + std::to_wstring(del) + L" 个环境");
}

// 新建环境（对齐 web-ui btnDrawerSave：中文名自动生成 ascii 目录名，中文存 remark 风格）
// 原生版：输入名含非 ascii 或无下划线时生成 env<base36>_local 目录，并在 ui 存档 remark 留原名。
static std::wstring MakeProfileDirName(const std::wstring& input, std::wstring& remarkOut) {
    bool ascii = true, hasUnder = false;
    for (auto c : input) {
        if (c > 127 || c == L'_') { if (c == L'_') hasUnder = true; else if (c > 127) ascii = false; }
        if (c > 127) ascii = false;
    }
    if (ascii && hasUnder) { remarkOut.clear(); return input; }
    unsigned long long t = (unsigned long long)time(NULL);
    wchar_t b[64];
    wsprintfW(b, L"env%llx_local", t & 0xFFFFFFFF);
    remarkOut = input;
    return b;
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
    int idx = (int)::SendMessageW(g.hList, LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
    if (idx < 0) return L"";
    // LISTVIEW 第 0 列文本即目录名（搜索过滤后索引不错位，直接读行文本）
    std::lock_guard<std::mutex> lk(g.mu);
    return ListNameOfRow(idx);
}

// 指定 profile 启动（单启 OnStart 与批量共用；调用方需持有 g.mu）
// 防重复启动：launcher 句柄存活直接返回；句柄丢失但浏览器仍在跑（DevToolsActivePort
// 存在且端口能连上，或按命令行找到同 user-data-dir 进程）也视为运行中，不再拉第二个。
static bool StartOneLocked(const std::wstring& name) {
    auto it = g.procs.find(name);
    if (it != g.procs.end() && it->second.hProcess) {
        DWORD code = 0;
        if (::GetExitCodeProcess(it->second.hProcess, &code) && code == STILL_ACTIVE)
            return true; // 已在运行
        ::CloseHandle(it->second.hProcess);
        g.procs.erase(it);
    }
    std::wstring exe = g.cfg.sunBrowserDir + L"\\SunBrowser.exe";
    if (::GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring m = L"找不到 SunBrowser.exe：" + exe;
        LOG(m); SetStatus(m); return false;
    }
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
            LOG(m); SetStatus(m); return false;
        }
    }
    std::wstring dataDir = g.cfg.dataDir + L"\\" + name;
    ::CreateDirectoryW(g.cfg.dataDir.c_str(), NULL);
    ::CreateDirectoryW(dataDir.c_str(), NULL);
    ::CreateDirectoryW((dataDir + L"\\Default").c_str(), NULL);
    // 同一 user-data-dir 已有 SunBrowser 在跑时，Chromium 会把新进程当“唤起旧窗口”的
    // 信使（旧窗口前置 + 新进程秒退），并留下一串标题为 exe 路径的信使窗口。
    // 精确清场：只结束命令行指向本 profile 的残留进程（FpKillProfileTree 精确匹配），
    // 其它 profile 的进程不动（多开互不干扰）。批量启动时逐个清场，不跨 profile 全杀。
    {
        std::vector<DWORD> stale = FpKillProfileTree(dataDir);
        if (!stale.empty()) {
            LOG(L"diag 启动前清场(仅本profile) " + name + L"：结束 " + std::to_wstring(stale.size()) + L" 个残留进程");
            ::Sleep(800);
        }
    }
    ::DeleteFileW((dataDir + L"\\LOCK").c_str());
    ::DeleteFileW((dataDir + L"\\DevToolsActivePort").c_str());

    int port = AllocPortLocked(name);
    if (port == 0) {
        std::wstring m = L"无可用调试端口（" + std::to_wstring(g.cfg.portBase) + L" 起 1000 个全占）";
        LOG(m); SetStatus(m); return false;
    }
    LOG(L"diag port记账=" + std::to_wstring(port) +
        L"（仅记ports.json备查；实际传 --remote-debugging-port=0 由浏览器随机，见官方buildLaunchOpt）");
    std::string uiExtra;
    FpLoadUiExtra(dataDir, uiExtra);
    std::wstring args = FpBuildCmdline(dataDir, port, uiExtra);
    // 命令行超限守卫：CreateProcess 上限 32767（err=206）。超限直接拒绝启动，
    // 记 ext.len 供判读（根因多为 uiExtra 大字段并入 ext，修 FpBuildCmdline 白名单）。
    {
        size_t extLen = 0;
        if (FpCmdTooLong(args, extLen)) {
            std::wstring m = L"拒绝启动：命令行超限（ext.len=" + std::to_wstring(extLen) +
                L"，上限约24000）。uiExtra 大字段不应进 ext，见 FpBuildCmdline 白名单；" +
                L"先删该 profile 的 ui_fingerprint.json 重试，ext 应回落到 ~468。";
            LOG(m);
            LOG(W(FpDiagDumpLaunch(exe, g.cfg.sunBrowserDir, dataDir, port, uiExtra, args, 0)));
            SetStatus(m);
            return false;
        }
    }

    HANDLE hProc = NULL; DWORD pid = 0, err = 0;
    LOG(L"---- 启动 " + name + L" ----");
    // diag-01: 启动前环境变量现场（AUTH/ELECTRON_RUN_AS_NODE 有无，官方会删、我方透传）
    {
        std::string envDetail;
        bool hit = FpDiagEnvAuth(envDetail);
        LOG(L"diag env AUTH/ELECTRON_RUN_AS_NODE=" + W(envDetail) + (hit ? L" (HIT)" : L" (none)"));
    }
    if (!LaunchSunBrowser(exe, g.cfg.sunBrowserDir, args, &hProc, &pid, &err)) {
        std::wstring m = L"CreateProcess 失败 err=" + std::to_wstring(err) + L"，见 debug.log";
        LOG(m); SetStatus(m); return false;
    }
    // diag-02: 诊断块（exe/三件套/sp/ext/三键/env/hint/manual，一次启动全部现场）
    LOG(W(FpDiagDumpLaunch(exe, g.cfg.sunBrowserDir, dataDir, port, uiExtra, args, pid)));
    // diag-03: 轮询式存活检查（每 500ms 采样一次，共 6 次；记录每次退出码 + 存活态）
    DWORD code = 0;
    bool exitedEarly = false;
    for (int i = 0; i < 6; i++) {
        ::Sleep(500);
        DWORD c = 0;
        if (::GetExitCodeProcess(hProc, &c) && c != STILL_ACTIVE) {
            code = c;
            exitedEarly = true;
            LOG(L"diag poll t=" + std::to_wstring((i + 1) * 500) + L"ms pid=" +
                std::to_wstring(pid) + L" EXIT code=" + std::to_wstring(c) +
                L" (signed=" + std::to_wstring((LONG)c) + L")");
            break;
        }
        LOG(L"diag poll t=" + std::to_wstring((i + 1) * 500) + L"ms pid=" +
            std::to_wstring(pid) + L" ALIVE");
    }
    if (exitedEarly) {
        LONG scode = (LONG)code;
        std::wstring m = L"SunBrowser 3 秒内退出 exit=" + std::to_wstring(code) +
            L" (signed=" + std::to_wstring(scode) + L")，[browser]/[diag] 输出与完整命令见 debug.log";
        LOG(m + L" pid=" + std::to_wstring(pid));
        // diag-04: 失败现场取证（残留进程/DevToolsActivePort/LOCK/子进程输出计数）
        {
            HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe{};
                pe.dwSize = sizeof(pe);
                int nSun = 0;
                if (::Process32FirstW(snap, &pe)) {
                    do {
                        std::wstring xn = pe.szExeFile;
                        for (auto& ch : xn) ch = towlower(ch);
                        if (xn == L"sunbrowser.exe" || xn == L"chrome.exe") nSun++;
                    } while (::Process32NextW(snap, &pe));
                }
                ::CloseHandle(snap);
                LOG(L"diag forensics残留 SunBrowser/chrome 进程数=" + std::to_wstring(nSun));
            }
            std::wstring dtap = dataDir + L"\\DevToolsActivePort";
            DWORD adt = ::GetFileAttributesW(dtap.c_str());
            LOG(L"diag forensics DevToolsActivePort=" +
                std::wstring(adt == INVALID_FILE_ATTRIBUTES ? L"缺失（监听未起或已清）" : L"存在（ws 可能已起，看端口连通性）"));
            DWORD alk = ::GetFileAttributesW((dataDir + L"\\LOCK").c_str());
            LOG(L"diag forensics LOCK=" +
                std::wstring(alk == INVALID_FILE_ATTRIBUTES ? L"缺失" : L"存在（异常残留会锁目录）"));
            LOG(L"diag forensics若上方零[browser]行+残留0+DevTools缺失=GUI静默早退；"
                L"复制[diag]manual行到cmd手工跑，看弹窗/退出码。");
        }
        ::CloseHandle(hProc);
        SetStatus(m);
        return false;
    }
    g.procs[name] = { hProc, pid };
    std::wstring m = L"已启动 " + name + L" pid=" + std::to_wstring(pid) +
        L" port=" + std::to_wstring(port);
    LOG(m); SetStatus(m);
    return true;
}

static void OnStart() {
    std::wstring name = SelectedProfile();
    if (name.empty()) { SetStatus(L"请先在列表中选中一个 profile"); return; }
    std::lock_guard<std::mutex> lk(g.mu);
    StartOneLocked(name);
}

static void OnStop() {
    std::wstring name = SelectedProfile();
    if (name.empty()) { SetStatus(L"请先在列表中选中一个 profile"); return; }
    std::lock_guard<std::mutex> lk(g.mu);
    std::vector<DWORD> killed;
    OnStopOneLocked(name, killed);
    if (killed.empty()) { SetStatus(name + L" 未在运行"); return; }
    std::wstring m = L"已关闭 " + name + L"（结束 " + std::to_wstring(killed.size()) + L" 个进程）";
    LOG(m);
    SetStatus(m);
}

// 指定 profile 停止（单停 OnStop、批量、删除共用；调用方需持有 g.mu）
static void OnStopOneLocked(const std::wstring& name, std::vector<DWORD>& killedOut) {
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
    killedOut = killed;
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
        g.hDataDir = mkEdit(IDC_DATADIR, 96, 12, 500);
        ::CreateWindowW(L"STATIC", L"浏览器目录:", WS_CHILD | WS_VISIBLE, 12, 44, 80, 22, h, NULL, hi, NULL);
        g.hBrowserDir = mkEdit(IDC_BROWSERDIR, 96, 42, 500);
        mkBtn(IDC_SAVEDIR, L"保存目录", 606, 10, 100);
        mkBtn(IDC_OPENDIR, L"打开日志目录", 606, 42, 100);
        // 环境表：LISTVIEW 三列（环境目录/状态/端口）+ 复选框 + 整行选择（对齐 web-ui 9 列表格）
        LOG(L"probe wmcreate listview-pre");
        g.hList = ::CreateWindowW(WC_LISTVIEWW, NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            12, 78, 470, 300, h, (HMENU)(INT_PTR)IDC_LIST, hi, NULL);
        LOG(std::wstring(L"probe wmcreate listview=") + (g.hList ? L"ok" : (L"fail err=" + std::to_wstring(::GetLastError()))));
        {
            DWORD ex = (DWORD)::SendMessageW(g.hList, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
            ex |= LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_GRIDLINES;
            ::SendMessageW(g.hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)ex);
            LVCOLUMNW c0{};
            c0.mask = LVCF_TEXT | LVCF_WIDTH;
            c0.pszText = (LPWSTR)L"环境目录";
            c0.cx = 220;
            ::SendMessageW(g.hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&c0);
            LVCOLUMNW c1{};
            c1.mask = LVCF_TEXT | LVCF_WIDTH;
            c1.pszText = (LPWSTR)L"状态";
            c1.cx = 150;
            ::SendMessageW(g.hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&c1);
            LVCOLUMNW c2{};
            c2.mask = LVCF_TEXT | LVCF_WIDTH;
            c2.pszText = (LPWSTR)L"端口";
            c2.cx = 96;
            ::SendMessageW(g.hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&c2);
        }
        mkBtn(IDC_START, L"启动", 494, 78, 100);
        mkBtn(IDC_STOP, L"关闭", 494, 116, 100);
        mkBtn(IDC_REFRESH, L"刷新", 494, 154, 100);
        mkBtn(IDC_FPCONFIG, L"指纹配置", 494, 192, 100);
        ::CreateWindowW(L"STATIC", L"新建环境:", WS_CHILD | WS_VISIBLE, 494, 236, 100, 22, h, NULL, hi, NULL);
        ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            494, 260, 212, 26, h, (HMENU)(INT_PTR)IDC_NEWNAME, hi, NULL);
        mkBtn(IDC_CREATE, L"新建", 494, 292, 100);
        // 搜索 + 批量行（y=386，互不重叠；窗口 760 宽，间隙 ≥16）
        ::CreateWindowW(L"STATIC", L"搜索:", WS_CHILD | WS_VISIBLE, 12, 388, 40, 22, h, NULL, hi, NULL);
        g.hSearch = ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            56, 386, 220, 24, h, (HMENU)(INT_PTR)IDC_SEARCH, hi, NULL);
        mkBtn(IDC_BSTART, L"批量启动", 292, 384, 88);
        mkBtn(IDC_BSTOP, L"批量停止", 396, 384, 88);
        mkBtn(IDC_BDEL, L"批量删除", 500, 384, 88);
        mkBtn(IDC_CLEARLOG, L"清空日志窗", 606, 384, 100);
        ::CreateWindowW(L"STATIC", L"DEBUG 日志（debug.log 尾部；单击复选框多选，双击行=选中，多选后用批量按钮）:",
            WS_CHILD | WS_VISIBLE, 12, 416, 694, 22, h, NULL, hi, NULL);
        g.hLog = ::CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            12, 440, 694, 150, h, (HMENU)(INT_PTR)IDC_LOG, hi, NULL);
        g.hStatus = ::CreateWindowW(L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE, 12, 598, 694, 22, h, NULL, hi, NULL);
        LOG(L"probe wmcreate ctrls-done");
        ::SetWindowTextW(g.hDataDir, g.cfg.dataDir.c_str());
        ::SetWindowTextW(g.hBrowserDir, g.cfg.sunBrowserDir.c_str());
        ::SetTimer(h, TIMER_POLL, 2000, NULL);
        LOG(L"probe wmcreate timer-ok");
        LOG(L"probe wmcreate refresh-pre");
        RefreshList(); RefreshLogView();
        LOG(L"probe wmcreate refresh-done");
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        (void)code; // code 仅 IDC_SEARCH / IDC_LIST 分支使用，其余分支忽略
        if (id == IDC_START) { OnStart(); RefreshList(); RefreshLogView(); }
        else if (id == IDC_STOP) { OnStop(); RefreshList(); RefreshLogView(); }
        else if (id == IDC_REFRESH) { RefreshList(); RefreshLogView(); SetStatus(L"已刷新"); }
        else if (id == IDC_FPCONFIG) {
            std::wstring name = SelectedProfile();
            if (name.empty()) { SetStatus(L"请先选中一个环境再点指纹配置"); break; }
            Config cfgCopy;
            { std::lock_guard<std::mutex> lk(g.mu); cfgCopy = g.cfg; }
            // 模态指纹窗口（fp_ui.cpp）：Tab 5 页，保存写 ui_fingerprint.json + cookies
            if (FpUiShowModal(h, cfgCopy, name)) {
                LOG(L"指纹已保存 " + name);
                SetStatus(L"指纹已保存 " + name);
            }
            RefreshList(); RefreshLogView();
        }
        else if (id == IDC_BSTART) { OnBatchStart(); RefreshList(); RefreshLogView(); }
        else if (id == IDC_BSTOP) { OnBatchStop(); RefreshList(); RefreshLogView(); }
        else if (id == IDC_BDEL) {
            // 二次确认（对齐 web-ui confirm）
            if (::MessageBoxW(h, L"确定删除勾选的环境吗？目录将被整体删除。", L"批量删除",
                    MB_YESNO | MB_ICONWARNING) == IDYES) {
                OnBatchDel(); RefreshList(); RefreshLogView();
            }
        }
        else if (id == IDC_SEARCH && code == EN_CHANGE) {
            std::wstring q = GetEdit(g.hSearch);
            { std::lock_guard<std::mutex> lk(g.mu); g.searchFilter = q; }
            RefreshList();
        }
        else if (id == IDC_LIST && code == LBN_DBLCLK) {
            // 旧 LISTBOX 双击分支保留占位；LISTVIEW 下单击复选框即勾选（LVN_ITEMCHANGED），双击行=选中。
            int idx = (int)::SendMessageW(g.hList, LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
            if (idx >= 0) {
                std::wstring nm = ListNameOfRow(idx);
                if (!nm.empty()) {
                    std::lock_guard<std::mutex> lk(g.mu);
                    g.checked[nm] = true;
                }
                RefreshList();
            }
        }
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
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm && nm->idFrom == IDC_LIST) {
            if (nm->code == LVN_ITEMCHANGED) {
                // 单击复选框=勾选/取消（对齐 web-ui 表格 checkbox；g.checked 为批量操作镜像）
                NMLISTVIEW* lv = (NMLISTVIEW*)lp;
                if ((lv->uChanged & LVIF_STATE) &&
                    ((lv->uOldState ^ lv->uNewState) & LVIS_STATEIMAGEMASK)) {
                    UINT check = ((lv->uNewState & LVIS_STATEIMAGEMASK) >> 12);
                    std::wstring nm2 = ListNameOfRow(lv->iItem);
                    if (!nm2.empty()) {
                        std::lock_guard<std::mutex> lk(g.mu);
                        g.checked[nm2] = (check == 2);
                    }
                }
            } else if (nm->code == NM_CUSTOMDRAW) {
                // 状态列着色（对齐 web-ui pill）。注意：RefreshList 快照模式已不在持锁时发
                // LVM 消息，但 CustomDraw 仍可能与 HTTP 线程的 ScanProfiles 只读并发，
                // 此处只读 g.hList 句柄 + SendMessage 同步取文本，不碰 g.mu/g.checked。
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    wchar_t st[64]{};
                    LVITEMW li{};
                    li.mask = LVIF_TEXT;
                    li.iItem = (int)cd->nmcd.dwItemSpec;
                    li.iSubItem = 1;
                    li.pszText = st;
                    li.cchTextMax = 64;
                    ::SendMessageW(g.hList, LVM_GETITEMTEXTW, (WPARAM)li.iItem, (LPARAM)&li);
                    if (wcsstr(st, L"运行中")) cd->clrText = kUiSuccess;
                    else cd->clrText = kUiMuted;
                    return CDRF_DODEFAULT;
                }
            }
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        // 浅灰蓝底 + 白输入框（对齐 app.css --bg/--panel；只读 STATIC 透底用底色刷）
        HDC dc = (HDC)wp;
        HWND ctl = (HWND)lp;
        wchar_t cls[32]{};
        ::GetClassNameW(ctl, cls, 32);
        if (::wcscmp(cls, L"Edit") == 0 || ::wcscmp(cls, L"SysListView32") == 0) {
            ::SetBkColor(dc, kUiPanel);
            ::SetTextColor(dc, kUiText);
            if (!g.hWhiteBrush) g.hWhiteBrush = ::CreateSolidBrush(kUiPanel);
            return (LRESULT)g.hWhiteBrush;
        }
        ::SetBkColor(dc, kUiBg);
        ::SetTextColor(dc, kUiText);
        if (!g.hBgBrush) g.hBgBrush = ::CreateSolidBrush(kUiBg);
        return (LRESULT)g.hBgBrush;
    }
    case WM_DESTROY:
        if (g.hBgBrush) { ::DeleteObject(g.hBgBrush); g.hBgBrush = NULL; }
        if (g.hWhiteBrush) { ::DeleteObject(g.hWhiteBrush); g.hWhiteBrush = NULL; }
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int show) {
    // 崩溃二分探针：异常码 0xc0000409（offset 0x121591）发生在“4 条 config 日志之后、窗口出现之前”，
    // 候选只剩 InitCommonControls/RegisterClass/CreateWindow/RefreshList/HTTP 线程。
    // 每过一个候选点写一条 debug.log，复现后看最后一条即定罪。
#ifdef NDEBUG
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    BOOL iccOk = ::InitCommonControlsEx(&icc);
    DebugLog::Instance().Init(AppDir());
    LOG(std::wstring(L"probe iccOk=") + (iccOk ? L"1" : L"0"));
    g.cfg = LoadConfig();
    g.ports = LoadPorts();
    LOG(L"config dataDir=" + g.cfg.dataDir);
    LOG(L"config browserDir=" + g.cfg.sunBrowserDir);
    LOG(L"config listen=" + g.cfg.listen);

    const wchar_t* cls = L"SunLauncherCls";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = cls;
    wc.hbrBackground = ::CreateSolidBrush(kUiBg); // 浅灰蓝底（对齐 --bg），WM_CTLCOLOR* 透底同色
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    ATOM regOk = ::RegisterClassW(&wc);
    LOG(std::wstring(L"probe RegisterClass=") + std::to_wstring((unsigned)regOk));

    g.hMain = ::CreateWindowExW(0, cls, L"SunLauncher（SunBrowser 启动器）",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 660,
        NULL, NULL, hi, NULL);
    LOG(std::wstring(L"probe CreateWindow=") + (g.hMain ? L"ok" : (L"fail err=" + std::to_wstring(::GetLastError()))));
    if (!g.hMain) {
        ::MessageBoxW(NULL, L"CreateWindow 失败，见 debug.log（probe CreateWindow 行）", L"SunLauncher", MB_OK | MB_ICONERROR);
        return 1;
    }
    ::ShowWindow(g.hMain, show);
    ::UpdateWindow(g.hMain);
    LOG(L"probe ShowWindow ok");
    LOG(L"probe http-thread-start");
    AppendLog(L"日志文件：" + DebugLog::Instance().Path());

    // 轻量 HTTP 离线接口（给同目录 web-ui 用 + 给 RPA 用），失败不影响主窗口。
    // 全是本机文件读写，不做任何出站网络。路由分两层：
    //  A. 离线自有（web-ui/api.js 契约）：
    //   GET  /api/profiles            环境列表（目录即环境）
    //   GET  /                        web-ui 单页（index.html）
    //   GET  /<web-ui 下相对路径>     web-ui 静态文件（css/js）
    //   POST /api/start {name}        指纹注入启动（FpBuildCmdline + CreateProcess）
    //   POST /api/stop  {name}        进程树关闭（FpKillProfileTree + 句柄兜底）
    //   GET  /api/fp/<static|dynamic|cookies|ui>?name=xxx   读指纹 JSON
    //   POST /api/fp/save {name, static?, dynamic?, cookies?, ui?}  写指纹
    //   GET  /api/proxy/check?addr=host:port  本机 TCP 连通性探测（connect 超时 3s）
    //  B. asar 本地 Koa 路由离线兼容（main.min.js De.get/De.post 原样对接，行为见各分支注释）：
    //   POST /api/openBrowser {info} / POST /api/openBrowserV3 {info}  指纹注入启动（formatOpenRes 形态回包）
    //   GET  /api/closeAllBrowser / GET /api/stopAllBrowser            全部关闭
    //   POST /api/batcCloseBrowser {ids}                               按 id 批量关闭
    //   POST /api/checkOpen {info}                                     启动预检
    //   POST /api/checkProfileOpen {profileId,ws}                      ws 一致性（离线按运行态回 1/0）
    //   GET  /api/getOpenStatus                                        队列/运行状态列表
    //   GET  /api/getChromeOpened                                      已开 chrome 列表
    //   GET  /api/getVersion                                           版本（固定 v2.8.8.7）
    //   GET  /api/getBrowserInfo?name|id=                              环境信息
    //   GET  /api/getTabUrls?name|id=                                  读 sf_tabs.txt
    //   GET  /api/cacheSize                                            数据目录占位回包
    //   POST /api/clearCache {ids}                                     删可再生缓存（保留三件套）
    //   POST /api/deleteCacheById {ids}                                停进程后删整目录
    //   POST /api/checkProxy {proxy|addr}                              本机 TCP 探测（asar checkProxy 离线版）
    //   POST /api/updateTabs {id,tabs}                                 落盘 sf_tabs_local.txt
    //   POST /api/frontBrowser {id}                                    窗口前置
    //   POST /api/log {data}                                           写 debug.log
    // 注意：云端 /api/v1/* /api/v2/*（需 login-token）与 reports/PE 原生层不在离线范围，
    //   一律不实现；请求到未知 /api/ 路由返回 404 unknown route。
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
                } else if (target == "/api/openBrowser" && isPost) {
                    // asar 兼容：POST /api/openBrowser {info:"{...json含id...}"}。
                    // 离线只取 id(=目录名)，走与 /api/start 相同的指纹注入启动。
                    std::string info = jsonStr(rbody, "info");
                    std::string pid = FpJsonGet(info, "id");
                    if (pid.empty()) pid = jsonStr(rbody, "id");
                    if (pid.empty()) pid = jsonStr(rbody, "name");
                    std::wstring wname = W(pid);
                    std::wstring dataDir = g.cfg.dataDir + L"\\" + wname;
                    if (wname.empty() || ::GetFileAttributesW(dataDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        code = 404; body = "{\"code\":100001,\"msg\":\"profile not found\",\"data\":{},\"debugUrl\":\"\"}";
                    } else {
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
                        if (!p2) { code = 503; body = "{\"code\":100037,\"msg\":\"no free port\",\"data\":{}}"; }
                        else {
                            std::string uiExtra;
                            FpLoadUiExtra(dataDir, uiExtra);
                            std::wstring cmd = FpBuildCmdline(dataDir, p2, uiExtra);
                            std::wstring exe = g.cfg.sunBrowserDir + L"\\SunBrowser.exe";
                            HANDLE hp = NULL; DWORD cpid = 0, cerr = 0;
                            if (!LaunchSunBrowser(exe, g.cfg.sunBrowserDir, cmd, &hp, &cpid, &cerr)) {
                                code = 500; body = "{\"code\":100001,\"msg\":\"CreateProcess failed\",\"data\":{}}";
                            } else {
                                g.procs[wname] = { hp, cpid };
                                std::string ws = "ws://127.0.0.1:" + std::to_string(p2) + "/devtools/browser/launcher";
                                body = "{\"code\":0,\"msg\":\"success\",\"data\":{\"ws\":{\"puppeteer\":\"" + ws +
                                    "\",\"selenium\":\"127.0.0.1:" + std::to_string(p2) +
                                    "\"},\"debug_port\":\"" + std::to_string(p2) +
                                    "\",\"webdriver\":\"\"},\"debugUrl\":\"" + ws + "\"}";
                            }
                        }
                    }
                } else if (target == "/api/openBrowserV3" && isPost) {
                    // asar 兼容：openBrowserV3 同 openBrowser（离线无云端代理预检/队列，直接启动）。
                    std::string info = jsonStr(rbody, "info");
                    std::string pid = FpJsonGet(info, "id");
                    if (pid.empty()) pid = jsonStr(rbody, "id");
                    if (pid.empty()) pid = jsonStr(rbody, "name");
                    std::wstring wname = W(pid);
                    std::wstring dataDir = g.cfg.dataDir + L"\\" + wname;
                    if (wname.empty() || ::GetFileAttributesW(dataDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        code = 404; body = "{\"code\":900001,\"msg\":\"\",\"data\":{},\"debugUrl\":\"\"}";
                    } else {
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
                        if (!p2) { code = 503; body = "{\"code\":100037,\"msg\":\"Queue exceeds 200\",\"data\":{}}"; }
                        else {
                            std::string uiExtra;
                            FpLoadUiExtra(dataDir, uiExtra);
                            std::wstring cmd = FpBuildCmdline(dataDir, p2, uiExtra);
                            std::wstring exe = g.cfg.sunBrowserDir + L"\\SunBrowser.exe";
                            HANDLE hp = NULL; DWORD cpid = 0, cerr = 0;
                            if (!LaunchSunBrowser(exe, g.cfg.sunBrowserDir, cmd, &hp, &cpid, &cerr)) {
                                code = 500; body = "{\"code\":100001,\"msg\":\"CreateProcess failed\",\"data\":{}}";
                            } else {
                                g.procs[wname] = { hp, cpid };
                                std::string ws = "ws://127.0.0.1:" + std::to_string(p2) + "/devtools/browser/launcher";
                                body = "{\"code\":0,\"msg\":\"success\",\"data\":{\"ws\":{\"puppeteer\":\"" + ws +
                                    "\",\"selenium\":\"127.0.0.1:" + std::to_string(p2) +
                                    "\"},\"debug_port\":\"" + std::to_string(p2) +
                                    "\",\"webdriver\":\"\"},\"debugUrl\":\"" + ws + "\"}";
                            }
                        }
                    }
                } else if ((target == "/api/closeAllBrowser" && !isPost) ||
                           (target == "/api/stopAllBrowser" && !isPost)) {
                    // asar: GET 二者都调 closeAll()，返回 {code:0,data:{},msg}。
                    std::vector<DWORD> all;
                    {
                        auto ps = ScanProfiles(g.cfg, g.procs, g.ports);
                        for (auto& p : ps) {
                            std::wstring dd = g.cfg.dataDir + L"\\" + p.name;
                            auto k = FpKillProfileTree(dd);
                            all.insert(all.end(), k.begin(), k.end());
                        }
                        for (auto& kv : g.procs) {
                            if (std::find(all.begin(), all.end(), kv.second.pid) == all.end()) {
                                ::TerminateProcess(kv.second.hProcess, 0);
                                all.push_back(kv.second.pid);
                            }
                            ::CloseHandle(kv.second.hProcess);
                        }
                        g.procs.clear();
                    }
                    body = "{\"code\":0,\"data\":{},\"msg\":\"success\"}";
                } else if (target == "/api/batcCloseBrowser" && isPost) {
                    // asar: POST {ids:"a,b"} -> closeByIds。
                    std::string ids = jsonStr(rbody, "ids");
                    std::vector<DWORD> all;
                    size_t s = 0;
                    while (s <= ids.size()) {
                        size_t e = ids.find(',', s);
                        std::string one = ids.substr(s, e == std::string::npos ? e : e - s);
                        // 去首尾空白
                        size_t a = one.find_first_not_of(" \t\r\n");
                        size_t b = one.find_last_not_of(" \t\r\n");
                        if (a != std::string::npos) {
                            one = one.substr(a, b - a + 1);
                            std::wstring dd = g.cfg.dataDir + L"\\" + W(one);
                            auto k = FpKillProfileTree(dd);
                            all.insert(all.end(), k.begin(), k.end());
                            auto it = g.procs.find(W(one));
                            if (it != g.procs.end()) {
                                if (std::find(all.begin(), all.end(), it->second.pid) == all.end()) {
                                    ::TerminateProcess(it->second.hProcess, 0);
                                    all.push_back(it->second.pid);
                                }
                                ::CloseHandle(it->second.hProcess);
                                g.procs.erase(it);
                            }
                        }
                        if (e == std::string::npos) break;
                        s = e + 1;
                    }
                    body = "{\"code\":0,\"msg\":\"success\",\"data\":{\"killed\":" + std::to_string(all.size()) + "}}";
                } else if (target == "/api/checkOpen" && isPost) {
                    // asar: POST {info:"{...}"} 预检。离线：目录存在即 {code:0}，缺失 100001。
                    std::string info = jsonStr(rbody, "info");
                    std::string pid = FpJsonGet(info, "id");
                    if (pid.empty()) pid = jsonStr(rbody, "id");
                    std::wstring dd = g.cfg.dataDir + L"\\" + W(pid);
                    if (pid.empty() || ::GetFileAttributesW(dd.c_str()) == INVALID_FILE_ATTRIBUTES)
                        body = "{\"code\":100001,\"msg\":\"profile not found\",\"data\":{}}";
                    else
                        body = "{\"code\":0,\"msg\":\"success\",\"data\":{}}";
                } else if (target == "/api/checkProfileOpen" && isPost) {
                    // asar: POST {profileId, ws} -> {code:0,data:{res:"1"/"0"}}。
                    // 离线无 ws 跟踪：运行中即 "1"。
                    std::string pid = jsonStr(rbody, "profileId");
                    if (pid.empty()) pid = jsonStr(rbody, "id");
                    std::wstring wn = W(pid);
                    bool run = false;
                    auto it = g.procs.find(wn);
                    if (it != g.procs.end() && it->second.hProcess) {
                        DWORD cd = 0;
                        if (::GetExitCodeProcess(it->second.hProcess, &cd) && cd == STILL_ACTIVE) run = true;
                    }
                    body = std::string("{\"code\":0,\"data\":{\"res\":\"") + (run ? "1" : "0") + "\"},\"msg\":\"success\"}";
                } else if (target == "/api/getOpenStatus" && !isPost) {
                    // asar: {code:0,msg:"success",data:[{id,status}]}。
                    // status: launcher 运行中=SUCCESS；其余按目录存在=CLOSED。
                    body = "{\"code\":0,\"msg\":\"success\",\"data\":[";
                    {
                        auto ps = ScanProfiles(g.cfg, g.procs, g.ports);
                        for (size_t i = 0; i < ps.size(); i++) {
                            if (i) body += ",";
                            body += "{\"id\":\"" + N(ps[i].name) + "\",\"status\":\"" +
                                (ps[i].running ? "SUCCESS" : "CLOSED") + "\"}";
                        }
                    }
                    body += "]}";
                } else if (target == "/api/getChromeOpened" && !isPost) {
                    // asar: {code:0,data:{list:[{accId,ws,id}]}} chrome 已开列表。
                    body = "{\"code\":0,\"data\":{\"list\":[";
                    {
                        auto ps = ScanProfiles(g.cfg, g.procs, g.ports);
                        bool first = true;
                        for (auto& p : ps) {
                            if (!p.running) continue;
                            if (!first) body += ",";
                            first = false;
                            std::string ws = "ws://127.0.0.1:" + std::to_string(p.port) + "/devtools/browser/launcher";
                            body += "{\"accId\":\"" + N(p.name) + "\",\"ws\":\"" + ws + "\",\"id\":\"" + N(p.name) + "\"}";
                        }
                    }
                    body += "]},\"msg\":\"success\"}";
                } else if (target == "/api/getVersion" && !isPost) {
                    body = "{\"code\":0,\"data\":{\"version\":\"v2.8.8.7\"},\"msg\":\"success\"}";
                } else if (target == "/api/getBrowserInfo" && !isPost) {
                    // asar: GET ?id= -> 环境信息。离线：目录名+运行态+static 顶层回填。
                    std::string pid = qp("name");
                    if (pid.empty()) pid = qp("id");
                    std::wstring dd = g.cfg.dataDir + L"\\" + W(pid);
                    if (pid.empty() || ::GetFileAttributesW(dd.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        code = 404; body = "{\"code\":100001,\"msg\":\"profile not found\",\"data\":{}}";
                    } else {
                        std::string sj;
                        FpLoadStaticJson(dd, sj);
                        bool run = false; int pp = 0;
                        auto it = g.procs.find(W(pid));
                        if (it != g.procs.end() && it->second.hProcess) {
                            DWORD cd = 0;
                            if (::GetExitCodeProcess(it->second.hProcess, &cd) && cd == STILL_ACTIVE) run = true;
                        }
                        auto itp = g.ports.find(W(pid));
                        if (itp != g.ports.end()) pp = itp->second;
                        body = "{\"code\":0,\"msg\":\"success\",\"data\":{\"fbccId\":\"" + pid +
                            "\",\"running\":" + (run ? "true" : "false") +
                            ",\"port\":" + std::to_string(pp) +
                            ",\"kernel\":\"152\"" +
                            ",\"static\":" + (sj.empty() ? "{}" : sj) + "}}";
                    }
                } else if (target == "/api/getTabUrls" && !isPost) {
                    // asar: GET ?id= -> {code:0,data:[urls]}；离线读 sf_tabs.txt。
                    std::string pid = qp("name");
                    if (pid.empty()) pid = qp("id");
                    std::wstring dd = g.cfg.dataDir + L"\\" + W(pid);
                    std::string tabs;
                    if (!pid.empty() && FpReadTextFile(dd + L"\\sf_tabs.txt", tabs) && !tabs.empty())
                        body = "{\"code\":0,\"data\":" + tabs + ",\"msg\":\"success\"}";
                    else if (!pid.empty() && ::GetFileAttributesW(dd.c_str()) != INVALID_FILE_ATTRIBUTES)
                        body = "{\"code\":0,\"data\":[],\"msg\":\"success\"}";
                    else { code = 404; body = "{\"code\":-1,\"msg\":\"get table urls failed\"}"; }
                } else if (target == "/api/cacheSize" && !isPost) {
                    // asar: {code:0,data:{path,size,percent}}。离线：返回数据目录路径，size 由 ScanProfiles 累加估算省略为 0。
                    body = "{\"code\":0,\"data\":{\"path\":\"" + N(g.cfg.dataDir) + "\",\"size\":\"0\",\"percent\":\"0\"},\"msg\":\"success\"}";
                } else if (target == "/api/clearCache" && isPost) {
                    // asar: POST {ids, invite_code, type}。离线：仅删 ids 目录下 Default/Cache 等可再生缓存，保留指纹三件套。
                    std::string ids = jsonStr(rbody, "ids");
                    int n = 0;
                    size_t s = 0;
                    while (s <= ids.size()) {
                        size_t e = ids.find(',', s);
                        std::string one = ids.substr(s, e == std::string::npos ? e : e - s);
                        size_t a = one.find_first_not_of(" \t\r\n");
                        size_t b = one.find_last_not_of(" \t\r\n");
                        if (a != std::string::npos) {
                            one = one.substr(a, b - a + 1);
                            std::wstring dd = g.cfg.dataDir + L"\\" + W(one);
                            // 只删可再生缓存子目录，不碰指纹三件套与 Default/Preferences 等
                            const wchar_t* sub[] = { L"\\Default\\Cache", L"\\Default\\Code Cache",
                                L"\\Default\\GPUCache", L"\\GrShaderCache", L"\\ShaderCache",
                                L"\\BrowserMetrics", NULL };
                            for (int i = 0; sub[i]; i++) {
                                std::wstring t = dd + sub[i];
                                // 递归删除：先文件后目录
                                WIN32_FIND_DATAW fd{};
                                HANDLE fh = ::FindFirstFileW((t + L"\\*").c_str(), &fd);
                                if (fh != INVALID_HANDLE_VALUE) {
                                    do {
                                        std::wstring n2 = fd.cFileName;
                                        if (n2 == L"." || n2 == L"..") continue;
                                        ::DeleteFileW((t + L"\\" + n2).c_str());
                                    } while (::FindNextFileW(fh, &fd));
                                    ::FindClose(fh);
                                }
                                ::RemoveDirectoryW(t.c_str());
                            }
                            n++;
                        }
                        if (e == std::string::npos) break;
                        s = e + 1;
                    }
                    body = "{\"code\":0,\"msg\":\"success\",\"data\":{\"cleared\":" + std::to_string(n) + "}}";
                } else if (target == "/api/deleteCacheById" && isPost) {
                    // asar: POST {ids,...} 按 id 删整目录。离线：先停进程再删目录（指纹三件套随目录一起删除）。
                    std::string ids = jsonStr(rbody, "ids");
                    int n = 0;
                    size_t s = 0;
                    while (s <= ids.size()) {
                        size_t e = ids.find(',', s);
                        std::string one = ids.substr(s, e == std::string::npos ? e : e - s);
                        size_t a = one.find_first_not_of(" \t\r\n");
                        size_t b = one.find_last_not_of(" \t\r\n");
                        if (a != std::string::npos) {
                            one = one.substr(a, b - a + 1);
                            std::wstring wn = W(one);
                            std::wstring dd = g.cfg.dataDir + L"\\" + wn;
                            auto k = FpKillProfileTree(dd);
                            (void)k;
                            auto it = g.procs.find(wn);
                            if (it != g.procs.end()) {
                                ::TerminateProcess(it->second.hProcess, 0);
                                ::CloseHandle(it->second.hProcess);
                                g.procs.erase(it);
                            }
                            // 递归删目录
                            std::vector<std::wstring> stack;
                            stack.push_back(dd);
                            // 简单两遍：先删文件
                            for (size_t si = 0; si < stack.size(); si++) {
                                WIN32_FIND_DATAW fd{};
                                HANDLE fh = ::FindFirstFileW((stack[si] + L"\\*").c_str(), &fd);
                                if (fh == INVALID_HANDLE_VALUE) continue;
                                do {
                                    std::wstring n2 = fd.cFileName;
                                    if (n2 == L"." || n2 == L"..") continue;
                                    std::wstring fp = stack[si] + L"\\" + n2;
                                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) stack.push_back(fp);
                                    else ::DeleteFileW(fp.c_str());
                                } while (::FindNextFileW(fh, &fd));
                                ::FindClose(fh);
                            }
                            for (size_t si = stack.size(); si > 0; si--)
                                ::RemoveDirectoryW(stack[si - 1].c_str());
                            n++;
                        }
                        if (e == std::string::npos) break;
                        s = e + 1;
                    }
                    body = "{\"code\":0,\"msg\":\"success\",\"data\":{\"deleted\":" + std::to_string(n) + "}}";
                } else if (target == "/api/checkProxy" && isPost) {
                    // asar: POST {proxy:{...},id,type} 走代理链测速。离线：只做本机 TCP 连通性探测。
                    // 兼容两种 body：{proxy:{proxyHost,proxyPort}} 与 {addr:"host:port"}。
                    std::string addr = jsonStr(rbody, "addr");
                    std::string h, pp;
                    if (!addr.empty()) {
                        size_t colon = addr.find_last_of(':');
                        if (colon != std::string::npos) { h = addr.substr(0, colon); pp = addr.substr(colon + 1); }
                    } else {
                        h = FpJsonGet(rbody, "proxyHost");
                        if (h.size() >= 2 && h.front() == '"' && h.back() == '"') h = h.substr(1, h.size() - 2);
                        pp = FpJsonGet(rbody, "proxyPort");
                        if (pp.size() >= 2 && pp.front() == '"' && pp.back() == '"') pp = pp.substr(1, pp.size() - 2);
                        if (h.empty()) {
                            // proxy 对象嵌套：{"proxy":{"proxy_host":"..","proxy_port":".."}} 粗取
                            h = FpJsonGet(rbody, "proxy_host");
                            if (h.size() >= 2 && h.front() == '"' && h.back() == '"') h = h.substr(1, h.size() - 2);
                            pp = FpJsonGet(rbody, "proxy_port");
                            if (pp.size() >= 2 && pp.front() == '"' && pp.back() == '"') pp = pp.substr(1, pp.size() - 2);
                        }
                    }
                    int pport = atoi(pp.c_str());
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
                    if (okc)
                        body = std::string("{\"code\":0,\"msg\":\"success\",\"data\":{\"ip\":\"") + h +
                            "\",\"ms\":" + std::to_string(ms) + "}}";
                    else { code = 502; body = "{\"code\":1,\"msg\":\"proxy unreachable\"}"; }
                } else if (target == "/api/updateTabs" && isPost) {
                    // asar: POST {id, tabs:"[...]"} 写 openTabs。离线：落盘 sf_tabs_local.txt。
                    std::string pid = jsonStr(rbody, "id");
                    std::string tabs = jsonStr(rbody, "tabs");
                    if (pid.empty()) { code = 400; body = "{\"code\":-1,\"msg\":\"missing id\"}"; }
                    else {
                        std::wstring dd = g.cfg.dataDir + L"\\" + W(pid);
                        if (::GetFileAttributesW(dd.c_str()) == INVALID_FILE_ATTRIBUTES) {
                            code = 404; body = "{\"code\":-1,\"msg\":\"profile not found\"}";
                        } else {
                            FpWriteTextFile(dd + L"\\sf_tabs_local.txt", tabs);
                            body = "{\"code\":0,\"msg\":\"success\"}";
                        }
                    }
                } else if (target == "/api/frontBrowser" && isPost) {
                    // asar: POST {id} 窗口前置。离线：按 user-data-dir 找 SunBrowser 主窗口并 SetForegroundWindow。
                    std::string pid = jsonStr(rbody, "id");
                    if (pid.empty()) pid = jsonStr(rbody, "name");
                    std::wstring dd = g.cfg.dataDir + L"\\" + W(pid);
                    HWND found = NULL;
                    ::EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
                        DWORD cpid = 0;
                        ::GetWindowThreadProcessId(hw, &cpid);
                        if (!cpid || !::IsWindowVisible(hw)) return TRUE;
                        HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, cpid);
                        if (!h) return TRUE;
                        wchar_t img[MAX_PATH]{};
                        DWORD n = MAX_PATH;
                        BOOL ok = ::QueryFullProcessImageNameW(h, 0, img, &n);
                        ::CloseHandle(h);
                        if (!ok) return TRUE;
                        std::wstring s = img;
                        for (auto& c : s) c = towlower(c);
                        if (s.find(L"sunbrowser.exe") == std::wstring::npos) return TRUE;
                        *((HWND*)lp) = hw;
                        return FALSE;
                    }, (LPARAM)&found);
                    (void)dd;
                    if (found) {
                        ::SetForegroundWindow(found);
                        ::ShowWindow(found, SW_RESTORE);
                        body = "{\"code\":0,\"data\":{},\"msg\":\"success\"}";
                    } else { code = 404; body = "{\"code\":-1,\"msg\":\"browser window not found\"}"; }
                } else if (target == "/api/log" && isPost) {
                    // asar: POST {data} 上报日志。离线：写入 debug.log。
                    std::string d = jsonStr(rbody, "data");
                    if (d.empty()) d = rbody;
                    LOG(W(d));
                    body = "{\"code\":0,\"msg\":\"success\"}";
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
    LOG(L"probe msgloop-enter");

    MSG m{};
    while (::GetMessageW(&m, NULL, 0, 0)) {
        ::TranslateMessage(&m);
        ::DispatchMessageW(&m);
    }
    return 0;
}
