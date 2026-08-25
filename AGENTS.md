# AGENTS.md

## Project

This repository is **TortoiseBots** — an optional native PlayerBots module for **Tortoise WoW 1.18.1**.

Public repo: <https://github.com/tortoise-wow-stack/TortoiseBots>

PlayerBots is rebuilt as a clean, optional module. The Tortoise core must remain usable without it.

> Harvest mature PlayerBots behavior without inheriting mature PlayerBots coupling.

---

## Read first

For any PlayerBots work, read in this order:

1. `docs/PLAN.md` — durable architecture rules and roadmap.
2. `docs/HOST_API.md` — when touching sessions, lifecycle, packets, commands, build/module integration, or any core seam.
3. `docs/PROVENANCE.md` — when porting or changing donor-derived behavior.
4. `docs/README.md` — documentation map.

`docs/PLAN.md` is the architecture source of truth. Historical evidence (audit/handover) lives in Git history and `docs/archive/` if retained.

This repo is self-contained. You do not need any checkout outside it to understand the architecture or to contribute. All required context is in `docs/` and this file.

If you have a local Tortoise core checkout, Docker stack, or Knowledge Base checkout alongside this repo, tell the agent explicitly when it is relevant. The agent must not assume any sibling directory exists or guess absolute paths.

---

## Canonical upstream

The canonical upstream and target core is:

<https://github.com/Penqle/tortoise-wow>

Unless qualified otherwise, these terms mean `Penqle/tortoise-wow`: upstream, upstream core, target core, core main, core PR.

`Shyalya/tortoise-wow` and other PlayerBots repos (`cmangos/playerbots`, `mod-playerbots`, `mangoszero/server`, `cmangos/mangos-classic`) are **read-only donor references**, not upstream. Source-of-truth order:

1. `Penqle/tortoise-wow` pinned target core
2. Turtle data / DBC / runtime evidence
3. This repo's host contract (`docs/HOST_API.md`, `docs/PLAN.md`)
4. Shyalya and other donors as references only

Git remote aliases are not authority — always identify a repo by `owner/repo`.

---

## Reference repositories

All references are remote, read-only, and optional. Clone only what you need for the current question — do not vendor them into this repo.

| Reference | URL | Purpose |
| --- | --- | --- |
| Knowledge Base | <https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase> | Behavioral spec, capability inventory, command reference, acceptance-test inspiration |
| Penqle core | <https://github.com/Penqle/tortoise-wow> | Target core |
| Shyalya fork | <https://github.com/Shyalya/tortoise-wow> | Turtle 1.18.1 compatibility evidence, known API differences, Turtle fixes |
| CMaNGOS PlayerBots | <https://github.com/cmangos/playerbots> | Mature combat/movement/class/healing/CC/dungeon behavior |
| CMaNGOS Classic | <https://github.com/cmangos/mangos-classic> | What CMaNGOS PlayerBots expects from its host |
| MangosZero | <https://github.com/mangoszero/server> | Lifecycle/session/group patterns |
| mod-playerbots | <https://github.com/mod-playerbots/mod-playerbots> | Newer behavior reference |
| Docker/runtime env | <https://github.com/Sagiroth/tortoise-docker-penqle> | Optional local runtime/validation environment |

Read `TortoiseWoWKnowledgeBase/AGENTS.md` before using its `playerbots/` docs. The Knowledge Base describes **what should happen**, not how to architect it.

Do not edit, commit to, or rebase reference repos. Do not blindly copy their architecture. Before relying on a commit for provenance, record its SHA (e.g. GitHub permalink or `git ls-remote <url> HEAD`).

### What each reference is for — quick guide

- **TortoiseWoWKnowledgeBase** — public behavior, commands, ownership, security
- **Shyalya** — Turtle spells/talents, session/movement/group/loot lessons, integration pain
- **CMaNGOS PlayerBots** — richest behavior source for combat/movement/healing/CC/dungeons
- **CMaNGOS Classic** — host API definitions and lifecycle semantics
- **MangosZero** — smaller bot lifecycle, character creation, group handling

