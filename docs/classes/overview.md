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

TortoiseBots implements combat and utility AI for all nine Vanilla classes (Warrior through Druid). Bots detect their active talent spec dynamically, execute class-appropriate rotations, and support Turtle WoW 1.18.1 custom spells and talents.

## Supported Roles & Classes

| Class | Tank | Healer | Melee DPS | Ranged DPS | Crowd Control (CC) | Interrupts |
| :--- | :---: | :---: | :---: | :---: | :--- | :--- |
| **[Warrior](warrior.md)** | Prot | - | Arms, Fury | - | Intimidating Shout, Concussion Blow | Shield Bash, Pummel |
| **[Paladin](paladin.md)** | Prot | Holy | Retribution | - | Hammer of Justice, Repentance | Hammer of Justice, Repentance |
| **[Hunter](hunter.md)** | - | - | Survival (ranged-first hybrid) | BM, MM | Freezing Trap, Scare Beast | None (Scatter Shot snares) |
| **[Rogue](rogue.md)** | - | - | All Specs | - | Sap, Blind, Gouge | Kick |
| **[Priest](priest.md)** | - | Holy, Disc | - | Shadow | Shackle Undead, Psychic Scream, Chastise | Silence (Shadow) |
| **[Shaman](shaman.md)** | Tank (experimental) | Resto | Enhancement | Elemental | None | Earth Shock |
| **[Mage](mage.md)** | - | - | - | All Specs | Polymorph | Counterspell |
| **[Warlock](warlock.md)** | - | - | - | All Specs | Fear, Banish | Spell Lock (Felhunter) |
| **[Druid](druid.md)** | Bear | Resto | Cat | Balance | Entangling Roots, Hibernate | Bash |

---

## Spec Detection & Strategy Selection

```mermaid
flowchart TD
    Talents["Spent Talent Points (Talent.dbc)"] --> Tally["AiFactory::GetPlayerSpecTab (Count Tab Points)"]
    Tally --> Tree{"Dominant Talent Tree?"}
    Tree -->|Tab 1 Highest| Spec1["Primary Spec Strategy (e.g. Arms, Holy, Assassination)"]
    Tree -->|Tab 2 Highest| Spec2["Secondary Spec Strategy (e.g. Fury, Protection, Combat)"]
    Tree -->|Tab 3 Highest| Spec3["Tertiary Spec Strategy (e.g. Prot, Retribution, Subtlety)"]
    Spec1 --> Modifiers["Apply Contextual Role Modifiers (Tank / Heal / DPS)"]
    Spec2 --> Modifiers
    Spec3 --> Modifiers
    Modifiers --> ActiveEngine["Active Class Strategy Engine Attached"]
```

The bot determines its combat strategy dynamically by inspecting spent talent points:

- **Dominant Talent Tree:** The bot tallies spent talent points across the three class tabs. Whichever tree has the highest allocation activates that combat strategy.
- **Hybrid Support:** Hybrid classes adapt forms and stances based on context and assigned group roles.
- **Battlegrounds:** Healing specs retain healing priorities in battlegrounds, layering battleground objectives and PvP strategies on top.
- **Dynamic Role Assignment:** In parties, players can set bot roles to adjust aggro generation, healing urgency, and positioning.

