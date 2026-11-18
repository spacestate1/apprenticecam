#!/usr/bin/env python3
"""Build apprenticeCAM's icon pack from YOUR OWN Mastercam 2017 installation.

The repository ships no Mastercam artwork. This script reads the resource DLLs of an installed (licensed) Mastercam,
cuts out the images the program uses and writes them all into ONE pack file. the program (viewer-qt icons.cpp) loads that file at start-up;
without it every command shows a generated letter tile instead.

Usage:  extract_icons.py MASTERCAM_DIR [OUT.pack]
  MASTERCAM_DIR  folder holding ribbonuires.dll, opmanres.dll, mccoreres.dll, mccadutilres.dll (the Mastercam
                 program folder, e.g. "C:\\Program Files\\mcam2017" mounted under Linux)
  OUT.pack       default <config dir>/icons.pack - Linux ~/.config/mcamui, Windows %APPDATA%\\apprenticeCAM (where
                 the program looks; MCAMUI_ICON_PACK overrides)

Images produced (names = what viewer-qt/src/icons.cpp (formerly viewer/mcamui.c) and viewer/ui/icon_map.tsv ask for):
  ribbonuires  BCGP ribbon strips cut per image index (tools/ribbon_extract.py: IDR_BCGP_IMAGE_<strip>_<i>.png,
               gallery IDR_BCGP_IMAGE_PALETTE_*), plus every PNG resource whole as <NAME>.png
  opmanres     Toolpaths-tree image list OPMANRES_TREE_<i>.png (COpMgrTree::LoadImageList: 65 bitmaps, white = clear),
               toolbars opmanres_tb<ID>_<k>.png (TOOLBAR resource cell size; PNG strip, else BITMAP with 192,192,192
               and 255,0,255 = clear), toolbar PNG strips OPMANRES_PNG_<ID>.png
  backplotres  VCR bar toolbar backplotres_tb30000_<k>.png
  mccoreres / mccadutilres / opmanres / backplotres / mcmillres / mcmsurfres / uictrls   every BITMAP as <dll>_BMP_<ID>.png (Analyze
               dialog images, the toolpath parameter dialog's page pictures and the like); for the bitmaps Mastercam draws
               with transparency colour 192,192,192 (UIBitmap::SetTransparencyColor 0xc0c0c0, ImageList_AddMasked
               0xc0c0c0) also <dll>_BMPK_<ID>.png with that colour clear
  mcmillres    every PNG resource as mcmillres_PNG_<ID>.png (UIPNGImage pictures, e.g. the Lead In/Out page)
  uictrls      GROUP_ICON 7021 / 7022 as uictrls_ICO_<ID>.png (UIVertDialog title-bar expand / contract icon)
  ubvs.dll     the Simulator's WPF icons (UBVS.g.resources "resources/icons/*", tools/dotnet_resources.py) as
               ubvs_<lower-case file name>.png (.ico: the largest frame)

Pack format (little endian): b"MCUIICN1", u32 count, then per image: u16 name length, name (utf-8),
u32 data length, PNG bytes. Needs Pillow (python3 -m pip install pillow).
"""
import io, os, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from apppaths import config_dir
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TREE_IDS = [0x76, 0x78, 0x61da, 0x7e, 0x6e, 0x61c8, 0x71, 0x61d5, 0xc6, 0x92, 0x61b3, 0xc9, 0x73, 0x7f, 0x106, 0x108,
            0x61b4, 0x138a, 0x7a, 0x209, 0x106, 0x61db, 0x61dd, 0x6401, 0x205, 0x207, 0x61dc, 0x61de, 0x6402, 0x221,
            0x226, 0x61c6, 0x61bc, 0x7d9, 0x7da, 0x7db, 0x24b, 0x7dd, 0x7dc, 0x255, 0x309, 0x30a, 0x30f, 0x310, 0x41c,
            0x311, 0x312, 0x313, 0x314, 0x30e, 0x315, 0x31c, 0xd27, 0xd28, 0x31d, 0xd29, 0x336, 0x61d1, 0x41b, 0x453,
            0x456, 0x6405, 0x6406, 0x640d, 0x6410]   # COpMgrTree::LoadImageList order (image index = position)

