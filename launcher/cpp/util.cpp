// util.cpp — 路径/JSON/端口/进程小工具 + DEBUG 日志落盘
#include "SunLauncher.h"
#include "fingerprint.h"

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p = buf;
    size_t i = p.find_last_of(L"\\/");
    return (i == std::wstring::npos) ? L"." : p.substr(0, i);
}
std::wstring AppDir() { return ExeDir(); }

// 注意：s.data()/a.data() 在 C++17 起返回可写指针；
// vcxproj 指定 /std:c++17（见 AdditionalOptions），故此处直接写目标缓冲。
std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w((size_t)(n > 0 ? n - 1 : 0), 0);
    if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
std::string N(const std::wstring& s) {
    if (s.empty()) return "";
    int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
    std::string a((size_t)(n > 0 ? n - 1 : 0), 0);
    if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &a[0], n, NULL, NULL);
    return a;
}

// 极简 JSON（只够读 config/ports，写用拼接，保证无第三方依赖）
static std::wstring JsonGet(const std::wstring& json, const std::wstring& key) {
    std::wstring k = L"\"" + key + L"\"";
    size_t p = json.find(k);
    if (p == std::wstring::npos) return L"";
    p = json.find(L":", p);
    if (p == std::wstring::npos) return L"";
    p++;
    while (p < json.size() && (json[p] == L' ' || json[p] == L'\t' || json[p] == L'\r' || json[p] == L'\n')) p++;
    if (p >= json.size()) return L"";
    if (json[p] == L'"') {
        size_t q = json.find(L'"', p + 1);
        if (q == std::wstring::npos) return L"";
        return json.substr(p + 1, q - p - 1);
    }
    size_t q = p;
    while (q < json.size() && (iswdigit(json[q]) || json[q] == L'-')) q++;
    return json.substr(p, q - p);
}

