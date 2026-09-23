/* 本地演示数据：单页版仅需 profiles / groups / proxies。
 * 接入真实后端后删除本文件引用即可（api.js 会自动走 fetch）。 */
(function () {
  window.__mock = true;
  const DB = {
    profiles: [
      { sn: "ENV-001", name: "亚马逊-主账号", group: "电商", proxy: "socks5://1.2.3.4:1080", kernel: "Chrome 143 (SunBrowser 150)", status: "open", remark: "美国站", browser: "sun", browserDir: "", ua: "", cookie: "" },
      { sn: "ENV-002", name: "FB-广告01", group: "广告", proxy: "http://5.6.7.8:8080", kernel: "Chrome 143 (SunBrowser 150)", status: "closed", remark: "", browser: "sun", browserDir: "", ua: "", cookie: "" },
      { sn: "ENV-003", name: "TikTok-小号03", group: "社媒", proxy: "socks5://9.9.9.9:1080", kernel: "Chrome 121 (SunBrowser 121)", status: "closed", remark: "", browser: "sun", browserDir: "", ua: "", cookie: "" },
      { sn: "ENV-004", name: "谷歌-测试", group: "测试", proxy: "直连", kernel: "Firefox 128 (FlowerBrowser)", status: "closed", remark: "", browser: "flower", browserDir: "", ua: "", cookie: "" },
    ],
    groups: ["电商", "广告", "社媒", "测试"],
    proxies: [
      { id: "proxy-us-01", name: "美国住宅-01", type: "socks5", addr: "1.2.3.4:1080", user: "u01", ip: "1.2.3.4", ms: 180, ok: true },
      { id: "proxy-uk-02", name: "英国机房-02", type: "http", addr: "5.6.7.8:8080", user: "u02", ip: "5.6.7.8", ms: 240, ok: true },
      { id: "proxy-sg-01", name: "新加坡-01", type: "socks5", addr: "9.9.9.9:1080", user: "", ip: "9.9.9.9", ms: 0, ok: false },
    ],
    robots: [{ name: "FB养号-每日浏览", envs: 12, status: "运行中" }, { name: "TK点赞任务", envs: 5, status: "已停止" }],
    backups: ["backup-2026-09-20.zip (1.2G)", "backup-2026-09-15.zip (1.1G)"],
    kernels: [
      { v: "Chrome 143.0.7499", t: "SunBrowser", size: "231MB", st: "已安装" },
      { v: "Chrome 121.0.6167", t: "SunBrowser", size: "228MB", st: "已安装" },
      { v: "Firefox 128", t: "Gecko", size: "190MB", st: "可下载" },
    ],
  };
  window.__db = DB;
  window.__mockReq = async function (method, path, body) {
    await new Promise((r) => setTimeout(r, 200)); // 模拟延迟
    if (path.includes("/api/v1/browser/start")) { const p = DB.profiles.find((x) => x.sn === body.serial_number); if (p) p.status = "open"; return { code: 0 }; }
    if (path.includes("/api/v1/browser/stop")) { const p = DB.profiles.find((x) => x.sn === body.serial_number); if (p) p.status = "closed"; return { code: 0 }; }
    if (path.includes("/api/v1/browser/active")) return { code: 0, data: { ws: "ws://127.0.0.1:9222/devtools/browser/xxx" } };
    if (path.includes("proxy-list")) return { code: 0, data: DB.proxies };
    if (path.includes("checkProxy")) return { code: 0, data: { ip: "1.2.3.4", ms: 180 } };
    if (path.includes("cacheSize")) return { code: 0, data: { size: "3.4G" } };
    if (path.includes("getVersion")) return { code: 0, data: { version: "8.7.23", kernel: "150.0.7871.47" } };
    return { code: 0, data: {} };
  };
})();
