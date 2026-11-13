# Local reference directory

Everything in this directory except this file is ignored. Never package the directory recursively. Repository URLs, exact commits, licenses, and intended uses are recorded in [references.lock.json](../references.lock.json).

The current workspace uses `key/`, `WiiCompiled/`, the pinned Aurora fork at `WiiCompiled/aurora-main`, and selected Dolphin source snapshots at `dolphin-source/`. The older statement that Aurora had not been initialized is obsolete. For reproduction, check out repositories at the exact locked commits and preserve per-file notices; do not replace pins with current branch tips. Reference projects' own build outputs are not public release contents.

Keep local adaptations in the project's explicitly reviewed source or preparation tools rather than silently modifying the reference checkouts. See [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) for reused components and [CONTRIBUTING.md](../CONTRIBUTING.md) for the development workflow. Do not treat these references as permission to redistribute game data.
