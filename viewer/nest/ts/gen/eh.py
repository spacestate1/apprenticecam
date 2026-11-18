"""eh.py - the MSVC C++ exception handling of n64dll.dll for the TrueShape port generator (gen.py).

The DLL is built with /EHsc: functions with a try block have unwind info naming __CxxFrameHandler3 whose handler data
is a FuncInfo (magic 0x19930520..22): the try-block map (state range tryLow..tryHigh, catch handlers: type descriptor
or "..." , catch funclet), the IP-to-state map (which state each instruction range is in).  _CxxThrowException
(FUN_180227464) takes the thrown object and its ThrowInfo (catchable type array: every class the object can be caught
as, with the object size).  The runtime (msvcrt __CxxFrameHandler3 -> FindHandler) walks the frames from the throw
outwards; in each frame the try blocks whose state range holds the frame's current state are searched in order for a
catch whose type is one of the thrown object's catchable types; the first match runs its funclet (the catch body),
which returns the address where the function continues.

The port does the same (ts_rt.c ts_throw): every transliterated function with a try block is split into
FUN_<a>__b (the decompile, which records the DLL's state of each call it makes in _eh->st: the IP-to-state map at that
call instruction) and FUN_<a> (pushes a ts_eh frame, setjmp, calls the body; on a catch runs the catch body and what
the continuation does).  The catch bodies + continuations are read from the machine code here (classify) when they are
the pattern the DLL uses throughout: copy the caught object, FUN_180053920 (report the error to the error table),
store constants (a result variable / globals), continue at code that returns that variable (or nothing) through the
stack-cookie check and the epilogue.  Any other catch is taken from gen/ehfix.txt (hand port, cited) or, without one,
is NOT PORTED (ts_bad when reached).  catch(...) blocks that only release a buffer and rethrow (std container
helpers) are dropped: the throw passes the frame as when no handler matches; the release is a free() the port skips
(memory only).  The unwind actions of the frames a throw passes (destructors of locals) are not run either; in the
transliterated functions they are all free() / container destructors (memory only; listed by ehdump.py --unwind)."""
import re, struct, subprocess, os

REPORT = 0x180053920       # FUN_180053920(caught, copy): FUN_1800538d0(code, ...) - the error-table report
COOKIE = 0x180224e50       # __security_check_cookie
THROW = 0x180227464        # _CxxThrowException
NOOPDTOR = 0x180010690     # destructor that does nothing: FUN_180001fc0 (empty) twice, n64dll_all.c FUN_180010690

class PE:
    def __init__(self, path):
        self.path = path
        d = self.d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = pe + 24; osz = struct.unpack_from('<H', d, pe + 20)[0]
        self.base = struct.unpack_from('<Q', d, opt + 24)[0]
        self.exc_rva, self.exc_sz = struct.unpack_from('<II', d, opt + 112 + 3 * 8)
        self.secs = []
        for i in range(nsec):
            o = opt + osz + 40 * i
            self.secs.append((struct.unpack_from('<I', d, o + 12)[0], struct.unpack_from('<I', d, o + 8)[0], struct.unpack_from('<I', d, o + 20)[0]))
    def off(self, rva):
        for va, vs, raw in self.secs:
            if va <= rva < va + max(vs, 1): return raw + rva - va
        raise ValueError(hex(rva))
    def u32(self, rva): return struct.unpack_from('<I', self.d, self.off(rva))[0]
    def i32(self, rva): return struct.unpack_from('<i', self.d, self.off(rva))[0]
    def cstr(self, rva):
        o = self.off(rva); e = self.d.index(b'\0', o); return self.d[o:e].decode('latin-1')
    def dis(self, a, b):
        o = subprocess.run(['objdump', '-d', '-M', 'intel', '--no-show-raw-insn', '--start-address=0x%x' % a,
                            '--stop-address=0x%x' % b, self.path], capture_output=True, text=True, check=True).stdout
        r = []
        for l in o.splitlines():
            m = re.match(r'\s+([0-9a-f]+):\s+(.*)', l)
            if m: r.append((int(m.group(1), 16), re.sub(r'\s+', ' ', m.group(2).strip())))
        return r

