# KEY-Recomp Working Agreement

## Goal and current approach

- Convert the user's own dump of Kirby's Epic Yarn into a native Windows x64 application, with support for high internal rendering resolutions.
- Only RK5E01 Rev 0 is supported. The approach is local DOL static translation -> C++ -> native code plus a Wii service compatibility layer. It must still meet the technical validation gates in docs/PLAN.md.
- Dolphin is an extraction and behavioral reference tool. Launching Dolphin or wrapping a JIT does not qualify as a native port.
- Swiftshine/key is a matching decompilation reference for symbols and structures, not a ready-made PC engine.

## Data boundaries (mandatory)

- Treat asset/ as read-only. Do not modify, move, or upload the user's dump.
- Store extracted files, DOLs, data initializers, translated code, disassembly, Ghidra projects, screenshots, logs, and locally compiled games in private/. Never commit or publish them.
- Third-party checkouts in references/ are local references only. Do not copy entire checkouts into the project or release packages. Record the URL, exact commit, and license of each new dependency.
- Public source and releases must not contain original Nintendo assets, code bytes, keys, fonts, audio, icons, or programs generated from the game. Generate the game EXE on the user's machine for local use only.
- Public distribution consists of preparation/build tools and the independently implemented runtime. Package from an explicit file allowlist; never recursively package the project. Ignore rules do not replace a content audit.
- Use independently generated synthetic data in tests. Run tests involving real dumps locally only. Do not print keys or game bytes in logs.

## Implementation rules

- Validate the game ID/revision and DOL hash first. Fail closed for other versions; never apply US-version addresses to an unknown build.
- Use 32-bit types for guest addresses. Handle big-endian access, MEM1/MEM2, alignment, and overflow consistently. Never treat a guest pointer as a host pointer.
- Unimplemented instructions, indirect branches, and HLE calls must fail explicitly with diagnostics. Never silently return success.
- Floating point, paired-single, GQRs, exceptions, threads/timing, GX FIFO, and EFB/XFB behavior are correctness requirements. Do not hide problems by skipping game logic.
- Preserve original timing and aspect ratio first. Validate higher resolution and higher frame rates separately; do not change the game tick as a shortcut.
- Do not blindly reuse another game's addresses, patches, memory layout, or boot entry point.
- Check licenses before importing third-party source, and retain attribution and license texts. Design WiiCompiled-derived distribution to be GPL-3.0-compatible; do not relabel dependencies as MIT.

## Workflow and collaboration

- Read README.md, docs/PLAN.md, and docs/STATUS.md first. Use docs/CURRENT-RUN.md for the current handoff, and update evidence, failures, and next steps after each stage.
- Use English for maintained project documentation, contribution/issue descriptions, and new explanatory code comments. Preserve identifiers, commands, hashes, and quoted diagnostic strings exactly. Conversation with the user may use their preferred language.
- Follow CONTRIBUTING.md for contributor onboarding and change handoffs. Mark historical checkpoints explicitly; do not present old stop points or test counts as current state.
- Implement small, independent changes and run the relevant synthetic tests. Distinguish dump validation from successful native execution.
- Separate directly observed results, user reports, and hypotheses. Do not claim a dependency is validated just because its README says so, or call translation/linking/window creation playable gameplay.
- Standard check: `python -m unittest discover -s tests -v`. For an existing graphics build, also run the relevant CTest checks; the full command is `ctest --test-dir private/build/graphics --output-on-failure`.
- Use Conventional Commits: `type(scope): short description` (scope optional). Keep the subject concise. An optional body of approximately 2–3 lines may explain the change, its rationale, or relevant validation; separate it from the subject with a blank line.
- Do not automatically commit, push, or publish. Local implementation and downloading development tools/reference projects are authorized by the project owner.
