# Work brief: where the ROM images are (for future cloud transcodes)

Read this before any `/rom-first` run that needs an Amiga emulator (FS-UAE) or the original-machine emulator (KEGS/MAME).
The repository `johnshawler/sod-amiga` is **private** (checked with the GitHub REST API: `private: true`). The Kickstart ROMs are copyrighted, so keep it private and do not copy these files into any public repository, artifact or release.

## 1. Amiga Kickstart images (committed in the repository)

Folder: `roms/` at the repository root (a fresh cloud session clones it with the repo, so they are there with no extra step).

| File | Size | SHA-1 | Use |
|---|---|---|---|
| `roms/Kickstart v3.1 r40.068 (1993-12)(Commodore)(A1200)[!].rom` | 524,288 | `e21545723fe8374e91342617604f1b3d703094f1` | **The ROM for every test of the AGA / A1200 target.** Boots 68020, AGA, 2 MB Chip. |
| `roms/Kickstart 1.3 34.5 A500-A2500-A3000-CDTV (Guardian 1.2 patched).rom` | 262,144 | `1ec9695989013d3e2f675ef321e7e95372e359bf` | OCS/ECS machines only (A500 class). It is the *Guardian-patched* 1.3, so it is not a clean reference ROM and **cannot run AGA**. Use it only for an ECS/OCS target, and say so in the report. |

Both were supplied by the user. Copies of the uploads may also sit in `/root/.claude/uploads/<session id>/` while a session lives (v3.1 is zipped there); that folder disappears with the container, so the repository copy is the permanent one.

How the tools find them:
* `ecs/game/tools/run_uae.sh` uses the v3.1 ROM by default (path relative to the script: `../../../roms/Kickstart v3.1 ...rom`). Override with `ROM=<path> ecs/game/tools/run_uae.sh ...`.
* `ecs/bench/run_bench.sh` and the older `tools/*.sh` UAE scripts take the ROM from the same folder.
* FS-UAE config lines that matter: `kickstart_file = <ROM path>`, `amiga_model = A1200`, `cpu = 68020`, `chip_memory = 2048`, `fast_memory = 0`.

If a ROM is ever missing: `git checkout -- roms/` restores it from the repository; failing that, ask the user to attach it again (never download one).

## 2. Source-machine ROMs (Apple IIGS) are NOT in the repository

The original game runs in KEGS 1.38 built from source (`ecs/tools/kegs-harness/build.sh`). It needs the IIGS ROM and the game disk, neither of which is committed (`ecs/README.md`):

| Item | Where it was when this brief was written | Notes |
|---|---|---|
| ROM 01 (`rom01`, 131,072 bytes, CRC `42f124b0`, MAME 342-0077-b) | inside the user's upload `apple2g1.zip` (`/root/.claude/uploads/<session>/…-apple2g1.zip`); extracted copy under the session scratchpad `romsets/` | This is the ROM the harness runs the game on. `ecs/tools/run_hx.sh` reads it from `$ROM01`. |
| ROM 03 (`rom03`, 262,144 bytes) + `apple2gs.chr` | inside the user's upload `apple2gs.zip`; also under the scratchpad | Not used for the capture runs. |
| Game disk `Skate_or_Die.2mg` | `/root/.claude/uploads/<session>/…-Skate_or_Die.2mg` (other uploads: `Skate_Or_Die.zip`, `Skate_or_Die_USA.zip`) | `$SODIMG` for `run_hx.sh`. The disk is possibly a cracked release; noted in `ecs/docs/SOURCE_GATE.md`. |

Uploads and the scratchpad vanish with the container. For a future session the user must attach `apple2g1.zip` and the disk again, **or** agree to commit them to `roms/` in this private repository (not done; not assumed). MAME's own `apple2gsr1` set does not work headless here (missing ADB microcontroller ROM `341s0345.bin`), which is why KEGS is used.

## 3. Quick start for a future cloud session
1. `git clone` the private repo (the `roms/` folder comes with it); check `sha1sum roms/*` against the table above.
2. Amiga tests: `ecs/game/build.sh`, `ecs/game/tools/mkhdf.sh <out.hdf>`, `ecs/game/tools/run_uae.sh <out.hdf> <out dir> "wait:10 key:space ..."` (needs Xvfb on :98, FS-UAE, xdotool, ImageMagick, amitools).
3. Source-machine runs: ask for the IIGS ROM zip and the disk if they are not already in the uploads folder, then follow `ecs/tools/run_hx.sh`.
4. One emulator at a time on display :98 (`run_uae.sh` now takes a lock): two runs at once send keys and `pkill` to each other and spoil the measurements.
