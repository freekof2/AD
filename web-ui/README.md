# SunLauncher 离线版 Web-UI

纯本机应用：目录即环境，只读写本地缓存目录，不连接 AdsPower 云端，无 token、无上报。

```
web-ui/
├── index.html       # 单入口：环境列表（目录即环境）+ 指纹 35+ 参数面板
├── css/
│   └── app.css      # 亮色现代风格、胶囊分段器、开关组件、“缓存为准”徽标
├── js/
│   ├── fp.js        # 离线指纹编解码：换表Base64 + md5三件套文件名 + 目录导入导出 + Cookie清洗
│   ├── api.js       # 离线 Local API：只调本地 :18900（/api/profiles|start|stop|fp/*），无 token
│   ├── mock.js      # file:// 双击时的演示数据；http://127.0.0.1:18900/ 下走真实后端
│   └── app.js       # 业务交互：启动/停止、目录导入、缓存为准保存、ui_fingerprint.json 侧车
└── README.md
```

## 使用

1. 运行 `SunLauncher.exe`（CI 产物 `offline-app.zip`），浏览器访问 `http://127.0.0.1:18900/`。
2. 环境列表 = 数据目录下的子目录（如 `k1h60tsv_hyg6dd`）；点启动即指纹注入启动。
3. 指纹面板：从缓存目录导入三件套解码回填；保存时写入 `ui_fingerprint.json` + cookies（清洗后）。
4. 冲突规则（以缓存为准）：`UserId/CanvasMark/WebGLMark/AudioFp/ClientRects/ProxyChain/TimeZone/Geoposition/WebRTCAddress/Langs/DeviceName/MacAddress/MediaDevices/TTSEngines` 等保护键在启动注入时被丢弃，实际生效的是 static/dynamic 文件里的值；面板上标“缓存为准”徽标。

## 自动化工作流与构建发布

GitHub Actions 工作流配置于 `.github/workflows/build-offline.yml`：
1. MSVC 编译 `launcher/cpp`（指纹注入版 SunLauncher.exe）
2. `node --check` 校验全部 JS 语法
3. 离线契约检查：web-ui 无云端域名/token/旧路由；C++ 指纹符号与离线路由齐备
4. 打包 `offline-app.zip`（SunLauncher.exe + web-ui/ + ports.json 模板）供下载使用

旧 `build-web-ui.yml`（纯前端打包）已废弃，保留仅供参考。
