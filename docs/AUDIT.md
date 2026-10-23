# Skate or Die (Apple IIGS) → Amiga (accelerated A1200 AGA, MiSTer Minimig): audit and status

Scope: asset extraction plus engine reimplementation. Target floor: accelerated A1200 (AGA, 68020 or better, 8 MB fast RAM), and it must also run on the MiSTer Minimig-AGA core. See §10 for what that implies.
Method: static analysis of the three supplied disk images, then (from the overworld work on) running the original code on a 65816 core written for this project as an oracle, and the Amiga build in FS-UAE with a real Kickstart 3.1.
"Verified" means decoded and visually checked. "Inferred" means deduced from code or data shape but not confirmed against the running game.

## 1. Verdict

**Feasible, and the asset side is already largely solved.** A 68030 with 8 MB is far above the IIGS (2.8 MHz 65816, ~1.1 MB in use) on CPU, RAM and graphics, so there are no hard hardware blockers. The two real risks are missing audio data (§5) and the volume of 65816 game logic that has to be reproduced faithfully (§6).

## 2. The disks

All three images are the same 800 KB ProDOS volume `SOD`:

| Image | Notes |
|---|---|
| `Skate or Die.2mg` | Clean layout, used as the reference for everything below. |
| `(Alt crack).2mg` | Same files, slightly different loader and tool setup. |
| `(Logo crack).po` | Same layout as the clean image. |

Boot chain (verified): `PRODOS` → `SYSTEM/P16` → `SOD.SYS16`. That is a small OMF v1 stub, apparently ORCA/C ("Can't allocate C stack"). It loads `DRIVER` (sound, resident at `$07:8000`) and `INTER` (main loop and resource loader, resident at `$07:6800`). `INTER` then loads numbered resources `/sod/a|b/fN` on demand through GS/OS reads.

## 3. Resource compression (verified, 45 of 45 files)

Recovered from `INTER` `$07:71F9`. It is a variable-width LZW:

- Header: little-endian u16 output length, then u16 code count. Then one discarded 9-bit marker (always `0x1C8`), then the codes. Length `0xFFFF` means 65535 or more.
- Bits are read LSB-first. The first real code is 8 bits wide. Width then steps 9→12 whenever the next free code equals the current threshold.
- There are no CLEAR or EOI codes. When the table reaches `0x1000` entries it resets implicitly.
- The decoder is in `tools/sodlzw.py`. Every resource decodes to exactly the length its header declares.

## 4. Asset inventory

| Resources | Content | Status |
|---|---|---|
| F1, F22, F23, F29–F33, F52 | Full-screen SHR images (32768 bytes). | **Verified** by rendering. |
| F4/F5/F6, F12/F13/F14, F18/F19/F20, F25/F26/F27, F36/F37/F38 | Sprite sets: frame table, 4bpp image, mask. | **Verified** by rendering. Palettes are provisional. |
| F7, F8, F15, F21, F28, F39 | 65816 code overlays. All load at **`$03:1000`** (confirmed from the loader table). | Identified, listings in `work/disasm/`. Logic not yet read. |
| F9, F16 | 32-byte palette plus 16 KB of graphics. | Layout inferred from the loader. |
| F10, F17, F2, F3, F11, F24, F35 | Smaller graphics, tables or masks. | Not decoded. |
| F40, F42, F44, F46, F48 | 64 KB blocks of unsigned 8-bit PCM (mean 128, small sample-to-sample steps). They share an identical first 2,048 bytes and are split into about 12 samples by 16-byte zero runs. These are almost certainly **IIGS Ensoniq sample banks**. | Identified by data shape. Not listened to. |
| F41, F43, F45, F47, F49, F51 | Low-entropy tables (60% zeros), 20–47 KB. Probably the IIGS song or instrument sequences that go with the sample banks. | Not decoded. |
| `SONGHDR.OBJ`, `BANK*TBL.OBJ` | Music and instrument tables. | Present but need the missing bank data. |

**Screens (verified).** All nine full-screen images use one 16-colour palette (palette 0), 320 mode, and no per-scanline palette or 640-mode tricks. The IIGS 12-bit `$0RGB` palette is identical to the Amiga's 12-bit colour format. These map 1:1 onto 4 bitplanes with no quantisation. This removes the main SHR-to-Amiga risk raised in the earlier audit.

**Sprites (verified format).**

