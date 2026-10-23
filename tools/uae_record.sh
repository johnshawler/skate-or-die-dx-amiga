#!/bin/bash
# Record the screen of an event running in FS-UAE (headless) as a video, with a scripted key sequence.
# usage: uae_record.sh <exe> <outdir> <rom> <eventkey F1|F2> ; env BOOT_WAIT, SEQ (xdotool commands after the event starts)
set -e
EXE=$(readlink -f "$1"); OUT=$(readlink -f "$2"); ROM=$(readlink -f "$3"); mkdir -p $OUT
T=$(mktemp -d /tmp/claude-0/-home-user/16859150-f4a5-5599-9aa3-5cfc20e9489a/scratchpad/uaeXXXXXX)
printf 'SYS:OverworldDemo\n' > $T/ss
rdbtool -f $T/t.hdf create size=8Mi + init + add size=100% name=SOD dostype=DOS1 bootable=1 pri=0 >/dev/null
xdftool $T/t.hdf open part=SOD + format SOD ffs + makedir S + makedir C + makedir L + makedir Libs + makedir Devs + write $EXE OverworldDemo + write $T/ss S/Startup-Sequence >/dev/null
cat > $T/t.fs-uae <<CFG
[config]
amiga_model = A1200
cpu = ${CPU:-68030}
chip_memory = 2048
fast_memory = 8192
hard_drive_0 = $T/t.hdf
kickstart_file = $ROM
fullscreen = 0
window_width = 720
window_height = 540
CFG
pgrep Xvfb >/dev/null || (Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 1
export XDG_RUNTIME_DIR=/tmp/claude-0/pa-run PULSE_SINK=cap
export DISPLAY=:99 SDL_AUDIODRIVER=pulseaudio LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
if [ -n "$AUDIO_FROM_START" ]; then (timeout ${AUDIO_FROM_START} parec -d cap.monitor --file-format=wav $OUT/audio_boot.wav >/dev/null 2>&1 &); fi
(fs-uae $T/t.fs-uae >$T/out.txt 2>&1 &)
sleep ${BOOT_WAIT:-30}
W=$(xdotool search --name "FS-UAE" | tail -1); xdotool windowfocus $W 2>/dev/null || true; sleep 1
(timeout $((${REC:-12}+${PRE:-3}+4)) parec -d cap.monitor --file-format=wav $OUT/audio.wav >/dev/null 2>&1 &)
if [ -z "$NOKEY" ]; then xdotool keydown ${4:-F1}; sleep 0.5; xdotool keyup ${4:-F1}; fi
sleep ${PRE:-3}
ffmpeg -loglevel error -y -f x11grab -framerate 25 -video_size 640x400 -i :99+192,128 -t ${REC:-12} $OUT/rec.mp4 &
FP=$!
sleep 0.3
eval "${SEQ:-sleep 1}"
wait $FP
pkill -x fs-uae || true; sleep 1
