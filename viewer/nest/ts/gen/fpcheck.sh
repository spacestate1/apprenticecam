#!/usr/bin/env bash
# fpcheck.sh - list implicit conversions between floating and integer types in the generated code (dev helper):
# in Ghidra's output an "undefined8" holding a double's bits must be copied, never converted.
cd "$(dirname "$0")/.."
gcc -std=gnu17 -fpermissive -fsyntax-only -w -Wfloat-conversion -Wno-error build/ts_gen.c 2>&1 | grep -A1 "float-conversion\|conversion from" | grep "^build\|^ *[0-9]* |" | head -${1:-60}
