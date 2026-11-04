---
id: play-boss-tactics
title: Boss Tactics Table
category: play
summary: The per-boss tactics table (ai_boss_tactic), the stand sets (ai_boss_stand) and the opt-in boss tactics strategy that applies them.
tags: [guide, dungeon, boss, tactics, uldaman, configuration, commander]
relates_to:
  - play-dungeons-and-raids
  - server-configuration
  - developers-strategy-engine
  - developers-architecture
---

# Boss Tactics Table

> Work in progress: this feature is still being built and tested.

A boss fight has a few mechanics that generic combat cannot know: which spell to interrupt, which add to kill first, where not to stand. This page describes the data table that holds them and the strategy that applies them.

It is **off by default**. A normal player's bots play exactly as before.

## When it is active

The `boss tactics` strategy is added to a bot by the `enter boss tactics` trigger of the `dungeon` strategy (re-checked every 5 ticks) when all of these hold:

- the bot is on a map that has a row in `ai_boss_tactic` OR `ai_boss_stand` (a stand set alone arms it), and
- either `AiPlayerbot.DungeonTactics = 1` (server owner opt-in; default `0`), or the bot is in a bound group:
  - a remote bind needs `AiPlayerbot.RemoteCommander = 1`,
  - an `inparty` bind needs `AiPlayerbot.DungeonSelfClear = 1`.

`leave boss tactics` removes it when the bot is on a map without rows (not while teleporting or loading), or when the key is off and the bot is no longer in a bound group. The gate is `BossTacticsWanted`.

Commander-bound groups use it because commanders cannot give interrupt and kill orders fast enough. The key stays `0` because it changes how bots play.

Two extra opt-ins:

- `linked_death` needs `AiPlayerbot.DungeonRunLinkedDeath = 1` (default `0`). Rows exist but are unread while it is off.
- Aura rows need `AiPlayerbot.DungeonRunAuraTactics = 1` (default `0`). These are `dps_hold_after`, `dispel_priority`, `flee_on_aura`, `freeze_on_aura`, `taunt_after_cast`, `swap_on_aura`.

## The table

`ai_boss_tactic` is created and filled by `data/sql/world/*_world.sql` migrations. It is read at startup into a cache, one pass, nothing per tick.

- `ai_boss_tactic` is also re-read by `.bot run reloadplans` (no restart needed for it).
- `ai_boss_stand` is startup-only (restart needed).

| Column | Meaning |
| :--- | :--- |
| `map` | Map id. |
| `boss_entry` | Creature entry whose presence among the bots' attackers activates the row. |
| `kind` | See Kinds below. |
| `target_entry` | The creature the row is about; `0` means the boss itself. `hold_kill` is inverted: `boss_entry` is the spared entry and `target_entry` is the blocker. `one_at_a_time` uses target `0` and `boss_entry` is the paced entry. `linked_death` lists one partner per row and `boss_entry` is the anchor. |
| `spell_id` | `interrupt` / `avoid_cast` spell. |
| `priority` | `kill_first`: lower dies first. `hold_kill`: budget, how many blockers may live before DPS stop (`0` = not one more). `linked_death`: group id. |
| `param` | `behind`: yards of front kept clear. `avoid_cast`: yards left. `linked_death`: hold percent, 0-100 exclusive of 0. `dps_hold_after`: hold seconds, 0-30. |
| `burn_order` | `linked_death` only: kill order once released. First non-empty wins. |
| `hp_min` / `hp_max` / `bot_aura` / `mode` | Row gates, where read. |
| `comment` | Free text. |

The true primary key is `(map, boss_entry, kind, target_entry, spell_id, hp_min, hp_max)`.

The loader skips unusable rows and logs the count: unknown kind, interrupt without spell, behind without radius, avoid without spell or radius, tank_hold with target_entry, hold_kill without target_entry, one_at_a_time with target_entry, linked_death without target_entry or param outside (0,100], duplicate key.

### Kinds

