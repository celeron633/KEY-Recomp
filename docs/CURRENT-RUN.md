# Current native runtime handoff (2026-11-14)

The native Windows x64 EXE has directly displayed the original Kirby's Epic Yarn title/menu (ONE PLAYER / TWO PLAYERS). Direction input, confirmation, single-player file selection, initial save creation, and save reload have been exercised through desktop automation. The original goal of reaching the main interface has been met.

**Gameplay checkpoint:** the owner reports that gameplay runs. The earlier first-level crash report remains recorded as **P4-001** in [ISSUES.md](ISSUES.md). A follow-up fixed a confirmed MEM2 palette-address truncation defect consistent with a retained scene-drawing crash log; the original input sequence and causal relationship to the owner's report remain unverified. The complete first-level acceptance loop and full-game compatibility remain unverified.

**Performance checkpoint (P4-002): improved, with remaining heavy-scene drops.** CPU profiles led to inline checked RAM access throughout the translated program, a sparse native-function index, smaller GPU hazard regions, and reduced graphics-command allocations. The owner tested the installed build and reported "much better than before". Game tick, physics, and audio rates are unchanged. Earlier page-only and scalar-helper candidates were insufficient; their evidence remains in ISSUES.md.

**Latest measurements:** the final 60.102-second sample at owner confirmation ranged from 42.78 to 57.48 display copies/s, averaging 51.38. Original game-root task traversal rate matched display-copy rate in that sample; the timebase remained 1.0000x and audio had no backpressure waits. This was moving gameplay, not a fixed-input before/after comparison. Consistent 60 Hz gameplay and exact character-speed equivalence are not established. Hardware rendering was directly verified on NVIDIA GeForce RTX 3060 through Direct3D 12; CPU adapters are disabled.

**Installed build:** `private/game/KEY-Recomp.exe` contains the current inline checked-RAM implementation, identified by `private/reports/timing-20261114/inline-build.json`. The full rebuild passed 16 CTest cases, 19 Python tests, and incorrect-DOL rejection, then launched and reached gameplay. `pre-inline.exe` and `nand-before-inline/` in that evidence directory preserve the preceding executable and save backup. The second/third-candidate build distinctions below are historical, not the current installation state.

## Local launch

Double-click `private/game/KEY-Recomp.exe` without arguments. It uses the host's current civil time for the RTC. The application has been run with a child-process PATH containing only Windows System32, without developer-tool PATH entries or a Dolphin process.

- Data directory: `private/extracted/RK5E01/DATA`. The DOL version/hash must match RK5E01 Rev 0.
- Default window: 1280x720; maximized title rendering has been observed. Preserve the original aspect ratio and game tick. Higher rendering resolution does not improve the source artwork, and resizing alone does not validate every rendering path at high resolution.
- Controls: arrows/WASD for directions; X = Wii 2 (confirm); Z = Wii 1 (back); Enter = A; C = B; Space = Plus. Numpad direction aliases are also supported.
- Saves: `private/user/nand`. Replaced/deleted nodes are moved to `private/user/nand-deleted`. The original dump remains read-only.
- Application log: `private/reports/native-app.log`. Preserve it before a new launch overwrites the evidence.
- Title/menu screenshot: `private/reports/screenshots/native-title-menu.jpg` (private evidence only).

The earlier exit after a window click was traced to the old two-second PCM backlog guard. Bounded output backpressure now pauses the guest timebase while waiting, preserving audio and pending events. Paused output devices can resume; a device that stops consuming audio still produces a diagnostic. The local EXE was rebuilt/reinstalled and tested with clicks, maximize/restore, and menu navigation. Details and remaining uncertainty are in P3-005.

## Directly observed startup path

Original OSInit -> 217 C++ static constructors -> gfl::Init / GXInit / KPAD / AX initialization -> Game::Init -> Game::Run -> wrist-strap notice -> sideways-controller notice -> save creation confirmation -> original title/menu.

Initial save creation wrote FLF.bin, banner.bin, and photo-slot files. A subsequent standalone launch read the save and reached title selection. No game flags were manually set to claim initialization or save completion.

The native DOL build profile contains approximately 32,674 functions. Native services and 14 functions with unsafe `rfi` lowering are explicitly excluded. Build coverage is not semantic validation coverage. All translated functions and game executables remain in private/.

## Implementation and fixes

