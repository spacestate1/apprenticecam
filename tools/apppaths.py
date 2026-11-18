"""apprenticeCAM folders for the Python tools - the same rules as viewer/platform.h (Linux and Windows)."""
import os, shutil


def config_dir():
    """Windows %APPDATA%\\apprenticeCAM; Linux $XDG_CONFIG_HOME/mcamui or ~/.config/mcamui (MCAMUI_CONFIG_DIR wins)"""
    if os.environ.get('MCAMUI_CONFIG_DIR'):
        return os.environ['MCAMUI_CONFIG_DIR']
    if os.name == 'nt':
        return os.path.join(os.environ.get('APPDATA') or os.path.expanduser('~'), 'apprenticeCAM')
    return os.path.join(os.environ.get('XDG_CONFIG_HOME') or os.path.expanduser('~/.config'), 'mcamui')


def work_dir():
    """scratch space for large extractions: Linux ~/.cache (not a RAM tmpfs), Windows %LOCALAPPDATA%\\Temp"""
    if os.name == 'nt':
        base = os.environ.get('LOCALAPPDATA') or os.environ.get('TEMP') or os.path.expanduser('~')
        return os.path.join(base, 'apprenticecam-setup')
    return os.path.join(os.environ.get('XDG_CACHE_HOME') or os.path.expanduser('~/.cache'), 'apprenticecam-setup')


def seven_zip():
    """7z executable: on PATH (7z / 7za / 7zz), or 7-Zip's default Windows install folder"""
    for n in ('7z', '7za', '7zz'):
        p = shutil.which(n)
        if p:
            return p
    if os.name == 'nt':
        for d in (os.environ.get('ProgramFiles'), os.environ.get('ProgramFiles(x86)')):
            if d and os.path.exists(os.path.join(d, '7-Zip', '7z.exe')):
                return os.path.join(d, '7-Zip', '7z.exe')
    return None
