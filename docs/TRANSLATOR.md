# 65816 -> C translator (tools/t816) and the events

The events are overlays of the original program (F7 ramp, F8, F15, F21, F28) written in 65816 code that draws straight into
IIGS memory. Rather than reverse them by hand, tools/t816 translates an overlay into C, one function per subroutine, and
the Amiga runs that C on top of a model of the IIGS memory (banks 0-5, 7, E0, E1) plus the soft switches the game reads.

## Pieces
- `isa.py`, `analyze.py`: decoder and abstract interpretation per instruction: the M/X flags, decimal mode, DB and D
  (when constant), constant A for `LDA #`/`PHA` idioms; function partition; inline-data callees (the print-inline routine
  takes a parameter byte and a zero-terminated string after the JSR); self-modified `JMP $FFFF` sites turned into a
  dispatch through a table (config `patched_jmp`, `dispatch_table`).
- `emit.py`: C emitter. Real emulated stack (JSR pushes return-1), flags in locals synced to a global `cpu` around calls,
  I/O page accesses ($C000-$C0FF) through macros, writes to E1 mark dirty rows for the c2p.
- `runtime/`: `m816.h` (memory/flag macros), `m816rt.c` (init, run, bad-return handling), `iigs_io.h` (keyboard, buttons,
  paddle timers; shared with the oracle), `bcd.h`.
- `cfg/{ramp,ev8,ev15,ev21,ev28}.json`: per-overlay configuration (ev28, the pool joust, translates but is not integrated).
- `make_pak.py`: the event's initial memory image taken from the oracle (the game's own loader and INTER state).

## Verification
The oracle is `tools/cpu816/cpu816.c`, an emulator of the 65816 with the same I/O model. `lockstep.py` runs the original
machine code and the translated C from the same image and the same scripted input (joystick, fire, keys, random seeds)
and compares, per main-loop pass: hash and count of every memory write, the registers, all banks at the end, and which
screen rows were changed against the rows the runtime marked dirty.

Ramp: 6 random-input seeds x 2500 passes (event end included): identical write logs (1.5-12.6 million writes each), identical
memory, identical registers. About 94% of the overlay's instructions were reached by the sessions.
This proves the translated C equals the original code on the inputs tried; it does not prove the Amiga build is fast
enough, or that the platform side (c2p, input, timing) is right: those were checked in FS-UAE only by eye.

## Cost
About 198,000 68020 instructions per pass (median, -O2, counted with Unicorn). At an assumed 24 passes/s that is ~4.8 MIPS
plus the chunky-to-planar conversion: fine on a 68030/28+ MHz, doubtful on a stock 68020. The hot functions are listed by
`hotfuncs.py` (E1 push 4A63, sprite draw/save 48D5/4867/4A22/49EC, text 2C83/2DC6, joystick 44FC); native replacements
checked by lockstep are the next step for speed.

## Not done
The results flow back to INTER (scores into the high-score table), the pool joust, a pause key, the keypad control scheme, the
race/jam mode-to-exit mapping is inferred from the NES event order, real hardware/MiSTer.

## Found while checking the ramp (sprite motion)
The lockstep runs compare the translation with my 65816 emulator on the same memory image, so they cannot find a wrong image.
The skater's parts were drawn at the wrong offsets because `tools/cpu816/event_sim.py` loaded resources of types 1, 3 and 5 as
plain files. INTER's loader (`$76F91`-`$7230`) treats them differently: type 1 reads a 16-bit count into a variable and
loads only the count*2 bytes after it (F3, F24: the per-pose offsets); types 3 and 5 split a 32-byte palette from 16 KB of
graphics that go to 04:D400 and 05:0000 (F9, F16). With the wrong image every pose used its neighbour's offsets. Fixed; the
event paks were regenerated.

Other checks added: `tools/cpu816/sst_test.py` runs the SingleStepTests/65816 suite against the emulator (native mode,
300-3000 tests per opcode: all pass except decimal-mode ADC overflow, fixed, and PC/RTI/XCE corner cases the game does not
use); `tools/t816/cost/m68k_check.py` runs the translated code compiled for the 68020 (as in the Amiga build) in Unicorn and
compares memory with the original code after N passes (identical for the ramp: 300 passes).

