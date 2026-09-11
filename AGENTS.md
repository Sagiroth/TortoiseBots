# AGENTS.md

## Project

This repository is **TortoiseBots** — an optional native PlayerBots module for **Tortoise WoW 1.18.1**.

Public repo: <https://github.com/Sagiroth/TortoiseBots>

PlayerBots is rebuilt as a clean, optional module. The Tortoise core must remain usable without it.

> Reuse existing PlayerBots behavior (primarily AzerothCore/mod-playerbots) without inheriting its coupling.

---

## Read first

For any PlayerBots work, consult the **Open Knowledge Format (OKF)** catalogue in `docs/`:

1. [`docs/README.md`](docs/README.md) — Central switchboard and navigation catalog.
2. [`docs/manifest.yaml`](docs/manifest.yaml) — OKF bundle index, ontology, and role entry points.
3. [`docs/concepts/architecture-invariants.md`](docs/concepts/architecture-invariants.md) — The 5 non-negotiable architectural rules.
4. [`docs/classes/overview.md`](docs/classes/overview.md) — 9-class combat rotations, specs, and Turtle WoW custom abilities.
5. [`docs/guides/player-controls.md`](docs/guides/player-controls.md) — Tactical intents (`.bot action`), CC by raid mark, and `/tbm` addon protocol.
6. [`docs/guides/living-world.md`](docs/guides/living-world.md) — Roaming bots, quest grinding, AH economy, and guild formation.
7. [`docs/HOST_API.md`](docs/HOST_API.md) — When touching sessions, lifecycle, packets, commands, or core seams.
8. [`docs/PROVENANCE.md`](docs/PROVENANCE.md) — When porting or changing donor-derived behavior.

`docs/PLAN.md` and `docs/concepts/` are the architecture source of truth. Historical audit evidence lives in Git history.

This repo is self-contained. All required context is indexed in `docs/` and validated via `python3 tools/verify_okf.py`.

### ⚠️ Keeping OKF Documentation Up-to-Date

The Open Knowledge Format (`docs/`) is the single source of truth for TortoiseBots architecture, bot commands, configuration, classes, and mechanics. Whenever making changes that impact user-facing behavior, class balance, commands, or host seams, update the corresponding documentation:

| Change Scope / Area | Typical Code Paths | Required OKF Doc Updates | Mandatory Gate |
| :--- | :--- | :--- | :--- |
| **Commands & Actions** | `commands/*`, `actions/*` | [`docs/guides/player-controls.md`](docs/guides/player-controls.md) | `python3 tools/verify_okf.py` |
| **Configuration & Tuning** | `PlayerbotAIConfig.*`, `aiplayerbot.conf*` | [`docs/guides/configuration-tuning.md`](docs/guides/configuration-tuning.md) | `python3 tools/verify_okf.py` |
| **Class AI & Rotations** | `strategy/<class>/*`, `AiObjectContext` | Relevant class doc in [`docs/classes/`](docs/classes/) | `python3 tools/verify_okf.py` |
| **Host Seams & Sessions** | `host/*`, `runtime/BotManager.*` | [`docs/HOST_API.md`](docs/HOST_API.md), [`docs/concepts/architecture-invariants.md`](docs/concepts/architecture-invariants.md) | `tools/verify_penqle_host_contract.sh` |
| **Doc Structure & Nodes** | Any file in `docs/` | [`docs/manifest.yaml`](docs/manifest.yaml), [`docs/README.md`](docs/README.md) | `python3 tools/verify_okf.py` |

---

## Canonical upstream

The canonical upstream and target core is:

<https://github.com/tortoise-wow/tortoise-wow>

Unless qualified otherwise, these terms mean `tortoise-wow`: upstream, upstream core, target core, core main, core PR.

`shyalya-tortoise-wow` and other PlayerBots repos (`cmangos/playerbots`, `mod-playerbots`, `mangoszero/server`, `cmangos/mangos-classic`) are **read-only donor references**, not upstream. Source-of-truth order:

1. `tortoise-wow` pinned target core
2. Tortoise data / DBC / runtime evidence
3. This repo's host contract (`docs/HOST_API.md`, `docs/PLAN.md`)
4. `shyalya-tortoise-wow` and other donors as references only

Git remote aliases are not authority — always identify a repo by `owner/repo`.

---

## Reference repositories

All references are remote, read-only, and optional. Clone only what you need for the current question — do not vendor them into this repo.

| Reference | URL | Purpose |
| --- | --- | --- |
| Upstream core | <https://github.com/tortoise-wow/tortoise-wow> | Target core (`tortoise-wow`) |
| shyalya-tortoise-wow | <https://github.com/Shyalya/tortoise-wow> | Tortoise 1.18.1 compatibility evidence, known API differences, Tortoise fixes |
| CMaNGOS PlayerBots | <https://github.com/cmangos/playerbots> | Existing combat/movement/class/healing/CC/dungeon behavior |
| CMaNGOS Classic | <https://github.com/cmangos/mangos-classic> | What CMaNGOS PlayerBots expects from its host |
| MangosZero | <https://github.com/mangoszero/server> | Lifecycle/session/group patterns |
| mod-playerbots | <https://github.com/mod-playerbots/mod-playerbots> | Newer behavior reference |
| Docker/runtime env | Local / private checkout | Optional local runtime/validation environment |

