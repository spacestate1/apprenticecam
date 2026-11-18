#!/usr/bin/env python3
"""gen.py - turn Ghidra's decompile of n64dll.dll (exported by ExportC2.java: funcs.c, globals.tsv, names.tsv) into
compilable C for the TrueShape port.

  gen.py EXPDIR FUNCLIST OUTDIR

FUNCLIST: hex addresses of the functions to transliterate (one per line, '#' comments).  Functions called but not
listed become stubs that stop with "NOT PORTED".  Functions with a hand-written replacement in ../fix/FUN_<addr>.c are
taken from there.  CRT functions (>= 0x180219e00) map to ts_crt.c by address (CRT table in ts_crt.h).
Output: OUTDIR/ts_gen.c, OUTDIR/ts_gen.h (prototypes + global defines)."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eh, vaargs
# the DLL itself: C++ exception data (gen/eh.py) and call-site arguments (gen/vaargs.py) are read from its machine code
PE = eh.PE(os.environ.get('N64DLL', os.path.expanduser('~/mastercam-re/files/n64dll.dll')))
_pdata_end = {}
for _k in range(PE.exc_sz // 12):
    _b, _e, _u = eh.struct.unpack_from('<III', PE.d, PE.off(PE.exc_rva) + 12 * _k)
    _pdata_end.setdefault(PE.base + _b, PE.base + _e)
def ins_of(a):
    """the instructions of function a (its first .pdata range)"""
    return PE.dis(a, _pdata_end[a])
VA_ERR = []

EXP, FLIST, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
HERE = os.path.dirname(os.path.abspath(__file__))
FIXDIR = os.path.join(HERE, '..', 'fix')
REFIDX = '<repo>/decomp/nest/n64dll_index.txt'

# ---- reference line numbers (n64dll_all.c) for the citations
refL = {}
if os.path.exists(REFIDX):
    for l in open(REFIDX):
        a, n, s, c = l.split(); refL[int(a, 16)] = (n, int(s))

# ---- functions
funcs = {}
cur = None; buf = []
for l in open(os.path.join(EXP, 'funcs.c'), encoding='utf-8', errors='replace'):
    m = re.match(r'// ==== FUN_([0-9a-f]+)$', l.rstrip('\n'))
    if m:
        if cur is not None: funcs[cur] = ''.join(buf)
        cur = int(m.group(1), 16); buf = []
        continue
    buf.append(l)
if cur is not None: funcs[cur] = ''.join(buf)

REF = None
def reftext(a):
    global REF
    if REF is None: REF = open(REFIDX.replace('n64dll_index.txt', 'n64dll_all.c'), encoding='utf-8', errors='replace').read().split('\n')
    nm, L = refL[a]
    n = [int(l.split()[3]) for l in open(REFIDX) if int(l.split()[0], 16) == a][0]
    body = '\n'.join(REF[L:L + n - 1])
    body = re.sub(r'\bfree\(', 'ts_free(', body)            # CRT by FID name in the reference text: ts_crt.h
    body = re.sub(r'\bmalloc\(', 'ts_malloc(', body)
    return re.sub(r'(?<![\w:])' + re.escape(nm) + r'\s*\(', 'FUN_%x(' % a, body)

fpassoc = {}
if os.path.exists(os.path.join(EXP, 'fpassoc.tsv')):
    for l in open(os.path.join(EXP, 'fpassoc.tsv')):
        q = l.split('\t')
        if len(q) >= 5 and q[2] == '1' and q[4].strip() == '1': fpassoc.setdefault(int(q[0], 16), []).append(q[1])

# patches.txt: "@<hexaddr> reason" then "- old text" / "+ new text" line pairs; each old text must occur exactly once
PATCHES = {}
pf = os.path.join(HERE, 'patches.txt')
if os.path.exists(pf):
    cur_p = None; old = None
    for l in open(pf):
        l = l.rstrip('\n')
        if l.startswith('@'):
            cur_p = int(l[1:].split()[0], 16); PATCHES.setdefault(cur_p, [])
        elif l.startswith('- '): old = l[2:]
        elif l.startswith('+ ') and old is not None:
            PATCHES[cur_p].append((old, l[2:])); old = None
        elif l.startswith('~ '): old = ('re', l[2:])
        elif l.startswith('= ') and old is not None:
            PATCHES[cur_p].append((old, l[2:])); old = None

def apply_patches(a, t):
    for old, new in PATCHES.get(a, []):
        if isinstance(old, tuple):      # "~ regex" / "= replacement" (whitespace-tolerant)
            t2, n = re.subn(old[1], new.replace('\\', '\\\\') if False else new, t)
            if n != 1: sys.exit('patch for FUN_%x: /%s/ matched %d times' % (a, old[1], n))
            t = t2; continue
        n = t.count(old)
        if n != 1: sys.exit('patch for FUN_%x: "%s" found %d times' % (a, old, n))
        t = t.replace(old, new + ' /* patched (gen/patches.txt) */')
    return t

# patches apply to the export text as loaded, so a patched signature (an argument Ghidra did not commit) also
# reaches the prototypes and the callers' argument rules
for _a in list(PATCHES):
    if _a in funcs: funcs[_a] = apply_patches(_a, funcs[_a])

# ---- tail-call returns: "void F(..) { ...; G(..); return; }" where G returns a value: the asm leaves G's value in
# RAX / XMM0 and callers use it (extraout_*), so F returns it.  Resolved to a fixpoint over wrapper chains.
CRT_RET = {}
for l in open(os.path.join(HERE, 'crtsigs.txt')):
    l = l.split('#')[0].split()
    if len(l) >= 2: CRT_RET[int(l[0], 16)] = {'d': 'double', 'f': 'float', 'v': 'void', '!': 'void'}.get(l[1], 'uint64_t')
TAIL = re.compile(r'\n(\s*)(FUN_([0-9a-f]{9})\((?:[^;]|\n)*?\));\s*\n\s*return;\s*\n\}\s*$')
TAILPTR = re.compile(r'\n(\s*)(\(\*(?:_?DAT_[0-9a-f]+|PTR_\w+)\)\((?:[^;]|\n)*?\));\s*\n\s*return;\s*\n\}\s*$')
def sig_ret(text):
    sg = re.sub(r'/\*.*?\*/', '', text[:text.find('\n{\n')], flags=re.S).strip()
    m = re.search(r'^(.*?)\bFUN_[0-9a-f]{9}\s*\(', sg, re.S)
    return m.group(1).strip() if m else None
upgraded = {}
for _ in range(6):
    changed = False
    for a, text in funcs.items():
        if a in upgraded: continue
        r = sig_ret(text)
        if r != 'void': continue
        m = TAIL.search(text)
        if m:
            g = int(m.group(3), 16)
            gr = upgraded.get(g) or CRT_RET.get(g) or (sig_ret(funcs[g]) if g in funcs else None)
        elif TAILPTR.search(text):
            gr = 'uint64_t'; m = TAILPTR.search(text)   # tail call through a callback pointer: RAX = its value
        else: continue
        if not gr or gr == 'void': continue
        nt = gr if gr in ('double', 'float') else 'uint64_t'
        i = text.find('\n{\n'); head = text[:i]
        head = re.sub(r'(?m)^void(\s+FUN_%x\s*\()' % a, nt + r'\1', head)
        body = text[i:]
        body = (TAIL if TAIL.search(body) else TAILPTR).sub(lambda mm: '\n%sreturn (%s)%s; /* tail call: value left in %s (gen.py) */\n}\n' % (mm.group(1), nt, mm.group(2), 'XMM0' if nt == 'double' else 'RAX'), body)
        funcs[a] = head + body; upgraded[a] = nt; changed = True
    if not changed: break

# retype.txt (gen/retype.py): "<func> <var> double" / "<func> <var> double *" / "<func> deref <snippet>"
RETYPE = {}
rtf = os.path.join(HERE, 'retype.txt')
if os.path.exists(rtf):
    for l in open(rtf):
        if l.startswith('#'): continue
        q = l.rstrip('\n').split(' ', 2)
        if len(q) == 3: RETYPE.setdefault(int(q[0], 16), []).append((q[1], q[2]))
def apply_retype(a, t):
    for n, ty in RETYPE.get(a, []):
        if n == 'deref':
            snip = ty.replace('\\x20', ' ')
            t = t.replace(snip, '*(double *)' + snip[len('*(undefined8 *)'):])
        elif ty == 'double':
            t = re.sub(r'(?m)(^[ \t]*|[(,]\s*)(?:undefined8|ulonglong)(\s+%s\s*[;,)\[])' % n, r'\1double\2', t)
        elif n == 'store':
            lv = ty.replace('\\x20', ' ')
            # not for raw copies "x[k] = *p;" / "x[k] = y[j];" (undefined8 to undefined8)
            t = re.sub(re.escape(lv) + r'(\s*=(?!=)(?!\s*\*?\w+\s*;)(?!\s*\w+\[[^\]]*\]\s*;))', '*(double *)&' + lv + r'\1', t)
        elif ty == 'double *':
            t = re.sub(r'(?m)(^[ \t]*|[(,]\s*)(?:undefined8|ulonglong) \*(\s*%s\s*[;,)\[])' % n, r'\1double *\2', t)
    return t
for a_ in list(RETYPE):
    if a_ in funcs: funcs[a_] = apply_retype(a_, funcs[a_])

want = []
for l in open(FLIST):
    l = l.split('#')[0].strip()
    if l: want.append(int(l, 16))
want = sorted(set(want))

def signature(text):
    """the prototype text before the opening brace"""
    i = text.find('\n{\n')
    head = re.sub(r'/\*.*?\*/', '', text[:i], flags=re.S)
    lines = [x for x in head.split('\n') if x.strip() and not x.strip().startswith('/*') and not x.strip().startswith('*') and not x.rstrip().endswith('*/')]
    # signature = last statement-like lines
    sig = ' '.join(x.strip() for x in lines)
    # drop comment blocks that might be inside
    sig = re.sub(r'/\*.*?\*/', '', sig).strip()
    return sig

TYPEFIX = [
    (r'\b__thiscall\b|\b__cdecl\b|\b__fastcall\b|\b__stdcall\b', ''),
    (r'\bunsigned long\b', 'ts_ulong'),
    (r'\bulong\b', 'ts_ulong'),
    (r'(?<![\w])long(?![\w])', 'ts_long'),
    (r'\bFILE\b', 'TSFILE'),
    (r'\bCHAR\b', 'char'),
    (r'\bwchar_t\b', 'wchar16'),
    (r'\bFUN_[0-9a-f]{9}(\s*\*)', r'code\1'),       # pointer-to-function-definition types (fntable.txt slots)
    (r'\bclock_t\b', 'ts_clock_t'),
    (r'___ptr64', ''),
    (r'\b([A-Za-z_]\w*)<([A-Za-z_][\w* ]*)>(?=\s*[\*\)>,&])', r'\1_T'),
    (r'\bbool\b', 'bool_t'),
    (r'\btrue\b', '1'), (r'\bfalse\b', '0'),
]
STRLIT = re.compile(r'"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'')
def code_only(fn, s):
    """apply fn to the text outside string / char literals"""
    out = []; pos = 0
    for m in STRLIT.finditer(s):
        out.append(fn(s[pos:m.start()])); out.append(m.group(0)); pos = m.end()
    out.append(fn(s[pos:]))
    return ''.join(out)
def typefix(s):
    def f(x):
        for a, b in TYPEFIX: x = re.sub(a, b, x)
        return x
    return code_only(f, s)

PART = re.compile(r'([A-Za-z_]\w*(?:\[[^\[\]]*\])?)\._(\d+)_(\d+)_')
PTYPE = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t', 8: 'uint64_t', 3: 'uint32_t', 5: 'uint64_t', 6: 'uint64_t', 7: 'uint64_t'}
def partfix(s):
    return PART.sub(lambda m: '(*(%s *)((char *)&%s + %s))' % (PTYPE.get(int(m.group(3)), 'uint64_t'), m.group(1), m.group(2)), s)

# ---- globals
gtypes = {}     # (func, name) -> type
for l in open(os.path.join(EXP, 'globals.tsv')):
    p = l.rstrip('\n').split('\t')
    if len(p) >= 5 and p[2] != '?': gtypes[(int(p[0], 16), p[1])] = p[3]
GNAME = re.compile(r'\b(_?DAT_([0-9a-f]{8,9})|PTR_(?:\w*?_)?([0-9a-f]{9})|[su]_\w*?_([0-9a-f]{9}))\b')
VFT = re.compile(r'\b([A-Za-z_]\w*)::vftable\b')
vfaddr = {}
for l in open(REFIDX.replace('n64dll_index.txt', 'n64dll_syms.txt')):
    m = re.match(r'([0-9a-f]{9})\t(\w+)::vftable$', l.rstrip('\n'))
    if m: vfaddr[m.group(2)] = int(m.group(1), 16)
data_addrs = {}

def ctype_decl(t):
    """global type string -> type usable in TS_G(type, addr)"""
    t = typefix(t)
    m = re.match(r'(.*)\[(\d+)\]$', t)
    if m: return '%s(*)[%s]' % (m.group(1), m.group(2)), True
    return t, False

used_globals = {}
called = {}
CALL = re.compile(r'\bFUN_([0-9a-f]{9})\b')

def gaddr(g):
    return int(re.search(r'([0-9a-f]{8,9})$', g).group(1), 16)

def gdefine(fa, g):
    """#define for global name g as used in function fa"""
    addr = gaddr(g); data_addrs[addr] = True
    ty = gtypes.get((fa, g))
    if g.startswith('s_') or g.startswith('u_'):
        return '#define %s (*(char (*)[1])(ts_img + (0x%xULL - TS_IMG_LO)))' % (g, addr)
    if (ty is None or ty == '?') and g.startswith('PTR_'):
        # no type in globals.tsv: Ghidra's PTR_ label = a pointer in the slot (the export declares it "undefined *",
        # e.g. the error table's message pointers (&PTR_DAT_1802932f8)[i * 0x23], FUN_1800536a0 L42188)
        return '#define %s TS_G(undefined *, 0x%xULL)' % (g, addr)
    if ty is None or ty == '?':
        return '#define %s TS_G(undefined, 0x%xULL)' % (g, addr)
    ct, arr = ctype_decl(ty)
    ct = re.sub(r'\b([A-Za-z_]\w*)<[^<>]*>', r'\1_T', ct)
    if arr: return '#define %s (*(%s)(ts_img + (0x%xULL - TS_IMG_LO)))' % (g, ct, addr)
    return '#define %s TS_G(%s, 0x%xULL)' % (g, ct, addr)

