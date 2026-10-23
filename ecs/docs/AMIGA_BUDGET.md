# Provisional Amiga budget (ECS, 68000 @ 7.09 MHz PAL, 1 MB Chip, no Fast RAM)

Everything here is an **estimate** unless it says "measured". Measured = instruction counts of the translated game code run in Unicorn (68000 build, `-O2`; `ecs/tools/cost68k/run68k.py`), plus a bus-cycle lower bound computed from the same runs (4 clocks per instruction word and per data access, +4 per taken jump/branch; no internal/idle cycles, so the true figure is higher). The counts are for the translated *game code* of the earlier AGA project (with its native sprite/fill routines); the picture conversion is **excluded** (the ECS design cannot use the AGA chunky-to-planar loop).

## 1. CPU time per pass
CPU clocks per PAL second: 7,093,790; assumed 90% available (refresh, audio, disk DMA; in 4-bitplane low resolution the bitplane DMA does not take CPU slots) = about 6.38 M.

| Event | source rate (KEGS) | clocks per pass available | translated game code, instr/pass (measured, mean / max) | same, bus-cycle lower bound (mean / max) | lower bound / available |
|---|---|---|---|---|---|
| Freestyle Ramp | 29.3 /s | 218 k | 46 k / 93 k | 343 k / 635 k | **1.57x over** |
| High Jump | 26.7 /s | 239 k | 55 k / 104 k | 407 k / 714 k | **1.70x over** |
| Downhill Race | 11.1 /s | 575 k | 54 k / 173 k | 604 k / 1.61 M | 1.05x over (max 2.8x) |
| Downhill Jam | 9.8 /s | 651 k | 60 k / 240 k | 642 k / 2.12 M | 0.99x (max 3.2x) |
| Pool Joust | about 14 /s (30 in play) | 456 k (213 k in play) | not measured: the cost run stays on the opponent screen | - | unknown |

Without the native routines the translated code costs 220 k (ramp), 244 k (high jump), 2.5 M (race), 2.6 M (jam) instructions per pass, which is 6-45 times more. The natives use unaligned 16/32-bit accesses that a 68000 does not allow (address error); they would have to be rewritten.

## 2. What is not in those numbers (and why they matter)
* **Picture path.** The game draws into a chunky (two pixels per byte) picture in IIGS memory. Converting it to four bitplanes on a 68000 costs at least ~20 instructions x ~8 clocks per 8-pixel group; a full-screen scroll is 8,000 groups = ~1.3 M clocks (estimate), against 575 k available for a whole race pass. The race picture changes by up to 13.8 KB (43%) per pass, so even converting only changed groups costs several hundred thousand clocks. CPU conversion is not viable.
* **Blitter conversion** (estimate): about 3-4 blitter passes over 16,000 words, 4 colour clocks per word at 3.55 MHz = 54-72 ms per full-screen conversion, against a race pass of 90 ms and a jam pass of 102 ms; the blitter also takes chip-bus slots from the CPU while it runs. Possible only with little margin, and it competes with sprite blits.
* **Alternative: draw planar** (redo the renderer: planar backgrounds, blitter bobs, hardware scroll). This removes the conversion but means re-implementing what the IIGS overlays do with their own sprite and scroll code, and re-checking pixel fidelity against the KEGS captures. Not attempted; effort and risk are large.
* Audio (NES music and effects through Paula; sample data is chip RAM), input, interrupts: small in CPU time (estimate under 3%), but samples cost memory (below).
* Real 68000 timing is slower than the lower bound (indexed modes, shifts, branches): expect +25-40% (estimate).

## 3. Proposed allocation per pass (race, at 575 k clocks; estimate, does not close)
translated game logic ~150 k (measured instructions x ~3 clocks of internal overhead are not included), sprite/blit work 50 % blitter + ~60 k CPU set-up, scroll/picture ~blitter 54 ms + ~40 k CPU, audio/input/interrupts ~20 k, safety margin 20% = ~115 k. Sum with the lower bound exceeds the budget; **no allocation that meets the update contract has been found**.

## 4. Memory (1,048,576 bytes of Chip; no Fast)
| Item | estimate |
|---|---|
| Display: 2 buffers x 4 planes x 40 bytes x 200 rows | 64,000 |
| Event memory image: non-zero bytes of the initial image (measured from the paks: ramp 194 KB, high jump 193 KB, race 178 KB, jam 164 KB) plus the zero-filled working areas (picture mirror 32 KB, background buffer up to 64 KB, variables) | 330-420 KB |
| Translated game code (68000 build, measured: ramp 136 KB, HJ 109 KB, race 152 KB, jam 202 KB) | 110-200 KB |
| Audio samples/streaming buffers (4 NES songs were ~0.5 MB raw in the AGA build; must be shortened/compressed) | 100-250 KB |
| Kickstart/DOS resident use with a booted HDF (1.3/3.1, no Workbench) | 100-200 KB |
| **Total** | **about 0.75-1.1 MB** |
Race/jam are at the top of that range. The full IIGS memory model (9 banks) alone is 576 KB. A trimmed model (only the pages the event touches), compressed audio and a custom loader that frees the OS would be required; none of this has been built.

## 5. Buffering, DMA, bottlenecks
Double-buffered 4-plane 320x200 display in Chip RAM (copper-flipped, one flip per game pass). All code and data share the chip bus with display, audio and blitter DMA; 4-plane low resolution leaves the CPU its normal slots, 5 planes would not. Likely bottlenecks, in order: picture conversion/rendering for the two scrolling downhill events, CPU time of the sprite engine in the ramp/high jump, chip-RAM size, then audio memory.

## 6. Verdict
The gate's three outputs exist (`docs/SOURCE_GATE.md`, `evidence/`, this file). They do **not** support a credible implementation plan on the declared target:
1. Ramp and High Jump exceed the CPU budget by 1.6-1.7x on the lower bound alone.
2. Race and Jam have no CPU left once the picture path (>= 54 ms of blitter or an unbuilt planar renderer) is added.
3. Memory is estimated at 0.75-1.1 MB of 1 MB with nothing built.
What would change this (needs your decision): a 68020 or Fast RAM (the earlier AGA/68020 work shows the translated events running at about the source rate), a lower update rate than the source (violates the update contract; for example 15 passes/s for ramp/jump and 5-6 for the downhill events), or a commitment to a new planar/blitter renderer with its fidelity risk.
