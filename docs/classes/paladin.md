---
id: class-paladin
title: Paladin Bot AI & Specs
category: classes
summary: Guide to Paladin bot behavior across Holy healing, Protection tanking, and Retribution DPS, including custom Holy Strike and Bulwark.
tags: [class, paladin, tank, healer, dps]
relates_to:
  - class-overview
  - guide-player-controls
---

# Paladin Bot AI & Specs

Paladins provide exceptional party utility, versatile auras, class blessings, and fill Tank, Healer, or Melee DPS roles.

## Supported Specs & Roles

- **Holy (Healer):** Single-target healing powerhouse. Relies on *Flash of Light* for efficient maintenance, *Holy Light* for heavy damage spikes, and *Holy Shock* for instant reaction heals.
- **Protection (Tank):** High AoE threat tanking using *Consecration*, *Holy Shield*, and *Righteous Fury*.
- **Retribution (Melee DPS):** Two-handed melee damage leveraging *Seal of Command* and Judgement bursts.

---

## Combat Rotations & Priorities

### 1. Holy (Healing)
- **Emergency:** Casts *Lay on Hands* when tank health < 15%. Casts *Divine Favor* followed by a guaranteed-crit *Holy Light* on critical targets.
- **Maintenance:** Maintains *Flash of Light* on injured allies. Weaves *Holy Shock* on moving or emergency targets.
- **Self-Defense:** Pops *Divine Shield* (Bubble) if personal health drops into danger, continuing to heal the group while immune.

### 2. Protection (Tank)
- **Aggro Mechanics:** Always keeps *Righteous Fury* active.
- **AoE Holding:** Drops *Consecration* on mob clusters to maintain lock on multiple targets. Keeps *Holy Shield* active on cooldown for block rating and reflective holy damage.
- **Burst Threat:** Judges *Seal of Righteousness* on primary target.

### 3. Retribution (DPS)
- **Seals & Judgement:** Maintains *Seal of Command* (or *Seal of Righteousness* on fast weapons) and unleashes *Judgement* on cooldown.
- **Finishers:** Casts *Hammer of Wrath* when target falls below 20% health.

---

## Turtle WoW 1.18.1 Custom Content

- **Holy Strike (Spell ID 679):**
  - Custom Turtle WoW instant holy melee strike (0.71 weapon coefficient + Mending Light bonus).
  - Gives Retribution and Protection Paladins an active on-demand melee filler and sustained holy damage.
- **Bulwark of the Righteous (Spell ID 51346):**
  - Protection talent granting an active shield-slam ability with damage reduction on a 5-minute cooldown.
  - Used as an emergency tank mitigation cooldown against boss enrages or large packs.
- **Exorcism Targeting:**
  - In Vanilla, *Exorcism* can only hit Undead and Demons. In Turtle WoW with the *Art of War* talent, it becomes usable on all creature types on proc. The bot's trigger explicitly verifies target type or proc status before attempting cast, eliminating wasted mana.

---

## Utility, Blessings & Auras

- **Blessings:** Coordinates blessings across party classes (*Blessing of Kings*, *Might*, *Wisdom*, *Salvation*, *Sanctuary*, *Light*). Automatically avoids overriding higher-tier blessings.
- **Auras:** Automatically selects the appropriate aura (e.g. *Devotion Aura* for physical damage, *Concentration Aura* for caster groups, or elemental resistance auras).
- **Cleansing:** Uses *Cleanse* and *Purify* to remove poisons, diseases, and magic debuffs from party members.
- **Crowd Control:** Stuns dangerous casters or runners using *Hammer of Justice*. Uses *Repentance* (Retribution) for humanoid CC when ordered via `.bot action cc`.

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots configures verified 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Holy** | `2.0` | Healer | `40 / 11 / 0` | Holy Shock (35), Divine Favor (40), Daybreak (45 shielding heal), Blessed Strikes, Illumination, 11 Prot dip. |
| **Protection** | `2.1` | Tank | `5 / 38 / 8` | Holy Shield (40), Bulwark of the Righteous (50), Righteous Strikes, Reckoning, Blessing of Sanctuary, Ret/Holy dips. |
| **Retribution** | `2.2` | Melee DPS | `11 / 0 / 40` | Seal of Command (35), Repentance (45), Vengeful Strikes, Sanctity Aura (+10% Holy damage aura), Benediction (-15% mana). |

### Leveling Milestones & Progression Rationale

- **Holy (`2.0`):**
  - *Levels 10–35:* Sustained healing path: *Divine Intellect* (5/5), *Healing Light* (3/3), *Illumination* (5/5 mana return on crit), and *Holy Shock* (35 signature instant heal).
  - *Levels 35–45:* *Divine Favor* (40 guaranteed crit, depends on Holy Shock) and *Daybreak* (45 shielding heal on critical heal).
  - *Levels 45–60:* *Blessed Strikes* (5/5 Holy Shock cooldown reset on Crusader Strike) and *Holy Power* (3/3), finished with 11 Protection points (*Improved Devotion Aura* + *Guardian's Favor* + *Toughness*).
- **Protection (`2.1`):**
  - *Levels 10–40:* Mitigation rush through *Redoubt* (5/5), *Precision* (3/3), *Improved Righteous Fury* (3/3 threat), *Blessing of Sanctuary* (20), *Shield Specialization* (3/3), and *Holy Shield* (40).
  - *Levels 40–50:* *Reckoning* (5/5), *Righteous Strikes* (5/5 Holy Strike threat/damage), and *Bulwark of the Righteous* (50 capstone mitigation slam).
  - *Levels 50–60:* Retribution dip (*Benediction* 5/5 + *Improved Judgement* 2/2) for mana efficiency and Holy dip (*Divine Strength* 5/5) for attack power.
- **Retribution (`2.2`):**
  - *Levels 10–35:* Mana-efficient damage rush: *Benediction* (5/5 reducing Seal/Judgement cost by 15%), *Improved Judgement* (2/2), *Conviction* (5/5 crit), *Blessing of Kings* (20), *Two-Handed Weapon Specialization* (3/3), and *Seal of Command* (35).
  - *Levels 35–45:* *Vengeance* (5/5 +15% physical/holy damage on crit), *Vengeful Strikes* (5/5 Zeal attack speed / Holy Strike strength buff), and *Repentance* (45 CC).
  - *Levels 45–60:* Completes Retribution utility (*Improved Seal of the Crusader* + *Eye for an Eye*), then takes 11 Holy points into *Divine Strength* (5/5), *Divine Intellect* (5/5), and *Sanctity Aura* (60), granting +10% Holy damage to the entire party to amplify Judgements, Seal procs, and Holy Strikes.

