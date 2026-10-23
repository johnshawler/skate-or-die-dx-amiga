#!/bin/bash
# Build sod_demo.hdf (8 MB, bootable, FFS) from demo/build/OverworldDemo. Needs amitools (rdbtool, xdftool). Run demo/build.sh first.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
printf 'SYS:OverworldDemo\n' > $T/ss
rm -f sod_demo.hdf
rdbtool -f sod_demo.hdf create size=8Mi + init + add size=100% name=SOD dostype=DOS1 bootable=1 pri=0 >/dev/null
xdftool sod_demo.hdf open part=SOD + format SOD ffs + makedir S + makedir C + makedir L + makedir Libs + makedir Devs + write demo/build/OverworldDemo OverworldDemo + write $T/ss S/Startup-Sequence + write demo/README.txt README >/dev/null
rm -rf $T
ls -l sod_demo.hdf
