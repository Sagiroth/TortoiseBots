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
- Spreads *Corruption*, *Siphon Life*, and *Immolate* across secondary attackers under the AoE strategy.
- Casts *Dark Harvest* when 2 or more of *Corruption*, *Siphon Life*, *Curse of Agony*, or *Curse of Doom* are active on the target (*Immolate* does not count).
- Uses *Dark Pact* (pet mana) and *Life Tap* (own health) to replenish mana.
- Channels *Drain Life* for self-sustain and *Drain Soul* on dying enemies (at or below 20% health) to harvest shards.

### Demonology
- Summons resilient demons, links damage via *Soul Link*, and utilizes *Demonic Sacrifice* in raids.
- Maintains *Immolate* while casting *Shadow Bolt* fillers. *Corruption* upkeep runs through the shared warlock rotation.
- Commands the demon to cast *Power Overwhelming* whenever a live enemy stands and the demon stays above 60% health.
- Channels *Health Funnel* when the active demon falls below 50% health in combat, provided personal health exceeds 60%.
- Burns the *Fel Domination* cooldown to summon a replacement demon during combat if the active pet dies (Demonology kit only).

### Destruction
- Defaults to *Curse of the Elements* once trained, *Curse of Agony* before that (gated by the spellbook, no level check in the bot). The curse outranks *Immolate*, so the curse — not Immolate — lands first.
- Delivers burst damage using *Conflagrate* (only while Immolate ticks with 7 seconds or less left) and *Shadowburn*.
- Casts *Shadow Bolt* as primary nuke and channels *Rain of Fire* on packs of 3 or more clustered enemies. *Shadowburn* fires through the shared warlock rotation on dying targets.
- Cancels *Rain of Fire* channels if fewer than 2 enemies remain in the target area, or a mob under the party's crowd control stands in it.

<details>
<summary>Soul Shard harvest and Life Tap safety</summary>

- **Shard Harvest:** Warlocks harvest Soul Shards organically with *Drain Soul* when targets drop to at or below 20% health. Bots keep up to 5 shards, destroying excess shards out of combat.
- **Life Tap Safety:** *Life Tap* requires personal health above the low-health line (50% default). The urgent tap fires at or below medium mana (40% default) in combat, while top-up tapping occurs only out of combat between pulls to avoid pausing spell damage. Top-up tapping stops at 85% mana.
- **Wand Stops:** Wand swings incur global cooldown on Turtle WoW. Dedicated triggers stop active wanding immediately when spells become affordable (only when Shadow Bolt is GCD-blocked, not for intentional raid or no-mana wands) or crowd control is needed.

</details>

---

## Turtle WoW custom spells

- **Dark Harvest:** Affliction finisher requiring 2 or more active own DoTs on the target, dealing rapid Shadow damage.
- **Power Overwhelming:** Demonology pet burst increasing pet damage at the cost of demon health (refused while the demon is dying).
- **Rain of Fire Safety:** Channels pause during [party threat back-off](overview.md#shared-bot-behaviors) if the warlock holds 50% or more tank threat.

---

## Utility & interrupts

- **Demon Summons:**
  - *Voidwalker:* Tanking demon that taunts with *Torment* in solo or tankless play and shields the warlock with *Sacrifice* in emergencies; grouped with a tank it never taunts.
  - *Imp:* Raid pet providing *Blood Pact* stamina buffs and ranged fire support.
  - *Felhunter:* Utility pet casting *Spell Lock* for ranged interrupts and *Devour Magic* for offensive and defensive dispels.
  - *Succubus:* CC demon casting *Seduction* on humanoids (never on dotted marks).
- **Pet Entry:** In a party the demon is held about 2 seconds for the tank to build a lead; a mob already on the warlock, an enemy player, a tankless party, and an explicit `pet attack` order send it in at once. Crowd-controlled and immune targets are never attacked.
- **Healthstones & Soulstones:** Creates Healthstones out of combat and uses them in combat. Stores one Soulstone out of combat, prioritizing the party healer, then the tank.
- **Weapon Stones & Buffs:** Conjures and carries *Spellstones* (Affliction/Demonology) or *Firestones* (Destruction) in bags. Maintains *Demon Armor* (*Demon Skin* fallback) and *Unending Breath* on self and party while swimming.
- **Curse Overrides:** Defaults to spec-appropriate curses (*Curse of Agony* or *Curse of the Elements*; second warlocks in raids cover *Recklessness* over Shadow). Players can manually override curse assignments using [`.bot strategy +curse <shadow|weakness|elements|agony|doom|tongues|exhaustion>`](../play/commands/party-actions.md#bot-strategy). Exhaustion snares runners. Curse claims prevent duplicate picks on the main curse rows.
- **Crowd Control:** Casts *Fear* on assigned CC marks (skipping undead, mechanical, or already controlled targets; off by default in dungeons, raids, and real-player groups unless `co +aoe fear`) and *Banish* on Demons and Elementals. Area spells avoid targets near CC marks.

---

## Talent builds

Bots learn pre-set talent orders at 5-level checkpoints (levels 10–60):

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Affliction** | `9.0` | Ranged DPS | `40 / 0 / 11` | Dark Harvest, Shadow Mastery, Nightfall, Siphon Life, Bane -0.5s Shadow Bolt dip. |
| **Demonology** | `9.1` | Ranged DPS / Tanky | `7 / 44 / 0` | Power Overwhelming, Soul Link, Master Demonologist, Instant Corruption dip. |
| **Destruction** | `9.2` | Ranged DPS | `7 / 0 / 44` | Conflagrate, Ruin, Instant Corruption + Improved Life Tap dip. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Affliction (`9.0`):** Early levels build Corruption and drain sustain (*Improved Corruption*, *Improved Life Tap*, *Nightfall*, *Grim Reach*), reaching *Siphon Life* (35). Mid levels take *Shadow Mastery* (40) and *Dark Harvest* (40), then *Suppression*. Late levels dip 11 points into Destruction (*Cataclysm*, *Bane*, *Shadowburn*).
- **Demonology (`9.1`):** Early levels build demon toughness (*Demonic Embrace*, *Fel Domination*, *Unholy Power*), reaching *Power Overwhelming* (30). Mid levels take *Master Demonologist* and *Soul Link* (40). Late levels dip 7 points into Affliction (*Improved Corruption*, *Improved Life Tap*).
- **Destruction (`9.2`):** Early levels build nuke damage (*Cataclysm*, *Bane*, *Shadowburn* at 20, *Devastation*, *Ruin* at 30). Mid levels take *Improved Immolate* and *Conflagrate* (40). Late levels take *Emberstorm* before the 7-point Affliction dip (*Improved Corruption*, *Improved Life Tap*).

</details>

---

## Class settings

- [`AiPlayerbot.WarlockPetBySpec`](../server/settings/class-behaviour.md#aiplayerbotwarlockpetbyspec): Configures warlocks to summon the demon tailored to their active talent spec in PvE content (Affliction/Demonology toward Felhunter, Destruction toward Imp, tankless parties toward Voidwalker).
- [`AiPlayerbot.WarlockRaidRotation`](../server/settings/class-behaviour.md#aiplayerbotwarlockraidrotation): Opt-in raid rotation that keeps DoTs, curses and *Drain Soul* off targets that are about to die (bosses and the raid-utility curses are exempt), caps each raid DoT spread row at two extra targets, and taps for mana only when two *Shadow Bolts* cannot be paid for.
