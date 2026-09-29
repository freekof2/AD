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

    ::WSACleanup();
    return 0;
}
