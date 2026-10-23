# Source-first gate: Skate or Die (Apple IIGS) -> Amiga ECS / 68000 / 1 MB Chip

Status of this gate: **evidence and budget produced; feasibility on the declared target NOT shown** (see "Verdict"). No Amiga implementation has been started.

## 1. The job (agreed values, assumptions, unknowns)
| Item | Value | Status |
|---|---|---|
| Game / source | Skate or Die, Apple IIGS, Electronic Arts 1988 | given |
| Source media | `Skate_or_Die.2mg`, 819,267 bytes, SHA-256 `4f24ff981fb84b89ac03857a1ce6d6e425a13e060f6731b501b497b495b01a2c`, MD5 `4eaf38f5ddf11e9c8cc2f20005e1abf6`. The zip it came from also holds two images labelled "Alt crack" and "Logo crack"; **I cannot tell whether this image is itself a cracked copy** | verified hash; provenance unknown |
| System ROM | Apple IIgs ROM 01 (`rom01`, 128 KB, SHA-256 `34cd454c…25fb`, CRC 42f124b0 = MAME `342-0077-b`). The ROM 03 set (`rom03`) was also supplied and not used | verified |
| Source emulator | KEGS 1.38 from `digarok/gsplus` commit `41e71031dbfc…` (upstream/kegs, X11 build) with a harness patch (`ecs/tools/kegs-harness`); machine: ROM 01, 2.8 MHz ("speed 2"), 3.5" drive in slot 5 (`s5d1`), default RAM, sound dummy. MAME 0.264 `apple2gsr1` was tried first and cannot run without the ADB microcontroller ROM (341s0345.bin), which was not supplied | verified |
| Amiga CPU | 68000, 7.09 MHz (PAL), no accelerator | **assumption you confirmed** |
| Chipset / memory | ECS, 1 MB Chip RAM, no Fast RAM | given |
| Video | PAL 50 Hz, 320x200, 16 colours (4 bitplanes) | assumption |
| Update contract | each event runs at the pass rate the source shows in KEGS (table below) with no deadline misses; presented at 50 Hz; headroom target 20% | assumption (proposed) |
| Fidelity | pictures, palette, HUD, controls, NES music/effects as before, no actors/effects removed | assumption |
| Runtime | HDF with Kickstart 1.3 (the supplied, Guardian-patched image is not a clean ROM) or 3.1; emulator not fixed yet | unknown |
| Test extent | five events from cold boot; about 7.5 emulated minutes of stress input per case; end-of-event flows only as seen | proposed |

KEGS timing is an approximation of a real IIGS (it rounds slow-RAM cycles and does not model every stall). The pass rates below are the pass rates of **this emulator**, not measurements of real hardware.

## 2. What was run
Cold boot (ROM 01 + the disk image) -> title -> Rodney's shop -> GO PRACTICE -> overworld -> the event's exit, with joystick input scripted at the paddle-poll level (`ecs/tools/make_script.py`, `route_plan.py`, `gen_stress.py`). The routes to the five exits were planned with my overworld model and then **played on the original game in KEGS**; all five events were reached (screens in `evidence/cases/*/screens`). Stress input: *waggle* (fast left/right alternation, fire pulses, now and then up/down) and *random* (random directions/fire with exponential holds), seed 1, run until emulated frame 36,000 (10 min of IIGS time from boot, about 7.5 min of event time).

Replay: the harness logs one line per 60 Hz frame (`frames.csv`: emulated 1 MHz cycles, joystick polls, 65816 main-loop passes (a counter on the overlay's pass-hook address), Ensoniq register writes, changed bytes of the Super Hi-Res picture, picture hash, video mode) and optionally dumps the SHR memory. Runs are **deterministic**: two cold-boot runs of `SRC-race-waggle-s1` gave identical per-frame hash, poll and pass sequences (checked with a hash over the CSV columns).

## 3. Ranked cases (source side)
| Case | event | passes/s (mean) | slowest 1 s window | busiest 2 s (changed picture bytes) | notes |
|---|---|---|---|---|---|
| SRC-jam-* | Downhill Jam | **9.8** | 7 | 204k-228k | arena scroll + 2 skaters + obstacles; 2 joystick polls per pass |
| SRC-race-* | Downhill Race | **11.1** | 7 | 131k-303k | whole-track scroll, grass dither, flags |
| SRC-hj-* | High Jump | 26.7-30.2 | (gaps while a jump resolves) | 14k | half-pipe, few sprites |
| SRC-ramp-* | Freestyle Ramp | 29.3-30.1 | (gaps between attempts) | 17k | half-pipe, few sprites, tricks |
| SRC-joust-* | Pool Joust | 13.6-14.4 (up to 30 in play) | gaps while rounds load | 33k | dithered pool, 2 jousters; screen goes off for long stretches between rounds (11k-19k frames in the runs) |

Per-case numbers: `evidence/cases/<id>/summary.json` and `evidence/hotspots.jsonl`. Hotspot windows (the 2 s with the fewest passes and the 2 s with the most picture change) were replayed from a cold boot and captured every 30 frames (`hotspot_slow/`, `hotspot_busy/`).

What the source does under load: the game runs flat out, so **its pass rate is its load signal**. The downhill events run at roughly 10-11 passes a second and dip to 7 in the slowest second; the ramp/jump events run near 30 and are not CPU-bound. The slowdown is already present in the original (races are visibly choppy); it is not something the Amiga port introduces. The picture changes by up to about 43% of its bytes within a pass in the race (about 13.8 KB of 32 KB), mostly the scrolling grass dither; flat road areas do not change.

## 4. Coverage and gaps (stated budget: one session, two input styles, seed 1)
Covered: cold boot, title, shop navigation, overworld routes to all five events, each event's start prompts, 7+ minutes of play per case (ramp: tricks, crashes, restarts; race: obstacles, "ON THE EDGE", scrolling course; jam: arena, Lester, scores; joust: opponent choice, bouts with scores).
**Not covered**: finishing a race or running the jam to its time limit and seeing the results/return-to-shop flow; competition mode (signed-up players), two-player play, name entry; the joust beyond the first rounds; the keypad control scheme; the "alt" crack differences; any state the two input styles did not reach. Instruction-level coverage of the original overlays by these runs was not measured on KEGS (the earlier oracle runs reached 83% of the race overlay and 90% of the jam overlay).
Inaccessible/unsupported: KEGS cannot save/restore states here; every case starts from a cold boot (about 80 emulated seconds before the shop, about 40 s of disk loading per event).
