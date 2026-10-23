# Rodney's shop, sign-up, name entry, board colour and high scores (overlay F39, states 1-9)

Source: `work/disasm/F39_full.txt` plus runs of the original code on the 65816 core. Addresses are overlay addresses (data bank 3).
The C port is `demo/src/menu.h`; its tables are generated from the overlay by `tools/make_menu_data.py`.

## One pass of the main loop (`$1000`..`$104D`)
`INC $3038` (pass counter) → input (`$4150`) → sprite engine (`$4200`, `$44E6`, `$43D0`) → setup requests `$15E1`, `$18BD`, `$16FA`, `$19BB`, `$1A40`, `$178E`, `$1229` → Rodney's speech (`$1669`) → colour cycling (`$1061`) → `$3094` (copy sprite slots to the shadow copy `$114A`, state handler through `$354A`, skater movement, random numbers).
A request flag set by a handler is therefore acted on at the start of the **next** pass.

## States (`$4BD5`)
| State | What it is | Handler for "no direction" |
|---|---|---|
| 0 | skater on the map | skater (see OVERWORLD_F39.md) |
| 1 | transient: the shop was requested (`$60`) | `$3618` |
| 2 | at the counter, the sign is a cursor | `$3618` (after the cursor moves) |
| 3 | "choose existing player" list (hotspot 2) | `$3712` |
| 4 | transient: board-colour screen requested (`$62`) | `$3781` |
| 5 | board colour selection | `$3781` |
| 6 | name entry | `$3848` → `$38DF` |
| 7 | sign-up list (hotspot 0) | `$3A7B` |
| 8 | notice "MUST REGISTER AT SKATESHOP" | `$3B91` |
| 9 | high scores (hotspot 3) | `$3BCB` |

Request flags: `$60` counter (F30), `$62` board colour (F33), `$64` player list (F31), `$66` name entry, `$68` sign-up list (F31), `$6E` high scores (F32), `$70` also high scores, `$6C` exit code of the map. Each setup routine zeroes the sprite slots, fades the old picture out, copies the new one and fades it in (17 steps of about 30 ms each way).

## Rodney's counter (state 2)
- Control byte: bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire, active low. The sign moves 2 px (x, units of 2 px, 0..`$94`) / 2 px (y, 0..`$B0`) per pass. Start (x,y) = (`$4E`,`$74`). The lock-out `$C6` = 8 counts down only on passes without fire.
- Hotspots (table `$4628`, 14 × x0,x1,y0,y1,id, first match wins; no match → id 255). Labels confirmed by looking at the screen:

| id | place | fire does | Rodney says (string no.) |
|---|---|---|---|
| 0 | left sign "SIGN-UP" | sign-up list (state 7) | 0 "SIGN IN HERE IF YOU WANNA COMPETE" |
| 2 | lower right sign | player list (state 3) | 2 "WANNA CHANGE BOARD COLOR?" |
| 3 | mid left sign | high scores (state 9) | 3 "I KEEP ONLY THE BEST HIGH SCORES" |
| 4 | "GO PRACTICE" (bottom left) | `$077E17` := 1, then out onto the map | 4 "QUIT YER YAKKIN' AND GO PRACTICE" |
| 5 | "GO COMPETE" (bottom, next) | out onto the map (`$077E17` stays 0) | 5 "GETTIN' SERIOUS? SO GO AND COMPETE" |
| 129 | top left corner | nothing | 1 "SOME DAY YOU MIGHT BE ON A POSTER!" |
| 134..138 | objects around Rodney | nothing | 6 "USE THIS BOPPER IN THE POOL JOUST", 7 "DONCHA' LIKE MY 'DO'?", 8 "WATCH THE CORNERS IN POOL JOUST", 9 "SO WHAT ARE YOU STARING AT?!", 10 "SEMPER FI OR DIE!!!" |
| 139 | Rodney's chest | nothing | one of 11..18 chosen by the random generator (`$1D93`) |
| 255 | nothing | nothing | 19 (blank); 20 ("CONGRATS…") when `$4BD3` is set after an event |

Speech is 4 lines of 9 characters at text column 5, rows 6..9, white (`$FFFF`); it is drawn only when the hotspot id changes (`$4BCD`).
After `GO PRACTICE` or `GO COMPETE` the exit of the map is consumed at `$1229`: with a player signed up or practice chosen it jumps to the event entry (`$12EE`, not ported); otherwise state 8 shows " MUST REGISTER AT SKATESHOP " and a press of fire returns to the shop.

