#!/usr/bin/env python3
"""Dump the Parasolid XT blobs (SOLIDXMT records, formats/xt.md) of Mastercam parts to OUT/<part>/<sld_N>.x_b and
write OUT/manifest.tsv (part, solid name, file).  Uses the existing readers tools/mcam_walk.py + tools/xt_parse.py
(read-only).  Usage: xt_extract.py OUT PART.mcx-9 ..."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import xt_parse

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rows = []
    for p in sys.argv[2:]:
        part = os.path.splitext(os.path.basename(p))[0]
        d = os.path.join(out, part)
        os.makedirs(d, exist_ok=True)
        seen = {}
        for name, blob in xt_parse.solids_from_mcam(p):
            name = name.decode('latin-1') if isinstance(name, bytes) else str(name)
            name = name.strip('\0 ') or 'solid'
            k = seen.get(name, 0); seen[name] = k + 1
            fn = os.path.join(d, name + (f'_{k}' if k else '') + '.x_b')
            open(fn, 'wb').write(blob)
            rows.append((part, name, fn))
    with open(os.path.join(out, 'manifest.tsv'), 'w') as f:
        for r in rows: f.write('\t'.join(r) + '\n')
    print(len(rows), 'solids')

if __name__ == '__main__':
    main()
