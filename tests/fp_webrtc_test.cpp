#include "fp_webrtc.h"
#include "fp_browser_config.h"
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
    CHECK(!forward.disableWebRtc && !forward.disableUdp && forward.address.empty());
    CHECK(!forward.proxyIpMissing && forward.proxyIpIgnored);
    CHECK(FpBuildWebRtcSunParams(forward) == "{\"DisableWebRTC\":false}");

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

    ::WSACleanup();
    return 0;
}
