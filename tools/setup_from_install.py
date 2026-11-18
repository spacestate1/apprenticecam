#!/usr/bin/env python3
"""Build apprenticeCAM's icon pack out of YOUR OWN Mastercam 2017 copy (the repository ships no artwork).

Takes the Mastercam 2017 DVD image (.iso), its Mastercam_Installer.msi, a folder holding either (the mounted DVD), or
the installed program folder; pulls out only the resource DLLs and writes ~/.config/mcamui/icons.pack
(tools/extract_icons.py; Windows: %APPDATA%\\apprenticeCAM\\icons.pack). Without the pack apprenticeCAM draws
letter tiles. Also writes gallery_tips.tsv (the palette tooltips' description texts; tools/gen_gallery_tips.py)
from the extracted ribbon - or, when present, from the repo's formats/ui_ribbon.json - and copies the drafting stroke fonts
font0..7.fnt to OUT_DIR/fonts (viewer/draft.c; dimensions and notes are drawn without text when they are missing).

With --layout DIR it also regenerates the menu / ribbon layout files that ship in viewer/ui/ (for developers):
  layout.txt  (ui_pack.py)   cmd_labels.tsv  (gen_cmd_labels.py)   icon_map.tsv  (gen_icon_map.py)
all passed through tools/debrand.py (product name out).

Usage:  setup_from_install.py SOURCE [OUT_DIR] [--layout DIR]
  OUT_DIR default: Linux ~/.config/mcamui, Windows %APPDATA%\\apprenticeCAM (tools/apppaths.py)
Needs 7-Zip (Linux: p7zip / 7zip package; Windows: 7-Zip installed or 7z on PATH) for .iso / .msi, and Pillow.
Temporary files go to ~/.cache/apprenticecam-setup (Windows %LOCALAPPDATA%) and are removed.
On Windows SOURCE may also be the installed program folder, e.g. "C:\\Program Files\\mcam2017".
"""
import fnmatch, os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from apppaths import config_dir, seven_zip, work_dir

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
SEVENZ = seven_zip() or '7z'
WANT = ['*.ft', '*.ft?', 'font?.fnt', 'font?.fnt.*', 'FONT?.FNT', 'FONT?.FNT.*', '*res.dll', '*res.dll.*', 'uictrls.dll', 'uictrls.dll.*', 'ubvs.dll', 'ubvs.dll.*']   # function tables + resource DLLs (MSI keys may carry .GUID); font0..7.fnt: the drafting stroke fonts (formats/drafting/text.md 2.1); uictrls: OK / Cancel / Apply / Help button bitmaps; ubvs: the Simulator icons (formats/simulator.md)


def run(*args, **kw):
    print('  ' + ' '.join(os.path.basename(a) if i < 2 else a for i, a in enumerate(args)))
    subprocess.run(list(args), check=True, **kw)


def sevenz(archive, out, *patterns):
    os.makedirs(out, exist_ok=True)
    subprocess.run([SEVENZ, 'e', '-y', '-ssc-', '-o' + out, archive, *patterns, '-r'], check=True, stdout=subprocess.DEVNULL)


def find(root, pattern):
    for d, _, fs in os.walk(root):
        for f in fs:
            if fnmatch.fnmatch(f.lower(), pattern.lower()):
                return os.path.join(d, f)
    return None


def main_msi(root):
    """the DVD carries Mastercam/data/{art,catia,mastercam,wirepower}/Mastercam_Installer.msi; the program is in
    data/mastercam (a lone .msi elsewhere is taken as is)"""
    hits = []
    for d, _, fs in os.walk(root):
        hits += [os.path.join(d, f) for f in fs if f.lower() == 'mastercam_installer.msi']
    main = [h for h in hits if os.path.basename(os.path.dirname(h)).lower() == 'mastercam']
    return (main or hits or [None])[0] if len(main) == 1 or len(hits) == 1 else None


