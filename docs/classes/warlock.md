---
id: classes-warlock
title: Warlock Bot Guide
category: classes
summary: Complete guide to Warlock bot AI, DoT spreading, demon summoning, Soul Shard economy, Life Tap, and custom Turtle abilities.
tags: [classes, warlock, dps, ranged, pet]
relates_to:
  - classes-overview
---

# Warlock Bot Guide

Warlocks provide sustained Shadow and Fire DPS through curses, damage-over-time (DoT) spells, demon summons, Healthstones, and crowd control (*Fear*, *Banish*).

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Affliction** | Ranged DPS | Multi-target DoT spreading, Drain Life sustain, and Dark Harvest burst. |
| **Demonology** | Ranged DPS / Tanky | Heavy demon empowerment, Soul Link damage sharing, Demonic Sacrifice, and high durability. |
| **Destruction** | Ranged DPS | Fire burst damage, Shadow Bolt nukes, and instant Conflagrate explosions. |

---

## What the bot does in combat

### Affliction
- Applies and maintains *Curse of Agony*, *Siphon Life*, and *Immolate* on primary targets. *Corruption* upkeep runs through the shared warlock rotation.
- Spreads *Corruption* and *Siphon Life* across secondary attackers under the AoE strategy.
- Casts *Dark Harvest* when 2 or more DoTs are active on the target.
- Uses *Dark Pact* and *Life Tap* to replenish mana from pet and personal health pools.
- Channels *Drain Life* for self-sustain and *Drain Soul* on dying enemies (at or below 20% health) to harvest shards.

### Demonology
- Summons resilient demons, links damage via *Soul Link*, and utilizes *Demonic Sacrifice* in raids.
- Maintains *Immolate* while casting *Shadow Bolt* fillers. *Corruption* upkeep runs through the shared warlock rotation.
- Commands the demon to cast *Power Overwhelming* during burst windows while the demon stays above 60% health.
- Channels *Health Funnel* when the active demon falls below 50% health in combat, provided personal health exceeds 60%.

### Destruction
- Opens with *Immolate* and defaults to *Curse of the Elements* once trained (level 32), *Curse of Agony* before that.
- Delivers burst damage using *Conflagrate* and *Shadowburn*.
- Casts *Shadow Bolt* as primary nuke and channels *Rain of Fire* on packs of 3 or more clustered enemies. *Shadowburn* fires through the shared warlock rotation on dying targets.
- Cancels *Rain of Fire* channels if fewer than 2 enemies remain in the target area, or a mob under the party's crowd control stands in it.

<details>
<summary>Soul Shard harvest and Life Tap safety</summary>

- **Shard Harvest:** Warlocks harvest Soul Shards organically with *Drain Soul* when targets drop to at or below 20% health. Bots maintain up to 5 shards, destroying excess shards out of combat.
- **Life Tap Safety:** *Life Tap* requires personal health above the low-health line (50% default). The urgent tap fires at or below medium mana (40% default) in combat, while top-up tapping occurs only out of combat between pulls to avoid pausing spell damage.
- **Wand Stops:** Wand swings incur global cooldown on Turtle WoW. Dedicated triggers stop active wanding immediately when spells become affordable or crowd control is needed.

</details>

---

## Turtle WoW custom spells