## Lists (states 3 and 7)
- Rows are 16 px apart starting at y = 68; names at byte column `$30`; the selection arrow `>` (glyph `$3E`) at byte column `$24`.
- State 7 rows: the players (`$0768CC` of them), a blank row for a new player, then "CANCEL" (grey, mask `$2222`). State 3: the players then "CANCEL".
- Up/down act only on passes where `$3038 & 7 == 0` (even while the lock-out runs); the repeat counter `$C4` starts at 8 and a step happens when it goes below 0, so a held key moves one row per ~72 passes after the first step. Wrap uses `$C8` (count) and `$CA` (last row).
- Fire on a player row → name entry (state 6) to edit it; on the blank row → name entry for a new player; on CANCEL → back to the counter. State 3: fire on a player → board colour for that player (with `$4BD1` = 0: back to the counter afterwards); on CANCEL → counter.

## Name entry (state 6)
- Keyboard routine `$3EE9` reads `$E0C000`. Accepted: lowercase letters (shown upper case), digits, space, `-`; Return = `$FF`, Delete/Backspace = `$FE`. **Upper-case key codes (`$C1`..`$DA`) are ignored** by the original. Maximum 15 characters; a leading space is ignored. A key equal to the previous one (`$D2`) is ignored until a pass sees no key.
- The first character typed wipes the old name (a 15-space string drawn at the row). Characters are drawn at byte column `$30 + 4·n`; a `*` cursor is drawn at the next column on passes without a key (not when 15 are typed).
- Delete: erases the glyph (glyph 0), then a space after it (unless that is the 15th column), then the `*`.
- Return with a name → count+1 if it was the blank row, board index := 1, state 4 → 5 (board colour). Return on an empty name: back to the counter; if the old name was erased (`$4BCF` set) the entry is **deleted** and the later ones move up. The original shifts the colour words by byte offsets instead of word offsets (a bug), which is reproduced.

## Board colour (state 5)
- Ten boards at fixed positions (tables `$4987` y, `$4991` x, bytes), the name centred above (mask `$4444`). Up/left = previous, down/right = next, on passes with `$3038 & 7 == 0`; the chosen board is marked with glyph `$0B` (mask `$FFFF`); the board index is stored at `$0768FE+player` and its colour word (table `$307A`) at `$0768EE+2·player`.
- Fire returns to the sign-up list (new player) or to the counter (existing player).

## High scores (state 9)
Twelve entries of 16 bytes at `$077F00` (name, BCD at +10..+12, colour byte at +15; the colour byte is overwritten with `$33` after the first display). Entries 0-2 and 6-11 are shown as scores (leading zeros dropped), entries 3-5 as minutes'seconds". The shipped `HIGHS` file is all zero, so the screen shows only the picture until a game writes scores.

## Colour cycling on the list screens (`$1061`, states 3, 6, 7)
Every 128 passes colour 7 toggles `$0FE0`/`$0FFF` and colour 4 steps `$0E0F` → `$0E00` → `$000F` → `$0E0F`.

## The sprite engine, as far as the shop needs it
The overlay draws into bank `$01` (the logical screen, `$2000`..) with shadowing inhibited and pushes only the touched rectangles to `$E1` (the displayed screen). Text routines write both. Consequences measured against the emulator:
- The cursor sign shown after pass *k* is drawn at the start of pass *k* from the slot values copied during pass *k−1* (after that pass's setup requests): a two-pass lag from the logic.
- Text drawn in a pass appears on top of the sign in that pass; the sign is painted over it again in the next pass.
- A sign that is hidden has y = 0 or x = `$168`.

## How it was checked
`tools/cpu816/menu_fuzz.py` runs the original overlay and the native build of `menu.h` side by side with identical inputs (control byte and keyboard value per pass) and compares after every pass: 39 variables, the player tables, the random-number state, the high-score table, the palette (outside screen changes) and the full 32,000-byte displayed picture including the cursor sign. Random and goal-driven (`directed`) input; pokes jump the sign to hotspots, raise exit codes and the `$70` flag, and move the pass counter near multiples of 128.
Final runs after the last change to `menu.h`: random input 20 trials / 3,161 passes and goal-driven input 10 trials / 15,000 passes, no difference (all of states 1-9 visited).
Mutation check: of 10 deliberate changes to `menu.h` 8 were detected; two are not observable by this test (`f38` is overwritten before it is read; a mistake in copying colour words while deleting an entry needs several players with different boards).

## Not done
Event entry (`$12EE`), what practice and compete mean inside the events, the speech for `$4BD3` after an event, high-score entry after events, music/sound for these screens, keypad control, timing of the fades against the real machine.
