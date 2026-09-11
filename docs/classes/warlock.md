---
id: class-warlock
title: Warlock Bot AI & Specs
category: classes
summary: Deep dive into Warlock DoT spreading, demon summoning, Soul Shard economy, Life Tap, and custom Dark Harvest / Power Overwhelming.
tags: [class, warlock, dps, ranged, pet]
relates_to:
  - class-overview
  - guide-player-controls
---

# Warlock Bot AI & Specs

Warlocks provide sustained Shadow and Fire DPS through curses and damage-over-time (DoT) spells, unique pet summons, Healthstones, and crowd control.

## Supported Specs & Roles

- **Affliction (Ranged DPS):** Dominant DoT dealer with *Corruption*, *Curse of Agony*, *Siphon Life*, and *Drain Life*.
- **Demonology (Pet DPS / Tanky):** Heavy pet empowerment, *Soul Link*, *Demonic Sacrifice*, and high durability.
- **Destruction (Burst DPS):** Fire burst nuke specialist utilizing *Shadow Bolt*, *Immolate*, *Conflagrate*, and *Searing Pain*.

---

## Combat Rotations & Priorities

### 1. DoT Upkeep & Shard Economy
- **Curses:** Coordinates curses with group composition: *Curse of Elements* (for Mages), *Curse of Shadows* (for Warlocks/Shadow Priests), *Curse of Weakness* (on heavy melee packs), or *Curse of Agony*.
- **DoTs:** Applies *Corruption* and *Immolate* to high-health targets.
- **Soul Shard Harvest:** Automatically casts *Drain Soul* when non-elite mobs fall below 15% health to restock the bot's Soul Shard pouch.

### 2. Mana Management: Life Tap
- Warlocks dynamically cast *Life Tap* to convert surplus health into mana.
- Safety check: *Life Tap* is suppressed if the bot's health is below 50% or if taking heavy incoming damage, preventing accidental suicide.

---

## Turtle WoW 1.18.1 Custom Content

- **Dark Harvest (Spell ID 52550):**
  - Custom Turtle WoW Affliction talent requiring 2+ active DoTs on the target.
  - Deals rapid Shadow damage with a 30-second cooldown that is automatically refunded if the target dies while afflicted.
- **Power Overwhelming (Spell ID 51714):**
  - Custom Demonology talent requiring pet health > 60%.
  - Sacrifices pet health to break crowd control on the demon and unleash massive burst damage.
- **Rain of Fire Channeling:**
  - Includes safe channel cancellation if all mobs leave the AoE radius or if the bot takes critical damage.

---

## Demon Summons & Utility

- **Pet Selection:**
  - *Imp:* Provides *Blood Pact* (Stamina buff) for dungeon parties.
  - *Voidwalker:* Off-tanks and uses *Sacrifice* for emergency shields.
  - *Succubus:* Provides humanoid crowd control via *Seduce*.
  - *Felhunter:* Uses *Spell Lock* for ranged interrupts and *Devour Magic* for offensive/defensive dispels.
- **Healthstones & Soulstones:**
  - Creates and uses *Healthstones* during combat.
  - Creates and stores Soulstones on the party healer or tank before boss pulls.
- **Crowd Control:**
  - Casts *Fear* on designated marks (or when fleeing).
  - Casts *Banish* on Demons and Elementals.
