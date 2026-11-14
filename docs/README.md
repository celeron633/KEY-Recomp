# Documentation index

Maintained documentation is written in English. Dates use `YYYY-MM-DD`. Keep identifiers, commands, addresses, hashes, and exact diagnostic strings unchanged when editing prose.

## Start here

1. [Project README](../README.md): purpose, supported input, local launch, and preparation.
2. [Working agreement](../AGENTS.md) and [contributor guide](../CONTRIBUTING.md): data boundaries, licensing, development checks, and collaboration.
3. [Plan](PLAN.md) and [status](STATUS.md): acceptance gates and the current milestone.
4. [Current handoff](CURRENT-RUN.md) and [issues](ISSUES.md): implementation evidence and the next investigation.

## Current records

| File | Responsibility |
| --- | --- |
| [STATUS.md](STATUS.md) | Concise current state, validated input, tools, and next work |
| [CURRENT-RUN.md](CURRENT-RUN.md) | Latest native build behavior, implementation details, verification, and handoff |
| [ISSUES.md](ISSUES.md) | Open issues and fixes, with evidence and stable issue IDs; early P2 entries are historical |
| [PLAN.md](PLAN.md) | Architecture, stages, and acceptance criteria |
| [RESEARCH.md](RESEARCH.md) | Initial research, chosen approach, and explicitly dated adoption updates |
| [CONTROLLERS.md](CONTROLLERS.md) | F1 settings, DualShock 4 motion, keyboard fallback, mappings, and validation scope |
| [PROFILING.md](PROFILING.md) | Separate throughput/CPU capture protocol, interpretation limits, and underwater optimization experiments |

## Historical experiments

[P1-RESULTS.md](P1-RESULTS.md) records translator feasibility on 2026-11-13. [P2-BOOT-PROBE.md](P2-BOOT-PROBE.md) records the early boot probe on that date. Their function counts, test counts, unsupported features, and stop points describe those experiments, not the current executable. Preserve them as evidence; consult STATUS.md and CURRENT-RUN.md for current behavior.

## Latest milestone and open issue

As of 2026-11-14, title/menu interaction, initial save creation/reload, and standalone native launch have been directly verified. The owner's latest report is that the game runs normally, including gameplay. The earlier first-level crash report is retained as **P4-001** in ISSUES.md, with no diagnosed cause or specific fix claimed. Gameplay completion and Dolphin differential validation remain open.

When updating documentation, separate direct observations, user reports, and hypotheses. Link local evidence by its repository-relative private path without embedding or publishing the evidence itself. Keep public source/release restrictions in AGENTS.md authoritative.
