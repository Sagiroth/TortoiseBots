# Observability daemon — developer notes

`tools/observability` is a standalone Go daemon (Prometheus + web dashboard) fed by `runtime/ObservabilityEmitter.{h,cpp}` over non-blocking UDP. It is optional and config-gated; core stays ignorant of it.

Rules that keep its state honest:

- The daemon has exactly one authoritative store (`internal/state`). REST, WebSocket, and Prometheus all read it.
- The emitter sends self-contained snapshot cycles: one `HEARTBEAT` + `BOT_BATCH` chunks sharing a `seq`. Publish a roster only from a complete cycle; clients replace their roster wholesale instead of merging deltas.
- Every datagram also carries a `session` epoch (server process start). The daemon resets sequence/roster state when it changes, so a server restart that resets `seq` cannot be locked out as "old cycles".
- Bound and prune every emitter table (bot tracking, action failures, anomaly cooldowns) on each snapshot. Macro-state ratios are windowed, never lifetime totals.
- Anomaly types are a closed set (`model.AcceptedAnomalyTypes`) so Prometheus label cardinality stays bounded.
- Bump `kProtocolVersion` in `ObservabilityEmitter.cpp` and `model.ProtocolVersion` in `internal/model/types.go` together.

## Telemetry surface (protocol v6)

Each `BOT_BATCH` bot entry carries: `name, guid, class, role, level, xp, next_xp, hp/max_hp, power/max_power, power_type, map, zone, x/y/z/o, target, target_level, strategy, state, last_action, last_trigger, travel_purpose, travel_to`.

- `xp` / `next_xp` are `PLAYER_XP` / `PLAYER_NEXT_LEVEL_XP` (progress into the current level); the roster and bot drawer render them as a progress bar. `next_xp == 0` means "no data" (old emitter, max level) and renders as `–`, never a fake bar.
- `target_level` is the combat target's level for creature targets (0 = none/self/non-unit); it renders next to the target name. `target` itself is unchanged (the bot's own name still means "no hostile target", shown as `self`).
- `travel_purpose` / `travel_to` are the active travel destination's short name (`grind`, `vendor`, ...) and title; empty when the bot is not travelling. Read from the cached AI value on the existing 2 s snapshot cadence — no DB, no world scan.
- The daemon derives per-bot `xp_per_hour` + `xp_gain_age_sec` from successive snapshots (30 min window, level-up aware, in `internal/state`), plus a pool-wide grinding rollup (`/api/v1/grinding`, `grinding` on every snapshot): bots gaining XP, median/total XP/hour, kills/min + % killed in 5 min (from `BOT_DEATH` anomalies), % in combat, % on grind travel, level bands. Prometheus: `tortoisebots_grinding_xp_per_hour_total`, `tortoisebots_grinding_bots_gaining`, `tortoisebots_grinding_kills_per_min`, `tortoisebots_grinding_pct_in_combat`.
- `SERVER_INFO` is a small datagram at startup and every 5 min carrying the *effective* running settings (live core rate getters + AiPlayerbot fields, never config files, no secrets): module version, core revision/date, uptime, max level, rates, bot flags, diagnostics. Stored latest-only (`/api/v1/server-info`, `server_info` WS event + stream bootstrap, `info` on every snapshot) and shown read-only in the dashboard Server panel (Rates / Bots / Diagnostics).
- `power_type` is the current resource (`mana`, `rage`, `energy`, `focus`, `happiness`); druids reflect their active form. Label bars by it, never hardcode "mana".
- `last_action`/`last_trigger` feed repeated-action detection; they are sampled per 2s snapshot, not per execution.
- Anomalies carry `guid` so the daemon can key episodes; accepted types are `STUCK`, `ACTION_LOOP`, `UNREACHABLE_TARGET`, `BOT_DEATH`.
- `STUCK` and `ACTION_LOOP` are counter-only (`tortoisebots_anomalies_total`): `STUCK` never enters the Incidents ring buffer (the 60 s `STUCK` issue episode is the surfaced signal) and `ACTION_LOOP` never opens an issue episode (cooldown-gated to one event/30 s, so it almost never reaches a surface gate).
- `BOT_DEATH` is emitted from `PlayerbotAI::OnDeath` (target/zone/position/level).
- Anomaly emitters that can persist (`UNREACHABLE_TARGET`) re-report every cooldown window so the daemon has a liveness signal; do not make them fire-once.
- `humans` counts live network-transport sessions with an in-world player (`World::GetAllSessions` + `HasNetworkTransport`). Headless bot sessions never enter the network map, so the old `sessions - bots` math is gone.

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

Dashboard UI: the Issues tab defaults to the ≥10 min (persistent) duration filter, hides the trigger column for anomaly rows (always empty — details carry the emitter text), and the resolved card shows "shown/total". The armory shows max-only power (no live current value exists) and the live telemetry zone for online bots.

Armory detail view: Spells and Professions are separate tabs (profession spells moved out of the spellbook groups into Professions, paired with live profession skill levels). The Skills tab dedupes by skill id (server `GROUP BY`, client highest-wins) and no longer lists professions. Talents render the three class trees in DBC page order (1.12 frame left-to-right), each as a 7-tier × 4-column grid from Talent/TalentTab row/column.

## Diagnostic report (recommended for bug reports)

The dashboard Server panel has a **Copy diagnostic report** button producing a compact plain-text (markdown) snapshot: module/core versions, effective rates and bot flags, pool health (tracked/gaining, median/total XP/hour, kills/min, killed-5min %, combat/grind %, level bands), issue counts, and server freshness. It contains no secrets (no IPs, hosts, account names, passwords). Paste it into a GitHub issue or Discord when reporting bugs — it answers "how is this server configured" without config-file archaeology.

## Validation

No host Go toolchain is assumed: `docker run --rm -v "$PWD/tools/observability:/src" -w /src golang:1.22-alpine sh -c 'go vet ./... && go test ./...'`. Live check: the server logs `Observability telemetry active`, and `/metrics` (default port 8095) shows `mangos_server_online 1` and a rising `tortoisebots_snapshots_total`.

Player/operator guide: [`docs/guides/observability-dashboard.md`](../../docs/guides/observability-dashboard.md).
