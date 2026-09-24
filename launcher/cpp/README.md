# SunLauncher（C++ 版）

Win32 原生单窗口启动器，替代旧 Go 版（`launcher/main.go` 已删除）。
点 profile 的**启动**后如果没有窗口出现，直接看 `debug.log` 即可定位。

## 无窗口三板斧（都在 debug.log 里）

1. **完整命令行**：`CreateProcess cmd=...` 那一行就是实际传给
   `SunBrowser.exe` 的全部参数，复制出来手工跑一遍即可复现；
2. **退出码**：`SunBrowser 3 秒内退出 exit=...` —— Chromium 启动器
   走静默退出分支（拼 `152.0.7977.54\chrome.dll` 失败等）就是这个表现；
3. **浏览器自己的输出**：子进程 stdout/stderr 通过管道重定向，
   以 `[browser]` 前缀逐行写入 debug.log（含 `Failed to load Chrome DLL` 类信息）。

`debug.log` 在 `SunLauncher.exe` 同目录，启动时自动把上一轮改名为
`debug.prev.log`。窗口里的日志框显示 tail（最后 ~40 行），
`打开日志目录` 按钮可直接定位文件。

## 本地编译（MSVC）

```powershell
cd launcher\cpp
msbuild SunLauncher.vcxproj /p:Configuration=Release /p:Platform=x64
```

## GitHub Actions 构建

`.github/workflows/build-launcher.yml` 在 `windows-latest` 上用
`microsoft/setup-msbuild` + `msbuild` 编译并上传 `SunLauncher.exe`。
不在本地测试时直接去 Actions 下载产物。
