// ExportSunBrowser.java
// SunBrowser / AdsPower headless 分片全量反编译导出脚本 (Java GhidraScript, headless 可用)
// Usage: analyzeHeadless ... -postScript ExportSunBrowser.java <mode> <shardIndex> <shardTotal> <funcTimeoutSec> -scriptPath .github/scripts
//   mode: full|true|1 = 全量分片反编译(默认); triage|list = 只导出清单不反编译; 其他 = 定向反编译(兼容老工作流 false)
//   shardIndex/shardTotal: 切片参数, 默认 0/1(不切片, 真全量, 无1500上限)
//   funcTimeoutSec: 单函数反编译超时秒, 默认 30
// Read-only: only reads program structure and exports decompiled C, never executes the sample.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.ExternalLocation;
import ghidra.program.model.symbol.Reference;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

public class ExportSunBrowser extends GhidraScript {

    private File outDir;
    private int shardIndex = 0;
    private int shardTotal = 1;
    private int funcTimeout = 30;

    private void writeFile(String name, String content) throws Exception {
        File f = new File(outDir, name);
        BufferedWriter w = new BufferedWriter(
                new OutputStreamWriter(new FileOutputStream(f, false), StandardCharsets.UTF_8));
        w.write(content);
        w.close();
    }

    private void appendFile(String name, String content) throws Exception {
        File f = new File(outDir, name);
        BufferedWriter w = new BufferedWriter(
                new OutputStreamWriter(new FileOutputStream(f, true), StandardCharsets.UTF_8));
        w.write(content);
        w.close();
    }

    @Override
    public void run() throws Exception {
        String mode = "directional";
        String[] args = getScriptArgs();
        if (args != null && args.length > 0) {
            String a = args[0].trim().toLowerCase();
            if (a.equals("true") || a.equals("full") || a.equals("1")) {
                mode = "full";
            } else if (a.equals("triage") || a.equals("list")) {
                mode = "triage";
            } else {
                mode = "directional";
            }
        }
        if (args != null && args.length > 1) {
            try { shardIndex = Integer.parseInt(args[1].trim()); } catch (Exception e) { shardIndex = 0; }
        }
        if (args != null && args.length > 2) {
            try { shardTotal = Integer.parseInt(args[2].trim()); } catch (Exception e) { shardTotal = 1; }
        }
        if (args != null && args.length > 3) {
            try { funcTimeout = Integer.parseInt(args[3].trim()); } catch (Exception e) { funcTimeout = 30; }
        }
        if (shardTotal < 1) { shardTotal = 1; }
        if (shardIndex < 0) { shardIndex = 0; }
        if (shardIndex >= shardTotal) { shardIndex = shardTotal - 1; }
        if (funcTimeout < 5) { funcTimeout = 5; }
        if (funcTimeout > 300) { funcTimeout = 300; }

        outDir = new File(System.getProperty("user.dir"), "analysis/ghidra/out");
        outDir.mkdirs();

        FunctionManager fm = currentProgram.getFunctionManager();
        List<Function> funcs = new ArrayList<Function>();
        // FunctionManager 只有 getFunctions(boolean) / getFunctions(addr,boolean)，
        // 没有 getAllFunctions()，用 getFunctions(true) 拿全部非外部函数（地址升序）。
        FunctionIterator it = fm.getFunctions(true);
        while (it.hasNext()) {
            funcs.add(it.next());
        }
        // 稳定排序: 名称 + 入口地址，保证多分片划分可复现、可拼回全量。
        Collections.sort(funcs, new Comparator<Function>() {
            public int compare(Function a, Function b) {
                int c = a.getName().compareTo(b.getName());
                if (c != 0) {
                    return c;
                }
                return a.getEntryPoint().toString().compareTo(b.getEntryPoint().toString());
            }
        });

        // 元数据导出开销小，每个分片都写一份，collect 任取一份即可，
        // 避免 shard0 超时导致清单缺失。
        StringBuilder sb = new StringBuilder();
        sb.append("Program: ").append(currentProgram.getName()).append("\n");
        sb.append("ImageBase: ").append(currentProgram.getImageBase()).append("\n");
        sb.append("Language: ").append(currentProgram.getLanguage().getLanguageID()).append("\n");
        sb.append("Mode: ").append(mode).append("\n");
        sb.append("Shard: ").append(shardIndex).append("/").append(shardTotal).append("\n\n");
        for (Function f : funcs) {
            if (sb.length() > 8 * 1024 * 1024) {
                break;
            }
            sb.append(f.getName()).append(" @ ").append(f.getEntryPoint()).append("\n");
        }
        sb.append("Total=").append(funcs.size()).append("\n");
        writeFile("function-list.txt", sb.toString());

        StringBuilder im = new StringBuilder();
        FunctionIterator ext = fm.getExternalFunctions();
        int extCount = 0;
        while (ext.hasNext()) {
            Function f = ext.next();
            extCount++;
            String lib = "?";
            try {
                ExternalLocation loc = f.getExternalLocation();
                if (loc != null) {
                    lib = loc.getLibraryName();
                }
            } catch (Exception e) {
                lib = "?";
            }
            im.append(lib).append("!").append(f.getName())
              .append(" @ ").append(f.getEntryPoint()).append("\n");
        }
        im.append("TotalExternal=").append(extCount).append("\n");
        writeFile("imports.txt", im.toString());

        StringBuilder sec = new StringBuilder();
        Memory mem = currentProgram.getMemory();
        for (MemoryBlock b : mem.getBlocks()) {
            sec.append(b.getName())
               .append(" start=").append(b.getStart())
               .append(" size=").append(b.getSize())
               .append(" R=").append(b.isRead())
               .append(" W=").append(b.isWrite())
               .append(" X=").append(b.isExecute())
               .append("\n");
        }
        writeFile("sections.txt", sec.toString());

        if (mode.equals("triage")) {
            writeFile(String.format("shard-%02d.txt", shardIndex),
                "mode=triage\nshard=" + shardIndex + "/" + shardTotal
                + "\ntotal=" + funcs.size() + "\ndone=0\nfailed=0\n");
            println("ExportSunBrowser triage done: funcs=" + funcs.size());
            return;
        }

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        if (mode.equals("full")) {
            runFullShard(fm, funcs, decomp);
            decomp.dispose();
            return;
        }

        runDirectional(fm, funcs, decomp);
        decomp.dispose();
    }