- A table is `N` followed by 7 arrays of `N` u16 values.
- Array 0 is the width in 16-bit words (4 pixels per word at 4bpp).
- Array 1 is the total word count, so the row count is array 1 divided by array 0.
- Array 2 is probably the hotspot, high byte Y and low byte X. This is inferred.
- Arrays 3 and 5 are the byte offset into the image and mask files. Arrays 4 and 6 are the same offset shifted right by 8.
- The mask is a separate 4bpp file. Nibble `0` is opaque and `F` is transparent.
- The skater is drawn as a composite of three sprite parts per pose (head and torso, legs, board). This is an inference from the sheets.
- The sets hold 474, 314, 259, 502 and 67 frames.
- All five sets are small enough to hold fully expanded in the 8 MB.

### Game modes and what loads for each (verified from `INTER`'s tables)

`INTER` holds a resource table (57 entries: path, load type, destination) and a list of resources per game mode. The full table is in `docs/resource_table.txt`. Result:

| Mode | Resources loaded | Reading |
|---|---|---|
| 1 | F1 (ramp background), F2, F3, F4/F5/F6 (sprites), **F7 code** | Ramp event, variant A. Probably Freestyle. |
| 2 | Same as mode 1 but **F8 code** | Ramp event, variant B. Probably High Jump. Both share the background and sprites. |
| 3 | F9, F2, F10, F11, F12/F13/F14, **F15 code** | Downhill event, variant A. |
| 4 | F16, F2, F17, F18, F19/F20, **F21 code** | Downhill event, variant B. |
| 5 | F22 (pool), F23 (faces), F2, F24, F25/F26/F27, **F28 code** | Pool Joust. |
| 0 and 6 | F29–F33 (shop map, shopkeeper, sign-up, high scores, board select), F2, highs, F35, F36/F37/F38, **F39 code** | Menus, shop, high scores. F39 is the only overlay that makes file calls, so it likely saves the high scores. |

Event names are inferred from the images and the high-score screen. Which downhill variant is the Jam and which is the Race is not confirmed.

Other facts from the table: all code overlays load at `$03:1000`; sprite images load to banks 4 and 5; backgrounds load into staging in bank 2; the sound resources (F40–F51) all target `$06:0000`.

### Overworld graphics (decoded)

| Resource | What it is | Status |
|---|---|---|
| F29 | Overworld map: Rodney's Skate Shop, signs to each event | Verified by rendering |
| F30, F31, F32, F33 | Shopkeeper, sign-up, high scores, board select | Verified by rendering |
| F52 | Title | Verified by rendering |
| F2 | Shared 16 KB memory snapshot. Offset `$400`: **8×8 font**, 59 glyphs for ASCII `$20`–`$5A` (space, punctuation, digits, upper case), 32 bytes each in 4bpp. The rest is leftover data that other tables overwrite. | Font verified by rendering and round-trip. The other blobs are unidentified. |
| F35 | Water animation for the map: 4 frames of 32×26, 4bpp, stacked | Verified by round-trip. Where it sits on the map is not yet known. |
| F36/F37/F38 | **The overworld skater**: 16 headings × 4 parts (torso 0–15, legs 16–31, board 32–47, shadow 48–63), then three "SKATE OR DIE" signs (64–66: white, yellow, red). The hot-spot word is (y offset high byte, x offset low byte) in pixels relative to the skater's origin: legs +12/+2, board +18/+4, shadow +12/+8 or +10, torso +0 and an x of 0, 4 or 6 depending on frame. | Composite verified by rendering at all 16 headings |
| F3, F24 | Per-pose (x, y) byte pairs for the ramp (158 poses = 474 frames ÷ 3) and joust (125 poses). Not overworld graphics. | Identified |

Heading order in F37, from the board and body orientation: frame 0 faces south, 4 west, 8 north, 12 east (clockwise seen from above). This is an inference from the sprite art, not from the game code.

### Overworld screens and the matching NES music

Matching is from where the NES code starts each track, not from listening. The NES game has the same overworld text ("MUST REGISTER AT SKATESHOP", "SKATING TO DOWNHILL / JAM / RAMP / HIGH JUMP / POOL JOUST", "SCORES FOR ...", "NEXT SKATER").

