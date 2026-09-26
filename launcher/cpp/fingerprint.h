// fingerprint.h — 离线指纹注入模块（SunLauncher）
// 对应 main.min.js v2.8.8.7 的指纹机制，只读/写本地缓存目录，不做任何网络 IO。
// 三个核心事实（k1h60tsv_hyg6dd 实测验证）：
//   1. 缓存目录名 = <fbccId>_<inviteCode>；fbccId 既是环境 ID 也是全部噪声的确定性种子
//   2. 目录下 md5(fbccId+"_static") / md5(fbccId+"_webrtc") / md5(fbccId+"_cookies")
//      三个文件 = staticConfig / dynamicConfig / CookiesFile（自定义换表 Base64 编码）
//   3. 启动 = spawn SunBrowser.exe + --user-data-dir=<缓存目录>
//      + --extended-parameters=<换表Base64(JSON(sunBrowserParams))>
//      其中 sunBrowserParams.StaticConfig/DynamicConfig/CookiesFile 是上述三个文件的绝对路径
#pragma once
#include "SunLauncher.h"
#include <cstdlib>   // strtol
#include <cwctype>   // towlower
#include <cstring>   // memcpy

// ---------- 换表 Base64（main.min.js encodeBase64/decodeBase64 原样移植） ----------
// C1 = 标准表，C2 = AdsPower 自定义表；encode: 标准->换表；decode: 换表->标准
extern const wchar_t* FP_C1;
extern const wchar_t* FP_C2;
std::string FpStdBase64Encode(const std::string& raw);   // 标准 base64（无第三方依赖）
std::string FpStdBase64Decode(const std::string& b64);   // 失败返回 ""
std::string FpEncode(const std::string& rawJson);        // 标准编码后 C1->C2 换表
std::string FpDecode(const std::string& mapped);         // C2->C1 还原后标准解码

// ---------- md5（RFC1321 自包含实现，fingerprint.cpp 内） ----------
std::string FpMd5Hex(const std::string& s);              // 小写 hex，与官方一致
// 约定文件名：md5(fbccId+"_static") / md5(fbccId+"_webrtc") / md5(fbccId+"_cookies")
std::string FpStaticName(const std::string& fbccId);
std::string FpDynamicName(const std::string& fbccId);
std::string FpCookiesName(const std::string& fbccId);

// ---------- 缓存 profile 读写 ----------
// profile 目录名形如 k1h60tsv_hyg6dd；fbccId = 下划线前段
std::string FpFbccIdOf(const std::wstring& profileDirName);  // "k1h60tsv_hyg6dd" -> "k1h60tsv"
bool FpReadTextFile(const std::wstring& path, std::string& out);
bool FpWriteTextFile(const std::wstring& path, const std::string& data);

// 读三件套并解码为 JSON 文本（""=缺失/损坏）。调用方负责 JSON 解析。
bool FpLoadStaticJson(const std::wstring& profileDir, std::string& jsonOut);
bool FpLoadDynamicJson(const std::wstring& profileDir, std::string& jsonOut);
bool FpLoadCookiesJson(const std::wstring& profileDir, std::string& jsonOut);
// 保存：JSON 文本 -> 换表编码 -> 写 md5 文件。写前做 md5(内容)比对，一致则跳过（与官方逻辑一致）。
bool FpSaveStaticJson(const std::wstring& profileDir, const std::string& jsonText);
bool FpSaveDynamicJson(const std::wstring& profileDir, const std::string& jsonText);

// ---------- 极简 JSON 键值读写（只够 static/dynamic 的顶层标量/小对象，无第三方依赖） ----------
// 取顶层 string/number/bool/object/array 的原始文本（含引号/括号）；缺失返回 ""。
std::string FpJsonGet(const std::string& json, const std::string& key);
// 设置顶层键：valueRaw 为已序列化的 JSON 片段（如 "\"Win32\"" / "16" / "{\"a\":1}"）。
// key 不存在则追加到尾部对象。返回新 JSON，失败返回 ""。
std::string FpJsonSet(const std::string& json, const std::string& key, const std::string& valueRaw);

// CookiesFile 读/写（明文 JSON 数组，官方 setCookie 即 writeFile 明文；读兼容旧换表编码）。
// 写前做 md5(内容)比对，一致则跳过。
bool FpSaveCookiesJson(const std::wstring& profileDir, const std::string& jsonText);

// ---------- UI 侧车 ui_fingerprint.json（明文 JSON，非换表编码） ----------
// UI 35+ 参数全量存档。启动时作为 extra 传入 FpBuildCmdline，其中保护键被丢弃（缓存为准）。
bool FpLoadUiExtra(const std::wstring& profileDir, std::string& jsonOut);
bool FpSaveUiExtra(const std::wstring& profileDir, const std::string& jsonText);

// ---------- 启动参数组装 ----------
//   --user-data-dir="<profileDir>" --profile-directory=Default
//   --remote-debugging-port=<port> --no-first-run --no-default-browser-check
//   --extended-parameters=<FpEncode(JSON(sunBrowserParams))>
//   --enable-logging=stderr --v=0 about:blank
// sunBrowserParams 最小集：UserId + StaticConfig/DynamicConfig/CookiesFile 三个绝对路径
// + 调用方附加的 extraJson（顶层合并，冲突时 extra 覆盖）。
std::wstring FpBuildCmdline(const std::wstring& profileDir, int port,
    const std::string& extraSunParamsJson);

// FpCmdTooLong：诊断用，ext/命令行是否超限（CreateProcess 上限 32767，超限即 err=206）。
// 返回 true=超限，lenOut=ext 值长度。调用方在 CreateProcess 前检查，超限直接记日志拒绝启动。
bool FpCmdTooLong(const std::wstring& cmdline, size_t& lenOut);

// ---------- 启动诊断（只写 debug.log，不做任何网络 IO） ----------
// FpDiagDumpLaunch：把一次启动的全部现场逐行写入 debug.log（调用方直接 LOG 整块文本）：
//   [diag] exe/workDir/profileDir/port/pid 三件套文件名+长度+md5(raw)+解码头 64 字符/
//   sunBrowserParams 明文全文/ext 首尾各 64 字符+长度/UserId 来源、
//   --user-data-dir/--extended-parameters/--remote-debugging-port 三键逐项展开、
//   环境变量 AUTH/ELECTRON_RUN_AS_NODE 有无（官方 filterEnv 会删）、
//   失败建议（3 秒退出且零 [browser] 输出 -> 手工复现命令）。
// 注意：三件套解码只取头 64 字符，避免指纹全文落盘。
std::string FpDiagDumpLaunch(const std::wstring& exe, const std::wstring& workDir,
    const std::wstring& profileDir, int port,
    const std::string& extraSunParamsJson,
    const std::wstring& cmdline, DWORD pid);
// FpDiagEnvAuth：检查当前进程环境 AUTH / ELECTRON_RUN_AS_NODE 是否存在。
// 存在且非空 = 官方 filterEnv 会删除但我们透传，返回 true（仅诊断，不删除）。
bool FpDiagEnvAuth(std::string& detailOut);

// ---------- 进程树关闭 ----------
// 先找 --user-data-dir 指向该 profile 的 SunBrowser 进程（Toolhelp 快照比对命令行），
// 找不到再退回结束 launcher 自己拉起的句柄。返回实际结束的 pid 列表。
std::vector<DWORD> FpKillProfileTree(const std::wstring& profileDir);