### Reference lookup strategy

Do not search every repo for every task:

- **Public behavior / commands / ownership** → Knowledge Base → Shyalya → CMaNGOS PlayerBots
- **Combat / class AI / healing / CC / movement** → Knowledge Base → CMaNGOS PlayerBots → Shyalya → MangosZero
- **Session / lifecycle / bot login** → Current Tortoise core → MangosZero → Shyalya → CMaNGOS
- **Turtle spells / talents / custom content** → Tortoise core/data → Knowledge Base → Shyalya → Vanilla refs
- **Runtime / integration failures** → Current core source → Docker env (if you have one) → logs → references

The current Tortoise architecture always outranks making a donor port easier.

---

## Architecture invariants

Non-negotiable.

### Keep PlayerBots optional

`BUILD_PLAYERBOTS=OFF` must remain a supported first-class build. The core must build without this module. No PlayerBots runtime/config/SQL dependency may be required when bots are off.

Native selection is via `MODULE_TORTOISEBOTS` in the target core. `BUILD_PLAYERBOTS` is not the native selector; `BUILD_LEGACY_PLAYERBOTS=OFF` is the normal setting for native work.

### Do not reintroduce legacy coupling

Never recreate:

- `WorldSession::GetBot()` / `SetBot()` / `m_bot` / `sPlayerBotMgr` / `PlayerBotEntry` in normal core code
- Scattered `if (IsBot())` / `if (GetBot())` checks

Normal gameplay systems must not know a `Player` is bot-controlled. **If a feature requires bot-specific conditions in unrelated core systems, stop and explain why.**

### Prefer module-only changes

1. Can it live entirely inside the module?
2. Does an existing `ScriptMgr`/event/lifecycle hook already expose it?
3. Can the missing capability be expressed as a generic core concept?

Only add a new core seam when the existing core cannot provide the capability cleanly.

### Centralize host integration

Unavoidable integration stays in the small approved host boundary. Do not spread hooks through `Player.cpp`, `Unit.cpp`, `Spell.cpp`, `WorldSession.cpp`, movement, groups, etc.

Aim for `<= 5` directly PlayerBots-aware core files. Approaching 8–10 before MVP is an architectural warning.

### Headless sessions

Bot sessions are a transport concern. Prefer generic concepts: `HasNetworkTransport()`, `CanReceiveClientPackets()`, network-backed vs headless session.

The core may understand generic headless/non-network capability. The module knows a particular headless session is a bot. Do not make core code ask "is this a bot session?".

### LLM isolation

LLMs must never be required for combat, movement, healing, threat, interrupts, or CC. LLM integration is asynchronous and optional. If the LLM is unavailable, bot gameplay continues.

---

## Donor/reference rule

```text
harvest behavior, not architecture
```

Do not vendor a donor tree into the core. Do not cherry-pick commits that expand host coupling. Do not preserve donor class structure just to make copying easier.

For imported behavior:

1. Understand observable behavior
2. Check Knowledge Base where applicable
3. Inspect the most relevant donor
4. Inspect Turtle differences
5. Define expected behavior / acceptance test
6. Implement inside the new module
7. Test it
8. Record provenance

Prefer `study -> extract intent -> port/reimplement -> test` over literal cherry-picks. A cherry-pick is acceptable only when isolated, compatible, licensed, and not expanding coupling.

---

## Provenance

Record substantial copied/ported behavior in `docs/PROVENANCE.md`:

```text
Feature:
Source repository:
Source commit:
Source files:
Copied / ported / independently reimplemented:
Reason:
Local validation:
```

Preserve upstream license/copyright notices. Do not silently copy large bodies of code.

---

## Scope discipline

