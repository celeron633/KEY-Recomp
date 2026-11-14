# Technical issues and validation record

Updated: 2026-11-14. Keep code generation, function returns, and correct game behavior as separate claims.

**Current checkpoint:** the standalone native EXE reaches the title/menu, creates initial save data, reloads it, and accepts menu input. The owner now reports normal gameplay; the earlier first-level crash remains recorded below. See [CURRENT-RUN.md](CURRENT-RUN.md) for the full handoff. P2 entries below retain historical experiment context; their old stop points are not the current startup state.

## P4-002: Smooth movement slowdown with normal audio

On 2026-11-14, the owner reported movement feeling approximately 0.8x normal speed, including the car transformation, while voice playback sounded normal. This was a subjective estimate, not a measured speed multiplier. The instrumented runtime confirmed a frame-throughput problem: the guest timebase advanced at 1.0000x wall time, VI events stayed near 59.94 Hz, and stereo 32 kHz PCM stayed near 125 KiB/s, while display copies fell below 60 FPS. The owner provided a stationary measurement scene; the final baseline screenshot shows the Grass Land overworld. The last twelve five-second baseline intervals measured 30.06–31.55 copies/s, zero scheduler idle time, and no audio-backpressure waits. Earlier moving/transitioning scenes varied more widely. Host frame-boundary/FIFO waits were small. These measurements do not constitute a fixed-input Dolphin comparison.

A local sampling profile attributed approximately 37% of samples to checked memory read/write routines, with further samples in address resolution and pending-GPU-copy checks. A synthetic workload isolated false overlap in pending EFB-copy tracking: its 64 KiB page index routed unrelated heap accesses through mutex-protected interval checks, and nonoverlapping writes rebuilt the entire index despite removing no pending copy. The final index uses one 64-bit bitmap per 4 KiB page (one bit per 64-byte region), refreshes only affected pages, and does not rebuild on nonoverlapping writes. RAM accesses also bypass the device-register decoder and use fixed-width endian conversions. Exact interval checks, partial-overwrite failures, CPU-read hazards, bounds, code protection, low-memory initialization checks, alignment rules, and game timing remain unchanged.

An initial page-only optimization improved the synthetic benchmark but did not resolve the owner's subjective slowdown ("i feel almost the same"); the live log remained slow. A second profile also exposed stack-probing overhead from rebuilding a larger page index. The final bitmap/affected-page approach removes that rebuild, and the checked RAM path addresses the remaining read/write overhead. This first unsuccessful gameplay retest is retained rather than treating a microbenchmark improvement as a completed fix.

Synthetic benchmark evidence at the scalar-helper checkpoint: four million checked write/read pairs beside a pending copy improved from 753.834 ms to 54.034 ms; without a pending copy, the same benchmark measured 71.829 ms before and 55.610 ms after. A second benchmark kept writes within the same tracked 4 KiB page and improved from 77.359 ms to 0.896 ms for 500,000 nonoverlapping writes. These are microbenchmarks, not gameplay speedup ratios. Tests additionally exercise multiple pending copies sharing a page, unrelated writes, removing one copy while retaining another, every region-mask bit, region/page crossings, and the last RAM page. Scalar tests compare all supported widths with a byte-wise endian oracle, including unaligned integer accesses, cross-page stores, and invalid-width rejection.

Evidence remains under `private/reports/timing-20261114/`: preserved pre-change build identity/log, `measured-before.log`, `scene-before.*`, `cpu-summary.txt`, and the `*-bench-*.txt` results. The Windows kernel profiler could not start; a local sampler recorded only instruction addresses from the game's busiest thread. Sampling can perturb timing and is kept separate from the final stationary baseline intervals. The runtime now emits five-second `TIMING` summaries so guest-clock speed, copy rate, VI events, PCM output, and waits can be distinguished on subsequent runs.