def tname(pe, td_rva):
    """type descriptor name: '.?AVnestErrorPrm@@' -> 'nestErrorPrm'"""
    n = pe.cstr(td_rva + 16)
    m = re.match(r'\.\?A[VU](\w+)@@$', n)
    return m.group(1) if m else n

def throwinfo(pe, ti):
    """ThrowInfo at image address ti: (object size, [catchable type names])"""
    rva = ti - pe.base
    cta = pe.u32(rva + 12)
    n = pe.u32(cta)
    names = []; size = 0
    for k in range(n):
        ct = pe.u32(cta + 4 + 4 * k)
        names.append(tname(pe, pe.u32(ct + 4)))
        if k == 0: size = pe.u32(ct + 20)
    return size, names

class Catch:
    def __init__(self, typ, funclet, cont):
        self.typ, self.funclet, self.cont = typ, funclet, cont
        self.code = None; self.why = ''; self.rethrow = False

class Try:
    def __init__(self, lo, hi):
        self.lo, self.hi, self.catches = lo, hi, []

class Func:
    def __init__(self, addr, end):
        self.addr, self.end, self.tries, self.ipmap = addr, end, [], []
        self.calls = {}          # call target -> [state of each call instruction]
    def state_at(self, ip):
        s = -1
        for a, st in self.ipmap:
            if a <= ip: s = st
            else: break
        return s