## Sound in events
`tools/make_event_audio.py` renders the songs and effects with the NES engine. `rt_jsl()` in `demo/src/evplat.h` is the
platform side of the IIGS sound driver calls: `$8003` (effect A), `$8006` (start the event music), `$8009` (stop it).

## Making it fast enough for a 68020 (native sprite routines)
The straight translation costs about 170,000 68020 instructions per game pass in the ramp and 190,000 in the high jump, 72% of it in
the sprite engine. Six routines, identical in the ramp (F7) and high-jump (F8) overlays up to addresses, were rewritten as native C in
`tools/t816/native/engine.h`: the "push" that makes the hardware copy changed rectangles from the off-screen copy (bank 1) to the
displayed one (bank E1) by rewriting each word with shadowing on (`4A63`/`444E`), two masked blits that save the background and draw a
sprite (`4867`, `48D5` / `4252`, `42C0`), two plain copies (`49EC`, `4A22` / `43D7`, `440D`) and the 8x8 character draw (`2C83`/`26B5`).
The addresses of each overlay's variables are in `native/ramp_consts.h` and `native/hj_consts.h`; `cfg/*.json` lists which routine each
native replaces (`native.fast`).

A native routine either does the whole job or returns 0 and the translated routine runs, so unusual situations (bank crossings, odd
sizes, overlapping buffers, shadowing already on) are never handled by guesswork. When it does the job it leaves exactly what the 65816
routine would: registers, flags (including the last carry/overflow of the final row), every byte of memory including the stale bytes the
routine's `PHY`/`PHX`/`PHA` leave in the stack page, and `SEP #$30`'s clearing of the index registers' high bytes.

Checks:
- `nativetest.py` runs the original machine code on the 65816 emulator, records the state at 50-100 real calls of each routine, runs the
  native version from the same state and compares registers, flags and all nine banks; `T816_MUTATE=1` also feeds the push and the
  character routine edge cases (x = 22, 342, 24, 343, cursor at the last column and row). Every routine: identical, with a few
  character-routine calls falling back.
- `lockstep.py` with `T816_FAST=1` (natives on) compares the whole machine state at every pass instead of the write log, and checks
  that every changed pixel lies inside the span the runtime marked changed: identical over 4 seeds x 2,500 passes for random and
  fast-waggle input, both events.
- `cost/m68k_check.py` runs the 68020 build of the translated code in Unicorn and compares memory with the original: identical.

The picture conversion also got cheaper: the runtime now records for each picture row the first and last changed 8-pixel group, and
`demo/src/c2p.h` converts only those groups (it used to convert whole rows).

Result, from `cost/run.py` (68020 instructions per pass, steady state): ramp 166,000 -> about 50,000 (including the conversion to
bitplanes), high jump 177,000 -> about 56,000. In FS-UAE emulating an A1200 with a 68020 at 14 MHz and fast RAM the ramp runs flat out at
40-54 passes a second and the high jump at 39-47, against the 24 the game needs (the pass rate itself is an estimate).

A mistake found on the way: `io_poll()`, which keeps the machine alive while the game waits for a key, was called by every ordinary key
check in the main loop and waited a whole frame each time, so the events ran at roughly 2/3 (ramp) and 1/2 (high jump) of the intended
rate in the earlier builds. It now only waits after a long run of checks without a pass in between.

## The downhill events (F15 race, F21 jam)
Translated with the same tool; what they needed beyond the ramp/high jump:
- **Patched jumps and calls.** Both overlays patch `JSR $FFFF`/`JMP $FFFF` operands at run time (F15: three JSRs through 16-entry tables at
  $5264/$5284 and one JMP with 28 targets from the $507A tables; F21: one JSR through a 20-entry table at $594C and a JMP with 36 targets
  from the $56E4 tables). `patched_jsr`, `patched_jmp`, `dispatch_table`, `dispatch_targets` in `cfg/ev15.json`/`ev21.json` list them;
  the emitter pushes the return address and calls `dispatch_call`.
- **Data bank unknown at two sites.** `$36A4` (F15) and `$3B33` (F21) are reached with different DB values; `dk_dynamic` makes the
  analyser leave the data bank unknown there and the emitter use the run-time value.
