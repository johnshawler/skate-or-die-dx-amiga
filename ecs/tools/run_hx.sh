#!/bin/bash
# Run the harness KEGS from a cold boot: run_hx.sh <run dir> <script|-> <frames> <shot_every> [shot_from] [watch spec]
# Needs the KEGS build (kegs-harness/build.sh <dir>) in $KEGS_BUILD, the ROM 01 file in $ROM01 and the disk image in $SODIMG; Xvfb on :98.
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
RUN=$1; SCRIPT=$2; FRAMES=$3; EVERY=$4; FROM=${5:-0}; WATCH=${6:-}
mkdir -p "$RUN/out"; cd "$RUN"
cp "$KEGS_BUILD/xkegs" . ; cp "$ROM01" ROM.01; cp "$SODIMG" sod.2mg
printf 's5d1 = %s/sod.2mg\ns5d2 =\ns6d1 =\ns6d2 =\ns7d1 =\ng_limit_speed = 2\n' "$PWD" > config.kegs
pgrep Xvfb >/dev/null || (Xvfb :98 -screen 0 1024x768x24 >/dev/null 2>&1 &); sleep 1
export DISPLAY=:98 SDL_AUDIODRIVER=dummy
export HX_OUT=$PWD/out HX_FRAMES=$FRAMES HX_SHOT_EVERY=$EVERY HX_SHOT_FROM=$FROM
[ -n "$WATCH" ] && export HX_WATCH=$WATCH
[ "$SCRIPT" != "-" ] && export HX_SCRIPT=$(readlink -f "$SCRIPT")
timeout 600 ./xkegs > kegs.out 2>&1 || true
python3 -I "$TOOLS/shr2png.py" out
