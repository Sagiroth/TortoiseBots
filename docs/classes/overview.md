---
id: class-overview
title: Class AI & Roles Overview
category: classes
summary: Summary of the nine Vanilla classes, spec detection, combat roles, and shared behaviors in TortoiseBots.
tags: [classes, overview, roles, specs, combat]
relates_to:
  - class-warrior
  - class-paladin
  - class-hunter
  - class-rogue
  - class-priest
  - class-shaman
  - class-mage
  - class-warlock
  - class-druid
  - guide-player-controls
---

# Class AI & Roles Overview

TortoiseBots implements comprehensive AI for all **nine Vanilla classes** (Warrior through Druid), with full spec awareness, rotational priorities, and support for **Turtle WoW 1.18.1** custom spells and talents.

## Supported Roles & Classes

| Class | Tank | Healer | Melee DPS | Ranged DPS | Crowd Control (CC) | Interrupts |
| :--- | :---: | :---: | :---: | :---: | :--- | :--- |
| **[Warrior](warrior.md)** | ✅ (Prot) | ❌ | ✅ (Arms/Fury) | ❌ | Intimidating Shout, Concussion Blow | Shield Bash, Pummel |
| **[Paladin](paladin.md)** | ✅ (Prot) | ✅ (Holy) | ✅ (Ret) | ❌ | Hammer of Justice, Repentance | Hammer of Justice (stun/interrupt), Repentance |
| **[Hunter](hunter.md)** | ❌ | ❌ | ✅ (Survival, ranged-first hybrid) | ✅ (BM/MM) | Freezing Trap, Scare Beast | None (Scatter Shot is a snare) |
| **[Rogue](rogue.md)** | ❌ | ❌ | ✅ (All Specs) | ❌ | Sap, Blind, Gouge | Kick |
| **[Priest](priest.md)** | ❌ | ✅ (Holy/Disc) | ❌ | ✅ (Shadow) | Shackle Undead, Psychic Scream, Chastise | Silence (Shadow) |
| **[Shaman](shaman.md)** | ❌ | ✅ (Resto) | ✅ (Enh) | ✅ (Ele) | None | Earth Shock |
| **[Mage](mage.md)** | ❌ | ❌ | ❌ | ✅ (All Specs) | Polymorph | Counterspell |
| **[Warlock](warlock.md)** | ❌ | ❌ | ❌ | ✅ (All Specs) | Fear, Banish | Spell Lock (Felhunter) |
| **[Druid](druid.md)** | ✅ (Bear) | ✅ (Resto) | ✅ (Cat) | ✅ (Balance) | Entangling Roots, Hibernate | Bash |

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

The bot's combat strategy is determined dynamically by analyzing its spent talent points (`AiFactory::GetPlayerSpecTab`):
- **Dominant Talent Tree:** The bot tallies spent talent points across the three class tabs. Whichever tree has the highest allocation determines the active combat strategy (e.g. Arms vs Fury vs Protection for Warriors).
- **Hybrid Support:** Hybrid classes adapt based on context. For example, a Feral Druid switches to Bear form when tanking or Cat form when DPSing.
- **Dynamic Role Assignment:** In party groups, the player can designate bot roles (Tank, Healer, DPS) which adjusts multiplier weights for aggro generation, healing urgency, and positioning.

Players can set `.bot role self <tank|healer|dps|clear>` to override runtime role inference. Without an override, role detection uses party context and current stance/form or tank equipment signals; if another tank already occupies the role, temporary stance/form changes do not promote a second tank.

---

## Shared Bot Behaviors

| Shared Subsystem | Trigger Condition | Bot Action / Behavior | Configuration / Control |
| :--- | :--- | :--- | :--- |
| **Resting & Recovery** | Out-of-combat, HP below `AiPlayerbot.AlmostFullHealth` (default 90%) for bots with the item cheat (free rations, seeded and refilled to a full level-tier stack) or below `AiPlayerbot.LowHealth` (default 50%) otherwise; drink stops at almost-full for cheat bots (85 otherwise), mana users only | Sits down, consumes food/water simultaneously | Automatic; pauses when group moves |
| **Buffs & Imbues** | Buff missing on self/party | Recasts class buffs (Arcane Intellect, PW:F, MotW) and poisons | Automated out-of-combat maintenance |
| **Targeting & Assist** | Leader/Tank enters combat | Assists master's target; honors Skull focus and CC exclusions | `.bot action focus skull`, CC mark priority |
| **Looting & Gathering** | Dead corpse or nearby resource node | Navigates to corpse, loots quest items, rolls Greed/Need | loot/roll actions; `RollBadItemsWithPlayer` only forces Need on empty-slot upgrades |

> Spell names and bot wiring described here are verified against the bot code; spell IDs and percentage effects (damage, crit, hit, threat) come from Turtle WoW game data, not from the bot code.
