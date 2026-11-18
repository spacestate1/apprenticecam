#!/usr/bin/env python3
"""sld_ops.py - write Mastercam solid history (ops) records and splice solids into a copy of a part (ours; spec:
formats/solid_history.md, formats/occt_xt.md "Proof").  Reading uses tools/sld_hist.py (read-only).

  sld_ops.py roundtrip PART...            parse every solid's ops block and re-serialise it; must be byte-identical
  sld_ops.py spec PART OUT.spec           the Extrude / Extrude Cut history of every solid, for occt/sld_regen
  sld_ops.py splice PART OUT N XT [--dirty 0|1]
                                          copy PART to OUT with solid N's XT replaced by the file XT (SOLIDXMT +0x168
                                          updated; id, name and ops block kept byte for byte)
  sld_ops.py addsolid PART OUT N_SRC N_NEW XT DIST [--dirty 0|1] [--records keep|none]
                                          copy PART to OUT plus a NEW solid N_NEW: SOLID_ID record (cloned from solid
                                          N_SRC's, new eptr / idn / id / bbox), SOLIDXMT + XT (file XT) + trailer,
                                          and a history of ONE Extrude op: solid N_SRC's first op with the distance
                                          replaced by DIST (chains, vector, chain data kept), records per --records

Serialisation = the inverse of the readers cited in tools/sld_hist.py (McOperation FUN_1802649f0 / write
FUN_180266560, McChainOp FUN_180271cc0, SldExtrudeOp FUN_18027bbe0 / write FUN_18027cb40, McOpsFile FUN_18026b3c0 /
write FUN_18026dc50). Only 64-bit files with ops version 0x1a / 0x1c (the user's parts)."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import sld_hist                     # noqa: E402
from mcam_walk import walk          # noqa: E402

def w_mceptr(e):
    return struct.pack('<QIIQQIBB', e['id'], e['kind'], e['op'], e['ref1'], e['ref2'], e['x28'], e['b2c'], e['b2d'])

def w_base(o):
    b = struct.pack('<i', len(o['ents'])) + b''.join(w_mceptr(e) for e in o['ents'])
    b += struct.pack('<i', len(o['ents2'])) + b''.join(w_mceptr(e) for e in o['ents2'])
    b += struct.pack('<i', len(o['ptrs3'])) + b''.join(struct.pack('<Q', p) for p in o['ptrs3'])
    nm = o['name'].encode('latin-1') + b'\0'               # u16 len = strlen + 1 (solid_history.md 2.3)
    b += struct.pack('<H', len(nm)) + nm
    b += struct.pack('<BBBBIQIIIBB', o['f9'], o['f10'], o['f11'], o['f12'], o['x10'], o['solid_eptr'], o['id'],
                     o['type'], o['ver'], o['x40'], o['x41'])
    return b

def w_chain(o):
    n = len(o['chains'])
    b = struct.pack('<Bi', o['x90'], n) + b''.join(struct.pack('<i', len(c)) for c in o['chains'])
    b += b''.join(struct.pack('<i', x) for x in o['chain_xa0'])
    b += b''.join(struct.pack('<Q', e) for c in o['chains'] for e in c)
    b += bytes(o['chain_xc0']) + bytes(o['chain_xa8'])
    b += b''.join(bytes(f) for f in o['chain_flags'])
    return b

def w_extrude(o):
    b = w_base(o) + w_chain(o) + bytes.fromhex(o['P_raw'])
    b += struct.pack('<i', len(o['trim_faces']))
    b += b''.join(struct.pack('<Q', x) for x in o['trim_faces']) + bytes.fromhex(o['trim_face_data'])
    b += b''.join(struct.pack('<12d', *cd) for cd in o['chain_data'])
    return b

def set_P(o, **kw):                 # edit SLD_EXTRUDE_PARAMS fields in P_raw (offsets: sld_hist.EXT_P)
    P = bytearray.fromhex(o['P_raw'])
    for k, v in kw.items():
        off, f = {n: (off, f) for n, off, f in sld_hist.EXT_P}[k]
        struct.pack_into('<' + ('3d' if f == '3d' else f), P, off, *(v if f == '3d' else (v,)))
    o['P_raw'] = P.hex(); o['P'][k] = v

def w_ops_block(hdr, ops):
    """McOpsFile write FUN_18026dc50: u8 dirty, u8 byte_e, u32 N, u32 active ops, u64 SOLID_ID eptr, u32 count, ops"""
    b = struct.pack('<BBIIQI', hdr['dirty_c'], hdr['byte_e'], hdr['solid_id'], hdr['nops_x14'], hdr['solid_eptr'], len(ops))
    for t, body in ops: b += struct.pack('<II', t, len(body)) + body
    return b

def serialise_op(t, body):
    try: o = sld_hist.parse_op(t, body)
    except Exception: return None                   # base layout of other op versions: not handled (kept)
    if t in (0, 1, 2) and o.get('consumed') and 'error' not in o: return w_extrude(o)
    return None                                     # other classes: kept as stored bytes

def cmd_roundtrip(paths):
    tot = same = kept = 0
    bad = 0
    for p in paths:
        d = open(p, 'rb').read()
        try: sols = list(sld_hist.solids(d))
        except Exception: bad += 1; continue        # file the walker cannot frame (reported, skipped)
        for rec, xt, sid, name, hdr, ops in sols:
            for t, sz, body in ops:
                tot += 1
                w = serialise_op(t, body)
                if w is None: kept += 1
                elif w == body: same += 1
                else: print('DIFF', p, sid, t)
    print(f'files {len(paths)} (unframeable {bad}); ops {tot}: re-serialised byte-identical {same}, other classes / '
          f'not parsed kept as stored {kept}, differing {tot - same - kept}')

def cmd_spec(part, out):
    d = open(part, 'rb').read()
    with open(out, 'w') as f:
        for rec, xt, sid, name, hdr, ops in sld_hist.solids(d):
            f.write(f'solid {sid}\n')
            for t, sz, body in ops:
                o = sld_hist.parse_op(t, body)
                if t not in (0, 1, 2): f.write(f'skip {t}\n'); continue
                P = o['P']
                f.write(f"op {t} {P['type']} {P['dist_method']} {P['distance']!r} {P['reverse']} {P['both']} {P['draft']} "
                        f"{P['thin_wall']} {P['trim_to_faces']} {len(o['chains'])}\n")
                for c, fl, cd in zip(o['chains'], o['chain_flags'], o['chain_data']):
                    f.write('chain %r %r %r %d\n' % (cd[0], cd[1], cd[2], len(c)))
                    for e, r in zip(c, fl): f.write(f'ent {e} {r}\n')
            f.write('end\n')

def xt_bbox(xt):
    """ours: box of the body in mm from its XT (POINT nodes + CIRCLE centre +- radius; part units = mm assumed, as the
    SOLID_ID +0x78 / +0x90 box of the user parts). Mastercam fills it from ps_get_body_bbox (solid_history.md 1.1)."""
    import re
    import xt_parse
    m = re.search(rb'SCH=SCH_\d+_(\d+)', xt[:4000])
    S = xt_parse.load_schema(os.path.expanduser('~/mastercam-re/files/sch_%s.s_t' % m.group(1).decode()))
    nodes, _ = xt_parse.parse(xt, S)
    pts = []
    for n in nodes.values():
        t = S[n['code']]['name']
        if t == 'POINT': pts.append(n['pvec'])
        if t == 'CIRCLE':
            for k in range(3):
                for sg in (-1, 1):
                    c = list(n['centre']); c[k] += sg * n['radius']; pts.append(c)
    lo = [min(p[k] for p in pts) * 1000 for k in range(3)]; hi = [max(p[k] for p in pts) * 1000 for k in range(3)]
    return lo, hi

def locate(d):
    """file offsets: SOLID_ID records (by N), SOLIDXMT segments (record offset, xt offset/size, ops start/end)"""
    sid_rec, segs, pend = {}, {}, None
    for off, n, rec in walk(d):
        if isinstance(rec, (bytes, bytearray)):
            cls = struct.unpack_from('<I', rec, 0)[0]
            if cls == 0x10000 and n >= 0xac: sid_rec[struct.unpack_from('<I', rec, 0xa8)[0]] = (off, n)
            if cls == 0x20000 and n >= 0x16c: pend = off
        elif rec[0] == 'xt':
            xo, xs, ver = off, n, rec[2]
        elif rec[0] == 'solid-ops':
            segs[rec[2]] = {'rec': pend, 'xt': (xo, xs), 'ver': ver, 'ops': (off, off + n)}
    return sid_rec, segs

def cmd_splice(part, out, N, xtpath, dirty=None):
    d = bytearray(open(part, 'rb').read()); xt = open(xtpath, 'rb').read()
    _, segs = locate(d)
    s = segs[N]; ro = s['rec']; xo, xs = s['xt']
    struct.pack_into('<I', d, ro + 4 + 0x168, len(xt))          # SOLIDXMT +0x168 xt_size (record after its u32 len)
    oo = s['ops'][0] - xs + len(xt)
    d = d[:xo] + xt + d[xo + xs:]
    if dirty is not None: d[oo] = dirty                           # ops header byte 0 = needs-regen (2.1)
    sid_rec, _ = locate(bytes(d))                                 # SOLID_ID +0x78 / +0x90 box from the new XT
    if N in sid_rec:
        lo, hi = xt_bbox(xt); so = sid_rec[N][0] + 4
        struct.pack_into('<3d', d, so + 0x78, *lo); struct.pack_into('<3d', d, so + 0x90, *hi)
    open(out, 'wb').write(d)

def cmd_addsolid(part, out, nsrc, nnew, xtpath, dist, dirty=0, records='none'):
    d = bytearray(open(part, 'rb').read()); xt = open(xtpath, 'rb').read()
    sid_rec, segs = locate(d)
    # eptrs / idns in use (every record's +0x20 / +0x34)
    eptrs, idns = set(), set()
    for off, n, rec in walk(bytes(d)):
        if isinstance(rec, (bytes, bytearray)) and n >= 0x38:
            eptrs.add(struct.unpack_from('<q', rec, 0x20)[0]); idns.add(struct.unpack_from('<i', rec, 0x34)[0])
    S = (max(eptrs) + 0x1000) & ~0xf                              # unique, 16-aligned (solid_history.md 5.1)
    idn = max(idns) + 1                                           # get_next_ent_idn = max + 1 (mcam_write.md)
    # 1. SOLID_ID: clone of solid nsrc's record, new eptr / idn / N; bbox from the new XT's points
    so, sn = sid_rec[nsrc]
    rec = bytearray(d[so + 4: so + 4 + sn])
    struct.pack_into('<q', rec, 0x20, S); struct.pack_into('<i', rec, 0x34, idn); struct.pack_into('<I', rec, 0xa8, nnew)
    lo, hi = xt_bbox(xt)
    struct.pack_into('<3d', rec, 0x78, *lo); struct.pack_into('<3d', rec, 0x90, *hi)
    rec[0xb0:] = bytes(len(rec) - 0xb0)                           # runtime pointers / stale tail: zero (5.4)
    ins_sid = so + 4 + sn
    # 2. section 13 segment: cloned SOLIDXMT record with the new key + size, XT, id, name, ops block
    s = segs[nsrc]; ro = s['rec']
    sx = bytearray(d[ro + 4: ro + 4 + 0x16c])
    sx[0x68:0x168] = (b'sld_%d' % nnew).ljust(0x100, b'\0'); struct.pack_into('<I', sx, 0x168, len(xt))
    nl = 0x100 if s['ver'] > 0x1b else 0x40
    trailer = struct.pack('<I', nnew) + bytes(nl)
    # history: the source solid's first op (an Extrude), distance replaced, op id 0, solid eptr = S
    hdr_src = list(sld_hist.solids(bytes(d)))
    src = [x for x in hdr_src if x[2] == nsrc][0]
    t, sz, body = src[5][0]
    o = sld_hist.parse_op(t, body)
    assert t == 2 and o['consumed'], 'source op must be an Extrude'
    set_P(o, distance=dist, target=0)
    o['solid_eptr'] = S; o['id'] = 0
    if records == 'none': o['ents'] = []; o['ents2'] = []; o['ptrs3'] = []
    else:                                                         # keep the source's names, ids made unique (ours)
        ids = {e['id'] for e in o['ents'] + o['ents2']}
        for e in o['ents'] + o['ents2']:
            e['id'] += 0x10000000
            if e['ref1'] in ids: e['ref1'] += 0x10000000
            if e['ref2'] in ids: e['ref2'] += 0x10000000
    ops = w_ops_block({'dirty_c': dirty, 'byte_e': src[4]['byte_e'], 'solid_id': nnew, 'nops_x14': 1, 'solid_eptr': S}, [(t, w_extrude(o))])
    seg = struct.pack('<I', 0x16c) + sx + xt + trailer + ops
    ins_seg = s['ops'][1]                                         # after the source solid's segment
    out_b = d[:ins_sid] + struct.pack('<I', sn) + rec + d[ins_sid:ins_seg] + seg + d[ins_seg:]
    open(out, 'wb').write(out_b)
    print(f'new solid N {nnew} eptr {S:#x} idn {idn} bbox {lo} {hi}')

if __name__ == '__main__':
    a = sys.argv
    opt = lambda k, dflt: a[a.index(k) + 1] if k in a else dflt
    if a[1] == 'roundtrip': cmd_roundtrip([x for x in a[2:]])
    elif a[1] == 'spec': cmd_spec(a[2], a[3])
    elif a[1] == 'splice': cmd_splice(a[2], a[3], int(a[4]), a[5], None if '--dirty' not in a else int(opt('--dirty', 0)))
    elif a[1] == 'addsolid': cmd_addsolid(a[2], a[3], int(a[4]), int(a[5]), a[6], float(a[7]), int(opt('--dirty', 0)), opt('--records', 'none'))
