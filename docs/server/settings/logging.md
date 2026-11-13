---
id: server-settings-logging
title: Logging & telemetry settings
category: server
summary: Configuration settings for module diagnostic log levels, CSV event logs, per-bot action logs, action counts, and retention.
tags: [config, logging, telemetry, diagnostics]
relates_to:
  - server-dashboard
  - server-settings-performance
---

# Logging & Telemetry Settings

These settings configure server diagnostic logging, event CSV files, per-bot action traces, and action performance analytics. Settings go in `aiplayerbot.conf` and `modules/tortoise_bots.conf`; see [How configuration works](../configuration.md).

The native module has its own independent logging system. This allows administrators to trace bot behavior in detail without enabling heavy core server engine debugging, and vice versa.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`TortoiseBots.LogLevel`](#tortoisebotsloglevel) | `2` (Detail) | Sets module logging verbosity independently of the core server log level. |
| [`AiPlayerbot.AllowedLogFiles`](#aiplayerbotallowedlogfiles) | `bot_events.csv,deaths.csv` | Comma-separated list of structured diagnostic CSV files enabled for writing. |
| [`AiPlayerbot.RaidBossDiagnostics`](#aiplayerbotraidbossdiagnostics) | `0` | Opt-in raid boss threat snapshots, spell-hit observations and bot speed-change acks in `bot_events.csv`. |
| [`AiPlayerbot.LogRetentionDays`](#aiplayerbotlogretentiondays) | `3` | Days to keep compressed historical CSV logs before automatic deletion. |
| [`AiPlayerbot.UpdateCheck`](#aiplayerbotupdatecheck) | `1` | Looks up the newest TortoiseBots and addon releases on GitHub once at server start. |
| [`AiPlayerbot.EnableActionLog`](#aiplayerbotenableactionlog) | `1` | Writes dedicated per-bot execution logs under `logs/bots/`. |
| [`AiPlayerbot.ActionLogScope`](#aiplayerbotactionlogscope) | `commanded` | Filters which bots generate action log files (`commanded`, `all`, `none`). |
| [`AiPlayerbot.ActionLogMaxAgeDays`](#aiplayerbotactionlogmaxagedays) | `3` | Maximum age in days for per-bot action log files before cleanup. |
| [`AiPlayerbot.ActionLogMaxTotalMB`](#aiplayerbotactionlogmaxtotalmb) | `2048` | Maximum cumulative disk space in megabytes for the `logs/bots/` folder. |
| [`AiPlayerbot.ActionLogMaxFileMB`](#aiplayerbotactionlogmaxfilemb) | `100` | Maximum size in megabytes of a single bot log file before rotating. |
| [`AiPlayerbot.ActionCountsLog`](#aiplayerbotactioncountslog) | `0` | Records cumulative action execution counts and failure rates to CSV. |
| [`AiPlayerbot.HealerDiagnostics`](#aiplayerbothealerdiagnostics) | `0` | Writes grouped-healer snapshots and failed heal checks to `bot_events.csv`. |
| [`AiPlayerbot.Observability`](#aiplayerbotobservability) | `1` | Enables non-blocking UDP telemetry stream to the observability daemon. |
| [`AiPlayerbot.ObservabilityHost`](#aiplayerbotobservabilityhost) | `127.0.0.1` | Destination host IP address or service name for UDP observability telemetry. |
| [`AiPlayerbot.ObservabilityPort`](#aiplayerbotobservabilityport) | `9195` | Destination UDP port for the observability telemetry daemon. |
| [`AiPlayerbot.RunTrace.Enabled`](#aiplayerbotruntraceenabled) | `0` | Opt-in run recorder (`.bot trace`): a party's run as a `bt-trace` v1 JSONL file plus a summary. |
| [`AiPlayerbot.RunTrace.IntervalMs`](#aiplayerbotruntraceintervalms) | `500` | Milliseconds between two frames of a run trace. |
| [`AiPlayerbot.RunTrace.GmOnly`](#aiplayerbotruntracegmonly) | `1` | Only a GameMaster may start a run trace. |
| [`AiPlayerbot.RunTrace.Dir`](#aiplayerbotruntracedir) | `traces` | Folder for run traces and their summaries (relative to `LogsDir`). |
| [`AiPlayerbot.RunTrace.MaxFileMb`](#aiplayerbotruntracemaxfilemb) | `64` | Size cap of one run trace file in megabytes. |
| [`AiPlayerbot.ObservabilityDungeonMobs`](#aiplayerbotobservabilitydungeonmobs) | `1` | Sends the mobs, threat and target lines of every dungeon run to the dashboard's Dungeons tab. |
| [`AiPlayerbot.ObservabilityRaids`](#aiplayerbotobservabilityraids) | `0` | Sends the raid groups of every raid map copy (leader, offline members, live fight window) to the dashboard's Raids tab without a commander bind. |
| [`AiPlayerbot.ObservabilityRaidsDetail`](#aiplayerbotobservabilityraidsdetail) | `0` | Appends the ready check and the boss debuffs to the raid groups (sent at most every 5 s). |
| [`AiPlayerbot.ObservabilityRealtime`](#aiplayerbotobservabilityrealtime) | `1` | Streams the bound groups' fight events (deaths, pulls, taunts, interrupts, casts, kill/wipe) to the dashboard as they happen. |
| [`AiPlayerbot.ObservabilityRealtime.Events`](#aiplayerbotobservabilityrealtimeevents) | `1` | Turns the fight-event stream on or off without touching the rest of the realtime feed. |
| [`AiPlayerbot.ObservabilityRealtime.EventGapMs`](#aiplayerbotobservabilityrealtimeeventgapms) | `250` | Minimum milliseconds between two event datagrams of one commander (100..2000). |
| [`AiPlayerbot.ObservabilityRealtime.CombatMs`](#aiplayerbotobservabilityrealtimecombatms) | `500` | Milliseconds between two position reports of a fighting commander group (250..2000). |
| [`AiPlayerbot.ObservabilityRealtime.IdleMs`](#aiplayerbotobservabilityrealtimeidlems) | `2000` | Milliseconds between two position reports out of combat (250..2000; `2000` = only the regular 2 s report). |

## Settings

### `TortoiseBots.LogLevel`
Controls module logging verbosity in `modules/tortoise_bots.conf`.
**Default:** `2` (Detail) · **Values:** `0` Minimal, `1` Basic, `2` Detail, `3` Debug · **Restart:** not needed (`.reload config` applies live)

<details>
<summary>How it works in detail</summary>

This setting lives in `modules/tortoise_bots.conf` and governs logging output for all bot lifecycle systems, the Auction House service, battleground queues, and Looking for Turtles backfill:

- `0` (Minimal): Logs fatal errors and critical issues only.
- `1` (Basic): Logs startup and shutdown events, diagnostic test results, and periodic `BOTPERF` performance lines.
- `2` (Detail): Default level. Logs per-bot state transitions, logins, logouts, party joins, auction postings, and battleground queue movements.
- `3` (Debug): Logs high-volume per-tick evaluations and network packet traces. Intended for short diagnostic sessions.

Errors are always logged regardless of this level. This setting is re-read dynamically when running `.reload config` in the server console, allowing log levels to be adjusted without restarting the world server.

</details>

### `AiPlayerbot.AllowedLogFiles`
Specifies which structured CSV event log files are written to the server's logs directory.
**Default:** `bot_events.csv,deaths.csv` · **Values:** comma-separated filenames · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the active CSV log streams:

- `bot_events.csv`: Records high-level bot behavioral events, travel destination changes, vendor trips, and unstuck interventions.
- `deaths.csv`: Records character deaths, death position, killer identity and level, other attackers, and travel destination.
- `pvp_seek.csv`: Records open-world PvP seek-role grants and new seek engagements (bot, target, level, map, zone).
- Additional diagnostic logs (such as `travel_route_gate.csv` and `ghost_moves.csv`) can be added to the list as needed.

Setting this key to an empty string (`""`) disables all CSV event logging. An omitted key falls back to `bot_events.csv,deaths.csv`; set it explicitly to empty to disable.

If a file cannot be written (for example, if the directory is missing or permissions are denied), the server logs an error, backs off for 5 minutes, and retries.

</details>

### `AiPlayerbot.LogRetentionDays`
Number of days historical compressed CSV log files are retained on disk.
**Default:** `3` · **Values:** days (0 = keep indefinitely) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Active CSV logs are automatically compressed into `<filename>_<date>_<time>.csv.gz` archives every hour and at server startup. Packing occurs in the background; a packing operation stopped mid-pack finishes on the next server start.

Compressed archives older than `LogRetentionDays` are automatically purged during background cleanup passes.

Setting this to `0` disables automated deletion, preserving all archives permanently.

</details>

### `AiPlayerbot.UpdateCheck`
Looks up the newest TortoiseBots and TortoiseBotsManager releases on GitHub once at server start.
**Default:** `1` · **Values:** `0` (never connect), `1` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The lookup runs once in the background with `curl` (standard on Linux and Windows 10+) and a 5-second limit. When a newer module exists, the server log and every player's login message say so. Players also see the newest addon version at login, and the addon shows an "update available" note when theirs is older. Offline servers, a missing `curl` or a failed lookup change nothing.

</details>

### `AiPlayerbot.EnableActionLog`
Enables verbose per-bot execution logs written to the `logs/bots/` directory.
**Default:** `1` · **Values:** `0` disabled, `1` enabled · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, the server generates detailed execution logs for individual bots at `logs/bots/<botname>_acc<id>_<timestamp>.log`. These logs track decision-making, target evaluations, and ability priority evaluations.

Because logging all bots simultaneously can produce gigabytes of data daily, file creation is filtered by `ActionLogScope` and capped by the retention limits below.

A failed move line (`A:move to position - FAILED ...`) ends with:
- `why=<reason>` (fail site of the tried move: `dispatch-short`, `empty-path`, `cantmove`; `no-move-try` when no move was tried)
- `water=` and `under=` (bot in or under water)
- `swim=` (swimming flag)
- `path=0x..` (core PathFinder type for that try; `0x0` when the route was reused)

Strategy AI action traces can also be enabled on an individual bot in-game via `.bot strategy +debug` (executed actions; also add `debug action` for considered actions). Both still need `EnableActionLog=1`, scope, and `LogInGroupOnly`.

</details>

### `AiPlayerbot.ActionLogScope`
Filters which categories of bots generate per-bot action log files.
**Default:** `commanded` · **Values:** `commanded`, `all`, `none` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls the scope of active action logs:

- `commanded`: Generates log files only for bots in parties bound to a remote commander (`.bot commander bind`) or hired companions. Masterless pool bots are ignored.
- `all`: Logs every bot on the server, including roaming pool bots. Use caution, as this generates significant disk I/O.
- `none`: Completely disables per-bot log generation.

Any unknown word logs an error and uses `commanded`.

Eligibility is re-evaluated every 15 seconds. If a bot is hired or bound, logging begins within 15 seconds; when dismissed, its log file closes cleanly.

Detailed per-action lines are further filtered by [`AiPlayerbot.LogInGroupOnly`](performance.md#aiplayerbotlogingrouponly), which restricts action detail messages to grouped bots.

</details>

### `AiPlayerbot.ActionLogMaxAgeDays`
Maximum age in days for per-bot action log files before automatic deletion.
**Default:** `3` · **Values:** days (0 = no age limit) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Files in `logs/bots/` whose last modification exceeds this age in days are automatically deleted during periodic cleanup cycles.

</details>

### `AiPlayerbot.ActionLogMaxTotalMB`
Total disk storage limit in megabytes for the `logs/bots/` directory.
**Default:** `2048` · **Values:** megabytes (0 = unlimited) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Places a hard ceiling on cumulative storage for the `logs/bots/` folder (default 2 GB). When folder size exceeds this limit, the oldest closed log files are pruned first until disk usage falls below the threshold. Open files currently in use by active bots are never deleted.

Cleanup executes at server startup, every hour on the world thread, or whenever bots write 25% of the total size limit.

</details>

### `AiPlayerbot.ActionLogMaxFileMB`
Maximum size in megabytes of a single bot action log file before rotating to a new file.
**Default:** `100` · **Values:** megabytes (0 = unlimited) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When an individual bot log file reaches this size, the file notes this on its last line, closes cleanly, and logging continues in a new numbered file for that character.

</details>

### `AiPlayerbot.ActionCountsLog`
Records cumulative action execution counts and success/failure rates to CSV.
**Default:** `0` · **Values:** `0` disabled, `1` enabled · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, every executed bot action is tracked by class and action name, tallying successful versus failed executions. Every 5 minutes, a cumulative snapshot (`utc_time,class,action,ok_count,fail_count`) is appended to `logs/action_counts.csv`.

Because the file is written while the server is active, copy twice and compare before using a live copy.

When disabled (`0`), the tracking branch costs negligible CPU time.

Administrators can analyze rotation efficiency and identify failing actions by taking snapshots over a measurement window and comparing them with the analysis script:

```bash
cp logs/action_counts.csv /tmp/actions_early.csv
# Wait 30+ minutes during active server gameplay...
cp logs/action_counts.csv /tmp/actions_late.csv
python3 tools/action_counts_report.py /tmp/actions_early.csv /tmp/actions_late.csv --top 10
```

</details>

### `AiPlayerbot.HealerDiagnostics`
Writes grouped-healer snapshots and failed heal checks to `bot_events.csv`.
**Default:** `0` · **Values:** `0` disabled, `1` enabled · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Only acts when `bot_events.csv` is allowed, and only for a grouped healer:
- at most every 10 s, one `HealState` row (injured members, heal range, mana, movement, casting, lowest health, distance, line of sight, motion, destination, last action)
- at most every 10 s, one `HealSkip` row (why no heal: range, line of sight, mana, moving, bad target)
- reads the group roster only, never the world; changes no decision

</details>

### `AiPlayerbot.Observability`
Enables the non-blocking UDP telemetry emitter streaming bot lifecycle events to the observability daemon.
**Default:** `1` · **Values:** `0` disabled, `1` enabled · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled in `modules/tortoise_bots.conf` or `aiplayerbot.conf`, the server emits structured telemetry packets over UDP to the external observability collector.

Telemetry feeds the live [Observability Dashboard](../dashboard.md), including real-time bot locations, combat engagements, level-up milestones, deaths, and dungeon runs without writing excessive logs to disk.

Because UDP emission is non-blocking, disabling or losing connection to the daemon never blocks the game server tick.

</details>

### `AiPlayerbot.ObservabilityHost`
Destination hostname or IP address for the UDP observability telemetry collector.
**Default:** `127.0.0.1` (built in; the key is not in `aiplayerbot.conf.dist.in` — add it to `aiplayerbot.conf` to change it) · **Values:** IP address or hostname · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Specifies the network destination where UDP telemetry packets are routed. An empty string defaults to loopback (`127.0.0.1`).

In containerized environments, set this to the service name of the observability container (e.g. `observability`) or specify it via the `OBSERVABILITY_HOST` environment variable.

</details>

### `AiPlayerbot.ObservabilityPort`
Destination UDP port listened to by the observability daemon.
**Default:** `9195` · **Values:** UDP port (1..65535) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Configures the destination UDP port for telemetry packet dispatch. Must match the listening port configured in the observability daemon service.

</details>

### `AiPlayerbot.ObservabilityDungeonMobs`
Sends the mobs of every dungeon run (with their threat and the bots' target lines) to the dashboard's Dungeons tab.
**Default:** `1` · **Values:** `0` disabled, `1` enabled · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Needs [`AiPlayerbot.Observability`](#aiplayerbotobservability) and `AiPlayerbot.CombatMonitor = 1`:
- while a bound group fights, sends `COMBAT_EVENTS` every `EventGapMs` (start, pulls, deaths, taunts, interrupts, elite and boss casts, kill or wipe)
- events often arrive before the `COMBAT_FIGHT` summary
- `0` stops events and positions at once (use it to cut traffic on a busy server)
- a fight that outlives `.bot run stop` is still followed to its end and recorded

Once per 2 s telemetry cycle the server sends a `DUNGEON_MOBS` datagram built from the run members' own victims, attackers and cached lists, without a world scan: up to 48 mobs per party run and 64 per raid run, fewer when many runs share the 384-mob cycle budget, packed by whole runs into parts of at most 96 mobs and 8 runs.

</details>

### `AiPlayerbot.ObservabilityRaids`
Sends the raid groups of every raid map copy to the dashboard's Raids tab, without a commander bind.
**Default:** `0` · **Values:** `0` disabled, `1` enabled · **Restart:** not needed (`.reload config` applies live)

<details>
<summary>How it works in detail</summary>

Only acts while [`AiPlayerbot.Observability`](#aiplayerbotobservability) is on. Both `modules/tortoise_bots.conf` and `aiplayerbot.conf` carry the key and the feed is on only when both say `1`; setting it to `0` in either switches it off, and a `.reload config` applies the change without a restart.

This key adds, on top of the roster and `DUNGEON_MOBS` lines:
- the leader and the offline members
- the live fight window and finished fights of unlisted groups (window needs `AiPlayerbot.CombatMonitor = 1`)
- one `RAIDS` row per group per 2 s cycle, filed under the map copy a bot stands in
- per member: guid, subgroup, name, offline flag
- while the group fights: the window raw JSON
- when a row would pass the datagram cap, it first loses the window, then offline names, then members, and says so in `trimmed`
- turning the flag off with `.reload config` closes the open fight (marked unfinished) and sends `"raids":[]` once

</details>

### `AiPlayerbot.ObservabilityRaidsDetail`
Appends the ready check and the boss debuffs to the raid groups of the dashboard's Raids tab.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies live)

<details>
<summary>How it works in detail</summary>

Only acts while [`AiPlayerbot.ObservabilityRaids`](#aiplayerbotobservabilityraids) is on (both config files).

- The ready check (buffs, consumables, key cooldowns) and the boss debuffs of every online member ride a `detail` array at the END of each `RAIDS` group object, keyed by member guid. It comes after the window, so a dashboard that does not know the key ignores the tail.
- The collection reads the same per-bot data the bound path reads (ready cache, aura holders), at most every 5 s. Cycles between two detail reads repeat the last read.
- With the key off the card says "detail off (AiPlayerbot.ObservabilityRaidsDetail)".

</details>

### `AiPlayerbot.ObservabilityRealtime`
Streams a commander group's fight events to the dashboard as they happen.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies live)

<details>
<summary>How it works in detail</summary>

Needs [`AiPlayerbot.Observability`](#aiplayerbotobservability) and `AiPlayerbot.CombatMonitor = 1`:

- while a bound group fights, sends `COMBAT_EVENTS` every `EventGapMs`
- each event carries start, pulls, deaths, taunts, interrupts, elite and boss casts, kill or wipe
- events often arrive before the `COMBAT_FIGHT` summary
- `0` stops events and positions at once (use it to cut traffic on a busy server)
- a fight that outlives `.bot run stop` is still followed to its end and recorded

</details>

### `AiPlayerbot.ObservabilityRealtime.Events`
Turns the fight-event stream on or off on its own.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies live)

<details>
<summary>How it works in detail</summary>

Only acts with [`AiPlayerbot.ObservabilityRealtime`](#aiplayerbotobservabilityrealtime) on. With it at `0` the module keeps the regular 2 s telemetry (and any faster position reports) but sends no `COMBAT_EVENTS`; the finished fight still arrives as a `COMBAT_FIGHT` summary.

</details>

### `AiPlayerbot.ObservabilityRealtime.EventGapMs`
Minimum time between two event datagrams of the same commander group.
**Default:** `250` · **Values:** `100`..`2000` (milliseconds, clamped) · **Restart:** not needed (`.reload config` applies live)

<details>
<summary>How it works in detail</summary>

Events are queued per group and drained every 100 ms, but one datagram per group goes out at most once per this interval; whatever happened in between rides the next one. A higher value means fewer, fuller datagrams; the fight header (state and `fight_ms`) rides every one. While a fight is open and nothing was sent for 5 s, a keepalive with an empty event list keeps the dashboard's timer live.

</details>

### `AiPlayerbot.ObservabilityRealtime.CombatMs`
How often a fighting commander group's positions are sent, in milliseconds.
**Default:** `500` · **Values:** `250`..`2000` (clamped) · **Restart:** not needed (`.reload config` applies live)

<details>
<summary>How it works in detail</summary>

While at least one bot of a bound group is in combat, the module sends one `COMMANDER_POS` datagram per group every this many milliseconds with the group's bots (place, facing, health, alive) and the enemies the lead's last 2 s report listed (place, health, victim). The report is a few field reads and one guid lookup per watched enemy, with no grid or world scan. Casts do not ride this report; their start arrives as a `COMBAT_EVENTS` line.

Only acts with [`AiPlayerbot.ObservabilityRealtime`](#aiplayerbotobservabilityrealtime) on and needs [`AiPlayerbot.Observability`](#aiplayerbotobservability). At most 40 bots and 24 enemies per group per datagram.

</details>

### `AiPlayerbot.ObservabilityRealtime.IdleMs`
How often positions are sent while a bound group does not fight, in milliseconds.
**Default:** `2000` · **Values:** `250`..`2000` (clamped) · **Restart:** not needed (`.reload config` applies live)

<details>
<summary>How it works in detail</summary>

The regular 2 s `COMMANDERS` report already carries every standing group, so the default `2000` sends no out-of-combat positions at all. A lower value adds `COMMANDER_POS` datagrams for the groups that are not fighting on their own period, which costs traffic on an idle server and is meant for a dashboard watching a group walk.

Only acts with [`AiPlayerbot.ObservabilityRealtime`](#aiplayerbotobservabilityrealtime) on and needs [`AiPlayerbot.Observability`](#aiplayerbotobservability).

</details>

---

### `AiPlayerbot.RunTrace.Enabled`
Opt-in run recorder (`.bot trace`): a party's run as a `bt-trace` v1 JSONL file plus a summary.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Run trace (opt-in):** records a party run for offline scoring:
- who: a GM, or the player who controls (or commands) the bots
- how: `.bot trace start <run_id> [party|bot <Name>]`
- what: a `bt-trace` v1 JSONL file plus `<run_id>.summary.json` (hp, power, position, target, strategy, hostiles, actions, deaths, edges, loot)
- score it with `tools/trace_score.py`
- costs nothing while no run is active; writes on its own thread
- with `0` the command answers that it is off, and a run in progress ends at the next tick
- see [run trace](../run-trace.md) and [`.bot trace`](../../play/commands/bot-panel.md#bot-trace)

</details>

### `AiPlayerbot.RunTrace.IntervalMs`
Milliseconds between two frames of a run trace.
**Default:** `500` · **Values:** `250`-`10000` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Run trace frame period:** milliseconds between two frames of a run. Clamped to 250-10000.

</details>

### `AiPlayerbot.RunTrace.GmOnly`
Only a GameMaster may start a run trace.
**Default:** `1` · **Values:** `0` the controlling player may too, `1` GMs only · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Run trace: who may start one:** with `1` only a GameMaster may start a trace (each run takes up to `MaxFileMb` of disk); with `0` the player who controls (or commands) the bots may too.

</details>

### `AiPlayerbot.RunTrace.Dir`
Folder for run traces and their summaries (relative to `LogsDir`).
**Default:** `traces` · **Values:** a path · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Run trace output directory:** where `<run_id>.jsonl` and `<run_id>.summary.json` are written. A relative path is under `LogsDir`.

</details>

### `AiPlayerbot.RunTrace.MaxFileMb`
Size cap of one run trace file in megabytes.
**Default:** `64` · **Values:** `1`-`1024` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Run trace size cap:** megabytes per trace file (clamped to 1-1024). When reached the run ends (end reason `size_cap`) with its end record intact.

</details>

## Log Rotation & Previous-Run Logs

### Previous-Run Log Retention (packed archives)

Enabled logs opened in write mode (including `bot_events.csv` and startup travel and pathfinding dumps) pack the previous run's file at server start into `<name>_<date>_<time>.csv.gz` next to the live file, then start fresh.

- Packing of the previous run happens once per filename per process. Later opens in the same run truncate the live file and leave existing packs alone.
- Append-only log streams (such as `deaths.csv`) keep appending instead of truncating.
- No CSV headers or column structures are altered.
- If packing or rotation fails (for example, due to filesystem permissions), an error is logged, the existing file is preserved, and writes to that log are skipped for 5 minutes before the next retry.
- Disabled logs are left untouched.

## AiPlayerbot.RaidBossDiagnostics

Default: `0` (disabled). Set to `1` and include `bot_events.csv` in
`AiPlayerbot.AllowedLogFiles` to record engaged raid bosses (living, in-combat, non-evading world bosses in a raid map). `BossThreat` rows
sample the current victim and top five numeric threat entries at most once per
two seconds per boss/instance. `BossSpellHit` rows record supported victim-session
spell-damage packets. `hp_sample` is sampled at packet
send and `hp_est` is clamped subtraction of reported damage; neither guarantees
exact post-hit health. `SpeedChange` rows record a bot answering a forced speed
change: info1 is the move type (`walk`, `run`, `run_back`, `swim`, `swim_back`,
`turn_rate`) and info2 is `rate=<applied rate>|counter=<packet counter>|slowed=<1
while a speed-reducing aura is worn>`, at most one row per bot per second. They
show whether a slow aura applies and clears at once on a bot (the Sand Storm's
Harsh Winds pulses every second). Disable the option after collecting a diagnostic capture.
See [column definitions and coverage](../../developers/host-api.md#csv-columns-unchanged-nine-column-layout).
