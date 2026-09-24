/* 离线指纹编解码 + 缓存目录导入导出。
 * 对应 C++ fingerprint.h/cpp 的 JS 移植：换表Base64、md5(fbcc+"_static/_webrtc/_cookies")
 * 文件名约定、ui_fingerprint.json 侧车。零网络：File System Access API 直读直写本地目录。
 */
(function () {
  "use strict";
  const C1 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const C2 = "hTy1bfRJz4nLPcBCO7WtmNIaGvVeul5Zo8kq32UxrYw_-0gsjp96SDFXQiEMKdHA";

  function mapTable(s, from, to) {
    const idx = {};
    for (let i = 0; i < 64; i++) idx[from[i]] = to[i];
    return [...s].map((ch) => (ch in idx ? idx[ch] : ch)).join("");
  }
  // 标准 base64（unicode 安全）
  function stdEncode(str) {
    const bytes = new TextEncoder().encode(str);
    let bin = "";
    bytes.forEach((b) => { bin += String.fromCharCode(b); });
    return btoa(bin);
  }
  function stdDecode(b64) {
    const bin = atob(b64);
    const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  }
  // AdsPower 换表编解码
  function fpEncode(jsonText) { return mapTable(stdEncode(jsonText), C1, C2); }
  function fpDecode(mapped) { return stdDecode(mapTable(mapped, C2, C1)); }

  // md5（自包含实现，与 C++ FpMd5Hex 一致，小写 hex）
  function md5Hex(ascii) {
    function rl(n, c) { return (n << c) | (n >>> (32 - c)); }
    function add(x, y) { const l = (x & 0xffff) + (y & 0xffff); return ((x >> 16) + (y >> 16) + (l >> 16) << 16) | (l & 0xffff); }
    const S = [7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21];
    const K = [0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x4881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391];
    let msg = unescape(encodeURIComponent(ascii));
    const bitLen = msg.length * 8;
    msg += "\x80";
    while (msg.length % 64 !== 56) msg += "\x00";
    for (let i = 0; i < 8; i++) msg += String.fromCharCode((bitLen >>> (i * 8)) & 0xff);
    let a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
    for (let i = 0; i < msg.length; i += 64) {
      const M = [];
      for (let j = 0; j < 16; j++) M[j] = msg.charCodeAt(i + j * 4) | (msg.charCodeAt(i + j * 4 + 1) << 8) | (msg.charCodeAt(i + j * 4 + 2) << 16) | (msg.charCodeAt(i + j * 4 + 3) << 24);
      let A = a, B = b, C = c, D = d, F, g;
      for (let k = 0; k < 64; k++) {
        if (k < 16) { F = (B & C) | (~B & D); g = k; }
        else if (k < 32) { F = (D & B) | (~D & C); g = (5 * k + 1) % 16; }
        else if (k < 48) { F = B ^ C ^ D; g = (3 * k + 5) % 16; }
        else { F = C ^ (B | ~D); g = (7 * k) % 16; }
        F = add(add(add(F, A), K[k]), M[g]);
        A = D; D = C; C = B;
        B = add(B, rl(F, S[k]));
      }
      a = add(a, A); b = add(b, B); c = add(c, C); d = add(d, D);
    }
    const hex = (n) => { let s = ""; for (let i = 0; i < 4; i++) s += ("0" + ((n >>> (i * 8)) & 0xff).toString(16)).slice(-2); return s; };
    return hex(a) + hex(b) + hex(c) + hex(d);
  }

  const fbccOf = (dirName) => (dirName || "").split("_")[0];
  const staticName = (fbcc) => md5Hex(fbcc + "_static");
  const dynamicName = (fbcc) => md5Hex(fbcc + "_webrtc");
  const cookiesName = (fbcc) => md5Hex(fbcc + "_cookies");

  // 启动注入保护键（与 C++ kProtected 一致）
  const PROTECTED_KEYS = ["UserId","StaticConfig","DynamicConfig","CookiesFile",
    "CanvasMark","WebGLMark","AudioFp","ClientRectFp","TimeZone","Geoposition",
    "WebRTCAddress","DisableWebRTC","ProxyChain","DeviceName","MacAddress",
    "MediaDevices","TTSEngines","Langs","AcceptLang"];

  // ---- File System Access 目录读写（Chromium 系浏览器；不支持时回退 webkitdirectory 导入） ----
  async function pickDir() {
    if (!window.showDirectoryPicker) throw new Error("当前浏览器不支持目录直接读写，请用 Chrome/Edge 打开");
    return window.showDirectoryPicker({ mode: "readwrite" });
  }
  async function readFile(handle, name) {
    const fh = await handle.getFileHandle(name, { create: false });
    const f = await fh.getFile();
    return (await f.text()).trim();
  }
  async function writeFile(handle, name, text) {
    const fh = await handle.getFileHandle(name, { create: true });
    const w = await fh.createWritable();
    await w.write(text);
    await w.close();
  }
  // 从已选目录句柄导入：解码三件套 + Preferences/cookie 回填，返回 {static, dynamic, cookies, uiExtra, notes}
  async function importFromDirHandle(handle, dirName) {
    const fbcc = fbccOf(dirName);
    const out = { fbcc, dirName, static: "", dynamic: "", cookies: "", uiExtra: "", notes: [] };
    const tryRead = async (name) => { try { return await readFile(handle, name); } catch (e) { return ""; } };
    const sRaw = await tryRead(staticName(fbcc));
    if (sRaw) { try { out.static = fpDecode(sRaw); out.notes.push("static 已解码"); } catch (e) { out.notes.push("static 解码失败"); } }
    const dRaw = await tryRead(dynamicName(fbcc));
    if (dRaw) { try { out.dynamic = fpDecode(dRaw); out.notes.push("dynamic 已解码"); } catch (e) { out.notes.push("dynamic 解码失败"); } }
    const cRaw = await tryRead(cookiesName(fbcc));
    if (cRaw) { try { out.cookies = fpDecode(cRaw); out.notes.push("cookies 已解码"); } catch (e) { out.notes.push("cookies 解码失败"); } }
    out.uiExtra = await tryRead("ui_fingerprint.json");
    if (out.uiExtra) out.notes.push("ui_fingerprint.json 已读取");
    // Preferences / Local State 辅助线索
    const pref = await tryRead("Default/Preferences").then((t) => t, async () => tryRead("Preferences"));
    if (pref) {
      try {
        const j = JSON.parse(pref);
        const ua = (j.profile && j.profile.user_agent) || (j.webkit && j.webkit.webprefs && j.webkit.webprefs.user_agent);
        if (ua) { out.ua = ua; out.notes.push("UA已回填"); }
      } catch (e) { out.notes.push("Preferences 解析失败"); }
    }
    return out;
  }
  // 导出：把 static/dynamic/cookies（换表编码后）+ ui_fingerprint.json（明文）写回目录
  async function exportToDirHandle(handle, dirName, payload) {
    const fbcc = fbccOf(dirName);
    const notes = [];
    // 写前 md5 比对，一致跳过（与官方/C++ 一致）
    const md5 = (s) => md5Hex(s);
    async function writeIfChanged(name, newRaw) {
      let old = "";
      try { old = await readFile(handle, name); } catch (e) { old = ""; }
      if (old && md5(old.trim()) === md5(newRaw)) { notes.push(name + " 内容一致，跳过"); return; }
      await writeFile(handle, name, newRaw);
      notes.push(name + " 已写入");
    }
    if (payload.static) await writeIfChanged(staticName(fbcc), fpEncode(payload.static));
    if (payload.dynamic) await writeIfChanged(dynamicName(fbcc), fpEncode(payload.dynamic));
    if (payload.cookies) await writeIfChanged(cookiesName(fbcc), fpEncode(payload.cookies));
    if (payload.uiExtra) await writeFile(handle, "ui_fingerprint.json", payload.uiExtra);
    if (payload.uiExtra) notes.push("ui_fingerprint.json 已写入");
    return notes;
  }
  // Cookie 清洗：剥离 CLIENT_HOST（云端地址），校正 BROWSER_ID=fbcc
  function sanitizeCookies(cookiesText, fbcc) {
    let arr;
    try { arr = JSON.parse(cookiesText); } catch (e) { return { text: cookiesText, notes: ["Cookie 非 JSON 数组，原样保留"] }; }
    if (!Array.isArray(arr)) return { text: cookiesText, notes: ["Cookie 非数组，原样保留"] };
    const notes = [];
    const out = [];
    for (const c of arr) {
      if (!c || !c.name) continue;
      if (c.name === "CLIENT_HOST") { notes.push("已剥离 CLIENT_HOST（云端地址，不上传不保存）"); continue; }
      if (c.name === "BROWSER_ID" && c.value !== fbcc) { notes.push(`BROWSER_ID 已校正为 ${fbcc}`); out.push(Object.assign({}, c, { value: fbcc })); continue; }
      out.push(c);
    }
    return { text: JSON.stringify(out, null, 2), notes };
  }

  window.FP = {
    C1, C2, PROTECTED_KEYS,
    fpEncode, fpDecode, md5Hex, fbccOf, staticName, dynamicName, cookiesName,
    pickDir, readFile, writeFile, importFromDirHandle, exportToDirHandle, sanitizeCookies,
  };
})();