| IIGS screen | NES sound id | Evidence | Confidence |
|---|---|---|---|
| Title (F52) | `$31` | Started by NES mode 0 (title) | Medium |
| Overworld map and shop (F29, F30, F31, F33) | `$35` (loops exactly every 1,296 frames, 21.6 s) | Started by NES modes 2 and 3, which draw "MUST REGISTER AT SKATESHOP" | Medium |
| High scores and results (F32) | `$39` (loop 8.5 s) | Started by NES mode 6, which draws "SCORES FOR", "NEXT SKATER" and "OVERALL" | Medium to low |
| Events | `$3D`, `$41`, `$45`, `$49`, `$4D` | One table entry per event, indexed by event number 1 to 5 | Medium |

NES event order (inferred from its text and shared setup code) is Downhill, Jam, Ramp, High Jump, Pool Joust. The IIGS order differs: its modes 1 and 2 are the ramp pair, 3 and 4 the downhill pair, and 5 is the pool. So the matching is Ramp → `$45`, High Jump → `$49`, Pool Joust → `$4D`, and the two downhill modes → `$3D` and `$41`. Which IIGS downhill mode is the Jam and which is the Race is **not** confirmed.

The NES sound ids are a different numbering from the IIGS driver calls (the IIGS overlays pass small per-event indexes such as 4, 5, 2, 3, 1 to the driver). Effects therefore cannot be matched by number and need the same kind of context matching.

### Overworld demo (built, not yet run on an Amiga)

`overworld.hdf` (4 MiB, RDB with one FFS partition `SOD:`, flagged **bootable**) holds `OverworldDemo` (a 530 KB AmigaDOS executable), `S:Startup-Sequence` (one line, `SYS:OverworldDemo`), empty `C`, `L`, `Libs` and `Devs` directories, and a README. It shows the map, lets you skate around it under the original game's movement rules (up accelerates, down brakes, left and right turn) with joystick or cursor keys, and loops the `$35` NES track on Paula. Source is in `demo/src`, build with `demo/build.sh`.

Tested in FS-UAE (A1200, 68030, 2 MB chip, 8 MB fast) with Kickstart 3.1 r40.068, booting this exact HDF:
- The bootable setup works with no Workbench files: the ROM's own shell runs `S:Startup-Sequence`, and without the demo the disk boots to the AmigaDOS `1>` prompt.
- The demo shows the map, steers with the cursor keys (right, down, left, up all exercised), and Esc or Q returns to the AmigaDOS prompt with the display restored.
- Audio was recorded from the emulator and compared with the source loop. Playback position advanced exactly 5.00 s per 5 s window across about three loops, wrapped cleanly at the 21.56 s loop point, with steady level and no dropouts, so the 4-block DMA chaining works. Left and right are identical (mono).
- Earlier, before any Kickstart was available, the binary was also run in a 68020 emulator against my own model of the hardware (`tools/sim_amiga.py`); its screen matched an independent model pixel-for-pixel.

**A bug the first real run found:** my first version crashed on exit with "Program failed (error #80000006)", later an illegal instruction. The CPU-level simulation could not see it. The cause was the inline-assembly wrappers for the AmigaOS library calls: with register-bound variables plus clobber lists the compiler lost a live value across the calls. The wrappers now move their operands into the OS registers explicitly, the file is compiled with `-ffixed-a6` so the compiler never uses the library-base register, and a small assembly stub at the start of the hunk saves and restores the registers. I found it by building diagnostic variants (`demo/build_test.sh`, levels 1 to 10) that stop at progressively later points, and booting each in FS-UAE.

Not tested: real hardware, MiSTer, a real joystick (only keyboard input was exercised), NTSC timing, OCS/ECS chipsets. The music was compared with the source loop but not listened to. The overworld cursor, shop and event hand-offs are not in the demo (see `docs/OVERWORLD_F39.md`).

### Skater movement (decoded from F39 and verified)

The original overworld movement is now reproduced exactly; the full write-up is in `docs/OVERWORLD_F39.md`. In short: the skater has a turn rate (−1, 0, +1), a heading of 64 steps (shown with 16 poses), and a speed level 0 to 5 that only changes when you accelerate or brake (there is no friction). Velocity comes from a diamond-shaped table, movement runs one sub-step per speed level per pass, and the map's walls and exits are tested at specific edges. The first attempt at the demo (free 8-way roaming) did not match this; the model was recovered by reading F39 and then running the **original code** on a 65816 core written for the purpose (`tools/cpu816/`).

