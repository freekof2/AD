/* 主交互：8 模块渲染 + 抽屉 + toast。对齐后端字段命名，注释标注来源路由。 */
(function () {
  const $ = (id) => document.getElementById(id);
  const db = () => window.__db;

  function toast(msg) {
    const t = $("toast"); t.textContent = msg; t.classList.add("show");
    clearTimeout(t._h); t._h = setTimeout(() => t.classList.remove("show"), 2000);
  }

  /* ---------- 导航 ---------- */
  const titles = { profiles: "环境管理", groups: "分组 / 标签", proxy: "代理管理", fingerprint: "指纹配置", cookie: "Cookie / 养号", cache: "缓存 / 备份", kernel: "内核管理", imexport: "导入 / 导出" };
  document.querySelectorAll(".nav-item").forEach((b) => b.addEventListener("click", () => {
    document.querySelectorAll(".nav-item").forEach((x) => x.classList.remove("active"));
    b.classList.add("active");
    document.querySelectorAll(".page").forEach((x) => x.classList.remove("active"));
    $("page-" + b.dataset.page).classList.add("active");
    $("pageTitle").textContent = titles[b.dataset.page];
    $("btnNewProfile").style.display = b.dataset.page === "profiles" ? "" : "none";
  }));

  /* ---------- 1. 环境 ---------- */
  const pill = (s) => s === "open" ? '<span class="pill open">已启动</span>' : s === "starting" ? '<span class="pill starting">启动中</span>' : '<span class="pill closed">已关闭</span>';
  function renderProfiles(filter) {
    const tb = $("profileRows"); tb.innerHTML = "";
    db().profiles.filter((p) => !filter || (p.sn + p.name + p.remark).includes(filter)).forEach((p) => {
      const tr = document.createElement("tr");
      tr.innerHTML = `<td><input type="checkbox" data-sn="${p.sn}"></td><td>${p.sn}</td><td>${p.name}</td>` +
        `<td>${p.group}</td><td>${p.tags.map((t) => `<span class="tagchip">${t}</span>`).join("")}</td>` +
        `<td>${p.proxy}</td><td>${p.kernel}</td><td>${pill(p.status)}</td><td>${p.remark || "-"}</td>` +
        `<td><button class="btn sm" data-op="start">启动</button> <button class="btn sm" data-op="stop">关闭</button> <button class="btn sm" data-op="active">激活</button></td>`;
      tr.querySelectorAll("button").forEach((b) => b.addEventListener("click", async () => {
        try {
          if (b.dataset.op === "start") await API.startProfile(p.sn);       // /api/v1/browser/start
          if (b.dataset.op === "stop") await API.stopProfile(p.sn);         // /api/v1/browser/stop
          if (b.dataset.op === "active") { const r = await API.activeProfile(p.sn); toast("激活成功：" + (r.data && r.data.ws || "已前置")); } // /api/v1/browser/active
          else toast("操作成功：" + p.sn);
          renderProfiles($("globalSearch").value.trim());
        } catch (e) { toast("失败：" + e.message); }
      }));
      tb.appendChild(tr);
    });
    // 队列
    $("qOpen").textContent = db().profiles.filter((p) => p.status === "open").length;
    $("qRun").textContent = db().profiles.filter((p) => p.status === "starting").length;
    $("qWait").textContent = db().profiles.filter((p) => p.status === "closed").length;
  }
  $("globalSearch").addEventListener("input", (e) => renderProfiles(e.target.value.trim()));
  $("checkAll").addEventListener("change", (e) => document.querySelectorAll("#profileRows input[type=checkbox]").forEach((c) => c.checked = e.target.checked));
  document.querySelectorAll('[data-act]').forEach((b) => b.addEventListener("click", () => {
    const sel = [...document.querySelectorAll("#profileRows input:checked")].map((c) => c.dataset.sn);
    toast(sel.length ? `${b.dataset.act}：${sel.join(",")}` : "请先勾选环境");
  }));

  /* 抽屉 */
  const openDrawer = (isNew) => {
    $("drawerTitle").textContent = isNew ? "新建环境" : "编辑环境";
    $("fGroup").innerHTML = db().groups.map((g) => `<option>${g.n}</option>`).join("");
    $("fProxy").innerHTML = db().proxies.map((p) => `<option value="${p.id}">${p.name}</option>`).join("") + "<option>直连</option>";
    $("drawer").classList.add("show"); $("drawerMask").classList.add("show");
  };
  $("btnNewProfile").addEventListener("click", () => openDrawer(true));
  $("btnDrawerClose").addEventListener("click", closeDrawer);
  $("drawerMask").addEventListener("click", closeDrawer);
  function closeDrawer() { $("drawer").classList.remove("show"); $("drawerMask").classList.remove("show"); }
  $("btnDrawerSave").addEventListener("click", () => {
    const name = $("fName").value.trim();
    if (!name) return toast("请填写名称");
    db().profiles.push({ sn: "ENV-" + String(db().profiles.length + 1).padStart(3, "0"), name, group: $("fGroup").value, tags: [], proxy: $("fProxy").value, kernel: $("fKernel").value, status: "closed", remark: $("fRemark").value });
    closeDrawer(); renderProfiles(); toast("已创建：" + name);
  });

  /* ---------- 2. 分组/标签 ---------- */
  function renderGroups() {
    $("groupList").innerHTML = db().groups.map((g) => `<li>${g.n}<span class="cnt">${g.c} 个环境</span><button class="btn sm">改名</button></li>`).join("");
    $("tagList").innerHTML = db().tags.map((t) => `<li><span class="tagchip">${t}</span><button class="btn sm">删除</button></li>`).join("");
    $("catList").innerHTML = db().cats.map((c) => `<li>${c}<button class="btn sm">改名</button></li>`).join("");
  }
  $("btnAddGroup").addEventListener("click", () => { const n = prompt("新分组名："); if (n) { db().groups.push({ n, c: 0 }); renderGroups(); } });
  $("btnAddTag").addEventListener("click", () => { const n = prompt("新标签名："); if (n) { db().tags.push(n); renderGroups(); } });

  /* ---------- 3. 代理 ---------- */
  function renderProxy() {
    $("proxyRows").innerHTML = db().proxies.map((p) =>
      `<tr><td><input type="checkbox"></td><td>${p.name}</td><td>${p.type}</td><td>${p.addr}</td><td>${p.user || "-"}</td>` +
      `<td>${p.ip || "-"}</td><td>${p.ms ? p.ms + "ms" : "-"}</td>` +
      `<td>${p.ok ? '<span class="pill open">正常</span>' : '<span class="pill err">异常</span>'}</td>` +
      `<td><button class="btn sm" data-ck="${p.id}">检测</button> <button class="btn sm">编辑</button></td></tr>`).join("");
    document.querySelectorAll("[data-ck]").forEach((b) => b.addEventListener("click", async () => {
      toast("检测中…"); const r = await API.checkProxy(b.dataset.ck); // /api/checkProxy
      toast(`出口 ${r.data.ip} · ${r.data.ms}ms`);
    }));
  }
  $("btnAddProxy").addEventListener("click", () => toast("演示模式：请在接入后端后使用 proxy-list/create"));
  $("btnCheckProxy").addEventListener("click", () => toast("演示模式：已检测全部代理"));

  /* ---------- 4. 指纹（35+ 参数，对齐 sunBrowserParams/staticConfig） ---------- */
  // 字段：[key, 中文名, 类型, 选项/说明]
  const FP = [
    ["ua", "User-Agent", "text", "browser-profile/ua"], ["platform", "Platform", "select", "Win32/MacIntel/Linux"],
    ["language", "语言", "text", "zh-CN,en"], ["screenResolution", "分辨率", "text", "1920_1080"],
    ["colorDepth", "色深", "select", "24/30/48"], ["hardwareConcurrency", "CPU核心数", "select", "default/4/8/16"],
    ["deviceMemory", "设备内存(GB)", "select", "default/4/8"], ["doNotTrack", "DoNotTrack", "select", "true/false"],
    ["canvas", "Canvas开关", "select", "0/1"], ["canvasId", "Canvas种子", "text", "fbccId派生"],
    ["webglImage", "WebGL图像", "select", "0/1"], ["webglImageId", "WebGL种子", "text", "fbccId派生"],
    ["audio", "Audio指纹", "select", "0/1"], ["audioId", "Audio种子", "text", "setAudioNoise"],
    ["clientRects", "ClientRects", "select", "0/1"], ["clientRectsId", "ClientRects种子", "text", "hashcode%2e4"],
    ["webrtc", "WebRTC模式", "select", "disabled/disable_udp/proxy/forward"], ["webrtcIp", "WebRTC出口IP", "text", "WebRTCAddress"],
    ["timezone", "时区", "text", "Asia/Shanghai"], ["tzAuto", "时区跟随代理", "select", "0/1"],
    ["country", "国家", "text", "US"], ["city", "城市", "text", ""], ["latitude", "纬度", "text", ""], ["longitude", "经度", "text", ""], ["accuracy", "精度", "text", ""],
    ["geolocation", "地理位置模式", "select", "allow/block"], ["scanPortType", "端口扫描", "select", "0/1"], ["allowScanPorts", "允许端口", "text", ""],
    ["fonts", "字体", "select", "all/custom"], ["proxySoft", "代理软件", "text", "ipService等"], ["proxyUser", "代理账号", "text", "ProxyUser"], ["proxyPassword", "代理密码", "text", "ProxyPassword"],
    ["flash", "Flash", "select", "allow/block"], ["touchPoints", "触摸点", "text", "MaxTouchPoints"],
    ["vendor", "Vendor", "text", "Google Inc."], ["mobileMode", "移动模式", "select", "0/1"], ["disableImage", "禁用图片", "select", "0/1"],
    ["dpi", "DPR", "text", "1"], ["tlsFp", "TLS指纹", "text", "setTls"], ["cookiesFile", "Cookies文件", "text", "md5(fbccId)"],
  ];
  function fpField(f) {
    const [k, name, type, opt] = f;
    if (type === "select") return `<label>${name} <span class="hint">${k}</span><select data-fp="${k}">${opt.split("/").map((o) => `<option>${o}</option>`).join("")}</select></label>`;
    return `<label>${name} <span class="hint">${k}</span><input data-fp="${k}" placeholder="${opt}"></label>`;
  }
  function renderFp() {
    $("fpCount").textContent = FP.length;
    const q = (arr) => arr.map(fpField).join("");
    $("fpBase").innerHTML = q(FP.slice(0, 8));
    $("fpMedia").innerHTML = q(FP.slice(8, 16));
    $("fpNet").innerHTML = q(FP.slice(16, 27));
    $("fpHw").innerHTML = q(FP.slice(27));
    $("fpProfile").innerHTML = db().profiles.map((p) => `<option>${p.sn} ${p.name}</option>`).join("");
  }
  $("btnFpRandom").addEventListener("click", () => {
    document.querySelectorAll("[data-fp]").forEach((el) => { if (el.tagName === "INPUT" && el.placeholder.includes("派生")) el.value = Math.floor(Math.random() * 999999); });
    toast("已随机种子（演示）");
  });
  $("btnFpSave").addEventListener("click", () => toast("已保存指纹（演示）：/api/v1/user/update"));

  /* ---------- 5. Cookie ---------- */
  function renderCookie() { $("ckProfile").innerHTML = db().profiles.map((p) => `<option>${p.sn}</option>`).join(""); renderRobots(); }
  function renderRobots() {
    $("robotRows").innerHTML = db().robots.map((r) => `<tr><td>${r.name}</td><td>${r.envs}</td><td>${r.status}</td><td><button class="btn sm">${r.status === "运行中" ? "停止" : "启动"}</button></td></tr>`).join("");
  }
  $("btnCkGet").addEventListener("click", () => { $("ckText").value = JSON.stringify([{ name: "BROWSER_ID", value: "demo-fbcc-001" }, { name: "CLIENT_HOST", value: "127.0.0.1:18900" }], null, 2); });
  $("btnCkSet").addEventListener("click", () => toast("已写入 Cookie（演示）"));
  $("btnRobotAdd").addEventListener("click", () => { const n = $("robotName").value.trim() || "新任务"; db().robots.push({ name: n, envs: 0, status: "已停止" }); renderRobots(); });

  /* ---------- 6. 缓存 ---------- */
  document.querySelectorAll("[data-cache]").forEach((b) => b.addEventListener("click", async () => {
    if (b.dataset.cache === "all") { await API.clearAllCache(); toast("全部缓存已清理（演示）"); } // /api/deleteAllCache
    else { await API.clearCache("ENV-001"); toast("ENV-001 缓存已清理"); } // /api/clearCache
  }));
  $("btnBackup").addEventListener("click", () => { db().backups.unshift("backup-2026-09-23.zip (1.3G)"); renderBackups(); });
  $("btnRestore").addEventListener("click", () => toast("恢复备份（演示）：/api/restoreBackup"));
  function renderBackups() { $("backupList").innerHTML = db().backups.map((b) => `<li>${b}<button class="btn sm">恢复</button></li>`).join(""); }

  /* ---------- 7. 内核 ---------- */
  function renderKernel() {
    API.getVersion().then((r) => { $("kernelInfo").textContent = `当前客户端 ${r.data.version} · 内核基线 ${r.data.kernel}`; }); // /api/getVersion
    $("kernelRows").innerHTML = db().kernels.map((k) =>
      `<tr><td>${k.v}</td><td>${k.t}</td><td>${k.size}</td><td>${k.st}</td>` +
      `<td>${k.st === "已安装" ? '<button class="btn sm">设为默认</button>' : '<button class="btn sm">下载</button>'}</td></tr>`).join("");
  }
  $("btnKernelCheck").addEventListener("click", () => toast("已是最新内核（演示）"));

  /* ---------- 8. 导入导出 ---------- */
  function fakeProgress(bar, status, done) {
    let p = 0; const h = setInterval(() => { p += 10; $(bar).style.width = p + "%"; $(status).textContent = `进度 ${p}%`; if (p >= 100) { clearInterval(h); toast(done); } }, 200);
  }
  $("btnImport").addEventListener("click", () => fakeProgress("impBar", "impStatus", "导入完成（演示）：import/start"));
  $("btnExport").addEventListener("click", () => { API.exportAccounts(); fakeProgress("expBar", "expStatus", "导出完成（演示）：/api/export/accounts"); }); // /api/export/accounts
  $("btnExportStop").addEventListener("click", () => { API.exportStop(); toast("已停止导出"); }); // /api/export/stop

  /* ---------- 启动 ---------- */
  renderProfiles(); renderGroups(); renderProxy(); renderFp(); renderCookie(); renderBackups(); renderKernel();
})();
