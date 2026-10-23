# Target change: 68020, AGA, 2 MB Chip, no Fast RAM; delivery = executable + data folder

Agreed by you after the first gate verdict (68000/ECS/1 MB did not work):
| Item | Now | Was |
|---|---|---|
| CPU | 68020 (assumed A1200 class, 14.19 MHz, no cache tricks beyond the 020's own 256-byte cache) | 68000 @ 7.09 MHz |
| Chipset | **AGA** (you confirmed) | ECS |
| Memory | 2 MB Chip, **no Fast RAM** (my reading of "2MB ChipRAM": nothing else is mentioned) | 1 MB Chip |
| Video | PAL 50 Hz, 320x200, 16 colours | same |
| Delivery | **Amiga executable + data folder** (assets are files in the folder, not built into the program; the HDF/ADF is only a way to carry it) | HDF |
Unchanged assumptions: update contract = each event runs at the source pass rate (ramp 29.3, high jump 26.7, race 11.1, jam 9.8 per second, KEGS timing), no actor/effect/audio cuts.

## What was measured
Bench (`ecs/bench/`): a copy of the earlier AGA demo that starts straight in one event, with other events and all large audio replaced by silent stubs, no pacing, and an on-screen counter (passes in the last second / lowest second after 8 s / average). It runs in FS-UAE 3.1.66 as A1200, 68020, 2 MB Chip, 0 Fast, Kickstart 3.1, with `uae_cpu_memory_cycle_exact` (UAE's approximation of CPU/chip-bus timing). Input is a seeded synthetic stress (fast left/right waggle, fire pulses, now and then up/down), not a replay of the source corpus.
Two changes to the earlier picture conversion were tried because Chip-only memory makes the 256 KB lookup table expensive: **conversion that skips 8-pixel groups whose chunky bytes did not change** (a shadow copy per display buffer; `ecs/bench/c2p/apply_diff_c2p.py`). Race on the bus model of a 16-bit-chip-bus machine went 8 -> 9-10 passes/s with it; on the A1200 the stress numbers below all include it. A table-free register conversion was derived and verified (`c2p/derive_c2p.py`, 6 exchange steps) but not benchmarked: by instruction count it is not cheaper.

## Results (A1200 class, Chip only, unpaced, counter read from screenshots at about 50 s and 100 s of one run per event)
| Event | needed (source, mean) | Amiga mean | lowest second | headroom on the mean | verdict |
|---|---|---|---|---|---|
| Freestyle Ramp | 29.3 | 30 | 12 | +2% | at the limit |
| High Jump | 26.7 | 26 | 7 | -3% | **slightly short** |
| Downhill Race | 11.1 | 14 | 11 | +26% | meets (lowest second = the needed mean) |
| Downhill Jam | 9.8 | 12 | 10 | +22% | meets (lowest second = the needed mean) |
Notes: the ramp/jump averages include the pauses between attempts (the source's averages do too); without stress input the same builds showed ramp 32-43 and high jump 31-32. With Fast RAM (8 MB) the earlier build ran ramp 40-54, jump 39-47, race 15, jam 14 without the new conversion, so Chip-only costs the ramp/jump events about a third of their speed.
For reference, on the bus model of a 16-bit-chip-bus ECS machine (no longer the target) the same code gave ramp 22, jump 19-21, race 9-10, jam 8-9: all short.

## Memory (2,097,152 bytes of Chip)
Measured in the bench: program with one event's image about 0.6 MB; one event's working memory 0.66 MB (IIGS banks) + 0.26 MB (conversion table) + 0.06 MB (shadow copies) + 0.1 MB (screens, copper list) = about 1.1 MB. So about 1.7 MB in use with silent audio. The full game also needs the song (the AGA build's raw 16 kHz songs are 0.58-0.92 MB each: too big; they must be compressed, for example IMA ADPCM as the title song already is, about 0.15-0.23 MB) and the OS (about 0.1-0.2 MB, my estimate). **Peak is estimated at 1.9-2.0 MB: it fits only just.** Shrinking the 256 KB table (or removing it) is the first thing to do.

## What this means for "executable + data folder"
* `SkateOrDie` executable plus `data/` (event images, screens, sprites, font, compressed audio), found through `PROGDIR:`. Only the event being played is loaded, which fits the memory picture above and replaces the earlier 6.5 MB single executable.
* The earlier demo takes over the machine; loading a data file needs AmigaDOS, so each event/screen load must happen while the system is alive (before the takeover or by handing the machine back). That is a design item, not yet done.

## Limits of this evidence
FS-UAE's memory-cycle-exact mode approximates timing: nobody has run this on an A1200. One run per event, three readings of a counter I read by eye; synthetic input, not the source corpus; audio is silent in the bench (real audio adds a little DMA contention and mixing work); the 14.19 MHz clock is an assumption; Joust was not benchmarked.
## Verdict for the gate
On the new target the downhill events fit with 22-26% headroom on average; ramp and high jump do not have the required 20% headroom (ramp +2%, high jump -3%) and memory is within a few percent of the limit. Those are optimisation targets (sprite/logic path of the ramp and jump overlays, smaller conversion table), not proven blockers.
