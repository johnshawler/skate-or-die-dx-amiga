# Skate or Die (Apple IIGS) -> Amiga ECS, source-first (workspace for the /rom-first run)

Target: Amiga ECS, 68000 @ 7.09 MHz PAL, 1 MB Chip RAM, no Fast RAM. Separate from the earlier AGA/020 work in the repository root.

- `tools/kegs-harness/` build of KEGS 1.38 (pinned commit) with a headless harness: scripted joystick/keys, per-frame log, SHR dumps, memory watch.
- `tools/run_hx.sh`, `make_script.py`, `route_plan.py`, `shr2png.py`: cold-boot runs, routes to the five events, frame converter.
- `evidence/`, `docs/`: corpus and reports (written as the gate progresses).

Source: `Skate_or_Die.2mg` (IIGS, EA 1988) on ROM 01 (`rom01`, crc 42f124b0 = MAME 342-0077-b). Not in this repository: the disk image and the ROM files.
