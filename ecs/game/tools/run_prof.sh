#!/bin/bash
# Sampling profile of one event on the PROFILE_PC build (see evrun.c): run_prof.sh <prof.hdf> <mode 1-5> <seconds of play> <out dir>
# Collects PROGDIR:data/pf_<mode>?.bin; analyze with analyze_pf.py <mode> <out dir>
set -e
HDF=$(readlink -f "$1"); MODE=$2; SECS=$3; OUT=$4; mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")
cp "$HDF" "$OUT/run.hdf"
"$(dirname "$0")/run_uae.sh" "$OUT/run.hdf" "$OUT/uae" "wait:10 key:space wait:6 key:F$MODE wait:$SECS shot:play key:Escape wait:25 shot:after" >/dev/null 2>&1 || true
for n in $(seq 0 23); do
  l=$(printf "\\x$(printf %x $((97 + n)))")
  xdftool "$OUT/run.hdf" open part=SOD + read data/pf_${MODE}${l}.bin "$OUT/pf_${MODE}${l}.bin" >/dev/null 2>&1 || break
  xdftool "$OUT/run.hdf" open part=SOD + read data/tm_${MODE}${l}.bin "$OUT/tm_${MODE}${l}.bin" >/dev/null 2>&1 || true
done
ls "$OUT"/pf_* 2>/dev/null | wc -l
