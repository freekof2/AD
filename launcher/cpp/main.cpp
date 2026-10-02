// main.cpp — Win32 原生窗口：profile 列表 + 启动/关闭/新建（无 DEBUG 日志框，日志只写 debug.log）
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
    IDC_SEARCH, IDC_CHECKALL, IDC_BSTART, IDC_BSTOP, IDC_BDEL, IDC_FPCONFIG,
    IDC_GROUPLBL,
    IDC_DEBUGLOG, // 主窗口底部：debug.log 记录开关（开=写，关=本次不写；落 Config.json）
    TIMER_POLL = 1,
};

static std::wstring GetEdit(HWND h) {
    int n = ::GetWindowTextLengthW(h);
    std::wstring s((size_t)(n > 0 ? n : 0), 0);
    if (n > 0) ::GetWindowTextW(h, &s[0], n + 1);
    return s;
}
static void SetStatus(const std::wstring& s) {
    if (g.hStatus) ::SetWindowTextW(g.hStatus, s.c_str());
}

// DEBUG 日志只写 debug.log 文件（主窗口无日志框；定时器只刷环境列表）

static void RefreshList() {
    HWND hList = g.hList;
    if (!hList || !::IsWindow(hList)) return; // 定时器/HTTP 线程早于 WM_CREATE 触发时直接返回
    // 快照模式：锁内只拷贝 POD 数据 + 短字符串，锁外再发 LVM 消息。
    // 背景：0xc0000409 定罪到 LVM_INSERTITEMW（row0-begin 后即崩）。INSERTITEM 在同线程
    // 同步触发 LVN_ITEMCHANGED -> WndProc -> ListNameOfRow(+lock g.mu) 及 NM_CUSTOMDRAW
    // 回调（也在 WndProc 内读 g.hList），若此时 RefreshList 持有 g.mu 就是“UI 线程自己
    // 锁自己 + 回调重入”的未定义行为：MSVC /GS 熔断即报 0xc0000409。锁外发消息消重入。
    struct RowSnap { std::wstring name; std::wstring remark; std::wstring st; bool checked; };
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
            li.iSubItem = 1; // 第 0 列是备注，环境目录名在第 1 列
            li.pszText = tmp;
            li.cchTextMax = 512;
            if (::SendMessageW(hList, LVM_GETITEMTEXTW, (WPARAM)cur, (LPARAM)&li))
                keep = tmp;
        }
        auto profiles = ScanProfiles(g.cfg, g.procs);
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
            r.remark = p.remark;
            r.st = p.running ? (L"运行中 pid=" + std::to_wstring(p.pid)) : L"已停止";
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
        li.iSubItem = 0; // LVM_INSERTITEMW 只能插入主列；主列显示备注
        li.pszText = (LPWSTR)r.remark.c_str();
        int idx = (int)::SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&li);
        if (row == 0) LOG(std::wstring(L"probe refresh row0-insert idx=") + std::to_wstring(idx));
        if (idx < 0) { row++; continue; } // 插入失败跳过本行，避免后续 SETITEM 用野 idx
        LVITEMW liName{};
        liName.mask = LVIF_TEXT;
        liName.iItem = idx;
        liName.iSubItem = 1; // 环境目录名在第 1 列
        liName.pszText = (LPWSTR)r.name.c_str();
        ::SendMessageW(hList, LVM_SETITEMTEXTW, (WPARAM)idx, (LPARAM)&liName);
        if (row == 0) LOG(L"probe refresh row0-name");
        LVITEMW li1{};
        li1.mask = LVIF_TEXT;
        li1.iItem = idx;
        li1.iSubItem = 2;
        li1.pszText = (LPWSTR)r.st.c_str();
        ::SendMessageW(hList, LVM_SETITEMTEXTW, (WPARAM)idx, (LPARAM)&li1);
        if (row == 0) LOG(L"probe refresh row0-status");
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

