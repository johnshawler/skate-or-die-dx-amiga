#!/bin/bash
# Boot an executable from a fresh bootable HDF in FS-UAE (headless, Xvfb) and screenshot the result.
# usage: uae_boot_test.sh <exe> <out.png> <kickstart.rom> [wait_seconds]
set -e
EXE=$(readlink -f "$1"); OUT=$(readlink -f "$2"); ROM=$(readlink -f "$3"); WAIT=${4:-30}
T=$(mktemp -d /tmp/uaeXXXXXX)
printf 'SYS:OverworldDemo\n' > $T/ss
rdbtool -f $T/t.hdf create size=4Mi + init + add size=100% name=SOD dostype=DOS1 bootable=1 pri=0 >/dev/null
xdftool $T/t.hdf open part=SOD + format SOD ffs + makedir S + makedir C + makedir L + makedir Libs + makedir Devs + write $EXE OverworldDemo + write $T/ss S/Startup-Sequence >/dev/null
cat > $T/t.fs-uae <<CFG
[config]
amiga_model = A1200
accelerator = blizzard-1230-iv
chip_memory = 2048
accelerator_memory = 8192
hard_drive_0 = $T/t.hdf
kickstart_file = $ROM
fullscreen = 0
window_width = 720
window_height = 540
CFG
pkill -f "fs-uae $T" 2>/dev/null || true
pgrep Xvfb >/dev/null || (Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 1
export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
(fs-uae $T/t.fs-uae >$T/out.txt 2>&1 &)
sleep $WAIT
import -window root $OUT
pkill -f "fs-uae $T/t.fs-uae" || true
sleep 1
echo "screenshot: $OUT"
