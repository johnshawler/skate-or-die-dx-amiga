#!/bin/bash
# A bootable test HDF for the WHDLoad build: C:WHDLoad (the executable is not part of the product: pass its path), the slave and its data directory,
# Startup-Sequence = "C:WHDLoad SkateOrDie/SkateOrDie.slave".   usage: mkhdf_whd.sh <out.hdf> <WHDLoad executable> [extra WHDLoad options]
set -e
cd "$(dirname "$0")/.."
OUT=$(readlink -f "$1"); WHD=$(readlink -f "$2"); shift 2; OPTS="$*"; T=$(mktemp -d)
printf 'CD SkateOrDie\nC:WHDLoad SkateOrDie.slave %s\n' "$OPTS" > $T/ss
rm -f $OUT
rdbtool -f $OUT create size=32Mi + init + add size=100% name=SOD dostype=DOS1 bootable=1 pri=0 >/dev/null
args=(open part=SOD + format SOD ffs + makedir S + makedir C + makedir L + makedir Libs + makedir Devs + makedir SkateOrDie + makedir SkateOrDie/data + write $WHD C/WHDLoad + write $T/ss S/Startup-Sequence + write dist_whd/SkateOrDie/SkateOrDie.slave SkateOrDie/SkateOrDie.slave)
for f in dist_whd/SkateOrDie/data/*; do args+=(+ write $f SkateOrDie/data/$(basename $f)); done
xdftool $OUT "${args[@]}" >/dev/null
echo "wrote $OUT"
