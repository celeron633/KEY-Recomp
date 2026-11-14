# Native gameplay profiling

Use separate captures for frame throughput and CPU hotspots. Do not rebuild, run Dolphin, or leave a sampler running during a comparison window. Keep all captures, symbol tables, generated disassembly, executable backups, and screenshots under `private/`.

## Capture protocol

1. Validate RK5E01 Rev 0 and the supported DOL hash before using game-specific symbols. Record the running process ID/path, executable SHA-256, `private/game/local-build.json`, host CPU/GPU, and settings. Preserve the application log before a new launch overwrites it.
2. Establish the exact level, location, starting save, collectible/enemy state, and input sequence with the owner. Keep the game active and unpaused. A stationary camera does not freeze moving objects. Record an owner report separately from an independently observed scene or repeatable replay.
3. Record approximately sixty seconds without a profiler. Preserve the log at both ends. Discard the first `TIMING wall=` interval crossing the start marker, then take every complete interval before the end marker. Average rates weighted by each interval's `wall`; record min/max as five-second averages, not individual-frame percentiles. Compare `copies_fps` with `game_root_hz`, `guest_ratio`, VI rate, PCM rate, and waits.
4. Measure per-thread CPU-time deltas over that window. One CPU second per wall second means one fully occupied logical core, regardless of the machine's aggregate CPU percentage. Include the query overhead in the uncertainty. Do not choose the hot thread solely from a lifetime total without checking recent deltas.
5. Sample the hot thread in a separate window, with no compilation. On this prepared workspace, the local helpers `private/analysis/timing/cpu_sampler.exe` and `stack_sampler.exe` attach to a PID and record module bases plus instruction pointers or stacks. Their C++ sources are beside them. They are local investigation tools, not a portable packaged profiler. Recheck the PID before every invocation; do not use a PID copied from an old handoff.
6. Resolve native addresses against symbols from the exact running executable, accounting for its PE image base and ASLR module base. Use `private/profiles/RK5E01.map` only after input validation. Inspect both leaf/self samples and callers: a low-level memory or system-library function alone does not identify the subsystem responsible.
7. Stop sampling before repeating throughput measurement. Retain failed runs and scene changes. Report a candidate improvement only against the same scene/input/build configuration, and keep synthetic benchmark ratios separate from gameplay ratios.

Example commands for the existing local helpers, after the validation and scene setup above:

```powershell
$gameProcess = Get-Process KEY-Recomp -ErrorAction Stop
if (@($gameProcess).Count -ne 1) { throw 'Select exactly one game process' }
$captureDir = Join-Path 'private/reports' ('cpu-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory $captureDir -ErrorAction Stop | Out-Null
Copy-Item private/game/local-build.json "$captureDir/local-build.json"
Copy-Item private/reports/native-app.log "$captureDir/before.log"
& .tools/llvm-mingw-20261104-ucrt-x86_64/bin/llvm-nm.exe -n -C $gameProcess.Path |
    Set-Content "$captureDir/native-symbols.txt"
& private/analysis/timing/cpu_sampler.exe $gameProcess.Id 2400 |
    Set-Content "$captureDir/cpu-samples.txt"
& private/analysis/timing/stack_sampler.exe $gameProcess.Id |
    Set-Content "$captureDir/stacks.txt"
Copy-Item private/reports/native-app.log "$captureDir/after.log"
```

Check command exit codes and sample counts before analyzing. These helpers suspend/resume a thread and therefore perturb its execution. They select the busiest lifetime thread; verify that it is also currently hot using step 4. `Sleep(1)` does not guarantee a 1 kHz sampling rate. Stack walking is limited to twenty callers. Nearest-preceding symbol lookup can misattribute padding or missing symbols. This is a wall-time hotspot estimate, not an ETW CPU-cycle profile, and cannot establish GPU utilization or p95/p99 frame times. Inclusive recursive frames must be deduplicated per stack, and inclusive subsystem percentages can overlap.

`boundary_ms`, `fifo_sync_ms`, `idle_ms`, and `audio_wait_ms` are totals over each logged interval. FIFO synchronization is included in boundary time. Do not interpret either field as per-frame GPU rendering time or add the nested values together.

## Underwater investigation: 2026-11-14

The initial controller-build profile measured **37.02 FPS** across eleven complete intervals totaling **55.106 seconds**, with approximately one logical core occupied by the guest thread. Evidence and limitations are in [CURRENT-RUN.md](CURRENT-RUN.md), with private captures under `private/reports/underwater-20261114/`. That initial investigation made no runtime change; the subsequent dispatch implementation and comparison are recorded below.

| Experiment | Evidence and implementation area | Required validation |
| --- | --- | --- |
| Reduce per-call dispatch/context work | `runtime/src/probe.cpp`: dispatch self 4.38%, context scope self 3.83%, lookup self 1.33%; separate stack capture finds clock sampling in 7.0% of stacks | Preserve guest event delivery and fresh TB/MMIO reads; check nested contexts, fibers, exceptions, FPSCR/NI changes, and host FP restoration before eliminating context operations |
| Use checked fast scalar access in native AX adapters | `runtime/include/key/ax_adapter.h` still calls general memory access; AX appears in 7.2% of stacks and explains most general memory leaf samples | Preserve endian behavior, alias/bounds/alignment checks, code protection, GPU hazards, and dirty tracking; compare synthetic AX output exactly in reference/fast modes |
| Reduce FIFO publish/wakeup frequency | `renderer/backend.cpp` calls publish after each feed; FIFO feed appears in 7.6% of stacks, including worker notification | Measure actual notifications and completed packets; preserve visibility, token/draw-done ordering, display-copy boundaries, buffer pressure, and explicit drains; test worker progress and pixel readback |

The percentages in different rows use different sample sets or inclusive scopes and must not be summed into a predicted speedup. Interrupt delivery appears in 14.0% of the stack samples but includes actual guest callbacks and AX work; it is not a removable overhead budget. Scene task/drawing callers also contain many small translated functions, so optimizing only the single highest leaf function is unlikely to solve this case.

At the measured rate, average frame time is approximately **27.01 ms**. Reaching 60 Hz requires approximately **10.35 ms less per frame**, or a **38.3% reduction**, under an equivalent workload. Validate each independent change with the relevant synthetic checks, `python -m unittest discover -s tests -v`, and `ctest --test-dir private/build/graphics --output-on-failure`, followed by the scene comparison. Do not change simulation timing, skip logic, weaken memory checks, or discard audio/interrupt work to reach the target.

## First dispatch/context implementation

The installed follow-up in `key/dispatch_context.h`, `key/dispatch_depth.h`, and `probe.cpp` inlines context boundaries, reuses the entry host-control read, caches per-context depth bookkeeping, and inlines sparse lookup. The reference FP boundary behavior and event polling remain intact. It passed 21 CTest cases, 19 Python tests, and incorrect-DOL rejection.

A fresh before window measured **36.26 FPS** over 55.151 seconds; the owner-ready candidate window measured **38.28 FPS** over 55.097 seconds, an observed throughput difference of **+5.55%**. Average frame time changed from **27.57 ms to 26.12 ms**. Both used eleven complete intervals after discarding the start-straddling interval, with 1.0000x guest time and no audio/scheduler waits. Screenshots show the same underwater region but different object/collectible states (93 beads before, 25 after), and the owner described the position as approximate. This is not an identical replay or an isolated causal speedup. Raw evidence, the previous EXE, verified save backup, and reproducible `compare.py` are under `private/reports/perf-context-20261114/`. The first attempted before window with a long audio-backpressure stall is retained separately. Stable 60 Hz remains unresolved.
