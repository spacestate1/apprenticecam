#!/usr/bin/env python3
"""xt_rt_types.py SCHEMA_DIR MANIFEST STATUS.tsv - for the round trip of tools/tests/xt_roundtrip.sh: per surface / curve type, how many
solids that contain it came back canonically EQUAL (or equal apart from foreign attributes) - the evidence that the writer's FACE.sense /
curve sense for that type agrees with Mastercam's own XT (ours; formats/occt_xt.md 10.1)."""
import os, re, sys, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import xt_parse

TYPES = ['PLANE', 'CYLINDER', 'CONE', 'SPHERE', 'TORUS', 'B_SURFACE', 'LINE', 'CIRCLE', 'ELLIPSE', 'B_CURVE']

def main():
    sd, man, st = sys.argv[1], sys.argv[2], sys.argv[3]
    status = {}
    for line in open(st):
        p, s, v = line.rstrip('\n').split('\t'); status[(p, s)] = v
    S = {}; tot = collections.Counter(); eq = collections.Counter(); faces = collections.Counter()
    for line in open(man):
        part, sld, path = line.rstrip('\n').split('\t')
        v = status.get((part, sld))
        if v is None: continue
        b = open(path, 'rb').read()
        m = re.search(rb'SCH=SCH_\d+_(\d+)', b[:3000])
        if not m: continue
        k = m.group(1).decode()
        if k not in S: S[k] = xt_parse.load_schema(os.path.join(sd, 'sch_%s.s_t' % k))
        try: nodes, _ = xt_parse.parse(b, S[k])
        except Exception: continue
        c = collections.Counter(S[k][n['code']]['name'] for n in nodes.values())
        good = v.endswith('EQUAL') or v.endswith('EQUAL_EXCEPT_FOREIGN_ATTRIBUTES')
        for t in TYPES:
            if c[t]:
                tot[t] += 1
                if good: eq[t] += 1; faces[t] += c[t]
    for t in TYPES: print('%-10s solids with it %4d, canonically equal %4d (%d nodes of the type in them)' % (t, tot[t], eq[t], faces[t]))

if __name__ == '__main__':
    main()
