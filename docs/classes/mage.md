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
