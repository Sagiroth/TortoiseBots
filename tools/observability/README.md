# Observability daemon — developer notes

`tools/observability` is a standalone Go daemon (Prometheus + web dashboard) fed by `runtime/ObservabilityEmitter.{h,cpp}` over non-blocking UDP. It is optional and config-gated; core stays ignorant of it.

Rules that keep its state honest:

- The daemon has exactly one authoritative store (`internal/state`). REST, WebSocket, and Prometheus all read it.
- The emitter sends self-contained snapshot cycles: one `HEARTBEAT` + `BOT_BATCH` chunks sharing a `seq`. Publish a roster only from a complete cycle; clients replace their roster wholesale instead of merging deltas.
- Player lag: the heartbeat carries `lag_p50`/`lag_p95` (Prometheus `mangos_player_lag_p50_ms`/`_p95_ms`): how long a player's action waits before the server handles it, from the world-tick lengths of the last 30 s. An action lands in a tick with probability proportional to its length and waits for the rest of it (`runtime/PlayerLagWindow.h`). Against a headless probe client at 2000 bots it read about 15 ms under the measured p50/p95 (the time into the next tick before the player's map is processed is not modelled). This is the server's share of what a player feels; their ping comes on top, and the in-game latency meter never includes it (the core answers `CMSG_PING` on the network thread). `diff_avg`/`diff_worst` are the plain mean and worst tick of the same window.
- Every datagram also carries a `session` epoch (server process start). The daemon resets sequence/roster state when it changes, so a server restart that resets `seq` cannot be locked out as "old cycles".
- Bound and prune every emitter table (bot tracking, action failures, anomaly cooldowns) on each snapshot. Macro-state ratios are windowed, never lifetime totals.
- Anomaly types are a closed set (`model.AcceptedAnomalyTypes`) so Prometheus label cardinality stays bounded.
- `SERVER_INFO`'s `bots` object mixes numbers (pool sizes, intervals, budgets) with `"0"`/`"1"` flag strings: `ServerInfoPayload.Bots` is `map[string]any`. Typing it as `map[string]string` made `encoding/json` reject the whole datagram (silently, via the listener's error return), which left the Server panel and diagnostic report permanently empty.
- Bump `kProtocolVersion` in `ObservabilityEmitter.cpp` and `model.ProtocolVersion` in `internal/model/types.go` together.

## Telemetry surface (protocol v8)

Each `BOT_BATCH` bot entry carries: `name, guid, class, role, level, xp, next_xp, hp/max_hp, power/max_power, power_type, map, zone, x/y/z/o, target, target_level, strategy, state, last_action, last_trigger, travel_purpose, travel_to, travel_status, travel_dist, ai_visits, ai_age_ms`.

