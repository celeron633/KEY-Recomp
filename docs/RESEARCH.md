# Approach research

Initial research: 2026-11-13. Adoption status updated: 2026-11-14.

Use each project's own source/documentation as evidence. Aggregator sites may help discover projects, but their labels such as "playable" or "native" are not validation results. The external-project observations below retain their research date; they are not a claim about the latest upstream branch state.

| Project | Primary sources and initial observations | Role in KEY-Recomp |
| --- | --- | --- |
| Swiftshine/key | [Repository](https://github.com/Swiftshine/key): matching decompilation targeting RK5E01 Rev 0, CC0-1.0; configuration provides symbols, sections, and DOL SHA-1 | Symbol/type reference for game code and SDK boundaries. It is not a ready-made PC port, and this project does not wait for complete decompilation. |
| WiiCompiled | [Repository](https://github.com/patchzyy/Wiicompiled), [translator documentation](https://github.com/patchzyy/Wiicompiled/blob/main/translator/README.md): PPC -> IR/SSA -> C++, generic DOL profile, GPL-3.0 | Selected translator candidate, tested in isolation. Selected runtime helpers are now used; MKW addresses/patches must not be inherited. |
| Aurora | [Upstream](https://github.com/encounter/aurora); WiiCompiled's pinned submodule comes from [its fork](https://github.com/patchzyy/aurora-wiicompiled), listed as MIT in WiiCompiled notices | Initially a GX/window/modern-GPU backend candidate. The pinned fork is now checked out and used for native GX/D3D12 and host WPAD. Synthetic graphics/input and actual title rendering have been exercised. |
| Nebula | [Project README](https://github.com/keepvaibin/nebula): the 2026-11-13 research recorded release 1.0.2 as local static recompilation and main as moving toward Petari-based native source modules | Reference for user-local build delivery. Distinguish a release architecture from its development branch. Not downloaded or validated for this project. |

Exact adopted revisions and local paths are recorded in [references.lock.json](../references.lock.json). The lock file, not a moving branch link, defines the build reference.

## Decision

Use **AOT static translation + preserved 32-bit guest-address semantics + platform-service HLE**. Game logic executes as compiled host code, while Wii API/hardware compatibility remains necessary. Native execution does not mean the absence of a compatibility layer. Windows x64 is the first target; C++/CMake/Clang and Aurora's Windows backend have progressed from candidates to the exercised local build path.

A complete source port was not chosen as the starting point. key is a work in progress, and its C++ structures, 32-bit pointers, endianness, and SDK dependencies would still require porting. A completed matching decompilation does not automatically produce a PC EXE. Static translation avoids requiring source recovery for every function first, but it still requires indirect-control-flow handling and service adaptation.

The examined WiiCompiled `generic-dol.yml` allowed unsupported instructions by default. This project must not inherit that permissive setting; unsupported instructions must block delivery. Its `generate-data-init` output includes original data initializers, so all translation outputs and linked game programs must remain in private/.

## Boundaries that require validation

1. **Boot and guest memory:** entry, r2/r13, MEM1/MEM2, image addresses, BSS clearing, static constructors, and heaps.
2. **PPC semantics:** CR/XER, floating-point rounding, paired-single, GQRs, atomics, cache/self-modification behavior. Generating C++ is insufficient evidence.
3. **Control flow:** jump tables, virtual calls, function pointers, exceptions/unwinding, and callbacks. Symbols are hints, not an assumed complete inventory.
4. **Wii services:** OS threads/interrupts/clocks, DVD/files, NAND/saves, WPAD/KPAD, and audio. Unimplemented functions must not all return zero.
5. **GX:** TEV, texture formats, display lists, FIFO, EFB copies, depth/alpha blending, and video. High-resolution seams, masks, and outlines require scene-by-scene comparison.
6. **Modules:** the extension scan found no standalone `.rel` files, but archives and module-loader calls still need investigation. This does not prove the absence of dynamic code.

## Licensing and distribution

A reference repository's root license does not establish that every data file may be redistributed. Audit each reused module and its notices. The initial research only checked out references; later work adopted selected WiiCompiled ISA/CPU helpers, PSQ/store conversion code, generic AX algorithms, and Aurora. Current attribution is in [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md). Distribution is designed to be GPL-3.0-compatible, retaining source, license texts, and notices.

Public source may contain tools, configuration, metadata such as addresses/hashes, and independently implemented or appropriately licensed runtime code. A future public Setup EXE will generate the game locally. Locally generated game EXEs, asset packages, and data initializers are not public release contents. Every package must use a per-file allowlist and undergo archive-content/license review; `.gitignore` alone is insufficient.

This report explains the chosen approach. It does not establish that the whole game is playable; see [STATUS.md](STATUS.md) for the current evidence.
