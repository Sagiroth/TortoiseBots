---
id: classes-overview
title: Class AI & Roles Overview
category: classes
summary: Summary of the nine Vanilla classes, spec detection, combat roles, and shared behaviors in TortoiseBots.
tags: [classes, overview, roles, specs, combat]
relates_to:
  - classes-warrior
  - classes-paladin
  - classes-hunter
  - classes-rogue
  - classes-priest
  - classes-shaman
  - classes-mage
  - classes-warlock
  - classes-druid
---

# Class AI & Roles Overview

Bots cover all nine Vanilla classes. Each bot reads its spent talent points, picks the matching spec strategy, and fights in that spec's style.

## Supported Roles & Classes

| Class | Tank | Healer | Melee DPS | Ranged DPS | Crowd Control (CC) | Interrupts |
| :--- | :---: | :---: | :---: | :---: | :--- | :--- |
| **[Warrior](warrior.md)** | Prot | - | Arms, Fury | - | Intimidating Shout, Concussion Blow | Shield Bash, Pummel |
| **[Paladin](paladin.md)** | Prot | Holy | Retribution | - | Hammer of Justice, Repentance | Hammer of Justice, Repentance |
| **[Hunter](hunter.md)** | - | - | - | BM, MM, Survival | Freezing Trap, Scare Beast | None (Scatter Shot snares) |
| **[Rogue](rogue.md)** | - | - | All Specs | - | Sap, Blind, Gouge | Kick |
| **[Priest](priest.md)** | - | Holy, Disc | - | Shadow | Shackle Undead, Psychic Scream (PvP and close range only) | Silence (Shadow) |
| **[Shaman](shaman.md)** | Tank (experimental) | Resto | Enhancement | Elemental | None | Earth Shock |
| **[Mage](mage.md)** | - | - | - | All Specs | Polymorph | Counterspell |
| **[Warlock](warlock.md)** | - | - | - | All Specs | Fear, Banish | Spell Lock (Felhunter) |
| **[Druid](druid.md)** | Bear | Resto | Cat | Balance | Entangling Roots, Hibernate | Bash |

---

## Spec Detection & Strategy Selection

The bot counts spent talent points in each of its three class trees. The tree with the most points picks the combat strategy. Hybrid classes also read stance, form, and gear to confirm the role.

