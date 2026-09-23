# AdsPower Global Web-UI 前端原型

基于逆向提取的真实路由协议规范（`/api/v1`、`/api/v2`、本地 Koa 路由）及参考截图 `ADSPower.png` 100% 精确复刻的纯前端交互界面。

零第三方运行依赖，原生 HTML5 + CSS3 + Vanilla ES6，双击 `index.html` 即可在任意现代浏览器运行体验。

---

## 包含的 8 大克隆功能模块

1. **环境 / 账号管理 (CRUD + 启动/停止/激活/多开队列)**
   * 对齐接口：`/api/v1/user/list`、`/api/v1/browser/start`、`/api/v1/browser/stop`、`/api/v1/browser/active`
   * 支持多开调度队列统计（等待中 / 启动中 / 运行中）
   * 批量启动、批量停止、批量删除及新建环境抽屉弹窗
2. **分组 / 标签 / 分类**
   * 对齐接口：`/api/v1/user/regroup`、`browser-tags`、`user-group`、`/category/list`
   * 支持分组重命名、标签管理与分类概览
3. **代理管理**
   * 对齐接口：`/api/v2/proxy-list/*`、`/api/checkProxy`
   * 字段支持：`proxy_soft`、`host`、`port`、`user`、`password`
   * 支持 HTTP / HTTPS / SOCKS5 节点添加与一键测速 / 出口 IP 探测
4. **指纹配置（35+ 底层参数，100% 对齐 ADSPower.png）**
   * **WebRTC**：转发 / 替换 / 真实 / 禁用 / 代理UDP 胶囊切换
   * **时区**：基于 IP / 真实 / 自定义（全时区列表）
   * **地理位置**：基于 IP / 自定义 / 禁止，每次询问 / 始终允许，经纬度与精度输入
   * **语言与界面语言**：基于语言 / 真实 / 自定义多语言管理
   * **分辨率**：基于 User-Agent / 1080P / 2K 等预定义与自定义
   * **字体**：206+ 常用字体清单与一键“换一换”
   * **硬件噪音 (开关组)**：Canvas、WebGL图像、AudioContext、媒体设备 (Auto)、ClientRects、SpeechVoices
   * **WebGL 元数据**：厂商 (Google/Intel/Apple/NVIDIA/AMD) + 渲染器 ANGLE 随机换一换
   * **WebGPU**：基于 WebGL / 真实 / 禁用
   * **CPU / RAM**：核心数与内存容量精确选择
   * **设备名称 & MAC 地址**：真实 / 自定义与随机生成器
   * **Do Not Track & 端口扫描保护**：默认/开启/关闭，端口白名单
   * **硬件加速 & 禁用 TLS 特性**
   * **启动参数**：自定义 Chrome/SunBrowser 命令行参数
5. **Cookie + Cookie 机器人养号**
   * 对齐接口：`/api/v2/browser-profile/cookies`、Cookie 机器人任务流
   * 支持读取/写入 `BROWSER_ID`、`CLIENT_HOST` 及 Session Cookie
6. **缓存 / 备份 / 清理**
   * 对齐接口：`/api/clearCache`、`/api/deleteAllCache`、`/api/restoreBackup`
   * 支持单环境清理、全量清理、一键创建快照与快照恢复
7. **内核管理**
   * 对齐接口：`/api/getVersion`、`kernels`
   * SunBrowser (Chrome 143/121) 与 FlowerBrowser (Firefox 128) 内核状态与下载管理
8. **导入 / 导出 / 批量**
   * 对齐接口：`import/start`、`/api/export/accounts`、`/api/export/stop`
   * 支持 Excel/TXT/CSV 文件导入进度模拟与全量导出、中途停止

---

## 目录结构

```
web-ui/
├── index.html       # 单入口主页面，包含侧边栏导航、8 大功能模块及新建抽屉
├── css/
│   └── app.css      # 严格对齐 ADSPower.png 的亮色现代风格、胶囊分段器与开关组件
├── js/
│   ├── api.js       # Local API 请求封装，支持无缝切换真实后端与 Mock
│   ├── mock.js      # 本地离线演示数据，开箱即用
│   └── app.js       # 8 大功能模块的完整交互逻辑与指纹随机生成算法
└── README.md
```

## 自动化工作流与构建发布

GitHub Actions 工作流配置于 `.github/workflows/build-web-ui.yml`：
1. 校验必选文件与 HTML 8 大模块完整性
2. 通过 Node.js 校验全部 JavaScript 代码语法
3. 打包压缩生成 `web-ui.zip` 产物供下载使用
