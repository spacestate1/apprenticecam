"""vaargs.py - the variadic arguments of NestLib's error builders at their call sites, read from the machine code.

FUN_1800537a0 (named: the error object, code, severity, kind) and FUN_180052fd0 (named: code, severity, kind) take the
message's printf arguments as MS x64 varargs (hand ports fix/FUN_1800537a0.c, fix/FUN_180052fd0.c).  Ghidra's
prototypes give them 4 parameters, so the decompile passes no message argument to FUN_1800537a0 and at most the 4th
(r9) to FUN_180052fd0: the port printed "Line is too small. ( 0.000000, 0.000000 ) to ( 0.000000, 0.000000 )." where
the DLL prints the line's end points.  For every call instruction: the stores into the outgoing argument slots
[rsp+0x20+8k] (and r9 for FUN_180052fd0's first variadic) after the previous call, each traced back to the instruction
that loaded the register: a frame slot ([rsp+X]: a parameter's home slot or the local Ghidra names by that offset), a
global (value or address), an immediate.  The C call with the same named constants (code, severity, kind - in order
among equal ones) gets those arguments.  Anything else is an error listed by gen.py (the generation stops)."""
import re

TARGETS = {0x1800537a0: 4, 0x180052fd0: 3}      # address -> named parameter count
REGS = {4: ('rcx', 'edx', 'r8d', 'r9d'), 3: ('ecx', 'edx', 'r8d')}

def frame_size(ins):
    """bytes between the entry RSP and RSP after the prologue (pushes + sub rsp / __chkstk)"""
    s = 0; eax = None
    for a, x in ins[:12]:
        if re.match(r'push r\w+$', x): s += 8
        m = re.match(r'mov eax,0x([0-9a-f]+)$', x)
        if m: eax = int(m.group(1), 16)
        m = re.match(r'sub rsp,0x([0-9a-f]+)$', x)
        if m: return s + int(m.group(1), 16)
        if x == 'sub rsp,rax' and eax is not None: return s + eax
    return None

def frame_names(t):
    """offset -> (name, byte size) of the frame variables named by offset in the function text (declarations or the
    ts_frame defines of gen.py stack_frame)"""
    out = {}
    SZ = {'double': 8, 'undefined8': 8, 'longlong': 8, 'ulonglong': 8, 'int': 4, 'uint': 4, 'undefined4': 4, 'float': 4,
          'short': 2, 'ushort': 2, 'undefined2': 2, 'char': 1, 'undefined1': 1, 'byte': 1, 'bool_t': 1, 'uchar': 1}
    for m in re.finditer(r'(?m)^#define ((?:local|[a-zA-Z]*Stack)_([0-9a-f]+)) \(\*\((.*?) \(?\*\)?((?:\[\d+\])*)\)?\(ts_frame', t):
        ty = m.group(3).strip(); n = 1
        for d in re.findall(r'\[(\d+)\]', m.group(4)): n *= int(d)
        out[int(m.group(2), 16)] = (m.group(1), SZ.get(ty, 8 if '*' in ty else 0) * n)
    for m in re.finditer(r'(?m)^\s+([A-Za-z_][\w \*]*?)\s*\b((?:local|[a-zA-Z]*Stack)_([0-9a-f]+))\s*((?:\[\d+\]\s*)*);', t):
        ty = m.group(1).strip(); n = 1
        for d in re.findall(r'\[(\d+)\]', m.group(4)): n *= int(d)
        out.setdefault(int(m.group(3), 16), (m.group(2), SZ.get(ty, 8 if '*' in ty else 0) * n))
    return out

def slot_expr(X, S, names, params, ty):
    """C lvalue of the frame slot [rsp+X] read as ty"""
    if X >= S + 8:
        k = (X - S) // 8
        if (X - S) % 8 == 0 and k <= len(params): return '*(%s *)&%s' % (ty, params[k - 1])
        return None
    n = S - X
    if n in names: return '*(%s *)&%s' % (ty, names[n][0])
    for off, (nm, sz) in names.items():          # inside an array / struct local
        if off > n > off - sz: return '*(%s *)((char *)&%s + %d)' % (ty, nm, off - n)
    return None

