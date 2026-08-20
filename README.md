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

Phase 1 (host-boundary discovery) complete — see `docs/HOST_API.md`. No module code yet; the first implementation gate is review of the host seam proposal.

## Quick start (local-first)

1. Check whether sibling checkouts already exist alongside this repo (`playerbots-references/`, `TortoiseWoWKnowledgeBase`, `tortoise-docker-penqle`).
2. If not present, clone from the online URLs above.
3. Read `AGENTS.md` → `docs/PLAN.md` → `docs/HOST_API.md`.