// 从 LISTVIEW 当前选中行取 profile 名（环境目录名在第 1 列，第 0 列是备注，无需反解）
static std::wstring ListNameOfRow(int idx) {
    if (!g.hList || !::IsWindow(g.hList) || idx < 0) return L"";
    wchar_t tmp[512]{};
    LVITEMW li{};
    li.mask = LVIF_TEXT;
    li.iItem = idx;
    li.iSubItem = 1;
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
        // 删除按该指纹生效父目录解析（独立目录的 profile 住在别处）
        std::wstring parentDel;
        { std::lock_guard<std::mutex> lk(g.mu); parentDel = EffDataDir(g.cfg, n); }
        std::wstring dd = parentDel + L"\\" + n;
        // 递归删目录（旧 /api/deleteCacheById 同逻辑的本地版，已删除）
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
        }
        del++;
        LOG(L"删除环境 " + n);
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
    // 生效目录：该指纹独立覆盖优先（sunlauncher.json profiles 段，指纹窗口 A2 行设置），
    // 否则全局。dataDir 实际是“父目录”，profile 子目录拼在后面。
    std::wstring effBrowserDir = EffBrowserDir(g.cfg, name);
    std::wstring effParent = EffDataDir(g.cfg, name);
    std::wstring exe = effBrowserDir + L"\\SunBrowser.exe";
    if (::GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring m = L"找不到 SunBrowser.exe：" + exe + L"（该指纹独立浏览器目录或全局目录不对，指纹窗口 A2 行可改）";
        LOG(m); SetStatus(m); return false;
    }
    {
        WIN32_FIND_DATAW fd{};
        HANDLE fh = ::FindFirstFileW((effBrowserDir + L"\\*").c_str(), &fd);
        bool found = false;
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                std::wstring cand = effBrowserDir + L"\\" + n + L"\\chrome.dll";
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
            std::wstring m = L"预检失败：浏览器目录下找不到 */chrome.dll（版本子目录缺失或损坏）：" + effBrowserDir;
            LOG(m); SetStatus(m); return false;
        }
        if (effParent != g.cfg.dataDir || effBrowserDir != g.cfg.sunBrowserDir)
            LOG(L"diag 独立目录生效 " + name + L" dataParent=" + effParent + L" browserDir=" + effBrowserDir);
    }
    std::wstring dataDir = effParent + L"\\" + name;
    ::CreateDirectoryW(effParent.c_str(), NULL);
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
    // Network 坏目录自愈：该 profile 若连续出现 Network service crashed 刷屏，
    // 多为 Default/Network 缓存损坏。本次只记提示，不自动删（删了重建，需用户确认）：
    // 若本次启动后 [browser] 仍刷屏，手动删 <profile>\Default\Network 后重试
    // （不碰三件套指纹，登录态 cookies 文件保留）。
    {
        DWORD nattr = ::GetFileAttributesW((dataDir + L"\\Default\\Network").c_str());
        if (nattr != INVALID_FILE_ATTRIBUTES)
            LOG(L"diag Network目录存在 " + name + L"（若本次仍刷 Network service crashed，删 Default\\Network 后重试）");
    }

    // 端口跟随官方：命令行只传 --remote-debugging-port=0，由浏览器自己随机，
    // 实际端口写 <profile>\DevToolsActivePort（启动后再读）。不再分配端口、
    // 不再有"1000 个端口全占"的失败路径，也没有 ports.json 端口表。
    int port = 0;
    LOG(L"diag port=官方随机(--remote-debugging-port=0)，实际值启动后读 DevToolsActivePort");
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
            LOG(W(FpDiagDumpLaunch(exe, effBrowserDir, dataDir, port, uiExtra, args, 0)));
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
    if (!LaunchSunBrowser(exe, effBrowserDir, args, &hProc, &pid, &err)) {
        std::wstring m = L"CreateProcess 失败 err=" + std::to_wstring(err) + L"，见 debug.log";
        LOG(m); SetStatus(m); return false;
    }
    // diag-02: 诊断块（exe/三件套/sp/ext/三键/env/hint/manual，一次启动全部现场）
    LOG(W(FpDiagDumpLaunch(exe, effBrowserDir, dataDir, port, uiExtra, args, pid)));
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
    // 实际端口：浏览器随机分配后写 DevToolsActivePort；3 秒存活轮询已过，直接读。
    port = ReadDevToolsPort(dataDir);
    std::wstring m = L"已启动 " + name + L" pid=" + std::to_wstring(pid) +
        L" port=" + (port > 0 ? std::to_wstring(port) : L"0(DevToolsActivePort未生成)") +
        L"（官方随机）";
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
    // dataDir 按该指纹生效父目录解析（独立目录住在别处也要杀到）。
    std::wstring dataDir = EffDataDir(g.cfg, name) + L"\\" + name;
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
        // 主窗口无全局目录区：数据/浏览器路径只在指纹配置 A2 行按指纹独立设置，
        // 存 sunlauncher.json profiles 段（EffDataDir/EffBrowserDir 解析）。
        // 环境表占满左侧空间：LISTVIEW (12,12,582x494)；三列：备注/环境目录/状态。
        // 无端口列：调试端口由浏览器随机写 DevToolsActivePort（官方 --remote-debugging-port=0），
        // 备注列吃掉原端口列的 64px（185->249），环境目录/状态列宽与总宽不变。
        LOG(L"probe wmcreate listview-pre");
        g.hList = ::CreateWindowW(WC_LISTVIEWW, NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            12, 12, 582, 494, h, (HMENU)(INT_PTR)IDC_LIST, hi, NULL);
        LOG(std::wstring(L"probe wmcreate listview=") + (g.hList ? L"ok" : (L"fail err=" + std::to_wstring(::GetLastError()))));
        {
            DWORD ex = (DWORD)::SendMessageW(g.hList, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
            ex |= LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_GRIDLINES;
            ::SendMessageW(g.hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)ex);
            LVCOLUMNW c0{};
            c0.mask = LVCF_TEXT | LVCF_WIDTH;
            c0.pszText = (LPWSTR)L"备注";
            c0.cx = 249;
            ::SendMessageW(g.hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&c0);
            LVCOLUMNW c1{};
            c1.mask = LVCF_TEXT | LVCF_WIDTH;
            c1.pszText = (LPWSTR)L"环境目录";
            c1.cx = 190;
            ::SendMessageW(g.hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&c1);
            LVCOLUMNW c2{};
            c2.mask = LVCF_TEXT | LVCF_WIDTH;
            c2.pszText = (LPWSTR)L"状态";
            c2.cx = 120;
            ::SendMessageW(g.hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&c2);
        }
        // 右列操作按钮（靠右对齐 x=606）：启动/关闭/刷新/指纹配置/新建（无日志目录按钮）
        mkBtn(IDC_START, L"启动", 606, 12, 100);
        mkBtn(IDC_STOP, L"关闭", 606, 50, 100);
        mkBtn(IDC_REFRESH, L"刷新", 606, 88, 100);
        mkBtn(IDC_FPCONFIG, L"指纹配置", 606, 126, 100);
        // 新建环境行（搜索行上方 y=524/526/528，标签+输入框+按钮同行；标签加宽防截断）
        ::CreateWindowW(L"STATIC", L"新建环境:", WS_CHILD | WS_VISIBLE, 12, 528, 92, 22, h, NULL, hi, NULL);
        ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            110, 526, 380, 26, h, (HMENU)(INT_PTR)IDC_NEWNAME, hi, NULL);
        mkBtn(IDC_CREATE, L"新建", 498, 524, 100);
        // 搜索 + 批量行移到底部（y=566，与状态条 598 不重叠；窗口 760 宽，间隙 ≥16；主窗口无日志框）
        ::CreateWindowW(L"STATIC", L"搜索:", WS_CHILD | WS_VISIBLE, 12, 568, 40, 22, h, NULL, hi, NULL);
        g.hSearch = ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            56, 566, 220, 24, h, (HMENU)(INT_PTR)IDC_SEARCH, hi, NULL);
        mkBtn(IDC_BSTART, L"批量启动", 292, 564, 88);
        mkBtn(IDC_BSTOP, L"批量停止", 396, 564, 88);
        mkBtn(IDC_BDEL, L"批量删除", 500, 564, 88);
        g.hStatus = ::CreateWindowW(L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE, 12, 598, 694, 22, h, NULL, hi, NULL);
        // 底部一行：debug.log 记录开关（开=写 debug.log，关=本次运行不再写任何一行）。
        // 初值来自 Config.json debug_log（ConfigDebugLogSwitch），勾选即写回同键。
        ::CreateWindowW(L"BUTTON", L"记录 debug.log（关闭后本次不再写日志）",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, 624, 360, 22, h,
            (HMENU)(INT_PTR)IDC_DEBUGLOG, hi, NULL);
        ::SendMessageW(::GetDlgItem(h, IDC_DEBUGLOG), BM_SETCHECK,
            g.cfg.debugLog ? BST_CHECKED : BST_UNCHECKED, 0);
        LOG(L"probe wmcreate ctrls-done");
        ::SetTimer(h, TIMER_POLL, 2000, NULL);
        LOG(L"probe wmcreate timer-ok");
        LOG(L"probe wmcreate refresh-pre");
        RefreshList();
        LOG(L"probe wmcreate refresh-done");
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        (void)code; // code 仅 IDC_SEARCH / IDC_LIST 分支使用，其余分支忽略
        if (id == IDC_START) { OnStart(); RefreshList(); }
        else if (id == IDC_STOP) { OnStop(); RefreshList(); }
        else if (id == IDC_REFRESH) { RefreshList(); SetStatus(L"已刷新"); }
        else if (id == IDC_DEBUGLOG && code == BN_CLICKED) {
            // debug.log 开关：开=写；关=留一条关闭标记后本次运行全程静默（含子进程输出/diag）。
            // 勾选态立刻写回 Config.json debug_log，下次启动同值生效（手改该键同样有效）。
            const bool on = ::SendMessageW(::GetDlgItem(h, IDC_DEBUGLOG), BM_GETCHECK, 0, 0) == BST_CHECKED;
            const bool ok = ConfigSetDebugLog(on);
            { std::lock_guard<std::mutex> lk(g.mu); g.cfg.debugLog = on; }
            DebugLog::Instance().SetEnabled(on);
            if (on) LOG(L"debug.log 开关=开 Config.json debug_log=" + (ok ? L"on" : L"写入失败"));
            SetStatus(ok ? (on ? L"已开启 debug.log 记录"
                               : L"已关闭 debug.log 记录（本次运行不再写入）")
                         : L"Config.json debug_log 写入失败（下次启动仍按旧值）");
        }
        else if (id == IDC_FPCONFIG) {
            std::wstring name = SelectedProfile();
            if (name.empty()) { SetStatus(L"请先选中一个环境再点指纹配置"); break; }
            Config cfgCopy;
            { std::lock_guard<std::mutex> lk(g.mu); cfgCopy = g.cfg; }
            // 模态指纹窗口（fp_ui.cpp）：Tab 5 页，保存写 ui_fingerprint.json + cookies
            if (FpUiShowModal(h, cfgCopy, name)) {
                // 指纹页可改 Config.json / sunlauncher.json 的 per-profile 目录；
                // g.cfg 是打开模态框前的快照，保存后必须重读，保证本进程下一次启动立刻用新浏览器目录。
                Config latestCfg = LoadConfig();
                { std::lock_guard<std::mutex> lk(g.mu); g.cfg = latestCfg; }
                LOG(L"指纹保存后已重载目录配置 " + name +
                    L" dataParent=" + EffDataDir(latestCfg, name) +
                    L" browserDir=" + EffBrowserDir(latestCfg, name));
                LOG(L"指纹已保存 " + name);
                SetStatus(L"指纹已保存 " + name);
            }
            RefreshList();
        }
        else if (id == IDC_BSTART) { OnBatchStart(); RefreshList(); }
        else if (id == IDC_BSTOP) { OnBatchStop(); RefreshList(); }
        else if (id == IDC_BDEL) {
            // 二次确认（对齐 web-ui confirm）
            if (::MessageBoxW(h, L"确定删除勾选的环境吗？目录将被整体删除。", L"批量删除",
                    MB_YESNO | MB_ICONWARNING) == IDYES) {
                OnBatchDel(); RefreshList();
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
            if (!name.empty())
                name.erase(name.find_last_not_of(L" \t") + 1);
            // 新建环境命名规范（与官方 fbccId_inviteCode 对齐）：
            // 仅允许 [A-Za-z0-9_-] 且必须含下划线（fbccId 下划线前段是全部噪声种子，
            // FpFbccIdOf 取 _ 前段；无下划线时三件套文件名错位，static 读不到）。
            // 中文名自动生成 env<base36>_local（与 OnBatchDel/旧逻辑一致，remark 留原名见 ui）。
            bool legal = !name.empty() &&
                name.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::wstring::npos &&
                name.find(L'_') != std::wstring::npos;
            if (!legal) {
                std::wstring remark = name;
                if (name.empty() || name.find_first_of(L"\\/ :*?\"<>|") != std::wstring::npos) {
                    SetStatus(L"非法 profile 名（仅允许字母数字_-(须含_)，中文自动生成目录名）");
                    break;
                }
                // 合法字符但无下划线：自动加 _local 后缀，保证 fbcc 种子可取
                name += L"_local";
                LOG(L"新建 profile 名补 _local 后缀：remark=" + remark + L" dir=" + name);
            }
            // 新建落到全局 dataDir 父目录；建完提示进指纹配置 A2 行改独立目录。
            // 已存在直接提示，不重复建。
            std::wstring parentNew;
            { std::lock_guard<std::mutex> lk(g.mu); parentNew = g.cfg.dataDir; }
            if (::GetFileAttributesW((parentNew + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES) {
                SetStatus(L"已存在 " + name); break;
            }
            ::CreateDirectoryW(parentNew.c_str(), NULL);
            ::CreateDirectoryW((parentNew + L"\\" + name).c_str(), NULL);
            ::CreateDirectoryW((parentNew + L"\\" + name + L"\\Default").c_str(), NULL);
            LOG(L"新建 profile " + name + L" parent=" + parentNew + L"（独立目录请进指纹配置 A2 行设置）");
            ::SetWindowTextW(::GetDlgItem(h, IDC_NEWNAME), L"");
            RefreshList(); SetStatus(L"已新建 " + name);
        }
        return 0;
    }
    case WM_TIMER:
        RefreshList();
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
                    li.iSubItem = 2; // 状态列（第 0 列备注，第 1 列环境目录）
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
    // 日志开关先于 Init 决定（Config.json debug_log）：关=本次运行完全不碰 debug.log
    DebugLog::Instance().Init(AppDir(), ConfigDebugLogSwitch());
    LOG(std::wstring(L"probe iccOk=") + (iccOk ? L"1" : L"0"));
    g.cfg = LoadConfig();
    // 旧版 ports.json 端口表已废弃（官方只传 --remote-debugging-port=0，端口由浏览器
    // 随机写 DevToolsActivePort）。启动即删，避免残留端口号误导排障。
    {
        std::wstring legacy = AppDir() + L"\\ports.json";
        if (::GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ::DeleteFileW(legacy.c_str());
            LOG(L"清理旧 ports.json（端口改为浏览器随机分配，端口表已废弃）");
        }
    }
    LOG(L"config dataDir=" + g.cfg.dataDir);
    LOG(L"config browserDir=" + g.cfg.sunBrowserDir);

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
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 690,
        NULL, NULL, hi, NULL);
    LOG(std::wstring(L"probe CreateWindow=") + (g.hMain ? L"ok" : (L"fail err=" + std::to_wstring(::GetLastError()))));
    if (!g.hMain) {
        ::MessageBoxW(NULL, L"CreateWindow 失败，见 debug.log（probe CreateWindow 行）", L"SunLauncher", MB_OK | MB_ICONERROR);
        return 1;
    }
    ::ShowWindow(g.hMain, show);
    ::UpdateWindow(g.hMain);
    LOG(L"probe ShowWindow ok");
    // 注：HTTP 离线接口（18900 + /api/* + web-ui 静态 serving）已整体删除，
    // 主窗口原生操作直调内部函数，不再开任何监听端口。
    LOG(L"probe msgloop-enter");

    MSG m{};
    while (::GetMessageW(&m, NULL, 0, 0)) {
        ::TranslateMessage(&m);
        ::DispatchMessageW(&m);
    }
    return 0;
}