| Kind | What bots do | Who |
| :--- | :--- | :--- |
| `interrupt` | A bot with a ready interrupt in range uses it: Pummel, Shield Bash, Kick, Counterspell, Earth Shock, first castable wins. Class interrupts on the current target are unchanged. | Any bot with an interrupt |
| `kill_first` | Takes the listed creature before anything else. Lowest priority first, nearer breaks ties, nothing past 40 yd. Generic assist and AoE do not pull back. An explicit attack order wins. | DPS only, never tanks or healers |
| `hold_kill` | DPS leave `boss_entry` alone while more than `priority` living blockers stand within 50 yd. Pickers skip the held entry. A bot already on it takes the `kill_first` pick if there is one, else the nearest attackable within 40 yd, else stops. | DPS |
| `one_at_a_time` | All DPS kill one living unit of the entry at a time: lowest-health fighting unit, lowest guid breaks ties. CC and evading units are never picked. | DPS |
| `linked_death` | The group must die together. Free DPS take the highest-health-fraction living member within 50 yd. A member at or below `param` % is held: never picked, AoE leaves it out, area and DoT casts are vetoed. Burn order applies once released. | Free DPS |
| `behind` | Within `param` yd in the front half, step 120 degrees off the boss facing at the same distance. | Everyone except tanks and the bot the boss attacks |
| `avoid_cast` | Within `param` yd 3D of the caster (channel slot included), walk to 2 yd past the radius on ring points, shortest walk first. | Everyone the row covers |
| `tank_hold` | Opt-in per caster, `target_entry` 0. A tank whose target is that caster stays in the blast, keeps it, and is the preferred pickup. | Tanks |
| `dps_hold_after` * | Hold DPS after the trigger, `param` seconds. | DPS; needs `DungeonRunAuraTactics = 1` |
| `dispel_priority` * | Ranks aura removal. | Dispensers; needs `DungeonRunAuraTactics = 1` |
| `flee_on_aura` * | The carrier leaves the group. | Carrier; needs `DungeonRunAuraTactics = 1` |
| `freeze_on_aura` * | The carrier holds all actions but freeze. | Carrier; needs `DungeonRunAuraTactics = 1` |
| `taunt_after_cast` * | Taunt on the trigger. | Tanks; needs `DungeonRunAuraTactics = 1` |
| `swap_on_aura` * | Tank rotation. | Tanks; needs `DungeonRunAuraTactics = 1` |

\* Needs `AiPlayerbot.DungeonRunAuraTactics = 1`.

Not yet covered: tank swap and re-grab tuning, spread, ground-effect avoidance. Generic code covers part of it.

## Shipped rows

Boss name first, then the rows that fire for it. No row means the boss has no tactics yet.

### Uldaman (70)

| Boss | Rows |
| :--- | :--- |
| Revelosh | `interrupt` Chain Lightning |
| Baelog, Eric, Olaf | `kill_first` order Baelog (1), Eric (2), Olaf (3) |
| Ironaya | `behind` 10 yd (Knock Away unhandled) |
| Obsidian Sentinel | `kill_first` Shard |
| Grimlok | `interrupt` Lightning Bolt 9532 |

### Deadmines (36)

| Boss | Rows |
| :--- | :--- |
| Voss | `interrupt` Poison Cloud |
| Harvester | none |
| Greenskin | `interrupt` Poisoned Harpoon |

### Wailing Caverns (43)

| Boss | Rows |
| :--- | :--- |
| Zandara | `interrupt` Chain Lightning, Gust of Wind |
| Vangros | none |
| Serpentis | `interrupt` Healing Touch, Sleep, Lightning Bolt |
| Cobrahn | `interrupt` Druid Slumber, Lightning Bolt, Healing Touch |
| Pythas | `interrupt` Healing Touch, Sleep, Lightning Bolt |
| Skum | `interrupt` Chained Bolt |
| Mutanus | `interrupt` Naralex Nightmare |

### Frostmane Hollow (822)

| Boss | Rows |
| :--- | :--- |
| Kan'za | `interrupt` Frostbolt |
| Tan'sha | none |
| Hailar | `interrupt` Frostbolt |
| Ubukaz | none (Ritualists unhandled: passive, never in attacker list, needs a code change, no row) |

### Windhorn Canyon (820)

| Boss | Rows |
| :--- | :--- |
| Stormhoof | `interrupt` Corruption |
| Shalk | `interrupt` Flame of Shalk |
| Vortalus | `interrupt` Gust of Wind |
| Ahgk'tos | none (no covered cast) |
| Pathun | none (no covered cast) |
| Narlgom | none (no covered cast) |
| Walgan | none (no covered cast) |
| Rotag | none (passive and immune, never targetable) |

### Gilneas City (815)

