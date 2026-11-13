# P2 boot probe: Historical early initialization checkpoint

> Historical record from 2026-11-13. This document describes an early diagnostic probe, not the current executable. Native main-loop and title/menu startup were subsequently implemented; the owner has also reported level entry. See [STATUS.md](STATUS.md) and [CURRENT-RUN.md](CURRENT-RUN.md) for current behavior.

The initial probe stopped at SPR 1017. Later early-P2 work validated original SystemTime, exception/syscall vector installation, and alarm initialization, then stopped at MMIO `0xCC003004` in `__OSInterruptInit`. Those later checkpoints are recorded in [ISSUES.md](ISSUES.md), P2-006 through P2-009. Both stop points have since been superseded. The design and experiment results below are retained as historical evidence.

At this checkpoint, P2 was **in progress**. A standalone native Windows x64 program linked 14 locally generated game functions. It did not launch Dolphin and contained no CPU interpreter or JIT. It was a diagnostic tool, not a playable game.

## Evidence from the initial probe

Both normal initialization and the `--poison-bss` experiment executed and returned from:

- `__init_registers`: guest SP=`0x809E4278`; r2/r13 matched P1 analysis.
- `__init_data`: executed translations of original `memset`, `__fill_mem`, and `__my_flush_cache`.
- `__OSPSInit`: initialized GQR/HID register state.
- `__OSFPRInit`: initialized floating-point registers and FPSCR through limited startup helpers.

Execution then entered `__OSCacheInit` (`0x80664000`) and stopped explicitly at `PPC_ReadSpr(1017)`. SPR 1017 was the unmodeled L2CR boundary at that time. No return value was fabricated and `__init_hardware` was not skipped. Exit code 2 represented a diagnostic stop, not successful game startup.

The poisoned-BSS experiment first filled the DOL BSS envelope with `0xA5`, then loaded initialized sections. After original initialization, **949,240 bytes of actual BSS were zero** and **1,086,336 bytes of initialized data were preserved byte for byte**. The envelope also contained **64 bytes of alignment gaps** after subtracting initialized sections. It would be incorrect to require the original initialization table to clear those gaps. An early coarse check made that assumption; it was replaced with validation against `_bss_init_info` ranges.

## Isolation design at this checkpoint

- `runtime/include/guest_abi/guest_abi.h` supplied an independent guest ABI entry point, referencing only upstream ISA configuration, CPU context, integer, and CR primitives. It did not include upstream `dispatch.h`, `memory.h`, `system_bridge`, or racing-game HLE.
- Optional typed/state-free call optimization was disabled in favor of full `CpuContext` dispatch. Unregistered targets threw diagnostic exceptions. `0x60` had a dedicated unimplemented debug-vector diagnostic, although execution had not reached it.
- Only **24 MiB MEM1** and its physical/cached/uncached aliases were modeled, with uniform big-endian access. MEM2 and MMIO were not yet mapped; the upstream 128 MiB NDEV configuration was not copied. These limitations describe the early probe, not the present runtime.
- Low-memory boot-info contracts were not yet established. Guest reads from the first `0x4000` bytes of MEM1 were rejected so zero-filled memory could not masquerade as correct system information. Later work needed original startup traces to establish fields.
- Bounds, overflow, and alignment were checked. Guest writes to code sections and their aliases were prohibited; host loading was distinguished from guest writes.
- ELF/DOL bytes were not embedded in publishable source. The probe EXE remained in private/ because it contained locally generated game functions and could not be published.
- Both the EXE and Python build entry point checked the DOL SHA-256. A synthetic DOL of the correct size but incorrect contents was rejected.

## Floating-point subset at this checkpoint

Only the startup float-pair load with GQR=0, W=0, I=0 was implemented, with checks for byte order and the ordering of both float lanes. NaNs and other quantization modes caused explicit stops. The FPSCR helper accepted only a full-field clear and updated both context and x64 MXCSR; other modes stopped. This was not a complete floating-point or paired-single implementation.

Upstream emitted some cache/sync instructions as no-ops. At this stage they were used only in a controlled, single-threaded, device-free boot experiment with code modification prohibited. That did not validate threading, DMA, cache coherence, or self-modifying code. HID startup values had not yet been compared with a Dolphin startup trace.

## Historical build and validation

The commands used for the early probe were:

```powershell
# Use pinned local tools; Ninja 1.13.2 was added and SHA-256-verified.
./tools/bootstrap.ps1 -WithClang
python tools/build_probe.py
python -m unittest discover -s tests -v
```

At that time, `build_probe.py` checked DOL and selected generated-source metadata hashes, generated a private registry, invoked CMake/Ninja/Clang, and ran two CTest cases, wrong-DOL rejection, normal startup, and poisoned-BSS experiments. It specifically required the expected SPR 1017 stop rather than accepting any crash. Each run had a 15-second timeout, with separate dispatch call-count and depth limits.

**Current command behavior differs:** the default build script now builds and validates without requiring that old stop point. Use `python tools/build_probe.py --graphics` for the prepared graphics workspace, and `--explore` only for a budgeted diagnostic boot. Direct diagnostic invocation requires an explicit RTC argument; the installed GUI EXE needs no arguments. See [CURRENT-RUN.md](CURRENT-RUN.md) for the current workflow.

Historical outputs:

- `private/build/probe/key-boot-probe.exe`: local probe, not distributable.
- `private/reports/boot-probe.log`: normal call/return/stop trace.
- `private/reports/boot-probe-poison.log`: nonzero-BSS validation trace.
- `private/build/probe/Testing/`: CTest results.

The two CTest cases covered endian access, aliases, bounds, alignment, MMIO, low memory, code write protection/reset, floating-point lane ordering, startup FPSCR, and diagnostics for unsupported modes/SPRs. All 13 Python tests also passed. Full-game linking, dynamic Dolphin comparison, and picture testing had not yet been performed at that checkpoint. Later runs may reuse these local output paths, so archived logs and their build identity must be used when reproducing historical claims.

## Follow-up work identified at the time

1. Check L2CR reset, invalidate, and cache-enable behavior against Dolphin/public PPC implementations, then implement a testable SPR state model rather than returning zero.
2. After cache initialization, establish low-memory boot contracts one field at a time and continue through OS initialization, constructors, and the main loop.
3. Collect original startup traces to compare register/memory values rather than only whether functions return.
4. Build MEM2, thread/interrupt, time, DVD, and exception services before treating rendering or distribution as complete.

Those items describe the historical handoff; current outstanding work is in CURRENT-RUN.md. P2's acceptance gate remained correct static initialization and main-loop entry, while this report established only reproducible early boot execution.
