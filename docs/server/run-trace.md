---
id: server-run-trace
title: Run Trace (opt-in recorder and scorer)
category: server
summary: Record what a party of bots did during one run as a bt-trace v1 JSONL file plus a per-run summary, and score it offline.
tags: [guide, trace, recorder, oracles, testing, schema]
relates_to:
  - server-configuration
  - play-commands
  - server-dashboard
  - developers-architecture
---

# Run Trace

> Work in progress. These features are not officially released and not supported yet. Enable them at your own risk. Until the official release, bug reports and feedback about them are not handled through the issue tracker; contact Hypeer directly.

The run trace answers "what did the bots actually do in that run" with data instead of a watched replay. A GM, or the owner of the bots, starts a **run** with a name (the **run id**); the server writes one JSONL **trace** of the party's bots until the run is stopped, plus a small **summary** file. `tools/trace_score.py` scores a trace offline and writes its verdicts into the summary, so a build can be compared with the next one on the same scenario.

It is **off by default** (`AiPlayerbot.RunTrace.Enabled = 0`) and costs nothing while no run is active. The schema is `bt-trace` version 1; an external driver (the turtle-agent) writes the same shape from its own observations and passes its run id to `.bot trace start`, so both sides of one run share the id.

## Start, stop, status

```text
.bot trace start <run_id> [party|bot <Name>] [driver=<model>:<effort>:<run>]
.bot trace stop [<run_id>]
.bot trace status
```