Verification: a Python transcription (`tools/overworld_model.py`) was compared with the original pass for pass on random driving (4,161 passes) and from random positions, headings and speeds (16,384 passes, 90 runs) with 0 mismatches, reaching all five exits and the shop door; a deliberately wrong constant makes the comparison fail. The C port in the Amiga demo (`demo/src/skater.h`) was then checked against that model through the CPU-level simulation, and run in FS-UAE under Kickstart 3.1.

The least certain number is the speed of the original loop (no frame throttle exists in the overlay; the emulator counts about 107,000 cycles a pass, roughly 26 passes a second at 2.8 MHz, less with real memory timing). The demo uses 25 a second.

Also found while doing this: the overworld has a second mode before skating, the shopkeeper screen (F30) where a flashing "SKATE OR DIE" sign is a cursor (2 px a pass, hotspot table at `$4628`), and the skater only appears on the map (F29) after a selection.

### Rodney's shop, sign-up, name entry, board colour, high scores (decoded, ported, checked against the original code)

Details in `docs/MENU_F39.md`. The logic of overlay F39 for game states 1-9 is reimplemented in `demo/src/menu.h` (portable C, no hardware access) and the demo now opens in the shop like the original, with the sign-up list, name entry (original keyboard rules, 15 characters, quirks included), board colour selection, player list, high-score screen and the "MUST REGISTER AT SKATESHOP" notice.

Check: `tools/cpu816/menu_fuzz.py` runs the original overlay on the 65816 core and the native build of `menu.h` with the same input every pass and compares 39 variables, the player tables, the random-number state, the high-score table, the palette and the whole displayed 320x200 picture (cursor sign and text included). Last runs: random input 20 trials / 3,161 passes and goal-driven input 10 trials / 15,000 passes without a difference; deliberate faults in `menu.h` were caught in 8 of 10 cases (the other two are not observable by this test). The Amiga build was then run in FS-UAE (A1200, real Kickstart 3.1) and driven through shop, sign-up, name entry ("MIKE"), board colour, second entry and the notice with screenshots; those match the emulator pictures by eye, the pixel comparison is the native one above.

Open: event entry (`$12EE`), practice versus compete inside the events, music for these screens, fade timing against a real IIGS, nothing tested on real hardware or MiSTer.

## 5. Audio: NES version is the source

**Correction to my earlier finding.** I said the sound data was missing from the disk. It looks like it is not: F40–F51 are most likely the IIGS sample banks and sequences (see §4). Only the files named `bank1`–`bank6` are absent, and those names are probably the driver's own banks on the original two-disk set. The NES plan below does not depend on either, but the IIGS audio may be recoverable from the supplied disk if you ever want a faithful IIGS option.

`DRIVER` opens `/sod/bank1`, `/sod2/bank2`–`bank4`, `/sod/bank5`, `/sod/bank6` and matching `.not` files. **None of these exist in any supplied image.** A second upload (`125ceabd-Skate_or_Die.2mg`) turned out to be byte-identical to the clean image. The disks hold only the tables (`SONGHDR.OBJ`, `BANK1TBL.OBJ`–`BANK6TBL.OBJ`).

**Decision (from you): music will be patched in from the NES version afterwards.** So the missing IIGS samples are no longer a blocker, provided the engine has a clean audio interface. That is the plan:

- The game logic calls a small sound API. The Amiga port keeps that call shape (`snd_event_init(n)`, `snd_play(id)`, `snd_stop(...)`) and implements it on the Amiga side, so the NES material can be swapped in without touching game logic.
- What I recovered from the IIGS side without the sample data (static analysis, **inferred**, not yet confirmed by running the game):
  - `DRIVER` has a 20-entry `JMP` table at `$07:8000`.
  - `$8018`–`$8027` look like one init routine per event. `INTER` calls them by game mode: mode 1 → `$8024` (F7), 2 → `$8027` (F8), 3 → `$801E` (F15), 4 → `$8021` (F21), 5 → `$801B` (F28).
  - `$8003` is called from 12 sites in the overlays with small immediate arguments (for example `$0A`–`$0E` in F15). It is probably "play sound or effect by id".
  - `$8006` and `$8009` are called with the event number (1 to 5). They are probably start and stop of that event's music.
  - `$8033` is called once per scene change and `$8036` once per overlay entry. The meaning of both is unknown.
  - `SONGHDR.OBJ` groups about 88 sound ids (`0x00`–`0x57`) into 12 groups, one per sound bank.
