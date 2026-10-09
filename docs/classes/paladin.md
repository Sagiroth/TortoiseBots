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
- **Emergency:** Casts *Lay on Hands* on any party member at critical health (default 20). Casts *Divine Favor* as a standalone buff trigger on medium/low mana; no code chains it into *Holy Light*.
- **Maintenance:** Maintains *Flash of Light* on injured allies. Casts *Holy Shock* on low-health bands only (no movement condition).
- **Mana sense:** Never skips a target at or below the low-health line for mana reasons; above the medium line it refuses oversized or average-efficiency heals, and below the medium-mana line it refuses mana-hungry heals in favor of *Flash of Light* (efficient) over *Holy Light* (heavy). Tanks count the expected heal at two-thirds.
- **Self-Defense:** Pops *Divine Shield* (Bubble) if personal health drops into danger, continuing to heal the group while immune.

### 2. Protection (Tank)
- **Aggro Mechanics:** Keeps *Righteous Fury* up in and out of combat (re-cast on loss mid-pull, e.g. after death or bubble). On lose-aggro the tank taunts with *Hand of Reckoning* (Turtle trainer spell 51303, level 10), falling back to *Righteous Defense* (ranks 51328-51330); *Judgement* is damage rotation, not the taunt path.
- **AoE Holding:** Drops *Consecration* on 2+ attackers in reach (3+, or 2 at 70%+ mana) with the AoE kit active. Keeps *Holy Shield* active on cooldown for block rating and reflective holy damage.
- **Burst Threat:** Judges *Seal of Righteousness* on primary target.

### 3. Retribution (DPS)
- **Seals & Judgement:** Maintains *Seal of Command* (falling back to *Seal of Righteousness* on spell availability, with no weapon-speed check) and unleashes *Judgement* on cooldown. The rotation also wires *Exorcism* (instant with the Art of War proc), *Holy Strike*, and *Crusader Strike*.
- **Builder order:** *Crusader Strike* is the main builder and outranks *Holy Strike* within the normal band; seal/judge upkeep still outbids both.
- **Finishers:** Casts *Hammer of Wrath* when target falls below 20% health.

---

## Turtle WoW 1.18.1 Custom Content

- **Holy Strike (Spell ID 679, per game data):**
  - Custom Turtle WoW instant holy melee strike (0.71 weapon coefficient + Mending Light bonus, per game data).
  - Gives Retribution and Protection Paladins an active on-demand melee filler and sustained holy damage.
- **Bulwark of the Righteous (Spell ID 51346, per game data):**
  - Protection talent granting an active shield-slam ability with damage reduction.
  - Auto-cast whenever ready at high priority (not held as an emergency cooldown); the cooldown length is game data, not bot logic.
- **Exorcism Targeting:**
  - In Vanilla, *Exorcism* can only hit Undead and Demons. In Turtle WoW with the *Art of War* talent, it becomes usable on all creature types on proc. The bot's trigger explicitly verifies target type or proc status before attempting cast, eliminating wasted mana.

---

## Utility, Blessings & Auras

- **Blessings:** Coordinates blessings across party classes (*Blessing of Kings*, *Might*, *Wisdom*, *Salvation*, *Sanctuary*, *Light*). Automatically avoids overriding higher-tier blessings. With two paladins in one party, a short shared *buff claim* keeps them from double-casting: while one paladin's blessing cast is in flight the other stands down on that member instead of overwriting it.
- **Blessing scheduling:** Out of combat, catching up to the master runs below the blessings (a pending blessing in range wins the tick, follow resumes next). Upkeep blessings wait for 40% mana, 20% with a real player master.
- **Auras:** Each spec keeps a fixed default aura (Holy → Concentration, Protection → Retribution, Retribution → Sanctity); otherwise the bot casts the first missing aura in a fixed list. No damage-type or group-composition detection exists.
- **Cleansing:** Uses *Cleanse* and *Purify* to remove poisons, diseases, and magic debuffs from party members (self first, then party).
- **Crowd Control:** Stuns dangerous casters or runners using *Hammer of Justice*. *Repentance* fires automatically from interrupt, enemy-healer and snare triggers with no humanoid restriction; the Paladin CC-strategy spell is *Turn Undead*, and `.bot action cc` assigns marks rather than ordering a spell.

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots configures verified 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Holy** | `2.0` | Healer | `40 / 11 / 0` | Holy Shock (35), Divine Favor (40), Daybreak (45 shielding heal), Blessed Strikes, Illumination, 11 Prot dip. |
| **Protection** | `2.1` | Tank | `5 / 38 / 8` | Holy Shield (40), Bulwark of the Righteous (50), Righteous Strikes, Reckoning, Blessing of Sanctuary, Ret/Holy dips. |
| **Retribution** | `2.2` | Melee DPS | `11 / 0 / 40` | Seal of Command (35), Repentance (45), Vengeful Strikes, Sanctity Aura (+10% Holy damage aura, per game data), Benediction (-15% mana, per game data). |

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

