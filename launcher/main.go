// SunLauncher - 类 AdsPower 的 SunBrowser.exe 启动器。
//
// 功能：
//   - 数据目录可自定义（默认 F:\.ADSPOWER_GLOBAL\cache），每个子目录 = 一个 profile；
//   - Web 界面：profile 列表 + 启动/关闭按钮 + 新建 profile + 自定义目录；
//   - 每个运行中的 profile 自动分配 remote-debugging-port（持久化到 ports.json）；
//   - 只做进程拉起/关闭（CreateProcess 级别），不注入、不读浏览器内存；
//   - 启动参数透传自反编译结论：--user-data-dir / --profile-directory / --remote-debugging-port。
package main

import (
	"encoding/json"
	"fmt"
	"html/template"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// ---------- 配置 ----------

type Config struct {
	// SunBrowser.exe 所在目录（含版本子目录 152.0.7977.54 的那一级）。
	SunBrowserDir string `json:"sun_browser_dir"`
	// 数据根目录，每个子目录 = 一个 profile（默认 F:\.ADSPOWER_GLOBAL\cache）。
	DataDir string `json:"data_dir"`
	// Web 界面监听地址。
	Listen string `json:"listen"`
	// 调试端口分配起点。
	PortBase int `json:"port_base"`
}

func defaultConfig() Config {
	return Config{
		SunBrowserDir: `C:\Users\admin6\AppData\Roaming\adspower_global\cwd_global\chrome_152`,
		DataDir:       `F:\.ADSPOWER_GLOBAL\cache`,
		Listen:        "127.0.0.1:18900",
		PortBase:      19222,
	}
}

func configPath() string {
	exe, err := os.Executable()
	if err != nil {
		return "sunlauncher.json"
	}
	return filepath.Join(filepath.Dir(exe), "sunlauncher.json")
}

func loadConfig() Config {
	cfg := defaultConfig()
	data, err := os.ReadFile(configPath())
	if err != nil {
		return cfg // 首次运行：用默认配置
	}
	_ = json.Unmarshal(data, &cfg)
	if cfg.SunBrowserDir == "" {
		cfg.SunBrowserDir = defaultConfig().SunBrowserDir
	}
	if cfg.DataDir == "" {
		cfg.DataDir = defaultConfig().DataDir
	}
	if cfg.Listen == "" {
		cfg.Listen = defaultConfig().Listen
	}
	if cfg.PortBase <= 0 {
		cfg.PortBase = defaultConfig().PortBase
	}
	return cfg
}

func saveConfig(cfg Config) error {
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(configPath(), data, 0644)
}

// ---------- 运行状态 ----------

type Profile struct {
	Name    string `json:"name"`
	Path    string `json:"path"`
	Running bool   `json:"running"`
	PID     int    `json:"pid,omitempty"`
	Port    int    `json:"port,omitempty"`
}

type portsFile struct {
	Ports map[string]int `json:"ports"`
}

func portsPath() string {
	exe, err := os.Executable()
	if err != nil {
		return "ports.json"
	}
	return filepath.Join(filepath.Dir(exe), "ports.json")
}

type Manager struct {
	mu   sync.Mutex
	cfg  Config
	proc map[string]*exec.Cmd // profile名 -> 运行中的进程
	port map[string]int       // profile名 -> 调试端口（持久化）
}

func NewManager(cfg Config) *Manager {
	m := &Manager{cfg: cfg, proc: map[string]*exec.Cmd{}, port: map[string]int{}}
	if data, err := os.ReadFile(portsPath()); err == nil {
		var pf portsFile
		if json.Unmarshal(data, &pf) == nil {
			for k, v := range pf.Ports {
				m.port[k] = v
			}
		}
	}
	return m
}

func (m *Manager) savePorts() {
	data, _ := json.MarshalIndent(portsFile{Ports: m.port}, "", "  ")
	_ = os.WriteFile(portsPath(), data, 0644)
}

// Profiles 扫描数据目录：每个子目录 = 一个 profile。
func (m *Manager) Profiles() []Profile {
	m.mu.Lock()
	defer m.mu.Unlock()
	entries, err := os.ReadDir(m.cfg.DataDir)
	if err != nil {
		return nil
	}
	var out []Profile
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		if strings.HasPrefix(e.Name(), ".") || strings.HasPrefix(e.Name(), "_") {
			continue
		}
		p := Profile{Name: e.Name(), Path: filepath.Join(m.cfg.DataDir, e.Name())}
		if cmd, ok := m.proc[e.Name()]; ok && cmd.Process != nil {
			p.Running = true
			p.PID = cmd.Process.Pid
		}
		if port, ok := m.port[e.Name()]; ok {
			p.Port = port
		}
		out = append(out, p)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

// allocPort 为 profile 分配一个可用端口（已分配过的复用）。
func (m *Manager) allocPort(name string) int {
	if p, ok := m.port[name]; ok && p > 0 && portFree(p) {
		return p
	}
	used := map[int]bool{}
	for _, p := range m.port {
		used[p] = true
	}
	for p := m.cfg.PortBase; p < m.cfg.PortBase+1000; p++ {
		if !used[p] && portFree(p) {
			m.port[name] = p
			m.savePorts()
			return p
		}
	}
	return 0
}

func portFree(port int) bool {
	l, err := net.Listen("tcp", "127.0.0.1:"+strconv.Itoa(port))
	if err != nil {
		return false
	}
	l.Close()
	return true
}

// Start 拉起 SunBrowser：透传 --user-data-dir / --profile-directory / --remote-debugging-port。
func (m *Manager) Start(name string) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if cmd, ok := m.proc[name]; ok && cmd.Process != nil {
		return fmt.Errorf("profile %s 已在运行 (pid=%d)", name, cmd.Process.Pid)
	}
	exe := filepath.Join(m.cfg.SunBrowserDir, "SunBrowser.exe")
	if _, err := os.Stat(exe); err != nil {
		return fmt.Errorf("找不到 SunBrowser.exe：%s", exe)
	}
	dataDir := filepath.Join(m.cfg.DataDir, name)
	if err := os.MkdirAll(dataDir, 0755); err != nil {
		return err
	}
	port := m.allocPort(name)
	if port == 0 {
		return fmt.Errorf("无可用调试端口")
	}
	args := []string{
		"--user-data-dir=" + dataDir,
		"--profile-directory=Default",
		"--remote-debugging-port=" + strconv.Itoa(port),
		"--no-first-run",
		"--no-default-browser-check",
		"about:blank",
	}
	cmd := exec.Command(exe, args...)
	cmd.Dir = m.cfg.SunBrowserDir // 关键：工作目录必须是版本子目录的父级，保证拼 152.0.7977.54 正确
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("启动失败：%v", err)
	}
	// 2 秒内退出视为启动失败（静默退出分支）。
	time.Sleep(2 * time.Second)
	if cmd.ProcessState != nil && cmd.ProcessState.Exited() {
		return fmt.Errorf("SunBrowser 2 秒内退出（可能缺 chrome.dll/版本子目录），exit=%v", cmd.ProcessState)
	}
	m.proc[name] = cmd
	go func() {
		cmd.Wait()
		m.mu.Lock()
		delete(m.proc, name)
		m.mu.Unlock()
	}()
	return nil
}