- **Sound effects: decided.** Both music and effects come from the NES version, so the IIGS sound banks are not needed at all.

### NES sound engine (ROM received and analysed)

ROM: `Skate or Die (USA).nes`, 128 KB, mapper 2 (UxROM), 8 KB CHR-RAM. The sound engine is in **bank 0** (`$8000`). The harness runs it with only bank 0 and the fixed bank mapped, and all 82 ids play, so the song and effect data is in those two banks. DPCM samples are read from the fixed bank by hardware.

**Engine interface** (found by static analysis of the fixed bank, then confirmed by running it):

| Entry | Meaning |
|---|---|
| `$8008` | reset and stop everything |
| `$8040` | start sound id in A, valid ids `$00`–`$57` (the engine rejects `$58` and above) |
| `$8245` | per-frame update, called once per NMI |

**Sound ids (observed by running the engine, not yet listened to):**

| Ids | Behaviour |
|---|---|
| `$00`–`$30`, `$51` | Short effects, mostly under 2 seconds. `$00`–`$02` use only the DMC channel. |
| `$2E` | Long looping effect, pulse 1 only. |
| `$31`–`$50` | Eight songs, each in four consecutive ids: the full mix, then pulse 2 only, triangle only, and noise plus DMC only. The single-channel versions are probably how the game restores music channels after an effect. |
| `$52` | Stop all. |
| `$53`–`$57` | Small channel commands. Purpose unconfirmed. |

**Evidence it runs correctly:** all 82 ids produce non-silent audio, and the triangle bass line of song `$31` is 100% on musical pitch (E2, C3, G2, D3…). Pulse 2 is 88% and pulse 1 is 77%, which is expected from arpeggio, vibrato and sweep effects. Nobody has listened to the renders yet.

**Cost of running the real engine:** mean 144 emulated 6502 instructions per frame, worst case 1,685. That is roughly 9,000 to 100,000 instructions per second, which is small.

**Tools:** `tools/nes_sound.py` (6502 harness), `tools/nes_apu.py` (approximate APU renderer, Python and numpy), `tools/nes_export.py` (writes per-id register logs and preview WAVs to `work/nes_audio/`, 82 ids, 60 MB).

**Recommended Amiga implementation:** run the original 6502 sound engine on the Amiga with a minimal 6502 interpreter and a software APU mixer that outputs through Paula. The reasons:

- Behaviour is exact, including the interplay between effects and music.
- Data is small: 16 KB of engine and song data, plus the DMC sample area.
- The game logic only needs to send a sound id.

The alternative is playing back pre-recorded register logs or rendered samples. That is simpler to start but loses the dynamic channel interplay and uses far more memory. This is a decision for you before the audio work starts.

**Still unknown:** which NES id corresponds to which IIGS sound call (§6). The IIGS driver numbers its sounds `$00`–`$57` too, but the two id sets are not confirmed to match. The mapping needs the IIGS overlay call sites plus listening to the previews.

- NES music is Konami/Ultra's work. Keep any extraction on the user's side from their own ROM, the same as the IIGS assets.

## 6. Engine and logic

- The overlays are 15–21 KB each, about 100 KB in total, plus about 13 KB resident. The flow-following disassembly in `tools/dis816.py` reaches 2.0K–5.5K instructions per overlay from the entry point alone. That is a lower bound because jump tables are not followed. A rough total is **under 40K instructions**, which is a tractable size.
- The overlays **modify their own code**. F7 patches its first-run initialisation with `NOP`s (`$EAEA`) once it has run. This rules out a simple static recompile and favours reimplementing the logic by hand.
- Overlays run in 16-bit native mode, with data banks 3 to 5 and the resident code in bank 7.
- Hardware dependence is small. Each overlay has about 16–32 direct `$E0C0xx` I/O pokes (video and sound setup) and about 26–40 direct references to the `$E1xxxx` screen. All sound goes through a clean entry-point table at `$07:8003+` (`JSL`).
- `INTER` runs a scene dispatcher. A game-mode index selects the scene, the resources to load and the overlay to run.

## 7. Recommended architecture (68020+ with 8 MB fast RAM; see §10)

