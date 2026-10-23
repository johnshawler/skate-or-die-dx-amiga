# Overworld logic (overlay F39), as recovered

Source: `work/disasm/F39_full.txt` (flow-following disassembly of the IIGS overlay F39), plus running the original code on a 65816 core (`tools/cpu816/`). Addresses are overlay addresses (loaded at `$03:1000`, data bank 3). All variables below are in bank 3 unless stated.

## How it was checked
- `tools/cpu816/overworld_sim.py` loads INTER, DRIVER and the mode-6 resources where the loader table puts them, stubs the sound driver and file calls, and injects the control byte once per main-loop pass.
- `tools/overworld_model.py` is a Python transcription of the skater movement. `tools/cpu816/fuzz_model.py` and `fuzz_model2.py` compare it with the original **pass for pass**. Result: 14 random-driving runs (4,161 passes) and 90 directed runs from random positions, headings and speeds (16,384 passes), **0 mismatches**. They reach all five map exits and the shop door. A deliberately wrong constant makes the fuzz fail in 6 of 14 runs, so the test can fail.
- `demo/src/skater.h` is the C port used by the Amiga demo; `tools/sim_amiga.py` checks its screen output against the model.

## Input
- Joystick (paddles, `$E0C064/65`, button `$E0C061`), mouse (ADB, `$E0C024`) and the numeric keypad (`1`–`9`, table at `$41F6`) are all reduced to one control byte, active low: **bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire**. It is stored at `$3062` (joystick and mouse) and `$3060` (keyboard); the game uses their AND.
- `$4150` collects input, `$354A` dispatches it: the low nibble indexes a table at `$4A47` that gives a handler list per state; the handler is reached through a self-modified `JMP`.

## States (`$4BD5`)
| State | Meaning (from behaviour) |
|---|---|
| 0 | Skating on the map (F29). The skater, with its shadow, is controlled by the player. |
| 1 | Entered through the shop door. The screen shown was not checked. |
| 2 | The shopkeeper screen (F30, verified by comparing screen memory): a flashing "SKATE OR DIE" sign is a cursor. Moving it into a hotspot and pressing fire leads to the map. |
| 8 | Leaving the map (after an exit code), restores the start position. |
| 3–7, 9 | Other screens and animations, not analysed. |

## Cursor (state 2)
- The cursor sprite (frames 64–66, the three coloured signs) steps **2 pixels per pass** in the pressed direction. It flashes by changing frame every 16 passes.
- Variables: `$4BC7` x (units of 2 px, 0..148), `$4BC9` y (px, 0..176). Screen position is `(2·x, y)`. A lock-out of about 8 passes after entering ignores the fire button.
- Hotspot rectangles (x, y in those units) are at `$4628`: 14 entries of five bytes (x0, x1, y0, y1, id). Selecting entry id 5 (x 40–80, y 170–200) with fire led to state 0 on the map. Which button label that is (practice or compete) was not confirmed.

## Skater (state 0)
- **Turn rate** `$4A` is −1, 0 or +1. Left/right push it toward ±1 (right toward −1); with no turn key it decays toward 0.
- **Speed level** `$42` is 0 to 5. Up raises it by one every 8 passes; down lowers it by one at once and then every 9 passes. There is no friction: the skater coasts at its level until braked.
- **Heading** `$40` runs from −32 to 32 (64 steps a turn, 0 = south, +16 = east, −16 = west, ±32 = north). It changes by one step every 2 passes while turning.
- **Velocity** comes from a table indexed by |heading|: `vx = |h|` up to 16 then falls back to 0; `vy = 16 − |h|` (negative past 16). The result is a diamond (|vx| + |vy| = 16), not a circle.
- **Movement** runs `speed level` sub-steps per pass. Each sub-step adds the velocity to two fine accumulators (`$BE` x, `$BC` y). Whenever one reaches ±48 it moves one unit: x in units of 2 px, y in px.
- **Sprite frames**: board `32 + a`, legs `16 + a`, shadow `48 + a`, torso `(a − turn) & 15`, with `a = ((((h − 2) & 0xFFFF) >> 2) & 15) ^ 15`. The torso leans into a turn.
- **Screen position**: the sprite coordinates in the original have an offset of (24, 50), so the torso is drawn at screen `(2·$3C − 4, $3E − 20)` plus each part's hot-spot word from the sprite table (high byte y, low byte x).
- **Start position** `$3C = $4B`, `$3E = $54` (in front of the shop door), heading 0.

## Map collisions and exits
Walls are tested only at specific map edges, per direction, with the rules in `demo/src/skater.h` (`sk_x_plus`, `sk_x_minus`, `sk_y_plus`, `sk_y_minus`). Observed exits:
- Right edge (x = `$98`): code 1 above y = `$28`, code 2 below.
- Left edge (x = 0): code 1 above y = `$28`, code 5 below (when no competition is under way, per `$0768CC`/`$077E17`).
- Bottom edge (y = `$C0`): code 3 left of x = `$2A`, code 4 right of it.
- Top edge (y = `$15`): blocked.
- Shop door (moving up with x in `$47`–`$4F` and y below `$53`): enters state 1.
Exit codes are consumed at `$1229`, which (in the cases run) enters state 8 and returns to the start position; with a competition under way it jumps to the event entry instead. The mapping from exit code to event was not decoded.

## Timing
There is no frame throttle in this overlay: `$1D93` is a random-number generator and the only waits are fixed delay loops. Speed comes from execution time. The emulator counts about 107,000 cycles per pass while skating (about 2,650 without sprite work). At 2.8 MHz that is about 26 passes a second; real memory timing makes it somewhat lower. The demo uses **25 passes a second** as an estimate (a clean 2 frames on PAL). This is the least certain number here: it could be 20–26. It is the constant `TICK_HZ` in `demo/src/ow.c`.

## Not analysed
Other states (3–7, 9), the shop interior, sign-up, board select, name entry, the mapping of exit codes to events, palette cycling of the fountain (`$1061`), the water animation placement, and the mouse path in detail.
