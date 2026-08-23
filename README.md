# TortoiseBots — Tortoise WoW 1.18.1 PlayerBots (private)

Clean, optional PlayerBots module for **Tortoise WoW 1.18.1** (Penqle/tortoise-wow, after PR #396).

> **Goal:** Harvest mature PlayerBots behavior without inheriting mature PlayerBots coupling. Keep the Tortoise core usable without bots (`BUILD_PLAYERBOTS=OFF`).

- **Plan / source of truth:** [`docs/PLAN.md`](docs/PLAN.md) — also mirrored online at https://github.com/tortoise-wow-stack/TortoiseBots/blob/main/docs/PLAN.md
- **Host boundary (Phase 1):** [`docs/HOST_API.md`](docs/HOST_API.md)
- **Historical research:** [`docs/DISCOVERY.md`](docs/DISCOVERY.md) (archive, not the plan)
- **Agent guide:** [`AGENTS.md`](AGENTS.md) — read first; repo is agnostic to absolute paths — check for a local checkout first before pulling

This repository is the **private development** counterpart to the public `tortoise-wow-stack` org. It is intentionally agnostic to where checkouts live on disk — before cloning anything, look for a sibling local checkout (e.g. `playerbots-references/`, `TortoiseWoWKnowledgeBase`, `tortoise-docker-penqle`).

Online canonical references (use only if local not present):

- Knowledge Base: https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase
- Penqle core: https://github.com/Penqle/tortoise-wow
- Upstream behavior: https://github.com/cmangos/playerbots, https://github.com/mangoszero/server, https://github.com/Shyalya/tortoise-wow

## Status

Current development is in **Phase 4 stabilization**.

The module is packaged through Penqle's native `modules/<name>/` loader. Only
`src/TortoiseBotsModule.cpp` lives below the loader's recursive `src/` tree;
the broad donor source is selected explicitly by `TortoiseBots.cmake` so
Death Knight, LFG, glyph, vehicle, Arena, and expansion-only families cannot
be compiled accidentally.

- Phase 3 headless lifecycle/session foundation is implemented.
- Native Penqle module discovery, ScriptMgr adapters, module-local AI ownership,
  and the broad Vanilla/Turtle strategy/value/trigger/action source set are
  compiled and linked into `mangosd`.
- `BUILD_PLAYERBOTS=OFF` with modules disabled and `BUILD_PLAYERBOTS=ON` with
  `MODULE_TORTOISEBOTS=static` both pass the cached `mangosd` build.
- The local runtime stack starts the native module and reaches “World server is
  up and running”; AI remains disabled unless `aiplayerbot.conf` explicitly
  enables it.

The first product target is intentionally small and playable: log into one normal character, spawn your own alts as Headless bots, party with them, and level together.

## Quick start (local-first)

1. Check whether sibling checkouts already exist alongside this repo (`playerbots-references/`, `TortoiseWoWKnowledgeBase`, `tortoise-docker-penqle`).
2. If not present, clone from the online URLs above.
3. Read `AGENTS.md` → `docs/PLAN.md` → `docs/HOST_API.md`.

The native core build is enabled with:

```sh
cmake -S <tortoise-wow> -B <build> -DMODULES=static -DMODULE_TORTOISEBOTS=static
cmake --build <build> --target mangosd
```

For the full native configuration also set `-DBUILD_PLAYERBOTS=ON`. The legacy
vendored CMaNGOS tree is not selected by that option; it requires the explicit
`BUILD_LEGACY_PLAYERBOTS=ON` migration escape hatch.

`scripts/sync-to-docker.sh` copies this repository into the sibling core's
`modules/TortoiseBots/` directory for the documented Docker workflow.


## Development loop

Keep iteration fast:

```text
coherent edit batch
-> one cached BUILD_PLAYERBOTS=ON build
-> smallest relevant runtime/manual check
-> continue
```

Do **not** run the full OFF/ON matrix after every edit. Run it once when a shared core/CMake change has stabilized or at a phase/PR/handover boundary.

If a manual gameplay test already passed and the relevant code has not changed, do not repeat it just to reconfirm unchanged behavior.