1. **Asset converter** (host-side, Python or C): the existing pipeline plus planar output (interleaved bitmaps, planar masks) and a flat resource pack. The original disks are the input, so no copyrighted data ships with the port.
2. **Renderer:** interleaved 4-plane 320×200 (or 256 lines on PAL), double-buffered. Blit the sprites. A 030 can also do chunky-to-planar in software if that proves simpler.
3. **Timing:** run logic at a fixed 60 Hz tick decoupled from the display, so PAL (50 Hz) does not slow the game by 17%.
4. **Logic:** reimplement in C, using the disassembly as the specification.
5. **Oracle (recommended):** write a small 65816 core that runs the original overlays against a virtual memory map with the screen and `JSL` entry points intercepted. Use it for differential testing of the C reimplementation, event by event. It is also a possible fallback runtime, but speed on a 030 is unproven and should not be relied on.

## 8. Effort (rough, one experienced developer)

| Item | Estimate |
|---|---|
| Asset converter and resource pack | 1–2 weeks (mostly done) |
| Renderer and sprite engine | 2–3 weeks |
| Per-event logic, 5 events plus shop and menu | 3–5 weeks each |
| Audio (depends on §5) | 2–4 weeks |
| **Total** | **roughly 5–8 months** |

## 9. Unknowns and next steps

1. Audio: the NES ROM is analysed (§5). Open: IIGS-to-NES id mapping (needs listening), and the decision on running the 6502 engine versus playing back logs.
2. Decode F2, F3, F10, F11, F17, F24, F35 (tilemaps, scroll data, fonts) and the F41–F51 sequence tables.
3. Event-to-file mapping is now known (§4). Still to confirm by tracing in an emulator: the hotspot field, which palette each sprite set uses, and the downhill Jam/Race labels.
4. Disassemble `INTER` fully and one overlay end to end, to size the per-event work properly.
5. Chipset floor is decided: accelerated A1200 AGA, MiSTer-compatible (§10). Still open: whether to use any AGA enhancements beyond a faithful port.
6. Keep original assets local. Ship only the engine and have the user supply the disk images.

## Tools (`tools/`)

- `extract_all.py <image> <outdir>` runs the whole pipeline. It writes raw files, decompressed resources, PNG screens and sprite sheets.
- `prodos.py` reads ProDOS volumes and `.2mg` or `.po` images.
- `sodlzw.py` is the resource decompressor.
- `sprites.py` handles sprite tables and masks.
- `dis816.py` is a flow-following 65816 disassembler with M/X tracking.
- `docs/resource_table.txt` is the loader's resource table and per-mode lists.
- `nes_sound.py`, `nes_apu.py`, `nes_export.py`, `nes_songinfo.py` run, render and measure the NES sound engine (§5).
- `cpu816/` is a small 65816 core (C, driven from Python) that runs the original IIGS overlays headless; `overworld_sim.py`, `fuzz_model.py`, `fuzz_model2.py` use it. `overworld_model.py` is the verified skater model. `make_demo_assets.py`, `elf2hunk.py`, `sim_amiga.py` build and test the overworld demo. `uae_boot_test.sh`, `uae_run_demo.sh` and `uae_audio_test.sh` boot it in FS-UAE under a virtual display (needs your own Kickstart ROM) and capture screenshots and audio.
- `cpu816/flow.py`, `cpu816/menu_test.c`, `cpu816/menu_fuzz.py` (+ `dbg*.py`) drive the shop code and compare it with `demo/src/menu.h` (see `docs/MENU_F39.md`); `make_menu_data.py` generates the shop tables and glyph font from the overlay; `uae_run_menu.sh` and `uae_run_menu2.sh` drive the shop in FS-UAE.
- `amiga_pack.py <resdir> <packdir> --verify` converts screens and sprite sets to big-endian planar Amiga data and checks that they round-trip to the original pixels. See §10.

## 10. Target platform: accelerated A1200 AGA and MiSTer Minimig

**Effect on the plan.** The floor is lower than the earlier "030" assumption. Treat the CPU as a **68020 with AGA**, and use 030 features only behind a runtime check. MiSTer's Minimig-AGA core does not behave exactly like a real 68030 machine. As I understand it, its CPU is a 68020-class soft core, but I haven't verified this against a specific core release, so confirm it on the build you will test.

**Compatibility rules for the engine:**

