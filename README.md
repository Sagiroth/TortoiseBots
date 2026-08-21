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

Current development is in **Phase 4**.

- Phase 3 headless lifecycle/session foundation is implemented.
- Phase 4 Slice 1 `Follow` is implemented and manually playtested.
- Same-account alt support is being finalized: one account may have **1 Network + N Headless** character sessions, so a player can control their own existing alts without dedicated bot accounts.
- Next behavior slice after the session seam is finalized: **Stay**, then Assist/basic combat.

The first product target is intentionally small and playable: log into one normal character, spawn your own alts as Headless bots, party with them, and level together.

## Quick start (local-first)

1. Check whether sibling checkouts already exist alongside this repo (`playerbots-references/`, `TortoiseWoWKnowledgeBase`, `tortoise-docker-penqle`).
2. If not present, clone from the online URLs above.
3. Read `AGENTS.md` → `docs/PLAN.md` → `docs/HOST_API.md`.


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
