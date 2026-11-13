# Implementation plan

## Architecture

```text
User-provided WBFS/ISO
  -> DolphinTool (extraction during development)
  -> Version and DOL hash validation
  -> Program and assets under private/
  -> key symbol assistance + PPC AOT translator
  -> C++ and data initializers under private/
  -> Clang + platform runtime
  -> Locally built KEY-Recomp.exe + local asset directory
```

Preparation may depend on DolphinTool; running the finished game must not require a Dolphin process. Decide whether the first Setup package includes an appropriately licensed extractor or requires a user-supplied DolphinTool after the core runtime is established.

## P0: Input baseline and project constraints

The foundational work is complete.

- [x] Identify the actual dump and check the key target and DOL hash.
- [x] Establish data isolation, pinned references, extraction tooling, and synthetic DOL tests.
- [x] Extract DATA; inspect DOL sections/entry/BSS; inventory files and explicit REL files.
- [x] Run full Dolphin verification and generate a file hash inventory. Low-severity issues are recorded in P1-RESULTS.md; a matching DOL does not establish whole-disc integrity.

Acceptance: unknown versions and incorrect DOLs fail explicitly; no game bytes enter public source. The DOL baseline passed, and full-verification issues are documented.

## P1: Translator feasibility

Experiments and the gap report are complete. See [P1-RESULTS.md](P1-RESULTS.md): toolchain checks passed and every input symbol had generated output. The unresolved target `0x60` and indirect-dispatch gaps were carried into P2. This phase alone did not demonstrate a running game.

1. Install the pinned .NET 8 SDK under `.tools/`; record its download URL and checksum. Build the pinned translator and run tests that do not use game data.
2. Establish r2/r13 from the validated DOL startup logic. Convert key function symbols into a translator map and ensure their addresses lie in executable sections; do not equate symbols with complete function coverage.
3. Create an independent RK5E01 manifest, disallow unsupported-instruction tolerance, and keep all output in private/. Do not inherit MKW configuration.
4. Translate small functions with synthetic semantic checks, then traverse entry points/symbols and inventory unsupported instructions, indirect branches, callbacks, and ABI requirements. The P1 synthetic experiment used independent integer calculations, not an actual Dolphin state trace. Dynamic differential validation remains required.
5. Inspect upstream BSS loading order and MEM2 assumptions. Report function coverage separately from semantic correctness.

Acceptance: reproducible translation experiments and an explicit gap report. If PPC semantics or runtime assumptions prove unsuitable, use evidence to choose upstream changes or a replacement translator. Success stubs are not progress.

## P2: Minimal host runtime

The main-loop milestone has been reached: original OSInit, static constructors, GXInit, gfl::Init, and Game::Init return, and Game::Run continues executing. Native interrupts, fibers, WPAD, AX/DMA, and local NAND support the paths needed for title startup. Full exception, floating-point, and device differential validation remains open. Evidence is in [CURRENT-RUN.md](CURRENT-RUN.md).

Scope: Clang/Ninja/CMake, checked guest memory, big-endian access, native function dispatch, diagnostics, and basic OS/heap/file interfaces. Compare reproducible startup traces with Dolphin. Unknown services must report errors. Keep Ghidra headless projects in private/analysis/; use a DOL loader or manual section mapping and retain the analysis configuration.

Acceptance: correctly finish static initialization and enter the main loop. Report actual missing services rather than accepting a successful link as the milestone.

## P3: First frame and title menu

Integrate the GX backend, FIFO/display lists, textures, and TEV; support VI, input, threads/timing, and title audio. Establish correctness at the original resolution/aspect ratio first.

As of 2026-11-14, title/menu display, initial save creation, and single-player file selection have been directly exercised. Standalone no-argument launch, menu direction/confirmation, and window enlargement have been verified. Fixed-input/screenshot comparison against Dolphin remains incomplete, so full P3 correctness acceptance and full-game compatibility are not claimed. The PCM backlog fix and remaining modal-window catch-up delay are recorded as P3-005 in ISSUES.md.

Acceptance: launch the game title/menu without a Dolphin process; accept input; compare screenshots against the original; do not silently accept unimplemented services. A window or test triangle does not count as a game frame.

## P4: Complete first-level gameplay loop

Scope: level loading, asset streaming, character physics, yarn transformations, collision, audio, video, save/load, and return to title. Test keyboard/controller and two-player requirements; compare fixed input sequences against the original.

On 2026-11-14, the owner reported entering gameplay and then encountering a crash in the first level. Track this as P4-001. The report establishes a reported level-entry milestone, not level completion or an independently diagnosed crash. A later owner report states that the game now runs normally. Preserve the earlier crash as undiagnosed history and investigate if it recurs; no specific fix for that report is claimed.

Acceptance: complete the first level from a new file, reload its save, and verify key scene graphics/audio/timing before expanding level coverage.

## P5: High resolution and stability

Validate 1080p/1440p/4K internal rendering, including EFB copies, masks, outlines, UI, cutscenes, and video. Rendering resolution must not change the simulation tick. Preserve the original aspect ratio by default; higher frame rates and widescreen behavior are separate follow-up work.

Acceptance: the same input preserves game behavior. Record frame times, VRAM/RAM use, and reproducible visual issues. Cover the full game, two-player behavior, and long runs before calling it a playable version.

## P6: Local builds and public distribution

Automate dump/tool-version checks, disk-space checks, caching, and recovery from preparation failures. Generate the game EXE locally for subsequent direct launch. Public Setup packages must use an explicit file allowlist, audit asset/code provenance and licensing, and be retested on a clean machine without this workspace's caches.

Acceptance: the public package contains no game data, translated game code, or game EXE. A new user can install and run using their own supported dump. Assets may remain in a separate directory; embedding them into a single EXE is not required.

## Current scope

Do not constrain research with an unvalidated calendar estimate. P0/P1 established reproducible input and translation baselines; P2 main-loop and P3 title/menu interaction have direct evidence. Continue with stability, Dolphin differential checks, and P4 gameplay validation, building on the owner's latest report of normal gameplay and retaining the earlier crash for recurrence tracking. No full-game playability conclusion has been reached. The current handoff is CURRENT-RUN.md.
