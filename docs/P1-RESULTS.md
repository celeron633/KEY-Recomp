# P1: Static translation feasibility results

> Historical checkpoint: 2026-11-13. The limitations and counts below describe the P1 experiment. See [STATUS.md](STATUS.md) and [CURRENT-RUN.md](CURRENT-RUN.md) for the later native menu build and reported level entry.

The P1 conclusion was to **retain WiiCompiled as the translator candidate and proceed to runtime isolation/adaptation in P2; the game could not run at this stage.** This phase validated code generation from the actual DOL and native execution of a synthetic program. It did not validate full-game instruction semantics or playability.

## Tools and reproduction

.NET SDK 8.0.428 and LLVM-MinGW 20261104 (Clang 23.1.5) were installed locally without modifying the system PATH. Package URLs/hashes are in `toolchain.lock.json`; downloads were verified against Microsoft's release metadata SHA-512 and the GitHub release asset SHA-256 respectively. Downloads/installations remain in `.tools/` and are excluded from source distribution.

```powershell
# Prepare a new environment; first check out references at references.lock.json revisions.
./tools/bootstrap.ps1 -WithClang

# Run from the project root. Default tests do not require game data.
./.tools/dotnet/dotnet.exe test references/WiiCompiled/translator/Translator.sln -c Release --logger "trx;LogFileName=upstream.trx" --results-directory private/reports/upstream

# Synthetic program validation; does not read the dump.
./tools/test_synthetic.ps1

# After the README extraction steps, create an independent profile and run experiments.
python tools/make_profile.py
python tools/probe_translation.py
python tools/audit_translation.py
```

The experiment scripts reject existing `private/generated/{leaf,entry,mapped}` directories to prevent old output from contaminating a new run. Preserve earlier experiments in another private location before rerunning. Profile generation checks reference HEADs against pinned commits, validates the DOL hash, and strictly checks function addresses/sizes. It interprets only known constant-building instructions in the startup function and stops on unknown instructions.

## Verified evidence at this checkpoint

| Item | Observed result | Limitation |
| --- | --- | --- |
| Default upstream tests | 778 passed, 0 reported failures | Some tests return early if external oracles are unavailable; not 778 independent semantic comparisons |
| Project Python tests | 13 passed | DOL/symbol/startup-constant parsing, not the complete PPC ISA |
| Synthetic AOT -> Clang -> EXE | 10,025 addition inputs, including overflow boundaries, plus preservation of other GPRs | Integer addition and minimal CpuContext ABI only; no floating-point/paired-single/memory/thread coverage |
| Game leaf function | One function generated successfully | The game function was not executed |
| Recursive entry-only traversal | 3,029 functions, about 4.9 seconds | Many indirect calls remain unreachable by this traversal |
| Symbol-assisted traversal | 32,737 functions, about 40.8 seconds; 3,037 found through call-graph traversal and 29,700 through symbol assistance | Code generation statistics, not execution coverage |
| Symbol coverage audit | All 32,697 input symbol entries had output; 40 additional entries found | Additional entries still need runtime/control-flow validation |
| Translator quality metadata | unsupported=0, invalid SSA=0 | Does not establish indirect-branch or platform-service support |
| Clang syntax sampling | Generated `__start`, `main`, and `GXInit` files passed | All 32,737 functions had not yet been compiled/linked at P1 |

Startup constants: entry `0x80004050`; `__init_registers` at `0x80004210`; r13=`0x808E73E0`, r2=`0x808EE8E0`. These came from validated DOL startup logic, not MKW addresses. The analysis image used 24 MiB MEM1; that was not a claim about the final runtime's entire memory configuration.

## Gaps carried into P2

1. **Unresolved direct target `0x00000060`:** called by `__OSDBJump` (`0x80662790`). Upstream reported a skipped map entry, but this address was not in the input map; it was a call target discovered during symbol-assisted processing. Trace the low-memory debug service, model it explicitly or fail diagnostically. Do not silently return or interpret translator exit 0 as completeness.
2. **Unverified indirect control flow:** 7,348 generated functions contained indirect dispatch, with 19,112 textual dispatch call sites. Inlining affects this count; it is not a count of distinct guest instructions. A registry must validate function pointers, callbacks, vtables, thread switches, and fail-fast handling of unresolved targets.
3. **Platform ABI not integrated:** the experiment deliberately used empty native registration and ABI directories, without MKW HLE addresses. OS, DVD, GX, audio, and input were still guest-code analysis/runtime requirements, not completed replacements.
4. **Memory configuration cannot be copied wholesale:** upstream `guest_abi/memory.h` fixed MEM2 at 128 MiB, and `memory.cpp` explicitly described MKW's NDEV extended heap. P2 needed a Kirby-specific startup memory contract distinguishing retail/default from debug expansion.
5. **Upstream game-specific hooks:** `ApplyRuntimeCallOptions` in `guest_abi/dispatch.h`, for example, contained MKW scene-rendering address checks. Isolate these so matching numerical addresses cannot accidentally trigger them in Kirby.
6. **BSS loading order checked:** `ProgramImageBuilder.cs` clears BSS before copying initialized sections. `DataSectionGenerator.cs` assumes host allocation starts zeroed and then copies data. The first behavior fits this DOL; the second must be guaranteed by P2 memory implementation, including reset/reload tests.

Do not eliminate these gaps with fake-success stubs or unsupported upstream edits. At P1, the evidence supported the translator approach only; reuse of the complete runtime still required P2 review.

## Input integrity

Full Dolphin verification reported three low-severity categories: a missing update partition, WBFS not preserving original disc size, and anomalies in 18,256 unused DATA blocks. This was not a zero-issue verification of a complete original disc image. The DOL matched the reference hash. A SHA-256 inventory of the 2,815 extracted files established a local reproduction baseline, not a third-party-certified original-asset hash list.

## Private evidence

`private/reports/` contains `upstream-test.log`, `upstream/upstream.trx`, `profile.json`, `translation-runs.json`, per-mode logs/metadata, `translation-audit.json`, `synthetic-native.log`, `syntax-sample.log`, `disc-verify.log`, and `files-sha256.json`. Game-generated C++ and data initializers remain in `private/generated/`; the synthetic program is in `private/synthetic/`. None of these private directories belongs in public commits or releases.
