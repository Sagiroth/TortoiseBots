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
- `.reload config` re-reads both keys without a restart.

## The table

`ai_boss_tactic` is created and filled by `data/sql/world/*_world.sql` migrations. It is read at startup into a cache, one pass, nothing per tick.

- `ai_boss_tactic` is also re-read by `.bot run reloadplans` (no restart needed for it).
- `ai_boss_stand` is startup-only (restart needed).

| Column | Meaning |
| :--- | :--- |
| `map` | Map id. |
| `boss_entry` | Creature entry whose presence among the bots' attackers activates the row. |
| `kind` | See Kinds below: `interrupt`, `kill_first`, `behind`, `avoid_cast`, `tank_hold`, `hold_kill`, `one_at_a_time`, `linked_death`, `seek_aura`, `pull_gate`, `spread`, `use_object`, `threat_successor`, plus the aura kinds behind their own key (`dps_hold_after`, `dispel_priority`, `flee_on_aura`, `freeze_on_aura`, `taunt_after_cast`, `swap_on_aura`). |
| `target_entry` | The creature the row is about; `0` means the boss itself. `hold_kill` is inverted: `boss_entry` is the spared entry and `target_entry` is the blocker. `one_at_a_time` uses target `0` and `boss_entry` is the paced entry. `linked_death` lists one partner per row and `boss_entry` is the anchor. A `seek_aura` row names the source creature whose death leaves the cloud; a `pull_gate` row has `target_entry` 0 and names the boss itself, as does a `spread` row. |
| `spell_id` | `interrupt` / `avoid_cast` spell. `seek_aura`: the aura a bot must carry; `pull_gate`: the aura the boss must not wear; `spread`: the splash the spacing answers (documents the row only). |
| `priority` | `kill_first`: lower dies first. `hold_kill`: budget, how many blockers may live before DPS stop (`0` = not one more). `linked_death`: group id. `seek_aura`: the health % at which DPS stop on the source while the gather is not ready; `pull_gate`: most stacks allowed (`0` = none); `taunt_after_cast` with mode `until_tank`: which raid tank takes the subject. |
| `param` | `behind`: yards of front kept clear. `avoid_cast`: yards left. `linked_death`: hold percent, 0-100 exclusive of 0. `dps_hold_after`: hold seconds, 0-30. `seek_aura`: gather radius around the source; `pull_gate`: seconds the run leader holds before ending the run (1-300); `spread`: yards the ranged raid keeps apart (5-30); `taunt_after_cast`: deadline seconds. |
| `tactic_mode` | Only some kinds read it. `until_tank`: a `taunt_after_cast` row assigns a tank by raid order while the subject stays off a tank. |
| `window_s` | `linked_death`: burn window seconds (default 7). `seek_aura`: dose window opens this many seconds before the cycle is due. `0` for every other kind. |
| `cycle_spell`, `cycle_s` | `seek_aura` only: boss spell whose aura restarts the cycle clock, and the cycle length in seconds. `0` for every other kind. |
| `spot_x`, `spot_y`, `spot_z` | `seek_aura` only: where the source is held and killed. `0` for every other kind. |
| `burn_order` | `linked_death` only: kill order once released. First non-empty wins. |
| `hp_min` / `hp_max` / `bot_aura` / `mode` | Row gates, where read (never on `seek_aura`, `pull_gate` or `spread`). |
| `comment` | Free text. |

The true primary key is `(map, boss_entry, kind, target_entry, spell_id, hp_min, hp_max)`.

The loader skips unusable rows and logs the count: unknown kind, interrupt without spell, behind without radius, avoid without spell or radius, tank_hold with target_entry, hold_kill without target_entry, one_at_a_time with target_entry, linked_death without target_entry or param outside (0,100], `seek_aura` without target_entry, spell_id, cycle_spell or spot, param outside (0,10], priority above 99, cycle_s outside (0,600] or window_s outside (0,cycle_s), `pull_gate` with target_entry, without spell_id or param outside (0,300], `spread` with target_entry or param outside 5-30, `threat_successor` with target_entry or a gate, a cycle/spot/mode column the kind does not read, duplicate key.

### Kinds

