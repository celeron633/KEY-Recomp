# Current native runtime handoff (2026-11-14)

The native Windows x64 EXE has directly displayed the original Kirby's Epic Yarn title/menu (ONE PLAYER / TWO PLAYERS). Direction input, confirmation, single-player file selection, initial save creation, and save reload have been exercised through desktop automation. The original goal of reaching the main interface has been met.

**Gameplay checkpoint:** the owner reports that gameplay runs. The earlier first-level crash report remains recorded as **P4-001** in [ISSUES.md](ISSUES.md). A follow-up fixed a confirmed MEM2 palette-address truncation defect consistent with a retained scene-drawing crash log; the original input sequence and causal relationship to the owner's report remain unverified. The complete first-level acceptance loop and full-game compatibility remain unverified.

**Performance checkpoint (P4-002): improved, with remaining heavy-scene drops.** CPU profiles led to inline checked RAM access throughout the translated program, a sparse native-function index, smaller GPU hazard regions, and reduced graphics-command allocations. The owner tested the installed build and reported "much better than before". Game tick, physics, and audio rates are unchanged. Earlier page-only and scalar-helper candidates were insufficient; their evidence remains in ISSUES.md.

**Latest measurements:** after the owner selected and confirmed a new measurement window, the coalesced-write build averaged **56.76 display copies and game-root traversals/s** over 60.115 seconds (five-second range 52.77–58.66). The screenshot shows the bridge area with 230 beads. Guest time remained 1.0000x, VI events approximately 59.94 Hz, PCM approximately 125 KiB/s, and audio waits zero. This is separate from the earlier 57.81/s window and the preceding event-polling build's 50.98/s result; collectible states and inputs differ. Consistent 60 Hz and exact character-speed equivalence remain unverified. Hardware rendering remains NVIDIA GeForce RTX 3060 through Direct3D 12, with CPU adapters disabled.

**Installed build:** `private/game/KEY-Recomp.exe` now includes the controller/settings follow-up and corrected gyro direction baseline, identified by `private/reports/controller-20261114/direction-build.json`. Verification passed 20 CTest cases, 19 Python tests, incorrect-DOL rejection, and graphics/input smoke checks. The earlier coalesced-write build is identified by `private/reports/perf-generations-20261114/after-build.json`; its performance samples remain historical measurements, not a benchmark of the controller build. The initial controller executable is retained as `before-direction.exe` in the controller evidence directory.

## Controller/settings follow-up

The owner requested Bluetooth DualShock 4 support, gyro-based IR aiming, sideways tilt for interactions such as the tank in `docs/wiimote.png`, and an F1 settings menu with FPS. The supplied image remains untracked; do not commit or publish it.

Implementation lives in `controller/`: `input.cpp` handles guest WPAD services/keyboard sampling, `controls.cpp` handles SDL device selection, mapping, settings persistence and overlay, and `motion.h` handles independent motion math. `controller/CMakeLists.txt` owns its object target and synthetic test targets. Graphics only calls lifecycle, polling, and overlay entry points. Existing pinned SDL/Aurora/ImGui dependencies are reused without source imports or reference modifications.

F1 opens settings, F2 toggles/saves FPS, Q/E provide keyboard tilt, and R/R3 recenter the pointer. Settings include player-1 device selection, button reassignment, gyro calibration, pointer source, sensitivity, deadzones and axis inversion. Edits apply immediately; Save settings persists them under `private/user/controls.ini`. Menu capture neutralizes gameplay input but deliberately does not claim to pause the game. See [CONTROLLERS.md](CONTROLLERS.md) for controls and limitations.

Direct observations from the initial controller build: the connected `PS4 Controller` reported both gyro and accelerometer support, F1 displayed the settings overlay, and a title-screen FPS overlay displayed approximately 60 FPS. A concurrent user-input notice ended automated key injection; the game was left running. This is a title/menu observation, not a timed gameplay performance result or physical motion validation.

The owner subsequently reported that basic functionality works, but gyro horizontal and vertical directions appeared reversed. Both default gyro signs are now reversed in the motion integrator, with inversion switches retained. Tilt and right-stick defaults are unchanged. The owner also requested a separate controller directory; all controller code, including the previously existing renderer WPAD implementation, has been moved. After installing this correction, the owner reported normal basic functionality with no further apparent bugs and requested a local commit. This is owner verification; the exact test sequence was not independently recorded.

Validation: `python tools/build_probe.py --graphics --jobs 12` passed 20 CTest cases and same-size wrong-DOL rejection (`private/reports/controller-direction-build.log`). Python discovery passed 19 tests (`controller-direction-python.log`). The relocated `private/build/graphics/controller/input_smoke.exe` passed synthetic SDL-to-guest WPAD button/acceleration/IR checks, duplicate-action prevention, menu capture/release, focus loss, disconnect, Q/E tilt, settings replacement/reload, and invalid settings rejection (`controller-direction-input.log`). Hardware D3D12 synthetic RGB readback passed (`controller-direction-graphics.log`). The malformed-settings warning in the input log is expected test evidence; teardown logs report the graphics device being destroyed. Earlier test iterations failed because hidden-window SDL joystick input was suppressed; the isolated synthetic fixture now explicitly enables background joystick events. Production focus handling remains enabled.