- **Guest memory:** MEM1/MEM2, big-endian access, aliases, bounds checks, and code write protection. The poisoned-BSS run verified 949,240 bytes cleared and 1,086,336 initialized bytes preserved. Memory's internal state is hidden in its C++ implementation to avoid recompiling all translated functions for each device change.
- **Interrupts and threads:** PI/VI/SI/EXI/DSP/PE event models. Windows fibers preserve native C++ continuations; original queues/thread code still manage priority and wakeups. IRQ restoration checks integer, FP, GQR, and execution state, including MEM2 contexts. The decrementer invokes original alarm callbacks.
- **Interrupt progress:** distinguish newly arriving interrupts from unacknowledged interrupts. When acknowledgments make progress, handle work in batches and leave remaining events pending. Do not mistake a queue still busy after 64 iterations for a deadlock or discard events.
- **Graphics:** convert guest CP/XF/BP, FIFO, display lists, and texture/TLUT pointers for Aurora/D3D12. The selected hardware adapter is logged; CPU adapters and unexpected backends are rejected. FIFO writes made with the GP link disconnected are stored in guest RAM for display-list recording.
- **Black-frame fix:** the game first CopyDisp's the previous frame, then draws the next one. Ending the host frame at VIWaitForRetrace cleared post-copy drawing. Presentation now occurs when the actual display copy completes.
- **Vertex-array ranges:** GX provides no array length. Derive the required byte range from draw/XF indices, with bounds checks and bounded growth, instead of uploading all remaining RAM. This avoids the observed roughly 45 MiB upload into an 8 MiB staging buffer.
- **Wii palette addresses:** TLUT uploads now retain all 24 BP source bits before converting 32-byte units to physical addresses. The old 21-bit mask could alias MEM2 palettes to MEM1 or fail on an unmapped address. Synthetic CI4/CI8/CI14X2 transfers verify address/content correctness, snapshots, masking, and bounds; the retained crash is consistent with this defect but its exact gameplay sequence has not been repeated.
- **EFB/XFB CPU access:** pending GPU readback regions have hazard checks. CPU reads and partial overwrites fail explicitly; synchronous readback remains unimplemented. GPU texture reuse does not require fabricated RAM contents.
- **Memory throughput:** pending-copy bitmaps distinguish 64-byte regions within each 4 KiB page, refresh only affected pages, and retain the index on no-op overwrites. Exact overlap checks still reject CPU reads/partial overwrites of pending copies. Ordinary RAM bypasses device decoding and uses fixed-width endian loads/stores, preserving bounds, alignment, low-memory, code-write, and GPU hazards. Five-second `TIMING` log summaries report guest-clock ratio, display-copy rate, VI-event rate, PCM throughput, scheduler sleep, frame-boundary time (including FIFO synchronization), and audio waits.
- **Inline scalar ABI:** translated loads/stores inline the checked RAM path using host-owned backing metadata. Uninitialized boot memory, MMIO, code-page writes, and possible GPU hazards retain the original checked handlers. Fast stores preserve generation tracking; MEM2 reallocation refreshes the view. Raw pointer/range optimizations stay disabled. `KEY_DISABLE_INLINE_RAM=1`, set before launch, selects the original checked-memory path for diagnostics. `game_root_hz` counts original game-root task traversals, separately from presentation.
- **Dispatch:** an immutable sparse index resolves complete 32-bit guest function addresses without binary searches. Misalignment, missing registrations, duplicate entries, and index-capacity errors remain explicit. Original native service handling and guest event delivery are unchanged.
- **Input:** SDL host devices -> native WPAD -> original KPAD. Standard Wii Remote profiles, callbacks, and big-endian reports are supported. Short keyboard events are held through sampling; directions use the sideways mapping. Input activity calls original __VIResetRFIdle. Idle dimming is a native presentation overlay, not a system display-setting change; the dimming curve still needs hardware comparison.
- **Audio:** validate the local AX program CRC=4CC52064. Import generic synchronous mixing algorithms from the pinned WiiCompiled revision, excluding other games' addresses and guessed layouts. DSP mailbox, original AX callbacks, and AI DMA feed native SDL PCM output. Coefficients are copied only from the user's local Dolphin directory to private/boot; Dolphin is not required at runtime.
- **PCM backpressure:** bound the SDL queue and suspend the guest timebase during output waits so the wait does not create additional AI/VI/alarm debt. Keep every PCM block and event. Device-loss/no-consumption failures remain explicit.
- **NAND:** directory-constrained creation, attributes, read/write, seek, stat, rename, usage, enumeration, and asynchronous callbacks. Virtual attributes persist, and cleanup retains old nodes reversibly. Neither the original dump nor extracted input files are written.

## Reproduction and verification

For the prepared workspace:

```powershell
python tools/build_probe.py --graphics
python tools/install_local.py
python -m unittest discover -s tests -v
ctest --test-dir private/build/graphics --output-on-failure
```

The default build_probe path builds and validates; it no longer expects an early P2 stop address. `--explore` starts a diagnostic run with call/time budgets. Standalone GUI startup is not subject to the diagnostic call-count limit. These commands are not yet a verified clean-machine installation workflow; see [CONTRIBUTING.md](../CONTRIBUTING.md).

