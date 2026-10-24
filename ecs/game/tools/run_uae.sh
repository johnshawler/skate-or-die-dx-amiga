#!/bin/bash
# Boot the HDF in FS-UAE headless (A1200, 68020, AGA, 2 MB Chip, no Fast RAM, Kickstart 3.1) and drive it with a key script.
# usage: run_uae.sh <game.hdf> <out dir> "<steps>"   steps: space-separated  wait:<s>  shot:<name>  key:<xdotool key>[:<hold s>]  down:<key>  up:<key>
# env: ROM (default roms/Kickstart 3.1 A1200), BOOT (seconds to wait for the boot, default 20), CPUOPT (extra uae lines)
set -e
exec 9>/tmp/run_uae.lock; flock 9   # one emulator at a time on the shared display (two runs at once send keys to each other)
HDF=$(readlink -f "$1"); OUT=$2; STEPS=$3; mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")
ROM=${ROM:-$(readlink -f "$(dirname "$0")/../../../roms/Kickstart v3.1 r40.068 (1993-12)(Commodore)(A1200)[!].rom")}
T=$(mktemp -d "$OUT/tmpXXXXXX")
cat > $T/t.fs-uae <<CFG
[config]
amiga_model = A1200
cpu = 68020
chip_memory = 2048
fast_memory = 0
hard_drive_0 = $HDF
kickstart_file = $ROM
fullscreen = 0
window_width = 720
window_height = 540
uae_cpu_memory_cycle_exact = true
uae_cpu_compatible = true
$CPUOPT
CFG
xdpyinfo -display :98 >/dev/null 2>&1 || (Xvfb :98 -screen 0 1024x768x24 >/dev/null 2>&1 9>&- &) ; sleep 1
mkdir -p /tmp/xdg_bench; chmod 700 /tmp/xdg_bench
export XDG_RUNTIME_DIR=/tmp/xdg_bench DISPLAY=:98 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
(fs-uae $T/t.fs-uae >$OUT/fsuae.log 2>&1 9>&- &)
sleep ${BOOT:-20}
W=$(xdotool search --name "FS-UAE" | tail -1); xdotool windowfocus $W 2>/dev/null || true
for s in $STEPS; do
  case $s in
    wait:*) sleep ${s#wait:};;
    shot:*) import -window root $OUT/${s#shot:}.png;;
    key:*) k=${s#key:}; h=0.4; case $k in *:*) h=${k#*:}; k=${k%%:*};; esac; xdotool keydown $k; sleep $h; xdotool keyup $k;;
    down:*) xdotool keydown ${s#down:};;
    up:*) xdotool keyup ${s#up:};;
  esac
done
pkill -x fs-uae || true; sleep 1
