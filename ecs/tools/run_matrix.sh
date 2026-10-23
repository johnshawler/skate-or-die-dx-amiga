#!/bin/bash
# Run every event with each stress input from a cold boot: run_matrix.sh <out dir> <frames> [seed]
# Needs KEGS_BUILD, ROM01, SODIMG (see run_hx.sh).
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd); OUT=$1; FR=$2; SEED=${3:-1}
declare -A PH=([ramp]=031076 [hj]=03107D [race]=031082 [jam]=031077 [joust]=0310A6)
declare -A WT=([ramp]=3:3011:2 [hj]=3:2AA4:2 [race]=3:5A55:12 [jam]=3:63C2:12 [joust]=)
export HX_PAD=0,70,255
for ev in ramp hj race jam joust; do for kind in waggle random; do
  id=SRC-$ev-$kind-s$SEED; d=$OUT/$id; mkdir -p $d
  python3 -I $TOOLS/gen_stress.py $ev $kind $SEED 40000 > $d/event_input.txt
  python3 -I $TOOLS/make_script.py $ev $d/event_input.txt 8700 > $d/script.txt
  HX_PCHOOK=${PH[$ev]} $TOOLS/run_hx.sh $d $d/script.txt $FR 3000 8700 ${WT[$ev]}
  python3 -I $TOOLS/analyze_case.py $d 8700 > $d/summary.json
  echo "$id $(cat $d/summary.json)"
done; done
