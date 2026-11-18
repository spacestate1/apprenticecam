#!/usr/bin/env python3
"""retype.py - find stack locals Ghidra declared "undefined8" (raw 8 bytes) that the decompile uses as doubles, by
compiling build/ts_gen.c with -Wfloat-conversion -Wconversion: a conversion between such a local and double is
C's numeric conversion, but in the DLL the bytes are a double (movsd).  Appends "<func> <local> double" lines to
gen/retype.txt (read by gen.py) and prints the conversions it cannot attribute to a local.  Dev helper; rerun
gen.py afterwards until it reports nothing."""
import re, subprocess, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'build', 'ts_gen.c')
r = subprocess.run(['gcc', '-std=gnu17', '-fpermissive', '-fsyntax-only', '-Wfloat-conversion', '-Wconversion', SRC],
                   capture_output=True, text=True, cwd=os.path.join(HERE, '..'))
lines = open(SRC).read().split('\n')
func_at = []; cur = None
for i, l in enumerate(lines):
    m = re.match(r'/\* FUN_([0-9a-f]{9}) ', l)
    if m: cur = m.group(1)
    func_at.append(cur)
decl = {}   # (func, name) -> declared type from the frame macros / declarations
for i, l in enumerate(lines):
    m = re.match(r'#define (local_[0-9a-f]+|[a-zA-Z]*Stack_[0-9a-f]+) \(\*\(([\w\s\*]+?) \*\)\(ts_frame', l)
    if m and func_at[i]: decl[(func_at[i], m.group(1))] = m.group(2).strip()
# declared types of every variable of every function (declarations + parameters), from the generated text
vdecl = {}
for i, l in enumerate(lines):
    f = func_at[i]
    if not f: continue
    for m in re.finditer(r'(?:^|[(,])\s*(undefined8|ulonglong|undefined8 \*|ulonglong \*)\s*(\w+)\s*(?=[;,)])', l):
        vdecl[(f, m.group(2))] = m.group(1)
found = set(); other = []
def balanced(t, i):
    d = 0
    for j in range(i, len(t)):
        if t[j] == '(': d += 1
        elif t[j] == ')':
            d -= 1
            if d == 0: return j + 1
    return len(t)
WARN = re.compile(r'ts_gen\.c:(\d+):(\d+): warning: conversion from ‘([^’]+)’(?: \{aka[^}]*\})? to ‘([^’]+)’')
NOTE = re.compile(r'ts_gen\.c:(\d+):(\d+): note: in expansion of macro')
errl = r.stderr.split('\n'); events = []
for i, l in enumerate(errl):
    w = WARN.search(l)
    if not w: continue
    ln, col = int(w.group(1)), int(w.group(2))
    for j in range(i + 1, min(i + 8, len(errl))):
        if WARN.search(errl[j]): break
        n = NOTE.search(errl[j])
        if n: ln, col = int(n.group(1)), int(n.group(2)); break
    events.append((ln, col, w.group(3), w.group(4)))
for ln, col, frm, to in events:
    if 'double' not in (frm, to): continue
    text = lines[ln - 1]; f = func_at[ln - 1]; rest = text[col - 1:]
    if text.startswith('#define '):
        n = text.split()[1]
        if decl.get((f, n)) in ('undefined8', 'ulonglong', 'longlong'): found.add((f, n, 'double')); continue
    if rest.startswith('*(undefined8 *)('):
        e = balanced(rest, len('*(undefined8 *)'))
        found.add((f, 'deref', rest[:e].replace(' ', '\\x20'))); continue
    tok = re.match(r'\w+', rest)
    cands = []
    if tok: cands.append((tok.group(0), rest[len(tok.group(0)):].lstrip().startswith('[')))
    m = re.match(r'\s*(\w+)\s*(\[[^\]]*\])?\s*=[^=]', text)
    if m: cands.append((m.group(1), bool(m.group(2))))
    done = False
    for n, idx in cands:
        ty = vdecl.get((f, n)) or decl.get((f, n))
        if ty in ('undefined8', 'ulonglong') and not idx and re.match(r'(local|\w*Stack)_(res)?[0-9a-f]+$', n): found.add((f, n, 'double')); done = True; break
        if ty in ('undefined8 *', 'ulonglong *') and idx:
            mi = re.match(r'\w+\s*\[[^\]]*\]', rest if rest.startswith(n) else text.strip())
            if mi: found.add((f, 'store', mi.group(0).replace(' ', '\\x20'))); done = True; break
    if not done:
        other.append('%d:%d %s->%s FUN_%s: %s' % (ln, col, frm, to, f, text.strip()[:120]))
rf = os.path.join(HERE, 'retype.txt')
have = set()
if os.path.exists(rf):
    for l in open(rf):
        p = l.split('#')[0].split(None, 2)
        if len(p) >= 3: have.add((p[0], p[1], p[2].strip()))
new = sorted(x for x in found if x not in have)
with open(rf, 'a') as fo:
    for x in new: fo.write('%s %s %s\n' % x)
print('%d new retypes, %d other conversions' % (len(new), len(other)))
for o in other: print('  ' + o)
