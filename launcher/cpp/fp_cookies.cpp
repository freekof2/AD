// fp_cookies.cpp — Chromium cookie 库读写 + DPAPI/AES-GCM
// 只动 `<profile>\Default\Network\Cookies` 与读 `Local State` 的 os_crypt 密钥，
// 不碰 History / Login Data 等其它库；写前先备份，写后重读校验。
// 纯函数（规范化 / 明文转换 / 时间与 sameSite 映射）在 fp_cookies.h，便于单测。
#include "fp_cookies.h"

// 顺序要求：先 fingerprint.h（其 SunLauncher.h 会先定义 WIN32_LEAN_AND_MEAN 再按正确顺序
// 引入 windows.h/winsock2.h），再 wincrypt/bcrypt；反序会触发 winsock 头冲突。
#include "fingerprint.h"
#include <wincrypt.h>
#include <bcrypt.h>
#include <stdio.h>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "third_party/sqlite/sqlite3.h"

// ================= 小工具 =================
static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return std::string();
    std::string s((size_t)n, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
static std::wstring Utf8ToW(const char* p, int n) {
    if (!p || n <= 0) return std::wstring();
    int m = ::MultiByteToWideChar(CP_UTF8, 0, p, n, NULL, 0);
    if (m <= 0) return std::wstring();
    std::wstring s((size_t)m, 0);
    ::MultiByteToWideChar(CP_UTF8, 0, p, n, &s[0], m);
    return s;
}
static std::string FileBytes(const std::wstring& path, bool& okOut) {
    okOut = false;
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 64 * 1024 * 1024) {
        ::CloseHandle(h);
        return std::string();
    }
    std::string s((size_t)sz.QuadPart, 0);
    DWORD got = 0;
    BOOL r = ::ReadFile(h, &s[0], (DWORD)s.size(), &got, NULL);
    ::CloseHandle(h);
    if (!r) return std::string();
    s.resize(got);
    okOut = true;
    return s;
}
static bool CopyFileOver(const std::wstring& src, const std::wstring& dst) {
    return !!::CopyFileW(src.c_str(), dst.c_str(), FALSE);
}

// ================= DPAPI + AES-256-GCM =================
static bool UnprotectDpapi(const std::vector<unsigned char>& in, std::vector<unsigned char>& out) {
    if (in.empty()) return false;
    DATA_BLOB b{};
    b.pbData = (BYTE*)in.data();
    b.cbData = (ULONG)in.size();
    DATA_BLOB o{};
    if (!::CryptUnprotectData(&b, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &o)) return false;
    out.assign(o.pbData, o.pbData + o.cbData);
    ::LocalFree(o.pbData);
    return true;
}
// Local State -> os_crypt.encrypted_key（base64）-> 去 "DPAPI" 头 -> DPAPI 解出 AES 密钥。
// errOut 写具体失败环节，方便从 debug.log 直接定位（不要笼统地报“读不到密钥”）。
static bool LoadOsCryptKey(const std::wstring& localStatePath,
    std::vector<unsigned char>& keyOut, std::string& errOut) {
    bool ok = false;
    std::string txt = FileBytes(localStatePath, ok);
    if (!ok || txt.empty()) { errOut = "Local State 读不到或为空"; return false; }
    const std::string marker = "\"encrypted_key\"";
    size_t p = txt.find(marker);
    if (p == std::string::npos) { errOut = "Local State 里没有 os_crypt.encrypted_key"; return false; }
    // 顺序必须是“键名 -> 冒号 -> 值的开引号”。原来先找引号再找冒号，会跳到后面的键上，
    // 实测取到 is_biometric_available 之类字段，DPAPI 必失败（导入报“无法读取 cookie”）。
    size_t colon = txt.find(':', p + marker.size());
    if (colon == std::string::npos) { errOut = "encrypted_key 后找不到冒号"; return false; }
    size_t q1 = txt.find('"', colon);
    if (q1 == std::string::npos) { errOut = "encrypted_key 值缺少开引号"; return false; }
    size_t q2 = txt.find('"', q1 + 1);
    if (q2 == std::string::npos) { errOut = "encrypted_key 值缺少闭引号"; return false; }
    std::string b64 = txt.substr(q1 + 1, q2 - q1 - 1);
    if (b64.empty()) { errOut = "encrypted_key 值为空"; return false; }

    DWORD len = 0;
    if (!::CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64,
            NULL, &len, NULL, NULL) || len < 6) {
        errOut = "encrypted_key base64 解码失败";
        return false;
    }
    std::vector<unsigned char> blob(len);
    DWORD used = 0;
    if (!::CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64,
            blob.data(), &len, &used, NULL)) {
        errOut = "encrypted_key base64 解码失败";
        return false;
    }
    blob.resize(len);
    // 头 5 字节 = "DPAPI"
    if (blob.size() > 5 && blob[0] == 'D' && blob[1] == 'P' && blob[2] == 'A' &&
        blob[3] == 'P' && blob[4] == 'I')
        blob.erase(blob.begin(), blob.begin() + 5);
    if (!UnprotectDpapi(blob, keyOut)) {
        errOut = "DPAPI 解密 os_crypt 密钥失败（换机器/换账户后需要重新登录一次）";
        return false;
    }
    return true;
}

