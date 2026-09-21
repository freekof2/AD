# SunLauncher

类 AdsPower 的 `SunBrowser.exe` 启动器：Web 界面列出数据目录下的 profile，
一键启动/关闭，每个运行中的 profile 自动分配 `remote-debugging-port`。

只做进程拉起/关闭（`CreateProcess` 级别），不注入、不读浏览器内存。
启动参数透传自 Ghidra 反编译结论（`FUN_140001000` / `chrome_exe_main_win.cc`）：

- `--user-data-dir=<数据目录>/<profile>`
- `--profile-directory=Default`
- `--remote-debugging-port=<自动分配>`
- `--no-first-run --no-default-browser-check about:blank`

## 目录约定

- 默认浏览器目录：`C:\Users\admin6\AppData\Roaming\adspower_global\cwd_global\chrome_152`
  （含版本子目录 `152.0.7977.54` 的那一级；`cmd.Dir` 必须设在这里，
  否则 `GetModuleFileNameW` 拼版本子目录会失败导致静默退出）
- 默认数据目录：`F:\.ADSPOWER_GLOBAL\cache`，每个子目录 = 一个 profile。
- 两个目录都可以在 Web 界面上修改并持久化到 `sunlauncher.json`。
- 端口分配持久化到 `ports.json`，重启后复用。

## 本地编译（需要 Go 工具链）

```powershell
cd launcher
go build -o SunLauncher.exe .
.\SunLauncher.exe
# 浏览器打开 http://127.0.0.1:18900
```

## GitHub Actions 构建

`.github/workflows/build-launcher.yml` 在 `windows-latest` 上编译并上传
`SunLauncher.exe` 产物。不在本地测试时直接去 Actions 下载。
