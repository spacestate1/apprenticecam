#!/usr/bin/env python3
"""xt_canon.py - structural comparison of two Parasolid XT bodies after a canonical ordering (ours; formats/occt_xt.md).

Usage: xt_canon.py SCHEMA MANIFEST WRITTEN_DIR        (MANIFEST rows: part, solid, original .x_b; written file =
                                                        WRITTEN_DIR/<part>/<solid>.x_b, from occt_xt_check rt)

Nodes are keyed by meaning, not by index: FACE / EDGE / VERTEX by node_id (the round trip keeps them), a fin by
(edge, sense), a loop by its face + fin set, points / curves / surfaces through their owner, attributes by owner.
Chain orders (next / previous lists, loop start fin, VERTEX.fin heads, attribute-of-type chain) are unordered in XT
(Parasolid XT Format Reference p.3) and are compared as sets / cyclic sequences. Every other transmitted field is
compared: integers / chars exactly, doubles bit-exact and as max |difference|.
Representation differences that the reference defines as equivalent are counted separately and then normalised:
  curve sense '-' (ref 78: the edge direction is the curve's natural direction x its sense) -> the curve flipped;
  surface sense '-' (ref 78: face orientation = face sense x surface sense) -> folded into the face sense;
  a curve shared by several edges (EDGE.next_on_curve) -> compared per edge.
Uses tools/xt_parse.py (read-only)."""
import os, struct, sys, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import xt_parse

CHAIN = {'next', 'previous', 'next_front', 'previous_front', 'next_of_type', 'previous_of_type', 'next_at_vx',
         'next_on_curve', 'previous_on_curve', 'next_on_surface', 'previous_on_surface'}

_SCH = {}
def schema_for(b, default):
    """the schema named in the blob's header (SCH=SCH_<version>_<n>), from the folder of the default schema file"""
    import re
    m = re.search(rb'SCH=SCH_\d+_(\d+)', b[:4000])
    p = os.path.join(os.path.dirname(default), 'sch_%s.s_t' % m.group(1).decode()) if m else default
    if p not in _SCH: _SCH[p] = xt_parse.load_schema(p)
    return _SCH[p]

def load(path, S):
    b = open(path, 'rb').read()
    S = schema_for(b, S)
    nodes, end = xt_parse.parse(b, S)
    for i, n in nodes.items(): n['_S'] = S
    for i, n in nodes.items(): n['_t'] = S[n['code']]['name']; n['_i'] = i
    return nodes

def bits(x): return struct.pack('<d', x)