def load(pe, want):
    """{function address: Func} for the functions in want that have try blocks"""
    out = {}
    base = pe.base
    for k in range(pe.exc_sz // 12):
        b, e, u = struct.unpack_from('<III', pe.d, pe.off(pe.exc_rva) + 12 * k)
        if base + b not in want: continue
        o = pe.off(u); flags = pe.d[o] >> 3; ncodes = pe.d[o + 2]
        if flags & 4 or not flags & 3: continue
        p = u + 4 + 2 * ((ncodes + 1) & ~1)
        try:
            fi = pe.u32(p + 4); magic = pe.u32(fi)
        except Exception:
            continue
        if magic & 0xfffffff0 != 0x19930520: continue
        maxstate, pumap, ntry, ptry, nip, pip = struct.unpack_from('<IIIIII', pe.d, pe.off(fi) + 4)
        if ntry == 0: continue
        f = Func(base + b, base + e)
        f.ipmap = sorted((base + pe.u32(pip + 8 * j), pe.i32(pip + 8 * j + 4)) for j in range(nip))
        for t in range(ntry):
            lo, hi, chi, ncat, phand = struct.unpack_from('<iiiII', pe.d, pe.off(ptry) + 20 * t)
            tb = Try(lo, hi)
            for c in range(ncat):
                adj, ptype, disp, fn = struct.unpack_from('<IIiI', pe.d, pe.off(phand) + 20 * c)[:4]
                tb.catches.append(Catch(None if ptype == 0 else tname(pe, ptype), base + fn, None))
            f.tries.append(tb)
        for ip, ins in pe.dis(f.addr, f.end):
            m = re.match(r'(call|jmp) 0x([0-9a-f]+)$', ins)
            if not m: continue
            tgt = int(m.group(2), 16)
            if m.group(1) == 'jmp' and f.addr <= tgt < f.end: continue
            st = f.state_at(ip) if m.group(1) == 'call' else -1      # a tail jump leaves the frame
            f.calls.setdefault(tgt, []).append(st)
            th = thunk(pe, tgt)
            if th: f.calls.setdefault(th, []).append(st)
        out[f.addr] = f
    return out

def thunk(pe, a):
    ins = pe.dis(a, a + 8)
    if ins and ins[0][0] == a:
        m = re.match(r'jmp 0x([0-9a-f]+)$', ins[0][1])
        if m: return int(m.group(1), 16)
    return None

PROLOGUE = re.compile(r'^(mov QWORD PTR \[rsp\+0x(8|10)\],r(cx|dx)|push r\w+|sub rsp,0x[0-9a-f]+|mov rbp,rdx)$')
EPILOGUE = re.compile(r'^(add rsp,0x[0-9a-f]+|pop r\w+|ret|nop|int3)$')

def classify(pe, c, rettype):
    """C code of catch funclet + continuation, or None (c.why says why)"""
    ins = pe.dis(c.funclet, c.funclet + 0x200)
    for j in range(2, len(ins)):                   # catch (...) { release; throw; }: _CxxThrowException(0, 0)
        if ins[j][1] == 'call 0x%x' % THROW and {ins[j - 1][1], ins[j - 2][1]} == {'xor edx,edx', 'xor ecx,ecx'}:
            c.rethrow = True; c.why = 'rethrow'; return None
        if ins[j][1] == 'ret': break
    i = 0; stores = {}; gstores = []; reported = False; cont = None; prev = []
    while i < len(ins):
        a, s = ins[i]
        if PROLOGUE.match(s) or s == 'nop': i += 1; continue
        # copy of the caught object to the catch variable (rep movs 0x3f4 bytes)
        if s == 'rep movs BYTE PTR es:[rdi],BYTE PTR ds:[rsi]' or s == 'rep movs BYTE PTR [rdi],BYTE PTR [rsi]':
            i += 1; prev = []; continue
        if re.match(r'^(lea r(ax|cx|dx),\[rbp\+0x[0-9a-f]+\]|mov rdi,rax|mov rsi,rcx|mov ecx,0x3f4)$', s):
            prev.append(s); i += 1; continue
        m = re.match(r'^call 0x([0-9a-f]+)$', s)
        if m and int(m.group(1), 16) == REPORT and prev[-2:] and all(x.startswith('lea r') for x in prev[-2:]):
            reported = True; prev = []; i += 1; continue
        m = re.match(r'^mov DWORD PTR \[rbp\+0x([0-9a-f]+)\],0x([0-9a-f]+)$', s)
        if m: stores[int(m.group(1), 16)] = int(m.group(2), 16); i += 1; continue
        m = re.match(r'^mov DWORD PTR \[rip\+0x[0-9a-f]+\],0x([0-9a-f]+) # 0x([0-9a-f]+)$', s)
        if m: gstores.append((int(m.group(2), 16), int(m.group(1), 16))); i += 1; continue
        m = re.match(r'^lea rax,\[rip\+0x[0-9a-f]+\] # 0x([0-9a-f]+)$', s)
        if m:
            cont = int(m.group(1), 16); break
        c.why = 'funclet instruction %x: %s' % (a, s); return None
    if not cont or prev:
        c.why = 'funclet: no continuation / unused operands %s' % prev; return None
    c.cont = cont
    if not reported:
        c.why = 'funclet does not report'; return None
    # continuation: the function's return value, the cookie check, the epilogue
    ret = None; a = cont; seen = 0; xmm0 = False
    while seen < 40:
        ins = pe.dis(a, a + 0x100)
        if not ins: break
        jumped = False
        for ia, s in ins:
            seen += 1
            m = re.match(r'^mov eax,DWORD PTR \[rsp\+0x([0-9a-f]+)\]$', s)
            if m:
                slot = int(m.group(1), 16)
                if slot not in stores:
                    c.why = 'continuation returns a variable the catch does not set ([rsp+0x%x])' % slot; return None
                ret = stores[slot]; continue
            m = re.match(r'^mov eax,0x([0-9a-f]+)$', s)
            if m: ret = int(m.group(1), 16); continue
            if s == 'xor eax,eax': ret = 0; continue
            if re.match(r'^(mov rcx,QWORD PTR \[rsp\+0x[0-9a-f]+\]|xor rcx,rsp|lea rcx,\[rsp\+0x[0-9a-f]+\])$', s): continue
            m = re.match(r'^call 0x([0-9a-f]+)$', s)
            if m and int(m.group(1), 16) in (COOKIE, NOOPDTOR): continue
            m = re.match(r'^mov DWORD PTR \[rip\+0x[0-9a-f]+\],0x([0-9a-f]+) # 0x([0-9a-f]+)$', s)
            if m: gstores.append((int(m.group(2), 16), int(m.group(1), 16))); continue
            if s == 'xorpd xmm0,xmm0': xmm0 = True; continue
            m = re.match(r'^movsd QWORD PTR \[rip\+0x[0-9a-f]+\],xmm0 # 0x([0-9a-f]+)$', s)
            if m and xmm0: gstores.append((int(m.group(1), 16), 'double')); continue
            m = re.match(r'^jmp 0x([0-9a-f]+)$', s)
            if m: a = int(m.group(1), 16); jumped = True; break
            if s == 'ret':
                return finish(c, rettype, ret, gstores)
            if EPILOGUE.match(s): continue
            c.why = 'continuation instruction %x: %s' % (ia, s); return None
        if not jumped: break
    c.why = 'continuation: no return found'; return None

def finish(c, rettype, ret, gstores):
    code = ['FUN_180053920((undefined8)(uintptr_t)_eh.obj, (undefined4 *)_eh.obj);']
    for g, v in gstores:
        code.append('TS_G(double, 0x%xULL) = 0.0;' % g if v == 'double' else 'TS_G(uint32_t, 0x%xULL) = 0x%xU;' % (g, v))
    if rettype == 'void':
        code.append('return;')
    elif ret is None:
        c.why = 'continuation leaves no return value in a function returning %s' % rettype; return None
    else:
        code.append('return (%s)(uint32_t)0x%xU;' % (rettype, ret))
    c.code = ' '.join(code)
    return c.code

def ehfix(path):
    """gen/ehfix.txt: '@<funclet hex> <citation>' then C lines up to the next '@' or end"""
    fx = {}; cur = None
    if not os.path.exists(path): return fx
    for l in open(path):
        if l.startswith('#'): continue
        m = re.match(r'^@([0-9a-f]+)\s*(.*)$', l)
        if m: cur = int(m.group(1), 16); fx[cur] = [m.group(2).strip(), []]; continue
        if cur is not None and l.strip(): fx[cur][1].append(l.rstrip())
    return fx

def split_sig(text):
    """(head before the name incl. return type, name, parameter text, rest) of a function text 'RET NAME(PARAMS)\\n{...'"""
    k = text.find('\n{\n')
    sig = text[:k]
    m = list(re.finditer(r'(?m)^(?!#|/\*| \*)[^\n]*?\b(FUN_[0-9a-f]{9})\(', sig))[-1]
    m = re.compile(r'\b(FUN_[0-9a-f]{9})\(').search(sig, m.start())
    return sig[:m.start()], m.group(1), sig[m.end():sig.rfind(')')], text[k:]

def param_names(params):
    p = params.strip()
    if p in ('', 'void'): return []
    parts = []; depth = 0; cur = ''
    for ch in p:
        if ch == '(' : depth += 1
        if ch == ')': depth -= 1
        if ch == ',' and depth == 0: parts.append(cur); cur = ''
        else: cur += ch
    parts.append(cur)
    names = []
    for x in parts:
        x = x.strip()
        if x == '...': raise ValueError('variadic')
        m = re.search(r'(\w+)\s*(\[[^\]]*\])?$', x) if '(' not in x else re.search(r'\(\s*\*\s*(\w+)\s*\)', x)
        if not m: raise ValueError("parameter %r" % x)
        names.append(m.group(1))
    return names

def transform(pe, f, text, refL, fixes, warn, throwers):
    """split a transliterated function with try blocks into body + catching wrapper"""
    head, name, params, rest = split_sig(text)
    nl = head.rstrip(' ').rfind('\n')
    hdr, rtype = head[:nl + 1], head[nl + 1:].strip()
    if not rtype:                                  # 'RET\nNAME(..' (long parameter lists)
        nl = hdr.rstrip('\n').rfind('\n')
        hdr, rtype = hdr[:nl + 1], hdr[nl + 1:].strip()
    if not rtype or rtype.startswith(('#', '/*')): raise ValueError('FUN_%x: return type not on the signature line' % f.addr)
    names = param_names(params)
    pfx = re.sub(r'\s+', ' ', params).strip()
    bparams = 'ts_eh *_eh' if pfx in ('', 'void') else pfx + ', ts_eh *_eh'
    # every call the decompile makes to a transliterated function / the throw records the state of that call
    unresolved = set()
    def call(m):
        callee = int(m.group(1), 16) if m.group(1) else THROW
        if callee != THROW and callee not in throwers: return m.group(0)
        sts = f.calls.get(callee)
        if sts is not None:            # a state no try block holds is "no try" (-1): the unwind states of locals
            sts = [x if any(t.lo <= x <= t.hi for t in f.tries) else -1 for x in sts]
        if sts is None:
            unresolved.add(callee); return m.group(0)
        if len(set(sts)) != 1:
            unresolved.add(callee); return m.group(0)
        return '(_eh->st = %d, %s)(' % (sts[0], m.group(0)[:-1])
    body = re.sub(r'\bFUN_([0-9a-f]{9})\(|\bts_cxx_throw\(', call, rest)
    # the throw's state: ts_cxx_throw replaced FUN_180227464 (gen.py), same rule
    tries = []; cases = []; h = 0
    for tb in f.tries:
        cats = []
        for c in tb.catches:
            fx = fixes.get(c.funclet)
            if fx and fx[0].startswith('INLINE'): continue
            if not fx: classify(pe, c, rtype)
            if c.rethrow: continue
            nm, L = refL.get(c.funclet, ('?', 0))
            cite = '%s n64dll_all.c L%d' % (nm, L)
            if fx:
                code = '\n'.join('    ' + x for x in fx[1]); src = 'gen/ehfix.txt: ' + fx[0]
            elif c.code:
                code = '    ' + c.code; src = 'from the machine code: funclet + continuation 0x%x' % c.cont
            else:
                code = '    ts_bad(0x%xULL, "catch funclet %x not ported: %s");' % (f.addr, c.funclet, c.why.replace('"', "'"))
                src = 'NOT PORTED'
                warn.append('FUN_%x catch %s funclet %x: %s' % (f.addr, c.typ or '...', c.funclet, c.why))
            cases.append('  case %d: /* catch (%s) states %d..%d: %s; %s */\n%s' % (h, c.typ or '...', tb.lo, tb.hi, cite, src, code))
            cats.append(c.typ); h += 1
        if cats: tries.append((tb.lo, tb.hi, cats))
    if unresolved:
        warn.append('FUN_%x: calls without a single DLL state: %s' % (f.addr, ' '.join('%x' % u for u in sorted(unresolved))))
    if not tries:
        return text, None
    info = 'static const ts_ehinfo ehi_%x = {0x%xULL, %d, {%s}};\n' % (f.addr, f.addr, len(tries), ', '.join(
        '{%d, %d, %d, {%s}}' % (lo, hi, len(c), ', '.join('"%s"' % (x or '...') for x in c)) for lo, hi, c in tries))
    bname = name + '__b'
    out = hdr + info
    out += 'static %s %s(%s)%s\n' % (rtype, bname, bparams, body)
    args = ', '.join(names + ['&_eh'])
    out += '%s %s(%s)\n{\n  ts_eh _eh; _eh.info = &ehi_%x; _eh.st = -1; _eh.prev = ts_eh_top;\n' % (rtype, name, pfx, f.addr)
    if rtype == 'void':
        out += '  if (setjmp(_eh.jb) == 0) { ts_eh_top = &_eh; %s(%s); ts_eh_top = _eh.prev; return; }\n' % (bname, args)
    else:
        out += '  if (setjmp(_eh.jb) == 0) { ts_eh_top = &_eh; %s _r = %s(%s); ts_eh_top = _eh.prev; return _r; }\n' % (rtype, bname, args)
    out += '  switch (_eh.handler) {\n%s\n  }\n  ts_bad(0x%xULL, "catch");\n}\n' % ('\n'.join(cases), f.addr)
    return out, info

def throwinfos(pe, addrs):
    return {a: throwinfo(pe, a) for a in addrs}
