---
id: concept-bot-mechanics-and-quirks
title: Deep Bot Mechanics, Quirks & Capabilities
category: concepts
summary: Comprehensive technical deep-dive into target selection algorithms, threat distribution, movement, interrupts, resting, and known quirks.
tags: [mechanics, quirks, targeting, threat, interrupts, movement, ai]
relates_to:
  - concept-strategy-engine
  - concept-known-limitations
  - guide-player-controls
---

# Deep Bot Mechanics, Quirks & Capabilities

This document provides a thorough technical breakdown of how TortoiseBots actually functions under the hood: how targeting decisions are calculated, how movement and threat interact with the core engine, subtle gimmicks/quirks, and current implementation gaps.

---

## 1. Targeting Algorithms & Threat Distribution

Targeting in TortoiseBots is governed by specialized value calculators rather than a flat "nearest mob" check:

| Role / Intent | Target Calculator | Selection Logic |
| :--- | :--- | :--- |
| **Tank** | `TankTargetValue` | **Lowest Personal Threat:** The tank inspects `threatManager->getThreat(bot)` across all engaged attackers and targets the mob where its threat is *lowest*. This produces automatic tab-target sunder/taunt behavior across multi-mob packs. Strictly ignores CC targets. |
| **DPS (Single Target)** | `DpsTargetValue` | **Lowest Health First:** Priority 1 = Explicit `.bot action attack` target; Priority 2 = Raid Target Icon (**Skull**); Priority 3 = Non-CC attacker with the **lowest current health** to burn mobs down one by one. |
| **DPS (AoE)** | `DpsAoeTargetValue` | **Highest Health First:** Targets the enemy with the **highest health** so damage-over-time (DoT) effects and cleaves tick for the longest possible duration. |
| **Crowd Control** | `CcTargetValue` | **Smart Exclusions:** Evaluates mobs matching the assigned raid mark. Automatically excludes: (1) Current tank/DPS target, (2) Mobs with < 50% HP (won't waste CC on dying mobs), and (3) Mobs inside active AoE spell radiuses (e.g. *Blizzard*, *Consecration*). |
| **Enemy Healer** | `EnemyHealerTargetValue` | Detects humanoid/creature enemies casting healing spells and surfaces them as high-priority interrupt/focus targets. |

---

## 2. Tactical Interrupt & CC Resolution Gimmicks

When you issue `.bot action interrupt` or `.bot action cc <mark>`, the server executes a multi-step resolution pipeline in `commands/BotCommandContext.cpp`:

```text
Player Issues .bot action interrupt
    │
    ▼
1. Target Validation: Is target alive, hostile, and actively casting a non-melee spell?
    │
    ▼
2. Executor Selection: Iterates party bots to find who has a ready interrupt:
   - Rogue: Kick
   - Warrior: Pummel (Berserker) / Shield Bash (Battle/Defensive + Shield)
   - Shaman: Earth Shock (Rank 1 for mana conservation)
   - Mage: Counterspell
   - Priest: Silence (Shadow)
   - Warlock: Spell Lock (Felhunter pet)
   - Paladin: Hammer of Justice (Stun interrupt) / Repentance
   - Druid: Bash (Bear) / Feral Charge
    │
    ▼
3. Execution / Reach Prerequisite:
   - If in range: Casts immediately.
   - If out of range: Enqueues reach action on the active engine so the bot closes distance.
```

### Pet Discipline Around Crowd Control
A classic PlayerBots bug was pets breaking crowd control immediately after application. In TortoiseBots, pets belonging to Hunter and Warlock bots inspect `IsCcTarget()`. When a mob is affected by *Polymorph*, *Freezing Trap*, *Sap*, or *Seduce*, pets are strictly blocked from attacking that GUID.

---

## 3. Movement, Formations & Physics Interactions

Bot movement bridges native C++ AI directly to the core server's `MotionMaster`:

| Movement Subsystem | Implementation Details |
| :--- | :--- |
| **Formations** | Supports 6 geometric formations: `arrow`, `queue`, `near`, `line`, `circle`, `shield`. The bot calculates local offsets relative to the master's orientation and updates target coordinates. |
| **Catch-Up Sprint (`BoostFollow`)** | When enabled in configuration (`AiPlayerbot.BoostFollow = 1`), bots falling further than 15 yards behind the leader temporarily gain a movement speed multiplier to close the gap quickly. |
| **Hunter Dead-Zone Weaving** | When an enemy approaches within 8 yards (the ranged dead zone), Hunter bots automatically cast *Wing Clip* or *Disengage*, step into melee with *Mongoose Bite* / *Carve*, and step back to ranged distance as soon as the target is snared. |
| **Kiting & Fleeing** | Casters and healers evaluate melee proximity. If an enemy closes in without tank threat, the bot invokes `FleeAction`, dropping snares (*Frost Nova*, *Earthbind Totem*, *Psychic Scream*) and retreating toward the tank. |
| **Elevators & Transports** | Moving transports (boats, zeppelins, elevators) use `TransportTeleportType` (default `2`). Rather than desyncing on complex moving geometry, bots safely teleport from dock to dock or follow the master's transport coordinates. |

---

## 4. Sustenance, Resting & Gear Evaluation

| Mechanic | How It Works |
| :--- | :--- |
| **Simultaneous Eat & Drink** | Out of combat, bots scan bags for Food (Item Category 11) and Drink (Item Category 59). If both health and mana are depleted, the bot consumes both simultaneously in a single rest phase. |
| **Conjured Item Sharing** | Mages out of combat automatically conjure food and water stacks and trade them to mana-using party members who have low supplies. |
| **Gear Upgrades & Scoring** | When `RandomGearUpgradeEnabled = 1`, the bot evaluates equipment by calculating spec-relevant stat weights (Strength/Agility for physical, Spell Power/Intellect for casters). Items with higher effective scores are equipped automatically. |

---

## 5. Known Quirks, Gaps & Edge Cases

An honest accounting of where the engine currently stands and where future work is needed:

| Subsystem / Quirk | Technical Reality & Current Behavior | Recommended Player / Operator Action |
| :--- | :--- | :--- |
| **Line-of-Sight & Doorways** | On complex multi-level geometry (e.g. Blackrock Depths stairs), pathfinding can occasionally hitch if line-of-sight is lost. | Use `.bot action come` or `.bot summon` to snap bots to your location. |
| **Warlock Life Tap Suicide Risk** | While safety guards suppress *Life Tap* below 50% health, high incoming ambient raid damage can occasionally catch a tapping bot before a heal lands. | Keep healer bots set to higher reaction urgency (`AiPlayerbot.LowHealth = 65`). |
| **Shaman Totem Churn** | When pulling through large dungeons, Shamans may leave totems behind, re-dropping them as new combat anchors are established. | Normal behavior; Shaman mana regenerates quickly during out-of-combat drinking. |
| **Loot Distance Waste** | The core currently lacks an early `CanLoot` query, meaning bots may approach a corpse before discovering it has no valid personal loot slot. | Purely cosmetic movement; does not impact combat or party progression. |
| **Self-Botting Limitation** | You cannot turn your currently logged-in human character into a bot; bots must be secondary headless characters from your account. | Spawn secondary characters from your account using `/tbm` or `.bot add`. |
