# Contributing to KEY-Recomp

Read [AGENTS.md](AGENTS.md), [the plan](docs/PLAN.md), and [the current status](docs/STATUS.md) before making changes. [CURRENT-RUN.md](docs/CURRENT-RUN.md) is the detailed handoff. The current build reaches the title/menu, and the owner now reports normal gameplay. An earlier first-level crash report remains documented without a diagnosed cause. Do not treat the latest report as a completed full-game acceptance test.

## Language and coordination

Use English for maintained documentation, contribution descriptions, issue reports, and new explanatory code comments. Keep code identifiers, addresses, hashes, commands, and exact error messages intact.

Keep each change focused on a concrete behavior or service boundary. State the area you are changing, its assumptions, and the evidence needed to accept it. Coordinate overlapping edits to shared runtime interfaces, generated-source configuration, and status documents. Use a separate branch/worktree when it helps isolate work; do not share mutable private build, save, or generated-output directories between simultaneous experiments. Agent-created branches use the `codex/` prefix unless the owner specifies otherwise.

No automatic commits, pushes, or releases. Local development does not imply permission to publish local artifacts.

## Data and licensing

Each contributor must supply their own supported dump locally. Treat `asset/` as read-only. Keep extracted files, generated game code, executables, screenshots, logs, analysis projects, and saves under `private/`. Do not attach them to public issues or reviews. Public reports should contain a written description, synthetic reproducer, relevant source locations, and only metadata permitted by the working agreement; redact private paths and never include keys or game bytes.

Use independently generated synthetic fixtures for shared tests. Before reusing third-party code, check its license and per-file notices, record the exact source revision in [references.lock.json](references.lock.json), and update [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Public packaging must use an explicit file allowlist and a content/license audit. `.gitignore` is not a release audit.

## Development environment

The currently exercised target is Windows x64 with PowerShell, Python, Git, CMake, Ninja, and LLVM-MinGW/Clang. CMake requires 3.25 or newer. Exact downloadable tool versions and hashes are in [toolchain.lock.json](toolchain.lock.json); reference revisions and paths are in [references.lock.json](references.lock.json).

Check out dependencies at those exact revisions, including Aurora at `references/WiiCompiled/aurora-main`. Do not substitute moving branch tips. Install the local toolchain from the project root:

```powershell
./tools/bootstrap.ps1 -WithClang
```

Follow [README.md](README.md) to extract and validate your own input. The existing translation experiment uses:

```powershell
python tools/make_profile.py
python tools/probe_translation.py
python tools/audit_translation.py
```

These scripts check reference revisions and input hashes. The translation experiment refuses existing `private/generated/{leaf,entry,mapped}` directories: preserve earlier experiments in another private location before rerunning, rather than mixing results.

For an already prepared graphics workspace:

```powershell
python tools/build_probe.py --graphics
python tools/install_local.py
```

`build_probe.py --jobs N` selects a positive number of compiler workers (default: 6). The development PC has also completed full builds with `--jobs 12`. Finish compilation before measuring game performance.

Close the local game before replacing its EXE/DLL files. Do not remove saves to make a build succeed. The graphics configuration currently reuses dependency sources/caches from `private/build/aurora`; the current setup has not been validated as a complete clean-machine onboarding path. Record and resolve missing preparation steps instead of claiming these commands alone provide a finished installer. See the historical [P1 report](docs/P1-RESULTS.md) for translator experiments, not for the current runtime's limitations.

## Validation

Run the standard synthetic checks appropriate to your change:

```powershell
python -m unittest discover -s tests -v
ctest --test-dir private/build/graphics --output-on-failure
```

The CTest command requires an existing configured/built graphics tree. The latest recorded baseline is 19 Python tests and 19 CTest cases; this is evidence from the last runtime change, not a guarantee for another machine or configuration. The audio output test uses SDL's dummy backend. Real-dump runs, graphical/input smoke checks, and manual gameplay checks provide separate evidence and must be reported separately.

For memory-path comparisons, set `KEY_DISABLE_INLINE_RAM=1` in the child process environment before launch to use the original checked handlers; omit it for normal operation. Both routes enforce memory policy. Preserve the build manifest and five-second `TIMING` lines, and compare the same scene/input with compilation stopped. `game_root_hz` counts game-root task traversals; display-copy FPS alone is not a measurement of character velocity.

For event-polling comparisons, `KEY_DISABLE_POLL_CLOCK=1` disables sharing the clock sample within a dispatch. It does not disable the VI halfline or GPU mailbox optimizations. Guest timebase/MMIO reads always sample fresh time, and callback/output work invalidates the dispatch sample. The selected mode is logged at startup.

Document what was actually tested, the exact commands/configuration, the expected result, and any failure or untested path. A generated function, a successful link, an open window, and playable gameplay are different milestones. Unknown instructions/services must fail with useful diagnostics; do not bypass game logic or return fake success to advance a test.

Documentation-only changes need link, command, language, and consistency checks; do not add implementation-mirroring tests or rebuild the game merely to translate prose.

## Issue reports and handoffs

For a runtime issue, record:

- Supported game ID/revision, local build identity, host OS/GPU/audio configuration when relevant, and the source revision if available.
- The starting state, input sequence, expected behavior, observed behavior, and whether the result is repeatable.
- Whether evidence was directly observed, reported by a user, or inferred. Do not assign a cause without evidence.
- A local evidence path under `private/reports/`, or a synthetic reproducer safe to share. Preserve logs before restarting a process that overwrites them.
- The affected service boundary, next investigation step, and remaining uncertainty.

Keep the current overview in [STATUS.md](docs/STATUS.md), implementation/evidence details in [CURRENT-RUN.md](docs/CURRENT-RUN.md), and issue records in [ISSUES.md](docs/ISSUES.md). Update [PLAN.md](docs/PLAN.md) only when the route or an acceptance gate changes. Preserve historical experiment results with their date and scope; do not silently rewrite an old test count as if the old run had tested new code.

A change description should explain the problem and resulting behavior, list meaningful validation, and disclose remaining limitations. Include a short handoff describing what another contributor should do next. Public release preparation is a separate milestone with its own data and license checks.