// Stop 关闭 profile 进程。
func (m *Manager) Stop(name string) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	cmd, ok := m.proc[name]
	if !ok || cmd.Process == nil {
		return fmt.Errorf("profile %s 未在运行", name)
	}
	if err := cmd.Process.Kill(); err != nil {
		return err
	}
	delete(m.proc, name)
	return nil
}

// ---------- Web 界面 ----------

var pageTmpl = template.Must(template.New("page").Parse(`<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>SunLauncher</title>
<style>
body{font-family:"Microsoft YaHei",sans-serif;max-width:900px;margin:24px auto;padding:0 16px;background:#f5f5f5}
h1{font-size:22px}.card{background:#fff;border-radius:8px;padding:16px;margin:12px 0;box-shadow:0 1px 4px rgba(0,0,0,.1)}
.row{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.badge{padding:2px 10px;border-radius:12px;font-size:12px}
.on{background:#e6f7e6;color:#137333}.off{background:#eee;color:#666}
button{padding:6px 18px;border:0;border-radius:6px;cursor:pointer;font-size:14px}
.start{background:#1a73e8;color:#fff}.stop{background:#d93025;color:#fff}.gray{background:#e8e8e8}
input[type=text]{padding:6px 10px;width:340px;border:1px solid #ccc;border-radius:6px}
.err{color:#d93025}.ok{color:#137333}
code{background:#f0f0f0;padding:2px 6px;border-radius:4px;font-size:12px}
</style></head><body>
<h1>SunLauncher（SunBrowser 启动器）</h1>
<div class="card">
<div class="row"><b>数据目录</b><code>{{.Cfg.DataDir}}</code></div>
<div class="row" style="margin-top:8px"><b>浏览器目录</b><code>{{.Cfg.SunBrowserDir}}</code></div>
<form method="POST" action="/config" style="margin-top:12px">
<div class="row">
<input type="text" name="data_dir" value="{{.Cfg.DataDir}}" placeholder="数据目录">
<input type="text" name="sun_browser_dir" value="{{.Cfg.SunBrowserDir}}" placeholder="浏览器目录">
<button class="gray" type="submit">保存目录</button>
</div></form>
{{if .Msg}}<p class="{{.MsgClass}}">{{.Msg}}</p>{{end}}
</div>
<div class="card">
<div class="row"><b>新建 profile</b>
<form method="POST" action="/create" style="display:inline">
<input type="text" name="name" placeholder="如 k1new01_hyg6dd" required>
<button class="gray" type="submit">新建</button>
</form></div>
</div>
{{range .Profiles}}
<div class="card"><div class="row">
<b>{{.Name}}</b>
{{if .Running}}<span class="badge on">运行中 pid={{.PID}} port={{.Port}}</span>
<form method="POST" action="/stop" style="display:inline">
<input type="hidden" name="name" value="{{.Name}}">
<button class="stop" type="submit">关闭</button></form>
{{else}}<span class="badge off">已停止{{if .Port}} port={{.Port}}{{end}}</span>
<form method="POST" action="/start" style="display:inline">
<input type="hidden" name="name" value="{{.Name}}">
<button class="start" type="submit">启动</button></form>
{{end}}
</div><div class="row"><code>{{.Path}}</code></div>
</div>
{{else}}<div class="card">数据目录下没有 profile，请新建。</div>
{{end}}
</body></html>`))

