---
id: classes-mage
title: Mage Bot Guide
category: classes
summary: Complete guide to Mage bot AI, elemental rotations, Polymorph CC, Counterspell interrupts, conjured refreshments, and Arcane Power safety.
tags: [classes, mage, dps, ranged, cc]
relates_to:
  - classes-overview
---

# Mage Bot Guide

Mages provide high-burst ranged spell DPS, dependable crowd control (*Polymorph*), school-locking interrupts (*Counterspell*), and party refreshments.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Frost** | Ranged DPS | Crowd control, survivability, and shattered critical strikes. |
| **Fire** | Ranged DPS | Massive spell burst, periodic Ignite burns, and instant Pyroblasts. |
| **Arcane** | Ranged DPS | High single-target burst, mana efficiency, and instant Presence of Mind casts. |

---

## What the bot does in combat

### Frost Mage
- Opens from maximum range with *Frostbolt*.
- If enemies close within 8 yards and attack the mage, turns away and casts *Blink* to reset distance.
- Roots attackers with *Frost Nova* — only when a mob within reach is attacking the mage itself, and skipping targets already frozen or immune.
- Casts *Arcane Explosion* when 3 or more enemies cluster inside 10 yards and none of them is loose on the party.
- Casts *Flamestrike* on 3 or more grouped enemies, layering channeled *Blizzard* over the burning zone.
- Weaves *Cone of Cold* against 2 or more attackers in front of the mage within 10 yards.
- Activates *Cold Snap* once Frost cooldowns (*Frost Nova*, *Cone of Cold*, *Ice Barrier*, *Ice Block*, *Frost Ward*) are spent.
- Area spells (*Blizzard*, *Flamestrike*, *Cone of Cold*) pause during [party threat back-off](overview.md#party-threat-back-off) if holding 50% or more tank threat.

### Fire Mage
- Casts *Fireball* as primary filler and weaves *Scorch* to maintain Fire Vulnerability debuffs.
- Rushes instant *Pyroblast* casts upon reaching full *Hot Streak* stacks.
- Consumes *Presence of Mind* with instant *Pyroblast*.
- Uses *Fire Blast* on the move or as an instant finishing blow.
- Casts *Arcane Explosion* when 3 or more enemies cluster inside 10 yards and none of them is loose on the party.
- Casts *Blast Wave* as a point-blank interrupt and daze against nearby clustered enemies.
- Automatically falls back to *Frostbolt* when facing fire-immune enemies.

### Arcane Mage
- Opens with *Arcane Rupture*, then channels *Arcane Missiles* while the empowerment buff holds.
- Uses *Presence of Mind* for instant burst spells.
- Casts *Arcane Explosion* when hostiles crowd into point-blank range.

<details>
<summary>Wand and mana recovery details</summary>

- When the primary filler is out of mana reach (*Arcane Missiles* at 320-410 mana, *Fireball* at 190-265), the bot fires cheaper *Frostbolt* (130-195) before falling back to the wand.
- On Turtle WoW, wand swings trigger the global cooldown. The bot uses a dedicated `wand blocks nuke` trigger to stop wanding as soon as filler spells are affordable again.
- While under threat back-off (≥80% of tank threat) or out of mana, the bot relies on *Shoot* to maintain damage safely.
- Wand swings immediately stand down when a *Polymorph* target is queued for crowd control.

</details>

---

## Turtle WoW custom spells

- **Arcane Power Safety (Spell ID 12042):** *Arcane Power* cannot be cancelled once active. The bot enforces a strict 70% mana floor and checks for a live hostile in range before activating.
- **Evocation Channeling (Spell ID 12051):** Automatically channels *Evocation* when out of mana and cancels the channel at 95% mana to resume combat.
- **Icicles (Spell ID 52516):** Custom Frost talent providing root and shatter synergy without disrupting primary spell rotations.

---

## Utility & interrupts

- **Polymorph CC:** Casts *Polymorph* on targets assigned via `.bot action cc <mark>`. Stands still during casting and prevents AoE spells near sheeped mobs.
- **Interrupts:** Casts *Counterspell* immediately upon detecting dangerous enemy spell casts, locking the school.
- **Food & Drink Conjuration:** Conjures food and water out of combat, sharing stacks with party members in need.
- **Mana Gems & Runes:** Consumes the strongest mana gem held (Ruby down to Agate) below 40% mana in combat, re-conjuring gems between pulls. Uses *Dark Rune* when gems are exhausted.
- **Party Buffs:** Maintains *Arcane Intellect* (and *Arcane Brilliance*) on mana users, refreshing expiring buffs (<15s) and coordinating buff claims with other mages.
- **Defensive Wards:** Casts *Mana Shield* at low health and *Ice Block* at critical health.
- **Decurse:** Casts *Remove Lesser Curse* on party members afflicted by debilitating curses.

---

## Talent builds

TortoiseBots provides pre-configured talent progressions at 5-level checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Arcane** | `8.0` | Ranged DPS | `40 / 0 / 11` | Arcane Power, Presence of Mind, Temporal Convergence, Accelerated Arcana, Frost hit dip. |
| **Fire** | `8.1` | Ranged DPS | `10 / 41 / 0` | Hot Streak, Combustion, Master of Elements, Clearcasting + threat dip. |
| **Frost** | `8.2` | Ranged DPS | `10 / 0 / 41` | Shatter, Ice Block, Ice Barrier, Icicles, Flash Freeze, Clearcasting dip. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Arcane (`8.0`):**
  - *Levels 10–35:* Takes *Arcane Subtlety* (2/2), *Improved Arcane Missiles* (5/5), *Arcane Concentration* (5/5), *Presence of Mind* (30), and *Temporal Convergence* (3/3).
  - *Levels 35–45:* Takes *Accelerated Arcana* (1/1), *Arcane Power* (40), and *Resonance Cascade* (5/5).
  - *Levels 45–60:* Dips into Frost for *Improved Frostbolt* (5/5) and *Elemental Precision* (3/3) for spell hit cap.
- **Fire (`8.1`):**
  - *Levels 10–35:* Takes *Improved Fireball* (5/5), *Ignite* (5/5), *Pyroblast* (20), *Master of Elements* (3/3), and *Hot Streak* (30).
  - *Levels 35–45:* Takes *Fire Power* (5/5) and *Combustion* (40).
  - *Levels 45–60:* Takes *Burning Soul* (2/2) and 10 Arcane points (*Arcane Subtlety* 2/2 + *Arcane Concentration* 5/5).
- **Frost (`8.2`):**
  - *Levels 10–35:* Takes *Improved Frostbolt* (5/5), *Elemental Precision* (3/3), *Ice Shards* (5/5), *Cold Snap* (25), and *Shatter* (30).
  - *Levels 35–45:* Takes *Ice Block* (35), *Ice Barrier* (40), *Icicles* (1/1), and *Winter's Chill* (5/5).
  - *Levels 45–60:* Takes *Flash Freeze* (2/2) and 10 Arcane points (*Arcane Subtlety* 2/2 + *Arcane Concentration* 5/5).

</details>

---

## Class settings

There are no class-specific configuration flags for Mages. General combat and mana thresholds are configured in [Combat settings](../server/settings/combat.md).