DECL = re.compile(r'^(\s*)([A-Za-z_][\w\s\*]*?)\s*\b((?:local|[a-zA-Z]*Stack)_([0-9a-f]+))\s*(\[[0-9]+\]\s*)*;\s*$')
def stack_frame(addr, t):
    """Stack locals at their MSVC frame offsets: the decompile names them by offset (local_c88 = [entry RSP - 0xc88])
    and some code depends on the layout (out-of-bounds reads of a neighbour, e.g. FUN_180203630's local_c88[-1]
    on an empty value = the last byte of local_1088), so every local_<hex> / *Stack_<hex> lives in one byte array laid
    out as in the original frame (zeroed: the original's never-written bytes are stack garbage)."""
    i = t.find('\n{\n')
    if i < 0: return t
    m_ = re.compile(r'\n[ \t]*\n').search(t, i + 2)
    if not m_: return t
    j = m_.start()
    decls = t[i + 3:j].split('\n'); keep = []; vars_ = []
    for d in decls:
        m = DECL.match(d)
        if m and '(' not in m.group(2):
            ty = m.group(2).strip(); nm = m.group(3); off = int(m.group(4), 16)
            dims = re.findall(r'\[([0-9]+)\]', d[m.end(3):])
            vars_.append((nm, ty, off, dims))
        else:
            keep.append(d)
    if not vars_: return t
    fs = max(off for _, _, off, _ in vars_)
    fs = (fs + 15) & ~15
    defs = []
    for nm, ty, off, dims in vars_:
        if dims:
            arr = ''.join('[%s]' % x for x in dims)
            defs.append('#define %s (*(%s (*)%s)(ts_frame + (0x%x - 0x%x)))' % (nm, ty, arr, fs, off))
        else:
            defs.append('#define %s (*(%s *)(ts_frame + (0x%x - 0x%x)))' % (nm, ty, fs, off))
    undefs = ['#undef %s' % nm for nm, _, _, _ in vars_]
    body = t[:i + 3] + '  unsigned char ts_frame[0x%x] __attribute__((aligned(16)));\n  memset(ts_frame, 0, sizeof ts_frame);\n' % fs + '\n'.join(keep) + t[j:]
    k = body.rfind('\n}')
    return '\n'.join(defs) + '\n' + body[:k + 2] + '\n' + '\n'.join(undefs) + body[k + 2:]

