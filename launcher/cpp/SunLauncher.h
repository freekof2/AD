// SunLauncher.h — C++ Win32 原生启动器（替代 Go 版）
// 单窗口：profile 列表 + 启动/关闭按钮 + 数据目录可改 + DEBUG 日志文件。
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <codecvt>
#include <locale>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <algorithm>

// ---------- 默认值 ----------
static const wchar_t* kDefaultBrowserDir = L"C:\\Users\\admin6\\AppData\\Roaming\\adspower_global\\cwd_global\\chrome_152";
// 数据目录的运行时默认值是 **SunLauncher.exe 同目录的 cache**（见 util.cpp DefaultDataDir，
// LoadConfig 启动即创建该目录与 config.json）；此处常量仅作 Config 结构体的占位兜底，
// 启动后一律被 LoadConfig / config.json 的实际值覆盖。
static const wchar_t* kDefaultDataDir    = L"F:\\.ADSPOWER_GLOBAL\\cache";
// 注：HTTP 离线接口（18900 + /api/*）已整体删除，不再开任何监听端口；
// listen/port_base 配置键一并移除（旧文件里的残留键读取时忽略、不再写回）。

// ---------- 配置 ----------
// 全局默认 + per-profile 覆盖（profiles.<name>.data_dir / .sun_browser_dir 为空即跟随全局）。
// 非官方设置（每个指纹独立的数据目录/浏览器目录）只存 sunlauncher.json 本地文件，
// 不进三件套、不进 --extended-parameters、不做任何网络 IO。
struct ProfileOverride {
    std::wstring dataDir;       // 为空=跟随全局 dataDir（profile 父目录）；非空=该指纹独立父目录
    std::wstring sunBrowserDir; // 为空=跟随全局；非空=该指纹独立浏览器目录
};
struct Config {
    std::wstring sunBrowserDir = kDefaultBrowserDir;
    std::wstring dataDir       = kDefaultDataDir;
    std::map<std::wstring, ProfileOverride> profiles; // profile 名 -> 独立目录覆盖
    bool debugLog = true; // Config.json 顶层 debug_log：true=写 debug.log，false=不写（主窗口开关）
};
// 取 profile 实际生效目录：覆盖优先，全局兜底。
inline std::wstring EffDataDir(const Config& c, const std::wstring& profile) {
    auto it = c.profiles.find(profile);
    if (it != c.profiles.end() && !it->second.dataDir.empty()) return it->second.dataDir;
    return c.dataDir;
}
inline std::wstring EffBrowserDir(const Config& c, const std::wstring& profile) {
    auto it = c.profiles.find(profile);
    if (it != c.profiles.end() && !it->second.sunBrowserDir.empty()) return it->second.sunBrowserDir;
    return c.sunBrowserDir;
}

// ---------- profile 运行状态 ----------
struct ProfileInfo {
    std::wstring name;
    std::wstring path;
    std::wstring remark; // ui_fingerprint.json 指纹备注（环境列表备注列显示）
    bool         running = false;
    DWORD        pid = 0;
    int          port = 0; // DevToolsActivePort 实际端口（官方随机；未运行/未生成=0）
};

struct ProcHandle {
    HANDLE hProcess = NULL;
    DWORD  pid = 0;
};

// ---------- DEBUG 日志（只写 debug.log 文件，主窗口无日志框） ----------
class DebugLog {
public:
    static DebugLog& Instance() {
        static DebugLog inst;
        return inst;
    }
    void Init(const std::wstring& dir, bool enabled) {
        std::lock_guard<std::mutex> lk(mu_);
        logPath_ = dir + L"\\debug.log";
        enabled_ = enabled;
        if (!enabled_) return; // 关：不动旧文件、不轮转、不写头部（本次运行全程静默）
        // 保留上一轮：改名为 debug.prev.log（只保留一轮，避免无限增长）
        ::DeleteFileW((dir + L"\\debug.prev.log").c_str());
        ::MoveFileW(logPath_.c_str(), (dir + L"\\debug.prev.log").c_str());
        WriteLocked(L"===== SunLauncher start =====");
    }
    // 主窗口开关（Config.json debug_log）：关=留一条关闭标记后全程不再写；开=留一条开启标记。
    void SetEnabled(bool on) {
        std::lock_guard<std::mutex> lk(mu_);
        if (enabled_ == on) return;
        if (on) { enabled_ = true; WriteLocked(L"===== 日志已开启 ====="); }
        else { WriteLocked(L"===== 日志已关闭（本次运行不再写入） ====="); enabled_ = false; }
    }
    bool Enabled() const {
        std::lock_guard<std::mutex> lk(mu_);
        return enabled_;
    }
    void Write(const std::wstring& line) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!enabled_) return; // 开关=关：不写任何内容（含子进程输出、diag）
        WriteLocked(line);
    }
    std::wstring Path() const { return logPath_; }

