#!/bin/bash
# Run a bench exe in FS-UAE headless and capture the passes-per-second counter: run_bench.sh <exe> <out dir> <kickstart rom> [extra config lines file]
# env: BOOT_WAIT (default 25), SHOTS (number of screenshots, default 4), SHOT_GAP (s, default 8), KEYS (xdotool key names pressed at the start, default "space")
set -e
EXE=$(readlink -f "$1"); OUT=$2; ROM=$(readlink -f "$3"); EXTRA=${4:+$(readlink -f "$4")}; mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")
T=$(mktemp -d "$OUT/tmpXXXXXX")
printf 'SYS:Bench\n' > $T/ss
rdbtool -f $T/t.hdf create size=6Mi + init + add size=100% name=SOD dostype=DOS1 bootable=1 pri=0 >/dev/null
xdftool $T/t.hdf open part=SOD + format SOD ffs + makedir S + makedir C + makedir L + makedir Libs + makedir Devs + write $EXE Bench + write $T/ss S/Startup-Sequence >/dev/null
cat > $T/t.fs-uae <<CFG
[config]
hard_drive_0 = $T/t.hdf
kickstart_file = $ROM
fullscreen = 0
window_width = 720
window_height = 540
CFG
cat $EXTRA >> $T/t.fs-uae
xdpyinfo -display :98 >/dev/null 2>&1 || (Xvfb :98 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 1
mkdir -p /tmp/xdg_bench; chmod 700 /tmp/xdg_bench; export XDG_RUNTIME_DIR=/tmp/xdg_bench DISPLAY=:98 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
(fs-uae $T/t.fs-uae >$OUT/fsuae.log 2>&1 &)
sleep ${BOOT_WAIT:-25}
W=$(xdotool search --name "FS-UAE" | tail -1); xdotool windowfocus $W 2>/dev/null || true
for k in ${KEYS:-space}; do xdotool keydown $k; sleep 0.4; xdotool keyup $k; sleep 2; done
for i in $(seq 1 ${SHOTS:-4}); do sleep ${SHOT_GAP:-8}; import -window root $OUT/shot$i.png; done
pkill -x fs-uae || true; sleep 1