See [`.bot trace`](../play/commands/bot-panel.md#bot-trace) for who may run them. `party` (the default) follows the bots of your party; for a [remote commander](../play/commands/commander.md) with a standing binding it follows the commanded group. `bot <Name>` follows one bot. One run at a time. A run also ends by itself when the size cap is reached (`size_cap`), when the group has no bots for 10 s (`scope_lost`), when the config switch or the bot AI (`AiPlayerbot.Enabled`) is turned off, e.g. by a config reload (`disabled`; a `start` with either off is refused) or at server shutdown (`shutdown`).

The scope is re-read every frame and permission is re-checked each time: a bot is recorded only while the player who started the run is online and is a GameMaster, controls that bot, or is the bound remote commander of its group, so a stranger's bots that join the party later are left out. If the starter logs out, the run ends by itself (`scope_lost`).

Who may start a run: with `AiPlayerbot.RunTrace.GmOnly = 1` (the default) only a GameMaster, since every run takes disk space; with `0` also the player who controls the bots. `.bot trace status` names the run id and scope to a GameMaster and to the player who started the run; anyone else is told only that a run is going. Stopping a run returns at once; its `end` record and summary are written a moment later, and a new `start` in that moment is refused (`finishing`).

`.bot trace stop <run_id>` stops the run only when it is that run's own trace: another run's id answers `other-run` and nothing is stopped. A plain `.bot trace stop` with no id stops whatever trace runs, as before.

`driver=<model>:<effort>:<run>` names what drove the run (the turtle-agent passes its model, effort and run id; a player leaves it out). It is written to the summary as `driver` and shown by the dashboard as self-reported: the server cannot verify it. It may stand anywhere after the run id, once, and takes 1-96 letters, digits and `. _ - : +`.

A run id is 1-64 characters of letters, digits, `.`, `_` and `-` (it becomes the file name), and a run id that already has a trace file is refused.

## Files

Under `AiPlayerbot.RunTrace.Dir` (default `traces`, relative to `LogsDir`):

| File | Content |
| :--- | :--- |
| `<run_id>.jsonl` | The trace: one JSON object per line. |
| `<run_id>.summary.json` | One JSON object per run, written when the run starts (`status: running`) and when it ends; `tools/trace_score.py` adds the verdicts. |

## Trace records

Every line is a flat JSON object with the same five leading fields: `"v":1` (schema version), `"k"` (the kind), `"run"` (the run id), `"src":"server"` (the producer; the turtle-agent writes `"agent"`) and `"t"` (milliseconds since the run started). GUIDs are strings, `"0x"` plus lower-case hex (a 64-bit GUID does not survive a JSON number). Readers ignore unknown kinds and fields, so fields can be added without a version bump; a changed meaning bumps `v`.

| `k` | When | Fields (besides the common five) |
| :--- | :--- | :--- |
| `header` | once, first | `schema` (`bt-trace`), `scope` (`party`/`bot`), `scope_name`, `requester`, `tb_version`, `start_unix`, `interval_ms`, `max_bytes`, `map`, `instance`, `caps` (what this producer records: `facing`, `moving`, `interrupts`, `in_fire`; a scorer answers `na` for an oracle whose capability is missing) |
| `party` | per scoped bot, when first seen (also a bot that joins later) | `g`, `name`, `class` (`warrior`...`druid`), `role` (`tank`/`heal`/`dps`), `level` |
| `mobs` | per hostile, the first time a bot sees it (registry, capped at 1024 per run) | `g`, `entry`, `name`, `level`, `rank` (0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare), `boss` |
| `frame` | every `IntervalMs` | `bots`: array of bot samples; `mobs`: array of hostile samples |
| `event` | on change | `ev`, and when set `a`, `b`, `v`, `s`, `s2` |
| `action` | an action the engine took out of its queue | `g`, `name`, `res`, `rel`, and when set `src_trigger`, `n`, `tl` |
| `end` | once, last | `reason`, `counters` |

**Bot sample** (in `frame.bots`): `g`, `al` (alive), `cb` (in combat), `st` (engine: `c` combat, `n` non-combat, `d` dead, `r` reaction), `hp`, `mhp`, `pw`, `mpw`, `pt` (power type: 0 mana, 1 rage, 2 focus, 3 energy), `x`, `y`, `z`, `mv` (a movement generator is active), and when set `tg` (target: selection, else victim), `fa` (facing angle: degrees, 0-180, between where the bot faces and the direction to its hostile target; only with a hostile target), `cs` (spell id being cast), `fr` (spell id of a harmful ground or area aura it stands in: a negative aura with periodic damage from a dynamic object or an area aura of someone else, e.g. Rain of Fire; read from the bot's own aura list), `act` (last executed action), `strat` (the engine's strategies, only in the frame where they changed, so the first frame and every engine switch carry it).

**Hostile sample** (in `frame.mobs`): `g`, `hp` (% of max), `x`, `y`, `z`, `cb`, and when set `tg` (what it targets), `cs`, `ev` (evading), `cc` (stunned, confused, fleeing or rooted), `thr` (its threat list, top three as `[guid, pct of the top entry]`, highest first; only for a hostile in combat). Hostiles are what the bots fight, target or are attacked by (deduplicated across the bots, at most 24 per frame); a unit no bot sees does not appear.

**Events** (`ev`): `death` (`a` the bot, `b` a hostile that had it targeted in the previous frame), `boss_kill` (`a` the boss unit, `v` its entry, `s` its name; seen at frame resolution), `combat_on` / `combat_off` (`a` the bot), `loot` (`a` the bot, `s` the item name, `v` the item id; or `s` = `money`, `v` the copper), `cast_interrupted` (`a` the bot, `v` the spell id, `s` = `moving` when the bot was moving at that moment; fired from the same place the AI learns that its cast was cancelled, so a deliberate stop-then-move counts too; the reason code is not available), `action_loop` (`a` the bot, `s` the action, `s2` the outcome, `v` the failures counted: the same action failed more than 8 times in 10 s, flagged live by the recorder). Deaths and combat edges are seen at frame resolution.

**Action outcomes** (`res`): `OK` (executed), `FAILED` (possible, ran, returned false), `IMPOSSIBLE` (not possible now), `USELESS` (not useful now), `PREREQ` (its prerequisites were queued first), `VETOED` (a multiplier zeroed its relevance), `BACKOFF` (the failure backoff dropped it). `rel` is the relevance after the multipliers. A bot repeating the same action with the same outcome and trigger within one second is **one record**: `t` is the first, `tl` the last, `n` how many.

**End counters**: `frames`, `events`, `actions` (records), `party`, `mobs`, `deaths`, `loops`, `records` and `bytes` (written), `dropped` (refused: queue full, size cap or a write error), `truncated` (the size cap closed the file). The writer drains its queue before the `end` record is built, so `records`, `bytes`, `dropped` and `truncated` count every line before the `end` record (not the `end` line itself), and the summary carries the same counters. If the `end` line itself could not be written, the summary counts it in `dropped`.

## Summary file

`<run_id>.summary.json`, one object, the file a dashboard or a nightly job reads:

```json
{"v":1,"schema":"bt-trace","run":"r1","src":"server","status":"complete","end_reason":"stop",
 "scope":"party","scope_name":"Aldo","requester":"Aldo","tb_version":"2026-10-14-v3",
 "trace_file":"r1.jsonl","start_unix":1760000000,"end_unix":1760000600,"duration_ms":600000,
 "interval_ms":500,"map":70,"instance":12,
 "counters":{"frames":1200,"events":14,"actions":3100,"party":5,"mobs":22,"deaths":0,"loops":1,
             "records":4341,"bytes":2104332,"dropped":0,"truncated":false},
 "oracles":[{"id":"action_loop","name":"action failure loop","verdict":"fail","count":1,
             "first_ms":41200,"detail":"1 loop(s); live detector flagged 1"}],
 "verdict":"fail","scored_unix":1760000700,"scorer":"trace_score.py v1","trace_complete":true,"bad_lines":0}
```

`status` is `running`, `complete`, `aborted` (a line could not be written), `failed` (the trace file could not be flushed or closed at the end; `error` says why) or `incomplete`. A trace interrupted by a shutdown or a crash is marked `incomplete` at the next server start: a summary still `running` then gets `status: incomplete`, `end_reason: "server stopped before the trace was finalised"` and `reconciled_at` (unix seconds), and keeps the counters it had; its trace file is left as it is (it usually has no `end` record). A failure to write the summary itself is reported by `.bot trace status` and in the server log. The first block is written by the server; `oracles`, `verdict`, `scored_unix`, `scorer`, `trace_complete` and `bad_lines` by the scorer. An oracle `verdict` is `pass`, `fail` or `na` (the trace has nothing to judge, e.g. no healer).

### Summary fields for the dashboard

The server also writes the fields the dashboard's Dungeons tab reads ([observability dashboard](../../tools/observability/README.md), issue #986). They come from events the recorder already sees; nothing is read from the database, nothing scans the world, and the JSON is rendered once, when the summary is written. The `running` summary written at start carries only the live fields (`mode`, `build`, `driver`, `started`, `plan`, `dungeon`, `furthest`, `wipes`, `deaths`, `party`): no `outcome`, `bosses` or `clear_s` until the run ends, so a live run is never read as a failure. Old summaries without any of these still parse; the dashboard shows their runs with outcome `unknown`.

| Field | Content |
| :--- | :--- |
| `mode` | `run_leader` when the module's run leader (`.bot run`) walks a plan for the requester, `commander` when the requester has a bound remote commander, else `observe`. Read at start and once a second; the strongest sign seen wins. |
| `build` | `{"image", "module", "core"}`: `image` and `module` both hold `TORTOISEBOTS_BUILD_VERSION` (the same string as `tb_version`; the server does not know its image tag or the module's git sha, follow-up), `core` the core revision (8 chars). |
| `driver` | The `driver=` argument; `human` for an `observe` run without one, `unknown` otherwise. |
| `started`, `ended` | ISO 8601 UTC (`ended` only in the final summary); `start_unix` / `end_unix` stay. |
| `plan` | The plan name the bosses come from, when known. |
| `dungeon` | `{"map": <id>}` once the party stood on an instance map. The top-level `map` is still the map at start. |
| `furthest` | `{"leg", "pull_group"}`: the highest plan step the run leader reached (`pull_group` is the step's sequence number in its leg). Run leader runs only. |
| `bosses` | `[{"entry", "name", "killed_at_s"}]`, `killed_at_s` seconds from the run start (one decimal, frame resolution), `null` for a boss not killed. |
| `outcome`, `stop_reason` | See below; `stop_reason` is the end reason (`stop`, `scope_lost`, `size_cap`, `shutdown`, `disabled`, `write_failed`). |
| `clear_s` | Seconds from the run start to the last boss kill; written only when `outcome` is `cleared`. |
| `wipes` | `[{"t_s", "map", "x", "y", "z", "role", "dead", "leg", "pull_group"}]`: every traced bot dead at once (at least two bots in scope; one wipe until somebody is up again). Position: the centroid of where they fell; `role`: of the first to fall. |
| `deaths` | `[{"t_s", "map", "x", "y", "z", "bot", "role", "leg", "pull_group"}]`: each time a traced bot goes from alive to dead (where it fell). `x`, `y` are game coordinates. `leg` and `pull_group` are the run leader's step (once a second) and are left out without a run leader. |
| `party` | `[{"class", "role", "level"}]` of the traced bots, as first seen. |

**Boss list.** The run leader's own plan when it runs; otherwise the one plan on the party's map if there is exactly one (a commander's agent keeps its plan to itself). The list is the plan's `boss` steps for the party's team. With several plans on the map and none chosen there is no list: the bosses of all of them are still watched, and the bosses met are reported. A boss counts as killed when a unit the bots saw alive is found dead by guid on the map of a bot in its instance (the `boss_kill` event, `a` the unit, `v` the entry, `s` the name). A boss killed before the trace started was never seen alive, so such a run cannot be `cleared`: start the trace before the first pull.

**Outcome rule** (first match wins). `cleared`: every boss on the plan's list was killed (never without a list). `wiped_out`: not cleared and the run ended while the whole party was dead. `stopped`: there is no list, so nothing says how far it got; or there is a list, no boss was killed and the run was stopped by hand. `partial`: a list and at least one boss killed, not all. `abandoned`: a list, no boss killed, and the run ended by itself. A trace that never stood on an instance map and met no boss carries no `outcome`, `bosses` or `dungeon`, so the dashboard ignores it. At most 200 wipes and 200 deaths are kept. Not written yet (follow-up): `gm_assists`, and the module's git sha in `build`.

## Scoring

```bash
python3 tools/trace_score.py traces/r1.jsonl            # report, and update r1.summary.json
python3 tools/trace_score.py traces/ --no-write          # every trace in a directory, read-only
python3 tools/trace_score.py r1.jsonl --json --set loop_failures=12
```

Ten oracles. The facing, self-interrupt, stuck and in-fire checks look for problems that do not show as a failed action. They are cheap per-frame fields (an angle, a flag, one aura-list read per bot), judged heuristically from sampled positions, so expect a few false positives and read the examples before trusting a count. The oracles are pure functions of the trace (no server needed), so an old trace can be re-scored after an oracle changes. Thresholds can be overridden with `--set key=value`.

| Id | Fails when |
| :--- | :--- |
| `action_loop` | the same action `FAILED`/`IMPOSSIBLE` more than `loop_failures` (8) times within `loop_window_ms` (10 s) for one bot: the spellstone, stay and melee loops. |
| `threat_off_tank` | a hostile in combat stays on a party member who is not a tank for `threat_hold_ms` (3 s) while a tank is alive and in combat. Crowd-controlled and evading hostiles are skipped; `na` without a tank. |
| `idle_caster` | a dps mage, warlock or priest in combat with a hostile in view, with at least `idle_min_power_pct` (15) % mana, neither casts nor executes one `OK` action for `idle_hold_ms` (4 s). A bot that wands is acting (its action is `OK`). |
| `deaths` | any party bot died (lists who, when, and what had it targeted). |
| `oom_healer` | a healer below `oom_pct` (5) % mana while the party is in combat for `oom_hold_ms` (3 s). |
| `facing` | a bot in combat is within `facing_melee_yd` (6 yd) of its target and standing (not moving), or casting at it, while more than `facing_deg` (90) degrees off it for `facing_hold_ms` (2 s). `na` without `caps` `facing`. |
| `self_interrupt` | one bot has `self_interrupt_count` (3) casts cancelled while moving within `self_interrupt_window_ms` (30 s). A single cancel is normal (kiting). `na` without `caps` `interrupts`. |
| `stuck` | a bot's movement is active (`mv`) but it ends up less than `stuck_min_move_yd` (1 yd) from where it started for `stuck_hold_ms` (5 s); or, out of combat for `osc_window_ms` (10 s) with movement active, it stays within `osc_max_span_yd` (8 yd) yet walks at least `osc_min_path_yd` (30 yd) turning back at least `osc_min_reversals` (4) times. `na` without `caps` `moving`. |
| `in_fire` | a bot is in a harmful ground or area effect (`fr`) for `fire_hold_ms` (1.5 s). `na` without `caps` `in_fire`. |
| `rest_failure` | eat / drink / rest actions fail `rest_fail_count` (5) times within `rest_fail_window_ms` (30 s), or a bot stands still out of combat below `rest_low_mana_pct` (30) % mana for `rest_stall_ms` (20 s) without its mana rising. |

## Cost and limits

- **Off, or no run active:** one atomic load per world tick, and one walk over an empty listener list per action the engine takes out of its queue (a run's listeners are removed from the bots' engines on the world tick after it ends). Nothing else runs.
- **During a run:** each interval, per scoped bot, member reads, one read of its own aura list, its cached attacker lists and a by-guid lookup of each hostile on the bot's own map: the same sources as the remote commander's `TBM:VIEW` feed. No world scan, no database access. The JSON lines are built on the world thread (a few microseconds per bot and frame); every disk write is on another thread, through a queue of at most 8192 lines / 4 MiB that drops (and counts) lines instead of blocking. The world thread never waits on the disk while a run is going or while it ends: stopping only queues the end marker, and the writer thread writes the end record and the summary and closes the file. It touches the disk only at `.bot trace start` (creating the directory and opening the file) and once at server start (marking summaries of interrupted runs `incomplete`). Shutdown does not wait for the writer either: a run still being written is left to finish on its own, and the next start reconciles its summary.
- **Size:** roughly 200 bytes per bot and 150 per hostile in each frame, plus the action records; five bots in a fight at the default 500 ms is about 0.3-1 MiB a minute (estimate, not measured). `AiPlayerbot.RunTrace.MaxFileMb` ends the run with an intact `end` record.
- Hostiles are limited to those the bots already track as attackers or targets, not everything in sight; a trace shows who fought whom, not the whole room.

Config keys are in [logging settings](settings/logging.md#aiplayerbotruntraceenabled). The scope of one run is one party; two parties at once need two servers or two runs one after the other.
