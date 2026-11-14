# KEY-Recomp

A native Windows x64 static recompilation project for Kirby's Epic Yarn. **The local EXE reaches the original title/menu, creates and reloads save data, and accepts menu input. The owner now reports that the game runs normally, including gameplay. An earlier first-level crash report is retained for follow-up; full-game completion and compatibility have not been validated.**

The intended workflow is: provide your own dump -> extract and validate locally -> generate C++ locally -> compile the game EXE -> launch directly, without a Dolphin process at runtime. Public downloads are intended to contain only tools and runtime code, never original assets or the translated game program. High internal rendering resolution is a goal; the original artwork remains unchanged, and higher frame rates require separate validation.

Supported input: US `RK5E01 Rev 0`. The DOL SHA-1 must be `f4280a2b0709bfde4132be22e9f7fa8ecfa2a130`; the preparation and build tools also enforce the supported SHA-256.

## Run and rebuild locally

On the prepared development workspace, double-click `private/game/KEY-Recomp.exe`. No command-line arguments or Dolphin process are required. Keep its surrounding private directory structure so the application can locate the extracted data. The default window is 1280x720 and can be maximized without changing the game tick. Window size alone is not evidence of validated high-resolution rendering across the game.

- Arrow keys / WASD: direction input, mapped for a sideways Wii Remote.
- X: Wii 2, confirm. Z: Wii 1, back.
- Enter: A. C: B. Space: Plus.
- Save data: `private/user/nand`. Replaced/deleted nodes are retained in `private/user/nand-deleted`.
- Application log: `private/reports/native-app.log`. Keep local evidence before a subsequent launch overwrites it.

After extraction, translation, and dependency preparation are complete:

```powershell
python tools/build_probe.py --graphics
python tools/install_local.py
```

By default, the build runs synthetic checks and verifies rejection of an incorrect DOL. Add `--explore` only for a diagnostic boot with a time/call budget. `install_local.py` copies an explicit list of EXE/DLL files into `private/game`; it is not a public release packager. The game EXE contains locally generated game code and must not be publicly distributed.

The PCM backlog exit reported after a window click has been addressed with output backpressure. A long pause in the Windows system menu can still cause an audio catch-up delay. See [known issues](docs/ISSUES.md) for evidence and remaining limitations.

The latest local build also corrects MEM2 palette-address truncation, which could cause invalid colors or an unmapped-memory stop during drawing. Synthetic regression checks pass; the earlier first-level crash sequence and complete gameplay loop still need validation.

## Prepare local input

The extraction tool requires Python 3.10+ and a DolphinTool build with the `header` and `extract` commands. Run these commands from the project root, adapting the local paths as needed:

```powershell
python tools/prepare.py --image "asset/Kirby's Epic Yarn.wbfs" --dolphin-tool "D:/Program Files/Dolphin-x64/DolphinTool.exe"
python -m unittest discover -s tests -v
```

Use `--reuse` for an existing extraction; files are not overwritten. Reuse validates the DOL again but does not establish integrity of every asset. If an extraction is incomplete, retain the old directory for investigation and extract into a fresh workspace. The tool does not automatically delete files.

Output is written to `private/extracted/RK5E01/` and `private/reports/inventory.json`. Do not upload these files or include them in a release. Input under `asset/` remains read-only. Unknown versions and mismatched DOLs are rejected.

This is a development workflow, not yet a verified installer for a clean machine. See [CONTRIBUTING.md](CONTRIBUTING.md) for toolchain, translation, test, and handoff guidance.

## Documentation

Maintained project documentation is in English.

| Document | Purpose |
| --- | --- |
| [Working agreement](AGENTS.md) | Data boundaries, correctness, licensing, and agent workflow |
| [Contributor guide](CONTRIBUTING.md) | Onboarding, validation, and collaboration |
| [Documentation index](docs/README.md) | Reading order and current versus historical records |
| [Plan](docs/PLAN.md) | Architecture and acceptance gates |
| [Status](docs/STATUS.md) | Current milestones and verification boundaries |
| [Current handoff](docs/CURRENT-RUN.md) | Latest implementation details, local evidence, and next steps |
| [Issues](docs/ISSUES.md) | Open problems, fixes, and historical debugging evidence |
| [Research](docs/RESEARCH.md) | Source references and rationale for the chosen approach |
| [P1 results](docs/P1-RESULTS.md) | Historical translator feasibility experiments |
| [P2 boot probe](docs/P2-BOOT-PROBE.md) | Historical early-boot experiments |
| [Dependency lock](references.lock.json) | Exact reference revisions |

This is an unofficial project and is not affiliated with Nintendo. Original project code is licensed under GPL-3.0-only; see [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). The runtime uses selected headers and algorithms from the pinned local references. Generated game code and game executables are excluded from public source and releases.