def bitcasts(addr, t):
    """Ghidra's "undefined8" / "undefined4" carry raw bits: a (double) / (float) cast of such a value is a bit
    reinterpretation (movq / movd between registers), not C's numeric conversion; and (undefinedN) of a double the
    reverse.  Names typed in the function (params + locals), globals, and calls by their prototype's return type."""
    i = t.find('\n{\n')
    if i < 0: return t
    head = t[:i]; decl_end = re.compile(r'\n[ \t]*\n').search(t, i + 2)
    decls = t[i:decl_end.start()] if decl_end else ''
    ty = {}
    for m in re.finditer(r'\b(undefined8|undefined4|double|float|ulonglong|longlong)\s+(\w+)\s*[,);]', head + decls): ty[m.group(2)] = m.group(1)
    for m in re.finditer(r'\b(undefined8|undefined4|double|float|ulonglong|longlong)\s+(\w+)\s*\[', head + decls): ty[m.group(2) + '[]'] = m.group(1)
    PTRTY.clear()
    for m in re.finditer(r'\b(undefined8|ulonglong)\s*\*\s*(\w+)\s*[,);]', head + decls): PTRTY[m.group(2)] = m.group(1) + ' *'
    def isu8(n): return ty.get(n) == 'undefined8' or (n.startswith(('DAT_', '_DAT_')) and gtypes.get((addr, n)) == 'undefined8')
    def isu4(n): return ty.get(n) == 'undefined4' or (n.startswith(('DAT_', '_DAT_')) and gtypes.get((addr, n)) == 'undefined4')
    def isd(n): return ty.get(n) == 'double' or (n.startswith(('DAT_', '_DAT_')) and gtypes.get((addr, n)) == 'double')
    def f(x):
        x = re.sub(r'\(double\)\s*(\w+)\b(?!\s*[\[(.]|\s*->)', lambda m: 'ts_bd(%s)' % m.group(1) if isu8(m.group(1)) else m.group(0), x)
        x = re.sub(r'\(float\)\s*(\w+)\b(?!\s*[\[(.]|\s*->)', lambda m: 'ts_bf(%s)' % m.group(1) if isu4(m.group(1)) else m.group(0), x)
        x = re.sub(r'\(undefined8\)\s*(\w+)\b(?!\s*[\[(.]|\s*->)', lambda m: 'ts_db(%s)' % m.group(1) if isd(m.group(1)) else m.group(0), x)
        x = re.sub(r'\(double\)\s*\*\(undefined8 \*\)', '*(double *)', x)
        # (double)p[k] / (double)*p of an undefined8 pointer or array: bit copy
        def elem(m):
            n = m.group(2)
            if PTRTY.get(n) in ('undefined8 *', 'ulonglong *') or ty.get(n + '[]') in ('undefined8', 'ulonglong'):
                return 'ts_bd(%s%s%s)' % (m.group(1), n, m.group(3) or '')
            return m.group(0)
        x = re.sub(r'\(double\)\s*(\*?)(\w+)(\[[^\[\]]*\])?(?![\w\[(])', lambda m: elem(m) if (m.group(1) or m.group(3)) else m.group(0), x)
        return x
    t = code_only(f, t)
    # a stack slot Ghidra typed as a pointer but also used for a double (slot reuse): bit copies both ways
    ptrnames = set(m.group(1) for m in re.finditer(r'\b[A-Za-z_]\w*\s*\*+\s*(\w+)\s*[;,)]', head + decls))
    t = re.sub(r'\(double\)(\w+)\b(?![\[(.]|->)', lambda m: 'ts_bd((uint64_t)%s)' % m.group(1) if m.group(1) in ptrnames else m.group(0), t)
    def pdbl(m):
        a = int(m.group(3), 16)
        r = upgraded.get(a) or CRT_RET.get(a) or (sig_ret(funcs[a]) if a in funcs else None)
        if r != 'double': return m.group(0)
        e, _ = call_args(t, m.end() - 1)
        return None
    out = []; pos = 0
    for m in re.finditer(r'(\w+) = \(([\w ]+\*+)\)FUN_([0-9a-f]{9})\(', t):
        a = int(m.group(3), 16)
        r = upgraded.get(a) or CRT_RET.get(a) or (sig_ret(funcs[a]) if a in funcs else None)
        if r != 'double' or m.start() < pos: continue
        e, _ = call_args(t, m.end() - 1)
        out.append(t[pos:m.start()]); out.append('%s = (%s)ts_dbits(%s)' % (m.group(1), m.group(2), t[m.start() + len(m.group(0)) - len('FUN_%s(' % m.group(3)):e])); pos = e
    out.append(t[pos:])
    t = ''.join(out)
    # "(T *)(<double expression>)": a double stored into a pointer-typed slot (stack argument / reused slot): bit copy
    ppnames = set(m.group(1) for m in re.finditer(r'\b[A-Za-z_]\w*\s*\*\*+\s*(\w+)\s*[;,)]', head + decls))
    t = re.sub(r'\(double\)(\w+)(\[[^\[\]]*\])', lambda m: 'ts_bd((uint64_t)%s%s)' % (m.group(1), m.group(2)) if m.group(1) in ppnames else m.group(0), t)
    out = []; pos = 0
    for m in re.finditer(r'=\s*\((?:undefined|[\w ]+?) \*+\)\(', t):
        if m.start() < pos: continue
        e, _ = call_args(t, m.end() - 1)
        inner = t[m.end():e - 1]
        if t[e:e + 1] == ';' and re.search(r'\d\.\d|\(double \*\)|\(double\)|ts_bd\(', inner):
            out.append(t[pos:m.end() - 1]); out.append('ts_dbits(' + inner + ')'); pos = e
    out.append(t[pos:])
    t = ''.join(out)
    # (double)FUN_x(...) with an undefined8 / ulonglong return: the value came back in XMM0 as raw bits
    out = []; pos = 0
    for m in re.finditer(r'\(double\)\s*FUN_([0-9a-f]{9})\(', t):
        a = int(m.group(1), 16)
        r = upgraded.get(a) or (sig_ret(funcs[a]) if a in funcs else None)
        if r not in ('undefined8', 'ulonglong', 'uint64_t'): continue
        e, _ = call_args(t, m.end() - 1)
        out.append(t[pos:m.start()]); out.append('ts_bd(' + t[m.start() + len('(double)'):e].lstrip() + ')'); pos = e
        hazards.append('FUN_%x: (double) of FUN_%x (%s) taken as a bit copy' % (addr, a, r))
    out.append(t[pos:])
    t = ''.join(out)
    # double values into undefined8 slots: call arguments for undefined8 parameters, stores through
    # *(undefined8 *), and undefined8 call results into double variables - all bit copies in the DLL
    def is_dexpr(e):
        e = e.strip()
        return isd(e) or e.startswith('*(double *)') or bool(re.match(r'^\w+\[[^\]]*\]$', e) and ty.get(e.split('[')[0]) == 'double *')
    out = []; pos = 0
    for m in re.finditer(r'\bFUN_([0-9a-f]{9})\(', t):
        a = int(m.group(1), 16)
        if m.start() < pos or a not in PARAMTYPES: continue
        if a in vaargs.TARGETS: continue        # variadic (gen/vaargs.py): the arguments keep their C types
        e, args = call_args(t, m.end() - 1)
        pts = PARAMTYPES[a]; changed = False; na = []
        for k, arg in enumerate(args):
            if k < len(pts) and pts[k] in ('undefined8', 'ulonglong') and is_dexpr(arg):
                na.append('ts_db(%s)' % arg.strip()); changed = True
            elif k < len(pts) and pts[k] == 'double' and (ty.get(arg.strip()) == 'undefined8' or (arg.strip().startswith(('DAT_', '_DAT_')) and gtypes.get((addr, arg.strip())) == 'undefined8')):
                # an undefined8 holding a double's bits passed to a double parameter (FUN_1800fc150 param_11 ->
                # FUN_1800fc420 param_10: asm movsd [rsp+..] -> stack slot, no conversion): bit copy
                na.append('ts_bd(%s)' % arg.strip()); changed = True
            else: na.append(arg)
        if changed:
            out.append(t[pos:m.end()]); out.append(','.join(na) + ')'); pos = e
    out.append(t[pos:])
    t = ''.join(out)
    t = re.sub(r'(?m)^(\s*)\*\(undefined8 \*\)(\(.*\)) = (\w+);$', lambda m: '%s*(double *)%s = %s;' % (m.group(1), m.group(2), m.group(3)) if isd(m.group(3)) else m.group(0), t)
    # register variables typed undefined8 receiving a double (call result or double variable): bit copy
    def rdb(m):
        n, rhs = m.group(2), m.group(3)
        if ty.get(n) not in ('undefined8', 'ulonglong'): return m.group(0)
        mm = re.match(r'FUN_([0-9a-f]{9})\(', rhs)
        if mm:
            a = int(mm.group(1), 16)
            r = upgraded.get(a) or CRT_RET.get(a) or (sig_ret(funcs[a]) if a in funcs else None)
            if r == 'double': return '%s%s = ts_db(%s);' % (m.group(1), n, rhs)
        elif isd(rhs): return '%s%s = ts_db(%s);' % (m.group(1), n, rhs)
        return m.group(0)
    t = re.sub(r'(?m)^(\s*)(\w+) = (FUN_[0-9a-f]{9}\(.*\)|\w+);$', rdb, t)
    # the same over several lines (FUN_180196e80: "uVar1 = FUN_1801d9d80(<expression wrapped by Ghidra>);")
    out = []; pos = 0
    for m in re.finditer(r'(?m)^(\s*)(\w+) = FUN_([0-9a-f]{9})\(', t):
        if m.start() < pos or ty.get(m.group(2)) not in ('undefined8', 'ulonglong'): continue
        a = int(m.group(3), 16)
        r = upgraded.get(a) or CRT_RET.get(a) or (sig_ret(funcs[a]) if a in funcs else None)
        if r != 'double': continue
        e, _ = call_args(t, m.end() - 1)
        if '\n' not in t[m.end():e] or t[e:e + 1] != ';': continue
        k = m.start(2) + len(m.group(2)) + 3
        out.append(t[pos:k]); out.append('ts_db(' + t[k:e] + ')'); pos = e
    out.append(t[pos:])
    t = ''.join(out)
    t = printf_args(addr, t, ty, isd)
    # undefined8 globals / pointer targets receiving a double: bit copy
    t = re.sub(r'(?m)^(\s*)(_?DAT_[0-9a-f]+) = (\w+);$', lambda m: '%s%s = ts_db(%s);' % (m.group(1), m.group(2), m.group(3)) if gtypes.get((addr, m.group(2))) in ('undefined8', 'ulonglong') and isd(m.group(3)) else m.group(0), t)
    def pstore(m):
        n, rhs = m.group(2), m.group(3)
        pty = ty.get(n) or PTRTY.get(n)
        if pty in ('undefined8 *', 'ulonglong *') and (rhs.startswith('*(double *)') or isd(rhs)):
            return '%s*(double *)%s = %s;' % (m.group(1), n, rhs)
        return m.group(0)
    t = re.sub(r'(?m)^(\s*)\*(\w+) = (\*\(double \*\)\([^;]*\)|\w+);$', pstore, t)
    # p[k] = d; with p an undefined8 pointer and d a double variable: bit copy (movsd store)
    t = re.sub(r'(?m)^(\s*)(\w+)(\[[^\]\n]*\]) = (\w+);$', lambda m: '%s%s%s = ts_db(%s);' % (m.group(1), m.group(2), m.group(3), m.group(4)) if (ty.get(m.group(2)) or PTRTY.get(m.group(2))) in ('undefined8 *', 'ulonglong *') and isd(m.group(4)) else m.group(0), t)
    # p[k] = q[j]; with p an undefined8 pointer and q a double pointer / array: bit copy (FUN_180140140
    # "local_res20[1] = param_2[0x10c]": asm 0x180140246 movsd load / movsd store of the angle)
    dptrs = set(m.group(1) for m in re.finditer(r'\bdouble\s*\*\s*(\w+)\s*[,);]', head + decls))
    t = re.sub(r'(?m)^(\s*)(\w+)(\[[^\]\n]*\]) = (\w+)(\[[^\]\n]*\]);$', lambda m: '%s%s%s = ts_db(%s%s);' % (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)) if (ty.get(m.group(2)) or PTRTY.get(m.group(2))) in ('undefined8 *', 'ulonglong *') and (m.group(4) in dptrs or ty.get(m.group(4) + '[]') == 'double') else m.group(0), t)
    out = []; pos = 0
    for m in re.finditer(r'(?m)^(\s*)(\w+) = FUN_([0-9a-f]{9})\(', t):
        a = int(m.group(3), 16)
        r = upgraded.get(a) or (sig_ret(funcs[a]) if a in funcs else None)
        if not (isd(m.group(2)) and r in ('undefined8', 'ulonglong')): continue
        e, _ = call_args(t, m.end() - 1)
        k = m.start(2) + len(m.group(2)) + 3
        out.append(t[pos:k]); out.append('ts_bd(' + t[k:e] + ')'); pos = e
    out.append(t[pos:])
    t = ''.join(out)
    return t

