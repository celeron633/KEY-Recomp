# Current native runtime handoff (2026-11-14)

The native Windows x64 EXE has directly displayed the original Kirby's Epic Yarn title/menu (ONE PLAYER / TWO PLAYERS). Direction input, confirmation, single-player file selection, initial save creation, and save reload have been exercised through desktop automation. The original goal of reaching the main interface has been met.

**Latest owner feedback:** the game now runs normally, including gameplay. The earlier first-level crash report remains recorded as **P4-001** in [ISSUES.md](ISSUES.md); its cause was not established and no specific fix is claimed for it. The latest report is a user observation, not an independently repeated full-game test. The complete first-level acceptance loop and full-game compatibility remain unverified.

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
- **Graphics:** convert guest CP/XF/BP, FIFO, display lists, and texture/TLUT pointers for Aurora/D3D12. FIFO writes made with the GP link disconnected are stored in guest RAM for display-list recording.
- **Black-frame fix:** the game first CopyDisp's the previous frame, then draws the next one. Ending the host frame at VIWaitForRetrace cleared post-copy drawing. Presentation now occurs when the actual display copy completes.
- **Vertex-array ranges:** GX provides no array length. Derive the required byte range from draw/XF indices, with bounds checks and bounded growth, instead of uploading all remaining RAM. This avoids the observed roughly 45 MiB upload into an 8 MiB staging buffer.
- **EFB/XFB CPU access:** pending GPU readback regions have hazard checks. CPU reads and partial overwrites fail explicitly; synchronous readback remains unimplemented. GPU texture reuse does not require fabricated RAM contents.
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

Latest runtime checks: **19 Python tests and 12 CTest cases passed**, including guest-clock suspension and PCM backpressure. Earlier D3D12 synthetic pixel readback, WPAD synthetic report/callback/short-key tests, and real poisoned-BSS checks passed. These are separate from observed native menu behavior. Before the initial commit, the 19 Python tests and 12 CTest cases were rerun successfully. Gameplay was not independently rerun in that pass.

Evidence locations:

- Earlier verification: `private/reports/final-checks.log`.
- PCM regression tests: `private/reports/pcm-backpressure-ctest.log` and `private/reports/pcm-backpressure-python.log`.
- Window-stall/backpressure run: `private/reports/pcm-backpressure-live.log`.
- Pre-fix log and build manifest: `private/reports/crash-20261114-pcm-backlog/`.
- Installed build manifest: `private/game/local-build.json`.

The window system-menu experiment induced actual PCM backpressure and recovered to responsive menu input. A long modal pause can still create an audio catch-up delay; this was not treated as a passed long-run synchronization test.

## Next work

1. Record structured gameplay coverage following the owner's report of normal operation. If P4-001 recurs, preserve its crash log/build identity and establish a reproducible case. Do not attribute it to the earlier PCM guard without evidence.
2. Complete the first-level loop: character input, scene streaming, audio, save/reload, and return to title. Normal gameplay has been reported, but completion is not verified.
3. Compare fixed input, pictures, and sound with Dolphin, especially floating-point/paired-single behavior, scheduling, copies, and filtering accuracy.
4. Implement/validate synchronous EFB/XFB readback, uncovered Wii services, exception/`rfi` paths, controller extensions, and remote-speaker behavior.
5. Establish explicit pause boundaries for long host window modal operations. Backpressure prevents the old queue-overflow exit, but previously accumulated audio debt can delay visual/input recovery.
6. Validate higher internal resolutions and long runs. Higher frame rates remain separate work.

All game data, generated code, EXEs, logs, screenshots, and saves remain private. The owner has authorized an initial local source commit; no push or publication is authorized. `install_local.py` uses an explicit local assembly allowlist and is not a public game-release packager.
