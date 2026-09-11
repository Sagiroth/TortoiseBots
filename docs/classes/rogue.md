---
id: class-rogue
title: Rogue Bot AI & Specs
category: classes
summary: Deep dive into Rogue stealth openers, combo point spenders, poison application, interrupts, and custom Turtle abilities.
tags: [class, rogue, dps, melee, stealth]
relates_to:
  - class-overview
  - guide-player-controls
---

# Rogue Bot AI & Specs

Rogues provide premier single-target melee physical DPS, invaluable pre-combat crowd control (*Sap*), rapid interrupts (*Kick*), and defensive evasion.

## Supported Specs & Roles

- **Combat (Melee DPS):** Sustained sword/mace DPS using *Sinister Strike*, *Slice and Dice*, and *Blade Flurry* for multi-target cleave.
- **Assassination (Melee DPS):** Dagger and poison specialist, maximizing critical strikes with *Backstab*, *Cold Blood*, and *Eviscerate*.
- **Subtlety (Melee DPS & Control):** Mobility and utility, leveraging *Hemorrhage*, *Premeditation*, *Preparation*, and high bleed uptime.

---

## Combat Rotations & Priorities

### 1. Stealth & Openers
- Automatically enters *Stealth* out of combat.
- Moves into position behind target to execute appropriate openers:
  - *Cheap Shot* for lockdown stuns on dangerous casters.
  - *Ambush* (Daggers) or *Garrote* (Bleed) for initial damage.
  - Out of combat *Sap* when assigned CC on humanoid targets.

### 2. Combo Points & Finisher Priority
- **Generator:** Uses *Sinister Strike* (Swords) or *Backstab* (Daggers), or *Hemorrhage* (Subtlety).
- **Slice and Dice Priority:** Always prioritizes maintaining *Slice and Dice* buff for attack speed.
- **Finishers:**
  - 4–5 Combo Points: Casts *Eviscerate* for burst damage.
  - Applies *Rupture* on high-health boss encounters.
  - Uses *Kidney Shot* when a stun is required to stop enemy channels.

### 3. Burst Cooldowns
- Casts *Adrenaline Rush* and *Blade Flurry* during tough encounters or multi-mob pulls.
- Activates *Evasion* immediately if taking unexpected melee aggro.
- Activates *Vanish* if health falls below 20% to wipe threat.

---

## Turtle WoW 1.18.1 Custom Content

- **Surprise Attack (Spell ID 52511):**
  - Combat talent providing an unblockable, undodgeable finisher that boosts offensive flow.
- **Noxious Assault (Spell ID 52714):**
  - Assassination talent providing +30% Attack Power and instant poison delivery.
- **Envenom (Spell ID 52531):**
  - Finisher consuming Deadly Poison stacks for instant Nature damage and an attack-speed poison buff.
- **Shadow of Death (Spell ID 52710) & Mark for Death (Spell ID 52538):**
  - Subtlety talents for banked burst detonation and party-wide attack power enhancement on fresh targets.

---

## Utility & Poisons

- **Poisons:** Automatically applies *Instant Poison* / *Deadly Poison* to main-hand and off-hand weapons out of combat.
- **Interrupts:** Casts *Kick* instantly to lock out enemy spell schools.
- **Disarm & Blinds:** Uses *Gouge* to incapacitate secondary attackers and *Blind* on out-of-control adds.
