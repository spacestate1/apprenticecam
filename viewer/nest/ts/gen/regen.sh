#!/usr/bin/env bash
# regen.sh - regenerate viewer/nest/ts/build/ (ts_gen.c, ts_gen.h, ts_data.c) for the TrueShape port.
#   regen.sh [--export]   --export first re-runs the Ghidra export (needs the nest64 project, ~15 min, ~4 GB)
# Export (Ghidra headless, project read-only, nothing saved): ExportC10.java renames every function FUN_<addr>, gives
# the CRT functions their C signatures (crtsigs.txt), creates functions at code labels used as values (labels.txt),
# types the function-pointer slots (fntable.txt), commits the decompiler's parameters (2 passes, so stack arguments
# reach the callers), corrects register parameters from the entry spills (double vs integer), gives tail-calling
# functions their callee's return type, types XMM values as double, writes call-site prototype overrides for indirect
# calls with one known target (callsites.txt), then writes funcs.c (decompile), globals.tsv (per-function global
# types), fpassoc.tsv (float evaluation-order hazards).
# gen.py also reads the DLL itself (N64DLL): the C++ exception data -> catching wrappers (eh.py, hand parts in ehfix.txt)
# and the error builders' message arguments at every call site (vaargs.py).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
EXP=${TS_EXPORT:-$HERE/export}
DLL=${N64DLL:-$HOME/mastercam-re/files/n64dll.dll}
if [ "${1:-}" = --export ]; then
  rm -rf "$EXP"
  MAXMEM=4G /opt/ghidra/support/analyzeHeadless "$HOME/mastercam-re/ghidra_nest64" nest64 -process n64dll.dll \
    -noanalysis -readOnly -scriptPath "$HERE" -postScript ExportC10.java "$EXP" 180001000 180219e00 2 \
    "$HERE/labels.txt" "$HERE/crtsigs.txt" "$HERE/fntable.txt" "$HERE/callsites.txt" > "$HERE/export.log" 2>&1
fi
mkdir -p "$HERE/../build"
python3 "$HERE/gen.py" "$EXP" "$HERE/funcs.txt" "$HERE/../build"
python3 "$HERE/retype.py"            # stack locals used as doubles: appends to retype.txt; rerun gen.py if it found any
python3 "$HERE/gen.py" "$EXP" "$HERE/funcs.txt" "$HERE/../build"
python3 "$HERE/gen_data.py" "$DLL" "$HERE/../build/ts_data_addrs.txt" "$HERE/funcs.txt" "$HERE/../build/ts_data.c"
# ts_gen.c is compiled with -w (Makefile); a pointer cast to a narrower integer is an image address folded into a
# constant or a mistyped parameter (both wrong for the port's memory): the generation fails until gen/patches.txt fixes it
if gcc -std=gnu17 -fsyntax-only -Wpointer-to-int-cast "$HERE/../build/ts_gen.c" 2>&1 | grep -A2 'Wpointer-to-int-cast'; then
  echo "regen.sh: pointer-to-int casts in ts_gen.c (above)" >&2; exit 1
fi