1. Compile for `-m68020`. No MMU or PFLUSH instructions, no 030-specific cache-control bits. Read CPU type from `exec` (`AttnFlags`) and enable any 030 path only if detected.
2. Do not rely on cycle-exact CPU timing or on busy-wait loops of fixed length. Sync to the vertical blank interrupt or `VPOSR`, and use the blitter's busy flag. Poll `BLTBUSY` properly, including the dummy read of `DMACONR`.
3. Flush caches after writing code or tables that the CPU will execute (`CacheClearU`). Avoid self-modifying code entirely.
4. Be OS-friendly at start-up. Boot under Kickstart, allocate memory with `AllocMem`, and take over the display only after `Forbid` and `LoadView(NULL)`, restoring everything on exit. Both MiSTer and real hardware run a full Kickstart 3.x.
5. Use only documented AGA behaviour. Set colours explicitly with the high and low nibble writes (`LOCT` in `BPLCON3`) or with `LoadRGB32`. Do not assume how OCS-style colour writes fill the low nibbles.
6. Keep bitmaps and anything the blitter or DMA reads in **chip RAM** (2 MB on AGA). Keep everything else in fast RAM. Align bitmaps to 32 bits so wider AGA fetch modes work.
7. Joystick and mouse: read standard hardware (`JOY1DAT`, `POTGO`) or `lowlevel.library`. MiSTer maps its controllers onto the Amiga ports. The original game is joystick-driven.
8. Audio: Paula direct with a software mixer. Do not require AHI.

**Memory budget (measured from the pack):**

| Item | Size |
|---|---|
| 9 screens as interleaved planar | 9 × 32,106 B = 289 KB |
| 5 sprite sets (4 planes + 1bpp mask, 1,616 frames) | 418 KB |
| Two 32 KB display buffers | 64 KB |

All of it fits comfortably in 2 MB chip RAM. The unpacked pack is about 706 KB (`work/amiga/`) before code and audio, so a single 880 KB ADF needs compression. A folder or HDF install is simpler.

**Visual fidelity.** The IIGS palette is 12-bit and one palette per scene. The converter stores both the 12-bit value and the AGA 24-bit value (each nibble × 17). The result is identical colours on OCS and AGA. AGA's extra depth is optional and unused for a faithful port. The IIGS 320×200 image also fits the Amiga lowres 320×200 (NTSC) or 320×256 (PAL) display.

**Converter status (verified):** `tools/amiga_pack.py` writes 4-plane interleaved screens and sprite sets, big-endian. All 9 screens and all 5 sprite sets (1,616 frames, including masks and hotspot field) round-trip back to the original pixels. This proves the data conversion, not the Amiga code that will consume it.

**Test matrix:**

1. WinUAE or FS-UAE configured as A1200, 68020, 2 MB chip and 8 MB fast, cycle-exact. This is the development loop.
2. The same on the strictest settings, with 68000-style timing off and the blitter in non-immediate mode, to expose timing assumptions.
3. MiSTer Minimig-AGA, with the CPU speed setting varied. This is the final compatibility gate and I cannot run it from this environment.

**What is not done.** There is no Amiga-side code yet. This environment has no Kickstart ROM and no Amiga NDK, so nothing here has been run on an Amiga or an emulator. The converter is host-side Python, verified only against the source pixels. `fs-uae` and an m68k cross-gcc are installable, but I'd need a Kickstart or AROS ROM and NDK headers from you or a download source you approve before I can build and run a first viewer.

## 11. Events: first look
`tools/cpu816/event_sim.py <mode>` loads the resources of game mode 1-5 the way the loader table says and runs the event overlay on the 65816 core. With no extra stubs all five overlays start and reach their first interactive screen: Freestyle Ramp (F7, "PASSES: 0 SCORE: 0"), High Jump (F8, "PASS: 0 HEIGHT: 0' 0\""), the two downhill modes (F15, F21: "CHOOSE CONTROL: REGULAR FOOT / PRESS YOUR BUTTON", art not yet drawn at that point) and Pool Joust (F28, "-- PRESS YOUR BUTTON --"). Flow-following disassembly sizes: F7 2,595 instructions, F8 2,048, F15 4,452, F21 5,490, F28 2,925 (indirect jumps not followed, so a lower bound). Nothing of the event logic is ported yet.

## 12. Translator
Events are now being done by static translation of the 65816 overlays to C with lockstep verification against an emulator; see docs/TRANSLATOR.md. Freestyle Ramp (F7) is translated and runs in the demo.
