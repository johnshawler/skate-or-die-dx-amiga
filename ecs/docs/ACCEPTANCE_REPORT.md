# Acceptance report: Skate or Die, Apple IIGS -> Amiga A1200 (AGA)

Maps each requirement of the `/rom-first` brief (`BRIEFS.md`, part 1) to its result. **Status: not finished in the brief's sense.** The game is complete and plays in all five events with the shop, competition and high scores; the open items are listed in section 7. Everything was run in FS-UAE's A1200 model; **nothing was run on a real Amiga.** Artifact identities and checksums: `MANIFEST.md`. Detailed results and method: `RESULTS_GAME_BUILD.md`; source evidence: `SOURCE_GATE.md`, `evidence/`.

## 1. The contract (what was agreed, what was assumed)
| Item | Value | Status |
|---|---|---|
| Game / source | Skate or Die (EA, 1988), Apple IIGS, game disk `Skate_or_Die.2mg` | verified |
| Source inputs (SHA-256) | `Skate_or_Die.2mg` `4f24ff981fb84b89ac03857a1ce6d6e425a13e060f6731b501b497b495b01a2c`; `Skate_Or_Die.zip` `62b9f36857f33aae042a1d4ef65fee0448502b8ad4fdbb553f2627a8b263e3e6`; `apple2g1.zip` `30cfecd94e5d5a5a117054f0a9bac239d524823321c69213099ad87838040c9e` containing ROM 01 (`rom01`, 131,072 bytes, SHA-256 `34cd454c6201bfd26839d6ace2ff6b1231e09d6279e49b4890311756dc3825fb`) | verified; none of these are in the repository |
| Source emulator | KEGS 1.38 (`digarok/gsplus` commit `41e71031dbfc...`) with a harness patch (`ecs/tools/kegs-harness`), ROM 01, 2.8 MHz, slot-5 3.5" drive | verified (`SOURCE_GATE.md`) |
| Amiga CPU | 68020 at an **assumed** 14.19 MHz, no accelerator, instruction cache on | assumption |
| Chipset / memory | AGA, 2 MB Chip, **no Fast RAM**, Kickstart 3.1 r40.068 A1200 (SHA-256 `6d43840d4099a74170ea0f0425b6257c3891ebcaa39c4d1840075a9ab22b5707`) | agreed; a Fast RAM machine is untested |
| Video | PAL 50 Hz, 320x200, 16 colours (4 bitplanes) | verified |
| Update contract | each event at the source pass rate from KEGS (ramp 29, high jump 27, race 11, joust 16 passes/s; jam 12, which is 20% above the original's 10, by your request), presented at 50 Hz, **>= 20% headroom** on the mean | proposed by me, accepted by you |
| Fidelity contract | gameplay, graphics/palette/HUD, controls: the original's code runs translated; audio: the NES songs and effects (your decision); no reduced actors, colours or rates | see section 3 |
| Delivery | executable `SkateOrDie` + `data/` folder (`PROGDIR:`); the HDF is only a carrier | agreed |
| Final milestone | not pinned down in writing. Used here: cold boot -> title -> shop -> sign-up / practice / competition -> all five events playable -> results, high scores, back to the shop | **my definition; please confirm or change** |

## 2. Source-first gate (brief section 2)
* **Hotspot report:** `evidence/hotspots.jsonl`, `SOURCE_GATE.md` (race 11.1 passes/s, jam 9.8, slowest second 7; ramp/high jump near 29/27 and not CPU-bound; picture change up to ~43% of bytes per pass in the race).
* **Replay corpus:** `evidence/cases/SRC-*` (10 cases: five events x waggle/random input, seed 1, cold-boot scripts, per-frame CSVs, screenshots, hotspot windows).
* **Provisional budget:** `AMIGA_BUDGET.md` (written for the first ECS/68000 target; the target was later changed to the 68020/AGA machine in `TARGET_020_AGA_2MB.md`).
* **Coverage gaps (stated at the time):** finishing a race, jam to its time limit, competition, two-player play, name entry, the joust beyond the first rounds, the keypad scheme; KEGS cannot save states, every case is a cold boot; real hardware timing never measured.

## 3. Fidelity (brief section 3)
| Check | Method | Result |
|---|---|---|
| Event code | `tools/lockstep_ecs.py`: translated overlay vs original on the 65816 oracle, same inputs; compares write log, all memory banks, registers, picture | identical for all five overlays (re-run with the final runtime) |
| Native routines | `tools/nativetest.py` on recorded game states | 0 differences |
| Competition driver (INTER) | `tools/intertest.py` vs the original, faked events | identical in all scenarios tried (1, 3, 5 players; practice; compete-all) |
| High-score update | `tools/hstest.py`, 1,600 random trials against F39's code | identical |
| Picture conversion | `tools/c2ptest.py` (68020 kernels in Unicorn) | 0 wrong |
| Independent image comparison | **not done**: no systematic comparison of Amiga screenshots against the KEGS captures; the lockstep picture check shares the oracle's model of the screen | **gap** |
| Audio | measured spectrum/level, **listened to by you: "great"** | accepted |

## 4. Performance (brief section 4; FS-UAE A1200 model, paced, 240 s of synthetic stress input per event; method in `RESULTS_GAME_BUILD.md`)
| Event | pace | mean work | p95 | p99 | headroom on the mean | passes over slot | free Chip |
|---|---|---|---|---|---|---|---|
| Freestyle Ramp | 29 | 24.9 ms | 29.4 | 34.2 | 28% | 1.0% | 333 KB |
| High Jump | 27 | 28.0 ms | 31.2 | 34.1 | 24% | 0.5% | 325 KB |
| Downhill Race | 11 | 57.1 ms | 56.7 | 83.3 | 37% | 0.3% | 135 KB |
| Downhill Jam (12/s: 20% faster than the original, as you asked) | 12 | 55.6 ms | 80.0 | 96.2 | 33% | 4.8% | not re-measured |
| Pool Joust | 16 | 54.7 ms | 57.2 | 63.2 | **12% (target 20% not met)** | 1.7% | 197 KB |

All five hold their pace (on-screen counter 29/27/11/12/16; the jam's lowest second was 11, the others equal to their average). The race runs at the original's rate on this model (its in-game clock advances the same as on the IIGS emulator within about 1%) and the jam 20% faster than the original; the NES version updates at 60 per second, this port does not. Worst single pass 0.12 s (ramp, high jump), 0.5 s (race, jam: a scene load), 2.2 s (joust: a round change).

## 5. Acceptance gate (brief section 5)
| Requirement | Result | Evidence |
|---|---|---|
| Overworld map animation | Rodney's neon sign alternates colour every 16 map passes and the fountain/pond redraws one of four frames every 4 passes (F39's code, resource F35); checked in FS-UAE screenshots (sign magenta/yellow, pond image changes), not against a KEGS capture | |
| Cold-boots from a clean launch into the playable game | yes (boot HDF, `S:Startup-Sequence` runs `SYS:SkateOrDie`; title, then the shop) | FS-UAE runs of the product HDF |
| Independent of development paths, services, test scripts | yes: `PROGDIR:data/`; test keys, timing and stress input are build options (`DEBUG_KEYS`, `BENCH_*`, `TIMING`) that `build.sh` leaves off | `build.sh` |
| Sustained heavy play across the corpus | 240 s per event paced, 9 minutes of practice in the race and jam, 40+ minutes in a competition race, no failure | `RESULTS_GAME_BUILD.md`, section 6 |
| Controls, audio, collisions, death/retry, transitions | controls (joystick/keys; second player in two-player events: digits 1-9 and Shift), audio and transitions yes; the shop, practice, competition turns, results screens, high scores and the winner's congratulation follow the original's code | section 6 |
| Game completion / final milestone | see section 1 (my definition) and section 6 | |
| Playable after automation ends; no auto-exit | yes; the product has no test keys and no timeout | `build.sh` |
| No unresolved failure against the contract | **one: Joust headroom 12% < 20%** | section 4 |

## 6. What was played, and how
| Case | Input | Result |
|---|---|---|
| Ramp, high jump, joust, practice | synthetic stress input (waggle) | each ends by itself (ramp and high jump about 35 s, joust about a minute) and returns to the shop |
| Downhill race and jam, practice | synthetic input | **do not end by themselves** (9 minutes: race clock 9:14, jam 8:46-9:26): the synthetic input never reaches the finish; ESC leaves |
| **Downhill race to its finish, real play** | **your save state** (`Saved_State_1.uss`, FS-UAE, restored here) | a practice race ended at the FINISH line (clock stopped at 1:55:2, score 2909 + bonus 100 = 3009); the skater stands; **a key press** gives "PRACTICE AGAIN? YES NO" and YES restarts the race. The race ends by itself in the original's code; what was missing was a run that reached the finish. Not yet seen: the same ending inside a competition (results screen after it) |
| Three-player competition in all events, each event ended by a test key (F6) with pseudo-random scores | test build | ramp, high jump, downhill, jam (pairs), joust (win/loss) with a results screen after each, high scores updated, congratulation, back to the shop |
| Three-player competition in all events from synthetic play | test build, stress input plus Return | ramp (3 skaters) and high jump play through by themselves; **the downhill race never ended in 40+ minutes of emulator time** (its clock stops at 9:59:9 and the race goes on), so this run did not get past it. A steering bot on the original machine drove the race to its cliff jump ("DANGER" sign), crashed, and the race did not end from there either; I did not find the race's normal end condition at the time; your save state later showed it (the FINISH line) |
| Pool joust competition, three players | stress input, Shift as the second player's button | **played to its end from synthetic input** (about 14 minutes of emulator time): bout ANNA vs BOB, then CHARLIE vs ANNA, then the results and back to the shop; two-player controls work (without Shift the second skater's "PRESS YOUR BUTTON" never completes) |
| High-score persistence | two separate emulator sessions on one HDF | session 1 wrote `data/highs.bin`; session 2 loaded it: BOB 6999, CHARLIE 3347, ANNA 954 |
| Practice prompt, ESC | test build | YES restarts, NO / `n` returns; ESC in an event or results screen returns to the shop |

## 7. Known gaps and decisions needed
1. **Joust headroom 12% on the mean** (median 19%): below the 20% contract. More optimisation is possible but not guaranteed to reach 20%; or accept 12%.
2. **Race and jam endings:** the race's natural ending is now seen (your save state, section 6) in practice mode; the race inside a competition, and the jam's end (time limit or finish), were run only with a test key (F6) ending each one, not by reaching their natural end.
3. **No independent screenshot comparison** against the KEGS captures (section 3).
4. **No real hardware**, no Fast RAM test, 14.19 MHz assumed, FS-UAE timing is a model.
5. **Final milestone** (section 1) to be confirmed.
6. Not ported because the original has nothing there: separate ceremony scenes, typed high-score names, winner pictures (the scene runner only loads files).
7. The source inputs (IIGS ROM, disk) and the Kickstart ROM are not part of the product; `MANIFEST.md` lists the Kickstart hash, section 1 the source hashes. The uploaded `Skate_or_Die.2mg` may be a cracked release (`SOURCE_GATE.md`).