| Boss | Rows |
| :--- | :--- |
| Greymane | `interrupt` Fear |
| Ragetooth | `interrupt` Howl of Terror, Shadow Bolt |
| Sutherland | `interrupt` Smite, Mind Blast, Fireball |
| Greystone | `interrupt` Holy Fire |
| Holtz, Dustivan, Mortimer | none |

### Karazhan Crypt (800)

| Boss | Rows |
| :--- | :--- |
| Enkhraz | `interrupt` Fear, Freeze |
| Corpsemuncher | `interrupt` Acid Spit |
| Andreon | `interrupt` Mind Blast |
| Marrowspike, Hivaxxis, Gort | none |

### Crescent Grove (802)

| Boss | Rows |
| :--- | :--- |
| A'lathea | `interrupt` Starfire |
| Ranathos | `interrupt` Wrath, Entangling Roots, Regrowth |
| Raxxieth | `interrupt` Shadow Bolt, Flamestrike |
| Engryss, Fenektis | none |

### Zul'Farrak (209)

| Boss | Rows |
| :--- | :--- |
| Velratha | `interrupt` Healing Wave, Shadow Bolt Volley |
| Sezzziz | `interrupt` Heal, Renew |
| Antusul | `interrupt` Flash Heal, Healing Ward of Antusul, Healing Ward; `kill_first` Servant |
| Ukorz, Ruuzlu | none (cleaves, no yard value) |
| Zumrah ward | none (totem never in attacker list) |
| Zumrah, Gahz'rilla, Theka, Nekrum | no row |

### Scholomance (289)

| Boss | Rows |
| :--- | :--- |
| Vectus | `kill_first` Marduk; `interrupt` Flamestrike |
| Marduk | `kill_first` self (Marduk's party-wide shadow first) |

### Zul'Gurub (309)

| Boss | Rows |
| :--- | :--- |
| Bat Rider | `avoid_cast` Unstable Concoction 13 yd (no interrupt: uninterruptible) |
| Voidwalker | `avoid_cast` Consuming Shadows 8 yd; `tank_hold` row |
| Mad Servant | `interrupt` Flamestrike; `hold_kill` budget 0; `one_at_a_time` |
| Venoxis | `interrupt` Holy Fire; ranged and healer stand set |
| Thekal, Lor'Khan, Zath | `linked_death` group, hold 2 %, burn order Zealots then Thekal; Lor'Khan `interrupt` Greater Heal |

## Known limits

Recurring threat-reset holds need a cast trigger. Mandokir and Hakkar rows were removed for that reason.

## Stand sets

Own table `ai_boss_stand`, startup-only (restart needed). The strategy arms on a map with a row in either table.

| Column | Meaning |
| :--- | :--- |
| `map` / `boss_entry` | Where and whose fight. |
| `anchor` | Fight spot plus `x y r`: the boss at it within `r`. |
| `role` | `ranged`, `healer`, or `melee`. Tanks count as melee; healers seat first. |
| `window_spell` | `0` means always. |
| `aura` | Aura gating the stand. |
| `side` + `seq` / `x y z` | Seat position. `z` is put back on the ground. |
| `dx dy` | Slots: a line of seats 9 yd apart. |
| `window_signal` | `cast` or `stopped_no_target`: still, targetless, with aura. |
| `hold_ms` | Default 8000, range 1000-15000, or until `release_anchor`. |
| `release_anchor` | Anchor that ends the stand. |

Primary key is `(map, boss_entry, anchor, role, window_spell, side, seq)`. Unusable rows are skipped with a log; a bad `release_anchor` for a boss drops that boss's set.

Seats go to same-role bots in id order. Taking part means being on the threat list or victim within 80 yd of the anchor, or holding a stand. Sides come from the lane cross product. While seated, the stand action walks the bot to its seat.

## Adding a boss

- Read the boss from the core: creature template, spells, events, scripts, spell template. Use Turtle ids only.
- Add rows in a NEW migration `data/sql/world/YYYYMMDDHHMMSS_world.sql` (idempotent; never edit an applied one).
- Run the SQL tests with the core checkout set: `tools/test_boss_tactics_sql.py`, `tools/test_boss_stand_sql.py`.
- Check in game with `DungeonTactics = 1` or a commander binding, then a boss pull.

## Not yet verified live

Uldaman, Deadmines, Wailing Caverns, Frostmane Hollow, Windhorn Canyon, Gilneas City, Karazhan Crypt, Crescent Grove, Zul'Farrak, Scholomance, Zul'Gurub: none of the rows above are verified in game yet.
