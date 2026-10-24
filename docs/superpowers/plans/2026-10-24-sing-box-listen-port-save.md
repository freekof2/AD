# Save sing-box listen port from the fingerprint proxy form Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make **保存代理** save an edited sing-box port into the selected profile's `config.json` `listen_port` while preserving other proxy and fingerprint data.

**Architecture:** Add a pure configuration helper that strictly validates port text and changes the single `socks`/`mixed` inbound's numeric `listen_port`. In the save button path, prepare the profile config and proxy UI/static JSON, save proxy files first, then atomically replace the profile config; roll back proxy files if a later write fails. Existing sing-box launch allocation continues to choose an available per-instance port from the saved preferred port, so saving does not restart a running browser.

**Tech Stack:** C++17, Win32 atomic file replacement, existing custom JSON parser/serializer, MSVC and GitHub Actions.

---

## File responsibilities

- `launcher/cpp/singbox_config.h/.cpp`: Pure validation and JSON transformation of the configured sing-box inbound port.
- `launcher/cpp/singbox.h/.cpp`: Public atomic UTF-8 file writer reusing the existing temp-file + `MoveFileExW` replacement pattern.
- `launcher/cpp/fp_ui.cpp`: The proxy-only save transaction, UI validation/status, and rollback.
- `tests/fp_webrtc_test.cpp`: Remote unit regression for valid/invalid ports and preserved configuration/proxy fields.
- `launcher/cpp/SunLauncher.vcxproj` and `tests/fp_webrtc_test.vcxproj`: Compile the helper and existing JSON support in the appropriate targets.
- `.github/workflows/build-app.yml`: Assert the save-to-config transaction is wired to CI-visible code.
- `launcher/README.md`: Document how saved listen ports take effect.

### Task 1: Add the failing listen-port regression first

**Files:**
- Modify: `launcher/cpp/singbox_config.h`
- Modify: `tests/fp_webrtc_test.cpp`

- [x] Declare the test-driven helper API:

```cpp
bool SbUpdateListenPortConfig(const std::string& sourceJson,
    const std::string& requestedPort, std::string& updatedJson,
    uint16_t& portOut, bool& changed, std::string& error);
```

- [x] Add tests with a `mixed` inbound on 1208 and unrelated `listen`, outbounds, `experimental.clash_api`, and cache path. Assert request `1308` succeeds, returns port 1308, changes only `listen_port`, and leaves the original JSON unchanged.
- [x] Add an equivalent `socks` inbound case.
- [x] Assert a request equal to the current port succeeds with `changed == false` and byte-identical output.
- [x] Assert `""`, `"0"`, `"65536"`, `"-1"`, and `"12x"` are rejected; also reject malformed JSON, no supported inbound, and multiple supported inbounds.
- [x] Commit the test-only change and push `main`; GitHub Actions #96 failed during the unit-test step before implementation. Do not run local tests.

### Task 2: Implement and remotely verify the pure JSON helper

**Files:**
- Modify: `launcher/cpp/singbox_config.cpp`
- Modify: `launcher/cpp/singbox_config.h` if the declaration needs correction

- [x] Parse `requestedPort` as ASCII decimal digits only; reject empty, signs, whitespace, non-digits, zero, and values above 65535. Use `std::strtoul` only after digit validation and range-check before narrowing to `uint16_t`.
- [x] Parse the JSON with the existing AST parser, require exactly one `socks` or `mixed` inbound using `FindSocksInbound`, replace only that object's `listen_port` with a JSON number, serialize to valid JSON, and set `changed` based on numeric equality with the original port.
- [x] On any failure, return false with a useful `error`, leave `updatedJson` equal to the source and `changed` false, and do not mutate the source string.
- [x] Commit and push; GitHub Actions #97 passed the unit-test and offline contract steps for this helper commit.

### Task 3: Persist config and proxy fields as one save operation

**Files:**
- Modify: `launcher/cpp/singbox.h`
- Modify: `launcher/cpp/singbox.cpp`
- Modify: `launcher/cpp/fp_ui.cpp`

- [x] Expose `SbWriteConfigAtomically(const std::wstring& path, const std::string& content, DWORD& errorOut)` in `singbox.h` and implement it as a thin wrapper around the existing unique-temp-file `SbReplaceUtf8File` implementation.
- [x] In `FpSaveProxyOnly`, after `FpCollect`, detect `proxyType == L"sing-box"`; force proxy host to `127.0.0.1`, read `<effective profile directory>\config.json`, strip an optional UTF-8 BOM for parsing, and call `SbUpdateListenPortConfig` with the proxy port edit text. On validation/config errors, show status and return before writing any proxy/config files.
- [x] Build updated UI/static JSON with `FpBuildProxySaveJson`, retaining the sing-box UI mode and writing `socks5` to `ProxyChain` with the edited port. Keep `socks5/http/https` on the existing proxy-only save path and never open or change sing-box config for those modes.
- [x] Save UI JSON, then static JSON, then atomically save config JSON only when `changed` is true. On any failed write, attempt to restore prior files or remove files newly created by this operation. Report rollback failure distinctly.
- [x] Update the proxy status label to say the preferred port is saved and takes effect on the next launch. Do not restart any running browser/sing-box instance.
- [x] Add a unit assertion that `FpBuildProxySaveJson` keeps the selected port in UI `proxyPort` and in the SOCKS5 `ProxyChain`, and preserves unrelated fields.
- [x] Commit and push; the pure helper and tests were remotely verified by GitHub Actions #97. Verify the integrated UI/config transaction in the next Actions run.

### Task 4: CI contract, user documentation, and final remote check

**Files:**
- Modify: `.github/workflows/build-app.yml`
- Modify: `launcher/README.md`

- [x] Add contract checks for the port-update helper, range validation, only updating `listen_port`, validation before writes, atomic `config.json` replacement, rollback of proxy JSON files, unchanged-port write skip, and no sing-box config access for other proxy modes.
- [x] Document that **保存代理** updates the selected profile's `config.json` preferred `listen_port`, and an already-running profile picks it up on its next launch.
- [x] Run `git status --short --branch`, `git diff --cached --check`, and inspect the staged name list. Stage only intended plan/spec/source/test/workflow/docs files; leave `.gitignore`, `SunBrowser/`, `UILOG/`, `asar/`, `chrome/`, `log/`, `reports/`, and other user files untouched.
- [x] Commit on `main` (`3913355`), push to `origin main`, and verify GitHub Actions #98 passed for that SHA. Do not run local tests.

## Self-review

- Every design requirement is covered: port edit/validation, only the supported inbound field changes, JSON escaping comes from the existing serializer, proxy UI/static persistence remains narrow, errors avoid success status, failed later writes roll back earlier proxy writes, and running instances are not restarted.
- Tests and workflow paths are the existing Windows/MSVC remote pipeline. No local test command is included.
- The configured `listen_port` is the preferred starting point; runtime allocation still resolves multi-profile and occupied-port conflicts as already implemented.