**Historical second-candidate result:** the second installed candidate still measured approximately 32–33 copies/s in the stationary overworld. The owner then reported normal-feeling speed after entering a stage, followed by slowdown as the picture became more complex. Some stage intervals reached 57–60 copies/s. A subsequent 50.100-second observation measured 39.13–57.95 copies/s (weighted average 44.15), with guest-clock ratio 1.0000, approximately 60 Hz VI events, 125 KiB/s PCM, and no audio-output waits in those ten intervals. Preserve this in `stage-observation-start.log` and `stage-observation-end.log`. Display-copy rate alone does not measure character velocity or simulation-update frequency; a fixed-input original/native comparison is still needed. Do not claim that those microbenchmark gains fixed gameplay slowdown.

At that checkpoint, the owner requested current-stage measurement rather than another rebuild, so the installed EXE remained the second candidate (`second-build.json`). Additional helper inlining and shared device-poll time sampling had passed 13 CTest cases (`third-candidate-ctest.log`) but were not yet installed. The later installations below supersede that state. No game tick, physics speed multiplier, frame skipping, or audio-rate change was introduced.

### Continued optimization and GPU verification

The owner subsequently requested continued optimization and asked whether software rendering was responsible. Direct verification in both the synthetic graphics test and the native game selected **NVIDIA GeForce RTX 3060, Direct3D 12**, driver `32.0.16.1088`, with CPU adapters disabled. The synthetic triangle readback passed (`adapter-smoke.log`). The backend now logs the selected adapter and explicitly rejects CPU adapters or an unexpected backend. This is direct device-selection evidence, not merely an inference from configuration.

The next installed checkpoint (`dispatch-build.json`) incorporated the previously uninstalled checked-helper inlining and shared device-poll timestamp, replaced binary searches for guest functions with an immutable sparse index, and removed small heap allocations from ordinary BP register forwarding. All 14 CTest cases passed. Live output still varied below 60 copies/s. A call-stack sampler additionally identified translated asset decompression and audio/OS work among the CPU costs; this sample included changing/loading scenes and is not a stationary-scene comparison.

The installed inline checked-RAM implementation maps supported guest aliases to host-owned RAM using inline bounds/alignment checks. Low boot memory, MMIO, executable writes, and possible pending GPU-copy overlaps fall back to the original checked handlers. Fast stores retain per-page generation tracking, and the view refreshes after MEM2 backing reallocation. Raw pointer/range optimizations remain disabled. Synthetic tests cover all scalar widths, byte-wise endian comparisons, noncanonical addresses, both memory regions, boundary crossings, low-memory initialization, template permissions, GPU hazards, generation updates, and reset/remapping. `KEY_DISABLE_INLINE_RAM=1`, set before launch, selects the original checked-memory route for diagnostic comparisons without changing the executable.

The independent four-million-pair benchmark measured 57.356 ms for the general checked route and 25.814 ms for inline checked RAM, with matching checksums. This is not a gameplay result. Compiler inspection initially found outlined helpers despite the `inline` declarations; explicit hot-helper inlining was then verified in a rebuilt translated function. The first full build was intentionally stopped to make that correction, and the corrected build was later resumed with 12 workers to use the available CPU. Build interruptions are recorded separately from test failures. All generated objects, profiling addresses, logs, and executables remain private.

**Latest outcome: owner-confirmed improvement; consistent 60 Hz remains open.** The complete rebuild passed 16 CTest cases (including both memory routes), 19 Python tests, and incorrect-DOL rejection, and was installed as `inline-build.json`. The owner tested it and reported "much better than before". The 60.102-second moving-gameplay sample preserved at that confirmation ranged from 42.78 to 57.48 display copies/s, averaging 51.38. The newly counted original game-root task traversals matched display-copy rate in these intervals; the timebase remained 1.0000x and no audio-output waits occurred. These were not identical fixed-input runs, so no numeric before/after gameplay speedup is claimed. The lower intervals also mean the issue is not fully resolved. Logs, summary, scene image, and build identity are `inline-owner-confirmed.log`, `inline-owner-summary.json`, `inline-overworld.*`, and `inline-build.json` in the timing evidence directory. Continue with reproducible complex-scene profiling and original/native input comparisons.

## P4-001: First-level crash after entering gameplay