def source(win, j, reg, S, names, params, gname):
    """C expression of register reg as loaded before index j of win"""
    r64 = {'eax': 'rax', 'ecx': 'rcx', 'edx': 'rdx', 'r8d': 'r8', 'r9d': 'r9'}.get(reg, reg)
    alias = {reg, r64, {'rax': 'eax', 'rcx': 'ecx', 'rdx': 'edx', 'r8': 'r8d', 'r9': 'r9d'}.get(r64, r64)}
    for i in range(j - 1, -1, -1):
        a, x = win[i]
        m = re.match(r'(\w+) (\w+),(.*)$', x)
        if not m or m.group(2) not in alias: continue
        op, dst, src = m.groups()
        g = re.search(r'# 0x([0-9a-f]+)$', src)
        fm = re.match(r'(QWORD|DWORD|XMMWORD) PTR \[rsp\+0x([0-9a-f]+)\]$', src)
        pm = re.match(r'(QWORD|DWORD) PTR \[(r\w\w)\+0x([0-9a-f]+)\]$', src)   # through a pointer register
        if pm and pm.group(2) != 'rsp':
            base = source(win, i, pm.group(2), S, names, params, gname)
            ty = 'double' if op == 'movsd' else ('uint64_t' if pm.group(1) == 'QWORD' else 'uint32_t')
            return base and '*(%s *)((char *)(uintptr_t)%s + 0x%s)' % (ty, base, pm.group(3))
        if op == 'movq' and re.match(r'xmm\d+$', src):                         # r9 = xmm3 (MS varargs: a double in both)
            return source(win, i, src, S, names, params, gname)
        if op == 'lea':
            if g: return '(void *)&%s' % gname(int(g.group(1), 16))
            m2 = re.match(r'\[rsp\+0x([0-9a-f]+)\]$', src)
            if m2:
                e = slot_expr(int(m2.group(1), 16), S, names, params, 'char')
                return e and '(void *)' + e[len('*(char *)'):]       # '&NAME' / '((char *)&NAME + d)'
            return None
        if op in ('movsd', 'movups') and dst.startswith('xmm'):           # movups: its low 8 bytes are what is stored
            if g: return '*(double *)&%s' % gname(int(g.group(1), 16))
            if fm: return slot_expr(int(fm.group(2), 16), S, names, params, 'double')
            return None
        if op == 'mov':
            ty = 'uint64_t' if dst.startswith('r') and not dst.endswith('d') else 'uint32_t'
            if g: return '*(%s *)&%s' % (ty, gname(int(g.group(1), 16)))
            if fm: return slot_expr(int(fm.group(2), 16), S, names, params, ty)
            m2 = re.match(r'0x([0-9a-f]+)$', src)
            if m2: return '(%s)0x%s' % (ty, m2.group(1))
            return None
        if op == 'movsxd' and fm:
            e = slot_expr(int(fm.group(2), 16), S, names, params, 'int32_t')
            return e and '(int64_t)' + e
        return None
    return None

