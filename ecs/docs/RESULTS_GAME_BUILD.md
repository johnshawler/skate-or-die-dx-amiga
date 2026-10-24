# Results of the game build (executable + data folder) on the 68020 / AGA / 2 MB Chip model

Status: **performance target met for four of the five events; Joust holds its pace but with 12% instead of 20% headroom.** Nothing here was run on a real Amiga.

## What exists (`ecs/game`, product in `ecs/game/dist` after `./build.sh`)
* `SkateOrDie` (19.6 KB) + `data/` (7.0 MB: title/shop/map/score screens, font, skater sprites, ADPCM title song, shop song, per-event memory image `evN.pak`, per-event song `evN.raw`, effects `sfx.raw`, and the five event overlays `ramp/hj/race/jam/joust.ovl`). The program finds its data through `PROGDIR:data/`; the HDF (`tools/mkhdf.sh`) is only a carrier.
* Event code = the original 65816 overlays translated to C (`tools/t816`, `tools/gen_ecs.py`), native replacements for the hot routines (`native/`), a platform layer (`src/evrun.c`, `platform.h`, `os.h`) and the picture conversion `src/c2p.h`. Only one event is in memory at a time (loaded with `LoadSeg`).
* Machine: A1200 class (68020 at 14.19 MHz assumed), AGA, PAL 50 Hz, 320x200x16, 2 MB Chip, **no Fast RAM**, Kickstart 3.1. Tested in FS-UAE 3.1.66 with `uae_cpu_memory_cycle_exact` (UAE's model of CPU/chip-bus timing).

## Measurements (final build, paced at the event's rate, 240 s of synthetic stress input per event; `tools/run_timing.sh`, `tools/analyze_timing.py`)
Work per pass = everything except the wait for the next slot (game code, picture conversion, input), timed with CIA timers (1.4 us ticks).
| Event | pace (passes/s) | mean work | p95 | p99 | headroom on the mean | passes over their slot | on-screen counter (now/lowest/avg) | free Chip during play |
|---|---|---|---|---|---|---|---|---|
| Freestyle Ramp | 29 | 24.9 ms | 29.4 | 34.2 | **28%** | 1.0% | 29/29/29 | 333 KB |
| High Jump | 27 | 28.0 ms | 31.2 | 34.1 | **24%** | 0.5% | 27/27/27 | 325 KB |
| Downhill Race | 11 | 57.1 ms | 56.7 | 83.3 | **37%** | 0.3% | 11/11/11 | 135 KB |
| Downhill Jam | 10 | 75.5 ms | 100.8 | 117.1 | **25%** | 5.2% | 10/10/10 | 176 KB |
| Pool Joust | 16 | 54.8 ms | 59.9 | 66.1 | **12% (short of 20%)** | 2.2% | 16/16/16 | 197 KB |
A pass that runs over its slot is followed at once by the next one (no extra wait), so the average rate holds as long as the mean has headroom; the counters show the rate held for all five. The Jam's p95/p99 are above its slot (full-screen scrolling conversion); the worst single pass was 0.12 s (ramp, high jump), 0.5 s (race, jam: a scene load) and 2.2 s (Joust: a round change).
Pace rates: ramp 29.3, high jump 26.7, race 11.1, jam 9.8 are the KEGS means of the source runs (`SOURCE_GATE.md`). **Joust:** the gate's 13.6-14.4/s is the mean over all event frames including the black round-load stretches; counting only one-second windows in which the original actually ran passes, its mean is about 16/s (one-second windows run 10-24/s, mostly 10-20). I paced Joust at 16. At 14/s the same measurement is 23% headroom; at 16/s it is 12%.
Unpaced (maximum speed, same input): ramp 40/s, high jump 36/s, race 17/s, jam 13/s, joust 18/s.

## What was done to get here (all verified, none of it lowers fidelity)
All natives and generator changes were checked against the original machine code: `tools/nativetest.py` (native routine vs the original on real game states: ramp/HJ/joust/race/jam, 40-200 samples each, 0 differences), `tools/lockstep_ecs.py` (whole overlay, translated vs original on the oracle, memory banks, registers, write log hash and picture: identical for all five events), `tools/c2ptest.py` (68020 conversion kernels run in Unicorn against a first-principles reference, 4 variants x 300 random rounds, 0 wrong).
* Natives added for the recolour loop (ramp, HJ, joust), the joystick/paddle routine (ramp, HJ, jam, race, joust), the text glyph (jam, race), faster rectangle push; direct word reads/writes for known banks; indexed accesses with a known bank compiled as direct loads (`tools/emit_ecs.py`); 256 KB conversion table for race and joust (2-14% of a pass; the jam has no memory for it); shorter paddle timers (same decisions); pacing against CIA time-of-day.
* Tried and dropped (measured slower or no gain): a 16-pixel-at-a-time conversion kernel (the loop no longer fits the 020's 256-byte instruction cache), 64-bit bitplane fetch, `-Os` (-25%), `-O3` (same), bank 3 pointer in a register (same).
* Sampling profiler (`-DPROFILE_PC`, `tools/run_prof.sh`, `analyze_pf.py`) found the real costs; the machine is bound by Chip-RAM accesses and instruction fetches, not by instruction counts.

## Audio check (headless; nobody has listened)
Recorded the emulated Paula output (`AUDIO=1 tools/run_uae.sh ... rec:name:seconds`, `tools/audiocheck.py`): title, shop and all five event songs play continuously, no clipping (peak 0.20-0.25), no silent seconds, and the long-term spectrum of each recording correlates 0.95-0.99 with the data file it should be playing (right pitch, no dropouts). Whether the songs are the right songs for each event (mapping inferred, Joust's song by elimination) and how they sound is **not verified by ear**.

## Corrections to earlier statements
* The first paced builds counted only the frames they waited, so their real rate was lower than printed; my earlier "held 12/11" statements for race/jam in that build were wrong, and the HDFs delivered before this were slower than I said (that is why you saw the events running slowly). Pacing now uses the CIA clock and the numbers above are measured with it.
* An earlier timing batch was spoiled by me running a second emulator on the same display (keys and `pkill` went to the wrong instance); it was discarded and redone one emulator at a time (`run_uae.sh` now takes a lock).

## Not verified / known gaps
* Synthetic waggle input, practice mode against the computer opponent; not the source corpus replays; no competition (several players) or two-player play; no name entry run; walking the overworld to an event door was not driven automatically on this build (shop to map was; events were entered with test keys F1-F5, compiled out of the product build with `DEBUG_KEYS`); natural endings (race finish, jam time-up, joust rounds) were seen for ramp and high jump (they restart in the timing loop) but not checked screen by screen for race/jam/joust; high-score/congratulation flows unchecked.
* FS-UAE's timing is a model; real A1200 timing, chip-bus contention and the audio under load are unmeasured. The 14.19 MHz clock is an assumption.
* Joust headroom 12%; Jam's 5% of passes over their slot are scroll-heavy full conversions.
* Kickstart 1.3 in `roms/` is Guardian-patched and cannot run AGA; the IIGS ROM and disk image are not in the repository (`docs/briefs/ROM-Locations-Brief.md`).