static bool AesGcmOpen(const std::vector<unsigned char>& key, BCRYPT_ALG_HANDLE* algOut,
    BCRYPT_KEY_HANDLE* keyOut) {
    *algOut = NULL;
    *keyOut = NULL;
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) return false;
    BCRYPT_ALG_HANDLE alg = NULL;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0)))
        return false;
    if (!BCRYPT_SUCCESS(::BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
            (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) {
        ::BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!BCRYPT_SUCCESS(::BCryptGenerateSymmetricKey(alg, &hKey, NULL, 0,
            (PUCHAR)key.data(), (ULONG)key.size(), 0))) {
        ::BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    *algOut = alg;
    *keyOut = hKey;
    return true;
}
static void AesGcmClose(BCRYPT_ALG_HANDLE alg, BCRYPT_KEY_HANDLE hKey) {
    if (hKey) ::BCryptDestroyKey(hKey);
    if (alg) ::BCryptCloseAlgorithmProvider(alg, 0);
}
// AES-GCM 参数块：SDK 的 BCRYPT_INIT_AUTHENTICATED_CIPHER_MODE_INFO 宏在部分头版本里
// 被 NTDDI 守卫裁掉（实测 MSVC C3861），这里手工填等价字段（dwInfoVersion 必须置 1）。
#ifndef BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION
#define BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION 1
#endif
static void InitGcmInfo(BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO& info,
    UCHAR* nonce, ULONG nonceLen, UCHAR* tag, ULONG tagLen) {
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.dwInfoVersion = BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION;
    info.pbNonce = nonce;
    info.cbNonce = nonceLen;
    info.pbTag = tag;
    info.cbTag = tagLen;
}
// blob = "v10"/"v11" + nonce(12) + ciphertext + tag(16)
static bool AesGcmDecryptBlob(const std::vector<unsigned char>& key,
    const unsigned char* blob, size_t blobLen, std::string& plainOut) {
    if (blobLen < 3 + 12 + 16) return false;
    if (!(blob[0] == 'v' && blob[1] == '1' && (blob[2] == '0' || blob[2] == '1'))) return false;
    const unsigned char* nonce = blob + 3;
    const size_t bodyLen = blobLen - 3 - 12;
    const size_t ctLen = bodyLen - 16;
    const unsigned char* ct = blob + 3 + 12;
    const unsigned char* tag = blob + 3 + 12 + ctLen;

    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!AesGcmOpen(key, &alg, &hKey)) return false;

    UCHAR iv[12];
    memcpy(iv, nonce, 12);
    UCHAR tagBuf[16];
    memcpy(tagBuf, tag, 16);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    InitGcmInfo(info, iv, sizeof(iv), tagBuf, sizeof(tagBuf));

    std::vector<UCHAR> out(ctLen ? ctLen : 1);
    ULONG outLen = 0;
    NTSTATUS st = ::BCryptDecrypt(hKey, (PUCHAR)ct, (ULONG)ctLen, &info, NULL, 0,
        out.data(), (ULONG)out.size(), &outLen, 0);
    AesGcmClose(alg, hKey);
    if (!BCRYPT_SUCCESS(st)) return false;
    plainOut.assign((const char*)out.data(), outLen);
    return true;
}
static bool AesGcmEncryptBlob(const std::vector<unsigned char>& key, const std::string& plain,
    std::vector<unsigned char>& blobOut) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!AesGcmOpen(key, &alg, &hKey)) return false;

    UCHAR iv[12];
    // 随机 nonce：用 CryptoAPI 的强随机（浏览器同款做法）
    HCRYPTPROV prov = 0;
    bool rndOk = ::CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        ::CryptGenRandom(prov, 12, iv) != 0;
    if (rndOk) ::CryptReleaseContext(prov, 0);
    if (!rndOk) {
        AesGcmClose(alg, hKey);
        return false;
    }

    UCHAR tag[16] = {};
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    InitGcmInfo(info, iv, sizeof(iv), tag, sizeof(tag));

    std::vector<UCHAR> ct(plain.size() ? plain.size() : 1);
    ULONG ctLen = 0;
    NTSTATUS st = ::BCryptEncrypt(hKey, (PUCHAR)plain.data(), (ULONG)plain.size(), &info,
        NULL, 0, ct.data(), (ULONG)ct.size(), &ctLen, 0);
    AesGcmClose(alg, hKey);
    if (!BCRYPT_SUCCESS(st)) return false;

    blobOut.clear();
    blobOut.push_back('v'); blobOut.push_back('1'); blobOut.push_back('0');
    blobOut.insert(blobOut.end(), iv, iv + 12);
    blobOut.insert(blobOut.end(), ct.begin(), ct.begin() + ctLen);
    blobOut.insert(blobOut.end(), tag, tag + 16);
    return true;
}

