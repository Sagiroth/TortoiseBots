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
