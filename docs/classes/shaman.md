---
id: class-shaman
title: Shaman Bot AI & Specs
category: classes
summary: Deep dive into Shaman totem orchestration, weapon imbues, chain heals, interrupts, and custom Turtle spells.
tags: [class, shaman, healer, dps, totems, melee]
relates_to:
  - class-overview
  - guide-player-controls
---

# Shaman Bot AI & Specs

Shamans bring unparalleled group utility through totem sets, elemental shocks, weapon imbues, and potent chain spells across Melee DPS, Caster DPS, and Healing.

## Supported Specs & Roles

- **Restoration (Healer):** Premier multi-target healer utilizing *Chain Heal*, *Healing Wave*, *Lesser Healing Wave*, and *Mana Tide Totem*.
- **Enhancement (Melee DPS):** Dual-wielding or two-handed melee powerhouse utilizing *Windfury*, *Stormstrike*, and shocks.
- **Elemental (Ranged DPS):** Nature and fire caster driving high burst through *Lightning Bolt*, *Chain Lightning*, and *Elemental Mastery*.

---

## Totem Orchestration

Shamans automatically drop and maintain 4-element totem sets based on party composition:

- **Earth Totem:** *Strength of Earth Totem* (for melee groups), *Stoneskin Totem*, or *Tremor Totem* (against fear/charm).
- **Fire Totem:** *Searing Totem* (single target), *Magma Totem* / *Fire Nova Totem* (AoE packs).
- **Water Totem:** *Mana Spring Totem* (caster/healer mana), *Healing Stream Totem*, or *Poison Cleansing Totem*.
- **Air Totem:** *Windfury Totem* (melee attack speed), *Grace of Air Totem* (agility), or *Grounding Totem* (redirecting hostile spells).

---

## Turtle WoW 1.18.1 Custom Content

- **Earthquake (Spell ID 48306):**
  - Custom Turtle WoW Elemental AoE spell causing Nature damage and aftershocks. Integrated into Elemental AoE rotations when AoE is enabled.
- **Lightning Strike (Spell ID 51387):**
  - Custom Enhancement talent that consumes Lightning Shield charges for an instant Nature burst.
- **Spirit Link (Spell ID 51363):**
  - Restoration talent linking party members to distribute incoming tank damage evenly across the group, mitigating lethal spike damage.
- **Ancestral Swiftness (Spell ID 16188):**
  - Instant cast trigger paired with *Healing Wave* for instantaneous emergency tank saves.
- **Bloodlust (Spell ID 45509):**
  - Custom Turtle WoW enhancement ability granting self frenzy and boosting party melee critical strikes.

---

## Utility & Interrupts

- **Interrupts:** Casts rank 1 *Earth Shock* instantly to interrupt enemy spell casts with minimal mana cost and a 6-second cooldown.
- **Weapon Imbues:** Automatically maintains *Windfury Weapon*, *Rockbiter Weapon*, or *Flametongue Weapon* on equipped weapons.
- **Dispels & Cleansing:** Uses *Purge* to strip enemy buffs (shields, HoTs) and *Cure Poison* / *Cure Disease* on party members.
- **Self-Resurrection:** Uses *Reincarnation* (Ankh) to revive after combat wipes.
