---
id: play-boss-tactics
title: Boss Tactics Table
category: play
summary: The per-boss tactics table (ai_boss_tactic) and the opt-in "boss tactics" strategy that applies it - row format, activation rule, supported kinds, the Uldaman rows and how to add a boss.
tags: [guide, dungeon, boss, tactics, uldaman, configuration, commander]
relates_to:
  - play-dungeons-and-raids
  - server-configuration
  - developers-strategy-engine
  - developers-architecture
---

# Boss Tactics Table

> Work in progress. These features are not officially released and not supported yet. Enable them at your own risk. Until the official release, bug reports and feedback about them are not handled through the issue tracker; contact Hypeer directly.

A boss fight has a few mechanics that generic combat AI cannot know: which spell to interrupt, which add to kill first, where not to stand. This page describes the data table that holds them, the strategy that applies them, and the first content (Uldaman up to Ironaya, plus the Obsidian Sentinel).

It is **off by default**. A normal player's bots play exactly as before.


> **Interrupt rule (corrected 2026-10-20):** the core stops a cast only when the spell has preventionType silence (1) AND, for a generic cast, interruptFlags with the damage bit 0x02 (SpellEffects.cpp, Spell::EffectInterruptCast), plus castingTimeIndex > 1. Earlier phases checked castingTimeIndex and flag bits only; the audit in pjerra/turtle-agent#246 removed the rows that failed (Tan'sha 3143, Shalk's add 36732, and on 2026-10-29 Grimlok's Chain Bolt 8292).

## When it is active

The `boss tactics` strategy is added to a bot by the `enter boss tactics` trigger of the `dungeon` strategy (re-checked every 5 ticks, like the per-dungeon strategies) when all of these hold:

1. the bot is on a map that has at least one row in `ai_boss_tactic`, and
2. either `AiPlayerbot.DungeonTactics = 1` (the server owner opted in; default `0`), **or** the bot is in a bound group (`.bot commander bind`: a remote bind needs `AiPlayerbot.RemoteCommander = 1`, an `inparty` bind needs only `AiPlayerbot.DungeonSelfClear = 1`; the run leader needs only the run leader gate).

When the bot leaves the map, or the key is switched off and the bot is no longer in a bound group, `leave boss tactics` removes it again. The gate is `BossTacticsWanted` in `ai/playerbot/BossTacticsPolicy.h`.

