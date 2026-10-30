---
id: classes-paladin
title: Paladin Bot Guide
category: classes
summary: Complete guide to Paladin bot AI, Holy healing, Protection tanking, Retribution DPS, blessing coordination, and Judgement upkeep.
tags: [classes, paladin, tank, healer, dps, hybrid]
relates_to:
  - classes-overview
---

# Paladin Bot Guide

Paladins provide resilient group utility, customizable auras, coordinated blessings, and capable Tank, Healer, or Melee DPS performance.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Holy** | Healer | Efficient single-target healing, emergency saves, and raid upkeep. |
| **Protection** | Tank | High AoE threat, Consecration holding, and Holy Shield mitigation. |
| **Retribution** | Melee DPS | Two-handed melee holy burst, Seal cycling, and Judgement maintenance. |

---

## What the bot does in combat

### Holy (Healer)
- Casts *Lay on Hands* on party members at critical health (default 20), claiming the target to prevent duplicate casts.
- Uses *Flash of Light* for efficient maintenance and *Holy Light* for heavy damage spikes.
- Downranks direct heals (*Holy Light*, *Flash of Light*) to match wounds per [shared downranking rules](overview.md#spell-downranking).
- Casts *Holy Shock* on allies below 50% health.
- Weaves *Crusader Strike* only when already in melee reach, without moving out of healing position.
- Deals off-spec damage (*Hammer of Wrath* on dying targets, *Holy Shock*, *Judgement*, *Exorcism* on Undead or Demon targets, *Consecration* into packs) per [healer off-DPS rules](overview.md#healer-off-dps-offdps).
- Casts *Divine Shield* (Bubble) when personally critically wounded, continuing to heal while immune.

### Protection (Tank)
- Maintains *Righteous Fury* in and out of combat to boost holy threat generation.
- Taunts loose mobs with *Hand of Reckoning* (Spell ID 51303), falling back to *Righteous Defense*.
- Drops *Consecration* against 3 or more attackers in melee reach while above medium mana (40% default), or against 2 at 70%+ mana.
- Keeps *Holy Shield* active on cooldown for block rating and reflective holy damage.
- Judges *Seal of Righteousness* on the primary target for sustained threat.

### Retribution (Melee DPS)
- Maintains *Seal of Command* (falling back to *Seal of Righteousness*) and unleashes *Judgement* on cooldown.
- Uses *Crusader Strike* as primary builder ahead of *Holy Strike*.
- Fires *Exorcism* instantly upon *Art of War* procs.
- Casts *Hammer of Wrath* as an execute when targets drop below 20% health.
- Off-heals wounded allies (*Lay on Hands*, *Holy Light*, *Blessing of Protection* on attacked allies) per [off-healing rules](overview.md#dps-off-healing-offheal).

<details>
<summary>Judgement upkeep in groups</summary>

When enabled, paladin bots coordinate to maintain *Judgement of Wisdom* and *Judgement of Light* on bosses or elite targets:
- **Assignment:** Retribution paladins maintain *Judgement of Wisdom*; Holy paladins maintain *Judgement of Light*. A second non-tank paladin takes the unassigned debuff.
- **Protection Exemption:** Protection tanks never upkeep group judgements, keeping *Seal of Righteousness* for threat.
- **Timing:** Debuffs are refreshed when 4 seconds or fewer remain.
- **Mana Gates:** Everyone needs at least medium mana; a healing paladin needs 50% mana and judges only while nobody is below the medium-health line (see [Combat settings](../server/settings/combat.md#aiplayerbotmediummana)).
- **Player Paladins:** Active judgements placed by human players are respected and never overwritten.

</details>

---

## Turtle WoW custom spells

- **Holy Strike (Spell ID 679):** Instant holy melee strike providing Retribution and Protection paladins an active physical/holy damage builder.
- **Bulwark of the Righteous (Spell ID 51346):** Protection talent granting an active shield-slam mitigation strike with damage reduction, auto-cast whenever ready.
- **Exorcism Targeting & Art of War:** Checks target creature types and *Art of War* procs, allowing *Exorcism* to damage any enemy when procced.

---

## Utility & interrupts

- **Blessings Coordination:** Coordinates blessings across party classes (*Kings*, *Might*, *Wisdom*, *Salvation*, *Sanctuary*, *Light*). Each paladin assigns one blessing per member, and multiple paladins stack distinct blessing families. Buff claims prevent duplicate casts.
- **Hands:** Casts *Hand of Protection* on non-tank allies facing physical danger and *Hand of Freedom* on rooted allies. Checks for *Forbearance* to prevent wasted casts.
- **Auras:** Holy runs *Concentration Aura*, Protection runs *Retribution Aura*, and Retribution runs *Sanctity Aura*. In raids, the first paladin bot in the fight auto-swaps to *Fire* or *Shadow Resistance Aura* on applicable bosses, then drops it out of combat.
- **Cleansing:** Uses *Cleanse* and *Purify* to remove poisons, diseases, and magic debuffs from self and allies.
- **Crowd Control & Stuns:** Casts *Hammer of Justice* to stun dangerous casters or runners. *Repentance* interrupts with no humanoid restriction and *Turn Undead* covers assigned-CC undead.
- **Solo Survival:** Solo paladins with mana at or above the low-mana line stand their ground and heal through combat rather than fleeing when outnumbered.

---

## Talent builds

TortoiseBots provides pre-configured talent progressions at 5-level checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Holy** | `2.0` | Healer | `40 / 11 / 0` | Holy Shock, Divine Favor, Daybreak, Blessed Strikes, Illumination, 11 Prot dip. |
| **Protection** | `2.1` | Tank | `5 / 38 / 8` | Holy Shield, Bulwark of the Righteous, Righteous Strikes, Reckoning, Sanctuary. |
| **Retribution** | `2.2` | Melee DPS | `11 / 0 / 40` | Seal of Command, Repentance, Vengeful Strikes, Sanctity Aura, Benediction. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Holy (`2.0`):**
  - *Levels 10–35:* Takes *Divine Intellect* (5/5), *Healing Light* (3/3), *Illumination* (5/5), and *Holy Shock* (35).
  - *Levels 35–45:* Takes *Divine Favor* (40) and *Daybreak* (45).
  - *Levels 45–60:* Takes *Blessed Strikes* (5/5) and *Holy Power* (3/3), then dips 11 points into Protection (*Improved Devotion Aura* + *Guardian's Favor* + *Toughness*).
- **Protection (`2.1`):**
  - *Levels 10–40:* Takes *Redoubt* (5/5), *Precision* (3/3), *Improved Righteous Fury* (3/3), *Blessing of Sanctuary* (20), *Shield Specialization* (3/3), and *Holy Shield* (40).
  - *Levels 40–50:* Takes *Reckoning* (5/5), *Righteous Strikes* (5/5), and *Bulwark of the Righteous* (50).
  - *Levels 50–60:* Dips into Retribution (*Benediction* 5/5 + *Improved Judgement* 2/2) and Holy (*Divine Strength* 5/5).
- **Retribution (`2.2`):**
  - *Levels 10–35:* Takes *Benediction* (5/5), *Improved Judgement* (2/2), *Conviction* (5/5), *Blessing of Kings* (20), *Two-Handed Weapon Specialization* (3/3), and *Seal of Command* (35).
  - *Levels 35–45:* Takes *Vengeance* (5/5), *Vengeful Strikes* (5/5), and *Repentance* (45).
  - *Levels 45–60:* Dips 11 points into Holy for *Divine Strength* (5/5), *Divine Intellect* (5/5), and *Sanctity Aura* (60).

</details>

---

## Class settings

- [`AiPlayerbot.PaladinJudgementUpkeep`](../server/settings/class-behaviour.md#aiplayerbotpaladinjudgementupkeep): Controls whether paladins coordinate to maintain Judgement of Wisdom and Judgement of Light on bosses and elite targets.