// ================= 进程/路径 =================
bool FpCookiesBrowserRunning(const std::wstring& profileDir) {
    return !FpProfileBrowserProcesses(profileDir).empty();
}
static std::wstring CookiesDbPath(const std::wstring& profileDir) {
    return profileDir + L"\\Default\\Network\\Cookies";
}
static std::wstring LocalStatePath(const std::wstring& profileDir) {
    return profileDir + L"\\Local State";
}


// ================= sqlite 封装 =================
struct SqliteDb {
    sqlite3* db = nullptr;
    ~SqliteDb() { if (db) ::sqlite3_close(db); }
    bool open(const std::wstring& path, int flags, std::string& errOut) {
        std::string u8 = WToUtf8(path);
        int rc = ::sqlite3_open_v2(u8.c_str(), &db, flags, NULL);
        if (rc != SQLITE_OK) {
            errOut = db ? ::sqlite3_errmsg(db) : "sqlite3_open_v2 failed";
            if (db) { ::sqlite3_close(db); db = nullptr; }
            return false;
        }
        ::sqlite3_busy_timeout(db, 8000);
        return true;
    }
};
static bool SqlExec(sqlite3* db, const char* sql, std::string& errOut) {
    char* em = NULL;
    int rc = ::sqlite3_exec(db, sql, NULL, NULL, &em);
    if (rc != SQLITE_OK) {
        errOut = em ? em : "exec failed";
        if (em) ::sqlite3_free(em);
        return false;
    }
    return true;
}
static void BindText(sqlite3_stmt* st, int idx, const std::string& v) {
    ::sqlite3_bind_text(st, idx, v.data(), (int)v.size(), SQLITE_TRANSIENT);
}