Why the commander binding counts: those runs are the ones that exist to clear a dungeon with bots, and a commander cannot give interrupt or kill-order orders fast enough. Why the key stays `0`: the tactics change how bots play (a bot may leave its target to kill an add, or step out of a boss's front), and the project rule is that new automation ships off by default.

## The table

`ai_boss_tactic`, created and filled by `data/sql/world/*_world.sql` migrations. It is read once at startup into a cache (one pass, `ai/playerbot/BossTacticsMgr.cpp`); nothing reads the database per tick. A change needs a restart.

| Column | Meaning |
| :--- | :--- |
| `map` | Map id (Uldaman is 70). |
| `boss_entry` | Creature entry whose presence among the bots' attackers activates the row. |
| `kind` | `interrupt`, `kill_first`, `behind` or `avoid_cast` (below). |
| `target_entry` | The creature the row is about; `0` means the boss itself. |
| `spell_id` | `interrupt`: the spell to interrupt; `avoid_cast`: the cast to walk out of. |
| `priority` | `kill_first`: lower is killed first. |
| `param` | `behind`: yards around the boss in which its front is kept clear; `avoid_cast`: yards around the caster bots leave (the blast radius plus a margin). |
| `comment` | Free text; where the row came from. |

The primary key is `(map, boss_entry, kind, target_entry, spell_id)`. The loader skips a row it cannot use (unknown kind, an `interrupt` without a spell, a `behind` without a radius, an `avoid_cast` without a spell or a radius, a duplicate key) and logs how many it skipped; `tools/test_boss_tactics_sql.py` rejects the same rows at commit time. With `TORTOISE_CORE=~/tortoise-wow` set, the same test also checks every live `interrupt` row against the core's data: that the spell exists and the core's interrupt rule can stop it, that the boss and target templates exist, and that the boss spawns on its map; without the variable those checks are skipped.

### Kinds

| Kind | What bots do | Who |
| :--- | :--- | :--- |
| `interrupt` | While a creature in the fight (`target_entry`, or the boss) casts `spell_id`, a bot that knows an interrupt and has it ready in range uses it: *Pummel*, *Shield Bash*, *Kick*, *Counterspell* or *Earth Shock*, first castable wins. The class interrupts that already fire on the bot's current target are unchanged. | any bot with an interrupt |
| `kill_first` | Takes the listed creature as its target before anything else; among several listed creatures the lowest `priority` goes first, the nearer one breaks a tie, nothing further than 40 yd is picked. While on that target the generic assist and AoE rows do not pull it back to the boss. | DPS only; never tanks or healers; an explicit `attack` order from a player wins |
| `behind` | A bot that stands within `param` yd of the boss, in its front half, steps to a point 120 degrees off its facing at the same distance (the same geometry as the generic rear flank). | everyone except the bot the boss is attacking |
| `avoid_cast` | While a creature in the fight (`target_entry`, or the row's entry itself, trash included) casts `spell_id`, a bot whose centre is within `param` yd of it (3D) walks to a point 2 yd past that radius: straight out first, then 45, 90 and 135 degrees to either side, skipping a point another listed cast covers or the bot cannot see. A cast of its own is stopped: the walk runs in the reaction engine, which acts while the bot is mid-cast, and it also runs for a bot the run holds on a stay or in a pull's join window. Until the cast ends or the caster dies, its reaches towards that caster, and a walk back to a stay spot inside the blast, are held, so a bot does not walk straight back in; then the normal reach takes it back. It outranks the generic step out of a ground effect. With `bot_events.csv` in `AiPlayerbot.AllowedLogFiles` (the default) each bot writes one `BossTacticsAvoidCast` row per cast: `caster=<entry>:<guid low>,spell=<id>` and `dist=<yd to the caster before the move>,radius=<yd>,dest=<yd of the chosen point, or none>,stopped=<1 if it broke its own cast>`. Pair it with an `interrupt` row when a kick can stop the spell. | everyone except the tank the caster is on (an off-tank leaves) |

Not in the table yet, and why: tank swap and re-grab (Ironaya's *Knock Away*), dispel priorities, spread and ground-effect avoidance. The generic raid hazard, spread and dispel code already covers part of it, and each of these needs a live fight to tune before it gets a kind. Adding one means a new `BossTacticKind`, a scan function and an action next to the existing four.

## Uldaman rows (map 70)

Every creature and spell was read from Turtle's own data (`tortoise-wow`: `creature_template`, `creature_spells`, `creature_ai_events`/`creature_ai_scripts`, `spell_template`, `src/scripts/dungeons/uldaman`), not from another core's tables.

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Revelosh (6910) | `interrupt` Chain Lightning 16006 and 15117 | Spell list 69100 casts 16006 (first at 8-13 s, then every 18-25 s) and a Lightning Bolt 15801 filler; the template's `spell_id1` is 15117. Only Chain Lightning is listed so interrupts are not burnt on the filler. |
| Baelog 6906, Eric "The Swift" 6907, Olaf 6908 | `kill_first` Baelog (1), Eric (2), Olaf (3), under all three entries | Baelog is the only dwarf with a ranged attack (*Shoot* 15620) and flees at 15 % health (event 690601); Eric opens with *Rushing Charge* 6268; Olaf has the most health (5867) and the *Shield Slam* stun 8242. The order is a judgement from that data, not something the core states. |
| Ironaya (7228) | `behind`, 10 yd, spell 8374 | `boss_ironaya` casts *Arcing Smash* 8374 on herself every 13 s; it is a cone (effect target 24) with radius index 8, which is the *War Stomp* radius (8 yd by its tooltip; the client DBC was not available, so 8 yd plus a 2 yd margin). Her *Knock Away* 10101 below 50 % health wipes the victim's threat and is not handled yet. |
| Obsidian Sentinel (7023) | `kill_first` Obsidian Shard 7209 | EventAI events 702301-702304 cast *Summon Obsidian Shard* 10061 (creature 7209) and *Splintered Obsidian* 10072 at 80, 60, 40 and 20 % health. |
| Grimlok (4854) | `interrupt` Lightning Bolt 9532 (the `interrupt` Chain Bolt 8292 row was removed 2026-10-29: interruptFlags 0x9 lacks the damage bit 0x02, a kick cannot stop it) | Spell list 48540 casts Chain Bolt 8292 (castingTimeIndex 19, interruptFlags 9) and Lightning Bolt 9532 (castingTimeIndex 14, interruptFlags 15), both silence-prevented (preventionType 1); only 9532 carries the kick bits. Shrink 11892 gets no row (not in the plan interrupt list); Tremor Totem 8143 is instant (castingTimeIndex 1). |

What needs a live check (not verified in game): the Lost Dwarves order and whether the three can be pulled apart, that 10 yd is right for the Arcing Smash cone, that the interrupt fires in time on Chain Lightning.

## Deadmines rows (map 36)

Every creature and spell was read from Turtle's own data (`tortoise-wow`: `creature_template`, `creature_spells`, `creature_ai_events`/`creature_ai_scripts`, `spell_template`), not from another core's tables.

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Jared Voss (61961) | `interrupt` Poison Cloud 3815 | Spell list 180300 casts Poison Cloud 3815 (7-7 s first, every 16-26 s), a cast (castingTimeIndex 4) that leaves a periodic-damage cloud centred on the caster (target type 22, self); the other three list spells (Poison 13298, Withering Poison 13884, Poisonous Stab 7357) are instant (castingTimeIndex 1), so they are not listed. EventAI is say-on-aggro/death only (2200058/2200059). |
| Masterpiece Harvester (61963) | no row | No spell list, no C++ script; EventAI is only say-on-aggro/death (2200054/2200055). Unimplemented in the core. |
| Captain Greenskin (647) | `interrupt` Poisoned Harpoon 5208 | Spell list 6470 (and inline `spell_id2`) casts Poisoned Harpoon 5208 (castingTimeIndex 5), silence-prevented with the kick bits (interruptFlags 15). Cleave 15584 is instant (castingTimeIndex 1). No ai_name. |

What needs a live check (not verified in game): that the Poison Cloud cast is actually interruptible in time at its cadence.

## Wailing Caverns rows (map 43)

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Zandara Windhoof (61968) | `interrupt` Chain Lightning 15659 and Gust of Wind 6982 | Spell list 180306 casts Chain Lightning 15659 (every 10 s, castingTimeIndex 14) and Gust of Wind 6982 (4-4 s first, every 11-14 s, castingTimeIndex 5), an AoE stun (effectApplyAura 12). EventAI is say-on-aggro/death only (2200062/2200063). |
| Vangros (61965) | no row | Spell list 180303 casts Crush Armor 21055 and Knock Away 18670, both instant (castingTimeIndex 1, interruptFlags 0); Knock Away is a single-target threat-drop, not a frontal cone. No ai_name or script. |
| Lord Serpentis (3673) | `interrupt` Healing Touch 6778, Sleep 700 and Lightning Bolt 9532 | Spell list 36730 casts Healing Touch 6778 (castingTimeIndex 22), Sleep 700 (castingTimeIndex 16) and Lightning Bolt 9532 (castingTimeIndex 14), all silence-prevented with the kick bits (interruptFlags 15). |
| Lord Cobrahn (3669) | `interrupt` Druid Slumber 8040, Lightning Bolt 9532 and Healing Touch 5187 | Druid-form spell list 36690 casts Druid Slumber 8040 (castingTimeIndex 19) and Lightning Bolt 9532 (castingTimeIndex 14); Healing Touch 5187 is the plan list third spell (castingTimeIndex 20, interruptFlags 15, preventionType 1). |
| Lord Pythas (3670) | `interrupt` Healing Touch 5187, Sleep 700 and Lightning Bolt 9532 | Spell list 36700 casts Healing Touch 5187 (castingTimeIndex 20), Sleep 700 (castingTimeIndex 16) and Lightning Bolt 9532 (castingTimeIndex 14), all silence-prevented with the kick bits. Thunderclap 8147 is instant (castingTimeIndex 1). |
| Skum (3674) | `interrupt` Chained Bolt 6254 | No spell list; Chained Bolt 6254 is the inline `spell_id1`, a real cast (castingTimeIndex 23), silence-prevented with the kick bits. |
| Mutanus the Devourer (3654) | `interrupt` Naralex Nightmare 7967 | Spell list 36540 casts Naralex Nightmare 7967 (first at 8-12 s, every 18-25 s, castingTimeIndex 5), silence-prevented with the kick bits. Terrify 7399 is instant (castingTimeIndex 1). |

What needs a live check (not verified in game): that the interrupts fire in time on the 11-14 s Gust of Wind window.

## Frostmane Hollow rows (map 822)

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Kan'za the Seer (63129) | `interrupt` Frostbolt 837 | Spell list 63129 casts Blizzard 6141 (a channel) and Frostbolt 837 (5-10 s first, every 6-10 s), a real cast (castingTimeIndex 24, cast time table not in the repo; treated as a real cast because interruptFlags 15, damage + slow). Blizzard 6141 has no row: the interrupt scan reads CURRENT_GENERIC_SPELL only, so channels are never seen. EventAI is aggro-line only (6312901). |
| Tan'sha the Sleek (63133) | no row (was `interrupt` Glacial Roar 3143, removed 2026-10-20: preventionType 0, a kick cannot stop it) | Spell list 63133 casts Glacial Roar 3143 (20-21 s first, every 17-24 s, castingTimeIndex 4, preventionType 0), an AoE damage + stun (effectApplyAura 12, radius index 8). Her EventAI Intimidating Shout 19134 at 50% health is instant. |
| Hailar the Frigid (63130) | `interrupt` Frostbolt 8406 | The script (`boss_hailar_the_frigid`) only says lines and drives the ritualists; his casts come from spell list 63130 (Frostbolt 8406, 1-3 s first, every 15-20 s, castingTimeIndex 14, and Flash Freeze 16803 every 24-28 s, an instant AoE stun, so no row). |
| Battlemaster Ubukaz (63131) | no row | EventAI, no spell list; only an aggro line (6313102) and Enrage 28131 at 25% health (6313101, self-buff, instant). |

Hailar has no `kill_first` row for the Frostmane Ritualists: the ritualists (entry 36519, `npc_frostmane_ritualist`) heal Hailar with Frostmane Ritual 41592 (`boss_hailar_the_frigid.cpp:118`) but are `REACT_PASSIVE`, ignore attacks (`boss_hailar_the_frigid.cpp:75,87-97`) and never enter the bots' attacker list, so the scan almost never sees them. This needs a code change to the scan (a source of nearby creatures of `target_entry` not in combat) before a `kill_first` row can work.

What needs a live check (not verified in game): that interrupting Hailar's Frostbolt is worth the global, and that Poison Cloud and the two Frostmane stuns are interruptible in time at their cadences.

## Windhorn Canyon rows (map 820)

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Prophet Stormhoof (62781) | `interrupt` Corruption 11672 | Spell list 62781 casts Corruption 11672 (2-3 s first, every 17-22 s, castingTimeIndex 16, interruptFlags 15, periodic-damage aura 3). Curse of Agony 11712 and Wail of Souls 57748 are instant. EventAI is aggro/50 %/death lines only. |
| Chieftain Shalk Blackwind (62782) | `interrupt` Flame of Shalk 41121 (the `interrupt` Bloodguard Ritual 36732 row on the add 62771 was removed 2026-10-20: preventionType 0, a kick cannot stop it) | Spell list 62782 casts Flame of Shalk 41121 (11-13 s first, every 32-47 s, castingTimeIndex 5, interruptFlags 15, periodic fire damage plus a healing debuff, radius index 22); Berserker Charge 19471 is instant. At 50 % health the script summons one Blackwind Bloodguard (62771) and attack-starts it on a random attacker (`src/scripts/dungeons/windhorn_canyon/windhorn_canyon.cpp:37-46`), so the add is on the bots' attacker list; the add's EventAI event 6277101 casts Bloodguard Ritual 36732 at its own 50 % health (castingTimeIndex 171, interruptFlags 15, preventionType 0, cast on self). |
| Ambassador Vortalus (62783) | `interrupt` Gust of Wind 6982 | Spell list 62783 casts Gust of Wind 6982 (4-5 s first, every 14-19 s, castingTimeIndex 5, interruptFlags 31), an AoE stun (aura 12, radius index 8); Chain Lightning 22355 is instant, and the EventAI cast Engulfing Vortex 36578 at 50 % health (6278301) is instant too. |
| Ahgk'tos the Pure (62778) | no row | Spell list 62778 is all instant: Lightning Cloud 23105 (ground area), Lightning Shield 945 (self) and Wing Flap 12882 (a frontal cone knock back, implicit target 24, radius index 13 — no `behind` row because the client SpellRadius table is not in the repo, so no yard value can be given). The creature has EventAI assigned but no EventAI rows and no C++ script. |
| Pathun Duskhide (62779) | no row | Spell list 62779: Kodo Stomp 6266 and Demoralizing Shout 11554 are instant; the only cast is Hooked Net 14030 (castingTimeIndex 3) with interruptFlags 9, without the kick bit. EventAI is an aggro/50 %/death line plus a threat reset at 50 % health (6277901, `MODIFY_THREAT`). |
| Bonespeaker Narlgom (62780) | no row | No spell list at all (`spell_list_id` 0 and no `creature_spells` row for the entry). The script (`boss_bonespeaker_narlgom.cpp`) casts Bone Armor 11445 (instant), Rain of Fire 57747 (a channel: channelInterruptFlags 31788) and Summon Rotag 42004 (castingTimeIndex 6 but interruptFlags 9, no kick bit). |
| Walgan Bloodcaller (62784) | no row | Spell list 62784: Cleave 16044 (instant, radius index 0, so no `behind` row) and War Stomp 16727 (instant, self-centred stun). EventAI Enrage 28131 at 15 % health (6278401) is instant. |

Two things about the data shape are worth knowing when these rows are revisited. First, six of the seven lists are attached by a separate migration, not by the row that creates the creature: `UPDATE creature_template SET spell_list_id = entry ...` in `sql/database_updates/world/20260608174707_world.sql`, and Narlgom (62780) has no list at all — reading only the `creature_template` REPLACE from the May file would suggest every Windhorn boss casts nothing. Second, no `behind` row exists for this instance: the cleaves (Walgan 16044, plus Raxxieth and Marrowspike in other instances) have radius index 0, and the only cone (Ahgk'tos Wing Flap 12882, radius index 13) has no yard value in the repo.

Champion Rotag (62785) gets no `kill_first` row although Bonespeaker Narlgom summons him at 50 % health: his script (`npc_champion_rotag`) is `REACT_PASSIVE`, overrides `AttackStart` to do nothing, stops combat on every update and the template is `UNIT_FLAG_IMMUNE_TO_PLAYER` (unit_flags 33024), so he never enters the bots' attacker list and can never be targeted. The summon itself is not a kill-first case either: the bloodguard is targetable (faction 16, no blocking flag, attack-started by the script) but it is a single 2.8 k health add whose only special mechanic is the ritual covered by the row above, and generic assist already kills it.

What needs a live check (not verified in game): whether Shalk's Flame of Shalk lands on players at all (the script polls for that aura to strip it when a player goes underwater, `windhorn_canyon.cpp:62`) and whether kicking the Bloodguard's ritual is worth the global.

## Gilneas City rows (map 815)

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Genn Greymane (61418) | `interrupt` Fear 22678 | Spell list 180166 (not run: the script never calls the base AI update; it casts Drain Life, Mortal Strike and Curse of Agony itself, `instance_gilneas_city.cpp:197/206/217`) has only Drain Life 17620 (a channel: castingTimeIndex 1 with channelInterruptFlags 31788), Mortal Strike 21551 and Curse of Agony 11711, both instant. The script casts Fear 22678 once below 50 % health (`instance_gilneas_city.cpp:230`, castingTimeIndex 16, interruptFlags 15). |
| Packmaster Ragetooth (61420) | `interrupt` Howl of Terror 5484 and Shadow Bolt 11660 | Spell list 180168: Howl of Terror 5484 (first at 0 s, every 30 s, castingTimeIndex 5, interruptFlags 15) is an AoE fear (aura 7, radius index 13); Shadow Bolt 11660 (2-2 s first, every 12 s, castingTimeIndex 14, interruptFlags 15). |
| Judge Sutherland (61421) | `interrupt` Smite 6060, Mind Blast 10945, Fireball 8402 | Spell list 180169: castingTimeIndex 19, 16 and 22, all with interruptFlags 15; every 6, 10 and 8 s. Blast Wave 13019 is instant. |
| Marshal Magnus Greystone (61423) | `interrupt` Holy Fire 17142 | Spell list 180171: Holy Fire 17142 (every 25 s, castingTimeIndex 6, interruptFlags 15) damages and applies a periodic-damage aura (aura 3). Slam 11605 is a cast (castingTimeIndex 20) but its interruptFlags is 8, without the kick bit; Sunder Armor 8380 is instant. |
| Matthias Holtz (61419) | no row | Spell list 180167 and EventAI: Hooked Net 13608 is a cast (castingTimeIndex 3) but with interruptFlags 9, without the kick bit; Fire Blast 8413 (list 180167, castingTimeIndex 1) and Multi-Shot 2643 (EventAI 2200030) are instant. |
| Dustivan Blackcowl (61422) | no row | Spell list 180170 is Rupture 15583 and Rend 11574, both instant; EventAI casts Evasion 5277 and Gouge 12540 (2200031/2200032), both instant. |
| Regent-Lord Mortimer Harlow (61264) | no row | The script (`boss_lord_mortimer.cpp:41,53,63`) casts Holy Strike 2495, Consecration 20924 and Blast Wave 13021, all instant (castingTimeIndex 1). No spell list. |

Mortimer and Celia Harlow (61263) come as a pair (`docs/play/dungeon-reference.md`), but Celia is not in the phase 0 survey and gets no row here.

What needs a live check (not verified in game): that the interrupts fire in time on Sutherland's 6 s Smite and Greystone's 25 s Holy Fire, and whether the Fear at 50 % is worth a kick a bot may have spent earlier.

## Karazhan Crypt rows (map 800)

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Archlich Enkhraz (91916) | `interrupt` Fear 12096 and Freeze 18763 | Spell list 201002: Fear 12096 (first at 12-17 s, every 15-16 s, castingTimeIndex 16, interruptFlags 15) and Freeze 18763 (first at 4-9 s, every 8-14 s, castingTimeIndex 22, interruptFlags 15). Chill Nova 18099 is instant; Consuming Shadows 17715 is a channel (channelInterruptFlags 31788). |
| Corpsemuncher (91917) | `interrupt` Acid Spit 25052 | Spell list 201051: Acid Spit 25052 (first at 1 s, every 4 s, castingTimeIndex 14, interruptFlags 15). Sweep 26103 (castingTimeIndex 16, interruptFlags 31) is a knock back around the caster (effectImplicitTargetA 22, effect 98), not a frontal cone, so it gets no `behind` row. |
| Commander Andreon (91919) | `interrupt` Mind Blast 26048 | Spell list 180104: Mind Blast 26048 (first at 3-4 s, every 45 s, castingTimeIndex 16, interruptFlags 15). Banshee Curse 16867 (cast on self, instant) and Mortal Strike 15708 (instant) get no row, and the template's inline spell_id2 5884 is not cast. |
| Marrowspike (91920) | no row | Spell list 201052 is all instant: Cleave 15579 (castingTimeIndex 1 and no radius index, so no `behind` row), War Stomp 16727 (self-centred 8 yd stun) and Knock Away 10101. |
| Hivaxxis (91929) | no row | Spell list 201053 is all instant: Web Spray 12252, Paralyzing Poison 3609 and Poison Mind 19469 (castingTimeIndex 1). |
| Guard Captain Gort (92935) | no row | Spell list 111210 has one spell, Banshee Curse 16867, instant. No C++ script, and the inline spell_id2 5884 is not cast. |

Alarus (91928, summoned by the rune event, listed as the crypt's end boss in `docs/play/dungeon-reference.md`) is not in the phase 0 survey and gets no row in this migration.

What needs a live check (not verified in game): the cast cadence of Fear and Freeze (the list delays are 4-17 s) and whether the 4 s Acid Spit is worth interrupting at all.

## Crescent Grove rows (map 802)

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| High Priestess A'lathea (92108) | `interrupt` Starfire 25298 | Spell list 201059: Starfire 25298 (first at 1 s, every 5 s, castingTimeIndex 22, interruptFlags 15). Moonfire 23380 is instant; the inline spell_id4 9613 is not cast. |
| Keeper Ranathos (92109) | `interrupt` Wrath 6780, Entangling Roots 11922, Regrowth 8941 | Spell list 201060: Wrath 6780 (first at 1 s, every 2 s, castingTimeIndex 5, interruptFlags 15), Entangling Roots 11922 (every 15-20 s, castingTimeIndex 16, interruptFlags 15, aura 26) and Regrowth 8941 (every 28 s, castingTimeIndex 5, interruptFlags 15), a self-heal (cast target 0, aura 8). |
| Master Raxxieth (92110) | `interrupt` Shadow Bolt 12471 and Flamestrike 12468 | Spell list 201061: both every 12 s, castingTimeIndex 14, interruptFlags 15; Flamestrike is aimed at a random attacker other than the top one (cast target 5). Cleave 15579 is instant and has no radius index, so no `behind` row. |
| Grovetender Engryss (92107) | no row | Spell list 201058 is all instant: Strike 12057, Knock Away 10101 and Rejuvenation 9839 (a friendly heal, cast target 15). EventAI is a say line only (2200008). |
| Fenektis the Deceiver (92111) | no row | Spell list 201062 is all instant: Poisonous Stab 7357, Sinister Strike 15667 and Blade Flurry 13877. |

What needs a live check (not verified in game): whether interrupting Ranathos' 2 s Wrath spam starves the kick for Entangling Roots and Regrowth, and whether the 5 s Starfire is interruptible in time.

## Zul'Farrak rows (map 209)

Rows added 2026-10-20 (`data/sql/world/20261020083400_world.sql`), read from `tortoise-wow` 5d79486. All three bosses carry their list through `creature_template.spell_list_id`; the cast-time rule is the same as above (no SpellCastTimes.dbc, so `castingTimeIndex` above 1 plus the kick bits in `interruptFlags`).

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Hydromancer Velratha (7795) | `interrupt` Healing Wave 12491, Shadow Bolt Volley 15245 | Spell list 77950 (also inline `spell_id1-4`): Healing Wave 12491 (first at 0 s, every 9-22 s, castingTimeIndex 14, interruptFlags 15, heal, cast target 15) and Shadow Bolt Volley 15245 (first at 5-10 s, every 17-22 s, castingTimeIndex 14, interruptFlags 15, radius index 10). Ward of Zumrah 11086 (every 13-28 s) is instant (castingTimeIndex 1, interruptFlags 0) and gets no row. Shadow Bolt 12739 is a real cast (castingTimeIndex 14, interruptFlags 15) but is deliberately not listed: its repeat timer is 2 s at 90 %, and `FindBossInterrupt` takes the first listed spell in progress with no priority, so it would eat the kick before the 9-22 s heal (the same reason as Revelosh's Lightning Bolt filler and Sezzziz's Shadow Bolt 15537). |
| Shadowpriest Sezzziz (7275) | `interrupt` Heal 12039 and Renew 8362 | Spell list 72750: Heal 12039 (first at 0 s, every 14-25 s, castingTimeIndex 14, interruptFlags 15, cast target 15) and Renew 8362 (every 28-35 s, castingTimeIndex 5, interruptFlags 15, cast target 15, aura 8). Renew is a real cast, not an instant (the phase 1b brief had assumed instant); the re-verification settled it. Psychic Scream 13704 (every 22-32 s) is instant (castingTimeIndex 1, interruptFlags 8, radius index 14). Shadow Bolt 15537 (repeat timer 2-4 s, castingTimeIndex 14) is the filler bolt and gets no row, so kicks are kept for the two heals. EventAI only has the 15 % flee event 727501. |
| Antusul (8127) | `interrupt` Flash Heal 10915, Healing Wave of Antusul 11895, Healing Ward 11899; `kill_first` Servant of Antusul 8156, priority 1 | Spell list 81270 and EventAI: Flash Heal 10915 (script 812709 at or below 60 % health, every 17-20 s, castingTimeIndex 16, interruptFlags 15); Healing Wave of Antusul 11895 (scripts 812708 at or below 30 % every 12 s and 812707 once at 20 %, castingTimeIndex 4, interruptFlags 15); Healing Ward 11899 (a random action of event 812704, scripts 812724/812725, first at 11.6 s then every 13.3-21.7 s, castingTimeIndex 5, interruptFlags 15, effect 87 summons Greater Healing Ward 8179). All three are silence-prevented (preventionType 1), so the interrupt stops the cast and no heal or ward happens. Earthgrab Totem 8376 (the other random action, script 812724, castingTimeIndex 3, interruptFlags 13, preventionType 0) gets no row: 13 is 0x0D and lacks `SPELL_INTERRUPT_FLAG_DAMAGE` (0x02), which `EffectInterruptCast` requires for a generic cast (`src/game/Spells/SpellEffects.cpp:3392`), so a kick would not stop it. The servant: events 812705/812706 cast Antusul Minion 11894 at 75 % and 25 % health (effect 42, `effectMiscValue1` 8156; one then two servants); 8156 is faction 107, unit_flags 0, no EventAI and no script, so it is reasoned to join the fight and the attackers list, not traced. The row is active while Antusul is among the attackers, which is when the servants appear. |
| Chief Ukorz Sandscalp (7267) and Ruuzlu (7797) | no row | Ukorz (list 72670) casts Cleave 15496 first at 6-20 s then every 8-19 s; Ruuzlu (list 77970) casts it first at 7-12 s then every 7-18 s. Cleave is effect 58 on a unit target (implicit target 6), radius index 0 and no radius in the spell text, so no yard value exists for a `behind` row. Ukorz also casts Wide Slash 11837 (first at 12-16 s, then every 7-24 s), a cone (implicit target 24) with radius index 13, but the client SpellRadius table is not in the repo (same reason as Ahgk'tos Wing Flap in Windhorn). Ruuzlu casts no other spell; both open with Berserker Stance 7366 on themselves (instant). |
| Witch Doctor Zumrah (7271) | no row for Ward of Zumrah 7785 | The ward is a ScriptedAI totem (`src/scripts/dungeons/zulfarrak/zulfarrak.cpp:605-632`) that casts Skeleton of Zumrah 11088 every 5 s and does not attack in that script; it is reasoned (not traced) that it never enters the bots' attacker list, so a boss-tactics `kill_first` row could not pick it. The Zul'Farrak run-leader plan already names 7785 in its own `kill_first` column; the migration does not duplicate it. |

Not covered yet, though they have qualifying casts (a real cast and the 0x02 interrupt bit): Zumrah 7271 (spell list 72710: Shadow Bolt 12739, Shadow Bolt Volley 15245, Healing Wave 12491), Gahz'rilla 7273 (spell list 72730: Icicle 11131, castingTimeIndex 16, and Freeze Solid 11836, castingTimeIndex 19, both interruptFlags 15), Theka the Martyr 7272 (spell list 72720) and Nekrum Gutchewer 7796 (spell list 77960), both with Fevered Plague 8600 (castingTimeIndex 30, interruptFlags 31). They need their own rows before those fights can rely on interrupts.

What needs a live check (not verified in game): that the kicks land on Velratha's 9-22 s Healing Wave and Sezzziz's heals, that Antusul's Flash Heal/Healing Wave/Healing Ward casts are interrupted in time, and that the DPS bots switch to the servants in both waves before the boss dies.


## Scholomance rows (map 289)

Rows added 2026-10-24 (`data/sql/world/20261024153900_world.sql`), read from `tortoise-wow` 5d79486. Map 289 had no rows before, so the `boss tactics` strategy now also activates for a bound party on map 289; only these two bosses have rows.

| Boss (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Vectus (10432) | `kill_first` Marduk Blackpool 10433, priority 1; `interrupt` Flamestrike 18399 | One creature group (`creature_groups` (48805,48806,0,0,2), flags 2 `OPTION_AGGRO_TOGETHER`): Vectus and Marduk aggro together with no distance limit, so the boss row fights both. Marduk first: his Defiling Aura 17695 (EventAI 1043302, on aggro; aura 133, -100 shadow resistance on enemies, radius index 10) and Shadow Bolt Volley 17228 (spell list 104330, every 6-14 s, instant, radius index 10) reach the whole party and the aura amplifies the Volley, while Vectus' Blast Wave 16046 (instant, around himself) and melee hit the tank and the melee only; Marduk also has less health (22970 vs 24420). Interrupt: Flamestrike 18399 every 30 s (castingTimeIndex 14, preventionType 1, interruptFlags 15, so kickable by the boss-tactics rule); Blast Wave 16046 is instant and gets no row, and there is no `behind` row (no frontal cone). |
| Marduk Blackpool (10433) | `kill_first` Marduk Blackpool 10433, priority 1 | Same row under Marduk: it is active while only he is on the attacker list. No interrupt row (Shadow Bolt Volley 17228 and Cleave 15284 are instant; Shadow Shield 12040 is self-cast). |

Kill order reason: the whole-party shadow damage of Marduk (Volley plus aura, hitting even the 4.1k hp priest) runs longer if Vectus dies first. No CC on either (both undead, immune masks; the plan's `cc_priority` never applies to a boss row).

What needs a live check: that Marduk dies first with Vectus on the tank, that Flamestrike is kicked in time.

## Zul'Gurub trash rows (map 309)

Row added 2026-10-30 (`data/sql/world/20261030160000_world.sql`), read from `tortoise-wow` 5d79486; the boss rows of map 309 come from the run plan migrations.

| Creature (entry) | Row | Data behind it |
| :--- | :--- | :--- |
| Gurubashi Bat Rider (14750) | `avoid_cast` Unstable Concoction 24024, 13 yd | `npc_guru_bat_riderAI` (`boss_jeklik.cpp`) casts it on itself once, below 40 % health, after turning fear-immune: a 3 s cast (castingTimeIndex 14), 4375-5625 fire damage to every enemy within 10 yd (radius index 13), then it kills itself. The core hits a player whose centre is within 10 yd in 3D, so the row keeps 10 + 3. No `interrupt` row: preventionType 0 and interruptFlags 9 (no damage bit), and the template is immune to stun, knockout and silence (mechanic_immune_mask 1019952979), so nothing a bot has stops the cast. Two live walks lost 6-11 of 19 tier-1 bots within 5 s of a rider dropping to 30-45 %. |

What needs a live check: that the melee and healers leave the 13 yd before the 3 s cast ends, that the blast hits nobody but the tank, and that the melee come back to their target after it.

## Adding a boss

1. Read the boss from the core: `creature_template` (`spell_list_id`, `ai_name`), `creature_spells`, the `creature_ai_events`/`creature_ai_scripts` rows or the `src/scripts` boss file, and `spell_template` for each spell. Do not use another core's ids.
2. Add the rows in a **new** migration `data/sql/world/YYYYMMDDHHMMSS_world.sql` (idempotent: `DELETE` the map's rows, `INSERT` them again). Never edit an applied migration.
3. Run `python3 tools/test_boss_tactics_sql.py`, then check it in game with `AiPlayerbot.DungeonTactics = 1` or a commander binding and a boss pull.

Behaviour is based on the mod-playerbots per-map raid strategy pattern, reimplemented as data (no donor code; the source is named in its PR).
