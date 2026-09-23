/* Local API 封装：对齐 main.min.js 本地 Koa 路由 + 云端 /api/v1/v2。
 * 有后端时走 fetch，无后端时由 mock.js 接管（window.__mock=true）。 */
(function () {
  const $ = (id) => document.getElementById(id);
  const api = {
    get base() { return ($("apiBase") && $("apiBase").value || "http://127.0.0.1:18900").replace(/\/$/, ""); },
    get key() { return ($("apiKey") && $("apiKey").value || "").trim(); },
    async req(method, path, body) {
      if (window.__mock) return window.__mockReq(method, path, body);
      const headers = { "Content-Type": "application/json" };
      if (this.key) headers["login-token"] = this.key;
      const r = await fetch(this.base + path, {
        method, headers, body: body ? JSON.stringify(body) : undefined,
      });
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    },
    get(p) { return this.req("GET", p); },
    post(p, b) { return this.req("POST", p, b || {}); },
    // —— 环境 ——
    listProfiles() { return this.post("/api/v1/user/list", {}); },
    startProfile(sn) { return this.post("/api/v1/browser/start", { serial_number: sn }); },
    stopProfile(sn) { return this.post("/api/v1/browser/stop", { serial_number: sn }); },
    activeProfile(sn) { return this.post("/api/v1/browser/active", { serial_number: sn }); },
    // —— 代理 ——
    listProxy() { return this.get("/api/v2/proxy-list/list"); },
    checkProxy(id) { return this.post("/api/checkProxy", { id }); },
    // —— 缓存 ——
    cacheSize() { return this.get("/api/cacheSize"); },
    clearCache(sn) { return this.post("/api/clearCache", { serial_number: sn }); },
    clearAllCache() { return this.post("/api/deleteAllCache", {}); },
    // —— 内核 / 版本 ——
    getVersion() { return this.get("/api/getVersion"); },
    // —— 导入导出 ——
    exportAccounts() { return this.post("/api/export/accounts", {}); },
    exportStop() { return this.post("/api/export/stop", {}); },
  };
  window.API = api;
  // 连接指示灯
  setInterval(async () => {
    const el = $("connStatus");
    if (!el || window.__mock) { if (el) { el.textContent = "本地演示模式（mock）"; el.className = "conn"; } return; }
    try { await api.getVersion(); el.textContent = "已连接 " + api.base; el.className = "conn ok"; }
    catch (e) { el.textContent = "未连接（" + e.message + "）"; el.className = "conn"; }
  }, 5000);
})();
