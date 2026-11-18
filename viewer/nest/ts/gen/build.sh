#!/usr/bin/env bash
# build.sh EXPORTDIR - regenerate ts/build from an existing export and rebuild nest_ts_test (dev helper)
set -e
cd "$(dirname "$0")/../.."
python3 ts/gen/gen.py "$1" ts/gen/funcs.txt ts/build
python3 ts/gen/gen_data.py ~/mastercam-re/files/n64dll.dll ts/build/ts_data_addrs.txt ts/gen/funcs.txt ts/build/ts_data.c
make -s nest_ts_test 2>&1 | grep -v "^$" | head -20