def files_from_source(src, work):
    """-> folder holding the .ft files and resource DLLs (flat)"""
    if os.path.isdir(src):
        if find(src, 'ribbonuires.dll'):
            return src                                         # installed program folder / earlier flat extraction
        msi = main_msi(src)
        if not msi:
            sys.exit('no ribbonuires.dll and no Mastercam_Installer.msi under ' + src)
        src = msi
    low = src.lower()
    if low.endswith('.iso'):
        print('unpacking the installer from the .iso')
        os.makedirs(os.path.join(work, 'iso'))                 # 'x' keeps paths: data/art, catia, mastercam,
        subprocess.run([SEVENZ, 'x', '-y', '-ssc-', '-o' + os.path.join(work, 'iso'), src, 'Mastercam_Installer.msi', '-r'],
                       check=True, stdout=subprocess.DEVNULL)  # wirepower all hold one of that name
        src = main_msi(os.path.join(work, 'iso'))
        if not src:
            sys.exit('Mastercam_Installer.msi not found in the .iso')
        low = src.lower()
    if not low.endswith('.msi'):
        sys.exit('unknown source (want .iso, .msi or a folder): ' + src)
    print('unpacking cabinets from', os.path.basename(src))
    cabs = os.path.join(work, 'cab'); sevenz(src, cabs, '*.cab')
    out = os.path.join(work, 'files')
    for c in sorted(os.listdir(cabs)):                         # spanned set (Data1.cab continues in Data11.cab):
        print('  picking .ft / resource DLLs out of', c)        # 7z follows the span; a volume opened on its own
        try:                                                    # may report the files cut at its edges
            sevenz(os.path.join(cabs, c), out, *WANT)
        except subprocess.CalledProcessError as e:
            print('   7z exit', e.returncode, '(spanned volume; checked below)')
    shutil.rmtree(cabs, ignore_errors=True)
    for f in os.listdir(out):                                  # MSI file keys: 'name.dll.GUID' -> 'name.dll'
        base = f.split('.')
        if len(base) > 2 and len(base[-1]) >= 32 and not os.path.exists(os.path.join(out, '.'.join(base[:-1]))):
            os.rename(os.path.join(out, f), os.path.join(out, '.'.join(base[:-1])))
    return out


def main():
    args = sys.argv[1:]; layout = None
    if '--layout' in args:
        k = args.index('--layout'); layout = os.path.abspath(args[k + 1]); del args[k:k + 2]
    if not args:
        print(__doc__); sys.exit(1)
    src = os.path.abspath(args[0])
    out = os.path.abspath(args[1] if len(args) > 1 else config_dir())
    work = work_dir()
    shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
    try:
        files = files_from_source(src, work)
        rjson = None
        if layout:
            os.makedirs(layout, exist_ok=True)
            tmp = os.path.join(work, 'gen'); os.makedirs(tmp, exist_ok=True)
            rib = find(files, 'ribbonuires.dll'); opm = find(files, 'opmanres.dll')
            if not rib or not opm:
                sys.exit('ribbonuires.dll / opmanres.dll not found in ' + files)
            print('reading the ribbon and manager resources')
            cmds = os.path.join(tmp, 'ui_commands.tsv')
            run(PY, os.path.join(HERE, 'ft_index.py'), files, cmds, stdout=subprocess.DEVNULL)
            run(PY, os.path.join(HERE, 'pe_resources.py'), rib, '--extract', os.path.join(tmp, 'ribbonuires'), stdout=subprocess.DEVNULL)
            rjson, ojson = os.path.join(tmp, 'ui_ribbon.json'), os.path.join(tmp, 'ui_opmanres.json')
            run(PY, os.path.join(HERE, 'ribbon_extract.py'), os.path.join(tmp, 'ribbonuires'), rjson, os.path.join(tmp, 'ricons'), cmds,
                stdout=subprocess.DEVNULL)
            run(PY, os.path.join(HERE, 'ui_res_extract.py'), opm, ojson, os.path.join(tmp, 'oicons'), stdout=subprocess.DEVNULL)
            print('writing', layout)
            run(PY, os.path.join(HERE, 'ui_pack.py'), rjson, ojson, os.path.join(layout, 'layout.txt'))
            run(PY, os.path.join(HERE, 'gen_icon_map.py'), rjson, os.path.join(layout, 'icon_map.tsv'), stdout=subprocess.DEVNULL)
            run(PY, os.path.join(HERE, 'gen_cmd_labels.py'), files, cmds, os.path.join(layout, 'cmd_labels.tsv'), stdout=subprocess.DEVNULL)
        fdir = os.path.join(out, 'fonts'); nf = 0             # drafting stroke fonts (mccore FUN_180365a90: Common\\Fonts\\font%d.fnt)
        for root, _, names in os.walk(files):
            for f in names:
                if fnmatch.fnmatch(f.lower(), 'font[0-7].fnt'):
                    os.makedirs(fdir, exist_ok=True); shutil.copyfile(os.path.join(root, f), os.path.join(fdir, f.lower())); nf += 1
        print('drafting fonts:', nf, 'copied to', fdir)
        print('building the icon pack')
        run(PY, os.path.join(HERE, 'extract_icons.py'), files, os.path.join(out, 'icons.pack'))
        tips = rjson if rjson else os.path.join(HERE, '..', 'formats', 'ui_ribbon.json')
        if os.path.exists(tips):
            run(PY, os.path.join(HERE, 'gen_gallery_tips.py'), tips, os.path.join(out, 'gallery_tips.tsv'), stdout=subprocess.DEVNULL)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print('done. apprenticeCAM reads', out)


if __name__ == '__main__':
    main()