Next: record a reproducible Bluetooth DS4 scene/input sequence for the corrected axes and tank tilt; extend validation to calibration/recenter behavior and reconnect stability. USB, other controller models, player-2 motion, and full-game motion coverage are unverified. No game-time or high-resolution behavior was changed. Local build backups and manifests remain under `private/reports/controller-20261114/`.

## Event polling follow-up

The owner supplied a stationary in-level scene on 2026-11-14. Before compilation, twelve intervals totaling 60.145 seconds measured 44.79 display copies and game-root traversals/s (43.92–45.90), 1.0000x guest time, 59.94 Hz VI events, 125 KiB/s PCM, and no scheduler/audio waits. The screenshot shows Kirby beside a bridge, with 24 beads. This is a new baseline, not the earlier moving-gameplay sample. Evidence is under `private/reports/perf-dispatch-20261114/`, including `scene-before.log`, `scene-before-summary.json`, `scene-before.jpg`, `before.exe`, `before-build.json`, and `nand-before/`.

The event-polling checkpoint shares a clock sample only within one dispatch's event checks, invalidates it after input polling/callbacks, audio submissions, and external IRQs, skips unchanged VI halflines, and reads empty GPU event mailboxes before attempting an atomic exchange. Guest timebase/MMIO reads remain fresh. That candidate was installed (`after-build.json` in the event-polling evidence directory) and measured at the same bridge location as above. Full verification at that checkpoint passed 18 CTest cases, 19 Python tests, incorrect-DOL rejection, D3D12 pixel readback on the RTX 3060, and WPAD input checks including the shared-timestamp 5 ms polling gate. Evidence: `final-validation.log`, `python-tests.log`, `graphics-smoke.log`, and `input-smoke.log`.

An early CTest invocation ran the existing 16 cases successfully but could not run the two new executables before they were built. The six-worker full rebuild was intentionally interrupted and resumed with twelve workers; this was not a compiler failure. The independent synthetic polling benchmark measured 84.09–84.65 ms with three clock samples versus 33.38–34.58 ms with one reused sample per million passes; both paths used the new device implementation. This isolates clock reuse and is not a gameplay speedup ratio (`poll-bench.cpp`, `poll-bench.log`). Set `KEY_DISABLE_POLL_CLOCK=1` before launch to disable only dispatch timestamp reuse for diagnostics.

Live evidence: `after-scene-start.log`, `scene-after.log`, `scene-after.jpg`, `comparison-window.log`, and `comparison.json`. `compare.py` selects the first twelve complete intervals after the ready marker, excluding the straddling interval, rather than choosing by FPS. Later interval 16 measured 40.96 copies/s with 841.25 ms at the frame boundary; its cause is not diagnosed. A separate post-measurement 1,200-sample profile is `poll-after-*` under `private/reports/timing-20261114/`. Clock overhead is reduced; checked memory, dispatch, and CPU-context setup remain material costs. The preceding 51.38/s sample was moving gameplay in a different run and is historical, not this comparison's baseline.

## Coalesced memory-write tracking

The owner supplied `1.png` showing the same bridge area in Dolphin 2512, with JIT64 / Vulkan / HLE in its title bar and 59.94 FPS in the overlay. The owner confirms stable 60 FPS at **5x native resolution and 8x MSAA**. The local configuration also records `InternalResolution=5` and `MSAA=0x00000008`; the screenshot alone is one instantaneous reading, not a timed capture. Private evidence: `private/reports/perf-generations-20261114/dolphin-owner.png` and `dolphin-config-summary.json`. The owner's original untracked screenshot is left untouched and must not be committed.

The installed implementation replaces per-store global atomic version increments with release stores to dirty-page flags. Version queries serialize consumption and assign globally increasing page versions, preserving invalidation across overlapping ranges and multiple readers. Pending EFB hazards and all access checks remain separate and unchanged. Five alternating synthetic trials measured 20.765–22.754 ms before and 7.120–7.565 ms after for four million inline checked write/read pairs, with equal checksums; these are not gameplay ratios. Evidence is in `private/reports/perf-generations-20261114/`. The complete twelve-worker rebuild passed 19 CTest cases, 19 Python tests, wrong-DOL rejection, hardware graphics readback, and WPAD input checks. `tools/build_probe.py --graphics --jobs 12` selects twelve compiler workers without interrupting a build. Synthetic variable-address code generation also confirms removal of `lock/xadd` from inline stores.

