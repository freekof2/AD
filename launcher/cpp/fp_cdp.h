#pragma once

#include <windows.h>
#include <string>

// Starts a background worker that applies the profile's fixed coordinates to
// page targets using this profile's loopback-only DevTools endpoint.
void FpStartGeolocationCdpWorker(const std::wstring& profileDir,
    const std::string& geoposition, const std::string& permissionMode,
    HANDLE browserProcess);