# bitmaps Mastercam draws with 192,192,192 transparent (-> <dll>_BMPK_<ID>.png; None = every bitmap of the DLL):
# mccoreres ToolpathDlg::OnInitDialog tree image list 1477 / 1509 / 1498 (ImageList_AddMasked 0xc0c0c0) and the
# InitToolBar / AddDefaultsButtons images; uictrls UIDialog::SetBitmapButtons (SetTransparencyColor 0xc0c0c0);
# mcmillres page pictures whose UIBitmap gets SetTransparent + 0xc0c0c0 (HSTDrillLinkPage / DepthCutsPageBase /
# HST2DCutParametersPageBase stock picture / HSTMultiPassPage / HSTTabPage / pocket rough + finish InitPage); the Plane Selection
# dialog's UIBitmapButtons (mccore CPlaneDlg FUN_180238ff0: SetTransparent + SetTransparencyColor(0xc0c0c0), formats/plane_selection.md)
KEYED = {'mccoreres': {'1477', '1509', '1498', '5699', '9430', '9431', '9433', '9435', '10320', '10501',
                       '5673', '5674', '5675', '5676', '1123', '1124', '1126', '1197', '1198', '1199', '1200'},   # + CPlaneDlg FUN_180238ff0 (Plane Selection 1085)
         'opmanres': {'3451'},   # CPlaneDlg's "Select point" buttons (GetMastercamResourceHandle = opmanres)
         'uictrls': None,
         'mcmillres': {str(i) for i in [25782, 25744, 25745, 25746, 25747, 25947, 25948, 25949] + list(range(25704, 25712))
                       + [25909, 25910, 25739, 25740, 25758, 25764, 25769, 25775, 25817, 25838, 25839]}}


def find_dll(root, name):
    for d, _, files in os.walk(root):
        for f in files:
            if f.lower() == name.lower():
                return os.path.join(d, f)
    return None


def extract(dll, out):
    subprocess.run([sys.executable, os.path.join(HERE, 'pe_resources.py'), dll, '--extract', out],
                   check=True, stdout=subprocess.DEVNULL)


def dib_to_image(data):
    """a BITMAP resource (DIB without file header) -> PIL image"""
    hs = struct.unpack_from('<I', data, 0)[0]
    bpp = struct.unpack_from('<H', data, 14)[0]
    clr = struct.unpack_from('<I', data, 32)[0] if hs >= 36 else 0
    ncol = clr if clr else (1 << bpp if bpp <= 8 else 0)
    bmp = b'BM' + struct.pack('<IHHI', 14 + len(data), 0, 0, 14 + hs + ncol * 4) + data
    return Image.open(io.BytesIO(bmp)).convert('RGBA')


def key_out(im, rgb):
    px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            if px[x, y][:3] == rgb:
                px[x, y] = (0, 0, 0, 0)
    return im


