#!/usr/bin/env bash
# build.sh - the oracle tracer used to grow the TrueShape port (dev tool, not part of the port): tools/peloader with
# cov.c (int3 at listed addresses; SIGTRAP handler counts hits and re-arms by single-stepping) plus two patches
# (peloader.patch: arm before DllMain, dump at exit; win32.patch: KEEPTMP=1 keeps deleted temp files as kept_<n>_*).
#   COV_ADDRS=file  hex addresses to trap (function entries, or instruction addresses with COV_ONESHOT=1)
#   COV_OUT=file    "addr count" of the hits          COV_TRACE=file  every hit in order (+ rcx rdx r8 r9)
#   COV_ICALL=file  with the indirect call sites armed: "callsite target" (-> ../callsites.txt)
#   COV_DUMPF=addr  at that function: COV_DUMPG=addr the 4 / 8 bytes of a global, COV_DUMPX=1 xmm0-3 as doubles, else 5
#                   doubles at rcx (+ COV_DUMPO hex offset) and rdx (dump.txt)
# Used for: ../funcs.txt (functions the DLL executes on the oracle cases), ../callsites.txt, and ../tdiff.py
# (first divergence between COV_TRACE of the DLL and TS_TRACE of nest_ts_test).
set -e
HERE=$(cd "$(dirname "$0")" && pwd); P=$HERE/../../../../../tools/peloader; O=${1:-$HERE/covload}
T=$(mktemp -d); cp $P/*.c $P/*.h $P/*.S "$T"/; cp "$HERE/cov.c" "$T"/
(cd "$T" && patch -s -p0 peloader.c < "$HERE/peloader.patch" && patch -s -p0 win32.c < "$HERE/win32.patch")
gcc -O1 -g -w -o "$O" "$T"/peloader.c "$T"/pe.c "$T"/win32.c "$T"/seh.c "$T"/cov.c "$T"/seh_asm.S "$T"/job.c \
    $P/../../viewer/nest/nest_job_io.c -I$P
rm -rf "$T"; echo "built $O"
