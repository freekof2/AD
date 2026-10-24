# Save sing-box listen port from the fingerprint proxy form

## Goal

Allow the fingerprint page's **保存代理** button to save an edited sing-box port to that profile's `config.json` `listen_port`, together with the current proxy settings, without saving other pending fingerprint edits.

## Current behavior

- Selecting `sing-box` reads the profile's `config.json` and fills the proxy host with `127.0.0.1` and the proxy port with the configured SOCKS-compatible inbound's `listen_port`.
- **保存代理** immediately saves proxy fields to `ui_fingerprint.json` and writes the corresponding SOCKS5 `ProxyChain` to the encoded static fingerprint data.
- At launch, sing-box uses the configured port as the starting preference for selecting an available per-instance port. Runtime port allocation remains necessary so multiple profiles can run concurrently without binding conflicts.

## Design

1. Keep the existing proxy port edit control as the editable source of the requested sing-box port.
2. When **保存代理** is clicked with proxy type `sing-box`, validate the port as a decimal integer in the inclusive range 1–65535. Other proxy types do not read or change sing-box configuration.
3. Read the current profile's `config.json` from its effective profile data directory. Require exactly one supported SOCKS-compatible inbound (`socks` or `mixed`). Validate the edit as decimal digits representing 1–65535, then update only that inbound's numeric `listen_port`. Preserve all other configuration values, including the inbound's `listen`, cache path, Clash API settings and outbounds. Serialize valid JSON with backslashes escaped as `\\`.
4. Prepare the proxy-only UI/static JSON pair and the updated config in memory. Persist in this order: `ui_fingerprint.json`, static `ProxyChain`, then atomically replace `config.json` if its listen port changed. If a later write fails, restore the previous UI/static files so the displayed operation does not leave a half-saved proxy. Do not rewrite `config.json` when the selected port is already present.
5. The saved UI proxy type remains `sing-box`; its static `ProxyChain.scheme` remains `socks5`, host is `127.0.0.1`, and port matches the configured listen port. Other proxy types do not access or change the profile `config.json`.
6. A running browser/sing-box pair is not restarted by this button. The edited `listen_port` becomes the preferred port on the next profile launch, where the existing per-instance free-port allocation still prevents multi-profile conflicts.
7. On invalid port, missing/malformed config, unsupported or ambiguous inbound, or file write failure, show a clear proxy status message and do not report the operation as successful. Do not save unrelated fingerprint fields.

## Test and verification

- Add focused configuration helper tests for updating a `mixed` inbound and a `socks` inbound, preserving unrelated configuration, and rejecting zero, out-of-range, non-numeric ports, missing/ambiguous supported inbounds, and malformed JSON.
- Add a proxy-save regression asserting that the UI proxy fields and SOCKS5 `ProxyChain` match the edited port while unrelated UI/static fingerprint values remain unchanged.
- Add CI contract checks that reject invalid ports before persistence, preserve files on validation/config errors, and skip rewriting an unchanged listen port.
- Build and run tests only through GitHub Actions; do not run tests locally.

## Files expected to change

- `launcher/cpp/singbox_config.h/.cpp`: validated, isolated inbound listen-port update helper.
- `launcher/cpp/singbox.h/.cpp`: expose the existing atomic UTF-8 config writer for the save flow.
- `launcher/cpp/fp_ui.cpp`: save-button integration, input validation, config persistence, rollback and status feedback.
- `tests/fp_webrtc_test.cpp` and `tests/fp_webrtc_test.vcxproj`: regression coverage.
- `.github/workflows/build-app.yml`: contract checks for the save path and config update behavior.
- `launcher/README.md`: document that saving a sing-box proxy updates the profile config's preferred `listen_port` and takes effect on next launch.
