#!/usr/bin/env python3
"""Extract MENU, TOOLBAR (+ their BITMAP strips) and STRING resources of an MFC resource DLL to JSON + PNG icons.

Usage: ui_res_extract.py RES.dll OUT.json ICON_DIR
  menus:    {id: [items]}  item = {text, id} | {text, popup: [items]} | {separator: true}
  toolbars: {id: {size: [w, h], buttons: [{id, icon, tip}] }}   (id 0 = separator)
  strings:  {id: text}     (MFC command strings: "status bar prompt\\ntooltip")
"""
import io, json, os, struct, sys
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_resources import walk

def utf16z(b, o):
    e = o
    while b[e:e + 2] != b'\0\0':
        e += 2
    return b[o:e].decode('utf-16-le', 'replace'), e + 2

def parse_menu(b):
    ver, off = struct.unpack_from('<HH', b, 0)
    o = 4 + off
    if ver == 1:                                      # MENUEX: not used by these DLLs
        return None

    def items(o):
        out = []
        while o < len(b):
            flags = struct.unpack_from('<H', b, o)[0]; o += 2
            if flags & 0x10:                          # MF_POPUP
                text, o = utf16z(b, o)
                sub, o = items(o)
                out.append({'text': text, 'popup': sub})
            else:
                mid = struct.unpack_from('<H', b, o)[0]; o += 2
                text, o = utf16z(b, o)
                out.append({'separator': True} if not text and mid == 0 else {'text': text, 'id': mid})
            if flags & 0x80:                          # MF_END
                break
        return out, o
    return items(o)[0]

def bmp_image(raw):
    """RT_BITMAP data is a DIB without BITMAPFILEHEADER: prepend one so PIL can read it."""
    hdr_size = struct.unpack_from('<I', raw, 0)[0]
    bits = struct.unpack_from('<H', raw, 14)[0]
    clr = struct.unpack_from('<I', raw, 32)[0] if hdr_size >= 36 else 0
    ncol = clr or (1 << bits if bits <= 8 else 0)
    off = 14 + hdr_size + 4 * ncol
    fh = b'BM' + struct.pack('<IHHI', 14 + len(raw), 0, 0, off)
    return Image.open(io.BytesIO(fh + raw))

def main():
    dll, out_json, icon_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(icon_dir, exist_ok=True)
    d, res = walk(dll)
    by = {}
    for p, off, size in res:
        by.setdefault(p[0], {})[p[1]] = d[off:off + size]
    strings = {}
    for bid, blob in by.get(6, {}).items():           # string tables: 16 strings per block, id = (block-1)*16 + i
        o = 0
        for i in range(16):
            if o + 2 > len(blob):
                break
            n = struct.unpack_from('<H', blob, o)[0]; o += 2
            if n:
                strings[(bid - 1) * 16 + i] = blob[o:o + 2 * n].decode('utf-16-le', 'replace')
            o += 2 * n
    menus = {}
    for mid, blob in by.get(4, {}).items():
        try:
            menus[str(mid)] = parse_menu(blob)
        except Exception as e:
            menus[str(mid)] = {'error': str(e)}
    toolbars = {}
    stem = os.path.splitext(os.path.basename(dll))[0]
    for tid, blob in by.get(241, {}).items():
        ver, w, h, n = struct.unpack_from('<4H', blob, 0)
        ids = struct.unpack_from(f'<{n}H', blob, 8)
        strip = None
        if tid not in by.get(2, {}) and tid in by.get('PNG', {}):      # newer toolbars ship a PNG strip instead
            try:
                strip = Image.open(io.BytesIO(by['PNG'][tid])).convert('RGBA')
            except Exception:
                strip = None
        if strip is None and tid in by.get(2, {}):
            try:
                strip = bmp_image(by[2][tid]).convert('RGBA')
                # classic toolbar bitmaps use magenta / light grey (192,192,192) as the transparent colour
                px = strip.load()
                key = px[0, 0]
                for yy in range(strip.height):
                    for xx in range(strip.width):
                        c = px[xx, yy]
                        if c[:3] in ((255, 0, 255), (192, 192, 192)) or (c[:3] == key[:3] and key[:3] in ((255, 0, 255),)):
                            px[xx, yy] = (0, 0, 0, 0)
            except Exception:
                strip = None
        buttons, k = [], 0
        for cid in ids:
            if cid == 0:
                buttons.append({'id': 0}); continue
            b = {'id': cid, 'tip': strings.get(cid)}
            if strip is not None and (k + 1) * w <= strip.width:
                name = f'{stem}_tb{tid}_{k}.png'
                strip.crop((k * w, 0, (k + 1) * w, min(h, strip.height))).save(os.path.join(icon_dir, name))
                b['icon'] = name
            k += 1
            buttons.append(b)
        toolbars[str(tid)] = {'size': [w, h], 'buttons': buttons}
    json.dump({'menus': menus, 'toolbars': toolbars, 'strings': {str(k): v for k, v in sorted(strings.items())}},
              open(out_json, 'w'), indent=1)
    print(f'{len(menus)} menus, {len(toolbars)} toolbars, {len(strings)} strings -> {out_json}')

if __name__ == '__main__':
    main()