type pageData struct {
	Cfg      Config
	Profiles []Profile
	Msg      string
	MsgClass string
}

func main() {
	mgr := NewManager(loadConfig())

	mux := http.NewServeMux()
	render := func(w http.ResponseWriter, msg, cls string) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		pageTmpl.Execute(w, pageData{Cfg: mgr.cfg, Profiles: mgr.Profiles(), Msg: msg, MsgClass: cls})
	}
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) { render(w, "", "") })
	mux.HandleFunc("/start", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Redirect(w, r, "/", http.StatusSeeOther)
			return
		}
		name := strings.TrimSpace(r.FormValue("name"))
		if name == "" {
			render(w, "profile 名为空", "err")
			return
		}
		if err := mgr.Start(name); err != nil {
			render(w, "启动失败："+err.Error(), "err")
			return
		}
		render(w, "已启动 "+name, "ok")
	})
	mux.HandleFunc("/stop", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Redirect(w, r, "/", http.StatusSeeOther)
			return
		}
		name := strings.TrimSpace(r.FormValue("name"))
		if err := mgr.Stop(name); err != nil {
			render(w, "关闭失败："+err.Error(), "err")
			return
		}
		render(w, "已关闭 "+name, "ok")
	})
	mux.HandleFunc("/create", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Redirect(w, r, "/", http.StatusSeeOther)
			return
		}
		name := strings.TrimSpace(r.FormValue("name"))
		if name == "" || strings.ContainsAny(name, `/\:*?"<>|`) {
			render(w, "非法 profile 名", "err")
			return
		}
		if err := os.MkdirAll(filepath.Join(mgr.cfg.DataDir, name, "Default"), 0755); err != nil {
			render(w, "新建失败："+err.Error(), "err")
			return
		}
		render(w, "已新建 "+name, "ok")
	})
	mux.HandleFunc("/config", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Redirect(w, r, "/", http.StatusSeeOther)
			return
		}
		if v := strings.TrimSpace(r.FormValue("data_dir")); v != "" {
			mgr.cfg.DataDir = v
		}
		if v := strings.TrimSpace(r.FormValue("sun_browser_dir")); v != "" {
			mgr.cfg.SunBrowserDir = v
		}
		if err := saveConfig(mgr.cfg); err != nil {
			render(w, "保存失败："+err.Error(), "err")
			return
		}
		render(w, "目录已保存", "ok")
	})
	// JSON 状态接口，供 RPA/脚本轮询。
	mux.HandleFunc("/api/profiles", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		json.NewEncoder(w).Encode(mgr.Profiles())
	})

	log.Printf("SunLauncher listening on http://%s", mgr.cfg.Listen)
	log.Fatal(http.ListenAndServe(mgr.cfg.Listen, mux))
}
