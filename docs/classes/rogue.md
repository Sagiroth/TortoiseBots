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
- **Slice and Dice Priority:** Combat ranks *Slice and Dice* above *Eviscerate*; Assassination and Subtlety rank *Eviscerate* above *Slice and Dice*.
- **Finishers:**
  - 4–5 Combo Points: Casts *Eviscerate* for burst damage (falling back to *Rupture* when the debuff is missing; Assassination/Subtlety also cast *Rupture* from 3+ combo points, on any target).
  - Interrupts are covered by *Kick* (*Cheap Shot* while stealthed); *Kidney Shot* is only a fallback when *Kick* cannot run.

### 3. Burst Cooldowns
- Combat casts *Adrenaline Rush* and *Blade Flurry* during tough encounters or multi-mob pulls; Assassination fires *Cold Blood* and Subtlety fires *Preparation*.
- Activates *Evasion* (with *Feint*) on the low-health trigger (own HP in the low band), regardless of aggro source.
- Activates *Vanish* if health falls below 20% to wipe threat.

---

## Turtle WoW 1.18.1 Custom Content

- **Surprise Attack (Spell ID 52511, per game data):**
  - Combat combo-builder strike wired into the Combat rotation via a combo-gated trigger.
- **Noxious Assault (Spell ID 52714, per game data):**
  - Assassination talent providing +30% Attack Power and instant poison delivery, per game data.
- **Envenom (Spell ID 52531, per game data):**
  - Finisher consuming Deadly Poison stacks for instant Nature damage and an attack-speed poison buff, per game data.
- **Shadow of Death (Spell ID 52710) & Mark for Death (Spell ID 52538, per game data):**
  - Subtlety talents for banked burst detonation and party-wide attack power enhancement on fresh targets, per game data.

---

## Utility & Poisons

- **Poisons:** Poison choice is context-dependent: raid/generic uses Instant (main hand) + Deadly (off hand, Instant fallback while Deadly is untrained); PvE uses Instant on both hands; PvP uses Mind (main hand) + Crippling (off hand). Open-world rogues keep Instant on the main hand themselves; expiry or rank upgrades re-apply automatically. Wound-poison actions exist per hand, but the factory seeds no wound ladder yet — Instant/Deadly/Crippling/Mind only.
- **Interrupts:** Casts *Kick* instantly to lock out enemy spell schools.
- **Gouge & Blind:** Both are self-defence reactions — *Gouge* fires on the bot's own medium-health band against its current target; *Blind* is the top-priority critical-health self-peel, ahead of *Vanish*.

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