// ================= 导入：浏览器库 -> 明文 JSON =================
bool FpCookiesImportFromBrowser(const std::wstring& profileDir, std::string& jsonOut,
    int* skippedOut, std::string& errOut) {
    jsonOut.clear();
    errOut.clear();
    int skipped = 0;
    if (skippedOut) *skippedOut = 0;

    const std::wstring dbPath = CookiesDbPath(profileDir);
    DWORD attr = ::GetFileAttributesW(dbPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        errOut = "该指纹还没有浏览器 cookie 库（Default\\Network\\Cookies 不存在）";
        return false;
    }
    std::vector<unsigned char> key;
    if (!LoadOsCryptKey(LocalStatePath(profileDir), key, errOut)) {
        if (errOut.empty()) errOut = "读不到 Local State 的 os_crypt 密钥，无法解密 cookie";
        return false;
    }

    // 复制到临时目录再读（浏览器可能正开着，直接打开会遇到锁/读到半截）
    wchar_t tmpDirBuf[MAX_PATH] = {};
    if (!::GetTempPathW(MAX_PATH, tmpDirBuf)) { errOut = "GetTempPath 失败"; return false; }
    std::wstring tmpDir = std::wstring(tmpDirBuf) + L"sunlauncher_ck_";
    tmpDir += std::to_wstring(::GetCurrentProcessId());
    ::CreateDirectoryW(tmpDir.c_str(), NULL);
    std::wstring tmpDb = tmpDir + L"\\db.sqlite";
    bool copied = CopyFileOver(dbPath, tmpDb);
    if (copied) {
        std::wstring j = dbPath + L"-journal";
        if (::GetFileAttributesW(j.c_str()) != INVALID_FILE_ATTRIBUTES)
            CopyFileOver(j, tmpDb + L"-journal");
    }
    if (!copied) {
        errOut = "复制 cookie 库到临时目录失败（权限/占用）";
        return false;
    }

    bool ok = false;
    do {
        SqliteDb s;
        if (!s.open(tmpDb, SQLITE_OPEN_READONLY, errOut)) break;
        const char* kSql =
            "SELECT host_key,name,value,encrypted_value,path,is_secure,is_httponly,"
            "expires_utc,has_expires,samesite FROM cookies";
        sqlite3_stmt* st = NULL;
        if (::sqlite3_prepare_v2(s.db, kSql, -1, &st, NULL) != SQLITE_OK) {
            errOut = ::sqlite3_errmsg(s.db);
            break;
        }
        std::string out = "[";
        bool first = true;
        while (::sqlite3_step(st) == SQLITE_ROW) {
            const char* host = (const char*)::sqlite3_column_text(st, 0);
            const char* name = (const char*)::sqlite3_column_text(st, 1);
            const char* value = (const char*)::sqlite3_column_text(st, 2);
            const void* ev = ::sqlite3_column_blob(st, 3);
            int evLen = ::sqlite3_column_bytes(st, 3);
            const char* path = (const char*)::sqlite3_column_text(st, 4);
            int isSecure = ::sqlite3_column_int(st, 5);
            int isHttpOnly = ::sqlite3_column_int(st, 6);
            long long expiresUtc = ::sqlite3_column_int64(st, 7);
            int hasExpires = ::sqlite3_column_int(st, 8);
            int sameSite = ::sqlite3_column_int(st, 9);

            if (!host || !name || !*name) { skipped++; continue; }

            std::string val;
            bool decrypted = false;
            if (ev && evLen > 0) {
                const unsigned char* p = (const unsigned char*)ev;
                if (evLen >= 3 && p[0] == 'v' && p[1] == '1' && (p[2] == '0' || p[2] == '1')) {
                    if (AesGcmDecryptBlob(key, p, (size_t)evLen, val)) decrypted = true;
                } else {
                    // 非 v10/v11 前缀：极少数旧格式用明文 value 列
                    decrypted = false;
                }
            }
            if (!decrypted) {
                if (value && *value) { val = value; decrypted = true; }
            }
            if (!decrypted) { skipped++; continue; }

            const bool session = (hasExpires == 0 || expiresUtc <= 0);
            const long long unixExp = session ? 0 : FpCookiesUnixFromChromeUs(expiresUtc);

            if (!first) out += ",";
            first = false;
            out += "{\"name\":\"" + FpCookieEscape(name) + "\"";
            out += ",\"value\":\"" + FpCookieEscape(val) + "\"";
            out += ",\"domain\":\"" + FpCookieEscape(host) + "\"";
            out += ",\"path\":\"" + FpCookieEscape(path ? path : "/") + "\"";
            out += std::string(",\"httpOnly\":") + (isHttpOnly ? "true" : "false");
            out += std::string(",\"secure\":") + (isSecure ? "true" : "false");
            out += std::string(",\"session\":") + (session ? "true" : "false");
            if (!session) out += ",\"expires\":" + std::to_string(unixExp);
            out += ",\"sameSite\":\"" + FpCookiesSameSiteToStr(sameSite) + "\"";
            out += std::string(",\"hostOnly\":") +
                   ((!host || host[0] != '.') ? "true" : "false");
            out += "}";
        }
        ::sqlite3_finalize(st);
        out += "]";
        jsonOut = FpNormalizeCookiesJson(out);
        if (jsonOut.empty()) { errOut = "规范化失败（cookie 库内容异常）"; break; }
        ok = true;
    } while (false);

    ::DeleteFileW(tmpDb.c_str());
    ::DeleteFileW((tmpDb + L"-journal").c_str());
    ::RemoveDirectoryW(tmpDir.c_str());
    if (!ok && errOut.empty()) errOut = "读取 cookie 库失败";
    if (skippedOut) *skippedOut = skipped;
    return ok;
}

