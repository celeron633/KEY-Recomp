# Third-party dependencies

The original KEY-Recomp runtime and tools are licensed under GPL-3.0-only; see LICENSE. Game data and generated game code are excluded from this source distribution.

The runtime includes WiiCompiled's PPC ISA / CPU context headers from the local reference checkout. WiiCompiled is GPL-3.0; upstream notices remain with the source. Source: https://github.com/patchzyy/Wiicompiled at the revision pinned in references.lock.json. The GPL license text is in LICENSE. `runtime/third_party/wiicompiled/ppc_quantized.cpp` is a modified copy of its PSQ fallback implementation (reserved encodings now throw diagnostic exceptions); `runtime/include/key/fp_store.h` adapts its bit-level store conversion. Game-specific dispatch code is not imported. Some referenced floating-point headers carry Dolphin's GPL-2.0-or-later notices, compatible with this project's GPL-3.0 distribution.

Aurora (MIT), from https://github.com/patchzyy/aurora-wiicompiled at the pinned submodule revision, supplies the native GX/D3D12 backend and host WPAD input. Its Dawn, SDL and other dependencies remain under their individual licenses; no public binary distribution has been prepared.

`tools/prepare_ax.py` selects generic AX/DSP mixer algorithms, parameter-block structures and scalar mix kernels from WiiCompiled's `ax_mix.cpp`, `ax_internal.h` and `ax_mix_kernels.h` at the pinned revision. It verifies the checkout contents and produces adapted GPL-3.0 sources in `private/generated/ax`, with source hashes and attribution. The adapter uses checked memory, synchronous execution, a verified AX layout and diagnostic failure on unsupported commands; no Mario Kart addresses or game program are imported. DSP coefficient bytes are copied only from the user's supplied local Dolphin installation into `private/boot` and are excluded from public source/release. The importer contains no coefficient bytes or DSP firmware.

Swiftshine/key (CC0-1.0) provides symbol metadata for local profile generation; see references.lock.json. Its sources remain in the excluded reference checkout.

Selected Dolphin Emulator sources (GPL-2.0-or-later, Dolphin Emulator Project contributors) are used as behavioral references for boot state and timing. Their pinned commit and local file digests are in references.lock.json under source_snapshots. They remain in the excluded references/dolphin-source directory; per-file notices and COPYING are retained there. No Nintendo boot program is copied or distributed.

The pinned .NET SDK, LLVM-MinGW and Ninja are local development tools, not redistributed in a release. Any future redistribution must include their applicable notices and corresponding source obligations separately.
