#!/usr/bin/env python3
"""Extract RT_STRING resources from a PE as TSV id<TAB>text (English/first language). Usage: pe_strings.py FILE"""
import struct, sys

def sections(d):
    off = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, off + 6)[0]
    optsz = struct.unpack_from('<H', d, off + 0x14)[0]
    magic = struct.unpack_from('<H', d, off + 0x18)[0]
    dd = off + 0x18 + (96 if magic == 0x10b else 112)
    rsrc_rva = struct.unpack_from('<I', d, dd + 2 * 8)[0]
    secs = [struct.unpack_from('<8sIIII', d, off + 0x18 + optsz + i * 40) for i in range(nsec)]
    def rva2off(rva):
        for _, vsz, va, rsz, rptr in secs:
            if va <= rva < va + max(vsz, rsz):
                return rptr + rva - va
    return rsrc_rva, rva2off

def entries(d, base, off):
    nnamed, nid = struct.unpack_from('<HH', d, off + 12)
    for i in range(nnamed + nid):
        name, ptr = struct.unpack_from('<II', d, off + 16 + i * 8)
        yield name, ptr

d = open(sys.argv[1], 'rb').read()
rsrc_rva, rva2off = sections(d)
base = rva2off(rsrc_rva)
for tid, tptr in entries(d, base, base):
    if tid != 6 or not tptr & 0x80000000:
        continue
    for bid, bptr in entries(d, base, base + (tptr & 0x7fffffff)):
        langs = list(entries(d, base, base + (bptr & 0x7fffffff)))
        _, lptr = langs[0]
        rva, size = struct.unpack_from('<II', d, base + lptr)
        o = rva2off(rva)
        for i in range(16):
            n = struct.unpack_from('<H', d, o)[0]; o += 2
            if n:
                s = d[o:o + 2 * n].decode('utf-16-le', 'replace').replace('\t', '\\t').replace('\n', '\\n').replace('\r', '')
                print(f'{(bid - 1) * 16 + i}\t{s}')
            o += 2 * n
