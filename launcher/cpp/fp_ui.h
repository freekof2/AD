// fp_ui.h — 指纹配置原生窗口（web-ui/index.html 单页版 1:1 纯原生重写）
// 对应 web-ui 75 个控件 id：环境表(11) + 浏览器/系统/代理/Cookie/备注(13)
// + WebRTC/时区/地理/语言/界面语言/分辨率(6组) + 字体/硬件噪音/WebGL/WebGPU(4组)
// + CPU/RAM/设备名/MAC/DNT/端口/加速/TLS(8组) + 启动参数。
// 布局：单页滚动 860x640（对齐网页 fp-row 顺序，无 Tab，无云端），无第三方依赖。
// 配色：浅灰蓝底 kUiBg(#f5f7fa)+白输入框（对齐 app.css --bg/--panel/--border/--primary）。
#pragma once
#include "SunLauncher.h"
#include <commctrl.h>

// ---------- 指纹表单数据（与 web-ui collectFp() 字段一一对应） ----------
struct FpFormData {
    // A. 浏览器/内核/目录
    std::wstring browser;      // sun | flower
    std::wstring kernelVer;    // chrome143 | chrome121 | firefox128
    std::wstring browserDir;   // 缓存目录名（= 环境名）
    // B. 系统/UA
    std::wstring os;           // win | mac | linux | android | ios
    std::wstring uaPreset;     // UA 大版本号，如 152
    std::wstring ua;
    // C. 代理
    std::wstring proxyType;    // socks5 | http | https
    std::wstring proxyHost;
    std::wstring proxyPort;
    std::wstring proxyUser;
    std::wstring proxyPass;
    // D. Cookie/备注
    std::wstring cookie;
    std::wstring remark;
    // 1. WebRTC: forward | proxy | disabled | disable_udp
    std::wstring webrtc;
    // 2. 时区
    std::wstring timezoneMode; // ip | custom
    std::wstring timezone;     // 如 Asia/Shanghai
    // 3. 地理
    std::wstring geoMode;      // ask | allow | block
    std::wstring geoIp;        // ip | custom
    std::wstring lat, lng, accuracy;
    // 4. 语言
    std::wstring langMode;     // ip | custom
    std::wstring langList;     // 如 en-US,en
    // 5. 界面语言
    std::wstring uiLang;       // follow_lang | custom
    std::wstring pageLang;
    // 6. 分辨率
    std::wstring resMode;      // preset | custom
    std::wstring resolution;   // none | 宽_高
    std::wstring resW, resH;
    // 7. 字体
    std::wstring fontMode;     // all | custom
    std::wstring fonts;
    // 8. 硬件噪音开关
    bool swCanvas = false, swWebglImg = false, swAudio = true;
    bool swClientRects = true, swSpeech = true;
    std::wstring mediaDevices; // 0 | 1 | 2
    std::wstring mediaIn, mediaVid, mediaOut;
    // 9. WebGL 元数据
    std::wstring webglMeta;    // real | custom
    std::wstring vendor, renderer;
    // 10. WebGPU
    std::wstring webgpu;       // follow_webgl | disabled | custom
    std::wstring gpuVendor, gpuArch;
    // 11. CPU / 12. RAM
    std::wstring cpuMode;      // real | custom
    std::wstring cpu;          // default | 2..24
    std::wstring ramMode;      // real | custom
    std::wstring ram;          // default | 2..128
    // 13. 设备名 / 14. MAC
    std::wstring devNameMode;  // off | random | custom
    std::wstring devName;
    std::wstring macMode;      // off | custom
    std::wstring mac;
    // 15. DNT: default | open | close
    std::wstring doNotTrack;
    // 16. 端口扫描：default | open | close + 白名单
    std::wstring portScan;
    std::wstring whitePorts;
    // 17. 硬件加速：default | open | close
    std::wstring hardwareAccel;
    // 18. TLS
    std::wstring disableTls;   // close | open
    std::wstring tlsBlacklist;
    // 19. 启动参数
    std::wstring launchArgs;
};

// 打开指纹配置模态窗口。profileName 为目录名（如 k1h60tsv_hyg6dd）。
// 返回 true=用户点了保存（已写 ui_fingerprint.json + 三件套），false=取消。
bool FpUiShowModal(HWND hParent, const Config& cfg, const std::wstring& profileName);

// 表单 <-> ui_fingerprint.json 存档互转（字段名与 web-ui collectFp/applyUiExtra 一致）。
std::string FpFormToUiJson(const FpFormData& f);
bool FpFormFromUiJson(const std::string& json, FpFormData& f);
// 表单 -> fingerprint_config（下划线命名，直传官方字段，与 web-ui btnFpSave 一致）。
std::string FpFormToFpConfig(const FpFormData& f);
// ---- asar 1:1 字体语义（main.min.js setScreenResolution 尾部 + setFakeFonts 全文移植）----
// os 胶囊值 -> asar e.platform（win->Win32，mac->MacIntel，linux->Linux x86_64，android->Linux armv8I，ios->iPhone）
std::wstring FpOsToAsarPlatform(const std::wstring& os);
// fonts=all -> DisabledFonts JSON 数组（getFonts(u[] 181 条) - mobileFonts(12 条)，顺序保留含重复）
std::string FpBuildDisabledFontsJson();
// Fakefonts JSON 对象（键=伪装表全键，值=本机 win32 表轮转；云端表缺失时 win32/darwin/linux 用 u[] 全集代替）
std::string FpBuildFakefontsJson(const std::wstring& asarPlatform);
