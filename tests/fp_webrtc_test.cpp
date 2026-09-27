#include "fp_webrtc.h"
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

    const FpWebRtcResolution forward = FpResolveWebRtc(L"forward", L"");
    CHECK(!forward.disableWebRtc && !forward.disableUdp && forward.address.empty());
    CHECK(!forward.proxyIpMissing);

    const FpWebRtcResolution disabled = FpResolveWebRtc(L"disabled", L"");
    CHECK(disabled.disableWebRtc && disabled.address.empty());
    CHECK(!disabled.proxyIpMissing);

    const FpWebRtcResolution proxy4 = FpResolveWebRtc(L"proxy", L"203.0.113.8");
    CHECK(!proxy4.disableWebRtc && proxy4.address == L"203.0.113.8");
    CHECK(!proxy4.proxyIpMissing);

    const FpWebRtcResolution proxyTrimmed = FpResolveWebRtc(L"proxy", L" 203.0.113.8 \r\n");
    CHECK(!proxyTrimmed.disableWebRtc && proxyTrimmed.address == L"203.0.113.8");

    const FpWebRtcResolution proxy6 = FpResolveWebRtc(L"proxy", L"2001:db8::8");
    CHECK(!proxy6.disableWebRtc && proxy6.address == L"2001:db8::8");

    const FpWebRtcResolution missing = FpResolveWebRtc(L"proxy", L"  ");
    CHECK(missing.disableWebRtc && missing.address.empty() && missing.proxyIpMissing);

    const FpWebRtcResolution invalid = FpResolveWebRtc(L"proxy", L"192.0.2.999");
    CHECK(invalid.disableWebRtc && invalid.address.empty() && invalid.proxyIpMissing);

    const FpWebRtcResolution disableUdp = FpResolveWebRtc(L"disable_udp", L"");
    CHECK(!disableUdp.disableWebRtc && disableUdp.disableUdp);

    ::WSACleanup();
    return 0;
}
