---
id: class-mage
title: Mage Bot AI & Specs
category: classes
summary: Deep dive into Mage ranged elemental rotations, Polymorph CC, Counterspell interrupts, food/water conjuring, and Arcane Power safety.
tags: [class, mage, dps, ranged, cc]
relates_to:
  - class-overview
  - guide-player-controls
---

# Mage Bot AI & Specs

Mages provide premier ranged spell DPS, the game's most reliable crowd control (*Polymorph*), school-locking interrupts (*Counterspell*), and free party refreshments.

## Supported Specs & Roles

- **Frost (Ranged DPS):** Exceptional control and survivability. Leverages *Frostbolt*, *Frost Nova*, *Blizzard*, and *Ice Barrier*.
- **Fire (Ranged DPS):** Massive burst damage with *Fireball*, *Pyroblast*, *Scorched Earth*, and *Combustion*.
- **Arcane (Ranged DPS):** High single-target burst with *Arcane Missiles*, *Arcane Power*, and *Presence of Mind*.

---

## Combat Rotations & Priorities

### 1. Frost Mage
- Opens at max range with *Frostbolt*.
- If enemies reach melee range, casts *Frost Nova* and *Blink* to reset distance.
- Uses *Cone of Cold* and *Blizzard* when AoE is enabled.
- Uses *Cold Snap* when defensive barriers or ice blocks are exhausted.

### 2. Fire Mage
- Pulls with *Pyroblast* if out of combat.
- Weaves *Scorched* stacks to apply *Improved Scorch* fire vulnerability.
- Casts *Fireball* as main nuke and *Fire Blast* on the move or for finishing blows.

### 3. Arcane Mage
- Channels *Arcane Missiles* with mana management.
- Uses *Presence of Mind* for instant cast nukes.

---

## Turtle WoW 1.18.1 Custom Content & Safety Gates

- **Arcane Power Safety (Spell ID 12042):**
  - *Arcane Power* carries the `SPELL_ATTR_CANT_CANCEL` attribute, meaning that once activated, the 20-second mana drain cannot be stopped.
  - The bot implements an explicit **70% mana floor** and verifies that a live hostile target is in range before activating Arcane Power, preventing bots from draining themselves dry right before combat ends.
- **Evocation Channeling (Spell ID 12051):**
  - Automatically activates *Evocation* when out of mana.
  - Automatically cancels the channel once mana reaches 95% to immediately re-enter combat rather than standing idle.
- **Icicles (Spell ID 52516):**
  - Custom Turtle Frost talent providing root and shatter synergy without breaking primary frost rotation loops.

---

## Utility, CC & Party Refreshments

- **Polymorph CC:** Instantly casts *Polymorph* (Sheep) on targets assigned via `.bot action cc <mark>`. Avoids damaging or AoEing sheeped targets.
- **Interrupts:** Casts *Counterspell* immediately when an enemy begins casting a dangerous spell, locking out that spell school for up to 10 seconds.
- **Food & Drink Conjuration:** Automatically conjures food and water out of combat, sharing stacks with party members who need mana or health.
- **Buffs:** Maintains *Arcane Intellect* on all mana-using party members and self *Mage Armor* / *Ice Armor*.
- **Curses:** Uses *Remove Lesser Curse* on party members affected by debilitating curses.

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots provides verified 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Arcane** | `8.0` | Ranged DPS | `40 / 0 / 11` | Arcane Power (40), PoM (30), Temporal Convergence, Accelerated Arcana, Frost hit cap dip. 0 points in Wand Spec. |
| **Fire** | `8.1` | Ranged DPS | `10 / 41 / 0` | Hot Streak (30), Combustion (40), Master of Elements, 5/5 Clearcasting + -40% threat dip. 0 points in Wand Spec. |
| **Frost** | `8.2` | Ranged DPS | `10 / 0 / 41` | Shatter (30), Ice Block (35), Ice Barrier (40), Icicles, Flash Freeze, Clearcasting + threat dip. 0 points in Wand Spec. |

### Leveling Milestones & Progression Rationale

- **Arcane (`8.0`):**
  - *Levels 10–35:* Burst and mana foundation: *Arcane Subtlety* (2/2 -40% threat), *Improved Arcane Missiles* (5/5 anti-pushback), *Arcane Concentration* (5/5 Clearcasting), *Presence of Mind* (30 instant cast), and *Temporal Convergence* (3/3 haste).
  - *Levels 35–45:* *Accelerated Arcana* (1/1), *Arcane Power* (40 +30% spell damage burst), and *Resonance Cascade* (5/5).
  - *Levels 45–60:* Frost dip into *Improved Frostbolt* (5/5) and *Elemental Precision* (3/3 providing 6% spell hit cap and threat reduction) to ensure high raid damage without resisting.
- **Fire (`8.1`):**
  - *Levels 10–35:* Fire nuke ramping: *Improved Fireball* (5/5 cast time reduction), *Ignite* (5/5 +40% periodic crit burn), *Pyroblast* (20 opener), *Master of Elements* (3/3 mana refund on crit), and *Hot Streak* (30 instant Pyroblast procs).
  - *Levels 35–45:* *Fire Power* (5/5 fire spell damage) and *Combustion* (40 critical strike engine).
  - *Levels 45–60:* *Burning Soul* (2/2 pushback resistance) and 10 Arcane points (*Arcane Subtlety* 2/2 for threat reduction + *Arcane Concentration* 5/5 for Clearcasting free spells).
- **Frost (`8.2`):**
  - *Levels 10–35:* Shatter and control engine: *Improved Frostbolt* (5/5), *Elemental Precision* (3/3 spell hit cap), *Ice Shards* (5/5 +100% crit damage bonus), *Cold Snap* (25 cooldown reset), and *Shatter* (30 +50% crit on frozen targets).
  - *Levels 35–45:* *Ice Block* (35 emergency immunity), *Ice Barrier* (40 damage absorb shield), *Icicles* (1/1), and *Winter's Chill* (5/5 stacking frost crit debuff).
  - *Levels 45–60:* *Flash Freeze* (2/2 instant Frostbolt procs) and 10 Arcane points into *Arcane Subtlety* (2/2) and *Arcane Concentration* (5/5 Clearcasting).