| Kind | What bots do | Who |
| :--- | :--- | :--- |
| `interrupt` | A bot with a ready interrupt in range uses it: Pummel, Shield Bash, Kick, Counterspell, Earth Shock, first castable wins. Class interrupts on the current target are unchanged. Generic slot then channel slot, wider radius wins. | Any bot with an interrupt |
| `kill_first` | Takes the listed creature before anything else. Lowest priority first, nearer breaks ties, nothing past 40 yd. Generic assist and AoE do not pull back. An explicit attack order wins. | DPS only, never tanks or healers |
| `hold_kill` | DPS leave `boss_entry` alone while more than `priority` living blockers stand within 50 yd. Pickers skip the held entry. A bot already on it takes the `kill_first` pick if there is one, else the nearest attackable within 40 yd, else stops. The grid search counts a fighting or blocked creature, so the portal counts while an unwoken add does not. | DPS |
| `one_at_a_time` | All DPS kill one living unit of the entry at a time: lowest-health fighting unit, lowest guid breaks ties. CC and evading units are never picked. | DPS |
| `linked_death` | The group must die together. Free DPS take the highest-health-fraction living member within 50 yd. A member at or below `param` % is held: never picked, AoE leaves it out, area and DoT casts are vetoed. Burn order applies once released. Release latches with `window_s` once armed (5 s, default 7 s); evade resets it. | Free DPS |
| `behind` | Within `param` yd in the front half, step 120 degrees off the boss facing at the same distance. | Everyone except tanks and the bot the boss attacks |
| `avoid_cast` | Within `param` yd 3D of the caster (channel slot included), walk to 2 yd past the radius on ring points, shortest walk first. Listed blasts hold reach, chase, heal, follow, and stay-back moves until the cast ends or the caster dies. | Everyone the row covers |
| `tank_hold` | Opt-in per caster, `target_entry` 0. A tank whose target is that caster stays in the blast, keeps it, and is the preferred pickup. Only its own caster's blast, and only while that caster is on a non-tank; the pickup outranks the raid mark when nearer. | Tanks |
| `dps_hold_after` * | Hold DPS after the trigger, `param` seconds. | DPS; needs `DungeonRunAuraTactics = 1` |
| `dispel_priority` * | Ranks aura removal. | Dispensers; needs `DungeonRunAuraTactics = 1` |
| `flee_on_aura` * | The carrier leaves the group. | Carrier; needs `DungeonRunAuraTactics = 1` |
| `freeze_on_aura` * | The carrier holds all actions but freeze. | Carrier; needs `DungeonRunAuraTactics = 1` |
| `taunt_after_cast` * | Taunt on the trigger. Mode `until_tank`: while the subject stands on a non-tank, the row's `priority` tank by raid order taunts it back within `param` seconds (`spell_id` 0 legal for instant charms). Also needs `AiPlayerbot.DungeonRunCharmYield = 1`. A subject the game makes immune to taunts opens no episode at all: no taunt of that tank can land on it, so it keeps its attack and its taunt for a target that can be moved, and the server log says `taunt_after skipped: target taunt-immune` once per fight for that subject. | Tanks; needs `DungeonRunAuraTactics = 1` |
| `swap_on_aura` * | Tank rotation. | Tanks; needs `DungeonRunAuraTactics = 1` |
| `seek_aura` | The raid doses itself from a dying source before the boss's cycle spell lands. The first siphon is timed from the fight's combat start, so cycle 1 gets the same dose window as every later one. While the dose window is open (`window_s` seconds before due, and up to 8 seconds past it when a sighting is missed, after which the cycle estimate rolls forward one cycle at a time (the grace stays under the gap between a cycle and its window, so the rolled cycle's window opens soon after)), bots lacking `spell_id` walk to the living source and stand within `param` yd; DPS bring it to `priority` % and stop until the gather is ready, then burn it so the cloud lands on the raid. A Son that is not fighting yet is fetched for real: the second off-tank walks to it and hits it until it joins the fight, then drags it to the spot; with fewer than three tanks alive (exactly two) the raid's nearest hunter (else a ranged damage dealer) opens a Son within 35 yd of the spot with one shot from its ranged weapon (the auto shot or a wand, never a damage over time whose ticks would run through the hold) and the first off-tank takes it over; a dead, charmed or boss-targeted member is never that puller. The source search reaches the spot's radius while three tanks are there, and 35 yd once the burn is forced, a source inside that reach already fights at or below the hold percent, or any source inside it is already fighting: that source becomes the gather and the kill pick wherever it stands. The only held hit that is let through is the puller's own strike on the one Son it is fetching, and only until that Son joins the fight (a DoT is still vetoed there). Only the picked source may be damaged, and only inside the window — every other Son stays held, and with the window shut the pick is left at whatever health it has so it serves the next siphon. An area cast that reaches a held Son is vetoed even when the picked Son beside it may burn, a Son that is not fighting is never picked, and once the whole raid carries the aura nothing is killed until the next siphon strips it. A damage-over-time with no direct part (Corruption, Serpent Sting) and a channelled one (Mind Flay, Drain Life) never goes on a Son, since its ticks would run through the hold; a nuke that also ticks (Fireball, Immolate, Moonfire) is allowed only on the Son the hold lets burn. The off-tank holding the Son stops auto-attacking it at or below the hold percent plus 10 while the window is shut, and keeps the threat. Hunters keep the siphon cycle: the pet is dismissed from 5 s before the due time until 2 s after it (and while the siphon's aura is on the bot) and called back afterwards, because an undosed pet is a full heal of its own. A warlock dismisses its demon once when Hakkar is engaged and summons nothing during the fight — a new demon costs a soul shard and a 10 s cast — and never an Infernal or a Doomguard. A shaman unsummons its totems for the same window and places none, since each totem is one more undosed unit. Needs `AiPlayerbot.DungeonRunSeekAura = 1` (default `0`); main tank and first off-tank never seek. | every bot without the aura except the two tanks; second off-tank pulls and holds the source (a hunter pulls when it is not there); pet owners send the pet away |
| `pull_gate` | A run-leader gate, not a fight scan: while the boss wears `spell_id` above `priority` stacks the run leader never pulls it and notes the gate; after `param` seconds it ends the run. Needs `AiPlayerbot.DungeonRunPullGate = 1` (default `0`). | the run leader |
| `spread` | While the boss fights, the ranged raid keeps `param` yd apart (the spell only documents the splash); a bot on a stay spreads too. Needs `AiPlayerbot.DungeonRunBossSpread = 1` (default `0`). | ranged bots and healers; melee and tanks never spread |
| `needs_aura` | Only a bot carrying `spell_id` may target the unit; with `AiPlayerbot.DungeonRunAuraGatedTargets = 1` other bots leave it alone. | Any bot with the aura |
| `use_object` | One claimed bot clicks the boss's game object at the right moment; mode `tank_anchor` also walks the boss over to it. Needs `AiPlayerbot.DungeonRunUseObject = 1` (default `0`). | Claimed clicker (free damage, healer or non-tank) |
| `burn_mana` | A few eligible bots (no tank, no healer, a Mana Burn priest, a Drain Mana warlock or a Viper Sting hunter; `priority` of them, `0` = all, ordered by guid) drain the boss's mana while it stands at or above the row's start % (`param`); only members that run the boss tactics and can cast right now (in combat, in control, their spell off cooldown, the boss in their spell's own range, enough mana) take a slot, a global cooldown does not cost one, and the burn pauses while the boss wears the stone-phase aura 25685. `target_entry` is 0, `spell_id` is unused. Needs `AiPlayerbot.DungeonRunBurnMana = 1` (default `0`). | Eligible drainers; the rest of the raid keeps its rotation |
| `threat_successor` | The boss itself (`target_entry` 0): its damage dealers are measured against the highest threat among the assigned off-tanks instead of the main tank, so the off-tank stays second and a threat reset (Hakkar's Cause Insanity) hands the boss to it instead of to a damage dealer. An off-tank carrying less than half the highest tank's threat on the boss is not used as the reference (the highest tank is), so a barely-engaged off-tank cannot stop the damage. Only the boss's own data turns this on; most raid bosses are taunt-immune but keep the highest tank as the reference. Read straight from the table, so the kind takes no gate and no mode word. | Damage dealers of the group |
| `ranged_only` | Melee bots (melee damage and tanks) leave the subject alone while it stands `param` yd (3 to 40) or more above them, and take the adds; a ranged bot is never held, an explicit attack order beats the hold, and the hold ends by itself when the subject lands. `target_entry` 0 is the row's boss. Needs `AiPlayerbot.DungeonRunRangedOnly = 1` (default `0`). | melee damage and tanks; ranged bots, healers and an ordered bot keep the subject |

\* Needs `AiPlayerbot.DungeonRunAuraTactics = 1`.
Notes: sole and linked picks use the fight-wide cached grid search, never the bot's own attacker list. A held member vetoes area and DoT casts within 10 yd (`kHoldKillAoeNearYd`), even for an explicitly ordered non-tank or healer, and tanks' own area actions and passive cleaves are vetoed near a held member while ticking DoTs keep ticking. The raid mark falls through a held member to the equalise pick.

Not yet covered: re-grabbing the boss after a threat wipe (the `taunt_after_cast` kind exists, but Ironaya's *Knock Away* has not had its live tuning pass), dispel priorities and ground-effect avoidance. The generic raid hazard and dispel code covers part of it.

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
| Gahz'rilla | `interrupt` Icicle 11131, Freeze Solid 11836 |
| Zumrah, Theka, Nekrum | no row |

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
| Hakkar the Soulflayer | `dps_hold_after` 5 s from the pull (no spell, mode `engage`); `seek_aura` Poisonous Blood 24321 on a Son of Hakkar, hold at 30 %, gather 4 yd; `pull_gate` on Hakkar Power 24692, 0 allowed, hold 30 s; `spread` 12 yd on Corrupted Blood 24328; `taunt_after_cast` mode `until_tank`, tank 1, 10 s (inert: Hakkar is taunt-immune, the row opens no episode); `threat_successor` (the damage dealers' threat cap is the assigned off-tanks' highest). Needs `DungeonRunSeekAura`, `DungeonRunPullGate`, `DungeonRunCharmYield` and `DungeonRunBossSpread` (all default `0`), plus aura tactics for the taunt row. |

## AQ20 rows (map 509)

Only kinds the module already runs. Needs `DungeonRunCharmYield = 1` on the two `until_tank` rows, `DungeonRunTankSwap = 1` (default `0`) on the Kurinnaxx row, `DungeonRunBurnMana = 1` on Moam's `burn_mana` row and `DungeonRunRangedOnly = 1` on Ayamiss's `ranged_only` row.

| Creature | Row |
| :--- | :--- |
| Kurinnaxx | `swap_on_aura` Mortal Wound, 5 stacks, 2 tanks |
| Ossirian | `use_object` crystal; `taunt_after_cast` mode `until_tank`, tank 1; `dispel_priority` Curse of Tongues |
| Moam | `kill_first` Mana Fiend; `burn_mana` from 10 % mana, 4 burners (paused during the stone phase, aura 25685) |
| Ayamiss | `kill_first` Hornet; `taunt_after_cast` mode `until_tank`, tank 0 (shut above 70 % by the hp band); `ranged_only` 8 yd |
| Qiraji Gladiator | `linked_death` on itself, hold 5 %, window 7 s |

## Hakkar the Soulflayer (map 309, entry 14834)

Needs `DungeonRunPullGate`, `DungeonRunSeekAura`, `DungeonRunCharmYield` and `DungeonRunBossSpread` (all default `0`), plus aura tactics for the taunt row. The five High Priests must be dead first, one stack of Hakkar Power per living priest; the run leader never pulls while he wears it and ends the run after 30 s.
Before each Blood Siphon (every 90 s) the raid takes a Poisonous Blood dose from a Son held and killed at the fixed spot: DPS hold it at 30 % until the raid gathers within 4 yd, then burn it so the cloud lands on them. Dispel never touches the poison; Corrupted Blood keeps the ranged raid 12 yd apart. Cause Insanity cannot be taunted off, so the `threat_successor` row makes the damage dealers keep below the off-tank: on the reset the off-tank takes the boss instead of a damage dealer. Once the off-tank has fallen well behind the highest tank's threat (under half), the reference is the highest tank again.
The pull holds all damage for 5 s (`dps_hold_after`, no spell, mode `engage`) so the main tank takes Hakkar first, before Cause Insanity (every 20-25 s on his current victim) can charm a damage dealer.


## Known limits

Recurring threat-reset holds need a cast trigger. A `dps_hold_after` row with `spell_id` 0 fires only its engage edge, so it serves the opening 5 s Hakkar pull hold only and never comes back mid-fight (the spell-less Mandokir row was removed). Hakkar's Cause Insanity is answered by the `threat_successor` row's cap at the assigned off-tanks, which is what keeps the boss off the damage dealers; his `taunt_after_cast` row in mode `until_tank` stays inert because the game makes him immune to taunts (see the Hakkar section above).

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
| `dx dy` | Slots 1-8 (default 5): a line of seats 9 yd apart. |
| `window_signal` | `cast` or `stopped_no_target`: still, targetless, with aura. `stopped_no_target` needs `aura` plus `window_spell`. |
| `hold_ms` | Default 8000, range 1000-15000, or until `release_anchor`. |
| `release_anchor` | Anchor that ends the stand. |

Primary key is `(map, boss_entry, anchor, role, window_spell, side, seq)`. Unusable rows are skipped with a log; a bad `release_anchor` for a boss drops that boss's set.

Seats go to same-role bots in id order. Taking part means being on the threat list or victim within 80 yd of the anchor, or holding a stand. Sides come from the lane cross product. While seated, the stand action walks the bot to its seat. After 3 refusals the seat is given up and reach or dodge takes over; seated bots suppress reach-spell, heal walk, dragon flank, and raid spread.

## Adding a boss

- Read the boss from the core: creature template, spells, events, scripts, spell template. Use Turtle ids only.
- Add rows in a NEW migration `data/sql/world/YYYYMMDDHHMMSS_world.sql` (idempotent; never edit an applied one).
- Run the SQL tests with the core checkout set: `tools/test_boss_tactics_sql.py`, `tools/test_boss_stand_sql.py`.
- Check in game with `DungeonTactics = 1` or a commander binding, then a boss pull.

## Not yet verified live

Uldaman, Deadmines, Wailing Caverns, Frostmane Hollow, Windhorn Canyon, Gilneas City, Karazhan Crypt, Crescent Grove, Zul'Farrak, Scholomance, Zul'Gurub, AQ20, Hakkar: none of the rows above are verified in game yet.