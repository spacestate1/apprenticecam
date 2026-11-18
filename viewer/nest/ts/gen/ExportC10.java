// ExportC10.java - Ghidra headless post-script (project opened read-only: nothing is saved) exporting the decompile of
// n64dll.dll for the TrueShape port generator (viewer/nest/ts/gen/gen.py).  See regen.sh for the steps it applies.
// Usage: ExportC10.java <outDir> <hexLo> <hexHi> <commitPasses> <labels.txt> <crtsigs.txt> <fntable.txt> <callsites.txt>
// Outputs into <outDir>: funcs.c (FUN_<addr> per function), names.tsv (addr, original name), globals.tsv (function,
// name, addr, type, size per global use), fpassoc.tsv (right-nested float + / * ops), types.h.
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import java.io.*;
import java.util.*;

public class ExportC10 extends GhidraScript {
    Function tailCallee(Function f, Address cookie) {
        Function x = null;
        InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (ii.hasNext()) {
            Instruction in = ii.next();
            String m = in.getMnemonicString();
            if (m.equals("JMP") && in.getFlows().length == 1) {
                Function t = getFunctionAt(in.getFlows()[0]);
                if (t != null && t != f) { Function c = t.isThunk() ? t.getThunkedFunction(true) : t; if (c != null) { if (x != null && x != c) return null; x = c; } }
                continue;
            }
            if (!m.equals("RET")) continue;
            // walk back over the fall-through chain to the last writer of RAX / XMM0
            Instruction p = in.getPrevious(); Function found = null; int guard = 0;
            while (p != null && guard++ < 40 && f.getBody().contains(p.getAddress())) {
                String pm = p.getMnemonicString();
                if (pm.equals("CALL")) {
                    Address[] fl = p.getFlows();
                    if (fl.length == 1 && fl[0].equals(cookie)) { p = p.getPrevious(); continue; }
                    if (fl.length == 1) { Function t = getFunctionAt(fl[0]); if (t != null) found = t.isThunk() ? t.getThunkedFunction(true) : t; }
                    break;
                }
                boolean writes = false;
                for (Object o : p.getResultObjects()) {
                    if (o instanceof ghidra.program.model.lang.Register) {
                        String rn = ((ghidra.program.model.lang.Register) o).getBaseRegister().getName();
                        if (rn.equals("RAX") || rn.startsWith("YMM0") || rn.startsWith("XMM0") || rn.startsWith("ZMM0")) writes = true;
                    }
                }
                if (writes) return null;     // the function computes its own return value
                if (pm.startsWith("J")) return null;  // a branch target in between: not a straight tail
                p = p.getPrevious();
            }
            if (found == null) return null;
            if (x != null && x != found) return null;
            x = found;
        }
        return x;
    }

