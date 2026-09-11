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

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots provides verified 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Restoration** | `7.0` | Healer | `13 / 0 / 38` | Ancestral Swiftness (40), Spirit Link (50), Improved Chain Heal, Improved Water Shield, Elemental Focus dip. |
| **Enhancement** | `7.1` | Melee DPS | `17 / 34 / 0` | Stormstrike (40), Bloodlust (50), Elemental Weapons (40% Windfury AP), Elemental Devastation melee crit synergy. |
| **Elemental** | `7.2` | Ranged DPS | `42 / 0 / 9` | Elemental Mastery (40), Earthquake (45), Call of Thunder, Lightning Mastery, Resto efficiency dip. |

### Leveling Milestones & Progression Rationale

- **Restoration (`7.0`):**
  - *Levels 10–35:* Healing throughput foundation: *Improved Healing Wave* (5/5 cast time reduction), *Tidal Focus* (5/5 cost reduction), *Ancestral Healing* (3/3 armor buff on crit heal), *Healing Way* (3/3), and *Restorative Totems* (5/5).
  - *Levels 35–50:* *Improved Water Shield* (3/3 mana sustain), *Ancestral Swiftness* (40 emergency instant heal), *Tidal Surge* (2/2), and *Spirit Link* (50 capstone damage distribution).
  - *Levels 50–60:* Transitions into Elemental for *Convection* (5/5 shock/spell cost reduction), *Earth's Grasp* (2/2), and *Elemental Focus* (Clearcasting).
- **Enhancement (`7.1`):**
  - *Levels 10–35:* Melee burst foundation: *Ancestral Knowledge* (5/5), *Thundering Strikes* (5/5 crit), *Stable Shields* (3/3), *Lightning Strike* (1/1 instant melee strike), and *Flurry* (30 +30% attack speed on crit).
  - *Levels 35–50:* *Elemental Weapons* (3/3 +40% Windfury AP bonus), *Enhancing Totems* (2/2), *Stormstrike* (40 extra attack), and *Bloodlust* (50 capstone).
  - *Levels 50–60:* Elemental synergy dip into *Convection* (5/5), *Concussion* (5/5 shock damage), *Elemental Focus* (1/1), and *Elemental Devastation* (3/3 granting +9% melee crit on spell crits).
- **Elemental (`7.2`):**
  - *Levels 10–40:* Caster nuking path: *Convection* (5/5), *Concussion* (5/5), *Elemental Focus* (1/1 Clearcasting), *Call of Thunder* (5/5 Lightning crit), *Call of Flame* (3/3), *Storm Reach* (2/2 range), and *Elemental Mastery* (40 guaranteed crit).
  - *Levels 40–50:* *Lightning Mastery* (5/5 -1s Lightning Bolt cast time) and *Earthquake* (45 custom AoE capstone).
  - *Levels 50–60:* Restoration dip into *Improved Healing Wave* (5/5) and *Tidal Focus* (4/5) to conserve mana during long dungeon encounters.

