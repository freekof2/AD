// util.cpp — 路径/JSON/DevTools端口/进程小工具 + DEBUG 日志落盘
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

// 极简 JSON（只够读 config，写用拼接，保证无第三方依赖）
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
        // 扫描到结束引号（跳过 \" 转义），取出原始串
        size_t q = p + 1;
        while (q < json.size()) {
            if (json[q] == L'"') break;
            if (json[q] == L'\\' && q + 1 < json.size()) { q += 2; continue; }
            q++;
        }
        if (q >= json.size()) return L"";
        std::wstring raw = json.substr(p + 1, q - p - 1);
        // SaveConfig 用 Esc() 写出标准 JSON（\\ 转义），读侧必须反转义，
        // 否则 dataDir 读回 F:\\.ADSPOWER... 双反斜杠：路径展示错、A2 目录错、
        // 从目录导入/打开指纹全失效（09-29 实测根因）。
        // 只在确实含 \\ 或 \" 时反转义，保留旧手写“单反斜杠”文件的原样行为。
        if (raw.find(L"\\\\") == std::wstring::npos && raw.find(L"\\\"") == std::wstring::npos)
            return raw;
        std::wstring out;
        for (size_t i = 0; i < raw.size(); i++) {
            if (raw[i] == L'\\' && i + 1 < raw.size()) {
                wchar_t c = raw[i + 1];
                if (c == L'\\') out += L'\\';
                else if (c == L'"') out += L'"';
                else if (c == L'/') out += L'/';
                else if (c == L'n') out += L'\n';
                else if (c == L'r') out += L'\r';
                else if (c == L't') out += L'\t';
                else { out += L'\\'; out += c; }
                i++;
            } else out += raw[i];
        }
        return out;
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

static std::wstring Esc(const std::wstring& s) {
    std::wstring o;
    for (auto ch : s) {
        if (ch == L'\\') o += L"\\\\";
        else if (ch == L'"') o += L"\\\"";
        else o += ch;
    }
    return o;
}

// 默认数据目录 = SunLauncher.exe 同目录的 cache；首次运行自动创建目录。
static std::wstring DefaultDataDir() { return ExeDir() + L"\\cache"; }

