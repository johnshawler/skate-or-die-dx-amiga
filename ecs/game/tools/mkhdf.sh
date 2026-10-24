#!/bin/bash
# Put dist/SkateOrDie and dist/data/ on a bootable 8 MB HDF (amitools): mkhdf.sh <out.hdf>   (the HDF is only a carrier: the product is the executable plus its data folder)
set -e
cd "$(dirname "$0")/.."
OUT=$(readlink -f "${1:-dist/SkateOrDie.hdf}"); T=$(mktemp -d)
printf 'SYS:SkateOrDie\n' > $T/ss
rm -f $OUT
rdbtool -f $OUT create size=8Mi + init + add size=100% name=SOD dostype=DOS1 bootable=1 pri=0 >/dev/null
args=(open part=SOD + format SOD ffs + makedir S + makedir C + makedir L + makedir Libs + makedir Devs + makedir data + write dist/SkateOrDie SkateOrDie + write $T/ss S/Startup-Sequence)
for f in dist/data/*; do args+=(+ write $f data/$(basename $f)); done
xdftool $OUT "${args[@]}" >/dev/null
xdftool $OUT open part=SOD + list data | tail -4
