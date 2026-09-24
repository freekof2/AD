/* 本地演示数据：目录即环境，mock SunLauncher 离线接口。
 * file:// 双击打开 index.html 时 File System Access 不可用，走此 mock；
 * 用 http://127.0.0.1:18900/ui/ 打开（SunLauncher 内置服务）时走真实离线后端。
 */
(function () {
  window.__mock = location.protocol === "file:";
  if (!window.__mock) return; // http(s) 下由真实后端接管
  const DB = {
    profiles: [
      { name: "k1h60tsv_hyg6dd", group: "默认", proxy: "socks5://127.0.0.1:1200", kernel: "Chrome 152 (SunBrowser)", status: "closed", remark: "示例（mock）", browser: "sun", browserDir: "", ua: "", cookie: "" },
    ],
    groups: ["默认"],
    proxies: [],
  };
  window.__db = DB;
  window.__mockReq = async function (method, path, body) {
    await new Promise((r) => setTimeout(r, 150));
    if (path === "/api/profiles") {
      return DB.profiles.map((p) => ({ name: p.name, running: p.status === "open", pid: p.pid || 0, port: p.port || 0 }));
    }
    if (path === "/api/start") {
      const p = DB.profiles.find((x) => x.name === (body && body.name));
      if (p) { p.status = "open"; p.port = p.port || 19222; }
      return { ok: true, pid: 0, port: p ? p.port : 0, mock: true };
    }
    if (path === "/api/stop") {
      const p = DB.profiles.find((x) => x.name === (body && body.name));
      if (p) p.status = "closed";
      return { ok: true, killed: [], mock: true };
    }
    if (path.indexOf("/api/fp/") === 0 && method === "GET") {
      return { json: "", mock: true, note: "mock 下无真实缓存，请用目录导入" };
    }
    if (path === "/api/fp/save") return { ok: true, mock: true, notes: ["mock：未写盘"] };
    return {};
  };
})();
