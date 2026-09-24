/* 离线 Local API：对接 SunLauncher 本地 :18900（只读/写本机缓存，无云端、无 token）。
 * 路由（SunLauncher main.cpp 内置 HTTP 状态接口扩展后提供）：
 *   GET  /api/profiles            -> [{name, running, pid, port}]
 *   POST /api/start {name}        -> {ok, pid, port}        指纹注入启动
 *   POST /api/stop  {name}        -> {ok, killed}           进程树关闭
 *   GET  /api/fp/static?name=xxx  -> {json}                 读 static 解码 JSON
 *   GET  /api/fp/dynamic?name=xxx -> {json}
 *   GET  /api/fp/cookies?name=xxx -> {json}
 *   GET  /api/fp/ui?name=xxx      -> {json}                 读 ui_fingerprint.json 明文
 *   POST /api/fp/save {name, static, dynamic, cookies, ui}
 * 无后端时（mock.js）走本地演示数据；有后端时直连，不带任何 token。
 */
(function () {
  const $ = (id) => document.getElementById(id);
  const api = {
    get base() { return ($("apiBase") && $("apiBase").value || "http://127.0.0.1:18900").replace(/\/$/, ""); },
    async req(method, path, body) {
      if (window.__mock) return window.__mockReq(method, path, body);
      const r = await fetch(this.base + path, {
        method,
        headers: { "Content-Type": "application/json" },
        body: body ? JSON.stringify(body) : undefined,
      });
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    },
    get(p) { return this.req("GET", p); },
    post(p, b) { return this.req("POST", p, b || {}); },
    // —— 本地 profile（目录即环境） ——
    listProfiles() { return this.get("/api/profiles"); },
    startProfile(name) { return this.post("/api/start", { name }); },
    stopProfile(name) { return this.post("/api/stop", { name }); },
    // —— 指纹三件套 + UI 侧车 ——
    fpGet(kind, name) { return this.get("/api/fp/" + kind + "?name=" + encodeURIComponent(name)); },
    fpSave(name, payload) { return this.post("/api/fp/save", Object.assign({ name }, payload)); },
  };
  window.API = api;
  // 连接指示灯：只探本地，不探云端
  setInterval(async () => {
    const el = $("connStatus");
    if (!el || window.__mock) { if (el) { el.textContent = "本地演示模式（mock）"; el.className = "conn"; } return; }
    try { await api.listProfiles(); el.textContent = "已连接 " + api.base + "（本机离线）"; el.className = "conn ok"; }
    catch (e) { el.textContent = "未连接本地启动器（" + e.message + "）"; el.className = "conn"; }
  }, 5000);
})();
