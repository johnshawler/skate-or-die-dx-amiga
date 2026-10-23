---
name: rom-first
description: Headless source-first ROM-to-Amiga-HDF transcode workflow with mandatory stress testing of the original game in its source emulator before any Amiga implementation. Use only when the user explicitly asks to run it for a named game.
---

# Headless Amiga Transcode: Source-First Stress Testing

## Purpose and scope

I want a headless workflow from the specified source game ROM to a finished, playable Amiga HDF. I should not have to watch intermediate screens, operate the emulators, or review each implementation step. You must still inspect captured screens, test gameplay, and validate the result yourself.

**The source ROM must be stress-tested in its original system emulator before any Amiga transcode or renderer implementation begins.** Find representative demanding gameplay first, preserve repeatable evidence, and use it to guide the Amiga design. Do not build around an easy opening scene and discover the real workload afterward.

This is a reusable specification for a new task. Creating or attaching this brief does not itself authorize starting a transcode or resuming any existing project. Begin only when I explicitly ask you to use it for the identified game. Existing paused projects must remain untouched unless I specifically authorize changes to them.

## 1. Establish the exact job

Start by briefly restating the requirements, declared target, test gates, and what would count as completion. Resolve values from the supplied files and explicit instructions first. Ask only for genuinely missing decisions or access that block the work. Do not silently choose a more powerful target or lower fidelity to make the job easier.

Complete this specification before implementation:

- **Game and source system:** [game title; original platform and hardware variant]
- **Source ROM identity:** [exact filenames, revision/version/region, ROM-set dependencies, and SHA-256 hashes]
- **Source emulator:** [name, version/build, machine configuration, BIOS dependencies if applicable, timing settings, and headless automation interface]
- **Amiga CPU:** [model, clock, accelerator status, and any relevant cache settings]
- **Amiga chipset:** [OCS/ECS/AGA and exact machine profile]
- **Memory limits:** [Chip RAM; Fast RAM; other memory if permitted]
- **Video standard:** [PAL or NTSC, display mode, resolution, and refresh rate]
- **Update contract:** [game simulation rate; rendered/presented update rate; allowed deadline misses or slowdown; required headroom]
- **Fidelity contract:** [required graphics, palette, HUD, gameplay, controls, audio, and any explicitly permitted differences]
- **Delivery/runtime:** [HDF format and boot arrangement; Kickstart/Workbench requirements; intended hardware or emulator and exact version/configuration]
- **Test extent:** [source exploration budget; sustained heavy-play duration; required levels/routes and completion or final reachable milestone]
- **Inputs and assets:** [supplied ROMs/assets/source, permitted tools, and authorized workspace/output location]

Record what is verified, what is an assumption, and what remains unknown. Keep the agreed configuration fixed throughout validation. Required ROMs, firmware, and other dependencies must be supplied or available through authorized means.

## 2. Mandatory source-first gate

### Run the original game before implementing the Amiga version

Boot the identified ROM in an emulator for its source system and automate repeatable gameplay without requiring me to watch it. Headless execution may use offscreen rendering, frame capture, input playback, instrumentation, and internal visual review. “Headless” must not mean skipping visual validation.

The motivating failure is the Bump 'n' Jump example: an empty road appeared to work, but performance crawled when traffic arrived. An empty scene is not evidence that the game is viable under load.

Use bounded, purposeful exploration to find a representative stress corpus, including applicable combinations of:

- High simultaneous traffic, enemy, sprite, and other object counts, including overlap and occlusion.
- Fast or complex scrolling, newly exposed scenery, background changes, and crowded screen regions.
- Multiple collisions, projectiles, explosions, particles, animation changes, and other effects.
- Overlapping music and sound effects during busy gameplay.
- Death, restart, retry, respawn, loading, and level or scene transitions.
- Later levels, new enemy types, difficult routes, and other scenes unlike the opening sequence.

Use scripted inputs and repeatable routes. Where supported, use deterministic seeds and save states to revisit difficult sections. Record any debug shortcuts or modified entry states separately from ordinary play, and verify that they represent reachable gameplay. Do not claim determinism where the emulator or game does not provide it.

### Preserve evidence that can be replayed

For each case, save a unique case ID, entry conditions, exact emulator configuration, input timeline, seed/state where supported, and run length in emulated time. Include screenshots or frame traces, an event timeline, relevant object/workload counts, and observed behavior. Store a cold-boot route where practical; save states are acceleration aids, not substitutes for boot testing.

Keep source emulated time and host wall-clock time separate. A slow headless run may still accurately model source timing. Host throughput alone does not establish source slowdown or prove that the eventual Amiga version will run in real time.

Measure source CPU cycles and timing where available, and identify slowdown already present in the original game. Rank cases by workload features as well as source CPU usage: scrolling, changed pixels, sprite overlap, palette changes, memory traffic, and audio work may have very different costs on Amiga hardware.

State the search budget, explored routes, supported instrumentation, inaccessible scenes, and coverage gaps. This is a representative corpus, not proof that every possible worst case has been found.

### Required outputs before the gate opens

Produce and preserve all three:

