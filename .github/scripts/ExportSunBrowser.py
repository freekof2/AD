# @category SunBrowser
# SunBrowser 定向反编译导出脚本（Ghidra Jython）
# 用法: analyzeHeadless ... -postScript ExportSunBrowser.py <full|false> -scriptPath .github/scripts
# 只读取程序结构并导出伪C，不执行样本。
from ghidra.app.decompiler import DecompInterface
from java.io import File

program = getCurrentProgram()

# 取 postScript 参数：full=true 则全量反编译，否则定向
full = False
try:
    args = getScriptArgs()
    if args and len(args) > 0 and str(args[0]).lower() in ("true", "full", "1"):
        full = True
except Exception:
    pass

outdir = File("analysis/ghidra/out")
outdir.mkdirs()

def write_file(name, text):
    f = File(outdir, name)
    w = open(f.getAbsolutePath(), "w", encoding="utf-8", errors="ignore")
    w.write(text)
    w.close()

# 1. 函数清单
fm = program.getFunctionManager()
funcs = list(fm.getAllFunctions())
funcs.sort(key=lambda f: f.getName())
lines = []
for f in funcs[:10000]:
    lines.append("%s @ %s" % (f.getName(), f.getEntryPoint()))
write_file("function-list.txt", "\n".join(lines) + "\nTotal=%d\nfull=%s\n" % (len(funcs), full))

# 2. 反编译目标选择
decomp = DecompInterface()
decomp.openProgram(program)

if full:
    targets = funcs
else:
    targets = []
    for f in funcs:
        n = f.getName().lower()
        if n in ("entry", "_entry", "winmain", "wmain", "wWinMain",
                 "main", "wmain", "start", "_start", "tls_callback",
                 "invoke_main", "__tmainCRTStartup", "WinMain"):
            targets.append(f)
    # 兜底：补前 30 个函数，防止入口名不匹配导致空输出
    for f in funcs[:30]:
        if f not in targets:
            targets.append(f)
    targets = targets[:80]

buf = []
for f in targets:
    try:
        r = decomp.decompileFunction(f, 60, None)
        buf.append("// ===== %s @ %s =====" % (f.getName(), f.getEntryPoint()))
        if r and r.getDecompiledFunction():
            buf.append(r.getDecompiledFunction().getC())
        else:
            buf.append("// decompile failed or timeout")
    except Exception as e:
        buf.append("// ===== %s : ERROR %s =====" % (f.getName(), e))

if not buf:
    buf.append("// no functions found")

write_file("entry-decompiled.c", "\n\n".join(buf))
print("ExportSunBrowser done: funcs=%d exported=%d full=%s" % (len(funcs), len(targets), full))
