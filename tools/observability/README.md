# Observability daemon — developer notes

`tools/observability` is a standalone Go daemon (Prometheus + web dashboard) fed by `runtime/ObservabilityEmitter.{h,cpp}` over non-blocking UDP. It is optional and config-gated; core stays ignorant of it.

Rules that keep its state honest:

- The daemon has exactly one authoritative store (`internal/state`). REST, WebSocket, and Prometheus all read it.
- The emitter sends self-contained snapshot cycles: one `HEARTBEAT` + `BOT_BATCH` chunks sharing a `seq`. Publish a roster only from a complete cycle; clients replace their roster wholesale instead of merging deltas.
- Every datagram also carries a `session` epoch (server process start). The daemon resets sequence/roster state when it changes, so a server restart that resets `seq` cannot be locked out as "old cycles".
- Bound and prune every emitter table (bot tracking, action failures, anomaly cooldowns) on each snapshot. Macro-state ratios are windowed, never lifetime totals.
- Anomaly types are a closed set (`model.AcceptedAnomalyTypes`) so Prometheus label cardinality stays bounded.
- Bump `kProtocolVersion` in `ObservabilityEmitter.cpp` and `model.ProtocolVersion` in `internal/model/types.go` together.

## Telemetry surface (protocol v8)

Each `BOT_BATCH` bot entry carries: `name, guid, class, role, level, xp, next_xp, hp/max_hp, power/max_power, power_type, map, zone, x/y/z/o, target, target_level, strategy, state, last_action, last_trigger, travel_purpose, travel_to`.

Macro states (`state`, heartbeat `states`, `tortoisebots_state_ratio`): `combat` (in combat), `moving` (a movement generator owns the bot), `busy` (standing still but doing real work: looting, casting, sitting to eat/drink, or movement within the last 45 s), `stalled` (standing still for 45+ s whose only activity was churn — an active travel target or a changing last-action name, no movement/loot/cast/sit — i.e. standing with a destination and getting nowhere), `resting` (rest flag), `idle` (no movement, action, cast, loot, or active target for 45+ s — really doing nothing), `dead`. `stalled` exists so a bot parked with a travel target is no longer reported as busy. Per-tick cost is member reads + one action-name compare; the 3-min window is unchanged.
- `power_type` is the current resource (`mana`, `rage`, `energy`, `focus`, `happiness`); druids reflect their active form. Label bars by it, never hardcode "mana".
- `last_action`/`last_trigger` feed repeated-action detection; they are sampled per 2s snapshot, not per execution.
- Anomalies carry `guid` so the daemon can key episodes; accepted types are `STUCK`, `ACTION_LOOP`, `UNREACHABLE_TARGET`, `BOT_DEATH`.
- `STUCK` and `ACTION_LOOP` are counter-only (`tortoisebots_anomalies_total`): `STUCK` never enters the Incidents ring buffer (the 60 s `STUCK` issue episode is the surfaced signal) and `ACTION_LOOP` never opens an issue episode (cooldown-gated to one event/30 s, so it almost never reaches a surface gate).
- `BOT_DEATH` is emitted from `PlayerbotAI::OnDeath` (target/zone/position/level).
- Anomaly emitters that can persist (`UNREACHABLE_TARGET`) re-report every cooldown window so the daemon has a liveness signal; do not make them fire-once.
- `SERVER_INFO` datagrams (startup + every 5 min) carry module/core versions, live core rates and `AiPlayerbot` flags from running getters — never config files. The dashboard Server panel renders them, and its **Copy diagnostic report** button turns them plus pool health (tracked/gaining, median/total XP/h, deaths/min, combat/grind %, level bands), issue counts and freshness into a paste-ready plain-text snapshot (no secrets) — the way to report issues.
- `/api/v1/grinding` serves the pool rollup behind the Grinding panel: per-bot XP/h + last-gain age over a 30 min level-up-aware window, pool gaining %, median/total XP/h, kills/min + killed-5min % (from `BOT_DEATH` anomalies), combat/grind %, adaptive level bands.
- `BOT_EVENTS` datagrams carry the activity stream: the module mirrors a whitelist of `bot_events.csv` rows (`PlayerbotAIConfig::logEvent` → `ObservabilityEmitter::EmitBotActivity`) between the heartbeat and the roster batches of a cycle, so the roster published by the same cycle already carries the refreshed counters. Whitelisted: quest accept/complete/turn-in/drop/travel, `StoreLootAction`, `GatherLoot`, `LootMoney`, `SellAction`, `BuyAction`, `RepairAllAction`, `TrainerAction`, `NearbyService` (kind = trainer/vendor/turn-in), `AhAction`, `AhBidAction`, `BotDeath`, the revive/repop events (ghost time), `ReachGiveUp`, and an explicit `Kill` event from `XpGainAction` — the CSV row cannot carry the kill/non-kill XP flag. Item events are enriched server-side with `ItemPrototype` quality/sell/buy (no DB); every event carries the bot's copper, from which the daemon derives earned/spent. The queue is bounded (2048 rows, dropped on overflow) and chunked 40/datagram; `logEvent` forwards even when `bot_events.csv` is not in `AllowedLogFiles`.
- `/api/v1/activity` (summary + per-bot counters + level timeline), `/api/v1/activity/loot` (`min_quality`/`class`/`min_level`/`max_level`/`bot`/`limit`), `/api/v1/activity/quests` (`bot`/`limit`) serve the Activity tab. Per-bot counters are also embedded in every roster snapshot as `activity`. All activity state is in-memory and bounded by the live population (activity is pruned with the roster; counters reset on session change or roster wipe); no DB reads.