Macro states (`state`, heartbeat `states`, `tortoisebots_state_ratio`): `combat` (in combat), `moving` (a movement generator owns the bot), `busy` (standing still but doing real work: looting, casting, sitting to eat/drink, or movement within the last 45 s), `stalled` (standing still for 45+ s whose only activity was churn — an active travel target or a changing last-action name, no movement/loot/cast/sit — i.e. standing with a destination and getting nowhere), `resting` (rest flag), `idle` (no movement, action, cast, loot, or active target for 45+ s — really doing nothing), `dead`. `stalled` exists so a bot parked with a travel target is no longer reported as busy. Per-tick cost is member reads + one action-name compare; the 3-min window is unchanged.
- `power_type` is the current resource (`mana`, `rage`, `energy`, `focus`, `happiness`); druids reflect their active form. Label bars by it, never hardcode "mana".
- `last_action`/`last_trigger` feed repeated-action detection; they are sampled per 2s snapshot, not per execution.
- Anomalies carry `guid` so the daemon can key episodes; accepted types are `STUCK`, `ACTION_LOOP`, `UNREACHABLE_TARGET`, `BOT_DEATH`.
- `STUCK` and `ACTION_LOOP` are counter-only (`tortoisebots_anomalies_total`): `STUCK` never enters the Incidents ring buffer (the 60 s `STUCK` issue episode is the surfaced signal) and `ACTION_LOOP` never opens an issue episode (cooldown-gated to one event/30 s, so it almost never reaches a surface gate).
- `BOT_DEATH` is emitted from `PlayerbotAI::OnDeath` (target/zone/position/level/strategy) and is the one anomaly exempt from the `(guid, type)` 30 s cooldown: it is an event, not a sustained condition, so two deaths in one window are two rows. Every other emitter keeps the cooldown.
- Dead bots report `hp: 0` in the roster snapshot (the core's `GetHealth()` returns 1 for a corpse, which made corpse bars read "1 HP").
- Anomaly emitters that can persist (`UNREACHABLE_TARGET`) re-report every cooldown window so the daemon has a liveness signal; do not make them fire-once.
- `SERVER_INFO` datagrams (startup + every 5 min) carry module/core versions, live core rates and `AiPlayerbot` flags from running getters — never config files. The dashboard Server panel renders them, and its **Copy diagnostic report** button turns them plus pool health (tracked/gaining, median/total XP/h, deaths/min, combat/grind %, level bands), issue counts and freshness into a paste-ready plain-text snapshot (no secrets) — the way to report issues.
- **Core revision**: the module probes the core checkout itself (`TortoiseBots.cmake`, `git -c safe.directory=…`) and passes it as `TORTOISEBOTS_CORE_REVISION`/`_DATE`; `ModuleVersion.h` falls back to the core's own `REVISION_HASH`/`REVISION_DATE`. The core's CMake detection uses a plain `git rev-parse`, which git refuses in the containerised dev builder (root-owned bind mount) so it silently bakes in "unknown"/epoch — hence the dashboard showing `core unknown 1970-01-01` for a core that was built that way.
- **Bot tuning settings** in `SERVER_INFO.bots` (all read from the running config, not files): `bot_loot_uncommon`, `bot_loot_rare`, `ah_market`, `auto_learn_trainer_spells`, `auto_learn_quest_spells`, `level_up_mounts`, `turtle_mount_at_level` — plus the pool/behaviour flags. The Server panel groups them as RATES / BOTS·POOL / BOTS·TUNING / BOTS·GROUPS / BOTS·WORLD / DIAGNOSTICS in one key/value row per setting, and the diagnostic report repeats them as a `- tuning:` line.
- `/api/v1/grinding` serves the pool rollup behind the Grinding panel: per-bot XP/h + last-gain age over a 30 min level-up-aware window, pool gaining %, median/total XP/h, deaths/min + died-in-5-min % (from `BOT_DEATH` anomalies), combat/grind %, and level bands. Bands are per level while the pool spans <= 10 levels, otherwise five-level buckets 1-5/6-10/.../56-60; each band carries `avg_item_level` + `gear_bots` (see the gear sweep below).
- `GET /api/v1/issues` serves the same `store.Issues()` episode snapshot the WebSocket pushes, so the Issues tab, sidebar badge and map glow recover after a missed event or a Refresh.
- Gear sweep (item level): `armory.GearRollup()` runs **one** character-DB query every 5 min (`cmd/server/main.go`, never per tick) — equipped slots 0-18 minus shirt (3) and tabard (18), joined to `world.item_template` — and folds it per bot into average item level + quality census (`grey/white/green/blue/epic`, epic-or-higher bucketed into `epic`). `Store.SetGear` replaces the map wholesale (it is DB truth, not session state); each roster snapshot then carries `gear` per bot, level bands average it, and the armory profile computes the same summary from the profile's own equipment rows (identical slot rule). The dashboard shows it as the roster **Gear** column, the armory "Equipped Gear" summary, and the pool panel's `ilvl x.x · y.y×` per level band; the diagnostic report has a `- gear:` line.
- `BOT_EVENTS` datagrams carry the activity stream: the module mirrors a whitelist of `bot_events.csv` rows (`PlayerbotAIConfig::logEvent` → `ObservabilityEmitter::EmitBotActivity`) between the heartbeat and the roster batches of a cycle, so the roster published by the same cycle already carries the refreshed counters. Whitelisted: quest accept/complete/turn-in/drop/travel, `StoreLootAction`, `GatherLoot`, `LootMoney`, `SellAction`, `BuyAction`, `RepairAllAction`, `TrainerAction`, `NearbyService` (kind = trainer/vendor/turn-in), `AhAction`, `AhBidAction`, `BotDeath`, the revive/repop events (ghost time), `ReachGiveUp`, and an explicit `Kill` event from `XpGainAction` — the CSV row cannot carry the kill/non-kill XP flag. Item events are enriched server-side with `ItemPrototype` quality/sell/buy (no DB); every event carries the bot's copper, from which the daemon derives earned/spent. The queue is bounded (2048 rows, dropped on overflow) and chunked 40/datagram; `logEvent` forwards even when `bot_events.csv` is not in `AllowedLogFiles`.
- **Quest completion** rides on `QuestCompleted` (emitted by `BotPlayerAdapter` from the core's own quest-complete hook), not on `QuestUpdateCompleteAction`: the latter is packet-driven and never fires for a headless bot session, so `quests_completed`/`open_quests` were structurally zero on the live dashboard. The daemon counts both names as "complete, not yet handed in".
- `/api/v1/activity` (summary + per-bot counters + level timeline), `/api/v1/activity/loot` (`min_quality`/`class`/`min_level`/`max_level`/`bot`/`limit`), `/api/v1/activity/quests` (`bot`/`limit`) serve the Activity tab. Per-bot counters are also embedded in every roster snapshot as `activity`. All activity state is in-memory and bounded by the live population (activity is pruned with the roster; counters reset on session change or roster wipe); no DB reads.
- **Counter window**: `summary.since`/`summary.since_str` report the start of the game-server session every counter covers, and the dashboard labels every counter panel with it ("since 10:24") — a bare `19 loot` must never read as a lifetime total. Feed rows (`/api/v1/activity/loot`, `/activity/quests`, the level timeline) carry `time_str`, the daemon's own rendering of the row timestamp, because formatting the epoch in the browser made the feeds disagree with the incident table (which has always used the daemon's time).
- **Persistence**: the rollup is snapshotted to one JSON file once a minute (`--activity-state-file`, default `<ICON_CACHE_DIR>/activity-state.json`, empty disables) and restored at startup, so a dashboard restart no longer resets the counters. The snapshot carries the game-server session; a snapshot from a different session is dropped by the first datagram (`beginSessionLocked`) rather than leaking the previous pool's numbers into a restarted game server.

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
- Cumulative session totals (`GET /api/v1/anomalies/totals`, same auth): per-type counts since the game-server session started plus the per-`last_action` breakdown for `ACTION_LOOP` / `UNREACHABLE_TARGET`, with `since`/`since_str` marking the window. Whole-run counts (`ACTION_LOOP` rows for the KPI report) come from here. Totals reset with the session (or a roster wipe); the dashboard's Clear wipes the feed only, and the Issues tab shows them as one totals line.

Seven tabs, each answering one question, with no panel in two places: **Overview** (is everything running: player lag, bots/players, snapshot age, issues, population and player-lag charts, the live-state census whose rows jump to the Bots list, and a short needs-attention list), **Bots** (the one list of bots), **Map** (zone census chips + world/zone map), **Progress** (pool XP/death KPIs, level bands with gear item level, level-up timeline), **Activity** (counters grouped by theme plus one feed panel switching between quests, loot and the live console), **Issues** (KPIs, issues over time/by zone, then one panel switching between active issues, recently resolved and the incident feed) and **Server** (effective settings, diagnostic report).

- **One bot list.** `Bots` joins the database list (`GET /api/v1/armory/bots`: every pool bot, online or offline, refreshed on tab activation and once a minute) with the live roster by guid. Live telemetry wins for everything it carries; offline bots show database values (level, spec, gold) and a dash for the rest. State and class chips double as the census and filters; the Live / Progress / Activity switch only changes the columns (the old per-bot activity table is the Activity columns). Default order is online first, then name.
- **One bot profile.** A slide-over opened from any tab (list row, map marker, issue/feed bot name, the topbar find-a-bot box or `/`): Overview (live status, issues, recent quest/loot events), Gear & stats, Talents, Spells, Professions, Skills, Bags, Activity. On the map it is a narrow side panel and the backdrop is click-through so another marker can be picked.
- **Routing.** The URL hash is the single source of truth: `#/<tab>` or `#/<tab>?bot=<guid>&p=<profile tab>`. Back, reload and pasted links land on the same view; without a hash the last tab (kept in `localStorage`) is restored.
- Only the visible tab renders on each snapshot (list/map do the heavy DOM work and are skipped while hidden). Counters keep their session window label ("since 10:24"). The Issues table defaults to the ≥5 min filter so the 5–10 min "watch" cohort the card counts is visible (rows without a duration are kept, not hidden), and its "Deaths" card counts the `BOT_DEATH` rows currently in the incident list. The Gear column shows the swept average item level (quality split in the tooltip), and the level bands show `ilvl` with its ratio per level. The profile shows live health/power/XP for online bots (the armory DB has no live values), a max-only power bar for offline ones, and an "Equipped Gear" summary (average item level + quality split) above the paperdoll.

Bot profile: Spells and Professions are separate tabs (profession spells moved out of the spellbook groups into Professions, paired with live profession skill levels). The Skills tab dedupes by skill id (server `GROUP BY`, client highest-wins) and no longer lists professions. Talents render the three class trees in DBC page order (1.12 frame left-to-right), each as a 7-tier × 4-column grid from Talent/TalentTab row/column.

## Diagnostic report (recommended for bug reports)

The dashboard Server panel has a **Copy diagnostic report** button producing a compact plain-text (markdown) snapshot: module/core versions, effective rates and bot flags, the bot-tuning line (XP rate, loot rates, autolearn trainer/quest, level-up mounts, AH market), pool health (tracked/gaining, median/total XP/hour, deaths/min, died-5min %, combat/grind %, level bands with their average item level), a pool gear line (swept bots, average item level, equipped-piece quality census), pool activity counters (quests/loot/money/kills/deaths/ghost time/services), issue counts, and server freshness. A flag the running server did not report reads `?`, never a false "off". It contains no secrets (no IPs, hosts, account names, passwords). Paste it into a GitHub issue or Discord when reporting bugs — it answers "how is this server configured" without config-file archaeology.

## Map artwork (`web/maps`)

The zone/continent images are the client's WorldMap texture. The client draws a
zone as 12 base tiles (a 4x3 grid of 256px textures, `Interface\WorldMap\<Zone>\<Zone><n>.blp`)
plus one `WorldMapOverlay.dbc` texture per sub-area that paints in the revealed
detail — exporting the base tiles alone leaves every zone looking fogged.
`scripts/gen_zone_maps.py` composites both at their DBC offsets and rewrites
`web/maps/*.webp` **at the existing file sizes**: the daemon's world->map
percentages are relative to that canvas. Maps of Mystery artwork (custom zones)
is hand-drawn and must not be regenerated. Run it against a client Data
directory when the art needs refreshing; the script needs `mpyq` and `Pillow`.

## Validation

No host Go toolchain is assumed: `docker run --rm -v "$PWD/tools/observability:/src" -w /src golang:1.22-alpine sh -c 'go vet ./... && go test ./...'`. Live check: the server logs `Observability telemetry active`, and `/metrics` (default port 8095) shows `mangos_server_online 1` and a rising `tortoisebots_snapshots_total`.

Operators without Go use the prebuilt binaries instead of building: every
daily release (`vYYYY-MM-DD`) carries
`tortoise-observability-linux-amd64` and
`tortoise-observability-windows-amd64.exe`, built by
`.github/workflows/dashboard-binaries.yml` (`go vet` + `go test` +
`CGO_ENABLED=0` static builds; also triggered by emitter wire changes).
Per-merge tags are plain git tags, not Releases, so binaries attach only to
the daily release (the workflow waits for it). The
one-command setup (`run-dashboard.sh` / `run-dashboard.ps1`) downloads the
binary, wires DB env from `mangosd.conf`, enables telemetry in
`aiplayerbot.conf`, and starts the daemon — see the operator guide above.

Player/operator guide: [`docs/guides/observability-dashboard.md`](../../docs/guides/observability-dashboard.md).