static std::wstring ReadFileW(const std::wstring& path) {
    std::wifstream f(path);
    if (!f) return L"";
    f.imbue(std::locale(f.getloc(), new std::codecvt_utf8<wchar_t>));
    std::wstringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Config LoadConfig() {
    Config c;
    std::wstring txt = ReadFileW(ExeDir() + L"\\sunlauncher.json");
    if (txt.empty()) return c;
    std::wstring v;
    if (!(v = JsonGet(txt, L"sun_browser_dir")).empty()) c.sunBrowserDir = v;
    if (!(v = JsonGet(txt, L"data_dir")).empty())        c.dataDir = v;
    if (!(v = JsonGet(txt, L"listen")).empty())          c.listen = v;
    if (!(v = JsonGet(txt, L"port_base")).empty())       c.portBase = _wtoi(v.c_str());
    if (c.portBase <= 0) c.portBase = kDefaultPortBase;
    // per-profile 覆盖：profiles: { "<name>": { "data_dir": "...", "sun_browser_dir": "..." } }
    // 极简解析：逐 profile 块取二键（空=跟随全局；兼容旧文件无 profiles 段）。
    {
        size_t pp = txt.find(L"\"profiles\"");
        if (pp != std::wstring::npos) {
            size_t b = txt.find(L"{", pp);
            size_t e = txt.find_last_of(L"}");
            // 外层最后一个 } 是根结束；逐个找 "<name>" : { ... } 内块
            size_t p = (b == std::wstring::npos) ? std::wstring::npos : b + 1;
            while (p != std::wstring::npos && p < txt.size()) {
                size_t q1 = txt.find(L'"', p);
                if (q1 == std::wstring::npos) break;
                size_t q2 = txt.find(L'"', q1 + 1);
                if (q2 == std::wstring::npos) break;
                std::wstring nm = txt.substr(q1 + 1, q2 - q1 - 1);
                size_t cb = txt.find(L'{', q2);
                size_t ce = (cb == std::wstring::npos) ? std::wstring::npos : txt.find(L'}', cb);
                if (cb == std::wstring::npos || ce == std::wstring::npos) break;
                if (nm == L"profiles" || nm == L"ports") { p = ce + 1; continue; }
                std::wstring blk = txt.substr(cb, ce - cb + 1);
                ProfileOverride o;
                std::wstring d = JsonGet(blk, L"data_dir");
                std::wstring s = JsonGet(blk, L"sun_browser_dir");
                if (!d.empty()) o.dataDir = d;
                if (!s.empty()) o.sunBrowserDir = s;
                if (!o.dataDir.empty() || !o.sunBrowserDir.empty())
                    c.profiles[nm] = o;
                p = ce + 1;
                if (e != std::wstring::npos && p >= e) break;
            }
        }
    }
    return c;
}

static std::wstring Esc(const std::wstring& s) {
    std::wstring o;
    for (auto ch : s) {
        if (ch == L'\\') o += L"\\\\";
        else if (ch == L'"') o += L"\\\"";
        else o += ch;
    }
    return o;
}

bool SaveConfig(const Config& c) {
    // wofstream+codecvt 在异常路径/坏 locale 下可能抛；调用方已 try/catch，
    // 此处再加 nothrow 守卫：任何异常一律返回 false，不穿越持锁区（闪退根因之一）。
    try {
        std::wstring j = L"{\r\n  \"sun_browser_dir\": \"" + Esc(c.sunBrowserDir) +
            L"\",\r\n  \"data_dir\": \"" + Esc(c.dataDir) +
            L"\",\r\n  \"listen\": \"" + Esc(c.listen) +
            L"\",\r\n  \"port_base\": " + std::to_wstring(c.portBase);
        if (!c.profiles.empty()) {
            j += L",\r\n  \"profiles\": {\r\n";
            bool first = true;
            for (auto& kv : c.profiles) {
                if (kv.second.dataDir.empty() && kv.second.sunBrowserDir.empty()) continue;
                if (!first) j += L",\r\n";
                first = false;
                j += L"    \"" + Esc(kv.first) + L"\": {";
                bool needComma = false;
                if (!kv.second.dataDir.empty()) {
                    j += L"\"data_dir\": \"" + Esc(kv.second.dataDir) + L"\"";
                    needComma = true;
                }
                if (!kv.second.sunBrowserDir.empty()) {
                    if (needComma) j += L", ";
                    j += L"\"sun_browser_dir\": \"" + Esc(kv.second.sunBrowserDir) + L"\"";
                }
                j += L"}";
            }
            j += L"\r\n  }";
        }
        j += L"\r\n}\r\n";
        std::wofstream f(ExeDir() + L"\\sunlauncher.json");
        if (!f) return false;
        f.imbue(std::locale(f.getloc(), new std::codecvt_utf8<wchar_t>));
        f << j;
        f.flush();
        return (bool)f;
    } catch (...) {
        return false;
    }
}

std::map<std::wstring, int> LoadPorts() {
    std::map<std::wstring, int> m;
    std::wstring txt = ReadFileW(ExeDir() + L"\\ports.json");
    if (txt.empty()) return m;
    // 解析 "name": port 对
    size_t p = 0;
    while ((p = txt.find(L'"', p)) != std::wstring::npos) {
        size_t q = txt.find(L'"', p + 1);
        if (q == std::wstring::npos) break;
        std::wstring key = txt.substr(p + 1, q - p - 1);
        if (key == L"ports") { p = q + 1; continue; }
        size_t c = txt.find(L":", q);
        if (c == std::wstring::npos) break;
        size_t r = c + 1;
        while (r < txt.size() && (txt[r] < L'0' || txt[r] > L'9')) {
            if (txt[r] == L'"' || txt[r] == L'{') break;
            r++;
        }
        size_t e = r;
        while (e < txt.size() && txt[e] >= L'0' && txt[e] <= L'9') e++;
        if (e > r) m[key] = _wtoi(txt.substr(r, e - r).c_str());
        p = e;
    }
    return m;
}

void SavePorts(const std::map<std::wstring, int>& m) {
    std::wstring j = L"{\r\n  \"ports\": {\r\n";
    bool first = true;
    for (auto& kv : m) {
        if (!first) j += L",\r\n";
        first = false;
        j += L"    \"" + Esc(kv.first) + L"\": " + std::to_wstring(kv.second);
    }
    j += L"\r\n  }\r\n}\r\n";
    std::wofstream f(ExeDir() + L"\\ports.json");
    if (!f) return;
    f.imbue(std::locale(f.getloc(), new std::codecvt_utf8<wchar_t>));
    f << j;
}

bool PortFree(int port) {
    WSADATA wd{};
    static bool wsa = false;
    if (!wsa) { ::WSAStartup(MAKEWORD(2, 2), &wd); wsa = true; }
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((u_short)port);
    int r = ::bind(s, (sockaddr*)&a, sizeof(a));
    ::closesocket(s);
    return r == 0;
}

std::vector<ProfileInfo> ScanProfiles(const Config& cfg,
    const std::map<std::wstring, ProcHandle>& procs,
    const std::map<std::wstring, int>& ports) {
    // 多父目录扫描：全局 dataDir + 各 profile 独立父目录（profiles 段），按名去重合并。
    // 指纹页 A2 行设置的独立数据目录是 profile 的真实住所，列表必须从那里读。
    std::vector<std::wstring> parents;
    parents.push_back(cfg.dataDir);
    for (auto& kv : cfg.profiles) {
        if (kv.second.dataDir.empty()) continue;
        bool dup = false;
        for (auto& p : parents) { if (p == kv.second.dataDir) { dup = true; break; } }
        if (!dup) parents.push_back(kv.second.dataDir);
    }
    std::map<std::wstring, ProfileInfo> merged;
    for (auto& parent : parents) {
        if (parent.empty()) continue;
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW((parent + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (!n.empty() && (n[0] == L'.' || n[0] == L'_')) continue;
            if (merged.find(n) != merged.end()) continue;
            ProfileInfo p;
            p.name = n;
            p.path = parent + L"\\" + n;
            merged[n] = p;
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    std::vector<ProfileInfo> out;
    for (auto& kv : merged) {
        ProfileInfo p = kv.second;
        // 备注列：读该 profile 目录 ui 存档 remark（小文件本地读，无网络）
        p.remark = FpProfileRemark(p.path);
        auto it = procs.find(p.name);
        if (it != procs.end() && it->second.hProcess) {
            DWORD code = 0;
            if (::GetExitCodeProcess(it->second.hProcess, &code) && code == STILL_ACTIVE) {
                p.running = true;
                p.pid = it->second.pid;
            }
        }
        auto ip = ports.find(p.name);
        if (ip != ports.end()) p.port = ip->second;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(),
        [](const ProfileInfo& a, const ProfileInfo& b) { return a.name < b.name; });
    return out;
}

// 子进程 stdout/stderr 重定向到日志线程（管道），避免浏览器子进程阻塞。
// 返回值：true=CreateProcess 成功。
bool LaunchSunBrowser(const std::wstring& exe, const std::wstring& workDir,
    const std::wstring& cmdline, HANDLE* outProcess, DWORD* outPid, DWORD* outErr) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
    HANDLE hRead = NULL, hWrite = NULL;
    if (!::CreatePipe(&hRead, &hWrite, &sa, 0)) { *outErr = ::GetLastError(); return false; }
    ::SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    // 日志线程：把子进程输出逐行写入 debug.log
    // diag：统计行数/字节数/首行（GUI 子系统无控制台时零输出本身就是证据），
    // 结束时写 [browser-eof] 汇总行，便于与轮询退出码对齐。
    HANDLE hReadCopy = hRead;
    std::thread([hReadCopy]() {
        char buf[4096];
        DWORD got = 0;
        std::string pend;
        unsigned long nLines = 0, nBytes = 0;
        bool firstLogged = false;
        for (;;) {
            BOOL ok = ::ReadFile(hReadCopy, buf, sizeof(buf) - 1, &got, NULL);
            if (!ok || got == 0) break;
            buf[got] = 0;
            nBytes += got;
            pend += buf;
            size_t p = 0, q = 0;
            while ((q = pend.find('\n', p)) != std::string::npos) {
                std::string line = pend.substr(p, q - p);
                while (!line.empty() && (line.back() == '\r')) line.pop_back();
                nLines++;
                if (!firstLogged) {
                    LOG(L"[browser-first] " + W(line));
                    firstLogged = true;
                }
                LOG(L"[browser] " + W(line));
                p = q + 1;
            }
            pend = pend.substr(p);
        }
        if (!pend.empty()) {
            nLines++;
            if (!firstLogged) LOG(L"[browser-first] " + W(pend));
            LOG(L"[browser] " + W(pend));
        }
        DWORD gle = ::GetLastError();
        LOG(L"[browser-eof] lines=" + std::to_wstring(nLines) +
            L" bytes=" + std::to_wstring(nBytes) +
            L" gle=" + std::to_wstring(gle) +
            L"（零行零字节+gle=109/管道结束=GUI静默早退典型；有行先看[browser-first]）");
        ::CloseHandle(hReadCopy);
    }).detach();

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Chromium 子进程会建自己的控制台窗口；强制隐藏，只留浏览器主窗口
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = NULL;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + cmdline;
    LOG(L"CreateProcess exe=" + exe);
    LOG(L"CreateProcess workDir=" + workDir);
    LOG(L"CreateProcess cmd=" + cmd);
    BOOL ok = ::CreateProcessW(exe.c_str(), &cmd[0], NULL, NULL, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        NULL, workDir.c_str(), &si, &pi);
    ::CloseHandle(hWrite);
    if (!ok) {
        *outErr = ::GetLastError();
        ::CloseHandle(hRead);
        return false;
    }
    ::CloseHandle(pi.hThread);
    *outProcess = pi.hProcess;
    *outPid = pi.dwProcessId;
    LOG(L"CreateProcess OK pid=" + std::to_wstring(pi.dwProcessId));
    return true;
}
