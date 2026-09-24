/* SunLauncher 离线版交互：目录即环境 + 指纹注入启动/停止 + 参数保存。
 * 零网络：只调本地 :18900 离线接口（api.js）或 File System Access 直读写（fp.js）。 */
(function () {
  const $ = (id) => document.getElementById(id);
  const db = () => window.__db;
  // 目录名即环境名（mock/离线统一为 name；兼容旧 mock 的 sn 字段）
  const pname = (p) => p.name || p.sn;
  let dirHandle = null;   // 当前导入/导出的目录句柄（File System Access）
  let dirName = "";       // 当前目录名（= 环境名）

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
      (p) => !filter || ((pname(p) || "") + (p.name || "") + (p.remark || "") + (p.group || "")).toLowerCase().includes((filter || "").toLowerCase())
    );
    list.forEach((p) => {
      const nm = pname(p);
      const tr = document.createElement("tr");
      tr.innerHTML =
        `<td><input type="checkbox" data-sn="${nm}"></td>` +
        `<td><b style="color:var(--primary)">${nm}</b></td>` +
        `<td><b>${p.name && p.sn ? p.name : (p.remark && p.remark.slice(0, 12)) || nm}</b></td>` +
        `<td><span class="tagchip" style="background:#f1f5f9;color:#475569;">${p.group || "默认"}</span></td>` +
        `<td><code>${p.proxy || "-"}</code></td>` +
        `<td><span style="font-size:12px;color:var(--text-muted);">${p.kernel || "-"}</span></td>` +
        `<td>${pill(p.status)}</td>` +
        `<td><span style="color:var(--text-muted);font-size:12px;">${p.remark || "-"}</span></td>` +
        `<td>` +
        `<button class="btn sm primary" data-op="start" ${p.status === "open" ? "disabled" : ""}>启动</button> ` +
        `<button class="btn sm" data-op="stop" ${p.status === "closed" ? "disabled" : ""}>停止</button> ` +
        `<button class="btn sm" data-op="fp">指纹配置</button>` +
        `</td>`;

      tr.querySelectorAll("button[data-op]").forEach((btn) => {
        btn.addEventListener("click", async () => {
          const op = btn.dataset.op;
          try {
            if (op === "start") {
              p.status = "starting";
              renderProfiles($("globalSearch").value.trim());
              // 离线启动：本地 :18900 指纹注入；mock 下仅改状态
              const r = await API.startProfile(nm);
              p.status = "open";
              if (r && r.port) p.port = r.port;
              toast(`浏览器已启动：${nm}${r && r.port ? "（port=" + r.port + "）" : ""}${r && r.mock ? "【演示，未真实启动】" : ""}`);
            } else if (op === "stop") {
              const r = await API.stopProfile(nm);
              p.status = "closed";
              toast(`浏览器已关闭：${nm}${r && r.killed && r.killed.length ? "（结束 " + r.killed.length + " 个进程）" : ""}`);
            } else if (op === "fp") {
              loadProfileToFp(nm);
              $("sec-fp").scrollIntoView({ behavior: "smooth" });
              toast(`已载入 ${nm} 的指纹配置，可在下方编辑`);
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
    // 离线：目录即环境。目录名规则 <fbccId>_<invite>（如下划线前段即环境 ID）；
    // 输入中文名时自动生成 ascii 目录名，中文名存 remark。
    if (!name) return toast("请填写环境名称");
    if (/[\\/ :*?"<>|]/.test(name)) return toast("名称含非法字符（\\ / : * ? \" < > |），请修改");
    let dirName = name;
    if (/[^\x00-\x7F_]/.test(name) || name.indexOf("_") < 0) {
      dirName = "env" + Date.now().toString(36) + "_local";
    }
    if (db().profiles.some((p) => pname(p) === dirName)) return toast("该环境目录已存在");
    // 新建时直接把下方指纹面板的关键字段一起存入
    db().profiles.push({
      name: dirName,
      group: $("fGroup").value,
      proxy: [$("fpProxyType").value, "://", $("fpProxyHost").value.trim(), $("fpProxyPort").value.trim() ? ":" + $("fpProxyPort").value.trim() : ""].join("").replace("://", $("fpProxyHost").value.trim() ? "://" : "") || "直连",
      kernel: $("fpKernelVer").selectedOptions[0].textContent,
      status: "closed",
      remark: (/[^\x00-\x7F_]/.test(name) ? name + " " : "") + $("fpRemark").value.trim(),
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
    $("fpProfile").value = dirName;
    toast("新环境创建成功：" + dirName + "（已关联下方指纹面板字段）");
    $("fName").value = "";
  });

  // 离线导入结果回填：static/dynamic 解码 JSON -> 表单；cookies 清洗后进 Cookie 框
  // 底层字段 -> 自定义选项映射（与 collectFp 互逆）：
  //   tzAuto/locationSwitch/scanPortType/do_not_track/hardware_concurrency/device_memory/
  //   screen_resolution/canvas/webgl_image/audio/client_rects/media_devices/speech_switch/
  //   webgl+webgl_config/mac_address_config/device_name_switch/language+language_switch 等
  function applyImportResult(out) {
    const notes = out.notes || [];
    let hitCount = 1;
    if (out.static) {
      try {
        const j = JSON.parse(out.static);
        const pick = (k) => (j[k] !== undefined ? j[k] : "");
        if (pick("Langs")) notes.push("语言=" + pick("Langs"));
        if (pick("Platform")) notes.push("Platform=" + pick("Platform"));
        if (pick("HardwareConcurrency")) { const s = $("fpCpu"); if (s) s.value = String(pick("HardwareConcurrency")); }
        if (pick("DeviceMemory")) { const s = $("fpRam"); if (s) s.value = String(pick("DeviceMemory")); }
        if (pick("DeviceName")) { $("fpDevName").value = pick("DeviceName"); }
        if (pick("MacAddress")) { $("fpMac").value = pick("MacAddress"); }
        const pc = pick("ProxyChain") || j.ProxyChain;
        if (pc && typeof pc === "object") {
          if (pc.scheme) $("fpProxyType").value = pc.scheme;
          if (pc.host) $("fpProxyHost").value = pc.host;
          if (pc.port) $("fpProxyPort").value = String(pc.port);
        }
        hitCount += 2;
      } catch (e) { notes.push("static JSON 解析失败"); }
    }
    if (out.dynamic) {
      try {
        const j = JSON.parse(out.dynamic);
        if (j.Geoposition) {
          const g = String(j.Geoposition).split(",");
          if (g.length >= 2) { $("fpLat").value = g[0]; $("fpLng").value = g[1]; }
          if (g[2]) $("fpAccuracy").value = g[2];
          notes.push("经纬度已回填");
        }
        hitCount++;
      } catch (e) { notes.push("dynamic JSON 解析失败"); }
    }
    if (out.cookies) {
      const clean = window.FP.sanitizeCookies(out.cookies, out.fbcc);
      $("fpCookie").value = clean.text;
      notes.push(...clean.notes);
      hitCount++;
    }
    if (out.ua) { $("fpUA").value = out.ua; syncUaPresetFromUA(); notes.push("UA已回填"); hitCount++; }
    // 注册到环境列表（目录即环境）
    if (out.dirName && !db().profiles.some((p) => pname(p) === out.dirName)) {
      db().profiles.push({ name: out.dirName, group: "默认", proxy: "-", kernel: "Chrome 152 (SunBrowser)", status: "closed", remark: "", browser: "sun", browserDir: out.dirName, ua: $("fpUA").value, cookie: $("fpCookie").value });
      renderProfiles(); syncFpProfileSelect();
    }
    $("fpProfile").value = out.dirName || $("fpProfile").value;
    $("fpBrowserDir").value = out.dirName || $("fpBrowserDir").value;
    $("fpImportStatus").textContent = `已从 ${out.dirName} 提取 ${hitCount} 项指纹线索：${notes.join("；")}`;
    toast("目录导入完成（CLIENT_HOST 已剥离，绝不上传）");
  }

  // fingerprint_config（下划线命名，导入文件/API 形态）-> 自定义选项 + 表单
  // 对齐 main.min.js：webrtc/proxy|disabled；tzAuto/timezone；location/location_switch；
  // language/language_switch；screen_resolution/hardware_concurrency/device_memory/do_not_track；
  // canvas/webgl_image/audio/client_rects/media_devices/speech_switch；webgl/webgl_config；
  // mac_address_config/device_name_switch/scan_port_type/allow_scan_ports
  function applyFpConfig(cfg) {
    if (!cfg || typeof cfg !== "object") return [];
    const notes = [];
    const pickCap = (group, val) => {
      const g = document.querySelector(`[data-fp-group="${group}"]`);
      if (!g || val === undefined) return;
      g.querySelectorAll(".capsule-btn").forEach((b) => b.classList.toggle("active", b.dataset.val === val));
    };
    const get = (...ks) => { for (const k of ks) if (cfg[k] !== undefined) return cfg[k]; return undefined; };
    let v;
    v = get("webrtc"); if (v !== undefined) { pickCap("webrtc", v); notes.push("WebRTC=" + v); }
    v = get("automatic_timezone", "tzAuto", "tz_auto"); if (v !== undefined) { pickCap("timezoneMode", String(v) === "1" ? "ip" : "custom"); }
    v = get("timezone"); if (v !== undefined && $("fpTimezone")) { $("fpTimezone").value = String(v).replace(/_/g, " "); notes.push("时区=" + v); }
    v = get("location"); if (v !== undefined) { pickCap("geoMode", v); notes.push("地理位置=" + v); }
    v = get("location_switch", "locationSwitch"); if (v !== undefined) { pickCap("geoIp", String(v) === "1" ? "ip" : "custom"); }
    v = get("latitude"); if (v !== undefined) $("fpLat").value = v;
    v = get("longitude"); if (v !== undefined) $("fpLng").value = v;
    v = get("accuracy"); if (v !== undefined) $("fpAccuracy").value = v;
    v = get("language"); if (v !== undefined) { const arr = Array.isArray(v) ? v : String(v).split(","); if ($("fpLangList")) $("fpLangList").value = arr.join(","); pickCap("langMode", "custom"); notes.push("语言=" + arr.join(",")); }
    v = get("language_switch", "languageSwitch"); if (v !== undefined) notes.push("languageSwitch=" + v);
    v = get("screen_resolution", "screenResolution"); if (v !== undefined && v !== "none" && $("fpResolution")) { $("fpResolution").value = v; notes.push("分辨率=" + v); }
    v = get("hardware_concurrency", "hardwareConcurrency"); if (v !== undefined && $("fpCpu")) { pickCap("cpuMode", String(v) === "default" ? "real" : "custom"); if (String(v) !== "default") $("fpCpu").value = String(v); }
    v = get("device_memory", "deviceMemory"); if (v !== undefined && $("fpRam")) { pickCap("ramMode", String(v) === "default" ? "real" : "custom"); if (String(v) !== "default") $("fpRam").value = String(v); }
    v = get("do_not_track", "doNotTrack"); if (v !== undefined) { pickCap("doNotTrack", String(v) === "true" ? "open" : String(v) === "false" ? "close" : "default"); }
    v = get("canvas"); if (v !== undefined && $("swCanvas")) $("swCanvas").checked = String(v) === "1";
    v = get("webgl_image", "webglImage"); if (v !== undefined && $("swWebglImg")) $("swWebglImg").checked = String(v) === "1";
    v = get("audio"); if (v !== undefined && $("swAudio")) $("swAudio").checked = String(v) === "1";
    v = get("client_rects", "clientRects"); if (v !== undefined && $("swClientRects")) $("swClientRects").checked = String(v) === "1";
    v = get("speech_switch", "speechSwitch"); if (v !== undefined && $("swSpeech")) $("swSpeech").checked = String(v) === "1";
    v = get("media_devices", "mediaDevices"); if (v !== undefined && $("fpMediaDevices")) $("fpMediaDevices").value = String(v);
    v = get("media_devices_num", "mediaDevicesNum"); if (v && typeof v === "object") { if ($("fpMediaIn") && v.audioinput_num !== undefined) $("fpMediaIn").value = v.audioinput_num; if ($("fpMediaVid") && v.videoinput_num !== undefined) $("fpMediaVid").value = v.videoinput_num; if ($("fpMediaOut") && v.audiooutput_num !== undefined) $("fpMediaOut").value = v.audiooutput_num; }
    v = get("webgl"); if (v !== undefined) { pickCap("webglMeta", String(v) === "2" || String(v) === "3" ? "custom" : "real"); }
    v = get("webgl_config", "webglConfig"); if (v && typeof v === "object") { if (v.unmasked_vendor && $("fpVendor")) $("fpVendor").value = v.unmasked_vendor; if (v.unmasked_renderer && $("fpRenderer")) $("fpRenderer").value = v.unmasked_renderer; if (v.webgpu && v.webgpu.webgpu_switch !== undefined) { const sw = String(v.webgpu.webgpu_switch); if (sw === "0") pickCap("webgpu", "disabled"); else if (v.webgpu.gpu_adapterinfo_vendor || v.webgpu.gpu_adapterinfo_architecture) { pickCap("webgpu", "custom"); if ($("fpGpuVendor") && v.webgpu.gpu_adapterinfo_vendor) $("fpGpuVendor").value = v.webgpu.gpu_adapterinfo_vendor; if ($("fpGpuArch") && v.webgpu.gpu_adapterinfo_architecture) $("fpGpuArch").value = v.webgpu.gpu_adapterinfo_architecture; } else pickCap("webgpu", "follow_webgl"); } }
    v = get("mac_address_config", "macAddressConfig"); if (v && typeof v === "object") { pickCap("macMode", String(v.model) === "2" ? "custom" : "off"); if (v.address && $("fpMac")) $("fpMac").value = v.address; }
    v = get("device_name_switch", "deviceNameSwitch"); if (v !== undefined) { pickCap("devNameMode", String(v) === "2" ? "custom" : String(v) === "1" ? "random" : "off"); }
    v = get("device_name", "deviceName"); if (v !== undefined && $("fpDevName")) $("fpDevName").value = v;
    v = get("scan_port_type", "scanPortType"); if (v !== undefined) { pickCap("portScan", String(v) === "1" ? "open" : String(v) === "0" ? "close" : "default"); }
    v = get("allow_scan_ports", "allowScanPorts"); if (v !== undefined && $("fpWhitePorts")) $("fpWhitePorts").value = Array.isArray(v) ? v.join(",") : v;
    v = get("fonts"); if (v !== undefined && $("fpFontsList")) { if (Array.isArray(v) && v.length === 1 && v[0] === "all") { pickCap("fontMode", "all"); } else { pickCap("fontMode", "custom"); $("fpFontsList").textContent = Array.isArray(v) ? v.join(", ") : v; } }
    v = get("ua", "userAgent"); if (v !== undefined && $("fpUA")) { $("fpUA").value = v; syncUaPresetFromUA(); }
    v = get("gpu"); if (v !== undefined) { const gv = String(v); pickCap("hardwareAccel", gv === "2" ? "close" : gv === "0" ? "open" : "default"); }
    return notes;
  }
  function syncFpProfileSelect() {
    const opts = db().profiles.map((p) => `<option value="${pname(p)}">${pname(p)}${p.name && p.sn ? " - " + p.name : ""}</option>`).join("");
    if ($("fpProfile")) $("fpProfile").innerHTML = opts;
  }

  function loadProfileToFp(nm) {
    const p = db().profiles.find((x) => pname(x) === nm);
    if (!p) return;
    $("fpProfile").value = nm;
    // 尝试从本地后端拉该环境的 ui_fingerprint.json + static/dynamic 回填（失败则用行内字段）
    API.fpGet("ui", nm).then((r) => {
      if (r && r.json) {
        try { applyUiExtra(JSON.parse(r.json)); toast(`已载入 ${nm} 的 UI 存档`); return; } catch (e) { /* 回退行内 */ }
      }
    }).catch(() => {});
    // 全量存档（内存态）优先回填自定义选项
    if (p.fp) { try { applyUiExtra(p.fp); } catch (e) { /* 忽略 */ } }
    if (p.browser) $("fpBrowser").value = p.browser;
    if (p.browserDir !== undefined) $("fpBrowserDir").value = p.browserDir || "";
    if (p.ua) $("fpUA").value = p.ua;
    if (p.ua) syncUaPresetFromUA();
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
  const curOS = () => ($("fpOsGroup").querySelector(".active") || {}).dataset?.os || "win";
  // UA大版本号输入框：取纯数字，非法/为空时回退 152
  const uaVer = () => {
    const v = ($("fpUaPreset") && $("fpUaPreset").value || "").trim().match(/\d{2,3}/);
    return v ? v[0] : "152";
  };
  const buildUA = (os, ver) => {
    switch (os) {
      case "mac": return `Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${ver}.0.0.0 Safari/537.36`;
      case "linux": return `Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${ver}.0.0.0 Safari/537.36`;
      case "android": return `Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${ver}.0.0.0 Mobile Safari/537.36`;
      case "ios": return "Mozilla/5.0 (iPhone; CPU iPhone OS 17_4 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Mobile/15E148 Safari/604.1";
      default: return `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${ver}.0.0.0 Safari/537.36`;
    }
  };
  // 从 UA 文本反解析版本号回填输入框（导入/载入存档时调用）
  const syncUaPresetFromUA = () => {
    if (!$("fpUaPreset") || !$("fpUA")) return;
    const m = ($("fpUA").value || "").match(/(?:Chrome|CriOS|Firefox)\/(\d{2,3})/);
    if (m) $("fpUaPreset").value = m[1];
  };
  const setFpUA = () => { $("fpUA").value = buildUA(curOS(), uaVer()); };
  $("btnShuffleFpUA").addEventListener("click", () => { setFpUA(); toast(`已按当前系统生成 Chrome ${uaVer()} 的 User-Agent`); });
  if ($("fpUA")) $("fpUA").addEventListener("input", syncUaPresetFromUA);
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

  /* ================= 初始化：本地 profile 列表（目录即环境） ================= */
  async function initProfiles() {
    try {
      const list = await API.listProfiles();
      if (Array.isArray(list) && list.length) {
        db().profiles = list.map((x) => ({
          name: x.name, status: x.running ? "open" : "closed",
          pid: x.pid, port: x.port, group: "默认",
          kernel: "Chrome 152 (SunBrowser)", proxy: "-", remark: "",
          browser: "sun", browserDir: "", ua: "", cookie: "",
        }));
      }
    } catch (e) { /* 未连接本地启动器时保留 mock/空列表，目录导入仍可用 */ }
    renderProfiles();
    syncFpProfileSelect();
    if (db().profiles.length) loadProfileToFp(pname(db().profiles[0]));
  }

  /* ---- 从环境目录导入指纹（离线：优先 File System Access 直读三件套） ---- */
  const fpPicker = $("fpDirPicker");
  $("btnFpImport").addEventListener("click", async () => {
    // 优先用 File System Access（可拿目录句柄，后续导出直接写回）
    if (window.showDirectoryPicker && window.FP) {
      try {
        const h = await window.FP.pickDir();
        dirHandle = h; dirName = h.name;
        $("fpBrowserDir").value = h.name;
        $("fpImportStatus").textContent = `正在读取目录 ${h.name} ...`;
        const out = await window.FP.importFromDirHandle(h, h.name);
        applyImportResult(out);
        return;
      } catch (e) {
        if (e && e.name === "AbortError") return;
        // 不支持/被拒则回退 webkitdirectory
      }
    }
    if (fpPicker) fpPicker.click();
  });
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
            if (ua) { $("fpUA").value = ua; syncUaPresetFromUA(); notes.push("UA已回填"); hitCount++; }
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

  /* ---- 保存指纹：写回（static/dynamic/cookies + ui 侧车） ---- */
  // 把 ui_fingerprint.json 存档回填表单（含自定义选项映射字段）
  function applyUiExtra(fp) {
    if (!fp || typeof fp !== "object") return;
    const set = (id, v) => { if (v !== undefined && $(id)) $(id).value = v; };
    const pickCap = (group, val) => {
      const g = document.querySelector(`[data-fp-group="${group}"]`);
      if (!g || val === undefined) return;
      g.querySelectorAll(".capsule-btn").forEach((b) => b.classList.toggle("active", b.dataset.val === val));
    };
    set("fpUaPreset", fp.uaPreset);
    set("fpProxyHost", fp.proxyHost); set("fpProxyPort", fp.proxyPort);
    set("fpLat", fp.lat); set("fpLng", fp.lng); set("fpAccuracy", fp.accuracy);
    set("fpDevName", fp.devName); set("fpMac", fp.mac);
    set("fpCookie", fp.cookie); set("fpRemark", fp.remark);
    set("fpUA", fp.ua); set("fpTimezone", fp.timezone);
    set("fpResolution", fp.resolution); set("fpRenderer", fp.renderer);
    set("fpVendor", fp.vendor); set("fpCpu", fp.cpu); set("fpRam", fp.ram);
    set("fpWhitePorts", fp.whitePorts); set("fpLaunchArgs", fp.launchArgs);
    set("fpBrowserDir", fp.browserDir);
    set("fpPageLang", fp.pageLanguage);
    set("fpResW", fp.resW); set("fpResH", fp.resH);
    set("fpTlsBlacklist", fp.tls);
    set("fpGpuVendor", fp.gpuVendor); set("fpGpuArch", fp.gpuArch);
    set("fpMediaIn", fp.mediaDevicesNum && fp.mediaDevicesNum.audioinput_num);
    set("fpMediaVid", fp.mediaDevicesNum && fp.mediaDevicesNum.videoinput_num);
    set("fpMediaOut", fp.mediaDevicesNum && fp.mediaDevicesNum.audiooutput_num);
    if (fp.language) set("fpLangList", Array.isArray(fp.language) ? fp.language.join(",") : fp.language);
    if (fp.mediaDevices) set("fpMediaDevices", fp.mediaDevices);
    pickCap("webrtc", fp.webrtc);
    pickCap("timezoneMode", fp.timezoneMode);
    pickCap("geoMode", fp.geoMode || fp.location);
    pickCap("geoIp", fp.geoIp);
    pickCap("langMode", fp.langMode);
    pickCap("uiLang", fp.uiLang);
    pickCap("resMode", fp.resMode);
    pickCap("fontMode", fp.fontMode);
    pickCap("webglMeta", fp.webglMeta);
    pickCap("webgpu", fp.webgpu);
    pickCap("cpuMode", fp.cpuMode);
    pickCap("ramMode", fp.ramMode);
    pickCap("devNameMode", fp.devNameMode);
    pickCap("macMode", fp.macMode);
    pickCap("doNotTrack", fp.doNotTrack);
    pickCap("portScan", fp.portScan);
    pickCap("hardwareAccel", fp.hardwareAccel);
    pickCap("disableTls", fp.disableTls);
    if (fp.hwNoise) {
      if ($("swCanvas")) $("swCanvas").checked = !!fp.hwNoise.canvas;
      if ($("swWebglImg")) $("swWebglImg").checked = !!fp.hwNoise.webglImg;
      if ($("swAudio")) $("swAudio").checked = !!fp.hwNoise.audio;
      if ($("swClientRects")) $("swClientRects").checked = !!fp.hwNoise.clientRects;
      if ($("swSpeech")) $("swSpeech").checked = !!fp.hwNoise.speech;
    }
    if (fp.canvas !== undefined && $("swCanvas")) $("swCanvas").checked = fp.canvas === "1";
    if (fp.webglImage !== undefined && $("swWebglImg")) $("swWebglImg").checked = fp.webglImage === "1";
    if (fp.audio !== undefined && $("swAudio")) $("swAudio").checked = fp.audio === "1";
    if (fp.clientRects !== undefined && $("swClientRects")) $("swClientRects").checked = fp.clientRects === "1";
    if (fp.speechSwitch !== undefined && $("swSpeech")) $("swSpeech").checked = fp.speechSwitch === "1";
    if (fp.fonts && typeof fp.fonts === "string" && $("fpFontsList")) $("fpFontsList").textContent = fp.fonts;
    if (fp.remark !== undefined) { $("fpRemarkCount").textContent = ($("fpRemark").value || "").length; }
  }
  function collectFp() {
    const cap = (g) => { const el = document.querySelector(`[data-fp-group="${g}"] .capsule-btn.active`); return el ? el.dataset.val : ""; };
    // —— 自定义选项 -> 官方底层字段映射（对齐 main.min.js 各 set* 语义） ——
    const webrtc = cap("webrtc");                       // forward / proxy / disabled / disable_udp（直传）
    const tzMode = cap("timezoneMode");                 // ip -> tzAuto:1；custom -> tzAuto:0 + timezone
    const geoMode = cap("geoMode");                     // ask / allow / block（直传 location）
    const geoIp = cap("geoIp");                         // ip -> locationSwitch:1；custom -> 0 + 经纬度
    const langMode = cap("langMode");
    const langList = (($("fpLangList") && $("fpLangList").value) || "en-US,en").split(/[,;\n]+/).map((s) => s.trim()).filter(Boolean);
    const uiLang = cap("uiLang");                       // follow_lang -> pageLanguageSwitch:1；custom -> 0 + pageLanguage
    const resMode = cap("resMode");
    let resolution = $("fpResolution").value;           // none / 宽_高（直传 screenResolution）
    if (resMode === "custom" && $("fpResW").value.trim() && $("fpResH").value.trim()) {
      resolution = $("fpResW").value.trim() + "_" + $("fpResH").value.trim();
    }
    const fontMode = cap("fontMode");                   // all -> fonts:["all"]；custom -> fonts:掩码列表 + disabledFonts
    const webglMeta = cap("webglMeta");                 // real -> webgl:0；custom -> webgl:2 + webgl_config
    const webgpu = cap("webgpu");                       // follow_webgl -> 1；disabled -> 0；custom -> 1+适配器
    const gpuVendor = ($("fpGpuVendor") && $("fpGpuVendor").value || "").trim();
    const gpuArch = ($("fpGpuArch") && $("fpGpuArch").value || "").trim();
    const cpuMode = cap("cpuMode");                     // real -> default；custom -> 下拉值
    const ramMode = cap("ramMode");
    // RAM：官方白名单 [2,4,6,8]（非法官方回退8）；本面板扩展 16/32/64/128，直传 DeviceMemory
    const ramVal = $("fpRam").value;
    const devMode = cap("devNameMode");                 // off -> 0；random -> 1；custom -> 2 + deviceName
    const macMode = cap("macMode");                     // off -> 0；custom -> 2 + address
    const dnt = cap("doNotTrack");                      // open -> true；close -> false；default 不传
    const portScan = cap("portScan");                   // default 不写；open -> 1；close -> 0
    const hwAccel = cap("hardwareAccel");               // open -> gpu:0+gpuSwitch:1；close -> gpu:2；default 不写
    const tlsMode = cap("disableTls");                  // open -> tlsSwitch:1 + tls 黑名单
    // 媒体设备三数量钳制（对齐官方：<=0按1，>=9按8/9）
    const clampMedia = (id) => {
      let n = parseInt(($(id) && $(id).value) || "1", 10);
      if (isNaN(n) || n <= 0) return 1;
      if (n >= 9) return 8;
      return n;
    };
    return {
      browser: $("fpBrowser").value,
      kernelVer: $("fpKernelVer").value,
      browserDir: $("fpBrowserDir").value.trim(),
      os: curOS(),
      uaPreset: ($("fpUaPreset") && $("fpUaPreset").value || "").trim(),
      ua: $("fpUA").value.trim(),
      proxyType: $("fpProxyType").value,
      proxyHost: $("fpProxyHost").value.trim(),
      proxyPort: $("fpProxyPort").value.trim(),
      proxyUser: $("fpProxyUser").value.trim(),
      cookie: $("fpCookie").value.trim(),
      remark: $("fpRemark").value.trim(),
      webrtc,
      timezoneMode: tzMode,
      tzAuto: tzMode === "ip" ? "1" : "0",
      timezone: $("fpTimezone").value,
      geoMode,
      location: geoMode,
      geoIp,
      locationSwitch: geoIp === "ip" ? "1" : "0",
      lat: $("fpLat").value, lng: $("fpLng").value, accuracy: $("fpAccuracy").value,
      langMode,
      language: langList,
      languageSwitch: langList.length <= 1 ? "1" : "0",
      uiLang,
      pageLanguageSwitch: uiLang === "custom" ? "0" : "1",
      pageLanguage: ($("fpPageLang") && $("fpPageLang").value || "").trim(),
      resMode,
      resolution,
      screenResolution: resolution,
      fontMode,
      fonts: fontMode === "all" ? ["all"] : $("fpFontsList").textContent,
      hwNoise: {
        canvas: $("swCanvas").checked, webglImg: $("swWebglImg").checked,
        audio: $("swAudio").checked,
        clientRects: $("swClientRects").checked, speech: $("swSpeech").checked,
      },
      canvas: $("swCanvas").checked ? "1" : "0",
      webglImage: $("swWebglImg").checked ? "1" : "0",
      audio: $("swAudio").checked ? "1" : "0",
      clientRects: $("swClientRects").checked ? "1" : "0",
      speechSwitch: $("swSpeech").checked ? "1" : "0",
      mediaDevices: $("fpMediaDevices").value,
      mediaDevicesNum: {
        audioinput_num: clampMedia("fpMediaIn"),
        videoinput_num: clampMedia("fpMediaVid"),
        audiooutput_num: clampMedia("fpMediaOut"),
      },
      webglMeta,
      webgl: webglMeta === "custom" ? "2" : "0",
      vendor: $("fpVendor").value,
      renderer: $("fpRenderer").value,
      webglConfig: webglMeta === "custom"
        ? { unmasked_vendor: $("fpVendor").value, unmasked_renderer: $("fpRenderer").value, webgpu: Object.assign({ webgpu_switch: webgpu === "disabled" ? "0" : "1" }, webgpu === "custom" ? { gpu_adapterinfo_vendor: gpuVendor, gpu_adapterinfo_architecture: gpuArch } : {}) }
        : "",
      webgpu,
      webgpuSwitch: webgpu === "disabled" ? "0" : "1",
      gpuVendor, gpuArch,
      cpuMode,
      hardwareConcurrency: cpuMode === "real" ? "default" : $("fpCpu").value,
      cpu: cpuMode === "real" ? "default" : $("fpCpu").value,
      ramMode,
      deviceMemory: ramMode === "real" ? "default" : ramVal,
      ram: ramMode === "real" ? "default" : ramVal,
      devNameMode: devMode,
      deviceNameSwitch: devMode === "off" ? "0" : devMode === "random" ? "1" : "2",
      devName: $("fpDevName").value, mac: $("fpMac").value,
      macMode,
      macAddressConfig: macMode === "custom"
        ? { model: "2", address: $("fpMac").value.trim() }
        : { model: "0", address: "" },
      doNotTrack: dnt,
      do_not_track: dnt === "open" ? "true" : dnt === "close" ? "false" : "",
      portScan, whitePorts: $("fpWhitePorts").value,
      scanPortType: portScan === "open" ? "1" : portScan === "close" ? "0" : "",
      allowScanPorts: $("fpWhitePorts").value.trim(),
      hardwareAccel: hwAccel,
      gpu: hwAccel === "close" ? "2" : hwAccel === "open" ? "0" : "",
      gpuSwitch: hwAccel === "open" ? "1" : hwAccel === "close" ? "" : "",
      disableTls: tlsMode,
      tlsSwitch: tlsMode === "open" ? "1" : "0",
      tls: ($("fpTlsBlacklist") && $("fpTlsBlacklist").value || "").trim(),
      launchArgs: $("fpLaunchArgs").value,
    };
  }
  $("btnFpSave").addEventListener("click", async () => {
    const nm = $("fpProfile").value;
    const fp = collectFp();
    const p = db().profiles.find((x) => pname(x) === nm);
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
    toast(`指纹配置已保存${nm ? "到 " + nm : ""}（35+ 参数全量存档）`);
    // 离线写回：static/dynamic/cookies + ui 侧车
    // 保存时同时组装底层 fingerprint_config（下划线命名，直传官方字段）+ ui 存档（自定义选项）
    const fpConfig = {
      webrtc: fp.webrtc,
      automatic_timezone: fp.tzAuto,
      timezone: fp.tzAuto === "0" ? fp.timezone.replace(/\s+/g, "_") : "",
      location: fp.location,
      location_switch: fp.locationSwitch,
      latitude: fp.locationSwitch === "0" ? fp.lat : "",
      longitude: fp.locationSwitch === "0" ? fp.lng : "",
      accuracy: fp.locationSwitch === "0" ? fp.accuracy : "",
      language: fp.language,
      language_switch: fp.languageSwitch,
      screen_resolution: fp.screenResolution,
      hardware_concurrency: fp.hardwareConcurrency,
      device_memory: fp.deviceMemory,
      do_not_track: fp.do_not_track,
      canvas: fp.canvas,
      webgl_image: fp.webglImage,
      audio: fp.audio,
      client_rects: fp.clientRects,
      media_devices: fp.mediaDevices,
      media_devices_num: fp.mediaDevices === "2" ? fp.mediaDevicesNum : undefined,
      speech_switch: fp.speechSwitch,
      webgl: fp.webgl,
      webgl_config: fp.webglConfig || undefined,
      gpu: fp.gpu || undefined,
      gpuSwitch: fp.gpuSwitch || undefined,
      mac_address_config: fp.macAddressConfig,
      device_name: fp.devName,
      device_name_switch: fp.deviceNameSwitch,
      scan_port_type: fp.scanPortType,
      allow_scan_ports: fp.allowScanPorts,
      fonts: fp.fontMode === "all" ? ["all"] : String(fp.fonts).split(/[,，\n]+/).map((s) => s.trim()).filter(Boolean),
      // asar 1:1（main.min.js setScreenResolution 尾部 + setFakeFonts，见 launcher/cpp/fp_ui.cpp）：
      // fonts=all -> static.DisabledFonts = getFonts(u[] 181 条) - mobileFonts(c[] 12 条)；
      // custom -> static.DisabledFonts = 上行切分数组（setFonts 直写 disabledFonts）。
      // Fakefonts 键值映射见 C++ FpBuildFakefontsJson（伪装 platform 查表取键、本机轮转取值）。
      // 预览（与 C++ 同算法，浏览器端可直接验算，不写盘）：
      fontsDisabledPreview: fp.fontMode === "all"
        ? window.FP.asarDisabledFonts()
        : String(fp.fonts).split(/[,，\n]+/).map((s) => s.trim()).filter(Boolean),
      fontsFakePreviewKeys: window.FP.asarFakeKeys(fp.os || "win"),
      ua: fp.ua,
    };
    try {
      const fbcc = (window.FP ? window.FP.fbccOf(nm) : nm.split("_")[0]);
      const cleanCookie = window.FP ? window.FP.sanitizeCookies(fp.cookie || "[]", fbcc) : { text: fp.cookie };
      const payload = { ui: JSON.stringify(fp), fingerprint_config: JSON.stringify(fpConfig) };
      // 代理/设备等只进 ui 存档；Cookie 清洗后可写 cookies 文件
      try { JSON.parse(cleanCookie.text); payload.cookies = cleanCookie.text; } catch (e) { /* 非 JSON 则不写 */ }
      if (dirHandle && dirName === nm) {
        const notes = await window.FP.exportToDirHandle(dirHandle, nm, payload);
        toast("已写回目录：" + notes.join("；"));
      } else {
        const r = await API.fpSave(nm, payload);
        toast(`已保存到本地${r && r.mock ? "（演示，未写盘）" : ""}：${(r && r.notes || []).join("；") || "ui 存档已更新"}`);
      }
      if (cleanCookie.notes && cleanCookie.notes.length) toast(cleanCookie.notes.join("；"));
    } catch (e) { toast("本地写回失败: " + e.message); }
  });

  /* ---- 导出/导入整目录（zip 占位：离线用目录句柄直接读写，无需打包） ---- */
  async function exportProfileDir() {
    const nm = $("fpProfile").value;
    if (!nm) return toast("请先选择环境");
    try {
      if (!dirHandle || dirName !== nm) {
        if (window.showDirectoryPicker && window.FP) {
          dirHandle = await window.FP.pickDir();
          dirName = dirHandle.name;
        } else return toast("当前浏览器不支持目录写回，请用 Chrome/Edge");
      }
      const fp = collectFp();
      const fbcc = window.FP.fbccOf(nm);
      const cleanCookie = window.FP.sanitizeCookies(fp.cookie || "[]", fbcc);
      const notes = await window.FP.exportToDirHandle(dirHandle, nm, {
        uiExtra: JSON.stringify(fp, null, 2),
        cookies: (() => { try { JSON.parse(cleanCookie.text); return cleanCookie.text; } catch (e) { return ""; } })(),
      });
      toast("导出/保存完成：" + notes.join("；"));
    } catch (e) { toast("导出失败: " + e.message); }
  }

  /* ================= 初始化 ================= */
  initProfiles();
})();
