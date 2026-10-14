---
id: guide-boss-tactics
title: Boss Tactics Table
category: guides
summary: The per-boss tactics table (ai_boss_tactic) and the opt-in "boss tactics" strategy that applies it - row format, activation rule, supported kinds, the Uldaman rows and how to add a boss.
tags: [guide, dungeon, boss, tactics, uldaman, configuration, commander]
relates_to:
  - guide-dungeon-tactics
  - guide-configuration-tuning
  - concept-strategy-engine
  - concept-architecture-invariants
---

# Boss Tactics Table

A boss fight has a few mechanics that generic combat AI cannot know: which spell to interrupt, which add to kill first, where not to stand. This page describes the data table that holds them, the strategy that applies them, and the first content (Uldaman up to Ironaya, plus the Obsidian Sentinel).

It is **off by default**. A normal player's bots play exactly as before.

## When it is active

The `boss tactics` strategy is added to a bot by the `enter boss tactics` trigger of the `dungeon` strategy (re-checked every 5 ticks, like the per-dungeon strategies) when all of these hold:

1. the bot is on a map that has at least one row in `ai_boss_tactic`, and
2. either `AiPlayerbot.DungeonTactics = 1` (the server owner opted in; default `0`), **or** the bot is in a group that a remote commander has bound (`.bot commander bind`, needs `AiPlayerbot.RemoteCommander = 1`; the commander runs).

When the bot leaves the map, or the key is switched off and the bot is no longer in a bound group, `leave boss tactics` removes it again. The gate is `BossTacticsWanted` in `ai/playerbot/BossTacticsPolicy.h`.

Why the commander binding counts: those runs are the ones that exist to clear a dungeon with bots, and a commander cannot give interrupt or kill-order orders fast enough. Why the key stays `0`: the tactics change how bots play (a bot may leave its target to kill an add, or step out of a boss's front), and the project rule is that new automation ships off by default.

## The table

`ai_boss_tactic`, created and filled by `data/sql/world/*_world.sql` migrations. It is read once at startup into a cache (one pass, `ai/playerbot/BossTacticsMgr.cpp`); nothing reads the database per tick. A change needs a restart.

| Column | Meaning |
| :--- | :--- |
| `map` | Map id (Uldaman is 70). |
| `boss_entry` | Creature entry whose presence among the bots' attackers activates the row. |
| `kind` | `interrupt`, `kill_first` or `behind` (below). |
| `target_entry` | The creature the row is about; `0` means the boss itself. |
| `spell_id` | `interrupt`: the spell to interrupt. |
| `priority` | `kill_first`: lower is killed first. |
| `param` | `behind`: yards around the boss in which its front is kept clear. |
| `comment` | Free text; where the row came from. |

The primary key is `(map, boss_entry, kind, target_entry, spell_id)`. The loader skips a row it cannot use (unknown kind, an `interrupt` without a spell, a `behind` without a radius, a duplicate key) and logs how many it skipped; `tools/test_boss_tactics_sql.py` rejects the same rows at commit time.

### Kinds

| Kind | What bots do | Who |
| :--- | :--- | :--- |
| `interrupt` | While a creature in the fight (`target_entry`, or the boss) casts `spell_id`, a bot that knows an interrupt and has it ready in range uses it: *Pummel*, *Shield Bash*, *Kick*, *Counterspell* or *Earth Shock*, first castable wins. The class interrupts that already fire on the bot's current target are unchanged. | any bot with an interrupt |
| `kill_first` | Takes the listed creature as its target before anything else; among several listed creatures the lowest `priority` goes first, the nearer one breaks a tie, nothing further than 40 yd is picked. While on that target the generic assist and AoE rows do not pull it back to the boss. | DPS only; never tanks or healers; an explicit `attack` order from a player wins |
| `behind` | A bot that stands within `param` yd of the boss, in its front half, steps to a point 120 degrees off its facing at the same distance (the same geometry as the generic rear flank). | everyone except the bot the boss is attacking |

Not in the table yet, and why: tank swap and re-grab (Ironaya's *Knock Away*), dispel priorities, spread and ground-effect avoidance. The generic raid hazard, spread and dispel code already covers part of it, and each of these needs a live fight to tune before it gets a kind. Adding one means a new `BossTacticKind`, a scan function and an action next to the existing three.

## Uldaman rows (map 70)

Every creature and spell was read from Turtle's own data (`tortoise-wow`: `creature_template`, `creature_spells`, `creature_ai_events`/`creature_ai_scripts`, `spell_template`, `src/scripts/dungeons/uldaman`), not from another core's tables.

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Revelosh (6910) | `interrupt` Chain Lightning 16006 and 15117 | Spell list 69100 casts 16006 (first at 8-13 s, then every 18-25 s) and a Lightning Bolt 15801 filler; the template's `spell_id1` is 15117. Only Chain Lightning is listed so interrupts are not burnt on the filler. |
| Baelog 6906, Eric "The Swift" 6907, Olaf 6908 | `kill_first` Baelog (1), Eric (2), Olaf (3), under all three entries | Baelog is the only dwarf with a ranged attack (*Shoot* 15620) and flees at 15 % health (event 690601); Eric opens with *Rushing Charge* 6268; Olaf has the most health (5867) and the *Shield Slam* stun 8242. The order is a judgement from that data, not something the core states. |
| Ironaya (7228) | `behind`, 10 yd, spell 8374 | `boss_ironaya` casts *Arcing Smash* 8374 on herself every 13 s; it is a cone (effect target 24) with radius index 8, which is the *War Stomp* radius (8 yd by its tooltip; the client DBC was not available, so 8 yd plus a 2 yd margin). Her *Knock Away* 10101 below 50 % health wipes the victim's threat and is not handled yet. |
| Obsidian Sentinel (7023) | `kill_first` Obsidian Shard 7209 | EventAI events 702301-702304 cast *Summon Obsidian Shard* 10061 (creature 7209) and *Splintered Obsidian* 10072 at 80, 60, 40 and 20 % health. |

What needs a live check (not verified in game): the Lost Dwarves order and whether the three can be pulled apart, that 10 yd is right for the Arcing Smash cone, that the interrupt fires in time on Chain Lightning.

## Adding a boss

1. Read the boss from the core: `creature_template` (`spell_list_id`, `ai_name`), `creature_spells`, the `creature_ai_events`/`creature_ai_scripts` rows or the `src/scripts` boss file, and `spell_template` for each spell. Do not use another core's ids.
2. Add the rows in a **new** migration `data/sql/world/YYYYMMDDHHMMSS_world.sql` (idempotent: `DELETE` the map's rows, `INSERT` them again). Never edit an applied migration.
3. Run `python3 tools/test_boss_tactics_sql.py`, then check it in game with `AiPlayerbot.DungeonTactics = 1` or a commander binding and a boss pull.

Behaviour is based on the mod-playerbots per-map raid strategy pattern, reimplemented as data; see [PROVENANCE](../PROVENANCE.md).
