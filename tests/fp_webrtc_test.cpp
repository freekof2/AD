#include "fp_webrtc.h"
#include "fp_browser_config.h"
#include "fp_cookies.h"
#include <iostream>

static int Check(bool condition, int line) {
    if (condition) return 0;
    std::cerr << "WebRTC mapping failed at line " << line << "\n";
    return line;
}

#define CHECK(condition) do { int rc = Check((condition), __LINE__); if (rc) return rc; } while (0)

int main() {
    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    const FpWebRtcResolution forward = FpResolveWebRtc(L"forward", L"104.28.152.166");
    // official setWebRTC：["proxy","forward"] 共用 t.ip -> ext WebRTCAddress（转发也覆盖候选地址）
    CHECK(!forward.disableWebRtc && !forward.disableUdp && forward.address == L"104.28.152.166");
    CHECK(!forward.proxyIpMissing && !forward.proxyIpIgnored);
    CHECK(FpBuildWebRtcSunParams(forward) ==
        "{\"DisableWebRTC\":false,\"WebRTCAddress\":\"104.28.152.166\"}");
    // 转发不填 IP：保持不禁用（见 fp_webrtc.h 注释里的有意差异）
    const FpWebRtcResolution forwardEmpty = FpResolveWebRtc(L"forward", L"");
    CHECK(!forwardEmpty.disableWebRtc && forwardEmpty.address.empty());
    CHECK(FpBuildWebRtcSunParams(forwardEmpty) == "{\"DisableWebRTC\":false}");
    // 转发填了非法 IP：忽略该值但不关 WebRTC
    const FpWebRtcResolution forwardBad = FpResolveWebRtc(L"forward", L"192.0.2.999");
    CHECK(!forwardBad.disableWebRtc && forwardBad.address.empty() && forwardBad.proxyIpIgnored);
    // disable_udp 官方二选一的判定（main.min.js setWebRTC 的三个条件）
    CHECK(FpBrowserKernelFromDirLeaf("chrome_152") == 152);
    CHECK(FpBrowserKernelFromDirLeaf("flower_100") == 100);
    CHECK(FpBrowserKernelFromDirLeaf("chrome") == 0);
    CHECK(FpBrowserKernelFromDirLeaf("152.0.7977.54") == 0);
    CHECK(FpUdpSocks5PathApplies(true, 152, 20260831));  // 实测环境：chrome_152 + 20260831
    CHECK(!FpUdpSocks5PathApplies(false, 152, 20260831)); // 非 chrome（flower/firefox）
    CHECK(!FpUdpSocks5PathApplies(true, 144, 20260831));  // 内核 < 145
    CHECK(!FpUdpSocks5PathApplies(true, 152, 20260401));  // browserVersion < 20260422

    const FpWebRtcResolution disabled = FpResolveWebRtc(L"disabled", L"");
    CHECK(disabled.disableWebRtc && disabled.address.empty());
    CHECK(!disabled.proxyIpMissing);
    CHECK(FpBuildWebRtcSunParams(disabled) == "{\"DisableWebRTC\":true}");

    const FpWebRtcResolution proxy4 = FpResolveWebRtc(L"proxy", L"203.0.113.8");
    CHECK(!proxy4.disableWebRtc && proxy4.address == L"203.0.113.8");
    CHECK(!proxy4.proxyIpMissing);
    CHECK(FpBuildWebRtcSunParams(proxy4) ==
        "{\"DisableWebRTC\":false,\"WebRTCAddress\":\"203.0.113.8\"}");

    const FpWebRtcResolution proxyTrimmed = FpResolveWebRtc(L"proxy", L" 203.0.113.8 \r\n");
    CHECK(!proxyTrimmed.disableWebRtc && proxyTrimmed.address == L"203.0.113.8");

    const FpWebRtcResolution proxy6 = FpResolveWebRtc(L"proxy", L"2001:db8::8");
    CHECK(!proxy6.disableWebRtc && proxy6.address == L"2001:db8::8");
    CHECK(FpBuildWebRtcSunParams(proxy6) ==
        "{\"DisableWebRTC\":false,\"WebRTCAddress\":\"2001:db8::8\"}");

    const FpWebRtcResolution missing = FpResolveWebRtc(L"proxy", L"  ");
    CHECK(missing.disableWebRtc && missing.address.empty() && missing.proxyIpMissing);
    CHECK(FpBuildWebRtcSunParams(missing) == "{\"DisableWebRTC\":true}");

    const FpWebRtcResolution invalid = FpResolveWebRtc(L"proxy", L"192.0.2.999");
    CHECK(invalid.disableWebRtc && invalid.address.empty() && invalid.proxyIpMissing);

    const FpWebRtcResolution disableUdp = FpResolveWebRtc(L"disable_udp", L"");
    CHECK(!disableUdp.disableWebRtc && disableUdp.disableUdp);

    CHECK(FpNormalizeTimezone("America/Los Angeles") == "America/Los_Angeles");
    CHECK(FpBuildTimeZoneSunParam("America/Los Angeles") ==
        "\"TimeZone\":\"America/Los_Angeles\"");
    CHECK(FpBuildTimeZoneSunParam("").empty());
    CHECK(FpBrowserPlatformTag("Win32") == "Other");
    CHECK(FpBrowserPlatformTag("MacIntel") == "MacOS");
    CHECK(FpBrowserPlatformTag("iPhone") == "iPhone");
    CHECK(FpBrowserPlatformTag("Linux armv8I") == "Android");
    CHECK(FpBuildWebGlConfigJson("Google Inc.", "ANGLE Renderer", "0", "", "") ==
        "{\"UNMASKED_VENDOR_WEBGL\":\"Google Inc.\",\"UNMASKED_RENDERER_WEBGL\":\"ANGLE Renderer\",\"SUPPORTED_EXTENSIONS\":[]}");
    CHECK(FpBuildWebGlConfigJson("Google Inc.", "ANGLE Renderer", "1", "amd", "gcn-5") ==
        "{\"UNMASKED_VENDOR_WEBGL\":\"Google Inc.\",\"UNMASKED_RENDERER_WEBGL\":\"ANGLE Renderer\",\"GPUAdapterInfo\":{\"vendor\":\"amd\",\"architecture\":\"gcn-5\"},\"SUPPORTED_EXTENSIONS\":[]}");
    CHECK(FpBuildWebGlConfigJson("", "ANGLE Renderer", "1", "amd", "gcn-5").empty());

    const auto fullProfile = FpResolveImportSourcePath(
        L"F:\\.ADSPOWER_GLOBAL\\cache\\k1ds12lu_hyg6dd\\",
        L"1111_local", true, true);
    CHECK(fullProfile.directProfilePath);
    CHECK(fullProfile.profilePath == L"F:\\.ADSPOWER_GLOBAL\\cache\\k1ds12lu_hyg6dd");

    const auto parentWithUnderscore = FpResolveImportSourcePath(
        L"F:\\user_cache", L"1111_local", true, false);
    CHECK(!parentWithUnderscore.directProfilePath);
    CHECK(parentWithUnderscore.profilePath == L"F:\\user_cache\\1111_local");

    const auto currentProfilePath = FpResolveImportSourcePath(
        L"F:\\cache\\1111_local", L"1111_local", true, false);
    CHECK(currentProfilePath.directProfilePath);
    CHECK(currentProfilePath.profilePath == L"F:\\cache\\1111_local");

    // 噪声开关真值优先级：ui > static 低位键 > official 种子存在性
    CHECK(FpResolveNoiseSwitch("\"0\"", "\"1\"", true) == "0");
    CHECK(FpResolveNoiseSwitch("", "\"1\"", false) == "1");
    CHECK(FpResolveNoiseSwitch("\"0\"", "", true) == "0");
    CHECK(FpResolveNoiseSwitch("", "", true) == "1");
    CHECK(FpResolveNoiseSwitch("", "", false) == "0");
    CHECK(FpBuildNoiseSunParams(true, true, true, true, "fbcc", "21", "3063") ==
        "{\"CanvasMark\":\"fbcc\",\"WebGLMark\":\"fbcc\",\"AudioFp\":21,\"ClientRectFp\":3063}");
    CHECK(FpBuildNoiseSunParams(true, false, false, false, "fbcc", "21", "3063") ==
        "{\"CanvasMark\":\"fbcc\"}");
    CHECK(FpBuildNoiseSunParams(false, false, false, false, "fbcc", "21", "3063").empty());

    // 系统门控（与指纹页 F_OS 一致）
    CHECK(FpOsSupportsFlash("win"));
    CHECK(FpOsSupportsFlash("mac"));
    CHECK(!FpOsSupportsFlash("linux"));
    CHECK(!FpOsSupportsFlash("ios"));
    CHECK(FpOsSupportsMobileExtras("android"));
    CHECK(FpOsSupportsMobileExtras("ios"));
    CHECK(!FpOsSupportsMobileExtras("win"));
    CHECK(FpOsIsAndroid("android") && FpOsIsIos("ios") && !FpOsIsIos("linux"));

    // 网络类型（official normalize/get/apply）
    CHECK(FpNormalizeNetworkInformationType("3") == "0");
    CHECK(FpNormalizeNetworkInformationType(" 2 ") == "2");
    CHECK(FpNetworkChromeType("1") == "wifi");
    CHECK(FpNetworkChromeType("0").empty());
    CHECK(FpBuildNetworkInformationStatic("1") == "{\"enabled\":true,\"type\":\"wifi\"}");
    CHECK(FpBuildNetworkInformationStatic("0").empty());
    CHECK(FpBuildAndroidBlinkFeatureValue() == "\"NetworkInformation,NetInfoDownlinkMax\"");

    // 陀螺仪 / 设备方向 / DeviceMotion（official setGyroscope）
    CHECK(FpBuildGyroscopeStaticJson().find("0.15") != std::string::npos);
    CHECK(FpBuildDeviceOrientationStaticJson("fbcc").find("absolute\":false") != std::string::npos);
    CHECK(FpBuildDeviceMotionStaticJson().find("9.78") != std::string::npos);

    // ClientHints -> UserAgentMetadata（official setClientHints）
    CHECK(FpBuildUserAgentMetadataJson("Windows", "10.0.0", "x86", "", "0", "", "") ==
        "{\"platform\":\"Windows\",\"platformVersion\":\"10.0.0\",\"architecture\":\"x86\","
        "\"model\":\"\",\"mobile\":false}");
    CHECK(FpBuildUserAgentMetadataJson("Linux armv8I", "10", "arm", "Pixel", "1", "64", "1") ==
        "{\"platform\":\"Linux armv8I\",\"platformVersion\":\"10\",\"architecture\":\"arm\","
        "\"model\":\"Pixel\",\"mobile\":true,\"bitness\":\"64\",\"wow64\":true}");

    // Flash / MaxTouchPoints / 种子
    CHECK(FpBuildFlashSettingSunParam("block") == "\"FlashPluginSetting\":\"block\"");
    CHECK(FpBuildFlashSettingSunParam("allow") == "\"FlashPluginSetting\":\"allow\"");
    CHECK(FpBuildFlashSettingSunParam("off").empty());
    CHECK(FpNormalizeMaxTouchPoints("12") == "12");
    CHECK(FpNormalizeMaxTouchPoints("x") == "0");
    CHECK(FpNormalizeMaxTouchPoints("0123") == "0");
    CHECK(FpSeedFromFbcc("abc", 1, 9999) >= 1);
    CHECK(FpSeedFromFbcc("abc", 1, 9999) <= 9999);
    CHECK(FpSeedFromFbcc("abc", -10000, 9999) >= -10000);
    CHECK(FpSeedFromFbcc("abc", -10000, 9999) <= 9999);

    // 系统 -> official 取值集：initBrowser.platform 与 clientHints.platform 是两张表
    CHECK(FpOsToOfficialPlatform("win") == "Win32");
    CHECK(FpOsToOfficialPlatform("mac") == "MacIntel");
    CHECK(FpOsToOfficialPlatform("linux") == "Linux x86_64");
    CHECK(FpOsToOfficialPlatform("android") == "Linux armv8I");
    CHECK(FpOsToOfficialPlatform("ios") == "iPhone");
    CHECK(FpOsToChPlatform("win") == "Windows");
    CHECK(FpOsToChPlatform("mac") == "macOS");
    CHECK(FpOsToChPlatform("linux") == "Linux");
    CHECK(FpOsToChPlatform("android") == "Android");
    CHECK(FpOsToChPlatform("ios") == "iPhone");
    CHECK(FpOsToChArchitecture("win") == "x86");
    CHECK(FpOsToChArchitecture("android") == "arm");
    CHECK(FpOsToChArchitecture("ios") == "arm");
    CHECK(FpOsToChPlatformVersion("win") == "10.0.0");
    CHECK(FpOsToChPlatformVersion("mac") == "10.15.7");
    CHECK(FpOsToChPlatformVersion("android") == "14.0.0");
    CHECK(FpOsToChPlatformVersion("linux").empty());
    CHECK(FpOsToChModel("android") == "Pixel 8");  // 与 UA 预设机型一致
    CHECK(FpOsToChModel("win").empty());
    CHECK(FpOsToChMobile("android") == "1");
    CHECK(FpOsToChMobile("ios") == "1");
    CHECK(FpOsToChMobile("mac") == "0");
    CHECK(FpChMatchesOs("win", "Windows"));
    CHECK(FpChMatchesOs("mac", "macOS"));
    CHECK(!FpChMatchesOs("win", "Android"));
    CHECK(!FpChMatchesOs("win", ""));

    // ---- Cookie：规范化 / 明文转换 / Chrome 时间 / sameSite（fp_cookies.h 纯函数）----
    CHECK(FpNormalizeCookiesJson("not json").empty());
    CHECK(FpNormalizeCookiesJson("[{}]") == "[]");           // 无效元素丢弃
    CHECK(FpNormalizeCookiesJson("[]") == "[]");
    {
        int dropped = 0;
        const std::string n = FpNormalizeCookiesJson(
            "[{\"name\":\"a\",\"value\":\"b\"}]", &dropped);
        CHECK(dropped == 0);
        auto p = FpCookieArraySplit(n);
        CHECK(p.size() == 1);
        CHECK(FpCookieGetStr(p[0], "name") == "a");
        CHECK(FpCookieGetStr(p[0], "path") == "/");
        CHECK(FpCookieGetBool(p[0], "session", false) == true);
        CHECK(FpCookieGetBool(p[0], "httpOnly", true) == false);
        CHECK(FpCookieGetStr(p[0], "sameSite") == "unspecified");
    }
    {
        const std::string n = FpNormalizeCookiesJson(
            "[{\"name\":\"a\",\"value\":\"v\",\"domain\":\".x.com\",\"expires\":1700000000,"
            "\"secure\":true,\"httpOnly\":true}]");
        auto p = FpCookieArraySplit(n);
        CHECK(p.size() == 1);
        CHECK(FpCookieGetStr(p[0], "domain") == ".x.com");
        CHECK(FpCookieGetBool(p[0], "hostOnly", true) == false); // .开头 = 域 cookie
        CHECK(FpCookieGetInt(p[0], "expires", 0) == 1700000000);
        CHECK(FpCookieGetBool(p[0], "session", true) == false);
        CHECK(FpCookieGetBool(p[0], "secure", false) == true);
    }
    {
        const std::string n = FpNormalizeCookiesJson(
            "[{\"name\":\"a\",\"value\":\"v\",\"domain\":\"x.com\"}]");
        CHECK(FpCookieGetBool(FpCookieArraySplit(n)[0], "hostOnly", false) == true);
    }
    // value 含 } / 引号时不得截断
    {
        const std::string n = FpNormalizeCookiesJson("[{\"name\":\"a\",\"value\":\"x}y\\\"z\"}]");
        auto p = FpCookieArraySplit(n);
        CHECK(p.size() == 1);
        CHECK(FpCookieGetStr(p[0], "value") == "x}y\"z");
    }
    // 明文 -> JSON：逐行 Name=Value
    {
        int cnt = 0;
        const std::string n = FpCookiesTextToJson("sid=abc123\nfoo=bar\n", &cnt);
        CHECK(cnt == 2);
        auto p = FpCookieArraySplit(n);
        if (p.size() != 2)
            std::cerr << "cookie debug parts=" << p.size() << " n=[" << n << "]\n";
        CHECK(p.size() == 2);
        CHECK(FpCookieGetStr(p[0], "name") == "sid");
        CHECK(FpCookieGetStr(p[0], "value") == "abc123");
        CHECK(FpCookieGetStr(p[1], "name") == "foo");
        CHECK(FpCookieGetStr(p[1], "value") == "bar");
    }
    // 明文 -> JSON：Cookie 头
    {
        int cnt = 0;
        const std::string n = FpCookiesTextToJson("Cookie: a=1; b=2", &cnt);
        CHECK(cnt == 2);
        auto p = FpCookieArraySplit(n);
        CHECK(FpCookieGetStr(p[0], "name") == "a");
        CHECK(FpCookieGetStr(p[0], "value") == "1");
        CHECK(FpCookieGetStr(p[1], "name") == "b");
    }
    // 明文 -> JSON：Netscape 七列（secure 在第 4 列，过期在第 5 列）
    {
        int cnt = 0;
        const std::string n = FpCookiesTextToJson(
            ".example.com\tTRUE\t/\tTRUE\t1893456000\tsid\txyz", &cnt);
        CHECK(cnt == 1);
        auto p = FpCookieArraySplit(n);
        CHECK(FpCookieGetStr(p[0], "domain") == ".example.com");
        CHECK(FpCookieGetStr(p[0], "name") == "sid");
        CHECK(FpCookieGetBool(p[0], "secure", false) == true);
        CHECK(FpCookieGetInt(p[0], "expires", 0) == 1893456000);
    }
    // 明文 -> JSON：#HttpOnly 行，'#' 注释行跳过
    {
        int cnt = 0;
        const std::string n = FpCookiesTextToJson(
            "# Netscape HTTP Cookie File\n#HttpOnly\t.ex.com\tFALSE\t/\tFALSE\t100\tk\tv", &cnt);
        CHECK(cnt == 1);
        auto p = FpCookieArraySplit(n);
        CHECK(FpCookieGetStr(p[0], "domain") == ".ex.com");
        CHECK(FpCookieGetBool(p[0], "httpOnly", false) == true);
        CHECK(FpCookieGetInt(p[0], "expires", 0) == 100);
    }
    // 已是 JSON 数组则透传
    CHECK(FpCookieArraySplit(FpCookiesTextToJson("[{\"name\":\"a\",\"value\":\"b\"}]")).size() == 1);
    CHECK(FpCookiesTextToJson("") == "[]");

    // Chrome 时间（µs since 1601-01-01）与 unix 秒互转：1970 = 1601 + 11644473600s
    CHECK(FpCookiesChromeFromUnixSec(0) == 0);
    CHECK(FpCookiesUnixFromChromeUs(0) == 0);
    CHECK(FpCookiesChromeFromUnixSec(1700000000LL) == 13344473600LL * 1000000LL);
    CHECK(FpCookiesChromeFromUnixSec(1700000000LL) > 0);
    CHECK(FpCookiesUnixFromChromeUs(FpCookiesChromeFromUnixSec(1700000000LL)) == 1700000000LL);
    CHECK(FpCookiesUnixFromChromeUs(1000) == 0); // 1601 年附近的值归 0
    // sameSite 双向（main.min.js h={"-1":"unspecified",0:"no_restriction",1:"lax",2:"strict"}）
    CHECK(FpCookiesSameSiteToStr(0) == "no_restriction");
    CHECK(FpCookiesSameSiteToStr(1) == "lax");
    CHECK(FpCookiesSameSiteToStr(2) == "strict");
    CHECK(FpCookiesSameSiteToStr(-1) == "unspecified");
    CHECK(FpCookiesSameSiteToInt("lax") == 1);
    CHECK(FpCookiesSameSiteToInt("Strict") == 2);
    CHECK(FpCookiesSameSiteToInt("none") == 0);
    CHECK(FpCookiesSameSiteToInt("Unspecified") == -1);
    CHECK(FpCookiesSameSiteToInt("") == -1);

    ::WSACleanup();
    return 0;
}
