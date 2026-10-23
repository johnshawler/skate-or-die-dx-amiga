#!/bin/bash
# Drive the shop/sign-up screens of the demo in FS-UAE (headless) and take screenshots.
# usage: uae_run_menu.sh <exe> <outdir> <kickstart.rom>
set -e
EXE=$(readlink -f "$1"); OUT=$(readlink -f "$2"); ROM=$(readlink -f "$3"); mkdir -p $OUT
T=$(mktemp -d /tmp/claude-0/-home-user/16859150-f4a5-5599-9aa3-5cfc20e9489a/scratchpad/uaeXXXXXX)
printf 'SYS:OverworldDemo\n' > $T/ss
rdbtool -f $T/t.hdf create size=8Mi + init + add size=100% name=SOD dostype=DOS1 bootable=1 pri=0 >/dev/null
xdftool $T/t.hdf open part=SOD + format SOD ffs + makedir S + makedir C + makedir L + makedir Libs + makedir Devs + write $EXE OverworldDemo + write $T/ss S/Startup-Sequence >/dev/null
cat > $T/t.fs-uae <<CFG
[config]
amiga_model = A1200
cpu = 68030
chip_memory = 2048
fast_memory = 8192
hard_drive_0 = $T/t.hdf
kickstart_file = $ROM
fullscreen = 0
window_width = 720
window_height = 540
CFG
pgrep Xvfb >/dev/null || (Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 1
export DISPLAY=:99 SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=$OUT/audio.raw LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
(fs-uae $T/t.fs-uae >$T/out.txt 2>&1 &)
sleep 24
W=$(xdotool search --name "FS-UAE" | tail -1); xdotool windowfocus $W 2>/dev/null || true; sleep 1
shot() { import -window root $OUT/$1.png; }
press() { xdotool keydown $1; sleep ${2:-0.35}; xdotool keyup $1; sleep 0.4; }
shot m01_shop
press Escape 0.5; sleep 6
shot q02_after_esc
pkill -x fs-uae || true; sleep 1
ls $OUT
