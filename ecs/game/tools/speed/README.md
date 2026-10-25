# Speed comparison with the NES version

Scripts used to measure how fast the NES version and the original IIGS code move (see `ecs/docs/RESULTS_GAME_BUILD.md`, "Matching the NES version's speed").

* NES side (FCEUX 2.6.5 with Lua, the ROM is not in the repository): `run.sh <script.lua> <out dir>` runs a script on `sod.nes`; `lib.lua`/`nav.lua` walk from power-on to the map and to an event. `race_nes.lua` (race scroll per frame, no input), `jam_nes.lua`/`jam_nes3.lua` (jam scroll with several inputs), `ow_nes2.lua` (map skater per direction). Paths inside `run.sh` point at the session's scratch directory: edit them.
* IIGS side (the original overlays on the 65816 oracle, `tools/lockstep_ecs.py` helpers; needs the extracted game files): `racespeed.py` (scroll rows per pass of the race / jam for scripted input), `skpos.py` (words of bank 3 that drift steadily: finds position variables), `clk.py` (pictures at given passes: the clock), `b2writes.py`, `e1writes.py`, `order.py` (which code writes bank 2 and bank 1/E1, and in which order within a pass: the basis of the no-copy window in `native/dh_engine.h`).
