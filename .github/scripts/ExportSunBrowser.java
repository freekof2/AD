// ExportSunBrowser.java
// SunBrowser headless 定向/全量反编译导出脚本 (Java GhidraScript, headless 可用)
// Usage: analyzeHeadless ... -postScript ExportSunBrowser.java <true|false> -scriptPath .github/scripts
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
import ghidra.program.model.symbol.ReferenceIterator;

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

    private void writeFile(String name, String content) throws Exception {
        File f = new File(outDir, name);
        BufferedWriter w = new BufferedWriter(
                new OutputStreamWriter(new FileOutputStream(f), StandardCharsets.UTF_8));
        w.write(content);
        w.close();
    }

    @Override
    public void run() throws Exception {
        boolean full = false;
        String[] args = getScriptArgs();
        if (args != null && args.length > 0) {
            String a = args[0].toLowerCase();
            if (a.equals("true") || a.equals("full") || a.equals("1")) {
                full = true;
            }
        }

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
        Collections.sort(funcs, new Comparator<Function>() {
            public int compare(Function a, Function b) {
                return a.getName().compareTo(b.getName());
            }
        });

        StringBuilder sb = new StringBuilder();
        sb.append("Program: ").append(currentProgram.getName()).append("\n");
        sb.append("ImageBase: ").append(currentProgram.getImageBase()).append("\n");
        sb.append("Language: ").append(currentProgram.getLanguage().getLanguageID()).append("\n");
        sb.append("Full: ").append(full).append("\n\n");
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

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        List<Function> targets = new ArrayList<Function>();
        if (full) {
            int cap = Math.min(funcs.size(), 1500);
            for (int i = 0; i < cap; i++) {
                targets.add(funcs.get(i));
            }
        } else {
            // 定向：入口链 entry -> FUN_xxx -> WinMain/wmain，
            // 外加 chrome/elf/loadlibrary/createprocess/signal 相关的真实函数，
            // 再用名称排序的前 N 个兜底，保证 entry-decompiled.c 可读且不爆炸。
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
        }

        // 跟随入口调用链：entry 调用的前两层 FUN_xxx 也一并反编译，
        // 这样能看到 WinMain 级别的 LoadLibrary(chrome.dll)/ExitProcess 逻辑。
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

        if (full) {
            int shardSize = 50;
            int shardIdx = 0;
            int done = 0;
            StringBuilder shard = new StringBuilder();
            for (int i = 0; i < targets.size(); i++) {
                Function f = targets.get(i);
                shard.append("// ===== ").append(f.getName())
                     .append(" @ ").append(f.getEntryPoint()).append(" =====\n");
                try {
                    DecompileResults r = decomp.decompileFunction(f, 60, monitor);
                    if (r != null && r.getDecompiledFunction() != null) {
                        shard.append(r.getDecompiledFunction().getC()).append("\n\n");
                    } else {
                        shard.append("// decompile failed or timeout\n\n");
                    }
                } catch (Exception e) {
                    shard.append("// ERROR ").append(e).append("\n\n");
                }
                done++;
                if ((done % shardSize == 0) || i == targets.size() - 1) {
                    writeFile(String.format("full-decompiled-%03d.c", shardIdx), shard.toString());
                    shardIdx++;
                    shard = new StringBuilder();
                }
            }
            println("ExportSunBrowser full done: funcs=" + funcs.size()
                + " exported=" + done + " shards=" + shardIdx);
        } else {
            StringBuilder buf = new StringBuilder();
            for (Function f : targets) {
                buf.append("// ===== ").append(f.getName())
                   .append(" @ ").append(f.getEntryPoint()).append(" =====\n");
                try {
                    DecompileResults r = decomp.decompileFunction(f, 60, monitor);
                    if (r != null && r.getDecompiledFunction() != null) {
                        buf.append(r.getDecompiledFunction().getC()).append("\n\n");
                    } else {
                        buf.append("// decompile failed or timeout\n\n");
                    }
                } catch (Exception e) {
                    buf.append("// ERROR ").append(e).append("\n\n");
                }
            }
            if (buf.length() == 0) {
                buf.append("// no functions found\n");
            }
            writeFile("entry-decompiled.c", buf.toString());
            println("ExportSunBrowser directional done: funcs=" + funcs.size()
                + " exported=" + targets.size());
        }

        decomp.dispose();
    }
}
