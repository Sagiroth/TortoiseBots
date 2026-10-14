---
id: guide-dungeon-run-leader
title: Dungeon Run Leader
category: guides
summary: The opt-in run leader (.bot run, AiPlayerbot.DungeonSelfClear) that lets a bound party clear a dungeon plan by itself, its safety rules and supported steps, and the plan tables (ai_dungeon_plan, ai_dungeon_leg, ai_dungeon_step) that turtle-agent's exporter fills.
tags: [guide, dungeon, plan, commander, self-clear, uldaman]
relates_to:
  - guide-boss-tactics
  - guide-dungeon-tactics
  - guide-player-controls
  - guide-configuration-tuning
  - concept-architecture-invariants
---

# Dungeon Run Leader

A **dungeon plan** is the route of one dungeon written as data: where to walk, which packs to pull, which bosses to kill, which objects to open and what to wait for. Plans are made and checked live by [turtle-agent](https://github.com/pjerra/turtle-agent) (its `dungeons/<name>.json`, plan format v1); its exporter turns them into world migrations for the tables below. The turtle-agent file is the single source: edit the plan there and export again, never the rows by hand.

The **run leader** (`.bot run`) lets a bound party clear such a plan by itself. It is **off by default** (`AiPlayerbot.DungeonSelfClear = 0`); see [Run leader](#run-leader) below.

The tables are read once at startup into a cache (`ai/playerbot/DungeonPlanMgr.cpp`, one pass per table). Nothing reads them per tick; a change needs a restart.

## Tables

The schema comes from `data/sql/world/20261014150000_world.sql` (schema only; the rows come in the exporter's own migrations).

### `ai_dungeon_plan`

| Column | Meaning |
| :--- | :--- |
| `plan` | The plan's name, lower case (`uldaman`). Primary key. |
| `map` | The instance map id (Uldaman 70). |
| `source` | Where the rows came from (the turtle-agent commit and file), for the reader. |
| `comment` | Free text. |

### `ai_dungeon_leg`

| Column | Meaning |
| :--- | :--- |
| `plan`, `leg` | The plan and the leg number (0, 1, ...). Primary key. |
| `name` | The leg's name in the plan (`baelog`, `keystone`), shown in the status line. |
| `comment` | Free text. |

### `ai_dungeon_step`

One row is one thing to do. Steps run in `(leg, seq)` order; the primary key is `(plan, leg, seq)`.

| Column | Meaning |
| :--- | :--- |
| `plan`, `leg`, `seq` | Where the step sits. `seq` need not be dense. |
| `kind` | What to do (table below). |
| `entry` | A creature, game object or item entry, by kind. |
| `x`, `y`, `z` | A point on the plan's map, by kind. |
| `radius` | Yards, by kind; at most 100. |
| `item` | An item entry, by kind; `0` for none. |
| `param` | `wait_object`: the state to wait for (`0` ready / closed, `1` active / open, `2` active_alt). |
| `flags` | `1` ambush (a stealthed pack), `2` Alliance only, `4` Horde only. |
| `timeout_s` | `wait_object`, `wait_clear`: seconds before the wait fails; `0` = 60, at most 600. |
| `entries` | Comma-separated creature entries (`4851,4852`), by kind; at most 16. |
| `comment` | Free text. |

### Step kinds

| Kind | Columns used | What it means |
| :--- | :--- | :--- |
| `move` | `x y z` | Walk on the navmesh to the point. A plan leg's `to` and every via point of the route are moves. |
| `move_direct` | `x y z` | Walk a straight line to the point (a short gap the navmesh does not bridge). |
| `pull` | `x y z` (centre), `radius` (0: 15), `entries`, `flags` | The pack: every living unit of `entries` within `radius` of the centre. `flags 1`: the pack is stealthed; the party walks through it and fights what shows. |
| `boss` | `entry`, `x y z`, `entries`, `radius` (0: 30), `flags` | Kill `entries` in that order, then the boss `entry`. |
| `loot` | `entry`, `x y z` (near), `item` | Loot a chest or a corpse of that entry near the point; `item` must be in the bags afterwards. |
| `open` | `entry`, `x y z` (near), `item` | Use a game object (a door, the Keystone); `item` is what its lock needs. |
| `use_item` | `entry` (the item), `item` | Use an item from the bags; `item` is what the use must make. |
| `wait_object` | `entry`, `x y z` (near), `param`, `timeout_s` | Wait until the object is in that state (the Seal of Khaz'Mul opens). |
| `wait_clear` | `x y z`, `radius`, `timeout_s` | Wait until no living hostile stands within `radius` of the point. |
| `rest` | — | A rest gate even when nobody is low. |
| `ritual` | `entry`, `x y z` (near) | An object several players must use together. Loaded; not run yet. |

The loader skips a row it cannot use and names it in the server log (`ai_dungeon_step: plan ... skipped`): an unknown kind, a `pull` without entries, a `boss`/`loot`/`open`/`use_item`/`wait_object`/`ritual` without `entry`, a negative or over-100 radius, a `wait_clear` without one, a timeout over 600 s, a `wait_object` state above 2, a duplicate key, or a step of a plan with no `ai_dungeon_plan` row, or an `entries` text that is not a comma list of non-zero entries (`BadEntries`). It also logs a plan whose consecutive moves are more than 60 yd apart (the exporter should cut such a leg into via points). The rules are `NormalizePlanStep` in `ai/playerbot/DungeonPlanPolicy.h`; `tools/test_dungeon_plan_policy.cpp` and `tools/test_dungeon_plan_sql.py` test them.

### From plan v1 to rows

| Plan v1 | Rows |
| :--- | :--- |
| `legs[i].name` | `ai_dungeon_leg` (`leg` = i) |
| `legs[i].to` (and the route's via points before it) | `move` |
| `legs[i].pull_groups[j]` | `pull`: centre = the mean of `positions`, `radius` = the group's `radius` or the farthest position from the centre plus 10 yd, `entries`, `flags` from `ambush` and `team` |
| `legs[i].steps[]` / `after_boss[]` `use_object` | a `move` to `stand` when given, then `open` (`item` = `requires_item`) |
| `wait_for.gameobject` | `wait_object` (`param` from `state`) |
| `wait_for.hostiles_gone` | `wait_clear` |
| `loot_object` | `loot` (`item` = `expect_item`) |
| `use_item` | `use_item` (`item` = `expect_item`) |
| `legs[i].boss` | `boss` (`entries` = `first`, `flags` from `team`); its `loot_item` becomes a `loot` of the boss entry right after it |
| `legs[i].after_boss` | after the `boss` row, same kinds as `steps` |
| `skip`, `rest`, `companions_loot`, `watchdog` | not carried over yet |

## Run leader

Owner decision (2026-10-14): bots may clear a dungeon by themselves as an **opt-in** mode, off by default. It needs `AiPlayerbot.DungeonSelfClear = 1`, `AiPlayerbot.RemoteCommander = 1` and a remote commander binding (`.bot commander bind <LeadBot>`): the run leads the bound party, nobody else's. The commands and the `TBM:RUN` status line are in [Player Controls §1b](player-controls.md#1b-run-leader-bot-run-dungeon-self-clear).

It is the **observe** mode of the dungeon modes: the bots clear, the commander (a player, or turtle-agent with `--mode observe`) only watches the status line and the `TBM:VIEW` feed, and may pause, skip, resume or stop.

### How it plays

Once a second the run reads the party (alive, dead, in combat, health, mana, spread) and the current step's surroundings, and the step machine (`runtime/DungeonRunPolicy.h`) answers with at most one order. The order is sent exactly as the commander's own `.bot cmd <order>` (`move`, `move ... direct`, `pullback`, `rest`, `loot`, `open`, `use`): same checks, same ACK/ERR lines. The fights are the bots' own combat AI, with the [boss tactics](boss-tactics.md) of a bound party.

| Step | What the run does |
| :--- | :--- |
| `move`, `move_direct` | Before the hop: pulls a hostile within 15 yd of the hop's point that no later `pull` or `boss` step owns. Then `cmd move` to the point; done within 3 yd, or within 6 yd once the lead bot has stopped gaining ground for 12 s (a point just off the mesh or on a ledge). No progress for 12 s: the move again, at most 3 times, then pause. |
| `pull` | Not within 60 yd of the centre yet: walks toward it in hops of the lead bot's own navmesh path (at most 40 yd each, stopping 30 yd short; at most 8 hops; no path at all pauses the run). Then, nearest first: gathers the party (every bot within 20 yd of the lead bot, `cmd move` to the lead bot's own spot; 30 s at most) and sends `cmd pullback <guid>`. A pull that starts no fight in 25 s is tried once more, then that unit is passed. Done when no living unit of the pack that is not friendly (hostile or neutral) is left within its radius. A stealthed (ambush) pack: the party walks through its centre and fights what shows. |
| `boss` | As `pull`, in kill order: the `entries` first, the boss last. A neutral boss is pulled; only a friendly one is left alone. |
| team flags (`2` Alliance, `4` Horde) on `pull`/`boss` | A `pull` for the other team is skipped without a walk to it, unless the party already stands within 60 yd and a unit of it is not friendly. A `boss` for the other team is always walked to and looked at, and skipped only when every unit of its encounter there is friendly to the party; a hostile or neutral one is fought (the Lost Dwarves carry team horde in the plan, yet were hostile to an Alliance party in a live run). A later step for the other team owns nothing (its units near a hop may be pulled). |
| `loot` | Already carried: done. Else `cmd loot entry:<id>@x,y,z <lead bot> [item]`, waits for the task, and pauses when the item is not in the bags afterwards. |
| `open` | Pauses when the lead bot lacks the item the lock needs; else `cmd open entry:<id>@x,y,z <lead bot>` and waits for the task. |
| `use_item` | Already made: done. Else `cmd use <item> <lead bot>`; pauses when the made item is missing. |
| `wait_object` | Reads the object's state near the point (like `cmd state`) until it matches; `timeout_s` then pause. |
| `wait_clear` | Walks to the spot if needed; waits until no living hostile is within the radius; `timeout_s` then pause. |
| `rest` | `cmd rest` and waits as the rest gate does. |
| `ritual` | **Not supported yet**: the run pauses and names it (do it by hand with `.bot cmd ritual`, then `skip`, `resume`). |

The lead bot is the carrier of every object and item step.

### Safety rules

From turtle-agent's remote driver (the same rules, now on the server):

- **Gather before a pull**, and pull only what is near: the nearest unit of the step, or of the next hop.
- **Never pull what a later step owns**: a unit near the next hop that a later `pull` or `boss` step lists (by entry, within its radius) is left for that step.
- **No unseen pack counted cleared, no live boss counted dead**: a pack or boss is judged only while the lead bot stands within 60 yd of it (its grid is loaded there), and only "no living unit left that is not friendly" ends it.
- **3 dead stop the run** (pause: regroup with `cmd revive`, then `resume`); so does a dead lead bot.
- **Switched off mid-run** (a config reload sets `AiPlayerbot.DungeonSelfClear = 0`): every run ends with a last status line.
- **Every wait is bounded**: gather 30 s, a pull's fight start 25 s, a fight 180 s (a boss 420 s), a rest 60 s (it then goes on), a task 90 s, a step 300 s out of combat (a wait step: its timeout plus 60 s), at most 10 pulls per step. A ceiling pauses the run with the reason; nothing loops.
- **Any player order pauses it**: `.bot cmd` (but `state`), `.bot action`, and any chat command a real player gives a bot of the party (a whisper such as `stay`, `follow` or `attack`, `.bot command`, party chat) that passes the bot's command check.
- **Rest gate** after every fight and before every step: when a living bot is under 80 % health, a healer under 50 % mana or another mana user under 35 %, `cmd rest` (again every 20 s) until everyone is at 90 % health, healers 80 % and others 60 % mana, or 60 s.

### Not supported yet

- `ritual` steps (Uldaman's Altar of the Keepers, Archaedas) pause the run.
- A party with a real player in it (a player leading their own party): the run leads bound parties only.
- Plan v1's `skip`, per-leg `rest`, `companions_loot` (Revelosh's Shaft: the run's `loot` takes the corpse with the lead bot, but other bots may loot it first), `area_trigger`, gossip and waves.
- Wipe recovery: the run pauses; reviving and resuming is up to the commander.

### Cost

Nothing while no run exists. A running run ticks once a second (never per bot tick) and does at most one local grid search around its lead bot, only when the step's point is within 60 yd. No database access after startup.
