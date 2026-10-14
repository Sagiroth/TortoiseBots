---
id: guide-dungeon-run-leader
title: Dungeon Run Leader
category: guides
summary: The dungeon plan tables (ai_dungeon_plan, ai_dungeon_leg, ai_dungeon_step) that turtle-agent's exporter fills, their row format, and how the server loads them.
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
