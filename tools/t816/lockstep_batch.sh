#!/bin/bash
# usage: lockstep_batch.sh <cfg> <mode> <passes> <seed_from> <seed_to> <outfile>
cd "$(dirname "$0")"
for s in $(seq $4 $5); do
  echo "== seed $s" >> $6
  python3 -u lockstep.py $1 $2 $3 --random=$s 2>&1 | tail -4 >> $6
done
echo DONE >> $6
