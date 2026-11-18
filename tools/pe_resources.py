#!/usr/bin/env python3
"""List (and optionally extract) the Win32 resources of a PE file without third-party modules.

Usage: pe_resources.py FILE.dll [--extract OUTDIR] [--type NAME_OR_ID]
Prints one line per resource: type / name / language / size. Standard types are shown by name (RT_BITMAP, ...).
"""
import os, struct, sys

RT = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING', 7: 'FONTDIR', 8: 'FONT',
      9: 'ACCELERATOR', 10: 'RCDATA', 11: 'MESSAGETABLE', 12: 'GROUP_CURSOR', 14: 'GROUP_ICON', 16: 'VERSION',
      17: 'DLGINCLUDE', 19: 'PLUGPLAY', 20: 'VXD', 21: 'ANICURSOR', 22: 'ANIICON', 23: 'HTML', 24: 'MANIFEST',
      240: 'DLGINIT', 241: 'TOOLBAR'}

def sections(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    ddir = pe + 24 + (96 if magic == 0x10b else 112)
    rsrc_rva, rsrc_size = struct.unpack_from('<II', d, ddir + 2 * 8)
    secs = []
    so = pe + 24 + optsz
    for i in range(nsec):
        name, vsize, va, rsize, rptr = struct.unpack_from('<8sIIII', d, so + i * 40)
        secs.append((va, max(vsize, rsize), rptr))
    return rsrc_rva, secs

def rva2off(rva, secs):
    for va, size, ptr in secs:
        if va <= rva < va + size:
            return rva - va + ptr
    return None

def walk(path):
    d = open(path, 'rb').read()
    rsrc_rva, secs = sections(d)
    base = rva2off(rsrc_rva, secs)
    if base is None:
        return d, []
    out = []

    def name_of(entry_name):
        if entry_name & 0x80000000:
            o = base + (entry_name & 0x7fffffff)
            n = struct.unpack_from('<H', d, o)[0]
            return d[o + 2:o + 2 + 2 * n].decode('utf-16-le')
        return entry_name

    def directory(off, level, path_):
        nnamed, nid = struct.unpack_from('<HH', d, off + 12)
        for i in range(nnamed + nid):
            name, target = struct.unpack_from('<II', d, off + 16 + 8 * i)
            nm = name_of(name)
            if target & 0x80000000:
                directory(base + (target & 0x7fffffff), level + 1, path_ + [nm])
            else:
                rva, size = struct.unpack_from('<II', d, base + target)
                out.append((path_ + [nm], rva2off(rva, secs), size))
    directory(base, 0, [])
    return d, out

def main():
    path = sys.argv[1]
    d, res = walk(path)
    want = sys.argv[sys.argv.index('--type') + 1] if '--type' in sys.argv else None
    outdir = sys.argv[sys.argv.index('--extract') + 1] if '--extract' in sys.argv else None
    for p, off, size in res:
        t = p[0]
        tname = RT.get(t, str(t)) if isinstance(t, int) else t
        if want and want not in (tname, str(t)):
            continue
        print(f'{tname}\t{p[1] if len(p) > 1 else ""}\t{p[2] if len(p) > 2 else ""}\t{size}')
        if outdir and off is not None:
            os.makedirs(os.path.join(outdir, str(tname)), exist_ok=True)
            open(os.path.join(outdir, str(tname), f'{p[1]}'), 'wb').write(d[off:off + size])

if __name__ == '__main__':
    main()
