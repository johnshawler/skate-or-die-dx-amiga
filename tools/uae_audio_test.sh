#!/bin/bash
# Boot the demo in FS-UAE (headless) and record the emulated Paula output through a PulseAudio null sink.
# usage: uae_audio_test.sh <exe> <out.wav> <kickstart.rom> [record_seconds]
set -e
EXE=$(readlink -f "$1"); OUT=$(readlink -f "$2"); ROM=$(readlink -f "$3"); SECS=${4:-60}
T=$(mktemp -d /tmp/uaeXXXXXX)
printf 'SYS:OverworldDemo\n' > $T/ss
rdbtool -f $T/t.hdf create size=8Mi + init + add size=100% name=SOD dostype=DOS1 bootable=1 pri=0 >/dev/null
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
export XDG_RUNTIME_DIR=/tmp/pa-run
pgrep Xvfb >/dev/null || (Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 1
export DISPLAY=:99 SDL_AUDIODRIVER=pulseaudio PULSE_SINK=cap LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
(timeout $((SECS+5)) parec -d cap.monitor --file-format=wav $OUT >/dev/null 2>&1 &)
(fs-uae $T/t.fs-uae >$T/out.txt 2>&1 &)
sleep $SECS
pkill -x fs-uae || true; sleep 1
ls -la $OUT