1. **Hotspot report:** ranked demanding cases, observed source behavior, timing evidence, why each case matters for the Amiga target, and remaining uncertainties.
2. **Replayable evidence corpus:** source identities, configurations, scripts/input recordings, seeds/states where supported, images/traces, event timelines, and reproduction instructions.
3. **Provisional Amiga budget and design implications:** per-subsystem frame-time/cycle and memory allocations for translated game logic, graphics/scrolling, sprites/blitting, audio, input/interrupts, and transition/loading work. Include Chip/Fast RAM placement, buffering, DMA/bus contention where relevant, safety margin, and likely bottlenecks. Clearly label estimates rather than measurements.

This is an automatic internal gate, not a request for me to inspect screenshots or approve each stage. Proceed only when these outputs exist and support a credible implementation plan on the declared target. If source execution, coverage, or feasibility is blocked, report the specific blocker instead of pretending the gate passed. Ask me only for a necessary decision, missing input, access, or change of scope.

## 3. Implement against the demanding cases

Only after the source-first gate passes, translate or reimplement the source CPU behavior, game logic, and hardware dependencies as needed for the agreed Amiga target. Do not promise that an arbitrary ROM can be mechanically converted into a working HDF.

Use the hotspot findings to choose architecture and rendering strategies. Replay the stress corpus continuously as functionality is added. Preserve versioned checkpoints and known-good builds so regressions can be isolated without discarding work.

For matched checkpoints and event sequences, compare:

- Visible actors and their positions, counts, animation, layering, and disappearance/reappearance.
- Colors, backgrounds, scrolling, HUD, score, lives, and other visible state.
- Controls, collisions, game-state transitions, death/retry, and level progression.
- Music, sound effects, overlap, timing, and interruptions within the agreed fidelity limits.

Use independent captured-image checks as well as internal counters and state assertions. A shared parser, scene model, or test oracle can omit the same sprite in both implementation and test and produce a false pass. Compare against the original emulator's output, inspect representative dense frames independently, and investigate mismatches rather than only checking that the build did not crash.

For cross-platform differences, align by gameplay events or emulated time and document the comparison tolerance. Do not hide an unexplained difference by weakening the test.

## 4. Measure the full game on the declared target

Measure complete game updates and presentation deadlines, not just an isolated rendering kernel or an empty-road benchmark. Include logic, graphics, audio, interrupts, memory movement, and relevant chipset contention in the measured workload.

Report at least:

- Target configuration, emulator timing model, build identity, corpus case, and sample duration.
- Simulation updates and displayed frames delivered versus required.
- Display-deadline misses, repeated/dropped frames, and any slow-motion behavior.
- Full-update timing distribution, including typical, tail percentiles such as p95/p99, worst observed frame, and sample count.
- Peak Chip RAM, Fast RAM, stack/heap or equivalent memory use, and available headroom.
- Separate results for cold boot/loading, death/retry, transitions, and steady gameplay.

Distinguish original/native slowdown from additional slowdown introduced by the conversion. If preservation of native slowdown is part of the fidelity contract, reproduce it deliberately and label it.

A headless Amiga emulator running slower than host real time can still supply useful emulated-cycle evidence if its timing model supports that conclusion. It does not prove wall-clock playability. State the limits of the model and validate actual playback against the declared runtime and update contract; do not imply physical-hardware testing if none occurred.

Do not silently increase CPU speed or RAM, enable an unagreed accelerator/JIT profile, remove actors/effects/audio, reduce colors, skip game logic, or lower the update rate to obtain a pass. Any necessary target or fidelity change requires my explicit agreement.

## 5. Finished HDF acceptance gate

Call the work finished only when the final HDF passes the agreed tests on the fixed target configuration:

- Cold-boots from a clean launch into the playable game, using only declared runtime dependencies.
- Runs independently of development-host paths, network services, test scripts, and instrumentation needed only during development.
- Sustains the agreed heavy-play duration across the stress corpus within the frame and memory budgets.
- Handles controls, audio, collisions, death, retry, restart, and level transitions correctly.
- Demonstrates game completion or the explicitly agreed final milestone; disclose any untested later content.
- Remains playable after the automated test ends. No test-only auto-exit, hidden timeout, or forced return that masquerades as completion.
- Has no unresolved failure that violates the agreed target, gameplay, fidelity, or performance contract.

The agent performs intermediate screen inspection and validation internally. I do not need to see or supervise those screens. Still preserve the evidence so results are auditable and reproducible.

## 6. Final handover and honest failure handling

Deliver:

1. The finished HDF and the source/build materials needed to reproduce it, with an artifact manifest and checksums.
2. Exact build, boot, source-replay, and Amiga-validation commands or scripts, plus tool versions and configuration files. Document any unavoidable manual prerequisite.
3. A short acceptance report mapping each requirement to its result, with performance/memory summaries, fidelity findings, and links or paths to the relevant evidence cases.
4. The stress corpus and hotspot report, known issues, limitations, coverage gaps, and any differences I explicitly accepted.

If a gate fails, keep the last good checkpoint and report what failed, the evidence, what is already working, and the smallest decision or next step needed. Mark an incomplete build as incomplete. Do not present a demo, a fast opening scene, a configuration change, or a partially tested HDF as a finished conversion.

The goal is a reproducible, evidence-backed result within the agreed scope, not a guarantee over every possible game state.