private:
    std::wstring timestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tmv{};
        localtime_s(&tmv, &t);
        wchar_t buf[64];
        wcsftime(buf, 64, L"%Y-%m-%d %H:%M:%S", &tmv);
        return buf;
    }
    static std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
        if (n <= 0) return {};
        std::string a((size_t)(n - 1), 0);
        ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &a[0], n, NULL, NULL);
        return a;
    }
    void WriteLocked(const std::wstring& line) {
        if (logPath_.empty()) return;
        // UTF-8 落盘：记事本/GUI/CI 全部直接可读，不再写 UTF-16（之前乱码根因）。
        std::ofstream f(logPath_, std::ios::app | std::ios::binary);
        if (!f) return;
        std::string u8 = ToUtf8(timestamp() + L" " + line + L"\r\n");
        f.write(u8.data(), (std::streamsize)u8.size());
    }
    mutable std::mutex mu_;   // enabled_ 由 Enabled() const 读取 -> mutable
    std::wstring logPath_;
    bool enabled_ = true;     // 日志总开关（Config.json debug_log / 主窗口复选框）
};

#define LOG(msg) DebugLog::Instance().Write(msg)

// ---------- util.cpp 实现的工具函数（声明在此，供 main.cpp 使用） ----------
std::wstring AppDir();
std::wstring W(const std::string& s);
std::string  N(const std::wstring& s);
Config LoadConfig();
bool SaveConfig(const Config& c);
// debug.log 开关（Config.json 顶层 debug_log，手改即生效）：
// ConfigDebugLogSwitch 读取（缺省=开；off/false/0/close=关），ConfigSetDebugLog 写回。
bool ConfigDebugLogSwitch();
bool ConfigSetDebugLog(bool on);
// 读 profile 目录 DevToolsActivePort 首行 = 浏览器实际调试端口（官方 buildLaunchOpt 只传
// --remote-debugging-port=0，端口由浏览器随机并写进该文件，puppeteer 同源读法）。
// waitMs>0 时按 100ms 轮询等待文件生成（启动后立即查需要等待）；读不到返回 0。
int ReadDevToolsPort(const std::wstring& profileDir, int waitMs = 0);
std::vector<ProfileInfo> ScanProfiles(const Config& cfg,
    const std::map<std::wstring, ProcHandle>& procs);
bool LaunchSunBrowser(const std::wstring& exe, const std::wstring& workDir,
    const std::wstring& cmdline, HANDLE* outProcess, DWORD* outPid, DWORD* outErr);

// ---------- 配色（对齐 web-ui/css/app.css :root + 参考图浅灰蓝底/白卡片/主蓝） ----------
static const COLORREF kUiBg      = RGB(245, 247, 250); // --bg #f5f7fa
static const COLORREF kUiPanel   = RGB(255, 255, 255); // --panel #ffffff
static const COLORREF kUiBorder  = RGB(226, 232, 240); // --border #e2e8f0
static const COLORREF kUiPrimary = RGB(45, 92, 246);   // --primary #2d5cf6
static const COLORREF kUiSuccess = RGB(16, 185, 129);  // --success #10b981
static const COLORREF kUiMuted   = RGB(148, 163, 184); // stopped gray #94a3b8
static const COLORREF kUiDanger  = RGB(239, 68, 68);   // --danger #ef4444
static const COLORREF kUiText    = RGB(26, 32, 44);    // --text-main #1a202c

// ---------- 全局状态 ----------
struct AppState {
    Config cfg;
    std::map<std::wstring, ProcHandle> procs; // profile -> 进程
    std::mutex mu;
    HWND hMain = NULL, hList = NULL, hStatus = NULL;
    HWND hSearch = NULL;   // 环境搜索框（对齐 web-ui globalSearch）
    std::wstring searchFilter; // 搜索关键字（刷新列表时过滤）
    std::map<std::wstring, bool> checked; // 多选勾选态（LISTVIEW 复选框镜像，批量操作用）
    HBRUSH hBgBrush = NULL;    // 主窗口底色画刷（kUiBg）
    HBRUSH hWhiteBrush = NULL; // 输入框/列表底色画刷（kUiPanel）
};
