// SunLauncher.h — C++ Win32 原生启动器（替代 Go 版）
// 单窗口：profile 列表 + 启动/关闭按钮 + 数据目录可改 + DEBUG 日志文件。
#pragma once

#define UNICODE
#define _UNICODE
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
static const wchar_t* kDefaultDataDir    = L"F:\\.ADSPOWER_GLOBAL\\cache";
static const wchar_t* kDefaultListen     = L"127.0.0.1:18900";
static const int      kDefaultPortBase   = 19222;

// ---------- 配置 ----------
struct Config {
    std::wstring sunBrowserDir = kDefaultBrowserDir;
    std::wstring dataDir       = kDefaultDataDir;
    std::wstring listen        = kDefaultListen;
    int          portBase      = kDefaultPortBase;
};

// ---------- profile 运行状态 ----------
struct ProfileInfo {
    std::wstring name;
    std::wstring path;
    bool         running = false;
    DWORD        pid = 0;
    int          port = 0;
};

struct ProcHandle {
    HANDLE hProcess = NULL;
    DWORD  pid = 0;
};

// ---------- DEBUG 日志（写文件 debug.log + 可选窗口回显） ----------
class DebugLog {
public:
    static DebugLog& Instance() {
        static DebugLog inst;
        return inst;
    }
    void Init(const std::wstring& dir) {
        std::lock_guard<std::mutex> lk(mu_);
        logPath_ = dir + L"\\debug.log";
        // 保留上一轮：改名为 debug.prev.log（只保留一轮，避免无限增长）
        ::DeleteFileW((dir + L"\\debug.prev.log").c_str());
        ::MoveFileW(logPath_.c_str(), (dir + L"\\debug.prev.log").c_str());
        WriteLocked(L"===== SunLauncher start =====");
    }
    void Write(const std::wstring& line) {
        std::lock_guard<std::mutex> lk(mu_);
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
    std::mutex mu_;
    std::wstring logPath_;
};

#define LOG(msg) DebugLog::Instance().Write(msg)

// ---------- util.cpp 实现的工具函数（声明在此，供 main.cpp 使用） ----------
std::wstring AppDir();
std::wstring W(const std::string& s);
std::string  N(const std::wstring& s);
Config LoadConfig();
bool SaveConfig(const Config& c);
std::map<std::wstring, int> LoadPorts();
void SavePorts(const std::map<std::wstring, int>& m);
bool PortFree(int port);
std::vector<ProfileInfo> ScanProfiles(const Config& cfg,
    const std::map<std::wstring, ProcHandle>& procs,
    const std::map<std::wstring, int>& ports);
bool LaunchSunBrowser(const std::wstring& exe, const std::wstring& workDir,
    const std::wstring& cmdline, HANDLE* outProcess, DWORD* outPid, DWORD* outErr);

// ---------- 全局状态 ----------
struct AppState {
    Config cfg;
    std::map<std::wstring, ProcHandle> procs; // profile -> 进程
    std::map<std::wstring, int>        ports; // profile -> 调试端口（持久化 ports.json）
    std::mutex mu;
    HWND hMain = NULL, hList = NULL, hDataDir = NULL, hBrowserDir = NULL, hLog = NULL, hStatus = NULL;
    HWND hSearch = NULL;   // 环境搜索框（对齐 web-ui globalSearch）
    std::wstring searchFilter; // 搜索关键字（刷新列表时过滤）
    std::map<std::wstring, bool> checked; // 多选勾选态（对齐 web-ui 表格 checkbox，批量操作用）
};