**Status: historical user report; cause undiagnosed, recurrence not established after the latest normal-gameplay report.** On 2026-11-14, after the PCM backlog fix, the owner confirmed that the application could enter gameplay but still crashed in the first level. The exact scene, actions, timing, and diagnostic stop have not been established in this documentation pass. The owner asked to defer this investigation and prepare English documentation for collaborative development. In a subsequent update, the owner stated that the game runs normally and requested the initial commit. No new runtime change or independent reproduction establishes a specific fix for this earlier crash.

This is evidence of reported level entry, not completion of P4's gameplay loop. Do not assume it shares the PCM cause from P3-005. If the crash recurs, preserve `private/reports/native-app.log` and `private/game/local-build.json` before another launch, establish the starting save and input sequence, and correlate a repeatable failure with its runtime service/diagnostic. Keep raw game logs, screenshots, saves, and binaries private. No crash fix or independent reproduction is claimed here.

### Follow-up: Wii MEM2 palette uploads (2026-11-14)

The next implementation pass found a retained native log ending with `STOP: Unmapped/MMIO access at guest address 0x02F662E0` while unwinding scene drawing. The original log and installed build identity were copied to `private/reports/gameplay-unmapped-20261114/` before relaunching. Its exact input sequence and relationship to the owner's earlier report are unknown.

