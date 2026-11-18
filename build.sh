#!/bin/bash
# build.sh [clean] [-j N]: build everything from the repo root.
#   1. viewer/Makefile "all": the C core objects (core_obj/) + cam_cli, tool_cli, mcamview, xt2obj, nc_dev;
#      then viewer/mp (mp_cli, the Mastercam post engine command line)
#   2. the Qt app viewer-qt/build/apprenticecam (CMake, RelWithDebInfo; SARibbon + Qt ADS fetched by CMake on the
#      first configure, or taken from ~/mastercam-re/work/qtdeps if that copy exists)
# "clean" wipes viewer/ objects and the Qt build directory first (the Qt deps are fetched again unless the local
# copy exists).
set -e
cd "$(dirname "$0")"
JOBS=4; CLEAN=0
while [ $# -gt 0 ]; do
    case "$1" in
        clean) CLEAN=1 ;;
        -j) shift; JOBS=$1 ;;
        -j*) JOBS=${1#-j} ;;
        *) echo "usage: $0 [clean] [-j N]"; exit 2 ;;
    esac
    shift
done
ulimit -v 8000000 2>/dev/null || true          # memory cap (desktop shares the RAM)

QB=viewer-qt/build
DEPS=${QTDEPS:-$HOME/mastercam-re/work/qtdeps}   # optional local copies of SARibbon / Qt-ADS (else CMake fetches them)
if [ $CLEAN = 1 ]; then
    echo "== clean"
    make -C viewer clean
    rm -rf "$QB"
fi

echo "== C core + tools (viewer/Makefile)"
make -C viewer -j"$JOBS" all

echo "== Mastercam post engine CLI (viewer/mp)"
make -C viewer/mp -j"$JOBS"

echo "== Qt app ($QB)"
if [ ! -f "$QB/CMakeCache.txt" ]; then
    ARGS=(-S viewer-qt -B "$QB" -DCMAKE_BUILD_TYPE=RelWithDebInfo)
    if [ -d "$DEPS/saribbon-src" ] && [ -d "$DEPS/qtads-src" ]; then
        ARGS+=(-DFETCHCONTENT_SOURCE_DIR_SARIBBON="$DEPS/saribbon-src"
               -DFETCHCONTENT_SOURCE_DIR_QTADS="$DEPS/qtads-src" -DFETCHCONTENT_FULLY_DISCONNECTED=ON)
    fi
    cmake "${ARGS[@]}"
fi
cmake --build "$QB" -j"$JOBS"

echo "== done"
echo "   run:  $QB/apprenticecam [PART.mcx-9]"
echo "   cli:  viewer/cam_cli PART.mcx-9 post OUT.nc"
