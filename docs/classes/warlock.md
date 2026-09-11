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

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots provides verified 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Affliction** | `9.0` | Ranged DPS | `40 / 0 / 11` | Dark Harvest (40), Shadow Mastery (40), Nightfall, Siphon Life, Bane -0.5s Shadow Bolt dip. |
| **Demonology** | `9.1` | Ranged DPS / Tanky | `7 / 44 / 0` | Power Overwhelming (30), Soul Link (40), Master Demonologist, Instant Corruption dip. |
| **Destruction** | `9.2` | Ranged DPS | `7 / 0 / 44` | Conflagrate (40), Ruin (30), Instant Corruption + Improved Life Tap dip. 0 points in Searing Pain threat. |

### Leveling Milestones & Progression Rationale

- **Affliction (`9.0`):**
  - *Levels 10–35:* DoT scaling foundation: *Improved Corruption* (5/5 instant cast), *Improved Life Tap* (2/2), *Nightfall* (2/2 instant Shadow Bolt on Corruption/Drain Life ticks), *Grim Reach* (2/2 range), *Soul Siphon* (3/3), and *Siphon Life* (30 periodic health siphon).
  - *Levels 35–45:* *Rapid Deterioration* (2/2), *Shadow Mastery* (40 +10% shadow damage), and *Dark Harvest* (40 Turtle custom finisher).
  - *Levels 45–60:* *Suppression* (5/5 spell hit cap) and 11 Destruction points (*Cataclysm* 5/5 + *Bane* 5/5 + *Shadowburn* 1/1) reducing Shadow Bolt cast time by 0.5s.
- **Demonology (`9.1`):**
  - *Levels 10–35:* Pet durability and burst: *Demonic Embrace* (5/5 Stamina), *Demonic Aegis* (3/3 Armor/healing buff), *Fel Intellect* (3/3), *Fel Domination* (1/1 fast pet summon), *Unholy Power* (3/3 pet damage), and *Power Overwhelming* (30 custom pet sacrifice burst).
  - *Levels 35–45:* *Demonic Precision* (3/3 pet spell hit), *Master Demonologist* (5/5 stat scaling), and *Soul Link* (40 30% damage transfer).
  - *Levels 45–60:* Completes Demonology utility (*Unleashed Potential* + *Nether Studies*), then takes 7 Affliction points into *Improved Corruption* (5/5) and *Improved Life Tap* (2/2).
- **Destruction (`9.2`):**
  - *Levels 10–35:* Fire/Shadow nuke path: *Cataclysm* (5/5 cost reduction), *Bane* (5/5 cast time reduction), *Shadowburn* (20 instant soul shard burst), *Devastation* (5/5 +5% crit), and *Ruin* (30 +100% crit damage bonus).
  - *Levels 35–45:* *Improved Immolate* (5/5) and *Conflagrate* (40 instant burst consuming Immolate).
  - *Levels 45–60:* Takes 7 Affliction points into *Improved Corruption* (5/5 instant cast) and *Improved Life Tap* (2/2) to eliminate cast vulnerability, then finishes Destruction with *Emberstorm* (5/5) and *Demonic Swiftness* (2/2 reduced Imp Firebolt cast time). Avoids threat-increasing talents (*Improved Searing Pain*).