def calls_in_asm(ins, target, S, names, params, gname):
    """[(named constants, [extra C args] or None, call address)] in address order"""
    nn = TARGETS[target]; out = []
    start = next(j + 1 for j, (a, x) in enumerate(ins) if x.startswith('sub rsp,'))   # after the prologue (home spills)
    for j, (a, x) in enumerate(ins):
        if not x.startswith('call'): continue
        if x != 'call 0x%x' % target: start = j + 1; continue
        win = ins[start:j]; start = j + 1
        consts = []
        for r in REGS[nn][-3:]:
            v = None
            for _, y in reversed(win):
                m = re.match(r'mov %s,0x([0-9a-f]+)$' % r, y)
                if m: v = int(m.group(1), 16); break
                if re.match(r'\w+ %s,' % r, y): break
            consts.append(v)
        extra = []; ok = True
        slots = {}
        for k, (_, y) in enumerate(win):
            m = re.match(r'(mov|movsd) (QWORD|DWORD) PTR \[rsp\+0x([0-9a-f]+)\],(\w+)$', y)
            if m and 0x20 <= int(m.group(3), 16) < 0x80:
                slots[int(m.group(3), 16)] = (k, m.group(4), m.group(1), m.group(2))
            m = re.match(r'mov (QWORD|DWORD) PTR \[rsp\+0x([0-9a-f]+)\],0x([0-9a-f]+)$', y)
            if m and 0x20 <= int(m.group(2), 16) < 0x80:
                slots[int(m.group(2), 16)] = (k, '#0x' + m.group(3), 'mov', m.group(1))
        # outgoing argument slots: contiguous from [rsp+0x20] and not read again before the call (a store the code
        # reads back is a local variable that happens to sit there)
        args = []
        for s_ in range(0x20, 0x80, 8):
            if s_ not in slots: break
            k = slots[s_][0]
            if any(re.search(r'\[rsp\+0x%x\]' % s_, y) for _, y in win[k + 1:]): break
            args.append(s_)
        if nn == 3:                                  # FUN_180052fd0: its first variadic is the 4th register argument
            r9 = [k for k, (_, y) in enumerate(win) if re.match(r'\w+ r9d?,', y)]
            if r9: extra.append(source(win, r9[-1] + 1, 'r9', S, names, params, gname))
            elif args: ok = False                    # stack arguments without r9: not this pattern
        for s_ in args:
            k, reg, op, w = slots[s_]
            if reg.startswith('#'): extra.append('(%s)%s' % ('uint64_t' if w == 'QWORD' else 'uint32_t', reg[1:])); continue
            extra.append(source(win, k, reg, S, names, params, gname))
        out.append((tuple(consts), extra if ok and None not in extra else None, a))
    return out

def fix(addr, t, pe, ins_of, call_args, err):
    """append the variadic arguments to the error-builder calls in function text t"""
    tg = [x for x in TARGETS if re.search(r'\bFUN_%x\(' % x, t)]
    if not tg: return t
    ins = ins_of(addr)
    S = frame_size(ins)
    if S is None: err.append('FUN_%x: frame size not found' % addr); return t
    names = frame_names(t)
    i = t.find('\n{\n'); sig = t[:i]
    pm = re.search(r'\bFUN_%x\s*\(((?:.|\n)*)\)\s*$' % addr, sig)
    params = [re.search(r'(\w+)\s*$', p.strip()).group(1) for p in pm.group(1).split(',') if p.strip() not in ('', 'void', '...')] if pm else []
    for target in tg:
        nn = TARGETS[target]
        asm = calls_in_asm(ins, target, S, names, params, lambda g: 'DAT_%x' % g)
        cs = []
        for m in re.finditer(r'\bFUN_%x\(' % target, t):
            if t[:m.start()].rstrip().endswith(('#define', '*')) or m.start() < i: continue
            e, args = call_args(t, m.end() - 1)
            def num(s):
                s = s.strip()
                try: return int(s, 0)
                except ValueError: return None
            cs.append((m.start(), e, args, tuple(num(x) for x in args[nn - 3:nn])))
        if len(cs) != len(asm):
            err.append('FUN_%x: %d calls of FUN_%x in the C, %d in the asm' % (addr, len(cs), target, len(asm))); continue
        used = [False] * len(asm); edits = []
        for st, e, args, consts in cs:
            k = next((k for k in range(len(asm)) if not used[k] and asm[k][0] == consts), None)
            if k is None: err.append('FUN_%x: FUN_%x(%s) not found in the asm' % (addr, target, ','.join(args))); continue
            used[k] = True
            extra = asm[k][1]
            if extra is None: err.append('FUN_%x: FUN_%x call 0x%x: arguments not traced' % (addr, target, asm[k][2])); continue
            if not extra: continue
            named = args[:nn]
            edits.append((st, e, 'FUN_%x(%s, %s /* varargs: asm call 0x%x (gen/vaargs.py) */)' % (target, ','.join(named), ', '.join(extra), asm[k][2])))
        for st, e, new in sorted(edits, reverse=True):
            t = t[:st] + new + t[e:]
    return t