## Issue episodes (`internal/state` issue tracker)

Persistent problems are tracked as open/closed episodes per bot, surfaced in the dashboard Issues tab, map glow, roster badge, and `/api/v1/issues`.

- Snapshot-derived: `STUCK` (moving state but position frozen >= 60s), `DEAD_LONG` (dead >= 2 min).
- Anomaly-derived: `UNREACHABLE_TARGET` only (refreshed by the emitter, expires after a 2 min TTL, or closes early when a snapshot contradicts it).
- **Minimum age**: only episodes that persist past their gate are shown (`ISSUE_MIN_AGE_SEC`, default 300s); shorter ones are discarded entirely. `UNREACHABLE_TARGET` carries its own 2 min lower gate — it can only shorten the configured minimum age, never lengthen it — because the snapshot contradiction bounds its false-positive risk. This is the guard against transient false positives — keep new detectors behind it.
- **Post-restart blackout**: no `DEAD_LONG` episode opens in the first 5 min after a game-server session change; the timer slides through the blackout so per-bot rows start timing only after it.
- Severity escalates `watch` -> `persistent` (>= 10 min).
- Resolved history survives a game-server restart (`Reset()` clears open episodes only); it is in-memory and bounded, so a daemon restart clears it.
- Metrics: `tortoisebots_issues_active{type}`; anomalies counted by type (including `BOT_DEATH`, counter-only `STUCK`/`ACTION_LOOP`).
- Incidents (`/api/v1/anomalies`) is a rolling last-1000-event window (~30 min at busy rates), not history; severity filters match case-insensitively.

Dashboard UI: the Issues tab defaults to the ≥10 min (persistent) duration filter, hides the trigger column for anomaly rows (always empty — details carry the emitter text), and the resolved card shows "shown/total". The armory shows max-only power (no live current value exists), the live telemetry zone for online bots, and an XP bar (current/next + XP/hour) from live telemetry for online bots — the armory DB has no per-level XP row, so offline bots show none.

Armory detail view: Spells and Professions are separate tabs (profession spells moved out of the spellbook groups into Professions, paired with live profession skill levels). The Skills tab dedupes by skill id (server `GROUP BY`, client highest-wins) and no longer lists professions. Talents render the three class trees in DBC page order (1.12 frame left-to-right), each as a 7-tier × 4-column grid from Talent/TalentTab row/column.

## Diagnostic report (recommended for bug reports)

The dashboard Server panel has a **Copy diagnostic report** button producing a compact plain-text (markdown) snapshot: module/core versions, effective rates and bot flags, pool health (tracked/gaining, median/total XP/hour, deaths/min, died-5min %, combat/grind %, level bands), pool activity counters (quests/loot/money/kills/deaths/ghost time/services), issue counts, and server freshness. It contains no secrets (no IPs, hosts, account names, passwords). Paste it into a GitHub issue or Discord when reporting bugs — it answers "how is this server configured" without config-file archaeology.

## Validation

No host Go toolchain is assumed: `docker run --rm -v "$PWD/tools/observability:/src" -w /src golang:1.22-alpine sh -c 'go vet ./... && go test ./...'`. Live check: the server logs `Observability telemetry active`, and `/metrics` (default port 8095) shows `mangos_server_online 1` and a rising `tortoisebots_snapshots_total`.

Player/operator guide: [`docs/guides/observability-dashboard.md`](../../docs/guides/observability-dashboard.md).