Latest runtime checks: **19 Python tests and 16 CTest cases passed**, including the inline/reference checked-memory routes, dispatch index, palette transfers, clock suspension, and PCM backpressure. Incorrect-DOL rejection and a hardware D3D12 triangle readback also passed. The earlier palette checkpoint had 13 CTest cases; its first fixture failure and successful regression remain documented in ISSUES.md. Earlier WPAD and poisoned-BSS evidence is separate from current gameplay observations. The historical pre-commit baseline was 19 Python tests and 12 CTest cases.

Evidence locations:

- Earlier verification: `private/reports/final-checks.log`.
- PCM regression tests: `private/reports/pcm-backpressure-ctest.log` and `private/reports/pcm-backpressure-python.log`.
- Window-stall/backpressure run: `private/reports/pcm-backpressure-live.log`.
- Pre-fix log and build manifest: `private/reports/crash-20261114-pcm-backlog/`.
- Installed build manifest: `private/game/local-build.json`.
- Retained scene-drawing failure, pre-fix build identity, and save backup: `private/reports/gameplay-unmapped-20261114/`.
- Palette regression before correction: `private/reports/tlut-regression-before.log`; passing checks: `private/reports/tlut-ctest.log` and `private/reports/tlut-python.log`.
- Final graphics build, 13 CTest checks, and same-size incorrect-DOL rejection: `private/reports/tlut-build-validation-final.log`.
- Timing investigation and optimized build: `private/reports/timing-20261114/`, including `measured-before.log`, `cpu-summary.txt`, synthetic benchmark results, `host.json`, `final-build.log`, `python-tests.log`, and `local-build-after.json`.
- Latest validation: `inline-final-validation.log` (16 CTest cases and wrong-DOL rejection), `inline-ram-python.log` (19 Python tests), and `adapter-smoke.log` (hardware GPU readback), all in the timing evidence directory.
- Latest live evidence: `inline-owner-confirmed.log`, `inline-owner-summary.json`, `inline-overworld.*`, and `inline-build.json` in the same directory.

The earlier scalar-helper candidate passed 13 CTest cases (`second-candidate-ctest.log`); it was subsequently superseded. The current scalar ABI is tested in both inline and reference modes against byte-wise endian expectations and access-policy failures. An independent four-million-pair benchmark measured 57.356 ms through general checked calls and 25.814 ms through inline checked RAM with equal checksums. This is not a gameplay speedup ratio. The owner subsequently confirmed the installed result as "much better than before".

The palette-corrected executable was installed with `python tools/install_local.py` and launched without arguments. Startup reached the original title screen directly; `private/reports/gameplay-unmapped-20261114/` contains the title screenshot, `native-after-title.log`, and `local-build-after.json`. The desktop tool detected concurrent user input, so automated button checks stopped and the game was left running for the owner. No automated gameplay completion or independent audio comparison is claimed in this pass. A pre-check NAND backup is retained in the same evidence directory.

The owner subsequently confirmed: "fixed, tested myself". Record this as successful owner verification of the installed fix. The exact sequence, level completion, and gameplay save/reload were not specified, so the broader P4 acceptance gate remains open.

The window system-menu experiment induced actual PCM backpressure and recovered to responsive menu input. A long modal pause can still create an audio catch-up delay; this was not treated as a passed long-run synchronization test.

## Next work

0. Continue P4-002 toward stable 60 Hz in reproducible complex scenes. Use `game_root_hz` with display-copy rate and a fixed-input original/native comparison. The current build is owner-confirmed as substantially improved, not validated as full-speed throughout the game.
1. Exercise gameplay scenes that upload MEM2 palettes with the corrected build, and record the starting save and input sequence. If P4-001 recurs, preserve its crash log/build identity and establish a reproducible case. The retained log is consistent with the palette defect, but has not been tied to the original reported sequence; do not attribute it to the earlier PCM guard.
2. Complete the first-level loop: character input, scene streaming, audio, save/reload, and return to title. Normal gameplay has been reported, but completion is not verified.
3. Compare fixed input, pictures, and sound with Dolphin, especially floating-point/paired-single behavior, scheduling, copies, and filtering accuracy.
4. Implement/validate synchronous EFB/XFB readback, uncovered Wii services, exception/`rfi` paths, controller extensions, and remote-speaker behavior.
5. Establish explicit pause boundaries for long host window modal operations. Backpressure prevents the old queue-overflow exit, but previously accumulated audio debt can delay visual/input recovery.
6. Validate higher internal resolutions and long runs. Higher frame rates remain separate work.

All game data, generated code, EXEs, logs, screenshots, and saves remain private. The owner has authorized an initial local source commit; no push or publication is authorized. `install_local.py` uses an explicit local assembly allowlist and is not a public game-release packager.
