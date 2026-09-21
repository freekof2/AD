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
    void WriteLocked(const std::wstring& line) {
        if (logPath_.empty()) return;
        std::wofstream f(logPath_, std::ios::app);
        // UTF-16LE 写出，记事本直接可读
        f.imbue(std::locale(f.getloc(),
            new std::codecvt_utf16<wchar_t, 0x10FFFF, std::little_endian>));
        f << timestamp() << L" " << line << L"\r\n";
    }
    std::mutex mu_;
    std::wstring logPath_;
};

#define LOG(msg) DebugLog::Instance().Write(msg)
static std::wstring W(const std::string& s);
static std::string  N(const std::wstring& s);

// ---------- 全局状态 ----------
struct AppState {
    Config cfg;
    std::map<std::wstring, ProcHandle> procs; // profile -> 进程
    std::map<std::wstring, int>        ports; // profile -> 调试端口（持久化 ports.json）
    std::mutex mu;
    HWND hMain = NULL, hList = NULL, hDataDir = NULL, hBrowserDir = NULL, hLog = NULL, hStatus = NULL;
};
