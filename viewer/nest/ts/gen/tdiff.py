#!/usr/bin/env python3
"""tdiff.py DLLTRACE PORTTRACE - first divergence between the DLL's function-entry trace (coverage loader,
COV_TRACE) and the port's (TS_TRACE).  Dev helper."""
import sys
# functions whose calls depend on uninitialised stack bytes in the DLL (garbage the port cannot reproduce):
# partType::partType -> UnknownPartName when the name at +0xfa is shorter than 11 (FUN_180032dd0 L28489)
IGN = set(['1800330b0'] + sys.argv[3:])
a = [l.split()[0] for l in open(sys.argv[1]) if l.strip() and l.split()[0] not in IGN]
b = [l.split()[0] for l in open(sys.argv[2]) if l.strip() and l.split()[0] not in IGN]
for i, (x, y) in enumerate(zip(a, b)):
    if x != y:
        print('diverge at %d' % i); print(' dll :', ' '.join(a[max(0, i - 8):i + 6])); print(' port:', ' '.join(b[max(0, i - 8):i + 6])); sys.exit(1)
print('same for %d entries (dll %d, port %d)' % (min(len(a), len(b)), len(a), len(b)))
if len(a) != len(b):
    i = min(len(a), len(b)); print(' dll :', ' '.join(a[max(0, i - 8):i + 6])); print(' port:', ' '.join(b[max(0, i - 8):i + 6]))