    // 全量分片：按 shardIndex/shardTotal 切片，无函数数量上限；
    // 片内关键词优先 + 每10个增量落盘 + 心跳，超时被杀时已落盘部分可救回。
    private void runFullShard(FunctionManager fm, List<Function> funcs,
            DecompInterface decomp) throws Exception {
        int n = funcs.size();
        int start = (int) ((long) n * shardIndex / shardTotal);
        int end = (int) ((long) n * (shardIndex + 1) / shardTotal);
        List<Function> slice = new ArrayList<Function>(funcs.subList(start, end));

        // 片内关键词优先(SunBrowser 启动链先反编译)，不做跨片截断，保证可拼回全量。
        final String[] prio = {"sunbrowser", "user-data", "user_data", "userdata",
            "debugging", "chrome", "elf", "profile", "spawn", "execfile",
            "createprocess", "shellexecute", "loadlibrary", "getmodulefilename",
            "winmain", "wmain", "entry"};
        Collections.sort(slice, new Comparator<Function>() {
            public int compare(Function a, Function b) {
                int pa = prioRank(a.getName().toLowerCase());
                int pb = prioRank(b.getName().toLowerCase());
                if (pa != pb) {
                    return pa - pb;
                }
                int c = a.getName().compareTo(b.getName());
                if (c != 0) {
                    return c;
                }
                return a.getEntryPoint().toString().compareTo(b.getEntryPoint().toString());
            }
            private int prioRank(String name) {
                for (int i = 0; i < prio.length; i++) {
                    if (name.indexOf(prio[i]) >= 0) {
                        return i;
                    }
                }
                return prio.length;
            }
        });

        String outName = String.format("full-decompiled-s%02d.c", shardIndex);
        writeFile(outName, "// shard " + shardIndex + "/" + shardTotal
            + " total=" + n + " range=[" + start + "," + end + ")\n\n");

        int done = 0;
        int failed = 0;
        StringBuilder buf = new StringBuilder();
        for (int i = 0; i < slice.size(); i++) {
            if (monitor.isCancelled()) {
                buf.append("// CANCELLED by monitor\n");
                break;
            }
            Function f = slice.get(i);
            buf.append("// ===== ").append(f.getName())
               .append(" @ ").append(f.getEntryPoint()).append(" =====\n");
            try {
                DecompileResults r = decomp.decompileFunction(f, funcTimeout, monitor);
                if (r != null && r.getDecompiledFunction() != null) {
                    buf.append(r.getDecompiledFunction().getC()).append("\n\n");
                } else {
                    buf.append("// decompile failed or timeout\n\n");
                    failed++;
                }
            } catch (Exception e) {
                buf.append("// ERROR ").append(e).append("\n\n");
                failed++;
            }
            done++;
            if (done % 10 == 0 || i == slice.size() - 1) {
                appendFile(outName, buf.toString());
                buf = new StringBuilder();
                println("ExportSunBrowser shard=" + shardIndex + "/" + shardTotal
                    + " done=" + done + "/" + slice.size() + " failed=" + failed);
            }
        }
        if (buf.length() > 0) {
            appendFile(outName, buf.toString());
        }
        writeFile(String.format("shard-%02d.txt", shardIndex),
            "mode=full\nshard=" + shardIndex + "/" + shardTotal
            + "\ntotal=" + n + "\nrange=[" + start + "," + end + ")"
            + "\nslice=" + slice.size() + "\ndone=" + done + "\nfailed=" + failed + "\n");
        println("ExportSunBrowser full shard done: shard=" + shardIndex + "/" + shardTotal
            + " funcs=" + n + " slice=" + slice.size() + " done=" + done + " failed=" + failed);
    }

