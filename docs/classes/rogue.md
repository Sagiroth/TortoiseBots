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

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots ships with verified 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Combat** | `4.0` | Melee DPS | `20 / 31 / 0` | Dual Wield Spec (30), Blade Rush (35), Adrenaline Rush (40), Hack and Slash extra attacks, Relentless Strikes, Lethality. |
| **Assassination** | `4.1` | Melee DPS | `40 / 11 / 0` | Cold Blood (35), Seal Fate (45), Noxious Assault (50), Envenom, Vigor, Efficient Poisons, Precision dip. |
| **Subtlety** | `4.2` | Melee DPS | `10 / 0 / 41` | Hemorrhage (30), Preparation (35), Mark for Death (45), Honor Among Thieves, Tricks of the Trade, Assassination crit dip. |

### Leveling Milestones & Progression Rationale

- **Combat (`4.0`):**
  - *Levels 10–30:* Melee efficiency path: *Opportunity* (5/5), *Precision* (5/5 +5% hit), *Improved Backstab* (3/3), *Improved Sprint* (2/2), and *Dual Wield Specialization* (30 +50% offhand damage).
  - *Levels 30–40:* *Surprise Attack* (1/1 unblockable strike), *Hack and Slash* (2/2 sword/axe extra attacks), *Blade Rush* (35 attack speed & energy recovery), and *Adrenaline Rush* (40 signature energy surge).
  - *Levels 40–60:* Transitions into Assassination for *Malice* (5/5 crit), *Ruthlessness* (3/3 combo points on finisher), *Relentless Strikes* (25 energy on finisher), and *Lethality* (5/5 +30% crit damage bonus).
- **Assassination (`4.1`):**
  - *Levels 10–35:* Poison and crit engine: *Malice* (5/5), *Ruthlessness* (3/3), *Murder* (2/2), *Relentless Strikes* (1/1), *Lethality* (5/5), *Vile Poisons* (3/3), *Improved Poisons* (3/3), and *Cold Blood* (35 guaranteed crit).
  - *Levels 35–50:* *Efficient Poisons* (3/3), *Envenom* (40 finisher), *Seal Fate* (45 double combo point on crit), and *Noxious Assault* (50 twin-weapon strike).
  - *Levels 50–60:* Combat dip (*Opportunity* 5/5 + *Precision* 3/5) to maximize hit and energy delivery.
- **Subtlety (`4.2`):**
  - *Levels 10–30:* Stealth and bleed path: *Camouflage* (5/5), *Serrated Blades* (3/3 armor penetration), *Initiative* (3/3), *Ghostly Strike* (3/3), and *Hemorrhage* (30 low-cost debuff strike).
  - *Levels 30–45:* *Preparation* (35 cooldown reset), *Shadow of Death* (1/1 burst), *Bloody Mess* (2/2 bleed scaling), and *Mark for Death* (45 capstone).
  - *Levels 45–60:* *Honor Among Thieves* (2/2 group crit synergy) and 10 points in Assassination (*Malice* 5/5 + *Ruthlessness* 3/3 + *Murder* 2/2) for reliable finisher cycling.