Do not edit, commit to, or rebase reference repos. Do not blindly copy their architecture. Before relying on a commit for provenance, record its SHA (e.g. GitHub permalink or `git ls-remote <url> HEAD`).

### What each reference is for — quick guide

- **shyalya-tortoise-wow** — Tortoise spells/talents, session/movement/group/loot lessons, integration pain
- **CMaNGOS PlayerBots** — richest behavior source for combat/movement/healing/CC/dungeons
- **CMaNGOS Classic** — host API definitions and lifecycle semantics
- **MangosZero** — smaller bot lifecycle, character creation, group handling

### Reference lookup strategy

Do not search every repo for every task:

- **Public behavior / commands / ownership** → `shyalya-tortoise-wow` → CMaNGOS PlayerBots
- **Combat / class AI / healing / CC / movement** → CMaNGOS PlayerBots → `shyalya-tortoise-wow` → MangosZero
- **Session / lifecycle / bot login** → Current Tortoise core → MangosZero → `shyalya-tortoise-wow` → CMaNGOS
- **Tortoise spells / talents / custom content** → Tortoise core/data → `shyalya-tortoise-wow` → Vanilla refs
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
2. Check donor behavior where applicable
3. Inspect the most relevant donor
4. Inspect Tortoise differences
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

## Observability subsystem

`tools/observability` is a standalone Go daemon (Prometheus + web dashboard) fed by `runtime/ObservabilityEmitter.{h,cpp}` over non-blocking UDP. It is optional and config-gated; core stays ignorant of it.

Rules that keep its state honest:

- The daemon has exactly one authoritative store (`internal/state`). REST, WebSocket, and Prometheus all read it.
- The emitter sends self-contained snapshot cycles: one `HEARTBEAT` + `BOT_BATCH` chunks sharing a `seq`. Publish a roster only from a complete cycle; clients replace their roster wholesale instead of merging deltas.
- Every datagram also carries a `session` epoch (server process start). The daemon resets sequence/roster state when it changes, so a server restart that resets `seq` cannot be locked out as "old cycles".
- Bound and prune every emitter table (bot tracking, action failures, anomaly cooldowns) on each snapshot. Macro-state ratios are windowed, never lifetime totals.
- Anomaly types are a closed set (`model.AcceptedAnomalyTypes`) so Prometheus label cardinality stays bounded.
- Bump `kProtocolVersion` in `ObservabilityEmitter.cpp` and `model.ProtocolVersion` in `internal/model/types.go` together.

### Telemetry surface (protocol v4)

Each `BOT_BATCH` bot entry carries: `name, guid, class, role, level, hp/max_hp, power/max_power, power_type, map, zone, x/y/z/o, target, strategy, state, last_action, last_trigger`.

- `power_type` is the current resource (`mana`, `rage`, `energy`, `focus`, `happiness`); druids reflect their active form. Label bars by it, never hardcode "mana".
- `last_action`/`last_trigger` feed repeated-action detection; they are sampled per 2s snapshot, not per execution.
- Anomalies carry `guid` so the daemon can key episodes; accepted types are `BOT_STUCK`, `ACTION_LOOP`, `UNREACHABLE_TARGET`, `BOT_DEATH`.
- `BOT_DEATH` is emitted from `PlayerbotAI::OnDeath` (target/zone/position/level).
- Anomaly emitters that can persist (`UNREACHABLE_TARGET`) re-report every cooldown window so the daemon has a liveness signal; do not make them fire-once.

### Issue episodes (`internal/state` issue tracker)

Persistent problems are tracked as open/closed episodes per bot, surfaced in the dashboard Issues tab, map glow, roster badge, and `/api/v1/issues`.

- Snapshot-derived: `STUCK` (moving state but position frozen >= 60s), `DEAD_LONG` (dead >= 2 min).
- Anomaly-derived: `ACTION_LOOP`, `UNREACHABLE_TARGET` (refreshed by the emitter, expire after a 2 min TTL, or close early when a snapshot contradicts them).
- **Minimum age**: only episodes that persist `ISSUE_MIN_AGE_SEC` (default 300s) are shown; shorter ones are discarded entirely. This is the guard against transient false positives — keep new detectors behind it.
- Severity escalates `watch` (>= 1 min) -> `persistent` (>= 10 min).
- Resolved history survives a game-server restart (`Reset()` clears open episodes only); it is in-memory and bounded, so a daemon restart clears it.
- Metrics: `tortoisebots_issues_active{type}`; anomalies counted by type (including `BOT_DEATH`).

Dashboard UI: Bots roster supports status/class/role filters, "issues only", sortable columns, and a power bar; the Issues tab filters by type and minimum duration and highlights issue bots on the map.

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

- **Docs/comments/config only** → verify via `./tools/verify_all.sh` (runs OKF validator, surface checks, and decision-trail test in ~1s), text checks + `git diff --check`, no C++ build.
- **Module-only C++** → one cached `MODULE_TORTOISEBOTS=static` build after the batch is coherent.
- **Observability tool (Go)** → `docker run --rm -v "$PWD/tools/observability:/src" -w /src golang:1.22-alpine sh -c 'go vet ./... && go test ./...'` (no host Go toolchain). Live check: server logs `Observability telemetry active`, `/metrics` shows `mangos_server_online 1` and rising `tortoisebots_snapshots_total`.
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
