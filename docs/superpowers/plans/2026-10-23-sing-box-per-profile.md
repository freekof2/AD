# Per-profile sing-box Integration Implementation Plan

> **For agentic workers:** Use this plan task-by-task in the current session. Steps use checkbox syntax for tracking.

**Goal:** Start one hidden sing-box instance only for profiles whose saved proxy mode is `sing-box`, route that browser through its SOCKS-compatible local inbound, and display summed live upload/download rates.

**Architecture:** Read each profile's `config.json` from its effective profile data directory and read the application-wide executable from `AppDir()\sing-box\sing-box.exe`. Build an unmodified-source runtime copy with per-instance inbound, Clash API, and cache paths; pair its process with the profile's browser process. Keep JSON/config transforms separately testable and keep network statistics off the UI thread.

**Tech Stack:** C++17, Win32 process/window APIs, WinHTTP WebSocket API, MSVC/GitHub Actions.

---

### Task 1: Add remote-run configuration transformation tests

**Files:**
- Modify: `tests/fp_webrtc_test.cpp`
- Modify: `tests/fp_webrtc_test.vcxproj`
- Create: `launcher/cpp/singbox_config.h`
- Create: `launcher/cpp/singbox_config.cpp`

- [x] Add a fixture with one `mixed` inbound (`listen_port: 1208`), a loopback `clash_api.external_controller`, and an enabled `cache_file`.
- [x] Assert parsing returns the inbound type and listen port; reject malformed JSON, no supported inbound, and multiple supported inbounds.
- [x] Assert runtime transformation changes only the selected inbound's listen/listen_port, changes the controller port, and gives the cache a profile-specific path while retaining outbound data.
- [x] Add a `socks` inbound fixture and assert it is accepted as well.
- [x] Add `singbox_config.cpp` and its header to the existing test project; do not run tests locally. The GitHub Actions test step is the verification environment.

### Task 2: Implement per-profile hidden sing-box lifecycle

**Files:**
- Create: `launcher/cpp/singbox.h`
- Create: `launcher/cpp/singbox.cpp`
- Modify: `launcher/cpp/SunLauncher.h`
- Modify: `launcher/cpp/SunLauncher.vcxproj`

- [x] Define profile runtime state containing the sing-box process handle/PID, effective SOCKS and API ports, runtime config path, API secret, and latest upload/download rates.
- [x] Resolve the executable as `AppDir() + L"\\sing-box\\sing-box.exe"` and the source config as `<effective profile directory>\\config.json`.
- [x] Allocate non-conflicting loopback inbound and API ports; generate the runtime config beside the profile config so relative paths keep the profile directory as working directory.
- [x] Start `sing-box.exe run -c <runtime config>` with hidden/no-console process flags, wait for the SOCKS port to accept connections, and roll back the process/config on startup failure.
- [x] Bind the generated SOCKS listener and Clash API to `127.0.0.1`; change an existing Clash controller port or add one when absent; assign a unique cache path if `cache_file` is enabled.
- [x] Add stop and status-query operations that close only the matching profile's sing-box instance and remove its generated config.
- [x] Link `winhttp.lib` and compile the new runtime module in the launcher project.

### Task 3: Integrate proxy selection, paired start/stop, and summed rates

**Files:**
- Modify: `launcher/cpp/fp_ui.h`
- Modify: `launcher/cpp/fp_ui.cpp`
- Modify: `launcher/cpp/fingerprint.cpp`
- Modify: `launcher/cpp/main.cpp`
- Modify: `launcher/cpp/SunLauncher.h`

- [x] Add `sing-box` to the proxy dropdown and, on selection or profile load, read that profile's config and fill `127.0.0.1` plus its configured base `listen_port`.
- [x] Persist the UI mode as `sing-box`; write the browser's saved proxy chain as SOCKS5 and translate a runtime copy of the UI parameters to `socks5://127.0.0.1:<allocated port>` before building the browser command line.
- [x] In `StartOneLocked`, start sing-box only when the profile's saved `proxyType` is `sing-box`; leave `socks5`, `http`, and `https` on the existing path. Start the browser only after the local inbound is ready.
- [x] In `OnStopOneLocked` and profile-exit polling, stop the paired sing-box process and clean generated config without affecting other profiles.
- [x] Stream each active instance's Clash API `/traffic` WebSocket on a worker thread, sum the current upload/download rates, and post updates to two labels in the main window's unused right-side area.
- [x] Show missing executable/config, invalid config, port allocation, and startup errors in the relevant status bar without opening a modal dialog. When API statistics are unavailable, keep the proxy/browser running and show zero rates.
- [x] Preserve all existing proxy behavior when the selected type is not `sing-box`.

### Task 4: Add GitHub Actions contract checks and remote verification

**Files:**
- Modify: `.github/workflows/build-app.yml`
- Modify: `launcher/README.md`

- [x] Add CI contract checks for the per-profile config path, supported `mixed`/`socks` inbound, hidden process flags, SOCKS5 command mapping, conditional startup, paired stop, and summed traffic UI.
- [x] Document the runtime asset layout and that the profile config is `<profile data directory>\\config.json`; missing resources are reported in the status bar.
- [ ] Review `git status`, `git diff`, and recent commits; stage only the implementation plan and intended feature/test/workflow files, leaving pre-existing `.gitignore` and untracked user files untouched.
- [ ] Commit on `main`, push to `origin main`, and use the resulting GitHub Actions run as the only test/build verification. Do not run local tests.

## Review Notes

- Runtime evidence from the deployment directory showed `F:\\AD\\sing-box\\sing-box.exe` and a profile config at `F:\\AD\\cache\\k1hiq8ua_hyg6dd\\config.json`.
- That config uses one `mixed` inbound, a loopback Clash API, and an enabled cache file. Other profiles may lack their own `config.json`; this must produce a status-bar message.
- The repository's current local changes are unrelated and must not be staged or overwritten.