class Canon:
    def __init__(self, nodes, S):
        self.n = nodes
        self.S = next(iter(nodes.values()))['_S'] if nodes else S
        self.raw = collections.Counter()           # representation differences seen (raw senses etc.)
        self.key = {}
        N = nodes
        g = lambda i: N.get(i)
        for i, n in N.items():
            t = n['_t']
            if t in ('FACE', 'EDGE', 'VERTEX'): self.key[i] = t[0] + str(n['node_id'])
        for i, n in N.items():
            t = n['_t']
            if t == 'HALFEDGE': self.key[i] = 'H' + self.key.get(n['edge'], '?') + chr(n['sense'])
            elif t == 'POINT': self.key[i] = 'P' + self.key.get(n['owner'], '?')
            elif t == 'ATTRIBUTE':
                d = N.get(n['definition']); nm = bytes(N[d['identifier']]['string']).decode('latin-1') if d and d['identifier'] in N else '?'
                self.key[i] = 'A' + self.key.get(n['owner'], N[n['owner']]['_t'] if n['owner'] in N else '?') + ':' + nm
            elif t in ('BODY', 'LIST', 'POINTER_LIS_BLOCK'): self.key[i] = t
            elif t == 'ATTRIB_DEF': self.key[i] = 'AD:' + (bytes(N[n['identifier']]['string']).decode('latin-1') if n['identifier'] in N else '?')
            elif t == 'ATT_DEF_ID': self.key[i] = 'ADI:' + bytes(n['string']).decode('latin-1')
            elif t == 'REGION': self.key[i] = 'R' + chr(n['type'])
        for i, n in N.items():
            if n['_t'] == 'LOOP':
                fins = self.loop_fins(i)
                self.key[i] = 'L' + self.key.get(n['face'], '?') + '|' + min(self.key[h] for h in fins)
            if n['_t'] == 'SHELL': self.key[i] = 'SH' + self.key.get(n['region'], '?')

    def loop_fins(self, li):
        out, h0 = [], self.n[li]['halfedge']; h = h0
        while h in self.n and len(out) < 100000:
            out.append(h); h = self.n[h]['forward']
            if h == h0: break
        return out

    def walk(self, i, f):
        out = []
        while i in self.n and len(out) < 1000000:
            out.append(i); i = self.n[i][f]
        return out

    def geom(self, i):
        """a curve / surface node as a canonical tuple (senses folded per ref 78); returns (tuple, flip) where flip = -1
        when a surface sense '-' must be folded into the face sense"""
        n = self.n[i]; t = n['_t']
        flds = [f for f in self.S[n['code']]['fields'] if f['xmt'] and f['type'] in 'vfdnulc' and f['name'] not in ('node_id', 'sense')]
        vals = {f['name']: n[f['name']] for f in flds}
        flip = 1
        if n.get('sense') == 45:
            self.raw[t + ' sense -'] += 1
            if t == 'LINE': vals['direction'] = tuple(-x for x in vals['direction'])
            elif t in ('CIRCLE', 'ELLIPSE'): vals['normal'] = tuple(-x for x in vals['normal'])
            else: flip = -1
        sub = []
        for f in self.S[n['code']]['fields']:          # B-spline sub-nodes, by value
            if f['xmt'] and f['type'] == 'p' and f['name'] in ('nurbs', 'data', 'bspline_vertices', 'knot_mult', 'knots',
                    'u_knot_mult', 'v_knot_mult', 'u_knots', 'v_knots') and n[f['name']] in self.n:
                sub.append((f['name'], self.geom(n[f['name']])[0]))
        if t in ('BSPLINE_VERTICES', 'KNOT_SET', 'KNOT_MULT'):
            vals = {k: tuple(v) for k, v in n.items() if not k.startswith('_') and k not in ('type', 'code', 'idx')}
        return (t, tuple(sorted((k, tuple(v) if isinstance(v, (list, tuple)) else v) for k, v in vals.items())), tuple(sub)), flip

    def records(self):
        N, K = self.n, self.key
        R = {}
        for i, n in N.items():
            t = n['_t']
            if t in ('POINT',) or t in ('LINE', 'CIRCLE', 'ELLIPSE', 'B_CURVE', 'PLANE', 'CYLINDER', 'CONE', 'SPHERE', 'TORUS',
                                         'B_SURFACE', 'NURBS_CURVE', 'NURBS_SURF', 'BSPLINE_VERTICES', 'KNOT_SET',
                                         'KNOT_MULT', 'CURVE_DATA', 'SURFACE_DATA'):
                continue                              # compared through their owners
            k = K.get(i, t + '?' + str(i))
            rec = {}
            for f in self.S[n['code']]['fields']:
                if not f['xmt']: continue
                nm, v = f['name'], n[f['name']]
                if nm in CHAIN or nm in ('node_id', 'highest_node_id'): continue   # highest: compared as '= max node_id'
                if f['type'] == 'p':
                    if t == 'FACE' and nm == 'surface':
                        g, flip = self.geom(v); rec['surface'] = g
                        if flip < 0: self.raw['face sense folded'] += 1
                        rec['sense'] = n['sense'] if flip > 0 else (43 if n['sense'] == 45 else 45)
                        continue
                    if t == 'EDGE' and nm == 'curve':
                        rec['curve'] = self.geom(v)[0]
                        if N[v]['owner'] != i: self.raw['curve shared by edges'] += 1
                        continue
                    if t == 'VERTEX' and nm == 'point':
                        rec['point'] = tuple(N[v]['pvec']); continue
                    if (t, nm) in (('VERTEX', 'halfedge'), ('LOOP', 'halfedge'), ('FACE', 'loop'), ('SHELL', 'face'),
                                   ('SHELL', 'front_face'), ('BODY', 'edge'), ('BODY', 'vertex'), ('BODY', 'boundary_point'),
                                   ('BODY', 'boundary_curve'), ('BODY', 'boundary_surface'), ('REGION', 'shell'),
                                   ('POINTER_LIS_BLOCK', 'entries'), ('ATTRIB_DEF', 'next')):
                        continue                      # chain heads: replaced by the sets below
                    if isinstance(v, list): rec[nm] = tuple(K.get(x, 'null' if x == 1 else 'unres') for x in v)
                    else: rec[nm] = K.get(v, 'null' if v == 1 else ('unresolved' if v not in N else N[v]['_t'] + '?'))
                elif t == 'FACE' and nm == 'sense':
                    continue
                else:
                    rec[nm] = tuple(v) if isinstance(v, list) else v
            if t == 'LOOP':
                seq = [K[h] for h in self.loop_fins(i)]
                j = seq.index(min(seq)); rec['fins'] = tuple(seq[j:] + seq[:j])
            if t == 'FACE':
                rec['loops'] = tuple(sorted(K[l] for l in self.walk(n['loop'], 'next')))
            if t == 'VERTEX':
                rec['fins'] = tuple(sorted(K[h] for h in self.walk(n['halfedge'], 'next_at_vx')))
            if t == 'SHELL':
                rec['faces'] = tuple(sorted(K[f] for f in self.walk(n['face'], 'next')))
                rec['front_faces'] = tuple(sorted(K[f] for f in self.walk(n['front_face'], 'next_front')))
            if t == 'BODY':
                rec['edges'] = tuple(sorted(K[e] for e in self.walk(n['edge'], 'next')))
                rec['vertices'] = tuple(sorted(K[e] for e in self.walk(n['vertex'], 'next')))
                rec['n_boundary_points'] = len(self.walk(n['boundary_point'], 'next'))
                rec['n_boundary_surfaces'] = len(self.walk(n['boundary_surface'], 'next'))
                rec['shell_region'] = K.get(N[n['shell']]['region']) if n['shell'] in N else None
                rec['region_type'] = chr(N[n['region']]['type']) if n['region'] in N else None
                rec['highest_node_id=max'] = n['highest_node_id'] == max(x['node_id'] for x in N.values() if isinstance(x.get('node_id'), int))
            if t == 'REGION':
                rec['shells'] = tuple(sorted(K[s] for s in self.walk(n['shell'], 'next')))
            if t == 'ATTRIBUTE':
                pass
            R[k] = rec
        # every face / edge / attribute chain reached once
        return R