    DataType ty(String c) {
        switch (c) {
            case "d": return DoubleDataType.dataType;
            case "f": return FloatDataType.dataType;
            case "i": return IntegerDataType.dataType;
            case "u": return UnsignedLongLongDataType.dataType;
            case "p": return new PointerDataType(VoidDataType.dataType);
            case "s": return new PointerDataType(CharDataType.dataType);
            default: return VoidDataType.dataType;
        }
    }
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        File dir = new File(a[0]); dir.mkdirs();
        long lo = Long.parseUnsignedLong(a[1], 16), hi = Long.parseUnsignedLong(a[2], 16);
        int passes = Integer.parseInt(a[3]);
        List<Function> fs = new ArrayList<>();
        if (a.length > 4) {
            for (String l : java.nio.file.Files.readAllLines(new File(a[4]).toPath())) {
                l = l.trim(); if (l.isEmpty()) continue;
                Address ad = toAddr(Long.parseUnsignedLong(l, 16));
                if (getFunctionAt(ad) == null) {
                    try { disassemble(ad); createFunction(ad, null); } catch (Exception e) { printerr("createFunction " + l + ": " + e); }
                }
            }
        }
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        try (PrintWriter nw = new PrintWriter(new File(dir, "names.tsv"), "UTF-8")) {
            while (it.hasNext()) {
                Function f = it.next();
                if (f.isExternal()) continue;
                long ep = f.getEntryPoint().getOffset();
                nw.printf("%x\t%s\t%s%n", ep, f.getName(true), f.isThunk() ? "thunk" : "");
                // rename all functions (also CRT) so calls print as FUN_<addr>
                try {
                    f.setParentNamespace(currentProgram.getGlobalNamespace());
                    f.setName(String.format("FUN_%x", ep), SourceType.USER_DEFINED);
                } catch (Exception e) { printerr("rename " + ep + ": " + e); }
                if (!f.isThunk() && ep >= lo && ep < hi) fs.add(f);
            }
        }
        // CRT signatures (the analysis left most of them void / without parameters)
        if (a.length > 5) {
            for (String l : java.nio.file.Files.readAllLines(new File(a[5]).toPath())) {
                l = l.trim(); if (l.isEmpty() || l.startsWith("#")) continue;
                String[] t = l.split("\\s+");
                Function cf = getFunctionAt(toAddr(Long.parseUnsignedLong(t[0], 16)));
                if (cf == null) { printerr("no CRT function " + t[0]); continue; }
                List<ghidra.program.model.listing.Variable> ps = new ArrayList<>();
                boolean va = false, nr = false; int k = 0;
                for (int i = 2; i < t.length; i++) {
                    if (t[i].equals("+")) { va = true; continue; }
                    if (t[i].equals("!")) { nr = true; continue; }
                    ps.add(new ParameterImpl("p" + (k++), ty(t[i]), currentProgram));
                }
                if (t[1].equals("!")) nr = true;
                ReturnParameterImpl rp = new ReturnParameterImpl(ty(t[1]), currentProgram);
                cf.updateFunction("__fastcall", rp, ps, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
                cf.setVarArgs(va);
                if (nr) cf.setNoReturn(true);
            }
        }
        // function-pointer slots typed as pointers to the function they hold (fntable.txt)
        if (a.length > 6) {
            for (String l : java.nio.file.Files.readAllLines(new File(a[6]).toPath())) {
                l = l.trim(); if (l.isEmpty() || l.startsWith("#")) continue;
                String[] t = l.split("\\s+");
                Address slot = toAddr(Long.parseUnsignedLong(t[0], 16));
                Function tf = getFunctionAt(toAddr(Long.parseUnsignedLong(t[1], 16)));
                if (tf == null) { printerr("fntable: no function " + t[1]); continue; }
                try {
                    FunctionDefinitionDataType fd = new FunctionDefinitionDataType(tf, false);
                    clearListing(slot, slot.add(7));
                    createData(slot, new PointerDataType(fd));
                } catch (Exception e) { printerr("fntable " + t[0] + ": " + e); }
            }
        }
        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.openProgram(currentProgram);
        for (int p = 0; p < passes; p++) {
            int n = 0;
            for (Function f : fs) {
                if (monitor.isCancelled()) return;
                DecompileResults r = ifc.decompileFunction(f, 120, monitor);
                if (r == null || !r.decompileCompleted()) continue;
                HighFunction hf = r.getHighFunction();
                try {
                    HighFunctionDBUtil.commitParamsToDatabase(hf, true, HighFunctionDBUtil.ReturnCommitOption.NO_COMMIT, SourceType.ANALYSIS);
                } catch (Exception e) { printerr("commit " + f.getEntryPoint() + ": " + e); }
                n++;
            }
            println("commit pass " + p + ": " + n);
        }
        // register parameters from the entry spills: the DLL is unoptimised code that stores every register parameter
        // to its home slot first ("mov [rsp+8k],reg" / "movsd [rsp+8k],xmmN"), which gives the exact kind of
        // parameters 1-4 (MS x64: by position, RCX/XMM0 .. R9/XMM3).  The commit passes sometimes chose an integer
        // register for a double parameter (e.g. FUN_18010d100 params 3/4 in XMM2/XMM3) - corrected here.
        int nspill = 0;
        for (Function f : fs) {
            try {
                String[] kind = new String[5];
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getEntryPoint(), true);
                int cnt = 0;
                while (ii.hasNext() && cnt++ < 6) {
                    Instruction in = ii.next();
                    String m = in.getMnemonicString();
                    if (!(m.equals("MOV") || m.equals("MOVSD") || m.equals("MOVSS"))) break;
                    Object[] o0 = in.getOpObjects(0), o1 = in.getOpObjects(1);
                    String txt = in.toString();
                    java.util.regex.Matcher mm = java.util.regex.Pattern.compile("\\[RSP \\+ 0x([0-9a-f]+)\\],(\\w+)").matcher(txt);
                    if (!mm.find()) break;
                    int off = Integer.parseInt(mm.group(1), 16);
                    if (off % 8 != 0 || off < 8 || off > 0x20) break;
                    String reg = mm.group(2);
                    int pos = off / 8;
                    if (m.equals("MOVSD")) kind[pos] = "d"; else if (m.equals("MOVSS")) kind[pos] = "f"; else kind[pos] = "i";
                }
                int maxpos = 0;
                for (int p = 1; p <= 4; p++) if (kind[p] != null) maxpos = p;
                if (maxpos == 0) continue;
                Parameter[] cur = f.getParameters();
                boolean change = false;
                List<ghidra.program.model.listing.Variable> np = new ArrayList<>();
                for (int p = 1; p <= Math.max(maxpos, cur.length); p++) {
                    Parameter c = p <= cur.length ? cur[p - 1] : null;
                    DataType dt = c != null ? c.getDataType() : null;
                    String kd = p <= 4 ? kind[p] : null;
                    if (kd != null) {
                        boolean isF = dt != null && (dt instanceof DoubleDataType || dt instanceof FloatDataType || dt.getName().equals("double") || dt.getName().equals("float"));
                        if (kd.equals("d") && !(dt != null && dt.getName().equals("double"))) { dt = DoubleDataType.dataType; change = true; }
                        else if (kd.equals("f") && !(dt != null && dt.getName().equals("float"))) { dt = FloatDataType.dataType; change = true; }
                        else if (kd.equals("i") && (dt == null || isF)) { dt = dt == null ? Undefined8DataType.dataType : Undefined8DataType.dataType; change = true; }
                    }
                    if (dt == null) { dt = Undefined8DataType.dataType; change = true; }
                    np.add(new ParameterImpl(c != null ? c.getName() : ("param_" + p), dt, currentProgram));
                }
                if (change) {
                    f.updateFunction(f.getCallingConventionName(), f.getReturn(), np, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.ANALYSIS);
                    nspill++;
                }
            } catch (Exception e) { printerr("spill " + f.getEntryPoint() + ": " + e); }
        }
        println("spill-corrected: " + nspill);
        // tail-call returns: F ends "call X ... ret" with RAX / XMM0 untouched after the call (or "jmp X"): F returns
        // what X returns.  Give F X's return type when F's is void / undefined, to a fixpoint.
        Address cookie = toAddr(0x180224e50L);
        for (int it2 = 0; it2 < 8; it2++) {
            int nfix = 0;
            for (Function f : fs) {
                DataType fr = f.getReturnType();
                if (fr != null && !(fr instanceof VoidDataType) && !fr.getName().startsWith("undefined")) continue;
                Function x = tailCallee(f, cookie);
                if (x == null) continue;
                DataType xr = x.getReturnType();
                if (xr == null || xr instanceof VoidDataType || xr.getName().startsWith("undefined") && xr.getLength() <= 0) continue;
                if (fr != null && fr.isEquivalent(xr)) continue;
                try { f.setReturnType(xr, SourceType.ANALYSIS); nfix++; } catch (Exception e) { printerr("setReturnType " + f.getEntryPoint() + ": " + e); }
            }
            println("tail-return pass " + it2 + ": " + nfix);
            if (nfix == 0) break;
        }
        // values passed / returned in XMM registers with an "undefined" type are doubles (MSVC x64: movsd): type them,
        // so callers do not reinterpret raw bits through (double) casts
        int nx = 0;
        for (Function f : fs) {
            try {
                Parameter r = f.getReturn();
                if (r != null && r.getRegister() != null && r.getRegister().getName().startsWith("XMM0") && r.getDataType().getName().startsWith("undefined") && r.getLength() == 8) {
                    f.setReturnType(DoubleDataType.dataType, SourceType.ANALYSIS); nx++;
                }
                for (Parameter p : f.getParameters()) {
                    if (p.getRegister() != null && p.getRegister().getName().startsWith("XMM") && p.getDataType().getName().startsWith("undefined") && p.getLength() == 8) {
                        p.setDataType(DoubleDataType.dataType, SourceType.ANALYSIS); nx++;
                    }
                }
            } catch (Exception e) { printerr("xmm types " + f.getEntryPoint() + ": " + e); }
        }
        println("xmm typed: " + nx);
        // call-site prototype overrides for indirect calls with one known target (callsites.txt)
        if (a.length > 7) {
            int no = 0;
            for (String l : java.nio.file.Files.readAllLines(new File(a[7]).toPath())) {
                l = l.trim(); if (l.isEmpty() || l.startsWith("#")) continue;
                String[] t = l.split("\\s+");
                Address cs = toAddr(Long.parseUnsignedLong(t[0], 16));
                Function tf = getFunctionAt(toAddr(Long.parseUnsignedLong(t[1], 16)));
                Function owner = getFunctionContaining(cs);
                if (tf == null || owner == null) { printerr("callsite " + l + ": no function"); continue; }
                try {
                    FunctionDefinitionDataType fd = new FunctionDefinitionDataType(tf, false);
                    HighFunctionDBUtil.writeOverride(owner, cs, fd);
                    no++;
                } catch (Exception e) { printerr("callsite " + l + ": " + e); }
            }
            println("callsite overrides: " + no);
        }
        Map<String, String> globals = new TreeMap<>();
        PrintWriter fw = new PrintWriter(new File(dir, "fpassoc.tsv"), "UTF-8");
        try (PrintWriter pw = new PrintWriter(new File(dir, "funcs.c"), "UTF-8")) {
            for (Function f : fs) {
                if (monitor.isCancelled()) break;
                DecompileResults r = ifc.decompileFunction(f, 180, monitor);
                pw.printf("// ==== FUN_%x%n", f.getEntryPoint().getOffset());
                if (r == null || !r.decompileCompleted()) { pw.printf("// decompile failed%n%n"); continue; }
                pw.println(r.getDecompiledFunction().getC());
                // right-nested float + / * (printed without parentheses by PrintC: evaluation order lost)
                Iterator<PcodeOpAST> ops = r.getHighFunction().getPcodeOps();
                while (ops.hasNext()) {
                    PcodeOpAST op = ops.next();
                    int oc = op.getOpcode();
                    if (oc != PcodeOp.FLOAT_ADD && oc != PcodeOp.FLOAT_MULT) continue;
                    for (int k = 0; k < 2; k++) {
                        Varnode in = op.getInput(k);
                        PcodeOp d = in.getDef();
                        if (d != null && d.getOpcode() == oc && in.getDescendants().hasNext()) {
                            int nd = 0; Iterator<PcodeOp> it2 = in.getDescendants(); while (it2.hasNext()) { it2.next(); nd++; }
                            fw.printf("%x\t%s\t%d\t%s\t%d%n", f.getEntryPoint().getOffset(), op.getSeqnum().getTarget(), k, oc == PcodeOp.FLOAT_ADD ? "+" : "*", nd);
                        }
                    }
                }
                for (ClangLine ln : ghidra.app.decompiler.component.DecompilerUtils.toLines(r.getCCodeMarkup())) {
                    for (ClangToken t : ln.getAllTokens()) {
                        if (!(t instanceof ClangVariableToken)) continue;
                        HighVariable hv = ((ClangVariableToken) t).getHighVariable();
                        if (hv instanceof HighGlobal) {
                            HighSymbol hs = hv.getSymbol();
                            Address ad = hs != null && hs.getStorage() != null && hs.getStorage().getMinAddress() != null ? hs.getStorage().getMinAddress() : null;
                            DataType dt = hv.getDataType();
                            String nm = t.getText();
                            globals.put(String.format("%x", f.getEntryPoint().getOffset()) + "\t" + nm + "\t" + (ad == null ? "?" : ad.toString()), (dt == null ? "?" : dt.getDisplayName()) + "\t" + (dt == null ? 0 : dt.getLength()));
                        }
                    }
                }
            }
        }
        fw.close();
        try (PrintWriter gw = new PrintWriter(new File(dir, "globals.tsv"), "UTF-8")) {
            for (Map.Entry<String, String> e : globals.entrySet()) gw.println(e.getKey() + "\t" + e.getValue());
        }
        try (PrintWriter tw = new PrintWriter(new File(dir, "types.h"), "UTF-8")) {
            DataTypeWriter w = new DataTypeWriter(currentProgram.getDataTypeManager(), tw);
            List<DataType> all = new ArrayList<>();
            Iterator<DataType> di = currentProgram.getDataTypeManager().getAllDataTypes();
            while (di.hasNext()) all.add(di.next());
            w.write(all, monitor);
        }
        ifc.dispose();
    }
}
