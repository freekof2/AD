/* AdsPower 单页版交互：环境CRUD + 指纹35+参数（目录导入/OS切UA/SOCKS5/Cookie备注） */
(function () {
  const $ = (id) => document.getElementById(id);
  const db = () => window.__db;

  function toast(msg) {
    const t = $("toast");
    t.textContent = msg;
    t.classList.add("show");
    clearTimeout(t._h);
    t._h = setTimeout(() => t.classList.remove("show"), 2200);
  }

  /* ================= 环境/账号 CRUD + 启动/停止/激活/多开队列 ================= */
  const pill = (s) =>
    s === "open"
      ? '<span class="pill open">运行中</span>'
      : s === "starting"
      ? '<span class="pill starting">启动中</span>'
      : '<span class="pill closed">已停止</span>';

  function renderProfiles(filter) {
    const tb = $("profileRows");
    if (!tb) return;
    tb.innerHTML = "";
    const list = db().profiles.filter(
      (p) => !filter || ((p.sn || "") + (p.name || "") + (p.remark || "") + (p.group || "")).toLowerCase().includes((filter || "").toLowerCase())
    );
    list.forEach((p) => {
      const tr = document.createElement("tr");
      tr.innerHTML =
        `<td><input type="checkbox" data-sn="${p.sn}"></td>` +
        `<td><b style="color:var(--primary)">${p.sn}</b></td>` +
        `<td><b>${p.name}</b></td>` +
        `<td><span class="tagchip" style="background:#f1f5f9;color:#475569;">${p.group || "-"}</span></td>` +
        `<td><code>${p.proxy || "-"}</code></td>` +
        `<td><span style="font-size:12px;color:var(--text-muted);">${p.kernel || "-"}</span></td>` +
        `<td>${pill(p.status)}</td>` +
        `<td><span style="color:var(--text-muted);font-size:12px;">${p.remark || "-"}</span></td>` +
        `<td>` +
        `<button class="btn sm primary" data-op="start" ${p.status === "open" ? "disabled" : ""}>启动</button> ` +
        `<button class="btn sm" data-op="stop" ${p.status === "closed" ? "disabled" : ""}>停止</button> ` +
        `<button class="btn sm" data-op="active">激活</button> ` +
        `<button class="btn sm" data-op="fp">指纹配置</button>` +
        `</td>`;

      tr.querySelectorAll("button[data-op]").forEach((btn) => {
        btn.addEventListener("click", async () => {
          const op = btn.dataset.op;
          try {
            if (op === "start") {
              p.status = "starting";
              renderProfiles($("globalSearch").value.trim());
              await API.startProfile(p.sn);
              p.status = "open";
              toast(`浏览器已启动：${p.name} (${p.sn})`);
            } else if (op === "stop") {
              await API.stopProfile(p.sn);
              p.status = "closed";
              toast(`浏览器已关闭：${p.name}`);
            } else if (op === "active") {
              const r = await API.activeProfile(p.sn);
              toast(`已前置激活：${r.data && r.data.ws ? "WebSocket 已连接" : "窗口已置顶"}`);
            } else if (op === "fp") {
              loadProfileToFp(p.sn);
              $("sec-fp").scrollIntoView({ behavior: "smooth" });
              toast(`已载入 ${p.sn} 的指纹配置，可在下方编辑`);
              return;
            }
            renderProfiles($("globalSearch").value.trim());
          } catch (e) {
            toast("操作失败: " + e.message);
          }
        });
      });
      tb.appendChild(tr);
    });

    $("qOpen").textContent = db().profiles.filter((p) => p.status === "open").length;
    $("qRun").textContent = db().profiles.filter((p) => p.status === "starting").length;
    $("qWait").textContent = db().profiles.filter((p) => p.status === "closed").length;
  }

  $("globalSearch").addEventListener("input", (e) => renderProfiles(e.target.value.trim()));
  $("checkAll").addEventListener("change", (e) => {
    document.querySelectorAll("#profileRows input[type=checkbox]").forEach((c) => (c.checked = e.target.checked));
  });

  document.querySelectorAll("[data-act]").forEach((b) => {
    b.addEventListener("click", () => {
      const checked = [...document.querySelectorAll("#profileRows input:checked")].map((c) => c.dataset.sn);
      if (!checked.length) return toast("请先勾选需要操作的环境！");
      const act = b.dataset.act;
      if (act === "batch-start") {
        checked.forEach((sn) => { const p = db().profiles.find((x) => x.sn === sn); if (p) p.status = "open"; });
        toast(`批量启动成功：共 ${checked.length} 个环境`);
      } else if (act === "batch-stop") {
        checked.forEach((sn) => { const p = db().profiles.find((x) => x.sn === sn); if (p) p.status = "closed"; });
        toast(`批量停止成功：共 ${checked.length} 个环境`);
      } else if (act === "batch-del") {
        if (!confirm(`确定删除选中的 ${checked.length} 个环境吗？`)) return;
        db().profiles = db().profiles.filter((p) => !checked.includes(p.sn));
        toast(`已批量删除 ${checked.length} 个环境`);
      }
      renderProfiles();
      syncFpProfileSelect();
    });
  });

  /* ---- 新建环境抽屉（精简版） ---- */
  const drawer = $("drawer");
  const mask = $("drawerMask");
  const openDrawer = () => {
    const groups = (db().groups || []).map((g) => (typeof g === "string" ? g : g.n));
    $("fGroup").innerHTML = groups.map((g) => `<option>${g}</option>`).join("") || "<option>默认分组</option>";
    drawer.classList.add("show");
    mask.classList.add("show");
  };
  const closeDrawer = () => { drawer.classList.remove("show"); mask.classList.remove("show"); };
  $("btnNewProfile").addEventListener("click", openDrawer);
  $("btnDrawerClose").addEventListener("click", closeDrawer);
  mask.addEventListener("click", closeDrawer);

  $("btnDrawerSave").addEventListener("click", () => {
    const name = $("fName").value.trim();
    if (!name) return toast("请填写环境名称");
    const newSn = "ENV-" + String(db().profiles.length + 1).padStart(3, "0");
    // 新建时直接把下方指纹面板的关键字段一起存入
    db().profiles.push({
      sn: newSn,
      name,
      group: $("fGroup").value,
      proxy: [$("fpProxyType").value, "://", $("fpProxyHost").value.trim(), $("fpProxyPort").value.trim() ? ":" + $("fpProxyPort").value.trim() : ""].join("").replace("://", $("fpProxyHost").value.trim() ? "://" : "") || "直连",
      kernel: $("fpKernelVer").selectedOptions[0].textContent,
      status: "closed",
      remark: $("fpRemark").value.trim(),
      browser: $("fpBrowser").value,
      browserDir: $("fpBrowserDir").value.trim(),
      os: $("fpOsGroup").querySelector(".active").textContent.trim(),
      ua: $("fpUA").value.trim(),
      cookie: $("fpCookie").value.trim(),
    });
    closeDrawer();
    renderProfiles();
    syncFpProfileSelect();
    // 新建后自动选中它
    $("fpProfile").value = newSn;
    toast("新环境创建成功：" + newSn + "（已关联下方指纹面板字段）");
    $("fName").value = "";
  });

  /* ================= 指纹面板：环境切换 ================= */
  function syncFpProfileSelect() {
    const opts = db().profiles.map((p) => `<option value="${p.sn}">${p.sn} - ${p.name}</option>`).join("");
    if ($("fpProfile")) $("fpProfile").innerHTML = opts;
  }

  function loadProfileToFp(sn) {
    const p = db().profiles.find((x) => x.sn === sn);
    if (!p) return;
    $("fpProfile").value = sn;
    if (p.browser) $("fpBrowser").value = p.browser;
    if (p.browserDir !== undefined) $("fpBrowserDir").value = p.browserDir || "";
    if (p.ua) $("fpUA").value = p.ua;
    if (p.cookie) $("fpCookie").value = p.cookie;
    if (p.remark !== undefined) { $("fpRemark").value = p.remark || ""; $("fpRemarkCount").textContent = ($("fpRemark").value || "").length; }
    if (p.kernel && $("fpKernelVer")) {
      [...$("fpKernelVer").options].forEach((o) => { if (o.textContent === p.kernel) $("fpKernelVer").value = o.value; });
    }
    // proxy 形如 socks5://host:port
    if (p.proxy && p.proxy.includes("://")) {
      const m = p.proxy.match(/^(socks5|http|https):\/\/(.+?)(?::(\d+))?$/);
      if (m) { $("fpProxyType").value = m[1]; $("fpProxyHost").value = m[2]; $("fpProxyPort").value = m[3] || ""; }
    }
  }
  $("fpProfile").addEventListener("change", (e) => loadProfileToFp(e.target.value));

  /* ================= 指纹面板：胶囊分段单选 ================= */
  document.querySelectorAll(".capsule-group").forEach((group) => {
    group.querySelectorAll(".capsule-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        group.querySelectorAll(".capsule-btn").forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
      });
    });
  });

  /* ================= 指纹面板：随机库 ================= */
  const RENDERER_LIST = [
    "ANGLE (Intel, Intel(R) HD Graphics (0x00002E12) Direct3D11 vs_5_0 ps_5_0, D3D11)",
    "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)",
    "ANGLE (AMD, AMD Radeon RX 6700 XT Direct3D11 vs_5_0 ps_5_0, D3D11)",
    "Apple M2 Pro (Metal 3.0)",
    "Apple M1 Max (Metal 2.4)",
  ];
  const FONT_PRESETS = [
    "Arial, Calibri, Cambria Math, Candara, Comic Sans MS, Consolas, Constantia, Corbel, Courier New, Ebrima, Franklin Gothic Medium, Gabriola, Gadugi, Georgia, Impact, Ink Free, Javanese Text... (206)",
    "Helvetica, PingFang SC, Hiragino Sans GB, Microsoft YaHei, WenQuanYi Micro Hei, Roboto, Segoe UI, Tahoma, Trebuchet MS, Verdana, Lucida Sans... (184)",
    "San Francisco, Monaco, Menlo, Consolas, Lucida Console, Apple Color Emoji, Noto Color Emoji, DejaVu Sans, Liberation Sans... (195)",
  ];
  const UA_POOL = {
    win: [
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36",
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0",
    ],
    mac: ["Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36"],
    linux: ["Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36"],
    android: ["Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Mobile Safari/537.36"],
    ios: ["Mozilla/5.0 (iPhone; CPU iPhone OS 17_4 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Mobile/15E148 Safari/604.1"],
  };
  const curOS = () => ($("fpOsGroup").querySelector(".active") || {}).dataset?.os || "win";
  const setFpUA = () => { const pool = UA_POOL[curOS()]; $("fpUA").value = pool[Math.floor(Math.random() * pool.length)]; };
  $("btnShuffleFpUA").addEventListener("click", () => { setFpUA(); toast("已按当前系统随机切换 User-Agent"); });
  if (!$("fpUA").value) setFpUA();

  function randomMac() {
    return Array.from({ length: 6 }, () => Math.floor(Math.random() * 256).toString(16).padStart(2, "0").toUpperCase()).join("-");
  }
  function randomDevName() {
    const prefixes = ["DESKTOP-", "LAPTOP", "PC-WIN11-", "MACBOOK-PRO-"];
    const chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    let s = prefixes[Math.floor(Math.random() * prefixes.length)];
    for (let i = 0; i < 7; i++) s += chars[Math.floor(Math.random() * chars.length)];
    return s;
  }

  $("btnShuffleFonts").addEventListener("click", () => {
    $("fpFontsList").textContent = FONT_PRESETS[Math.floor(Math.random() * FONT_PRESETS.length)];
    toast("已随机换一组字体列表");
  });
  $("btnShuffleRenderer").addEventListener("click", () => {
    $("fpRenderer").value = RENDERER_LIST[Math.floor(Math.random() * RENDERER_LIST.length)];
    toast("已随机更新 WebGL 渲染器");
  });
  $("btnShuffleDevName").addEventListener("click", () => { $("fpDevName").value = randomDevName(); toast("已生成随机设备名称"); });
  $("btnShuffleMac").addEventListener("click", () => { $("fpMac").value = randomMac(); toast("已生成随机 MAC 地址"); });

  $("btnFpRandom").addEventListener("click", () => {
    $("fpFontsList").textContent = FONT_PRESETS[Math.floor(Math.random() * FONT_PRESETS.length)];
    $("fpRenderer").value = RENDERER_LIST[Math.floor(Math.random() * RENDERER_LIST.length)];
    $("fpDevName").value = randomDevName();
    $("fpMac").value = randomMac();
    $("fpLat").value = (Math.random() * 80 - 40).toFixed(4);
    $("fpLng").value = (Math.random() * 360 - 180).toFixed(4);
    $("fpAccuracy").value = Math.floor(Math.random() * 3000 + 500);
    setFpUA();
    toast("底层 35+ 项指纹参数已全量随机生成完毕！");
  });

  /* ---- 从环境目录导入指纹 ---- */
  const fpPicker = $("fpDirPicker");
  $("btnFpImport").addEventListener("click", () => fpPicker && fpPicker.click());
  if (fpPicker) {
    fpPicker.addEventListener("change", async () => {
      const files = [...fpPicker.files];
      if (!files.length) return;
      const dirName = (files[0].webkitRelativePath || "").split("/")[0] || "所选目录";
      $("fpBrowserDir").value = dirName;
      $("fpImportStatus").textContent = `正在读取目录 ${dirName}（${files.length} 个文件）...`;
      const byName = {};
      files.forEach((f) => { byName[f.name.toLowerCase()] = f; });
      const readText = (f) => new Promise((res, rej) => { const r = new FileReader(); r.onload = () => res(r.result); r.onerror = rej; r.readAsText(f); });
      let hitCount = 1; // 目录名本身算 1 项
      const notes = [`浏览器目录=${dirName}`];
      try {
        if (byName["preferences"]) {
          const txt = await readText(byName["preferences"]);
          try {
            const j = JSON.parse(txt);
            const ua = j?.profile?.user_agent || j?.webkit?.webprefs?.user_agent;
            const lang = j?.intl?.accept_languages || j?.profile?.accept_languages;
            if (ua) { $("fpUA").value = ua; notes.push("UA已回填"); hitCount++; }
            if (lang) { notes.push("语言=" + lang); hitCount++; }
            const lat = j?.profile?.geolocation?.latitude ?? j?.geolocation?.latitude;
            const lng = j?.profile?.geolocation?.longitude ?? j?.geolocation?.longitude;
            if (typeof lat === "number" && typeof lng === "number") {
              $("fpLat").value = lat.toFixed(4); $("fpLng").value = lng.toFixed(4); hitCount++;
            }
          } catch (e) { notes.push("Preferences 解析失败，仅记录目录"); }
        }
        for (const key of ["local state", "secure preferences"]) {
          if (byName[key]) {
            const txt = await readText(byName[key]);
            const mTz = txt.match(/"timezone"\s*:\s*"([^"]+)"/);
            if (mTz) {
              const sel = $("fpTimezone");
              [...sel.options].forEach((o) => { if (o.value.includes(mTz[1]) || mTz[1].includes(o.value.split(" ").pop())) sel.value = o.value; });
              hitCount++;
            }
            notes.push(key + " 已扫描");
          }
        }
        if (byName["cookies"] || files.some((f) => /cookies/i.test(f.name))) { notes.push("检测到 Cookies 文件，可粘贴到下方 Cookie 框合并"); hitCount++; }
        $("fpImportStatus").textContent = `已从 ${dirName} 提取 ${hitCount} 项指纹线索：${notes.join("；")}`;
        toast("目录导入扫描完成");
      } catch (e) {
        $("fpImportStatus").textContent = "目录读取失败：" + e.message;
      }
      fpPicker.value = "";
    });
  }

  /* ---- 指纹面板：浏览器目录选择（内核目录） ---- */
  const fpDirPicker2 = $("fpBrowserDirPicker");
  $("btnPickFpBrowserDir").addEventListener("click", () => fpDirPicker2 && fpDirPicker2.click());
  if (fpDirPicker2) {
    fpDirPicker2.addEventListener("change", () => {
      const f = fpDirPicker2.files[0];
      if (f) { $("fpBrowserDir").value = (f.webkitRelativePath || "").split("/")[0]; toast("已选择浏览器/内核目录"); }
      fpDirPicker2.value = "";
    });
  }

  /* ---- 指纹面板：SOCKS5 测速 + 保存 ---- */
  $("btnFpProxyTest").addEventListener("click", async () => {
    const host = $("fpProxyHost").value.trim(), port = $("fpProxyPort").value.trim();
    if (!host || !port) return toast("请先填写代理主机和端口");
    $("fpProxyStatus").textContent = "检测中...";
    try { const r = await API.checkProxy(host + ":" + port); $("fpProxyStatus").textContent = `出口 ${r.data.ip} · ${r.data.ms}ms · 连通正常`; toast("代理检测成功"); }
    catch (e) { $("fpProxyStatus").textContent = "检测失败：" + e.message; }
  });
  $("btnFpProxySave").addEventListener("click", () => {
    const host = $("fpProxyHost").value.trim(), port = $("fpProxyPort").value.trim();
    if (!host || !port) return toast("请填写代理主机和端口");
    db().proxies.push({
      id: "proxy-" + Date.now(),
      name: `${host}:${port}`,
      type: $("fpProxyType").value,
      addr: `${host}:${port}`,
      user: $("fpProxyUser").value.trim(),
      ip: "-", ms: 0, ok: false,
    });
    toast("已保存为代理节点（供环境引用）");
  });

  /* ---- 指纹面板：Cookie 合并 + 备注计数 ---- */
  function parseCookieRaw(raw) {
    raw = (raw || "").trim();
    if (!raw) return [];
    if (raw.startsWith("[")) return JSON.parse(raw);
    if (raw.includes("#HttpOnly") || raw.startsWith(".") || /^[^\s]+\t/.test(raw)) {
      return raw.split("\n").filter((l) => (l.trim() && !l.trim().startsWith("#")) || l.includes("#HttpOnly")).map((l) => {
        const p = l.split("\t");
        if (p.length >= 7) return { name: p[5], value: p[6].trim() };
        const i = l.indexOf("="); return { name: l.slice(0, i).trim(), value: l.slice(i + 1).trim() };
      });
    }
    return raw.split(/[;\n]+/).filter(Boolean).map((kv) => {
      const i = kv.indexOf("="); return { name: kv.slice(0, i).trim(), value: kv.slice(i + 1).trim() };
    });
  }
  $("btnMergeFpCookie").addEventListener("click", () => {
    const raw = $("fpCookie").value.trim();
    if (!raw) return toast("请先粘贴 Cookie 内容");
    try {
      const arr = parseCookieRaw(raw);
      $("fpCookie").value = JSON.stringify(arr, null, 2);
      toast(`Cookie 合并成功，共 ${arr.length} 个`);
    } catch (e) { toast("Cookie 解析失败：" + e.message); }
  });
  $("fpRemark").addEventListener("input", (e) => { $("fpRemarkCount").textContent = (e.target.value || "").length; });

  /* ---- 保存指纹：写回当前选中环境 ---- */
  function collectFp() {
    const cap = (g) => { const el = document.querySelector(`[data-fp-group="${g}"] .capsule-btn.active`); return el ? el.dataset.val : ""; };
    return {
      browser: $("fpBrowser").value,
      kernelVer: $("fpKernelVer").value,
      browserDir: $("fpBrowserDir").value.trim(),
      os: curOS(),
      ua: $("fpUA").value.trim(),
      proxyType: $("fpProxyType").value,
      proxyHost: $("fpProxyHost").value.trim(),
      proxyPort: $("fpProxyPort").value.trim(),
      proxyUser: $("fpProxyUser").value.trim(),
      cookie: $("fpCookie").value.trim(),
      remark: $("fpRemark").value.trim(),
      webrtc: cap("webrtc"),
      timezoneMode: cap("timezoneMode"),
      timezone: $("fpTimezone").value,
      geoMode: cap("geoMode"),
      lat: $("fpLat").value, lng: $("fpLng").value, accuracy: $("fpAccuracy").value,
      langMode: cap("langMode"),
      uiLang: cap("uiLang"),
      resMode: cap("resMode"),
      resolution: $("fpResolution").value,
      fontMode: cap("fontMode"),
      fonts: $("fpFontsList").textContent,
      hwNoise: {
        canvas: $("swCanvas").checked, webglImg: $("swWebglImg").checked,
        audio: $("swAudio").checked, media: $("swMedia").checked,
        clientRects: $("swClientRects").checked, speech: $("swSpeech").checked,
      },
      webglMeta: cap("webglMeta"),
      vendor: $("fpVendor").value,
      renderer: $("fpRenderer").value,
      webgpu: cap("webgpu"),
      cpu: $("fpCpu").value, ram: $("fpRam").value,
      devName: $("fpDevName").value, mac: $("fpMac").value,
      doNotTrack: cap("doNotTrack"),
      portScan: cap("portScan"), whitePorts: $("fpWhitePorts").value,
      hardwareAccel: cap("hardwareAccel"),
      disableTls: cap("disableTls"),
      launchArgs: $("fpLaunchArgs").value,
    };
  }
  $("btnFpSave").addEventListener("click", () => {
    const sn = $("fpProfile").value;
    const fp = collectFp();
    const p = db().profiles.find((x) => x.sn === sn);
    if (p) {
      p.browser = fp.browser;
      p.browserDir = fp.browserDir;
      p.os = fp.os;
      p.ua = fp.ua;
      p.proxy = fp.proxyHost ? `${fp.proxyType}://${fp.proxyHost}${fp.proxyPort ? ":" + fp.proxyPort : ""}` : p.proxy;
      p.cookie = fp.cookie;
      p.remark = fp.remark;
      p.kernel = $("fpKernelVer").selectedOptions[0].textContent;
      p.fp = fp; // 全量 35+ 参数存档
      renderProfiles($("globalSearch").value.trim());
    }
    toast(`指纹配置已保存${sn ? "到 " + sn : ""}（35+ 参数全量存档）`);
  });

  /* ================= 初始化 ================= */
  renderProfiles();
  syncFpProfileSelect();
  if (db().profiles.length) loadProfileToFp(db().profiles[0].sn);
})();
