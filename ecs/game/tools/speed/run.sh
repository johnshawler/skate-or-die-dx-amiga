#!/bin/bash
# run.sh <script.lua> <outdir>
S=/tmp/claude-0/-home-user/16859150-f4a5-5599-9aa3-5cfc20e9489a/scratchpad
rm -rf "$2"; mkdir -p "$2"
(xdpyinfo -display :97 >/dev/null 2>&1 || (Xvfb :97 -screen 0 800x600x24 >/dev/null 2>&1 &)); sleep 1
NAV=$S/nes/nav.lua OUT="$2" LIB=$S/nes/lib.lua DISPLAY=:97 SDL_AUDIODRIVER=dummy timeout ${T:-300} /usr/games/fceux --sound 0 --loadlua "$1" $S/nes/sod.nes > "$2/fceux.log" 2>&1
python3 $S/nes/mont.py "$2" "$2.png"