- **Dark Harvest (Spell ID 52550):** Affliction finisher requiring 2 or more active own DoTs on the target, dealing rapid Shadow damage. Cooldown refunds if the target dies while afflicted.
- **Power Overwhelming (Spell ID 51714):** Demonology pet burst breaking crowd control and increasing pet damage at the cost of base demon health.
- **Rain of Fire Safety:** Channels pause during [party threat back-off](overview.md#party-threat-back-off) if the warlock holds ≥50% tank threat.

---

## Utility & interrupts

- **Demon Summons:**
  - *Voidwalker:* Tanking demon that taunts with *Torment* in solo or tankless play and shields the warlock with *Sacrifice* in emergencies; grouped with a tank it never taunts.
  - *Imp:* Raid pet providing *Blood Pact* stamina buffs and ranged fire support.
  - *Felhunter:* Utility pet casting *Spell Lock* for ranged interrupts and *Devour Magic* for offensive and defensive dispels.
  - *Succubus:* CC demon casting *Seduction* on humanoids.
- **Pet Entry:** In a party with a living tank the demon is sent in once the tank really holds the mob (a tank is its victim, or the tank's threat has reached 110% of the victim's in melee reach, 130% outside); a mob already on the warlock, an enemy player, a tankless party and an explicit `pet attack` order send it at once. Before 2026-10-21 the demon went in the moment the warlock picked a target, ahead of the tank.
- **Healthstones & Soulstones:** Creates and uses *Healthstones* in combat. Stores one Soulstone out of combat, prioritizing the party healer, then the tank.
- **Weapon Stones & Buffs:** Conjures and carries *Spellstones* (Affliction/Demonology) or *Firestones* (Destruction) in bags. Maintains *Demon Armor* (*Demon Skin* fallback) and *Unending Breath* on self and party while swimming.
- **Curse Overrides:** Defaults to spec-appropriate curses (*Curse of Agony*, *Curse of the Elements*, or *Curse of Shadow*); players can manually override curse assignments using [`.bot strategy +curse <shadow|weakness|elements|agony|doom|tongues|exhaustion>`](../play/commands/party-actions.md#bot-strategy). Exhaustion snares runners; further warlocks cover Recklessness in raids; curse claims prevent duplicate picks (shared registry).
- **Fel Domination:** Burns the 5-minute *Fel Domination* cooldown to summon a replacement demon during combat if the active pet dies.
- **Crowd Control:** Casts *Fear* on assigned CC marks (skipping undead, mechanical, or already controlled targets) and *Banish* on Demons and Elementals. Area spells avoid targets near CC marks.

---

## Talent builds

TortoiseBots provides pre-configured talent progressions at 5-level checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Affliction** | `9.0` | Ranged DPS | `40 / 0 / 11` | Dark Harvest, Shadow Mastery, Nightfall, Siphon Life, Bane -0.5s Shadow Bolt dip. |
| **Demonology** | `9.1` | Ranged DPS / Tanky | `7 / 44 / 0` | Power Overwhelming, Soul Link, Master Demonologist, Instant Corruption dip. |
| **Destruction** | `9.2` | Ranged DPS | `7 / 0 / 44` | Conflagrate, Ruin, Instant Corruption + Improved Life Tap dip. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Affliction (`9.0`):**
  - *Levels 10–35:* Takes *Improved Corruption* (5/5), *Improved Life Tap* (2/2), *Nightfall* (2/2), *Grim Reach* (2/2), *Soul Siphon* (3/3), and *Siphon Life* (30).
  - *Levels 35–45:* Takes *Rapid Deterioration* (2/2), *Shadow Mastery* (40), and *Dark Harvest* (40).
  - *Levels 45–60:* Takes *Suppression* (5/5) and 11 Destruction points (*Cataclysm* 5/5 + *Bane* 5/5 + *Shadowburn* 1/1).
- **Demonology (`9.1`):**
  - *Levels 10–35:* Takes *Demonic Embrace* (5/5), *Demonic Aegis* (3/3), *Fel Intellect* (3/3), *Fel Domination* (1/1), *Unholy Power* (3/3), and *Power Overwhelming* (30).
  - *Levels 35–45:* Takes *Demonic Precision* (3/3), *Master Demonologist* (5/5), and *Soul Link* (40).
  - *Levels 45–60:* Takes *Unleashed Potential* and *Nether Studies*, then dips 7 points into Affliction (*Improved Corruption* 5/5 + *Improved Life Tap* 2/2).
- **Destruction (`9.2`):**
  - *Levels 10–35:* Takes *Cataclysm* (5/5), *Bane* (5/5), *Shadowburn* (20), *Devastation* (5/5), and *Ruin* (30).
  - *Levels 35–45:* Takes *Improved Immolate* (5/5) and *Conflagrate* (40).
  - *Levels 45–60:* Takes *Emberstorm* and *Demonic Swiftness* before taking the 7-point Affliction dip (*Improved Corruption* 5/5 + *Improved Life Tap* 2/2).

</details>

---

## Class settings

- [`AiPlayerbot.WarlockPetBySpec`](../server/settings/class-behaviour.md#aiplayerbotwarlockpetbyspec): Configures warlocks to summon the demon tailored to their active talent spec in PvE content.