- **Aux-bank switches.** The background fill (`$3674`/`$3B03`) and several other loops store through `RAMWRT`/`RAMRD` ($E0:C002-C005)
  so that direct-page-style stores land in bank E1/aux. The oracle, `iigs_io.h` (`ramrd`/`ramwrt`), the emitter (`T816_AUX`, bank-0
  accesses in [$200,$C000) redirected while a switch is on) and `m816.h` model them; `cfg` key `aux: true` turns this on per overlay.
- **INTER state.** The jam needs the opponent state INTER leaves in $7E00-$7E13 (the computer skater, "Lester"); without it the event
  waits at "PRESS YOUR BUTTON" forever. `event_sim.py` (`T816_PLAYERS`) and `ev_run` in `demo/src/evplat.h` copy it.
- **Natives.** `native/dh_engine.h` `nat_bgfill` replaces the two background fills ($3674/$3B03): their net effect is a 26,880-byte copy
  from the background image in bank 2 to $3400.. in bank 1 and E1, with a fixed register/stack residue (found by `nativetest.py`:
  60/60 identical on mutated real call states each; the first version assumed 104 rows and was wrong). The copy uses `movem.l` on the 68k.
- **Sprite loops.** The sprite draw (F15 $44D1, F21 $495D: save the background word, mask, OR the picture in, three direct-page long
  pointers) and the matching restore ($460B/$4A9C) spend their time in one nested loop each. `native.mid` in the cfg hooks a native
  into the middle of a translated function: at the loop's label the emitter calls it and, if it did the whole rectangle, jumps to the
  label after the loop; otherwise the translated loop simply runs. `nat_dh15_blit`, `nat_dh21_blit`, `nat_dh_restore` (dh_engine.h)
  reproduce X, Y, A, N, Z, C, V (the row-end SBCs run through the same macros) and the direct-page counters, and return 0 unless the
  rectangle's source pointers are in banks 4-7, the destination is below $FB00 in bank 2 and the save buffer does not touch the
  variables the loop reads. In a 1,500-pass random run all 17,900 calls took the native path.
- **Verification.** `lockstep.py` with `T816_FAST=1`, `T816_PLAYERS=1`: waggle and random input, 4 seeds x 8,000 passes each: identical machine state
  every pass (event ends inside the run, after 183-1,568 passes); instruction coverage of those runs (`cov_union.py`): race 83%, jam 90%.

Cost (`cost/run.py`, 68020 instructions per pass, mean): race 366,000 -> 152,000, jam 400,000 -> 157,000. Most of the first figure was the
picture conversion, because the whole screen scrolls every pass (8,000 8-pixel groups): `c2p.h` now uses one 65,536-entry table (256 KB)
that turns two source bytes into four pixels' plane bits, and the inner loop is 68k assembly (13 instructions a group, was 23 from the
compiler); the fill uses `movem.l`; the sprite loops are native (above). The conversion is now 2/3 of what is left (103,000 a pass).
In FS-UAE (68020, 14 MHz, fast RAM) run unpaced the race manages about 15 passes a second and the jam about 14, against the pace I
estimate for a 2.8 MHz IIGS of 12 and 11 (from 65816 cycle counts). With the cap on they hold 12 and 11. Note the frames: the display is
paced in whole frames (50 Hz), so a pass that takes slightly more than its slot drops the rate to the next step (12 -> 10 -> 8.3), which is
what the first downhill build did on the jam.
Pass rates are set per event in `ev_run` (`ev_hz`: 29, 26, 12, 11).

## Title screen
The original's start-up stub (`SOD.SYS16`, 5.6 KB) is: load the sound driver, load F52 through INTER's "load resource 43+A" entry
(`$07:6809`, A = 12), show it, start song 0, wait for any key or button 0, stop the music, `JML $076806` into the shop (mode 0).
`demo/src/ow.c` `title_screen()` does the same. The title song (NES `$31`) repeats exactly every 8,448 frames (140.6 s) after an intro of
about 300 frames; it is stored as IMA ADPCM (`tools/make_title_audio.py`, 1.1 MB) and decoded into a small chip RAM ring while it plays.