The first post-install ready-marked window measured 57.81/s over 60.123 seconds (55.77–59.86), with 40 beads visible in its screenshot. It is retained in `comparison.json` and `comparison-window.log`; the apparent difference from the preceding 50.98/s checkpoint is not an identical-replay speedup. The owner subsequently reported moving elsewhere, so further measurement paused and resumed only after a new ready message. The new window independently measured 56.76/s as described above: `new-scene-start.log`, `new-scene-end.log`, `new-scene-window.log`, `new-scene-window-summary.json`, and `new-scene.jpg`. Both selectors discard the interval crossing the ready marker and take the next twelve complete intervals, without choosing by achieved FPS. No compiler, Dolphin instance, or sampler ran during the new window.

The separate `generation-after-*` CPU profile under `private/reports/timing-20261114/` may include changing scenes and must not be treated as the new stationary measurement. Checked memory, dispatch, context setup, and function lookup remain prominent. The previous event-polling executable remains available for local rollback.

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

Latest runtime checks: **19 Python tests and 20 CTest cases passed**, adding controller motion contracts to the preceding 19-case coalesced-write baseline. The controller section above records the latest build, wrong-DOL rejection, graphics readback, and expanded SDL/WPAD smoke evidence. The preceding event-polling checkpoint had 18 CTest cases, inline RAM had 16, and palettes had 13. Earlier poisoned-BSS evidence is separate from current gameplay observations. The historical pre-commit baseline was 19 Python tests and 12 CTest cases.

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
- Earlier inline-RAM validation: `inline-final-validation.log` (16 CTest cases and wrong-DOL rejection), `inline-ram-python.log` (19 Python tests), and `adapter-smoke.log` (hardware GPU readback), all in the timing evidence directory.
- Earlier inline-RAM live evidence: `inline-owner-confirmed.log`, `inline-owner-summary.json`, `inline-overworld.*`, and `inline-build.json` in the same directory.

The earlier scalar-helper candidate passed 13 CTest cases (`second-candidate-ctest.log`); it was subsequently superseded. The current scalar ABI is tested in both inline and reference modes against byte-wise endian expectations and access-policy failures. An independent four-million-pair benchmark measured 57.356 ms through general checked calls and 25.814 ms through inline checked RAM with equal checksums. This is not a gameplay speedup ratio. The owner subsequently confirmed the installed result as "much better than before".

The palette-corrected executable was installed with `python tools/install_local.py` and launched without arguments. Startup reached the original title screen directly; `private/reports/gameplay-unmapped-20261114/` contains the title screenshot, `native-after-title.log`, and `local-build-after.json`. The desktop tool detected concurrent user input, so automated button checks stopped and the game was left running for the owner. No automated gameplay completion or independent audio comparison is claimed in this pass. A pre-check NAND backup is retained in the same evidence directory.

The owner subsequently confirmed: "fixed, tested myself". Record this as successful owner verification of the installed fix. The exact sequence, level completion, and gameplay save/reload were not specified, so the broader P4 acceptance gate remains open.

The window system-menu experiment induced actual PCM backpressure and recovered to responsive menu input. A long modal pause can still create an audio catch-up delay; this was not treated as a passed long-run synchronization test.

## Next work

0. Continue P4-002 toward stable 60 Hz. Coalesced-write samples measured 57.81/s and, in the separately confirmed latest window, 56.76/s; the preceding event-polling checkpoint measured 50.98/s with different collectible state. Profile checked-memory fallbacks, dispatch/context setup, and translated hot functions next; retain all access and timing contracts. Diagnose the earlier frame-boundary stall if reproducible. Establish identical input/save-state comparisons with Dolphin's owner-reported stable 60 FPS at 5x native and 8x MSAA; full-speed native gameplay remains unvalidated.
1. Exercise gameplay scenes that upload MEM2 palettes with the corrected build, and record the starting save and input sequence. If P4-001 recurs, preserve its crash log/build identity and establish a reproducible case. The retained log is consistent with the palette defect, but has not been tied to the original reported sequence; do not attribute it to the earlier PCM guard.
2. Complete the first-level loop: character input, scene streaming, audio, save/reload, and return to title. Normal gameplay has been reported, but completion is not verified.
3. Compare fixed input, pictures, and sound with Dolphin, especially floating-point/paired-single behavior, scheduling, copies, and filtering accuracy.
4. Implement/validate synchronous EFB/XFB readback, uncovered Wii services, exception/`rfi` paths, controller extensions, and remote-speaker behavior.
5. Establish explicit pause boundaries for long host window modal operations. Backpressure prevents the old queue-overflow exit, but previously accumulated audio debt can delay visual/input recovery.
6. Validate higher internal resolutions and long runs. Higher frame rates remain separate work.

All game data, generated code, EXEs, logs, screenshots, and saves remain private. The owner has authorized an initial local source commit; no push or publication is authorized. `install_local.py` uses an explicit local assembly allowlist and is not a public game-release packager.