PARAMTYPES = {}
PTRTY = {}
# cvt64.txt: "<func> <count of 64-bit cvttsd2si>" [bits <var>...]: in these functions a (longlong) cast of a double is
# kept as a conversion when the reference has it or it is narrowed at once; any other one stops the generation unless
# listed after "bits" (checked in the asm to be a raw 8-byte copy)
CVT64 = {}; CVT64_BITS = set()
for l in open(os.path.join(HERE, 'cvt64.txt')):
    l = l.split('#')[0].split()
    if not l: continue
    CVT64[int(l[0], 16)] = int(l[1])
    if len(l) > 2 and l[2] == 'bits': CVT64_BITS |= set((int(l[0], 16), v) for v in l[3:])
PRINTF = {0x180225b04: 1, 0x1802259c0: 1}    # fprintf / sprintf: index of the format argument
def printf_args(addr, t, ty, isd):
    """variadic printf arguments for %f conversions: a raw 8-byte value (undefined8) holding a double is passed
    in a general register by the decompile's C, but the DLL passed the double (the callee reads it with va_arg
    double): bit copy to double."""
    out = []; pos = 0
    for m in re.finditer(r'\bFUN_(180225b04|1802259c0)\(', t):
        if m.start() < pos: continue
        e, args = call_args(t, m.end() - 1)
        fi = PRINTF[int(m.group(1), 16)]
        if len(args) <= fi: continue
        fm = re.match(r'\s*"((?:[^"\\]|\\.)*)"\s*$', args[fi])
        if not fm: continue
        convs = re.findall(r'%[-+ #0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:l|ll|h|I64)?([diouxXcsfeEgGp%])', fm.group(1))
        convs = [c for c in convs if c != '%']
        na = list(args); changed = False
        for k, c in enumerate(convs):
            j = fi + 1 + k
            if j >= len(na) or c not in 'feEgG': continue
            a = na[j].strip()
            if isd(a) or a.startswith('*(double *)') or a.startswith('(double)'): continue
            am = re.match(r'^(\w+)\[[^\]]*\]$', a)
            if ty.get(a) in ('undefined8', 'ulonglong', 'longlong') or a.startswith('*(undefined8 *)') or (am and ty.get(am.group(1) + '[]') in ('undefined8', 'ulonglong')) or (a.startswith(('DAT_', '_DAT_')) and gtypes.get((addr, a)) in ('undefined8',)):
                na[j] = 'ts_bd(%s)' % a; changed = True
        if changed:
            out.append(t[pos:m.end()]); out.append(','.join(na) + ')'); pos = e
    out.append(t[pos:])
    return ''.join(out)
