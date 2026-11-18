"""Read Windows Installer (.msi) database tables from the streams 7-Zip extracts out of the .msi (!_StringPool,
!_StringData, !_Columns, !<Table>). No dependencies. load(DIR) -> table(NAME) -> list of {column: value} rows
(string columns as str, integers as int / None). Layout: the string pool (u16 length / u16 refcount pairs after a
codepage word; bit 31 of it = 3-byte string references), tables stored column by column, integers offset by 0x8000 /
0x80000000."""
import os, struct, sys
def load(d):
    rd = lambda n: open(os.path.join(d, n), 'rb').read()
    pool, data = rd('!_StringPool'), rd('!_StringData')
    cp, = struct.unpack_from('<I', pool, 0); big = bool(cp & 0x80000000)
    strs = [None]; off = 0; i = 4
    while i + 4 <= len(pool):
        ln, rc = struct.unpack_from('<HH', pool, i); i += 4
        if ln == 0 and rc != 0:                      # long string: next entry holds the length
            ln, = struct.unpack_from('<I', pool, i); i += 4
        strs.append(data[off:off + ln].decode('cp1252', 'replace')); off += ln
    sw = 3 if big else 2
    cols = rd('!_Columns'); n = len(cols) // (sw + 2 + sw + 2)
    # _Columns: Table(s) Number(i2) Name(s) Type(i2), column-major
    o = 0; T = []; N = []; NM = []; TY = []
    T = [int.from_bytes(cols[o + r*sw:o + r*sw + sw], 'little') for r in range(n)]; o += n*sw
    N = [struct.unpack_from('<H', cols, o + 2*r)[0] - 0x8000 for r in range(n)]; o += 2*n
    NM = [int.from_bytes(cols[o + r*sw:o + r*sw + sw], 'little') for r in range(n)]; o += n*sw
    TY = [struct.unpack_from('<H', cols, o + 2*r)[0] - 0x8000 for r in range(n)]
    tables = {}
    for t, num, nm, ty in zip(T, N, NM, TY): tables.setdefault(strs[t], []).append((num, strs[nm], ty))
    def table(name):
        cs = sorted(tables[name]); b = rd('!' + name)
        def w(ty):
            if ty & 0x0800: return sw                 # string
            return 4 if (ty & 0xff) == 4 else 2
        rw = sum(w(ty) for _, _, ty in cs); rows = len(b) // rw; out = [dict() for _ in range(rows)]; o = 0
        for _, cn, ty in cs:
            k = w(ty)
            for r in range(rows):
                v = int.from_bytes(b[o + r*k:o + r*k + k], 'little')
                if ty & 0x0800: v = strs[v] if v else ''
                elif k == 2: v = v - 0x8000 if v else None
                else: v = v - 0x80000000 if v else None
                out[r][cn] = v
            o += rows * k
        return out
    return table
if __name__ == '__main__':
    t = load(sys.argv[1])
    for r in t(sys.argv[2])[:int(sys.argv[3]) if len(sys.argv) > 3 else 10]: print(r)
