# Skate or Die (Apple IIGS) -> Amiga A1200: "Skate or Die DX"

An Amiga A1200 (68020, AGA, 2 MB Chip RAM, Kickstart 3.1, PAL) port of Electronic Arts' 1988 *Skate or Die*, made by translating the Apple IIGS game's own 65816 code to C and running it on a small Amiga runtime, so the game logic is the original's. Amiga version by Boatofcar.

**This repository contains code and documentation only.** It does not contain, and you must not add to it, any of Electronic Arts' game files (disk images, extracted resources, pictures, music, sprites, disassemblies or the C generated from the original's code) or any Amiga Kickstart ROM. To build the game you need your own copy of the original Apple IIGS game, extracted with the tools here, and (to test) your own Kickstart 3.1 ROM in an emulator. *Skate or Die* is the property of Electronic Arts; this project is not affiliated with or endorsed by EA.

## What is here
- `tools/t816/` the static 65816 -> C translator (generator, runtime headers, natives, configs, lockstep checks against the oracle).
- `tools/cpu816/`, `ecs/tools/cpu816x/` a 65816 emulator ("the oracle") used to check the translated code instruction for instruction, plus helpers.
- `ecs/game/` the Amiga game: `src/` (the program: shop, overworld, event runner, c2p, audio, WHDLoad slave), `native/` and `runtime/` (hand-written replacements for hot routines and the runtime), `cfg/` (translator configuration per event), `tools/` (data builders, emulator test drivers, timing and analysis scripts), `build.sh`, `build_whd.sh`.
- `ecs/docs/` results, acceptance report, performance budget and the history of what was done and measured (`RESULTS_GAME_BUILD.md`).
- `docs/` how the translator and the original's structure work (`TRANSLATOR.md`, `AUDIT.md`, ...).

## What is not here, and what that means
The build expects the original's extracted data (`work/res`, `work/raw`, the screens, sprites, songs and event images derived from them) and a few generated headers (`ecs/game/src/menu_data.h`, `water.h`) that are made from it by the tools (`tools/extract_all.py`, `tools/make_demo_assets.py`, `tools/make_menu_data.py`, `tools/make_event_audio.py`, `ecs/game/tools/make_data.py`). Those steps and their inputs are described in the docs, but **I have not tried a clean build from this repository alone**; expect to fill gaps.

## Building (with the data in place)
    cd ecs/game
    ./build.sh            # program, five event overlays, data/ -> dist/ (needs m68k-linux-gnu-gcc/binutils, python3, numpy)
    tools/mkhdf.sh dist/SkateOrDie.hdf     # test HDF (needs amitools: rdbtool, xdftool)
    ./build_whd.sh        # WHDLoad slave + data -> dist_whd/ (needs 2 MB Fast RAM at run time)
Emulator tests use FS-UAE (A1200, cycle exact), Xvfb and xdotool: `ecs/game/tools/run_uae.sh`.

## Licence
The code and documentation in this repository are released under the MIT licence (`LICENSE`). That licence covers only the material written for this project; it gives no rights to *Skate or Die* or to any Commodore/Amiga ROM.

## Credits
Original *Skate or Die*: Michael Kosaka, Stephen Landrum, David Bunch (Apple IIGS 1.1: David Bunch, Michael Kosaka, Michelle Shelfer), Electronic Arts, 1988. Amiga version by Boatofcar.
