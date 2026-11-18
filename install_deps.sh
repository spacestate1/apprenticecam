#!/bin/bash
# install_deps.sh [--minimal] [--yes]: install what apprenticeCAM needs to build, with the distribution's package manager.
#   Supported: Debian 13+ / Ubuntu 25.04+ (and derivatives: Mint, Pop!_OS, ...), Fedora 40+ / RHEL-likes with EPEL + CRB,
#   Arch / Manjaro / EndeavourOS, openSUSE Tumbleweed / Leap 15.6+.
#   (Older releases with Qt < 6.6 - Debian 12, Ubuntu 22.04 / 24.04 - build the command-line tools only.)
#   Required: C / C++ compiler, make, CMake >= 3.21, git (CMake fetches SARibbon and Qt-ADS), Qt >= 6.6 (Widgets, OpenGL,
#   OpenGLWidgets, Svg, and Qt's private GUI headers, which Qt-ADS uses), OpenGL / EGL, GLEW, SQLite 3.
#   Optional (left out by --minimal): OpenCASCADE (3D toolpaths and solid import; without it those commands refuse with
#   "built without OpenCASCADE"), SDL2 (mcamview, the old stand-alone viewer), 7-Zip + Pillow (tools/setup_from_install.py,
#   tools/import_libraries.py: data from your own Mastercam copy).
# Then: ./build.sh
set -e
MINIMAL=0; YES=0
for a in "$@"; do
    case "$a" in
        --minimal) MINIMAL=1 ;;
        --yes|-y) YES=1 ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "usage: $0 [--minimal] [--yes]"; exit 2 ;;
    esac
done

SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO=sudo
[ -r /etc/os-release ] || { echo "no /etc/os-release: unknown distribution, install the packages by hand (see README.md)"; exit 1; }
. /etc/os-release
FAMILY=""
for id in $ID $ID_LIKE; do
    case "$id" in
        debian|ubuntu) FAMILY=debian; break ;;
        fedora|rhel|centos) FAMILY=fedora; break ;;
        arch|archlinux) FAMILY=arch; break ;;
        opensuse|opensuse-tumbleweed|opensuse-leap|suse|sles) FAMILY=suse; break ;;
    esac
done
[ -n "$FAMILY" ] || { echo "unsupported distribution '$ID' (ID_LIKE '$ID_LIKE'): install the packages by hand (see README.md)"; exit 1; }
echo "== $PRETTY_NAME ($FAMILY family)"

case $FAMILY in
debian)
    REQ="build-essential cmake git ca-certificates pkg-config qt6-base-dev qt6-base-private-dev libqt6opengl6-dev libqt6svg6-dev
         libgl-dev libegl-dev libglew-dev libsqlite3-dev libxkbcommon-dev"
    OPT="p7zip-full python3-pil libsdl2-dev libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev libocct-data-exchange-dev"
    Y=""; [ $YES = 1 ] && Y=-y
    $SUDO apt-get update
    PKGS="$REQ"; [ $MINIMAL = 0 ] && PKGS="$REQ $OPT"
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install $Y --no-install-recommends $PKGS
    ;;
fedora)
    REQ="gcc gcc-c++ make cmake git pkgconf-pkg-config qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtsvg-devel
         mesa-libGL-devel mesa-libEGL-devel glew-devel sqlite-devel libxkbcommon-devel"
    OPT="7zip python3-pillow SDL2-devel opencascade-devel"
    Y=""; [ $YES = 1 ] && Y=-y
    PKGS="$REQ"; [ $MINIMAL = 0 ] && PKGS="$REQ $OPT"
    $SUDO dnf install $Y $PKGS
    ;;
arch)
    REQ="base-devel cmake git qt6-base qt6-svg glew sqlite mesa libglvnd libxkbcommon"
    OPT="7zip python-pillow sdl2 opencascade"
    Y=""; [ $YES = 1 ] && Y=--noconfirm
    PKGS="$REQ"; [ $MINIMAL = 0 ] && PKGS="$REQ $OPT"
    $SUDO pacman -Sy --needed $Y $PKGS
    ;;
suse)
    REQ="gcc gcc-c++ make cmake git pkgconf qt6-base-devel qt6-base-private-devel qt6-opengl-devel qt6-openglwidgets-devel
         qt6-svg-devel Mesa-libGL-devel Mesa-libEGL-devel glew-devel sqlite3-devel libxkbcommon-devel"
    OPT="7zip python3-Pillow SDL2-devel occt-devel"
    Y=""; [ $YES = 1 ] && Y=-n
    PKGS="$REQ"; [ $MINIMAL = 0 ] && PKGS="$REQ $OPT"
    $SUDO zypper $Y install $PKGS
    ;;
esac
QTV=$(qmake6 -query QT_VERSION 2>/dev/null || qmake-qt6 -query QT_VERSION 2>/dev/null || /usr/lib/qt6/bin/qmake -query QT_VERSION 2>/dev/null || true)
if [ -n "$QTV" ] && [ "$(printf '%s\n' 6.6 "$QTV" | sort -V | head -1)" != 6.6 ]; then
    echo "== NOTE: this distribution's Qt is $QTV; the Qt app needs Qt >= 6.6 (SARibbon). ./build.sh will build the command-line"
    echo "   tools (viewer/cam_cli ...) and then stop at the Qt app. Debian 13+, Ubuntu 25.04+, Fedora 40+, Arch, openSUSE Tumbleweed have it."
fi
echo "== done. Next: ./build.sh"