Prefer small vertical slices. Do not create speculative abstractions, implement every class at once, start raid/BG/random-bot systems before the dungeon MVP, refactor unrelated core code, or optimize for 1000 bots before the small-party case works.

First target: `human + owned bot` → then `2 humans + bots filling a 5-player dungeon`.

---

## Performance rules

From the start: no DB query every bot tick, no full-world scan every tick, no synchronous external network calls on game/map threads, no rebuilding large strategy graphs every update, cache immutable spell/talent metadata, use event-driven invalidation, keep expensive diagnostics opt-in. Measure rather than guess.

Useful metrics: bot update time, total bot CPU, DB queries, path/movement requests, AI decisions/sec, memory per bot.

---

## Git safety

Never reset/clean/overwrite/stage/include unrelated user changes. Inspect `git status --short` before and after work. Do not use destructive Git commands unless explicitly requested.

---

## Validation

Use the **smallest check that proves the current change**. Do not let validation dominate implementation.

### Default loop

```text
inspect -> coherent batch of edits -> one build if compiled code changed -> smallest runtime/manual check -> continue
```

A successful build/test remains evidence for unchanged code. Do not rebuild after every file or re-run the full matrix for unrelated edits.

### Validation cadence

- **Docs/comments/config only** → text checks + `git diff --check`, no C++ build.
- **Module-only C++** → one cached `MODULE_TORTOISEBOTS=static` build after the batch is coherent.
- **Core-seam change** → cached module build while iterating; full ON/OFF matrix only when stable.
- **Build-gating / CMake change** → directly affected configurations.
- **Phase / PR / handover boundary** → full OFF/ON matrix once (`git diff --check` + coupling audits + required runtime gates).

Use cached builds by default. Rebuild images only when build inputs actually changed. Reuse existing binaries/stacks when unchanged. Do not ask the user to repeat a manual validation that already passed for unchanged code.

### Coupling audits

```bash
rg -n 'GetBot\(\)|SetBot\(|\bm_bot\b|sPlayerBotMgr|PlayerBotEntry|PB_STATE_' src
rg -n -i 'PlayerBot|BotService|BotSession|HeadlessSession' src/game
```

Every hit in normal core code must be explainable. Growing unrelated matches = regression. Do not flag legitimate `PlayerAI`, `PlayerControlledAI`, Discord bot code, or gameplay entities whose names naturally contain "Bot".

---

## Testing behavior

Prefer deterministic debug scenarios. Key areas: headless login/logout, duplicate login, human reconnect, save/reload, shutdown, follow, movement, target selection, threat, healing, interrupts, CC, death, wipe recovery, teleport/map transitions, dungeon regrouping.

Never claim behavior was tested if it was only inspected statically.

### Runtime validation (optional, when you have a running server)

If you have a local core checkout or Docker stack and the user has pointed the agent at it, use the **smallest relevant check** for the behavior just changed:

- server starts, module loads, bot logs in and enters world, follows/acts, logs out cleanly, human reconnect works, shutdown is clean, no unexpected DB/session errors.

Prefer one focused gameplay check per slice. Full regression belongs at phase/PR boundaries or after lifecycle/session seam changes.

If you have a Docker environment, read its own `AGENTS.md`/`README` before using it. Never run destructive operations (`docker compose down -v`, `docker volume rm`, `docker system prune`, `DROP DATABASE`, `TRUNCATE`) unless explicitly requested — explain why first.

Examples of runtime evidence: server starts, module loads, bot enters world, follows, logs out, human reclaim works, shutdown clean, no DB/session errors. Never report "runtime tested" for static inspection alone.

---

## Reporting

At the end of each task report:

- files changed (core vs module)
- new host hooks and why each was necessary
- tests/builds performed (and what was *not* run)
- runtime validation performed and observed behavior + log evidence
- remaining issues, architecture concerns, provenance for imported behavior

If no build/test was run, say so explicitly. If a requested feature would violate an invariant, stop and explain the conflict.
