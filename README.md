# AD 静态反编译工作流

本仓库提供一个**不执行 Windows 样本**的、可复现的静态分析工作流。它面向 `AdsPower.Global.exe` 1.0.0.1 发布资产，先校验 SHA-256，再读取 PE 头、导入表、CLR 目录、可打印字符串和常见嵌入载荷标记，最后生成 JSON 与 Markdown 报告。若检测到 .NET/CLR，工作流可以在显式开启 `--decompile` 后调用已安装的 `ilspycmd`；否则会记录跳过原因。任何 JavaScript、DLL 或 EXE 都不会被加载或启动。

> 使用本仓库分析第三方软件前，请确认你拥有相应的授权、许可或安全研究依据。该工作流不用于绕过授权、破解保护、窃取凭据或规避反分析机制。

## 目录结构

| 路径 | 用途 |
| --- | --- |
| `scripts/static_decompile.py` | 下载、哈希校验、PE 指纹、导入表、字符串与嵌入载荷扫描，以及可选代码恢复入口 |
| `Makefile` | 本地可复现命令入口 |
| `.github/workflows/static-analysis.yml` | 手动触发的 GitHub Actions 工作流 |
| `samples/` | 本地样本目录，默认被 `.gitignore` 忽略，不提交二进制 |
| `analysis/` | 分析工件目录，默认被 `.gitignore` 忽略，可作为 Actions artifact 上传 |

## 本地运行

建议在一次性容器、离线虚拟机或专用分析主机中运行；不要双击样本，也不要把分析目录加入会自动加载脚本的开发环境。安装可选的 PE 解析依赖后，执行：

```bash
python3 -m pip install --user -r requirements-static.txt
make fetch
make analyze
```

默认资产与发布页给出的摘要如下：

| 字段 | 值 |
| --- | --- |
| 发布标签 | `1.0.0.1` |
| 文件 | `AdsPower.Global.exe` |
| 公开下载地址 | `https://github.com/freekof2/AD/releases/download/1.0.0.1/AdsPower.Global.exe` |
| SHA-256 | `c9ac99bb20ef2121a4e1c4331af249c4db4edd81b7482a2e3a04c7ee59d309cb` |

如果你已经通过受信任渠道取得样本，也可以跳过下载步骤：

```bash
python3 scripts/static_decompile.py local \
  --sample /absolute/path/to/AdsPower.Global.exe \
  --analysis-dir analysis/local
```

只有在确认所需工具与分析环境隔离后，才显式增加 `--decompile`。它不会让样本运行；它只会在检测到 CLR 目录且本机存在 `ilspycmd` 时尝试恢复 .NET 项目。对于 Electron/Node 应用，应先对安装包做**只读归档提取**，再将单独的 `app.asar` 放到 `analysis/app.asar`，由后续工具处理；不要直接执行其中的 JavaScript。

## GitHub Actions

工作流仅支持 `workflow_dispatch`，避免在普通提交时自动下载和处理大型二进制。进入仓库的 **Actions → Static sample analysis → Run workflow**，保留默认 URL 与 SHA-256，或输入你有权分析的其他 HTTPS 样本。工作流会：

1. 检出仓库并安装 Python 依赖；
2. 使用脚本下载并校验样本；
3. 读取 PE 元数据、导入表、字符串和嵌入载荷标记；
4. 将 `analysis/` 作为构建工件上传；
5. 默认不启用反编译器、不执行样本、不进行网络连接。

工作流输出的 `report.json`、`SUMMARY.md` 和 `strings.txt` 仅代表静态 triage 结果，不是运行时行为证明。动态调试、网络交互、凭据访问、反分析绕过和持久化测试均不在本仓库范围内。

## 结果文件

| 文件 | 内容 |
| --- | --- |
| `analysis/report.json` | 样本身份、PE 结构、导入表、数据目录与工具状态 |
| `analysis/SUMMARY.md` | 适合人工审阅的摘要 |
| `analysis/strings.txt` | 去重后的 ASCII 与 UTF-16LE 可打印字符串 |

## 许可证与样本处理

仓库代码沿用原仓库当前状态；发布资产不应提交到 Git。请根据软件许可、所在司法辖区和研究授权决定是否可以保存、提取或发布反编译结果。