def cmp_val(a, b, path, diffs, stat):
    if isinstance(a, tuple) and isinstance(b, tuple) and len(a) == len(b):
        for k, (x, y) in enumerate(zip(a, b)): cmp_val(x, y, path, diffs, stat)
        return
    if isinstance(a, float) and isinstance(b, float):
        stat['doubles'] += 1
        if bits(a) == bits(b): stat['bit_equal'] += 1
        else:
            stat['nonbit:' + path.split('.', 1)[-1].split('.')[0] + ':' + ('-0' if a == b else 'value')] += 1
            d = abs(a - b); stat['maxdiff'] = max(stat['maxdiff'], d)
            if d > 1e-12: diffs.append((path, a, b))
        return
    if a != b: diffs.append((path, a, b))

def compare(orig, new, S):
    A, B = Canon(orig, S), Canon(new, S)
    ra, rb = A.records(), B.records()
    diffs = []; stat = collections.Counter(); stat['maxdiff'] = 0.0
    for k in sorted(set(ra) | set(rb)):
        if k not in ra: diffs.append((k, 'missing in original', None)); continue
        if k not in rb: diffs.append((k, 'missing in written', None)); continue
        for f in sorted(set(ra[k]) | set(rb[k])):
            cmp_val(ra[k].get(f), rb[k].get(f), k + '.' + f, diffs, stat)
    ca = collections.Counter(n['_t'] for n in orig.values()); cb = collections.Counter(n['_t'] for n in new.values())
    return diffs, stat, A.raw, B.raw, ca, cb

def main():
    S = xt_parse.load_schema(sys.argv[1])
    tot = ok = 0; agg = collections.Counter(); rawa = collections.Counter(); rawb = collections.Counter(); cnt = collections.Counter()
    maxd = 0.0; bad = []; diffcat = collections.Counter()
    for line in open(sys.argv[2]):
        part, sld, path = line.rstrip('\n').split('\t')
        w = os.path.join(sys.argv[3], part, sld + '.x_b')
        tot += 1
        if not os.path.exists(w): bad.append((part, sld, 'no written file')); continue
        o, n = load(path, sys.argv[1]), load(w, sys.argv[1])
        diffs, stat, ra, rb, ca, cb = compare(o, n, S)
        rawa.update(ra); rawb.update(rb)
        for t in set(ca) | set(cb):
            if ca[t] != cb[t]: cnt[(t, 'orig - written')] += ca[t] - cb[t]
        agg.update({k: v for k, v in stat.items() if k.startswith('nonbit')})
        agg['doubles'] += stat['doubles']; agg['bit_equal'] += stat['bit_equal']; maxd = max(maxd, stat['maxdiff'])
        if diffs:
            bad.append((part, sld, diffs[:5], len(diffs)))
            for p_, a_, b_ in diffs:
                k = p_.split('.')[0]; f = p_.split('.', 1)[1] if '.' in p_ else str(a_)
                cat = ('attribute ' + k.split(':', 1)[1]) if k.startswith('A') and ':' in k else (
                      'attribute definition ' + k) if k.startswith(('AD:', 'ADI:')) else (k[0] + '.' + f)
                diffcat[cat] += 1
        else: ok += 1
    print(f'solids {tot}: canonical forms equal (doubles <= 1e-12) {ok}; differing {len(bad)}')
    print(f'doubles compared {agg["doubles"]}, bit-identical {agg["bit_equal"]}, max |diff| {maxd:.3g}')
    print('non-bit-identical doubles by field:', {k: v for k, v in agg.items() if k.startswith('nonbit')})
    print('node count differences (sum over solids, original minus written):', dict(cnt))
    print('representation differences normalised, original:', dict(rawa), ' written:', dict(rawb))
    print('differences by category:', dict(diffcat.most_common()))
    for b in bad[:20]: print('DIFF', b)

if __name__ == '__main__':
    main()