def build_paramtypes():
    for a, text in funcs.items():
        sg = re.sub(r'/\*.*?\*/', '', text[:text.find('\n{\n')], flags=re.S)
        m = re.search(r'\bFUN_[0-9a-f]{9}\s*\((.*)\)\s*$', sg.strip(), re.S)
        if not m: continue
        ps = [x.strip() for x in m.group(1).split(',')]
        PARAMTYPES[a] = [re.sub(r'\s*\b\w+$', '', x).strip() for x in ps]

def longlong_elem_as_double(addr, t):
    """(double)p[k] with p a longlong pointer parameter / local: after the parameter commits Ghidra types a double array
    as longlong * (FUN_18004ecd0: the reference decompile passes param_1[1] to FUN_18003da80(double, ..) without a
    conversion, the asm loads it with movsd); unless the reference has the same conversion it is a bit load."""
    if addr not in refL: return t
    ref = reftext(addr)
    i = t.find('\n{\n')
    if i < 0: return t
    m_ = re.compile(r'\n[ \t]*\n').search(t, i + 2)
    llp = set(m.group(1) for m in re.finditer(r'\blonglong\s*\*\s*(\w+)\s*[,);]', t[:m_.end() if m_ else i]))
    def f(m):
        if m.group(1) not in llp or m.group(0) in ref: return m.group(0)
        hazards.append('FUN_%x: %s taken as a bit load (reference: no conversion)' % (addr, m.group(0)))
        return 'ts_bd(%s[%s])' % (m.group(1), m.group(2))
    return re.sub(r'\(double\)(\w+)\[([^\[\]]*)\]', f, t)

