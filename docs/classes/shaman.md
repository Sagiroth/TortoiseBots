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

- **Restoration (Healer):** Premier multi-target healer utilizing *Chain Heal*, *Healing Wave*, *Lesser Healing Wave*, and *Mana Tide Totem*. *Chain Heal* fires on headcount (5+ members within 30 yards, 3 hurt in a 5-man scaling to 15 in a raid). When nobody needs healing and mana is comfortable, casts *Flame Shock* / *Lightning Bolt* (*Chain Lightning* into packs) at the lowest relevance so every heal wins first — the same healer-dps pattern priests use.
- **Enhancement (Melee DPS):** Dual-wielding or two-handed melee powerhouse utilizing *Windfury*, *Stormstrike*, and shocks.
- **Elemental (Ranged DPS):** Nature and fire caster driving high burst through *Lightning Bolt*, *Chain Lightning*, and *Elemental Mastery*.

---

## Totem Orchestration

Shamans automatically drop and maintain fixed per-spec 4-element totem sets (other totems are selectable via manual `totem ...` strategies):

- **Earth Totem:** *Strength of Earth Totem* (automatic; *Stoneskin Totem* as fallback). Solo bots at low health drop *Stoneclaw Totem* as a panic button instead (taunts attackers off the bot); grouped bots keep the spec totem unless ordered `totem earth stoneclaw`. *Tremor Totem* is manual-only.
- **Fire Totem:** *Searing Totem* (single target), *Magma Totem* / *Fire Nova Totem* (AoE packs).
- **Water Totem:** *Mana Spring Totem* or *Healing Stream Totem* (automatic per spec). *Poison Cleansing Totem* is manual-only.
- **Air Totem:** *Windfury Totem* (automatic; *Grace of Air Totem* for Enhancement), *Grounding Totem* as fallback.

---

## Turtle WoW 1.18.1 Custom Content

- **Earthquake (Spell ID 48306):**
  - Custom Turtle WoW Elemental AoE spell causing Nature damage and aftershocks. Integrated into Elemental AoE rotations when AoE is enabled.
- **Lightning Strike (Spell ID 51387):**
  - Custom Enhancement talent that releases the active shield for an instant Nature burst.
- **Spirit Link (Spell ID 51363):**
  - Restoration talent linking party members to distribute incoming tank damage evenly across the group, mitigating lethal spike damage.
- **Ancestral Swiftness (Spell ID 16188):**
  - Instant cast trigger paired with *Healing Wave* for instantaneous emergency tank saves.
- **Bloodlust (Spell ID 45509):**
  - Custom Turtle WoW enhancement ability: self frenzy whose haste spreads through party melee crits.

---

## Utility & Interrupts

- **Shock rotation:** Elemental and Enhancement keep *Flame Shock* up first, then spend the shared shock cooldown on *Earth Shock*; *Earth Shock* keeps its separate interrupt duty on casting targets (own target and off-target enemy healers).
- **Strike order (Enhancement):** *Stormstrike* fires before the shield-consuming *Lightning Strike*, so the nature-vulnerability debuff amplifies everything after it.
- **Snares:** Casts *Frost Shock* to snare targets.
- **Weapon Imbues:** Automatically maintains weapon imbues (*Windfury Weapon* / *Flametongue Weapon* for Enhancement, *Flametongue Weapon* otherwise, with *Rockbiter Weapon* / *Frostbrand Weapon* as fallbacks).
- **Dispels & Cleansing:** Uses *Purge* to strip enemy buffs (shields, HoTs) and *Cure Poison* / *Cure Disease* on party members (no curse cure exists for shamans in 1.12; curses stay a druid/mage job).
- **Mana sense:** Never skips a target at or below the low-health line for mana reasons; above the medium line it refuses oversized or average-efficiency heals, and below the medium-mana line it refuses mana-hungry heals (*Lesser Healing Wave*) in favor of *Healing Wave*. Tanks count the expected heal at two-thirds.
- **Resurrection:** Resurrects fallen party members with *Ancestral Spirit* (no self-Reincarnation).
- **Defensive & Utility:** Keeps *Earth Shield* on the party tank, uses *Call of the Elements* / *Call of the Ancestors* / *Call of the Spirits* totem recall, and shifts to *Ghost Wolf* when carrying a PvP flag.

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