Players can use [`.bot role <Name|self>`](../play/commands/roster-and-hiring.md#bot-role) to override runtime role inference. Without an override, role detection uses party composition, stance or form, and tanking equipment (a shaman needs a shield, *Rockbiter Weapon* and *Totemic Alignment* or *Spirit Armor*; a bot that rolled the tank build counts as a tank from its talents alone — see [Shaman tank](shaman.md#class-settings)). If another tank already occupies the role, temporary stance or form changes do not promote a second tank.

---

## Shared Bot Behaviors

| Subsystem | Trigger Condition | Bot Action / Behavior | Configuration / Control |
| :--- | :--- | :--- | :--- |
| **Resting & Recovery** | Out-of-combat health or mana below recovery thresholds. | Sits down and consumes food and drink simultaneously. | See [Combat settings](../server/settings/combat.md). |
| **Buffs & Imbues** | Buff missing on self or party member. | Recasts class buffs, seals, auras, and weapon poisons. | Automatic out-of-combat maintenance. |
| **Targeting & Assist** | Tank or party leader engages hostile enemies. | Assists leader focus target and respects crowd control marks. | See [Focus skull](../play/commands/party-actions.md#bot-action-focus-skull). |
| **Finishing Runners** | Enemy mob flees below 30% health. | Ranged DPS finish within spell range; eligible melee close distance. | See detailed runner policy below. |
| **Threat Back-Off** | AoE threat at 50% of tank's threat, single-target threat at 80%. | Pauses high-threat single-target or AoE spells. | Controlled by `threat` strategy. |
| **Healer Off-DPS** | Party stable (heal target at/above 90%), own mana above the fight reserve, and the mob already fighting the group. | Contributes offensive spells and debuffs; healing always wins. | Controlled by `offdps` strategy. |
| **DPS Off-Healing** | An ally drops into a wound band (see below). | Damage dealers cast emergency heals instead of attacking. | Controlled by `offheal` strategy. |
| **Spell Downranking** | Friendly target wounded but not in critical danger. | Casts lower-rank heals matching missing health. | Automatic triage optimization. |
| **Healer Positioning** | Party member out of heal range or line of sight. | Closes distance toward the tank or injured party member. | Automatic positioning routine. |
| **Looting & Gathering** | Corpse or nearby gathering node available. | Approaches corpse, loots quest items, and rolls on items. | See [Party actions](../play/commands/party-actions.md). |

<details>
<summary>Runner finishing, threat back-off, off-DPS, downranking, and healer positioning details</summary>

### Finishing Runners
When an enemy mob flees below 30% health (either flee state or flee-for-assistance):
- Ranged DPS finish the target within spell range.
- Warriors finish the runner if *Intercept* is available (in combat, Berserker Stance, rage available, off cooldown) within 25 yards.
- Other melee only attack runners already in melee reach.
- During `stay` holds, melee bots hold their position while ranged bots shoot within spell range.
- Healers and tanks do not chase runners.
- The active runner is never dropped for a fresh mob, and crowd-controlled targets are never chased.
- **Chases stop at ledges** (owner decision 2026-10-23, scoped after review): a bot of an active dungeon run let a running enemy go when it, or the end of its run, was more than 6 yards below the bot's floor AND the bot's navmesh way there had a cliff (a drop of more than 6 yards over less than 5 yards across) or no complete path; a staircase or a slope down was chased as before, and so was the bot's explicit attack target (an explicit order also cleared the let-go). Random bots and normal groups were not touched. A runner let go stayed let go for up to 30 seconds while it was still down there.

### Party Threat Back-Off
In groups with the `threat` strategy active, damage dealers (including Druids, Hunters, Mages, Shamans, and Warlocks) monitor relative threat against the tank. When AoE threat reaches 50% of the tank's, or single-target threat reaches 80% (high threat), they pause high-threat single-target nukes or area-of-effect spells (*Volley*, *Multi-Shot*, *Blizzard*, *Flamestrike*, *Rain of Fire*, *Chain Lightning*, *Earthquake*) until threat stabilizes.

### Healer Off-DPS (`offdps`)
Healing specs get `offdps` by default (Discipline and Holy priests, Restoration shamans and druids, Holy paladins). When it is on, the healer only attacks when every condition holds:
- Nobody needs healing: the heal target (worst-off member, tank favored) is at or above 90% health.
- The healer's own mana is above the fight reserve: 85% in hard fights, 70% in even or easy fights, 95% in raids.
- The mob already fights the group (or the player explicitly ordered the attack on it); healers never open on a fresh mob.
- No ordered pull is walking in, and the bot is not the run leader's heal-only healer.

Casting yields to healing instantly: damage rows sit at the lowest priority, so any firing heal outbids them. Solo or far from the group, the healer fights like a normal bot.

What each class casts: priests use *Shadow Word: Pain*, *Holy Fire*, *Smite*, *Starshards*, *Mind Blast* (*Holy Nova* into packed melee, wand when mana is spent); druids shift out of Tree of Life for *Moonfire*, *Wrath*, *Starfire*; shamans use *Flame Shock* and *Lightning Bolt* (*Chain Lightning* into packs); paladins use *Hammer of Wrath* on dying targets, *Holy Shock*, *Judgement* and *Exorcism* on Undead or Demon targets (*Consecration* into packed melee). See the class pages for details.

### DPS Off-Healing (`offheal`)
Damage specs of hybrid classes get `offheal` by default (Shadow priests, Elemental and Enhancement shamans, Retribution paladins, Cat and Balance druids). When it is on, the damage dealer stops attacking and heals once an ally falls into a wound band:

| Class | Ally below 20% (critical) | Ally at 20–50% (low) | Ally at 50–70% (medium) |
| :--- | :--- | :--- | :--- |
| Priest (Shadow) | *Power Word: Shield*, then *Flash Heal* | *Greater Heal* | *Renew* |
| Shaman (Ele/Enh) | - | *Healing Wave* | - |
| Paladin (Ret) | *Lay on Hands*; *Blessing of Protection* on an attacked ally | *Holy Light* | - |
| Druid (Cat/Balance) | *Regrowth*, then *Healing Touch* | *Regrowth* | - |

Off-heals outbid damage, so they fire the moment an ally enters a band; outside the bands the bot keeps its normal rotation. Out of combat only the low band (20–50%) heals. There is no party/raid or PvP difference: the same rows run everywhere. Off-heals use the same mana care as real heals (downranking, no wasteful casts at high health), and are never held back at or below 50% ally health.

### Switching off-spec behavior on and off
Per bot, whisper the bot (`/w <BotName> co +offheal`, `/w <BotName> co -offdps`; `co ?` lists active combat strategies), or use [`.bot strategy`](../play/commands/party-actions.md#bot-strategy) for one bot, a role, or the party (`.bot strategy -offdps @heal`). Changes are saved per bot. Server admins can change the default for newly created bots with [`AiPlayerbot.EnableOffSpecStrategies`](../server/settings/class-behaviour.md#aiplayerbotenableoffspecstrategies) (`0` leaves the off-spec kit off; players can still toggle it per bot).

### Spell Downranking
Healing bots automatically select the lowest spell rank that covers the target's missing health to conserve mana. Full spell ranks are enforced whenever a target falls into critical health (at or below 20% health).

### Healer Positioning
When a party member is out of heal range or line of sight:
- The healer moves toward that member. The default follow anchor is the tank when no member is injured.
- If another living healer already has that member in range and sight, the healer does not path to them unless the member is at or below critical health.
- Silenced, pacified, crowd-controlled, or out-of-mana healers do not satisfy the coverage check.
- Out of combat, healers only path to members below 90% health.

</details>
