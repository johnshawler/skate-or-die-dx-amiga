# Skate or Die (Apple IIGS) -> Amiga

Port work: static 65816->C translator (`tools/t816`), 65816 oracle (`tools/cpu816`), Amiga demo (`demo/`).
See `demo/README.txt` (what runs, controls, specs) and `docs/TRANSLATOR.md`, `docs/AUDIT.md`.

## Layout
- `tools/` extraction, asset and audio generators, translator, oracle, FS-UAE helper scripts (`uae_run_event.sh`, `uae_record.sh`; `CPU=68020` picks the CPU).
- `demo/` Amiga source (`src/`), `build.sh` (needs `m68k-linux-gnu-gcc`), assets.
- `work/` extracted game data used by the tools.
- `roms/` Kickstart 3.1 (A1200, r40.068) used by the FS-UAE test scripts. Private repo only: the ROM is copyrighted.
- `roms/Kickstart 1.3 ... (Guardian 1.2 patched).rom` 256 KB, SHA-256 e8a1cd19...54bb2: Kickstart 1.3 (exec 34.2) with the third-party Guardian 1.2 utility built in, so not a pristine ROM. OCS/68000 only (A500-class): it cannot run the AGA demo, only useful for OCS-era emulation tests.
- `sod_demo.fs-uae` FS-UAE config for the HDF.

## Build and run
    cd demo && ./build.sh                 # -> demo/build/OverworldDemo (Amiga hunk executable)
    # FPS overlay / unpaced speed test:  EXTRA="-DFPS_ON -DEV_UNPACED" ./build.sh
    cd tools && CPU=68020 EVKEY=F3 BOOT_WAIT=30 ./uae_run_event.sh ../demo/build/OverworldDemo OUT_DIR ../roms/"Kickstart v3.1 r40.068 (1993-12)(Commodore)(A1200)[!].rom"
Needs fs-uae, Xvfb, xdotool, ImageMagick, amitools (rdbtool/xdftool) for the emulator tests.
Make the HDF: `tools/mkhdf.sh` (after build.sh).
Lockstep checks: `tools/t816/lockstep_batch.sh`, `nativetest.py`, `cov_union.py` (see docs/TRANSLATOR.md).
