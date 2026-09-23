# ADSPOWER 功能原型 · web-ui

纯静态三件套（HTML / CSS / JS，零依赖），双击 `index.html` 即可打开。
视觉与信息架构对齐 AdsPower Global 客户端（深色侧边栏 + 环境列表 + 右侧批量操作），字段命名对齐逆向得到的后端路由。

## 目录

| 文件 | 说明 |
|---|---|
| `index.html` | 8 个功能页 + 新建环境抽屉，单文件入口 |
| `css/app.css` | 深色主题样式 |
| `js/api.js` | Local API 封装，对齐 `/api/v1` · `/api/v2` · 本地 Koa 路由；无后端时自动走 mock |
| `js/mock.js` | 本地演示数据，接入真实后端后删除引用即可 |
| `js/app.js` | 8 模块渲染与交互 |

## 8 大模块与后端路由对照

| 页面 | 对齐的后端接口 |
|---|---|
| 环境管理 | `/api/v1/browser/start` · `stop` · `active`，多开队列 `OPEN_$$` |
| 分组/标签 | `/api/v1/user/regroup` · `browser-tags` · `user-group` · `category/list` |
| 代理管理 | `/api/v2/proxy-list/*` · `/api/checkProxy`（字段 proxy_soft/host/port/user/password） |
| 指纹配置 37 项 | `sunBrowserParams.*` / `staticConfig.*`（Canvas/WebGL/Audio/WebRTC/时区/地理/字体/TLS…） |
| Cookie/养号 | `browser-profile/cookies` · Cookie 机器人任务 |
| 缓存/备份 | `/api/clearCache` · `/api/deleteAllCache` · `/api/restoreBackup` |
| 内核管理 | `/api/getVersion` · `kernels` / `download-kernel` |
| 导入/导出 | `import/start` · `/api/export/accounts` · `/api/export/stop` |

## 接入真实后端

1. 启动本地 API 服务（默认 `http://127.0.0.1:18900`）；
2. 在侧栏底部填写 Local API 地址与 API Key；
3. 删除 `index.html` 中的 `<script src="js/mock.js"></script>` 一行，页面即走真实 `fetch`。

> 当前为**纯前端功能原型**，只做界面与交互（先看哪些功能要改）。
> 后端（Electron 壳 / Koa 本地 API / SunBrowser 拉起）不在本目录，待界面定稿后再按此原型联调。
