# SunLauncher（离线指纹注入版）

Win32 原生单窗口 + 内置 web-ui（`http://127.0.0.1:18900/`）：列出数据目录下的
profile，一键**指纹注入启动**/关闭。只读写本地缓存目录，**不连接 AdsPower 云端**。

实现见 `cpp/`（C++ Win32，无第三方依赖）：

- `fingerprint.h/cpp`：换表 Base64（C1/C2 与 `main.min.js` 一致）、自包含 MD5、
  `md5(fbcc+"_static/_webrtc/_cookies")` 三件套读写、`FpBuildCmdline` 拼
  `--extended-parameters` 注入、`FpKillProfileTree` 进程树关闭。
- 启动参数：`--user-data-dir=<数据目录>/<profile> --profile-directory=Default`
  `--remote-debugging-port=<自动分配> --extended-parameters=<注入>`
  `--no-first-run --no-default-browser-check about:blank`。
- 停止：先按 `user-data-dir` 树杀 SunBrowser 进程，再结束 launcher 句柄兜底。
- 离线 HTTP（127.0.0.1:18900）：`GET /api/profiles`、`POST /api/start|stop`、
  `GET /api/fp/<static|dynamic|cookies|ui>?name=`、`POST /api/fp/save`、
  `GET /` 直接 serving 同目录 `web-ui/`。
- 冲突规则（以缓存为准）：`UserId/CanvasMark/WebGLMark/AudioFp/ClientRects/`
  `ProxyChain/DeviceName/MacAddress/MediaDevices/TTSEngines/Langs/`
  `TimeZone/Geoposition/WebRTCAddress/DisableWebRTC` 等保护键在注入与
  `/api/fp/save` 写 static/dynamic 时被缓存值覆盖；UI 全量参数存
  `ui_fingerprint.json` 明文侧车。

点启动后无窗口时看 `debug.log`（exe 同目录）：完整命令行、
3 秒存活检查的退出码、浏览器子进程输出（`[browser]` 前缀）全在里面。

## 目录约定

- 默认浏览器目录：`C:\Users\admin6\AppData\Roaming\adspower_global\cwd_global\chrome_152`
  （含版本子目录 `152.0.7977.54` 的那一级；`cmd.Dir` 必须设在这里，
  否则 `GetModuleFileNameW` 拼版本子目录会失败导致静默退出）
- 默认数据目录：`F:\.ADSPOWER_GLOBAL\cache`，每个子目录 = 一个 profile。
- 两个目录都可以在窗口上修改并持久化到 `sunlauncher.json`。
- 端口分配持久化到 `ports.json`，重启后复用。

## 构建（GitHub Actions，MSVC）

`.github/workflows/build-offline.yml` 在 `windows-latest` 上编译
`launcher/cpp/SunLauncher.vcxproj`，校验 web-ui 离线契约，并打包
`offline-app.zip`（SunLauncher.exe + web-ui/ + ports.json 模板）。
不在本地测试时直接去 Actions 下载。