def ptr_as_double(addr, t):
    """(longlong)*(double *)(X): after the parameter commits Ghidra sometimes types a pointer slot as double (the
    reference decompile n64dll_all.c, made without commits, reads the same slot as *(longlong *)); unless the
    reference has the same conversion, it is a pointer load, not a cvttsd2si."""
    if addr not in refL: return t
    ref = reftext(addr)
    def f(m):
        e, _ = call_args(t, m.end() - 1)
        whole = t[m.start():e]
        if whole in ref: return None
        return (m.start(), e, '*(%s *)' % m.group(1) + t[m.end() - 1:e])
    out = []; pos = 0
    for m in re.finditer(r'\((longlong|ulonglong)\)\*\(double \*\)\(', t):
        if m.start() < pos: continue
        r = f(m)
        if r is None: continue
        out.append(t[pos:r[0]]); out.append(r[2]); pos = r[1]
        hazards.append('FUN_%x: (%s)*(double *) taken as a pointer load (reference: no conversion)' % (addr, m.group(1)))
    out.append(t[pos:])
    t = ''.join(out)
    # (longlong)d / (ulonglong)d of a double variable: a raw store of the double's bytes into an integer-typed
    # slot (movsd [mem],xmm) unless the reference decompile has the same conversion (then it is cvttsd2si)
    dnames = set(m.group(2) for m in re.finditer(r'\b(double)\s+(\w+)\s*[,);]', t))
    def ll(m):
        if m.group(2) not in dnames: return m.group(0)
        if addr in CVT64 and m.group(0) in ref: return m.group(0)   # may be a real cvttsd2si: as the reference
        # narrowed at once ((short)(longlong)d): the value is used as a number - a cvttsd2si (FUN_1800eeb00
        # 0x1800ef2b2.., the reference decompile names the variables differently)
        if addr in CVT64 and re.search(r'\((?:short|ushort|char|uchar|byte|int|uint)\)\s*$', t[max(0, m.start() - 12):m.start()]): return m.group(0)
        hazards.append('FUN_%x: %s taken as a bit copy (not in the reference decompile)' % (addr, m.group(0)))
        if addr in CVT64 and (addr, m.group(2)) not in CVT64_BITS: CVT_ERR.append('FUN_%x: %s' % (addr, m.group(0)))
        return '(%s)ts_dbits(%s)' % (m.group(1), m.group(2))
    t = re.sub(r'\((longlong|ulonglong)\)(\w+)\b(?![\[(])', ll, t)
    # (longlong)p[k] with p a double pointer: an 8-byte load of a slot Ghidra typed double (e.g. a paged-array page
    # pointer, FUN_1800c1080 local_2600[2]) - a bit load unless the function has a 64-bit cvttsd2si (cvt64.txt)
    dptr = set(m.group(1) for m in re.finditer(r'\bdouble\s*\*\s*(\w+)\s*[,);]', t[:t.find('\n{\n') + 2] + t[t.find('\n{\n'):]))
    def llp(m):
        if m.group(2) not in dptr or (addr in CVT64 and m.group(0) in ref): return m.group(0)
        hazards.append('FUN_%x: (%s)%s[..] taken as a bit load' % (addr, m.group(1), m.group(2)))
        if addr in CVT64 and (addr, m.group(2)) not in CVT64_BITS: CVT_ERR.append('FUN_%x: %s..]' % (addr, m.group(0)))
        return '*(%s *)&%s[' % (m.group(1), m.group(2))
    t = re.sub(r'\((longlong|ulonglong)\)(\w+)\[', llp, t)
    return t
CVT_ERR = []