    // 定向：入口链 entry -> FUN_xxx -> WinMain/wmain，
    // 外加 chrome/elf/loadlibrary/createprocess/signal 相关的真实函数，
    // 再用名称排序的前 N 个兜底，保证 entry-decompiled.c 可读且不爆炸。
    // 跟随入口调用链往下 2 层，能看到 WinMain 级别的 LoadLibrary/ExitProcess 逻辑。
    private void runDirectional(FunctionManager fm, List<Function> funcs,
            DecompInterface decomp) throws Exception {
        List<Function> targets = new ArrayList<Function>();
        java.util.Set<Function> picked = new java.util.LinkedHashSet<Function>();
        for (Function f : funcs) {
            String n = f.getName().toLowerCase();
            if (n.equals("entry") || n.equals("_entry") || n.equals("winmain")
                    || n.equals("wwinmain") || n.equals("wmain") || n.equals("main")
                    || n.equals("start") || n.equals("_start")
                    || n.equals("tls_callback") || n.equals("invoke_main")
                    || n.equals("__tmaincrtstartup")) {
                picked.add(f);
            }
        }
        String[] keys = {"chrome", "elf", "signal", "loadlibrary", "createprocess",
            "getmodulefilename", "winmain", "wmain", "isbrowser", "crash"};
        for (Function f : funcs) {
            String n = f.getName().toLowerCase();
            for (String k : keys) {
                if (n.indexOf(k) >= 0) {
                    picked.add(f);
                    break;
                }
            }
        }
        for (Function f : funcs) {
            if (picked.size() >= 40) {
                break;
            }
            picked.add(f);
        }
        targets.addAll(picked);
        if (targets.size() > 40) {
            targets = targets.subList(0, 40);
        }

        {
            java.util.Set<Function> chain = new java.util.LinkedHashSet<Function>(targets);
            Listing listing = currentProgram.getListing();
            int depth = 0;
            java.util.List<Function> frontier = new java.util.ArrayList<Function>(targets);
            while (depth < 2 && !frontier.isEmpty()
                    && chain.size() < 60) {
                java.util.List<Function> next = new java.util.ArrayList<Function>();
                for (Function f : frontier) {
                    InstructionIterator ins = listing.getInstructions(f.getBody(), true);
                    while (ins.hasNext() && chain.size() < 60) {
                        Instruction in = ins.next();
                        for (Reference r : in.getReferencesFrom()) {
                            if (r.getReferenceType().isCall()) {
                                Address to = r.getToAddress();
                                Function callee =
                                    fm.getFunctionContaining(to);
                                if (callee != null && !callee.isExternal()
                                        && !chain.contains(callee)) {
                                    chain.add(callee);
                                    next.add(callee);
                                }
                            }
                        }
                    }
                }
                frontier = next;
                depth++;
            }
            targets = new java.util.ArrayList<Function>(chain);
        }

        StringBuilder buf = new StringBuilder();
        int done = 0;
        for (Function f : targets) {
            buf.append("// ===== ").append(f.getName())
               .append(" @ ").append(f.getEntryPoint()).append(" =====\n");
            try {
                DecompileResults r = decomp.decompileFunction(f, funcTimeout, monitor);
                if (r != null && r.getDecompiledFunction() != null) {
                    buf.append(r.getDecompiledFunction().getC()).append("\n\n");
                } else {
                    buf.append("// decompile failed or timeout\n\n");
                }
            } catch (Exception e) {
                buf.append("// ERROR ").append(e).append("\n\n");
            }
            done++;
            if (done % 10 == 0) {
                println("ExportSunBrowser directional done=" + done + "/" + targets.size());
            }
        }
        if (buf.length() == 0) {
            buf.append("// no functions found\n");
        }
        writeFile("entry-decompiled.c", buf.toString());
        println("ExportSunBrowser directional done: funcs=" + funcs.size()
            + " exported=" + targets.size());
    }
}
