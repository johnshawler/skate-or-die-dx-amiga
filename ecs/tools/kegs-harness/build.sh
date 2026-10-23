#!/bin/bash
# Build the KEGS 1.38 test emulator: build.sh <output dir>. Needs git, gcc, make, perl, libx11/libxext/libpulse dev packages.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); OUT=$(mkdir -p "$1" && cd "$1" && pwd)
PIN=41e71031debbde98e66ea5c5c67498e9b4726a6d     # digarok/gsplus main, contains upstream/kegs 1.38
[ -d $OUT/src ] || { git clone -q --depth 1 https://github.com/digarok/gsplus $OUT/src; }
[ "$(git -C $OUT/src rev-parse HEAD)" = "$PIN" ] || { echo "unexpected KEGS source revision"; exit 1; }
git -C $OUT/src checkout -q -- . && git -C $OUT/src clean -fdxq
K=$OUT/src/upstream/kegs/src
cp $HERE/harness.c $K/ && python3 -I $HERE/apply_hooks.py $K
cd $K && rm -f vars ../xkegs xkegs && ln -s vars_x86linux vars && make -s > $OUT/make.log 2>&1 || { tail -20 $OUT/make.log; exit 1; }
cp $OUT/src/upstream/kegs/xkegs $OUT/xkegs && ls -l $OUT/xkegs