def home_slots(addr, t):
    """local_res<off>: the decompile reads a register parameter back from its home slot [entry RSP + off]; the
    unoptimised code spills every register parameter there first (e.g. FUN_1800979f0 asm 0x1800979f0..0x180097a00
    movsd [rsp+0x8..0x20],xmm0..3), so the slot holds parameter off/8 at entry (stack parameters for off >= 0x28)."""
    i = t.find('\n{\n')
    if i < 0: return t
    m_ = re.compile(r'\n[ \t]*\n').search(t, i + 2)
    if not m_: return t
    decls = t[i:m_.start()]
    inits = []
    for m in re.finditer(r'\b(local_res([0-9a-f]+))\s*(\[[^\]]*\])?\s*;', decls):
        off = int(m.group(2), 16)
        if off % 8 or m.group(3): continue
        pn = 'param_%d' % (off // 8)
        if not re.search(r'\b%s\b' % pn, t[:i]): continue
        inits.append('  memcpy(&%s, &%s, sizeof %s < sizeof %s ? sizeof %s : sizeof %s);   /* home slot +0x%x (gen.py) */'
                     % (m.group(1), pn, m.group(1), pn, m.group(1), pn, off))
    if not inits: return t
    return t[:m_.start()] + '\n' + '\n'.join(inits) + t[m_.start():]

def fixbody(addr, text):
    def vft(m):
        a = vfaddr.get(m.group(1))
        if a is None: return 'TS_VFTABLE_UNKNOWN_' + m.group(1)
        data_addrs[a] = True
        return '((void *)(ts_img + (0x%xULL - TS_IMG_LO)))' % a
    text = VFT.sub(vft, text)
    text = re.sub(r'\bthunk_FUN_', 'FUN_', text)
    t = typefix(text)
    t = re.sub(r'undefined1 (auVar\d+) \[16\];', r'unsigned __int128 \1;', t)
    t = partfix(t)
    t = vaargs.fix(addr, t, PE, ins_of, call_args, VA_ERR)   # the error builders' message arguments (gen/vaargs.py)
    t = re.sub(r'\(\*(_?DAT_[0-9a-f]+|PTR_\w+)\)\(', r'((code *)\1)(', t)
    # absolute addresses of static data printed as integers (e.g. the NipReader object 0x1804f1120): into ts_img
    def absaddr(m):
        v = int(m.group(1), 16)
        if 0x180261000 <= v < 0x180508000:
            data_addrs[v] = True
            return '((uint64_t)(ts_img + (0x%xULL - TS_IMG_LO)))' % v
        return m.group(0)
    t = re.sub(r'(?<![\w.])0x(18[0-9a-f]{7})\b', absaddr, t)
    # a double global copied into a pointer-typed stack slot (stack argument): bit copy
    t = re.sub(r'\(undefined \*\)(_?DAT_[0-9a-f]+)\b', lambda m: '(undefined *)ts_dbits(%s)' % m.group(1) if gtypes.get((addr, m.group(1))) == 'double' else m.group(0), t)
    names = sorted(set(m.group(1) for m in GNAME.finditer(t)))
    if names:
        defs = '\n'.join(gdefine(addr, g) for g in names)
        undefs = '\n'.join('#undef %s' % g for g in names)
        i = t.find('\n{\n')
        t = defs + '\n' + t.rstrip() + '\n' + undefs + '\n'
    for g in names: used_globals[g] = True
    for m in CALL.finditer(t):
        a = int(m.group(1), 16)
        if a != addr: called[a] = True
    # throw
    t = re.sub(r'\bFUN_180227464\(', 'ts_cxx_throw(', t)
    t = widen_extraout_var(addr, t)
    t = bind_extraout(t)
    t = check_arity(addr, t)
    t = ptr_as_double(addr, t)
    t = longlong_elem_as_double(addr, t)
    t = bitcasts(addr, t)
    t = stack_frame(addr, t)
    t = home_slots(addr, t)
    # registers / stack slots the caller did not set (decompiler "in_" inputs): 0, and listed in ts_gen.h
    def inz(m):
        hazards.append('FUN_%x: %s' % (addr, m.group(2)))
        return '%s%s = 0;' % (m.group(1), m.group(2))
    t = re.sub(r'(?m)^(\s+[\w\s\*]+?\b)(in_[A-Za-z0-9_]+|unaff_[A-Za-z0-9_]+)\s*;', inz, t)
    t = re.sub(r'&stack0x[0-9a-f]+', '(ts_bad(0x%xULL, "address of an incoming stack slot"), (void *)0)' % addr, t)
    return t

hazards = []
CRT_ARITY = {}
CRTSIGS = os.path.join(HERE, 'crtsigs.txt')
for l in open(CRTSIGS):
    l = l.split('#')[0].split()
    if len(l) >= 2 and '+' not in l: CRT_ARITY[int(l[0], 16)] = len([x for x in l[2:] if x != '!'])

def call_args(t, i):
    """t[i] == '(' -> (end index after ')', list of argument strings)"""
    depth = 0; args = []; cur = ''; j = i
    while j < len(t):
        c = t[j]
        if c == '(':
            depth += 1
            if depth > 1: cur += c
        elif c == ')':
            depth -= 1
            if depth == 0:
                if cur.strip() or args: args.append(cur)
                return j + 1, args
            cur += c
        elif c == ',' and depth == 1: args.append(cur); cur = ''
        elif c == '"':
            k = j + 1
            while t[k] != '"': k += 2 if t[k] == '\\' else 1
            cur += t[j:k + 1]; j = k
        else: cur += c
        j += 1
    return j, args

def check_arity(fa, t):
    out = []; pos = 0
    for m in re.finditer(r'\bFUN_([0-9a-f]{9})\(', t):
        a = int(m.group(1), 16)
        if a not in CRT_ARITY or m.start() < pos: continue
        e, args = call_args(t, m.end() - 1)
        if len(args) != CRT_ARITY[a]:
            out.append(t[pos:m.start()])
            out.append('(ts_bad(0x%xULL, "call FUN_%x with %d arguments (decompile lost some)"), 0)' % (fa, a, len(args)))
            hazards.append('FUN_%x: FUN_%x called with %d arguments' % (fa, a, len(args)))
            pos = e
    out.append(t[pos:])
    return ''.join(out)
def widen_extraout_var(addr, t):
    """CONCATxy(extraout_var, v): the full RAX after the call that set v (the decompiler kept only v's low bytes,
    e.g. a pointer returned by the allocation callback in FUN_1800e6960): v becomes 64-bit and the CONCAT is v."""
    names = set(re.findall(r'CONCAT\d\d\(extraout_var\w*,(\w+)\)', t))
    for v in names:
        t = re.sub(r'CONCAT\d\d\(extraout_var\w*,%s\)' % v, '((uint64_t)%s)' % v, t)
        t = re.sub(r'(?m)^(\s*)(?:undefined|undefined1|undefined2|undefined4|uint|int|char|byte|bool_t|bool|ushort|short)\s+%s;' % v, r'\1uint64_t %s;' % v, t)
        hazards.append('FUN_%x: %s widened to the full call result (CONCAT with extraout_var)' % (addr, v))
    return t

def bind_extraout(t):
    """A use of extraout_RAX / _EAX / _XMM0_Qa: the decompiler did not connect a call's return register to the call
    (callee typed void).  Bind it to the nearest preceding call when that line is a plain call statement (the register
    is read after that call in the asm); otherwise zero it and list it as a hazard.  extraout_var* (upper bytes of RAX
    after a narrower return) are zeroed."""
    lines = t.split('\n'); bound = set()
    for i, l in enumerate(lines):
        for m in re.finditer(r'\bextraout_\w+', l):
            v = m.group(0)
            if re.match(r'^\s*[\w\s\*]+\b' + re.escape(v) + r'\s*;', l): continue
            if v.startswith('extraout_var'): continue
            ok = False
            for j in range(i - 1, -1, -1):
                if (v + ' = (__typeof__') in lines[j]: ok = True; break   # already bound to the call before this use
                st = re.match(r'^(\s*)(FUN_[0-9a-f]+\(|\(\*\w+\)\(|\(\(code \*\)\w+\)\()', lines[j])
                if st:
                    k = j
                    while k < i and not lines[k].rstrip().endswith(');'): k += 1
                    if k < i:
                        call = lines[j][len(st.group(1)):]
                        if v.startswith('extraout_XMM0'):
                            # a double comes back in XMM0: call through a double-returning pointer type
                            call = re.sub(r'^\(\*(\w+)\)\(|^\(\(code \*\)(\w+)\)\(', lambda mm: '((double (*)())%s)(' % (mm.group(1) or mm.group(2)), call)
                        lines[j] = '%s%s = (__typeof__(%s))(%s' % (st.group(1), v, v, call)
                        lines[k] = lines[k].rstrip()[:-1] + ');'; ok = True
                    break
                if re.search(r'\b(FUN_[0-9a-f]+|ts_\w+)\s*\(|\(\*', lines[j]): break
            bound.add(v)
            if not ok: hazards.append('unbound %s (zeroed)' % v)
    t = '\n'.join(lines)
    t = re.sub(r'(?m)^(\s+[\w\s\*]+?\b)(extraout_[A-Za-z0-9_]+)\s*;', r'\1\2 = 0;', t)
    return t

build_paramtypes()
out_funcs = []
for a in want:
    fx = os.path.join(FIXDIR, 'FUN_%x.c' % a)
    if os.path.exists(fx):
        text = open(fx).read()
        src = 'hand fix viewer/nest/ts/fix/FUN_%x.c' % a
    elif a in funcs:
        text = funcs[a]; src = None
    elif a in refL:
        text = reftext(a); src = 'reference dump text (not in the export range)'
        funcs[a] = text
    else:
        print('no decompile for %x' % a, file=sys.stderr); continue
    nm, L = refL.get(a, ('?', 0))
    hdr = '/* FUN_%x %s - n64dll_all.c L%d%s */\n' % (a, '' if nm.startswith('FUN_') else '(' + nm + ')', L, '; ' + src if src else '')
    if a in fpassoc and not src:
        hdr += '/* FP-ORDER: right-nested float + / * at %s: Ghidra prints these without parentheses - checked against the asm: %s */\n' % (', '.join(fpassoc[a]), 'yes, bracketed in gen/patches.txt' if a in PATCHES else 'NO')
    body = fixbody(a, text)                      # patches: applied at load (above)
    k = body.find('\n{\n')
    if k >= 0: body = body[:k + 3] + '  TS_ENTER(0x%xULL);\n' % a + body[k + 3:]
    out_funcs.append((a, hdr + body))

# ---- C++ exception handling (gen/eh.py): functions with try blocks get a catching wrapper; the throw table
EHF = eh.load(PE, set(want))
EHFIX = eh.ehfix(os.path.join(HERE, 'ehfix.txt'))
eh_warn = []
# functions that can throw: a ts_cxx_throw in their text or a call to one that can (fixpoint over the call graph)
callg = {a: set(int(x, 16) for x in re.findall(r'\bFUN_([0-9a-f]{9})\(', t)) for a, t in out_funcs}
throwers = set(a for a, t in out_funcs if 'ts_cxx_throw(' in t)
while True:
    more = set(a for a, cs in callg.items() if a not in throwers and cs & throwers)
    if not more: break
    throwers |= more
for i, (a, t) in enumerate(out_funcs):
    if a in EHF:
        t2, info = eh.transform(PE, EHF[a], t, refL, EHFIX, eh_warn, throwers)
        out_funcs[i] = (a, t2)
throw_tis = sorted(set(int(x, 16) for a, t in out_funcs for x in re.findall(r'\bts_cxx_throw\)?\([^;]*?&DAT_([0-9a-f]{9})\)', t)))
for w in eh_warn: print('eh: ' + w, file=sys.stderr)
if VA_ERR:
    sys.exit('gen.py: variadic arguments of the error builders not recovered (gen/vaargs.py):\n  ' + '\n  '.join(VA_ERR))
if CVT_ERR:
    sys.exit('gen.py: (longlong) of a double taken as a bit copy in a function with 64-bit cvttsd2si (cvt64.txt) - decide '
             'from the asm (list it after "bits" when it is a copy):\n  ' + '\n  '.join(CVT_ERR))

# ---- prototypes for every function defined or called
protos = []
crt = {}
crtfile = os.path.join(HERE, '..', 'ts_crt.h')
if os.path.exists(crtfile):
    for l in open(crtfile):
        m = re.search(r'FUN_([0-9a-f]{9})', l)
        if m and '#define' in l: crt[int(m.group(1), 16)] = True
defined = set(want)
stubs = []
for a in sorted(set(called) | defined):
    if a in crt: continue
    fx = os.path.join(FIXDIR, 'FUN_%x.c' % a)
    if os.path.exists(fx):
        protos.append(signature(open(fx).read()) + ';')
    elif a in funcs:
        sig = typefix(signature(funcs[a]))
        sig = partfix(sig)
        protos.append(sig + ';')
        if a not in defined:
            stubs.append(a)
    else:
        stubs.append(a)
        protos.append('/* FUN_%x: no decompile (CRT?) */ uint64_t FUN_%x();' % (a, a))

KNOWN = set()
for hf in ('ghidra.h', 'ts_types.h', 'ts_rt.h'):
    for m in re.finditer(r'typedef [^;]*?\b(\w+)\s*(?:\[[^\]]*\])?\s*;', open(os.path.join(HERE, '..', hf)).read()): KNOWN.add(m.group(1))
    for m in re.finditer(r'typedef [^;]*?\(\s*\*?\s*(\w+)\)', open(os.path.join(HERE, '..', hf)).read()): KNOWN.add(m.group(1))
KNOWN |= set('ts_eh ts_ehinfo ts_ehthrow code ts_ulong ts_long _func_void_ptr_void_ptr undefined undefined1 undefined2 undefined4 undefined8 void char short int long float double signed unsigned struct union enum const volatile size_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t uintptr_t va_list TSFILE return sizeof if else while for do switch case goto break continue default'.split())
alltext = '\n'.join(t for a, t in out_funcs) + '\n'.join(protos)
cand = set(re.findall(r'\(\s*([A-Za-z_]\w*)\s*\*+\s*\)', alltext)) | set(re.findall(r'(?m)^\s*([A-Za-z_]\w*)\s+\**\s*\w+\s*(?:\[[^\]]*\])?\s*;', alltext)) | set(re.findall(r'[(,]\s*([A-Za-z_]\w*)\s*\*+\s*\w+\s*(?=[,)])', alltext))
autot = sorted(c for c in cand if c not in KNOWN and not c.startswith('FUN_') and not re.match(r'^(local|param|uVar|iVar|lVar|pcVar|puVar|piVar|dVar|bVar|cVar|sVar|pvVar|plVar|ppVar|auVar|in_|extraout|unaff)', c))
with open(os.path.join(OUT, 'ts_data_addrs.txt'), 'w') as fa:
    for a in sorted(data_addrs): fa.write('%x\n' % a)
with open(os.path.join(OUT, 'ts_gen.h'), 'w') as h:
    h.write('/* generated by viewer/nest/ts/gen/gen.py from the Ghidra export of n64dll.dll - do not edit */\n')
    h.write('#ifndef TS_GEN_H\n#define TS_GEN_H\n#include "../ghidra.h"\n#include "../ts_rt.h"\n#include "../ts_types.h"\n#include "../ts_crt.h"\n\n')
    for c in autot: h.write('typedef unsigned char %s; /* class without a recovered layout */\n' % c)
    h.write('\n')
    for p in protos: h.write(p + '\n')
    h.write('/* decompiler hazards (inputs the caller does not set; zeroed):\n')
    for z in hazards: h.write(' *   %s\n' % z)
    h.write(' */\n#endif\n')
with open(os.path.join(OUT, 'ts_gen.c'), 'w') as c:
    c.write('/* generated by viewer/nest/ts/gen/gen.py from the Ghidra export of n64dll.dll - do not edit.\n * Each function is the decompile of the address in its name; "L<n>" = decomp/nest/n64dll_all.c. */\n')
    c.write('#include "ts_gen.h"\n\n')
    for a, t in out_funcs: c.write(t + '\n')
    # thrown objects (gen/eh.py throwinfo): ThrowInfo image address, object size, the classes it can be caught as
    c.write('const ts_ehthrow ts_eh_throws[] = {\n')
    for ti in throw_tis:
        sz, names = eh.throwinfo(PE, ti)
        c.write('  {0x%xULL, %d, %d, {%s}},\n' % (ti, sz, len(names), ', '.join('"%s"' % n for n in names)))
    c.write('  {0, 0, 0, {0}}\n};\n')
    for a in stubs:
        if a in funcs:
            sig = partfix(typefix(signature(funcs[a])))
            c.write('%s { ts_not_ported(0x%xULL); }\n' % (sig, a))
        else:
            c.write('uint64_t FUN_%x() { ts_not_ported(0x%xULL); }\n' % (a, a))
print('%d functions, %d stubs, %d globals' % (len(out_funcs), len(stubs), len(used_globals)), file=sys.stderr)