Players can use [`.bot role <Name|self>`](../play/commands/roster-and-hiring.md#bot-role) to override the result.
Without an override, role detection uses:
- Party composition, stance or form, and tanking gear.
- A shaman needs a shield, *Rockbiter Weapon*, and *Totemic Alignment* or *Spirit Armor*.
- A bot that rolled the tank build counts as a tank from its talents alone — see [Shaman tank](shaman.md#class-settings).
- A detected shaman tank fights alongside a warrior, paladin, or druid tank instead of standing down.

---

## Shared Bot Behaviors

| Subsystem | Trigger | Bot Behavior |
| :--- | :--- | :--- |
| **Resting & Recovery** | Out of combat, health or mana low. | Sits and eats or drinks to recover. See [Combat settings](../server/settings/combat.md). |
| **Buffs & Imbues** | Buff missing on self or party member. | Recasts class buffs, seals, auras, and weapon poisons out of combat. |
| **Targeting & Assist** | Tank or party leader engages. | Assists the leader's target and leaves crowd-controlled mobs alone. |
| **Finishing Runners** | Enemy mob flees below 30% health. | Ranged DPS finish within spell range; eligible melee close distance. See details below. |
| **Threat Back-Off** | AoE threat at 50% of the tank's, single-target threat climbing past the tank's. | Pauses high-threat spells until threat stabilizes. Controlled by the `threat` strategy. |
| **Healer Off-DPS** | Party stable (heal target at or above 90%), own mana above the fight reserve, and the mob already fighting the group. | Contributes damage; healing always wins. Controlled by the `offdps` strategy. |
| **DPS Off-Healing** | An ally drops into a wound band (see below). | Casts emergency heals instead of attacking. Controlled by the `offheal` strategy. |
| **Spell Downranking** | Friendly target wounded but not in critical danger. | Casts the lowest rank that covers the missing health. |
| **Healer Positioning** | Party member out of heal range or line of sight. | Moves toward the tank or the injured member. |
| **Looting & Gathering** | Corpse or nearby gathering node. | Pool bots approach corpses, loot quest items, and roll. Hired companions pass unless [`AiPlayerbot.HireLoot`](../server/settings/hiring.md#aiplayerbothireloot) is on. |

<details>
<summary>Runner finishing, threat back-off, off-DPS, downranking, and healer positioning details</summary>

### Finishing Runners

When an enemy mob flees below 30% health (fleeing or fleeing for help, still in the fight, and not feared or crowd-controlled):
- Ranged DPS finish the target within spell range (plus a small slack).
- Warriors finish the runner if *Intercept* is ready (in combat, Berserker Stance, rage available, off cooldown) within 25 yards.
- Other melee only attack runners already in melee reach.
- During `stay` holds, melee bots hold position while ranged bots shoot within spell range.
- Healers and tanks never chase runners.
- The active runner is kept until it dies or escapes, and crowd-controlled targets are never chased.
- In a dungeon run, a bot lets a fleeing enemy go when following means dropping more than 6 yards down or losing the path. Stairs and slopes are still chased, and so is an enemy the player ordered the bot to attack. The enemy is left alone for up to 30 seconds. Pool bots and normal groups are not affected.

### Party Threat Back-Off

In groups with the `threat` strategy active, damage dealers watch their threat against the tank. AoE threat at 50% of the tank's pauses area spells (*Volley*, *Multi-Shot*, *Blizzard*, *Flamestrike*, *Rain of Fire*, *Chain Lightning*) until threat stabilizes. Single-target threat pauses nukes only once it is climbing to pass the tank (at or above 80% with a rising trend), not on a flat 80% reading. On a boss whose fight data names a threat successor (`ai_boss_tactic` kind `threat_successor`, shipped for Hakkar) no tank can take the mob back, so the reference is the highest threat among the assigned off-tanks: damage dealers stay below the off-tank, and a threat reset hands the mob to that tank instead of to a damage dealer. A taunt-immune boss without that row (most raid bosses in this world data) keeps the highest tank as the reference, so the raid still has a working cap. On a taunt-immune target, with or without the row, the back-off also holds in a commanded group while the mob hits a member; elsewhere it still yields.

### Healer Off-DPS (`offdps`)

Healing specs get `offdps` by default (Discipline and Holy priests, Restoration shamans and druids, Holy paladins). When it is on, the healer only attacks when every condition holds:
- Nobody needs healing: the heal target (worst-off member, tank favored) is at or above 90% health (`AlmostFullHealth` default).
- The healer's own mana is above the fight reserve: 85% in hard fights, 70% in even or easy fights, 95% in raids.
- The mob already fights the group (or the player explicitly ordered the attack); healers never open on a fresh mob.
- No ordered pull is walking in, and the bot is not the run leader's heal-only healer.

Damage rows sit at the lowest priority, so any firing heal outbids them. Solo or far from the group, the healer fights like a normal bot.

What each class casts: priests use *Shadow Word: Pain*, *Holy Fire*, *Smite*, *Starshards*, *Mind Blast* (*Holy Nova* into packed melee, wand when mana is spent); druids leave Tree of Life Form for *Moonfire*, *Wrath*, *Starfire*; shamans use *Flame Shock* and *Lightning Bolt* (*Chain Lightning* into packs); paladins use *Hammer of Wrath* on dying targets, *Holy Shock*, *Judgement* and *Exorcism* on Undead or Demon targets (*Consecration* into packed melee). See the class pages for details.

### DPS Off-Healing (`offheal`)

Damage specs of hybrid classes get `offheal` by default (Shadow priests, Elemental and Enhancement shamans, Retribution paladins, Cat and Balance druids). When it is on, the damage dealer stops attacking and heals once an ally falls into a wound band:

| Class | Ally below 20% (critical) | Ally at 20–50% (low) | Ally at 50–70% (medium) |
| :--- | :--- | :--- | :--- |
| Priest (Shadow) | *Power Word: Shield*, then *Flash Heal* | *Greater Heal* | *Renew* |
| Shaman (Ele/Enh) | - | *Healing Wave* | - |
| Paladin (Ret) | *Lay on Hands* | *Holy Light* | - |
| Druid (Cat/Balance) | *Regrowth*, then *Healing Touch* | *Regrowth* | - |

Retribution's *Blessing of Protection* on an attacked ally sits in the same off-heal kit at critical priority.
Off-heals outbid damage, so they fire the moment an ally enters a band; outside the bands the bot keeps its normal rotation. Out of combat only the low band (20–50%) heals. Off-heals use the same care as real heals (downranking, no wasteful casts at high health), and are never held back at or below 50% ally health (the `LowHealth` default). A Shadow priest in *Shadowform* skips the medium-band *Renew* while another healer covers the ally; at or below 50% it always heals.

### Switching off-spec behavior on and off

Per bot, use [`.bot behavior <bot|party|raid> offheal|offdps <on|off>`](../play/commands/bot-panel.md#bot-behavior) (the addon's bot panel shows the switch that fits the bot's current spec), whisper the bot, or use [`.bot strategy`](../play/commands/party-actions.md#bot-strategy):
- `/w <BotName> co +offheal` or `/w <BotName> co -offdps` (`co ?` lists active combat strategies).
- `.bot strategy -offdps @heal` for one bot, a role, or the party.
- Changes are saved per bot.
- A spec change (`.bot role <bot> <spec>`, the addon's role buttons, or a talent respec) sets the new spec's default again: a healer spec gets `offdps` and loses `offheal`, a damage spec the other way round, a tank spec neither. The priest `hybrid` spec keeps its own `offdps`.
- Server admins change the default for new bots with [`AiPlayerbot.EnableOffSpecStrategies`](../server/settings/class-behaviour.md#aiplayerbotenableoffspecstrategies) (`0` leaves the off-spec kit off and a spec change turns both off; players can still toggle it per bot).

### Spell Downranking

Healing bots pick the lowest spell rank that covers the target's missing health, to save mana. Full ranks are used at or below critical health (`CriticalHealth`, 20% default). Some lines never downrank: heal-over-time spells, shields, area heals, and cooldown heals.

### Healer Positioning

When a party member is out of heal range or line of sight:
- The healer moves toward that member. The default follow anchor is the tank when nobody is injured.
- If another living healer already has that member in range and sight, the healer does not move unless the member is at or below critical health.
- Silenced, pacified, crowd-controlled, or out-of-mana (below 20%) healers do not count as covering a member.
- Out of combat, healers only move to members below 90% health.

</details>
