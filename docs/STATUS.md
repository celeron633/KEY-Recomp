# Project status (2026-11-14)

See [CURRENT-RUN.md](CURRENT-RUN.md) for the detailed handoff. The local `private/game/KEY-Recomp.exe` launches without arguments into the original title/menu. Initial save creation, save reload, direction input, and confirmation have been directly verified. No Dolphin process is used at runtime.

**Gameplay checkpoint:** the owner reports that gameplay runs. The earlier first-level crash report is retained as **P4-001** in [ISSUES.md](ISSUES.md). A follow-up found a retained scene-drawing failure and fixed a confirmed Wii MEM2 palette-address truncation defect consistent with that diagnostic. The original crash sequence has not been reproduced, so the relationship remains an inference. Full-game completion, gameplay save/reload, and compatibility still need structured validation.

After installation, the owner confirmed: "fixed, tested myself". This is owner verification of the fix; the exact tested sequence and full first-level acceptance loop have not been independently recorded.

**Performance follow-up — event polling optimization installed:** the stationary bridge comparison improved from 44.79 to 50.98 display copies and game-root traversals/s, about 13.8% higher observed throughput. The new 60.104-second window ranged from 48.77 to 52.88/s, with 1.0000x guest time and no audio/scheduler waits. The location matches, but collectible states differ, so this is not an identical-input replay. A later interval had an undiagnosed frame-boundary stall; consistent 60 Hz remains open. The build reduces repeated clock/device checks while retaining inline checked RAM, indexed dispatch, and GPU hazard tracking. Hardware Direct3D 12 rendering on the RTX 3060 is directly verified. See P4-002 and CURRENT-RUN.md for evidence and remaining work.

## Current milestone

- P2: native main-loop milestone reached. Full exception, floating-point, and device correctness remain under validation.
- P3: title/menu interaction and save creation/reload verified. Fixed-input comparison against Dolphin is still pending.
- P4: normal gameplay reported by the owner; MEM2 palette upload decoding is now corrected and covered synthetically. Completion, save/reload after gameplay, and return-to-title acceptance are not established.
- P5/P6: high-resolution coverage, long-run stability, clean-machine setup, and public packaging remain incomplete. This is not a fully playable release.
- The exit after a window click was traced to a PCM backlog guard. Output backpressure and guest-clock suspension were implemented and installed locally; see P3-005. Long Windows system-menu pauses can still cause audio catch-up delays.
- The MEM2 palette correction was rebuilt and installed locally; no-argument startup reached the title screen. Automated input checks stopped on detected concurrent user input, leaving the game running. This pass does not establish first-level completion.

## Validation baseline

The latest runtime verification passed **18 CTest cases and 19 Python tests**, including dispatch-clock reuse/refresh, VI/GPU event polling, inline/reference memory-policy checks, the native dispatch index, Wii palette transfers, clock suspension, and PCM backpressure. Incorrect-DOL rejection, hardware D3D12 pixel readback, and WPAD input checks also passed. Real-DOL poisoned-BSS checks remain separate earlier evidence. These results and manual observations do not establish full-game correctness. The preceding inline-RAM checkpoint had 16 CTest cases; the historical pre-commit baseline was 12 CTest cases and 19 Python tests.

The native DOL build profile contains approximately 32,674 functions, excluding native services and 14 functions with unsafe `rfi` lowering. Historical translation experiments generated a different set of 32,737 functions; code generation counts are not execution or semantic coverage.

## Validated input baseline

- Input: `asset/Kirby's Epic Yarn.wbfs`, 3,831,496,704 bytes. DolphinTool identified RK5E01, USA, NTSC-U, Rev 0.
- DATA partition: `private/extracted/RK5E01/DATA/`, 2,815 files totaling 3,705,257,015 bytes. The original dump was not modified.
- `main.dol`: 9,340,736 bytes; SHA-1 matches the key configuration: `f4280a2b0709bfde4132be22e9f7fa8ecfa2a130`.
- DOL SHA-256: `a73cd2f5b1768268451aa28efa649bcf95461b25a481fc4b6cd053c941cf9079`.
- Entry: `0x80004050`; two text sections and eight data sections. The BSS envelope starts at `0x808E4D00`, has size `0xEF578`, and surrounds data[7]. Loading must preserve initialized data.
- No standalone `.rel` files were found by extension scanning. Archive contents and dynamic module loading are not fully validated.
- Full Dolphin verification and an extracted-file SHA-256 inventory were completed. Verification reported three low-severity issue categories; see [P1-RESULTS.md](P1-RESULTS.md). This is not a zero-error disc-integrity result.

## Tool and dependency status

- Python 3.14.6, Git, and CMake are available in the exercised workspace.
- DolphinTool at `D:/Program Files/Dolphin-x64/DolphinTool.exe` was used for header inspection and extraction. This is a local example path, not a portable installation requirement.
- The Ghidra headless entry point exists at `D:/ghidra_12.2_PUBLIC/support/analyzeHeadless.bat`; its execution and Java configuration have not been verified.
- `.tools/dotnet/` contains SDK 8.0.428; `.tools/llvm-mingw-20261104-ucrt-x86_64/` contains Clang 23.1.5; `.tools/ninja/` contains verified Ninja 1.13.2. Downloads are pinned in [toolchain.lock.json](../toolchain.lock.json). The system PATH was not changed.
- key, WiiCompiled, the Aurora fork, and selected Dolphin reference sources are pinned in [references.lock.json](../references.lock.json). Aurora is now used by the native graphics/input backend; older statements that it was not checked out are obsolete.

## Historical evidence

[P1-RESULTS.md](P1-RESULTS.md) preserves the 13 Python parsing checks, 778 upstream reported passes, 10,025 synthetic integer-addition cases, 32,697 input symbols, and entry/mapped translation experiments. Some upstream oracle tests returned early when external dependencies were unavailable, so their pass count is not a complete differential validation result.

The initial extraction command `extract -s sys/main.dol` did not extract the system DOL; full DATA extraction with `-g` succeeded. The parser initially rejected a BSS envelope surrounding initialized data; it now records that overlap and has a synthetic regression test. These are resolved input-preparation issues, not current native boot blockers.

## Continue from here

Use CURRENT-RUN.md for structured gameplay validation. If the earlier P4-001 crash recurs, preserve the crash log and local build identity before relaunching and establish a reproduction sequence. Do not restart work from historical MMIO or static-constructor stop points: title/menu startup is already implemented. Keep the dump read-only and continue validating floating-point behavior, device timing, gameplay, and Dolphin comparisons separately.