The GX bridge decoded the TLUT source with a 21-bit mask before shifting by five. This discards the Wii MEM2 address selector: for example, `0x12F662E0` becomes the observed unmapped address. Lower MEM2 offsets could instead silently read unrelated MEM1 palette data. The bridge now preserves all 24 BP payload bits, matching the Wii path in pinned Dolphin [BPStructs.cpp](https://github.com/dolphin-emu/dolphin/blob/e36cd326c0af3179bf75a8deb5e2ecf92ab78b72/Source/Core/VideoCommon/BPStructs.cpp#L385-L402). The reference remains local, with its digest added to `references.lock.json`; no third-party implementation was imported.

A new independent synthetic test feeds CI4/CI8/CI14X2 palette uploads from MEM1 and multiple MEM2 locations, checks the resolver address/extent and emitted palette content, verifies immutable snapshots and masked BP writes, and rejects out-of-range reads and TMEM writes. The old bridge fails with `TLUT upload lost Wii physical address bits or transfer extent` (`private/reports/tlut-regression-before.log`). This establishes an address-decoding defect and a plausible explanation for the retained crash, not a reproduced first-level completion or proof that every reported crash had this cause.

After correcting a fixture that reused a terminally failed FIFO, all 13 CTest cases and 19 Python tests passed. The final `python tools/build_probe.py --graphics` run passed, including incorrect-DOL rejection (`private/reports/tlut-build-validation-final.log`). The corrected EXE was installed and reached the original title screen. Automated input checks stopped when the desktop tool detected concurrent user input; the game was left running. Gameplay completion and exact crash reproduction remain pending.

**Owner verification:** after testing the installed build, the owner confirmed "fixed, tested myself". The palette defect is fixed with synthetic regression coverage and owner confirmation. Preserve the original diagnostic and its causal uncertainty; this report does not specify full-level completion, save/reload, or full-game compatibility.

## P3-001: Black game frames — native frame boundary corrected

The wrist-strap and sideways-controller notices displayed, but save confirmation could turn black. Per-draw GPU pixel readback showed colors after drawing and black after the display copy. Command order established that the game first CopyDisp's the previous frame, then draws the next. Ending the host frame at VIWaitForRetrace cleared the later drawing. Native presentation now occurs when the display-copy command completes. Original drawing and the game state machine are preserved; confirmation and title screens display correctly.

## P3-002: Vertex upload overflow — ranges derived from actual indices

The animation after save creation requested roughly 45 MiB of data for an 8 MiB staging buffer. GXSetArray provides a base address and stride, not a length; treating all remaining RAM as the array was incorrect. The bridge now scans each draw's indices and computes the required byte range per array. Indexed XF accesses are likewise checked against index, stride, and word count. Growth is bounded and geometric; the position-index skip sentinel does not expand the read range. Synthetic tests cover ranges, address conversion, and skipping. The save animation and title were also exercised successfully.

## P3-003: NAND and standalone local launch

The first missing ioctl, 7, deleted temporary entries; it was not GetAttr. Native NAND services now enforce canonical directory boundaries and move replaced/deleted nodes into a private backup directory. Initial creation of FLF.bin, banner.bin, and photo slots, followed by loading them on the next launch, was directly verified. The standalone GUI EXE locates extracted local data and initializes the RTC automatically. It launched with no arguments and only the system PATH. Explicit-allowlist assembly into private/game is a local workflow, not a public game distribution package.

## P3-004: Input and idle behavior

Short keyboard presses can finish between WPAD samples. Event holding spans a short sampling window; sideways direction mapping includes WASD and numpad aliases. Desktop automation reported arrow-key input as numpad scancodes on this machine, and the aliases were verified in the actual menu. Original WPAD normally calls __VIResetRFIdle to report activity; native WPAD now queues that original call too. Standard idle-dimming requests reduce brightness in native presentation. Disable requests include both 0 and 0x12, which retains the strength parameter; the latter was checked against original VI calls and added to tests. Unknown parameters still fail. The dimming curve has not yet been compared against hardware.

## P3-005: Exit after a window click — PCM backpressure

The owner reported an exit after clicking the picture. The old log's explicit stop was `Native PCM output backlog exceeds two seconds`, raised by the audio output guard. This was not an established mouse-handler out-of-bounds fault. The old process had already displayed approximately 438,960 frames. The log did not record the preceding queue trend, so it could not establish whether the cause was a click, a window stall, device state, or long-term clock drift. The old log and build manifest are preserved in `private/reports/crash-20261114-pcm-backlog/`.

**Fix:** before another block would exceed the SDL PCM queue's 10,240-byte high-water mark, wait until consumption reduces it to at most 5,120 bytes. These thresholds correspond to 80/40 ms at 32 kHz stereo S16. Continue collecting window events while waiting, and exclude the backpressure wait from the unified guest timebase so it does not create additional AI/VI/alarm debt. Preserve the guest oscillator rate, event ordering, and every PCM block; do not clear the queue or drop audio. Resume a paused valid logical device. A lost binding or two consecutive seconds without consumption progress still produces an explicit diagnostic rather than fake success.

New synthetic clock tests cover nested pauses, resumption, and decrementer deadlines. The SDL dummy output integration test submits three seconds of PCM as fast as possible, checks complete submission, queue bounds, and exclusion of wait time from the guest clock, then covers paused-device recovery, 32/48 kHz format changes, and device-unbinding errors. It uses the dummy backend without modifying the user's physical audio device. The first CTest attempt could not start because the test EXE lacked SDL3.dll; copying that DLL to the target directory resolved the setup issue. Full results: 12 CTest cases and 19 Python tests passed. Logs: `private/reports/pcm-backpressure-ctest.log` and `private/reports/pcm-backpressure-python.log`.

The updated standalone EXE launched to the title with a minimal PATH. Clicking the picture, maximizing/restoring, X to enter single-player file selection, and Z to return all worked. Leaving the Windows title-bar system menu open for an extended period, then clicking the game to close it, induced real PCM backpressure. The log recorded queue 10236 -> 5116 bytes with waits around 31–34 ms and no STOP. After the backlog drained, S selected TWO PLAYERS, confirming recovered interaction. The captured run is `private/reports/pcm-backpressure-live.log`. This establishes modal window stalls as a reproducible backlog source, but does not prove the owner's original click had exactly the same cause.

**Remaining limitation:** a long system-menu stall can accumulate device-time debt before recovery, leaving the picture waiting for audio to catch up. All events/audio are retained; no frame dropping or queue clearing hides the delay. Explicit host modal-window pause boundaries remain future work. This experiment does not replace long-run audio/video synchronization or gameplay validation.

## Current validation boundary

16 CTest cases and 19 Python tests passed after the inline checked-RAM work. The palette checkpoint had 13 CTest cases; the historical PCM/pre-commit baseline was 12. Hardware D3D12 pixel readback also passed. Earlier input, real poisoned-BSS, native title/menu/save creation, and standalone launch checks remain separate evidence. The owner confirms improved speed, but heavy-scene performance and fixed-input equivalence remain open. Full gameplay, complete hardware/floating-point semantics, synchronous EFB readback, and full-game playability are not established.

## P2-010: Native scheduling and GX/WPAD integration (2026-11-14)

This section records the integration checkpoint; its test count predates the later P3 checks.

- VIWaitForRetrace reached original SelectThread through OSSleepThread and spun with no runnable thread. The AOT function had no dispatch boundary at which device events could be delivered. Skipping VI waits was not a valid fix. SelectThread is now an explicit native platform service that uses the same guest ready queue and polls modeled VI/IOS events while idle. New threads use Windows fibers to retain C++ local state; unknown OSLoadContext paths still fail.
- External IRQ restoration checks GPRs, CR, LR, CTR, XER, FPSCR, FPRs, GQRs, and SRR. Original OSSetCurrentContext may set the FP-enable bit for a known FPU owner; other execution redirection is rejected.
- The local bridge converts guest GX addresses and sends commands to the Aurora FIFO worker, modeling CP/PE/PI state and completion notifications. A synthetic triangle was first verified here; later actual title evidence is recorded in P3 above.
- The first synthetic triangle was black because the test omitted native GXInit, leaving BP register IDs in Aurora's producer shadow uninitialized. Adding initialization and GXFlush fixed the test. The game still executes its original GXInit.
- Copying GXRenderModeObj used unaligned integer accesses. The memory layer had incorrectly rejected every unaligned scalar access. RAM integer halfword/word accesses are now supported, while MMIO and scalar floating-point alignment checks remain. Synthetic tests cover bounds and cross-page byte order. The reference is the alignment exception definition in the [IBM Gekko manual](https://datasheets.chipdb.org/IBM/PowerPC/Gekko/gekko_user_manual.pdf). Instruction-level lmw/stmw/atomic alignment still needs a separate audit.
- The PC runtime does not continue emulating the Wii Bluetooth USB startup stack. WPAD connects to SDL/Aurora at an explicit service boundary while preserving original KPAD processing. Guest callbacks/buffers remain distinct from host pointers. Unsupported extensions and remote-speaker playback fail explicitly.
- At this checkpoint, 19 Python tests and six CTest cases passed; synthetic GPU drawing passed separately. Real dumps, generated programs, logs, and screenshots remained private.

## Historical early-P2 records

The following entries preserve what was implemented, missing, or observed at each early checkpoint. References to unmapped MEM2/MMIO, unfinished OSInit, or a current stop describe that historical experiment. Later progress is in P2-010 and the current handoff; do not treat the old statements as present limitations without checking current evidence.

## P2-001: L2CR blocked cache initialization — boot subset resolved

- Failure site: `__OSCacheInit` accessed SPR 1017, which the initial probe did not implement.
- Reference: [Interpreter_SystemRegisters.cpp](https://github.com/dolphin-emu/dolphin/blob/e36cd326c0af3179bf75a8deb5e2ecf92ab78b72/Source/Core/Core/PowerPC/Interpreter/Interpreter_SystemRegisters.cpp) at Dolphin commit `e36cd326c0af3179bf75a8deb5e2ecf92ab78b72`. mfspr reads SPR state, mtspr first stores the written value, and the L2CR branch performs no asynchronous cache work.
- Implementation: independent L2CR state accepts the enable/invalidate bits needed by startup. At this checkpoint, probe memory was coherent, DMA was absent, and code modification was prohibited, so invalidation had no pending cycles and L2IP stayed zero. Unknown bits failed explicitly; reset cleared state. **This is a limited state model consistent with that Dolphin path, not full hardware cache emulation.**
- Validation: reset, write/read retention, invalidate, enable, and unknown-bit tests passed. Original `__OSCacheInit`, `OSSetErrorHandler`, and `__init_hardware` returned.

## P2-002: All-zero CPU startup state — sourced baseline, dynamic comparison pending

- [SetupMSR/SetupHID in Boot_BS2Emu.cpp](https://github.com/dolphin-emu/dolphin/blob/e36cd326c0af3179bf75a8deb5e2ecf92ab78b72/Source/Core/Core/Boot/Boot_BS2Emu.cpp) established MSR=`0x2032`, HID0=`0x0011C664`, HID1=`0x80000000`, HID2=`0xE0000000`.
- Original PS/FPR/cache initialization continued rather than being skipped. An actual DOL-entry register snapshot for this disc and the user's Dolphin version had not been captured. The result was therefore a source-model baseline, not a dynamic equivalence result.

## P2-003: Low memory treated as all-zero or wholly unreadable — byte-level tracking

- Original `__start` stopped at `0x800000F4`. SetupWiiMemory in the same Dolphin source established its explicit value `0x8179B500`; other unverified fields were not automatically permitted.
- Host loads, explicit initialization, and guest writes mark individual bytes known. Physical/cached/uncached aliases share that state. Writing a halfword does not make the entire word readable. Reset clears known-state tracking.
- High memory at `0x8179B500` was still zero-initialized probe memory without a captured apploader snapshot. That assumption required comparison against actual startup.
- Validation: rejection of partially initialized low-memory reads and alias consistency passed. Original `__start` reached `OSInit`.

## P2-004: Time Base and absolute system time — historical stop

- The single-thread probe implemented a monotonic timebase: elapsed `steady_clock` duration -> 60,750,000 ticks/s. This follows the 729 MHz Wii core frequency in pinned Dolphin [SystemTimers.cpp](https://github.com/dolphin-emu/dolphin/blob/e36cd326c0af3179bf75a8deb5e2ecf92ab78b72/Source/Core/Core/HW/SystemTimers.cpp) and the divisor 12 in [SystemTimers.h](https://github.com/dolphin-emu/dolphin/blob/e36cd326c0af3179bf75a8deb5e2ecf92ab78b72/Source/Core/Core/HW/SystemTimers.h).
- The epoch was configurable and set to zero in that probe. High/low 32-bit reads came from one 64-bit clock; original OSGetTime's high/low/high retry logic ran normally.
- Whole-second, half-second, long-duration integer conversion, explicit high-32-bit epoch, and monotonicity checks passed. Original `OSGetTime` returned.
- Historical stop: `__OSGetSystemTime` read `0x800030DC` before a contract existed for it; its paired high word is `0x800030D8`. Combining Dolphin's source initializers directly with the probe's zero epoch could produce incorrect absolute time.
- The next requirement was a consistent Wii RTC epoch, local-time offset, boot-time offset, and TB relationship, with an injectable clock and actual startup-field capture. Probe elapsed time must not be mistaken for a calendar, nor the `0xFFFFFFFF` high word blindly copied just to unblock execution.
- Alarms, decrementer behavior, scheduling, pause/resume, and cross-thread clock synchronization were not yet implemented at this checkpoint. The timebase only validated early read paths. P2-006 and later work superseded this stop.

## P2-005: Optimized ABI and checked memory — full execution fallback retained

- Typed/state-free call optimization was disabled; original full-CpuContext functions continued executing.
- A null ResolveRangeHost result means direct mapping is unavailable. A false bulk-write optimization result falls back to the generated code's individual checked writes; it is not an empty successful write stub.
- Unregistered targets, unknown SPRs, then-unmapped MMIO/MEM2, and illegal code writes failed explicitly.

### Other unresolved work at that historical checkpoint

- Complete indirect-call coverage, the `0x60` debug vector, exception returns, and full PPC floating-point/quantization semantics.
- Post-OS-initialization heaps, files, threads, audio, input, and GX services.
- Some upstream cache/sync instructions generated as no-ops; the controlled single-thread probe did not validate threading or DMA.
- P2 acceptance had not yet passed: static constructors and the game main loop were incomplete.

That run linked 19 game functions. Normal and poisoned-BSS experiments both reached the P2-004 stop. Two CTest cases, 13 Python tests, and wrong-DOL rejection passed; BSS clearing and initialized-data preservation continued to pass. Generated code, EXEs, and logs remained in private/.

## P2-006: RTC and system-time offset — startup contract validated

- Pinned Dolphin `EXI_DeviceIPL.h/.cpp` and `Common/Timer.cpp` established a 2000-01-01 TB epoch and an RTC input expressed as Unix seconds representing local civil time. `SystemTimers.cpp` multiplies the epoch difference by 60,750,000.
- The diagnostic probe gained an explicit RTC argument, independent of the machine's current timezone, wall clock, or conversation date. The build fixture is `946771200` (2000-01-02 local civil time). **This is synthetic test time, not the current date.** Inputs before 2000 or outside the 32-bit RTC range are rejected.
- `0x30D8/0x30DC` use Dolphin SetupWiiMemory's paired `0xFFFFFFFF/0` values, interpreted as a TB offset modulo 2^64, not independent fields that can arbitrarily be zeroed.
- Original `__OSGetSystemTime` returned. The probe checked whether r3/r4 fell between TB+offset samples taken before/after the call; both normal and poisoned runs passed. Synthetic tests cover integer epoch, range, and timebase conversion.
- Scheduling/pause/alarm semantics and actual Dolphin traces remained follow-up work at this stage. A fixed RTC does not make host instruction duration deterministic; `steady_clock` still advances during execution.

## P2-007: IOS version and memory arenas — metadata established before MEM2 support

- The user's local `tmd.bin` requested IOS title ID `0x0000000100000038`, or IOS56. The build script checks both the TMD signature format and that ID without reading/copying IOS code or keys.
- The IOS56 rows in pinned Dolphin `IOS/IOS.cpp` and `IOS/VersionInfo.cpp` established MEM1/MEM2 arenas and IPC buffer endpoints. SetupWiiMemory supplied legacy arena and exec-params fields.
- This step only established guest-readable boot metadata. Actual MEM2 accesses were still rejected at that checkpoint; an address field did not imply an implemented memory region. Post-apploader arena values still required dynamic comparison. MEM2 support was added later.
- MMCR0/MMCR1 accepted only the disabled value zero. Disabled PMC state supported writes/reads; enabling event counting failed. HID4 accepted only the checked Wii startup mode `0x83900000`. Unknown-mode rejection and reset were tested.

## P2-008: Self-modifying exception templates — data-template handling validated

- Actual stop: `OSExceptionInit` wrote text address `0x80662838`.
- key's symbol table identifies it as `__OSEVSetNumber`. `src/revolution/OS/OS.c` shows the function updating the exception number and `__DBVECTOR`, copying the template to 15 exception locations, then restoring the numbered instruction.
- Only the two 32-bit template words at `0x80662828` and `0x80662838` were declared data uses. Writes were permitted only within a controlled `OSExceptionInit` call scope. Other code writes, widths, or out-of-scope writes remained blocked, and exceptions automatically revoked permission. Synthetic tests cover these boundaries.
- The template functions themselves cannot be dispatched, and the vectors copied into low memory have no executable registrations. Modified templates are therefore not mistaken for valid AOT functions; no interpreter/JIT was introduced.
- The original installation returned. Each word of all 15 vectors matched the current template plus its exception number; `__OSInitSystemCall` returned next. **At this checkpoint only installation was validated; actual exception, interrupt, rfi, and syscall handling was not yet implemented.** Later native IRQ support does not establish every exception path.
- This does not allow arbitrary self-modifying code. Other modifications require their own provenance and native-behavior contract.

## P2-009: Interrupt controller/MMIO — historical stop

- Original `__OSInitAlarm` and `OSRegisterShutdownFunction` returned. That established queues/callbacks, not proof that alarm callbacks could fire.
- `__OSInterruptInit` accessed `0xCC003004`, which the memory layer explicitly rejected at that stage. Follow-up work needed PI register/mask semantics, individual register state, and aggregation of device pending sources, rather than blanket zero/no-op MMIO.
- Exception-vector installation also needed to connect to native interrupt dispatch before OSInit or main-loop completion could be claimed. P2-010 records subsequent integration.

### Verification at that checkpoint

Both normal and poisoned-BSS startup stopped at `0xCC003004`. Original SystemTime return-range checks, all 15 exception-vector copies, 949,240 bytes of BSS clearing, and preservation of 1,086,336 initialized bytes passed. Two CTest cases, 13 Python tests, and wrong-DOL rejection passed. Logs remained in `private/reports/`. These historical results do not replace the current validation baseline above.