// 启动时检查 exe 同目录有没有 config.json：没有就按初始参数创建，
// 初始参数只有 cache 目录位置（顶层 data_dir）。已有文件保持不动（手改即生效）。
static void EnsureConfigJson() {
    const std::wstring path = ExeDir() + L"\\Config.json";
    if (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    const std::wstring dataDir = DefaultDataDir();
    ::CreateDirectoryW(dataDir.c_str(), NULL);
    try {
        std::wstring j = L"{\r\n  \"data_dir\": \"" + Esc(dataDir) + L"\"\r\n}\r\n";
        std::wofstream f(path);
        if (!f) { LOG(L"Config.json 创建失败（目录不可写）path=" + path); return; }
        f.imbue(std::locale(f.getloc(), new std::codecvt_utf8<wchar_t>));
        f << j;
        f.flush();
        if (f) LOG(L"已创建 Config.json 初始参数 data_dir=" + dataDir);
        else   LOG(L"Config.json 创建失败（写盘失败）path=" + path);
    } catch (...) { LOG(L"Config.json 创建异常 path=" + path); }
}

// Config.json 写盘（wofstream + utf8，与 EnsureConfigJson 同法）
static bool WriteConfigFile(const std::wstring& path, const std::wstring& text) {
    try {
        std::wofstream f(path);
        if (!f) { LOG(L"Config.json 写入失败（目录不可写）path=" + path); return false; }
        f.imbue(std::locale(f.getloc(), new std::codecvt_utf8<wchar_t>));
        f << text;
        f.flush();
        if (!f) { LOG(L"Config.json 写入失败（写盘失败）path=" + path); return false; }
        return true;
    } catch (...) { LOG(L"Config.json 写入异常 path=" + path); return false; }
}

// debug.log 开关：Config.json 顶层 debug_log（手改即生效，主窗口底部复选框读写同一键）。
// 关 = off/false/0/close（不区分大小写）；键缺失或其它值 = 开（默认记录）。
bool ConfigDebugLogSwitch() {
    const std::wstring v = JsonGet(ReadFileW(ExeDir() + L"\\Config.json"), L"debug_log");
    if (v.empty()) return true;
    std::wstring t;
    for (wchar_t c : v) t += (wchar_t)towlower(c);
    return !(t == L"off" || t == L"false" || t == L"0" || t == L"close");
}

// 写回顶层 debug_log：键已存在就改它的字符串值，不存在就插在第一个 { 之后；
// 其余键（data_dir / start_url / 各指纹对象）原样保留。返回写盘是否成功。
bool ConfigSetDebugLog(bool on) {
    const std::wstring path = ExeDir() + L"\\Config.json";
    std::wstring txt = ReadFileW(path);
    if (txt.empty()) { EnsureConfigJson(); txt = ReadFileW(path); if (txt.empty()) return false; }
    const std::wstring pat = L"\"debug_log\"";
    const std::wstring nv = on ? L"\"on\"" : L"\"off\"";
    std::wstring out;
    const size_t k = txt.find(pat);
    if (k != std::wstring::npos) {
        size_t c = txt.find(L':', k + pat.size());
        size_t v = (c == std::wstring::npos) ? std::wstring::npos
                                             : txt.find_first_not_of(L" \t\r\n", c + 1);
        if (v == std::wstring::npos || txt[v] != L'"') return false; // 值不是字符串：不动文件
        size_t e = txt.find(L'"', v + 1);
        if (e == std::wstring::npos) return false;
        out = txt.substr(0, v) + nv + txt.substr(e + 1);
    } else {
        const size_t b = txt.find(L'{');
        if (b == std::wstring::npos) return false;
        size_t i = b + 1;
        while (i < txt.size() && iswspace(txt[i])) i++;
        const bool emptyObj = (i < txt.size() && txt[i] == L'}');
        out = txt.substr(0, b + 1) + (emptyObj ? L"" : L"\r\n  ") + L"\"debug_log\": " + nv +
              (emptyObj ? L"" : L",") + txt.substr(b + 1);
    }
    if (out == txt) return true;
    return WriteConfigFile(path, out);
}

// Config.json 按指纹存目录（手改即生效）：
//   { "<env名>": { "data_dir": "F:\\.ADSPOWER_GLOBAL\\cache\\k1ds12lu_hyg6dd",
//                  "sun_browser_dir": "C:\\...\\chrome_152", "webrtc_ip": "1.2.3.4" },
//     "<旧env名>": "1.2.3.4" }          // 旧写法：纯字符串=只有伪装 IP，无目录语义
// 语义：只对键名对应的那一个指纹生效（不写=回落 sunlauncher.json）。
// data_dir 按你的约定存**完整指纹目录**，但内部统一转成父目录存进 profiles
//（EffDataDir 的约定是父目录，所有调用方都是 EffDataDir + "\" + 环境名）。
static void ApplyConfigJsonProfiles(Config& c) {
    std::wstring cfgTxt = ReadFileW(ExeDir() + L"\\Config.json");
    if (cfgTxt.empty()) return;
    auto trimSep = [](std::wstring s) {
        while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
        return s;
    };
    // 扁平对象扫描：一次顶层键 + 其值（对象/字符串），配置无嵌套，够用。
    // 顶层两个字符串键是**全局**参数（与 profile 对象里的同名键不同层级）：
    //   顶层 data_dir = 数据父目录（默认 <exe目录>\cache），顶层 sun_browser_dir = 浏览器目录。
    // 手改这两个值即生效（优先级高于 sunlauncher.json；单指纹 profile 对象优先级更高）。
    std::wstring gData, gBrowser;
    size_t p = cfgTxt.find(L'{');
    if (p == std::wstring::npos) return;
    p++;
    while (p < cfgTxt.size()) {
        while (p < cfgTxt.size() && (iswspace(cfgTxt[p]) || cfgTxt[p] == L',')) p++;
        if (p >= cfgTxt.size() || cfgTxt[p] == L'}') break;
        if (cfgTxt[p] != L'"') break;
        size_t q2 = cfgTxt.find(L'"', p + 1);
        if (q2 == std::wstring::npos) break;
        const std::wstring name = cfgTxt.substr(p + 1, q2 - p - 1);
        size_t colon = cfgTxt.find(L':', q2);
        if (colon == std::wstring::npos) break;
        size_t v = cfgTxt.find_first_not_of(L" \t\r\n", colon + 1);
        if (v >= cfgTxt.size()) break;
        if (cfgTxt[v] == L'"') {
            // 字符串值：顶层 data_dir / sun_browser_dir 取为全局参数；
            // 其余是旧写法 profile 的伪装 IP（无目录语义，跳过）。
            size_t e = v + 1;
            std::wstring val;
            while (e < cfgTxt.size()) {
                if (cfgTxt[e] == L'\\') {
                    if (e + 1 >= cfgTxt.size()) break;
                    if (cfgTxt[e + 1] == L'\\') val += L'\\';
                    else { val += L'\\'; val += cfgTxt[e + 1]; } // 兼容旧手写单反斜杠
                    e += 2; continue;
                }
                if (cfgTxt[e] == L'"') break;
                val += cfgTxt[e++];
            }
            if (name == L"data_dir" && !val.empty()) gData = trimSep(val);
            else if (name == L"sun_browser_dir" && !val.empty()) {
                size_t i = val.find_last_of(L"\\/");
                if (i != std::wstring::npos) {
                    std::wstring tail = val.substr(i + 1);
                    for (auto& ch : tail) ch = towlower(ch);
                    if (tail == L"sunbrowser.exe") val = val.substr(0, i);
                }
                gBrowser = val;
            }
            p = (e < cfgTxt.size()) ? e + 1 : cfgTxt.size();
            continue;
        }
        if (cfgTxt[v] != L'{') break;
        size_t e = cfgTxt.find(L'}', v);
        if (e == std::wstring::npos) break;
        const std::wstring blk = cfgTxt.substr(v, e - v + 1);
        std::wstring d = trimSep(JsonGet(blk, L"data_dir"));
        std::wstring b = trimSep(JsonGet(blk, L"sun_browser_dir"));
        // data_dir 完整指纹目录 -> 去掉末尾环境名一层，还原成父目录
        if (!d.empty()) {
            size_t i = d.find_last_of(L"\\/");
            if (i != std::wstring::npos && d.substr(i + 1) == name) d = d.substr(0, i);
        }
        // 手改容错：粘成 SunBrowser.exe 完整路径则取父目录（参数语义是目录）
        if (!b.empty()) {
            size_t i = b.find_last_of(L"\\/");
            if (i != std::wstring::npos) {
                std::wstring tail = b.substr(i + 1);
                for (auto& ch : tail) ch = towlower(ch);
                if (tail == L"sunbrowser.exe") b = b.substr(0, i);
            }
        }
        if (!d.empty() || !b.empty()) {
            ProfileOverride& o = c.profiles[name]; // 缺的字段保留 sunlauncher.json 里的值
            if (!d.empty()) o.dataDir = d;
            if (!b.empty()) o.sunBrowserDir = b;
            LOG(L"Config.json 指纹目录 " + name + L" dataParent=" +
                (d.empty() ? L"(未设)" : d) + L" browserDir=" +
                (b.empty() ? L"(未设)" : b));
        }
        p = e + 1;
    }
    // 顶层全局参数最后落（本函数在 sunlauncher.json 解析之后调用，故 Config.json 优先）
    if (!gData.empty()) { c.dataDir = gData; LOG(L"Config.json 全局 dataDir=" + gData); }
    if (!gBrowser.empty()) { c.sunBrowserDir = gBrowser; LOG(L"Config.json 全局 browserDir=" + gBrowser); }
}

Config LoadConfig() {
    // 先补齐缺失的 config.json（初始参数=cache 目录），再读配置。
    EnsureConfigJson();
    Config c;
    // debug.log 开关与主窗口复选框同源（Config.json debug_log），启动即生效
    c.debugLog = ConfigDebugLogSwitch();
    // 默认数据目录：SunLauncher.exe 同目录的 cache（config.json/sunlauncher.json 有值则覆盖）
    c.dataDir = DefaultDataDir();
    ::CreateDirectoryW(c.dataDir.c_str(), NULL); // 默认 cache 目录随程序自动创建
    std::wstring txt = ReadFileW(ExeDir() + L"\\sunlauncher.json");
    if (txt.empty()) { ApplyConfigJsonProfiles(c); return c; }
    std::wstring v;
    if (!(v = JsonGet(txt, L"sun_browser_dir")).empty()) c.sunBrowserDir = v;
    if (!(v = JsonGet(txt, L"data_dir")).empty())        c.dataDir = v;
    // listen/port_base 已废弃（HTTP 接口整体删除）：旧文件残留键直接忽略，不再读回。
    // 端口不落盘：官方只传 --remote-debugging-port=0（浏览器随机，写 DevToolsActivePort），
    // 旧 sunlauncher.json 里的 port_base 键读取时直接忽略。
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
                if (nm == L"profiles") { p = ce + 1; continue; }
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
    // Config.json 按指纹覆盖（放最后：同一指纹优先于 sunlauncher.json 的 profiles 段）
    ApplyConfigJsonProfiles(c);
    return c;
}

bool SaveConfig(const Config& c) {
    // wofstream+codecvt 在异常路径/坏 locale 下可能抛；调用方已 try/catch，
    // 此处再加 nothrow 守卫：任何异常一律返回 false，不穿越持锁区（闪退根因之一）。
    try {
        std::wstring j = L"{\r\n  \"sun_browser_dir\": \"" + Esc(c.sunBrowserDir) +
            L"\",\r\n  \"data_dir\": \"" + Esc(c.dataDir) + L"\"";
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

// DevToolsActivePort：官方 buildLaunchOpt 只传 --remote-debugging-port=0，端口由浏览器
// 自己随机选并写进 <profile>\DevToolsActivePort（首行端口、次行 ws 路径；puppeteer 同源读法）。
// 不再有 ports.json 端口表，也不存在"端口不够用"的问题。waitMs>0 时按 100ms 轮询等它生成。
int ReadDevToolsPort(const std::wstring& profileDir, int waitMs) {
    if (profileDir.empty()) return 0;
    std::wstring path = profileDir + L"\\DevToolsActivePort";
    for (int waited = 0; ; waited += 100) {
        std::wstring txt = ReadFileW(path);
        size_t s = 0;
        while (s < txt.size() && (txt[s] < L'0' || txt[s] > L'9')) s++;
        size_t e = s;
        while (e < txt.size() && txt[e] >= L'0' && txt[e] <= L'9') e++;
        if (e > s) {
            int port = _wtoi(txt.substr(s, e - s).c_str());
            if (port > 0 && port < 65536) return port;
        }
        if (waited >= waitMs) return 0;
        ::Sleep(100);
    }
}

std::vector<ProfileInfo> ScanProfiles(const Config& cfg,
    const std::map<std::wstring, ProcHandle>& procs) {
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
        // 实际端口只在运行中才读（DevToolsActivePort 由本次浏览器随机写入；已停=0）
        if (p.running) p.port = ReadDevToolsPort(p.path);
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