def toolbars(o, prefix, images):
    """TOOLBAR resources cut into cells: <prefix>_tb<ID>_<k>.png (PNG strip, else BITMAP with 192,192,192 and 255,0,255 =
    clear); opmanres PNG strips also whole as OPMANRES_PNG_<ID>.png"""
    tbdir = os.path.join(o, 'TOOLBAR')
    for t in (os.listdir(tbdir) if os.path.isdir(tbdir) else []):
        tb = open(os.path.join(tbdir, t), 'rb').read()
        _, w, h, _ = struct.unpack_from('<4H', tb, 0)
        pp, bp = os.path.join(o, 'PNG', t), os.path.join(o, 'BITMAP', t)
        if os.path.exists(pp):
            strip = Image.open(pp).convert('RGBA'); images['%s_PNG_%s.png' % (prefix.upper(), t)] = open(pp, 'rb').read()
        elif os.path.exists(bp):
            strip = key_out(key_out(dib_to_image(open(bp, 'rb').read()), (192, 192, 192)), (255, 0, 255))
        else:
            continue
        for k in range(strip.size[0] // w):
            images['%s_tb%s_%d.png' % (prefix, t, k)] = png_bytes(strip.crop((k * w, 0, k * w + w, h)))


def png_bytes(im):
    b = io.BytesIO(); im.save(b, 'PNG'); return b.getvalue()


def group_icon(d, gid):
    """GROUP_ICON <gid> + its ICON entries -> .ico bytes (GRPICONDIRENTRY: 12 bytes + u16 icon id, ICONDIRENTRY: 12 + u32
    offset); None when missing"""
    gp = os.path.join(d, 'GROUP_ICON', str(gid))
    if not os.path.exists(gp):
        return None
    g = open(gp, 'rb').read()
    n = struct.unpack_from('<H', g, 4)[0]
    ents, datas = [], []
    for k in range(n):
        e = g[6 + 14 * k: 6 + 14 * k + 14]
        iid = struct.unpack_from('<H', e, 12)[0]
        ip = os.path.join(d, 'ICON', str(iid))
        if not os.path.exists(ip):
            continue
        ents.append(e[:12]); datas.append(open(ip, 'rb').read())
    out = struct.pack('<HHH', 0, 1, len(ents)); off = 6 + 16 * len(ents)
    for e, data in zip(ents, datas):
        out += e[:8] + struct.pack('<I', len(data)) + struct.pack('<I', off); off += len(data)
    return out + b''.join(datas)


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    root = sys.argv[1]
    out_pack = sys.argv[2] if len(sys.argv) > 2 else os.path.join(config_dir(), 'icons.pack')
    images = {}                                   # name -> PNG bytes
    with tempfile.TemporaryDirectory() as tmp:
        dlls = {}
        for n in ('ribbonuires.dll', 'opmanres.dll', 'mccoreres.dll', 'mccadutilres.dll', 'mccadres.dll', 'backplotres.dll', 'mcmillres.dll', 'mcmsurfres.dll',
                  'uictrls.dll'):
            p = find_dll(root, n)
            if not p:
                print('not found:', n, '(its images are left out)'); continue
            d = os.path.join(tmp, n.split('.')[0]); extract(p, d); dlls[n] = d

        if 'ribbonuires.dll' in dlls:             # ribbon strips cut by image index + whole PNGs
            r = dlls['ribbonuires.dll']; icon_dir = os.path.join(tmp, 'ribbon_icons')
            subprocess.run([sys.executable, os.path.join(HERE, 'ribbon_extract.py'), r, os.path.join(tmp, 'ribbon.json'), icon_dir],
                           check=True, stdout=subprocess.DEVNULL)
            for f in os.listdir(icon_dir):
                images[f] = open(os.path.join(icon_dir, f), 'rb').read()
            for f in os.listdir(os.path.join(r, 'PNG')):
                images.setdefault(f + '.png', open(os.path.join(r, 'PNG', f), 'rb').read())

        if 'opmanres.dll' in dlls:
            o = dlls['opmanres.dll']
            for i, bid in enumerate(TREE_IDS):        # Toolpaths tree image list, white = transparent
                p = os.path.join(o, 'BITMAP', str(bid))
                if os.path.exists(p):
                    images['OPMANRES_TREE_%d.png' % i] = png_bytes(key_out(dib_to_image(open(p, 'rb').read()), (255, 255, 255)))
            toolbars(o, 'opmanres', images)
        if 'backplotres.dll' in dlls:                 # Backplot VCR bar toolbar 30000
            toolbars(dlls['backplotres.dll'], 'backplotres', images)

        for n, prefix in (('mccoreres.dll', 'mccoreres'), ('mccadutilres.dll', 'mccadutilres'), ('mccadres.dll', 'mccadres'), ('opmanres.dll', 'opmanres'),
                          ('backplotres.dll', 'backplotres'), ('mcmillres.dll', 'mcmillres'), ('mcmsurfres.dll', 'mcmsurfres'), ('uictrls.dll', 'uictrls')):
            if n not in dlls:
                continue
            bdir = os.path.join(dlls[n], 'BITMAP')
            keyed = KEYED.get(prefix, ())
            for f in (os.listdir(bdir) if os.path.isdir(bdir) else []):
                try:
                    im = dib_to_image(open(os.path.join(bdir, f), 'rb').read())
                    images['%s_BMP_%s.png' % (prefix, f)] = png_bytes(im)
                    if keyed is None or f in keyed:
                        images['%s_BMPK_%s.png' % (prefix, f)] = png_bytes(key_out(im.copy(), (192, 192, 192)))
                except Exception as e:
                    print('skip', n, f, e)
        if 'uictrls.dll' in dlls:                     # UIVertDialog expand / contract title icons (LoadIconA 0x1b6d / 0x1b6e)
            for gid in (7021, 7022):
                ico = group_icon(dlls['uictrls.dll'], gid)
                if ico:
                    try:
                        images['uictrls_ICO_%d.png' % gid] = png_bytes(Image.open(io.BytesIO(ico)).convert('RGBA'))
                    except Exception as e:
                        print('skip uictrls icon', gid, e)
        if 'mcmillres.dll' in dlls:                   # UIPNGImage pictures
            pdir = os.path.join(dlls['mcmillres.dll'], 'PNG')
            for f in (os.listdir(pdir) if os.path.isdir(pdir) else []):
                images['mcmillres_PNG_%s.png' % f] = open(os.path.join(pdir, f), 'rb').read()

        ubvs = find_dll(root, 'ubvs.dll')               # the Simulator (WPF, formats/simulator.md): its icons as ubvs_<file>.png
        if ubvs:
            from dotnet_resources import read_resources
            for name, tc, val in read_resources(ubvs):
                low = name.lower()
                if not low.startswith('resources/icons/') or not isinstance(val, bytes):
                    continue
                base = os.path.splitext(os.path.basename(low))[0]
                if low.endswith('.png'):
                    images['ubvs_%s.png' % base] = val
                elif low.endswith('.ico'):              # MachineSimulation.ico / UBVSIcon.ico: the largest frame
                    im = Image.open(io.BytesIO(val)); im.size = max(im.info.get('sizes', [im.size]))
                    images['ubvs_%s.png' % base] = png_bytes(im.convert('RGBA'))
        else:
            print('not found: ubvs.dll (the Simulator icons are left out)')

    os.makedirs(os.path.dirname(os.path.abspath(out_pack)), exist_ok=True)
    with open(out_pack, 'wb') as fo:
        fo.write(b'MCUIICN1'); fo.write(struct.pack('<I', len(images)))
        for name in sorted(images):
            nb = name.encode('utf-8'); data = images[name]
            fo.write(struct.pack('<H', len(nb))); fo.write(nb); fo.write(struct.pack('<I', len(data))); fo.write(data)
    print('%d images -> %s (%.1f MB)' % (len(images), out_pack, os.path.getsize(out_pack) / 1e6))


if __name__ == '__main__':
    main()