// ================= 写库：明文 JSON -> Chromium cookie =================
bool FpCookiesWriteToBrowser(const std::wstring& profileDir, const std::string& jsonArr,
    int* writtenOut, int* skippedOut, int* removedOut, std::string& errOut) {
    int written = 0, skipped = 0, removed = 0;
    if (writtenOut) *writtenOut = 0;
    if (skippedOut) *skippedOut = 0;
    if (removedOut) *removedOut = 0;
    errOut.clear();

    if (FpCookiesBrowserRunning(profileDir)) {
        errOut = "该指纹的浏览器正在运行，请先关闭再保存（避免库被占用/覆盖）";
        return false;
    }
    const std::wstring dbPath = CookiesDbPath(profileDir);
    if (::GetFileAttributesW(dbPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        errOut = "该指纹还没有浏览器 cookie 库（Default\\Network\\Cookies）";
        return false;
    }
    std::vector<unsigned char> key;
    if (!LoadOsCryptKey(LocalStatePath(profileDir), key, errOut)) {
        if (errOut.empty()) errOut = "读不到 Local State 的 os_crypt 密钥，无法加密 cookie";
        return false;
    }
    int dropped = 0;
    const std::string norm = FpNormalizeCookiesJson(jsonArr, &dropped);
    if (norm.empty()) { errOut = "cookie 不是 JSON 数组，未写库"; return false; }

    // 备份（保留最近 3 份）
    SYSTEMTIME stNow{};
    ::GetLocalTime(&stNow);
    wchar_t ts[64];
    swprintf_s(ts, L"%04d%02d%02d-%02d%02d%02d", stNow.wYear, stNow.wMonth, stNow.wDay,
        stNow.wHour, stNow.wMinute, stNow.wSecond);
    const std::wstring bak = dbPath + L".bak-" + ts;
    if (!::CopyFileW(dbPath.c_str(), bak.c_str(), FALSE)) {
        errOut = "备份 cookie 库失败（文件被占用？）";
        return false;
    }
    {
        // 清理旧备份，只留最近 3 份
        std::vector<std::wstring> baks;
        std::wstring dir = dbPath.substr(0, dbPath.find_last_of(L"\\/"));
        std::wstring pat = dir + L"\\Cookies.bak-*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW(pat.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { baks.push_back(fd.cFileName); } while (::FindNextFileW(h, &fd));
            ::FindClose(h);
        }
        std::sort(baks.begin(), baks.end());
        for (size_t i = 0; i + 3 < baks.size(); i++)
            ::DeleteFileW((dir + L"\\" + baks[i]).c_str());
    }

    SqliteDb s;
    if (!s.open(dbPath, SQLITE_OPEN_READWRITE, errOut)) return false;

    // 事务；失败整体回滚并还原备份
    const bool clearing = (norm == "[]");
    bool ok = SqlExec(s.db, "BEGIN IMMEDIATE;", errOut);
    if (ok && clearing) {
        ok = SqlExec(s.db, "DELETE FROM cookies;", errOut);
        if (ok) removed = ::sqlite3_changes(s.db);
    }
    if (ok && !clearing) {
        const char* kDel =
            "DELETE FROM cookies WHERE host_key=? AND name=? AND path=?";
        const char* kIns =
            "INSERT INTO cookies (creation_utc,host_key,top_frame_site_key,name,value,"
            "encrypted_value,path,expires_utc,is_secure,is_httponly,last_access_utc,"
            "has_expires,is_persistent,priority,samesite,source_scheme,source_port,"
            "last_update_utc,source_type,has_cross_site_ancestor) VALUES (?,?,?,?,?,?,?,?,"
            "?,?,?,?,?,?,?,?,?,?,?,?)"; // 20 列对 20 个占位符
        sqlite3_stmt *delSt = NULL, *insSt = NULL;
        if (::sqlite3_prepare_v2(s.db, kDel, -1, &delSt, NULL) != SQLITE_OK ||
            ::sqlite3_prepare_v2(s.db, kIns, -1, &insSt, NULL) != SQLITE_OK) {
            errOut = ::sqlite3_errmsg(s.db);
            if (delSt) ::sqlite3_finalize(delSt);
            if (insSt) ::sqlite3_finalize(insSt);
            ok = false;
        }
        if (ok) {
            const long long nowUs = FpCookiesChromeFromUnixSec(FpCookiesNowUnix());
            for (const std::string& obj : FpCookieArraySplit(norm)) {
                const std::string name = FpCookieGetStr(obj, "name");
                const std::string domain = FpCookieGetStr(obj, "domain");
                if (name.empty()) { skipped++; continue; }
                if (domain.empty()) { skipped++; continue; } // 没有域名无法落到 host_key
                std::string path = FpCookieGetStr(obj, "path");
                if (path.empty()) path = "/";
                const std::string value = FpCookieGetStr(obj, "value");
                const bool httpOnly = FpCookieGetBool(obj, "httpOnly", false);
                const bool secure = FpCookieGetBool(obj, "secure", false);
                const bool session = FpCookieGetBool(obj, "session", false);
                long long expires = FpCookieGetInt(obj, "expires", 0);
                if (session || expires <= 0) { expires = 0; }
                const int sameSite = FpCookiesSameSiteToInt(FpCookieGetStr(obj, "sameSite"));
                bool hostOnly = FpCookieGetBool(obj, "hostOnly", !domain.empty() && domain[0] != '.');
                std::string hostKey = domain;
                if (hostOnly && !hostKey.empty() && hostKey[0] == '.') hostKey.erase(0, 1);

                std::vector<unsigned char> ev;
                if (!AesGcmEncryptBlob(key, value, ev)) { skipped++; continue; }

                const long long expUs = session ? 0 : FpCookiesChromeFromUnixSec(expires);
                const int hasExpires = session ? 0 : 1;
                const int persistent = session ? 0 : 1;

                ::sqlite3_reset(delSt);
                ::sqlite3_clear_bindings(delSt);
                BindText(delSt, 1, hostKey);
                BindText(delSt, 2, name);
                BindText(delSt, 3, path);
                if (::sqlite3_step(delSt) != SQLITE_DONE) { errOut = ::sqlite3_errmsg(s.db); ok = false; break; }

                ::sqlite3_reset(insSt);
                ::sqlite3_clear_bindings(insSt);
                ::sqlite3_bind_int64(insSt, 1, nowUs);
                BindText(insSt, 2, hostKey);
                BindText(insSt, 3, "");                       // top_frame_site_key
                BindText(insSt, 4, name);
                BindText(insSt, 5, "");                       // value 留空，值走 encrypted_value
                ::sqlite3_bind_blob(insSt, 6, ev.data(), (int)ev.size(), SQLITE_TRANSIENT);
                BindText(insSt, 7, path);
                ::sqlite3_bind_int64(insSt, 8, expUs);
                ::sqlite3_bind_int(insSt, 9, secure ? 1 : 0);
                ::sqlite3_bind_int(insSt, 10, httpOnly ? 1 : 0);
                ::sqlite3_bind_int64(insSt, 11, nowUs);
                ::sqlite3_bind_int(insSt, 12, hasExpires);
                ::sqlite3_bind_int(insSt, 13, persistent);
                ::sqlite3_bind_int(insSt, 14, 1);             // priority = medium
                ::sqlite3_bind_int(insSt, 15, sameSite);
                ::sqlite3_bind_int(insSt, 16, secure ? 2 : 1); // source_scheme: 2=https 1=http
                ::sqlite3_bind_int(insSt, 17, secure ? 443 : 80);
                ::sqlite3_bind_int64(insSt, 18, nowUs);
                ::sqlite3_bind_int(insSt, 19, 0);             // source_type = unknown
                ::sqlite3_bind_int(insSt, 20, 1);             // has_cross_site_ancestor
                if (::sqlite3_step(insSt) != SQLITE_DONE) { errOut = ::sqlite3_errmsg(s.db); ok = false; break; }
                written++;
            }
            ::sqlite3_finalize(delSt);
            ::sqlite3_finalize(insSt);
        }
    }
    if (ok) ok = SqlExec(s.db, "COMMIT;", errOut);
    if (!ok) {
        const std::string origErr = errOut;   // ROLLBACK 失败会覆盖错误信息，先留原错
        SqlExec(s.db, "ROLLBACK;", errOut);
        errOut = origErr;
        if (s.db) { ::sqlite3_close(s.db); s.db = nullptr; }
        ::CopyFileW(bak.c_str(), dbPath.c_str(), FALSE); // 还原备份
        return false;
    }

    // 写后校验：重新统计行数（清空必须归零）
    if (clearing) {
        sqlite3_stmt* st = NULL;
        if (::sqlite3_prepare_v2(s.db, "SELECT COUNT(*) FROM cookies", -1, &st, NULL) == SQLITE_OK &&
            ::sqlite3_step(st) == SQLITE_ROW) {
            int cnt = ::sqlite3_column_int(st, 0);
            if (cnt != 0) {
                ::sqlite3_finalize(st);
                errOut = "清空校验失败（仍有 " + std::to_string(cnt) + " 行），已还原备份";
                if (s.db) { ::sqlite3_close(s.db); s.db = nullptr; }
                ::CopyFileW(bak.c_str(), dbPath.c_str(), FALSE);
                return false;
            }
        }
        if (st) ::sqlite3_finalize(st);
    }
    if (writtenOut) *writtenOut = written;
    if (skippedOut) *skippedOut = skipped;
    if (removedOut) *removedOut = removed;
    return true;
}
