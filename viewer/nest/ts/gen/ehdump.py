#!/usr/bin/env python3
"""ehdump.py N64DLL [funcs.txt]: the MSVC C++ exception-handling data of n64dll.dll - for every function whose unwind info
names __CxxFrameHandler3 (FuncInfo magic 0x19930520..22): its try blocks (state range, the IP ranges of those states from
the IP-to-state map) and their catch handlers (type descriptor name or "...", the catch funclet, the continuation address
the funclet returns = mov/lea rax). With funcs.txt only the functions listed there. Dev tool for the TrueShape port."""
import struct, sys

d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = pe + 24; osz = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, opt + 24)[0]
dd = opt + 112
exc_rva, exc_sz = struct.unpack_from('<II', d, dd + 3 * 8)
secs = []
for i in range(nsec):
    o = opt + osz + 40 * i
    va, vs, raw, rs = struct.unpack_from('<IIII', d, o + 12)[0], struct.unpack_from('<I', d, o + 8)[0], struct.unpack_from('<I', d, o + 20)[0], struct.unpack_from('<I', d, o + 16)[0]
    secs.append((struct.unpack_from('<I', d, o + 12)[0], struct.unpack_from('<I', d, o + 8)[0], struct.unpack_from('<I', d, o + 20)[0]))
def off(rva):
    for va, vs, raw in secs:
        if va <= rva < va + max(vs, 1): return raw + rva - va
    raise ValueError(hex(rva))
def u32(rva): return struct.unpack_from('<I', d, off(rva))[0]
def i32(rva): return struct.unpack_from('<i', d, off(rva))[0]
def cstr(rva):
    o = off(rva); e = d.index(b'\0', o); return d[o:e].decode('latin-1')

UNWIND = '--unwind' in sys.argv
if UNWIND: sys.argv.remove('--unwind')
want = None
if len(sys.argv) > 2:
    want = set(int(l.split()[0], 16) - base for l in open(sys.argv[2]) if l.strip() and not l.startswith('#'))

funcs = []
for k in range(exc_sz // 12):
    b, e, u = struct.unpack_from('<III', d, off(exc_rva) + 12 * k)
    funcs.append((b, e, u))
def unwind_handler(u):
    o = off(u); vf = d[o]; flags = vf >> 3; ncodes = d[o + 2]
    p = u + 4 + 2 * ((ncodes + 1) & ~1)
    if flags & 4:                      # chained
        return None
    if flags & 3:
        return u32(p), p + 4
    return None
def cont_of(funclet):                  # the continuation the catch funclet returns: lea rax,[rip+x] / mov eax,imm before ret
    o = off(funclet)
    for i in range(0, 0x400):
        if d[o + i:o + i + 3] == b'\x48\x8d\x05':
            return funclet + i + 7 + struct.unpack_from('<i', d, o + i + 3)[0]
    return None
out = []
for b, e, u in funcs:
    if want is not None and b not in want: continue
    h = unwind_handler(u)
    if not h: continue
    hrva, hdata = h
    try:
        fi = u32(hdata)
        magic = u32(fi)
    except Exception:
        continue
    if magic & 0xfffffff0 != 0x19930520: continue
    maxstate, pumap, ntry, ptry, nip, pip = struct.unpack_from('<IIIIII', d, off(fi) + 4)
    if UNWIND:
        acts = []
        for s in range(maxstate):
            to, act = struct.unpack_from('<iI', d, off(pumap) + 8 * s)
            if act: acts.append('%d:%x' % (s, base + act))
        if acts: print('%x unwind %s' % (base + b, ' '.join(acts)))
        continue
    if ntry == 0: continue
    ipmap = [(u32(pip + 8 * j), i32(pip + 8 * j + 4)) for j in range(nip)]
    for t in range(ntry):
        lo, hi, chi, ncat, phand = struct.unpack_from('<iiiII', d, off(ptry) + 20 * t)
        ranges = []
        for j, (ip, st) in enumerate(ipmap):
            nxt = ipmap[j + 1][0] if j + 1 < len(ipmap) else e
            if lo <= st <= hi and b <= ip < e: ranges.append('%x-%x' % (base + ip, base + nxt))
        cats = []
        for c in range(ncat):
            adj, ptype, disp, fn = struct.unpack_from('<IIiI', d, off(phand) + 20 * c)[:4]
            tn = '...' if ptype == 0 else cstr(ptype + 16)
            co = cont_of(fn)
            cats.append('%s funclet %x -> %s' % (tn, base + fn, hex(base + co) if co else '?'))
        print('%x try %d states %d..%d ip %s | %s' % (base + b, t, lo, hi, ' '.join(ranges), ' ; '.join(cats)))
