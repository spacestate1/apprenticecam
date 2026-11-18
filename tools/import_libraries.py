#!/usr/bin/env python3
"""Copy the tool libraries and operation defaults libraries out of YOUR OWN Mastercam 2017 copy into apprenticeCAM's
config folder (the repository ships none of them).

SOURCE is one of
  - the Mastercam 2017 DVD image (.iso), or a folder holding the mounted DVD
  - its Mastercam_Installer.msi
  - the installed program folder (holds common/SharedDefaults, e.g. "C:\\Program Files\\mcam2017")
  - a Mastercam shared data folder (holds mill/Tools, common/ToolData, ...; e.g.
    <windows>/Users/Public/Documents/shared Mcam2017)
In the installer and the program folder the shared data sits under common/SharedDefaults (Mastercam copies it to the
shared folder on first run). The files land in OUT_DIR/shared in the layout <APP_DIR_SHARED> uses:
  mill/Tools/*.tooldb, router/Tools/*.tooldb              tool libraries (config 2301, the machine's ToolLibFile)
  mill/Ops/*.mcam-defaults, router/OPS/*.mcam-defaults    operation defaults libraries (config 2303)
  common/ToolData/DatabaseDefaults.tooldb, DatabaseCatalog.tooldb   new-tool defaults, catalog
  MATERIALS/*.mcam-materials                              material libraries (feeds / speeds From material)
apprenticeCAM takes OUT_DIR/shared when paths.conf "shared" is unset or a file is missing there (viewer/paths.h
host_path). It lists what it copied.

Usage:  import_libraries.py SOURCE [OUT_DIR]
  OUT_DIR default: Linux ~/.config/mcamui, Windows %APPDATA%\\apprenticeCAM (tools/apppaths.py; MCAMUI_CONFIG_DIR wins)
Needs 7-Zip for .iso / .msi (as setup_from_install.py). Temporary files go to ~/.cache/apprenticecam-setup (Windows
%LOCALAPPDATA%) and are removed.
"""
import fnmatch, os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from apppaths import config_dir, seven_zip, work_dir
import msi_tables

SEVENZ = seven_zip() or '7z'
WANT = ['mill/tools/*.tooldb', 'router/tools/*.tooldb', 'mill/ops/*.mcam-defaults', 'router/ops/*.mcam-defaults',
        'common/tooldata/databasedefaults.tooldb', 'common/tooldata/databasecatalog.tooldb', 'materials/*.mcam-materials']
TABLES = ['!_StringPool', '!_StringData', '!_Columns', '!File', '!Component', '!Directory']


def wanted(rel):
    low = rel.replace('\\', '/').lower()
    return any(fnmatch.fnmatch(low, w) for w in WANT)


def from_folder(root):
    """-> [(relative path, source file)] of a shared folder (or a program folder's common/SharedDefaults)"""
    for d, _, fs in os.walk(root):
        if os.path.basename(d).lower() == 'shareddefaults' and os.path.basename(os.path.dirname(d)).lower() == 'common':
            root = d; break
    out = []
    for d, _, fs in os.walk(root):
        for f in fs:
            rel = os.path.relpath(os.path.join(d, f), root).replace(os.sep, '/')
            if wanted(rel):
                out.append((rel, os.path.join(d, f)))
    return out


def main_msi(root):
    """the DVD's Mastercam/data/mastercam/Mastercam_Installer.msi (setup_from_install.py's rule)"""
    hits = []
    for d, _, fs in os.walk(root):
        hits += [os.path.join(d, f) for f in fs if f.lower() == 'mastercam_installer.msi']
    main = [h for h in hits if os.path.basename(os.path.dirname(h)).lower() == 'mastercam']
    return (main or hits or [None])[0] if len(main) == 1 or len(hits) == 1 else None


def from_msi(msi, work):
    """the MSI's File / Component / Directory tables give each file key's folder; the shared data is the files under
    .../common/SharedDefaults; their keys are extracted from the cabinets (Data1.cab spans into Data11.cab)"""
    tdir = os.path.join(work, 'tables'); os.makedirs(tdir)
    subprocess.run([SEVENZ, 'e', '-y', '-o' + tdir, msi, *TABLES], check=True, stdout=subprocess.DEVNULL)
    t = msi_tables.load(tdir)
    D = {r['Directory']: r for r in t('Directory')}
    C = {r['Component']: r for r in t('Component')}
    long_name = lambda s: s.split('|')[-1]
    def dpath(d):
        p = []
        while d in D:
            r = D[d]; p.append(long_name(r['DefaultDir'].split(':')[-1])); nd = r['Directory_Parent']
            if not nd or nd == d: break
            d = nd
        return '/'.join(x for x in reversed(p) if x not in ('.', 'SourceDir'))
    keys = {}
    for f in t('File'):
        p = dpath(C[f['Component_']]['Directory_']).split('/')
        low = [x.lower() for x in p]
        for i in range(len(low) - 1):
            if low[i] == 'common' and low[i + 1] == 'shareddefaults':
                rel = '/'.join(p[i + 2:] + [long_name(f['FileName'])])
                if wanted(rel):
                    keys[f['File']] = rel
                break
    if not keys:
        sys.exit('no shared-data libraries listed in ' + msi)
    cabs = os.path.join(work, 'cab'); os.makedirs(cabs)
    subprocess.run([SEVENZ, 'e', '-y', '-o' + cabs, msi, '*.cab'], check=True, stdout=subprocess.DEVNULL)
    out = os.path.join(work, 'files'); os.makedirs(out)
    for c in sorted(os.listdir(cabs)):
        r = subprocess.run([SEVENZ, 'e', '-y', '-ssc', '-o' + out, os.path.join(cabs, c), *keys], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        if r.returncode:
            print('  7z exit %d on %s (spanned volume; checked below)' % (r.returncode, c))
    return [(rel, os.path.join(out, k)) for k, rel in sorted(keys.items(), key=lambda kv: kv[1].lower())
            if os.path.exists(os.path.join(out, k))]


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    src = os.path.abspath(sys.argv[1])
    dst = os.path.join(os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else config_dir()), 'shared')
    work = work_dir()
    shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
    try:
        if os.path.isdir(src) and not main_msi(src):
            files = from_folder(src)
        else:
            msi = src
            if os.path.isdir(src):
                msi = main_msi(src)
            elif src.lower().endswith('.iso'):
                print('unpacking the installer from the .iso')
                iso = os.path.join(work, 'iso'); os.makedirs(iso)
                subprocess.run([SEVENZ, 'x', '-y', '-ssc-', '-o' + iso, src, 'Mastercam_Installer.msi', '-r'], check=True,
                               stdout=subprocess.DEVNULL)
                msi = main_msi(iso)
            if not msi or not msi.lower().endswith('.msi'):
                sys.exit('unknown source (want .iso, .msi, a program folder or a shared folder): ' + src)
            print('reading', os.path.basename(msi))
            files = from_msi(msi, work)
        if not files:
            sys.exit('no tool / defaults libraries found in ' + src)
        for rel, f in files:
            to = os.path.join(dst, *rel.split('/'))
            os.makedirs(os.path.dirname(to), exist_ok=True)
            shutil.copyfile(f, to)
            print('  %-60s %9d' % (rel, os.path.getsize(to)))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print('%d files -> %s' % (len(files), dst))


if __name__ == '__main__':
    main()
