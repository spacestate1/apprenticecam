#!/usr/bin/env python3
"""dotnet_resources.py - list / extract the embedded .resources blobs of a .NET assembly (pure Python, no .NET needed).

Purpose: the Mastercam Simulator UI (ubvs.dll, a WPF assembly; formats/simulator.md) keeps its icons in the WPF
"<Name>.g.resources" blob as Stream entries ("resources/icons/verify32.png", ...). tools/extract_icons.py uses
read_resources() to put them into the icon pack (built locally from the user's own installation, never committed).

Format (the .NET System.Resources.ResourceReader layout, "ResourceManager header" + "RuntimeResourceSet" v2):
  magic 0xBEEFCACE, header version, skip-bytes count + skipped reader/set type names; version 2; resource count;
  type count + 7-bit-length-prefixed type names; padding to 8; count x name hash; count x name position;
  data section offset; name section (7-bit length, UTF-16LE name, int32 data offset); data (7-bit type code, value).
Type codes used here: 1 String (7-bit length + UTF-8), 0x20 ByteArray / 0x21 Stream (int32 length + bytes); other
codes are listed but not decoded. Embedded resource blobs are found by their magic (each is preceded by its int32
length in the assembly's resources directory).

Usage: dotnet_resources.py ASSEMBLY [--list] [--out DIR]
"""
import os, struct, sys


def _r7(d, p):
    v = s = 0
    while True:
        b = d[p]; p += 1; v |= (b & 0x7f) << s; s += 7
        if not b & 0x80:
            return v, p


def parse_blob(d, base):
    """one .resources blob at d[base:] -> list of (name, type_code, bytes or str)"""
    p = base
    magic, hver, skip = struct.unpack_from('<III', d, p); p += 12
    if magic != 0xBEEFCACE:
        return []
    p += skip
    ver, n, ntypes = struct.unpack_from('<III', d, p); p += 12
    for _ in range(ntypes):
        l, p = _r7(d, p); p += l
    p += (-(p - base)) & 7
    p += 4 * n                                         # hashes
    pos = struct.unpack_from('<%dI' % n, d, p); p += 4 * n
    data_off, = struct.unpack_from('<I', d, p); p += 4
    names_start = p
    out = []
    for i in range(n):
        q = names_start + pos[i]
        l, q = _r7(d, q); name = d[q:q + l].decode('utf-16-le'); q += l
        off, = struct.unpack_from('<I', d, q)
        r = base + data_off + off
        tc, r = _r7(d, r)
        if tc == 1:
            l, r = _r7(d, r); val = d[r:r + l].decode('utf-8')
        elif tc in (0x20, 0x21):
            l, = struct.unpack_from('<I', d, r); val = d[r + 4:r + 4 + l]
        else:
            val = None
        out.append((name, tc, val))
    return out


def read_resources(path):
    """every resource entry of every embedded .resources blob of the assembly at PATH"""
    d = open(path, 'rb').read()
    out, p = [], 0
    while True:
        p = d.find(b'\xce\xca\xef\xbe', p)
        if p < 0:
            return out
        try:
            out += parse_blob(d, p)
        except (struct.error, IndexError, UnicodeDecodeError):
            pass
        p += 4


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); sys.exit(2)
    res = read_resources(a[0])
    outdir = a[a.index('--out') + 1] if '--out' in a else None
    for name, tc, val in res:
        if '--list' in a or not outdir:
            print('%-60s type 0x%x %s' % (name, tc, len(val) if isinstance(val, (bytes, str)) else '-'))
        if outdir and isinstance(val, bytes):
            fp = os.path.join(outdir, name.replace('\\', '/'))
            os.makedirs(os.path.dirname(fp) or outdir, exist_ok=True)
            open(fp, 'wb').write(val)


if __name__ == '__main__':
    main()
