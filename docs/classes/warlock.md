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
- **Demonology (Pet DPS / Tanky):** Heavy pet empowerment, *Soul Link*, *Demonic Sacrifice*, and high durability. Keeps *Immolate* up like the other specs.
- **Destruction (Burst DPS):** Fire burst nuke specialist utilizing *Shadow Bolt*, *Immolate*, and *Conflagrate* (no *Searing Pain* wiring in the Destruction strategies).

---

## Combat Rotations & Priorities

### 1. DoT Upkeep & Shard Economy
- **Curses:** Defaults to *Curse of Agony*; other curses only when you enable them manually.
- **DoTs:** Only *Corruption* is health-gated (skipped at/below 20% target health); *Immolate* has no gate and is kept up by all three specs. Affliction also maintains *Siphon Life*. *Immolate* spreads to extra attackers (aoe strategy, plus Demonology/Destruction at spec level) under the 16-debuff cap.
- **DoTs:** Only *Corruption* is health-gated (skipped at/below 20% target health); *Immolate* has no gate and is kept up by all three specs. Affliction also maintains *Siphon Life*. Affliction spreads *Corruption* and *Siphon Life* to extra attackers, Demonology spreads *Corruption*, at spec level even with the aoe strategy off (its rows still win when it is on); spreading runs below single-target upkeep and mana recovery, under the 16-debuff cap.
- **Curses:** Defaults to *Curse of Agony*; other curses only when you enable them manually (`.bot strategy +curse <shadow|weakness|...|exhaustion>` still overrides). *Curse of Exhaustion* snares runners and kiters via the snare target.
- **DoTs:** Only *Corruption* is health-gated (skipped at/below 20% target health); *Immolate* has no gate and is kept up by all three specs. Affliction also maintains *Siphon Life*.
- **Soul Shard Harvest:** Shards are never seeded or conjured — the bot harvests them organically with *Drain Soul* when the target is at/below 20% health, provided it holds fewer than 5 Soul Shards and has bag space; no elite check. Excess above 5 is destroyed out of combat. Healthstones (*Create Healthstone*, level 10+) and Soulstones (*Create Soulstone (Minor)*, level 18+) are created out of combat whenever the bot holds a shard and lacks one.

### 2. Mana Management: Life Tap
- Affliction also casts *Dark Pact* on low mana, so *Life Tap* is not the only mana tool.
- Warlocks dynamically cast *Life Tap* to convert surplus health into mana.
- Safety check: *Life Tap* fires when mana is at/below the medium-mana line (default 40) and health exceeds the low-health line (default 50); tapping early keeps dots rolling instead of wanding the second half of the fight. No incoming-damage check exists.

---

## Turtle WoW 1.18.1 Custom Content

- **Dark Harvest (Spell ID 52550, per game data):**
  - Custom Turtle WoW Affliction talent requiring 2+ active own DoTs on the target.
  - Deals rapid Shadow damage with a 30-second cooldown that is automatically refunded if the target dies while afflicted, per game data.
- **Power Overwhelming (Spell ID 51714, per game data):**
  - Custom Demonology pet burst (crowd-control break plus damage buff) costing the demon a share of base health over the duration, per game data. The bot requires a live pet above 60% health and a live enemy.
- **Rain of Fire Channeling:**
  - Includes safe channel cancellation when fewer than 2 enemies remain in the AoE.

---

## Demon Summons & Utility

- **Pet Selection:**
  - *Voidwalker:* The PvE/dungeon default (higher-priority summon, also the non-combat default); off-tanks and uses *Sacrifice* for emergency shields. A solo pool bot that learns *Summon Voidwalker* (level 10) and holds a Soul Shard upgrades its Imp to a Voidwalker on its own (`runtime/WarlockPetPolicy.h`); below that it keeps the Imp, a shardless bot stays quiet (no failed summon queued), and a bot whose player picked a demon manually is never overridden.
  - *Imp:* The raid pet, providing *Blood Pact* (Stamina buff).
  - *Succubus:* Provides humanoid crowd control via *Seduce*.
  - *Felhunter:* Uses *Spell Lock* for ranged interrupts, and *Devour Magic* both ways: strips a magic buff off the current target (purge) and lifts magic off a party member (cleanse). Both fire only while a Felhunter is out; other demons idle those nodes safely.
- **Out-of-combat upkeep:** The bot maintains *Demon Armor* (with *Demon Skin* as fallback) and casts *Unending Breath* on itself and the party.
  - *Felhunter:* Uses *Spell Lock* for ranged interrupts. *Devour Magic* has no registered action wiring, so the bot never casts it.
- **Out-of-combat upkeep:** The bot maintains *Demon Armor* (with *Demon Skin* as fallback) and casts *Unending Breath* (5697) on itself and the party, but only while swimming — no buff churn on land.
  - **Pet upkeep:** the demon's known spells are swept for autocast (non-passive, except *Spell Lock* / *Devour Magic* deliberate-cast abilities) and pet plus guardians are pinned to *Defensive* after each summon — same autonomous pair as hunter pets. Explicit `.bot pet aggressive|defensive|passive` orders still apply immediately.
  - **Pet upkeep:** the demon's known spells are swept for autocast (non-passive, except *Spell Lock* / *Devour Magic* deliberate-cast abilities) and pet plus guardians are pinned to *Defensive* after each summon — same autonomous pair as hunter pets. Explicit `.bot pet aggressive|defensive|passive` orders still apply immediately. Mid-fight the bot channels *Health Funnel* into a demon below half health while its own health stays above 60% — never a suicide channel, combat only.
  - **Pet ranks:** the demon learns the highest rank its level allows on summon and on the periodic initialize-pet tick (Torment, Firebolt, Lash of Pain...; *Sacrifice* and *Seduction* stay manual, never autocast), replacing lower ranks automatically — pool bots never read Grimoires, so without this they would cast rank 1 forever.
- **Weapon stones:** Affliction and Demonology create (one shard each) and apply *Spellstones* to the weapon (never over an oiled weapon), Destruction creates and equips *Firestones* — vanilla off-hand semantics, only into an empty off-hand beside a one-handed weapon, never displacing real gear or fighting a staff. Each spec only conjures its own stone.
- **Healthstones & Soulstones:**
  - Creates and uses *Healthstones* during combat.
  - Creates and stores Soulstones on the party healer or tank whenever an in-range healer/tank lacks one (not timed to boss pulls).
- **Fel Domination (Demonology combat recovery):**
  - Burns the 5-minute *Fel Domination* cooldown only in combat with a dead pet, then immediately re-summons (Voidwalker fallback); never wasted out of combat where the free summon applies.
- **Crowd Control:**
  - Casts *Fear* only on its assigned raid CC mark, and never on a mob already under another CC (a feared mob pulls adds). There is no automatic Fear on unmarked mobs. Skips undead and mechanical marks (fear never lands on them).
  - *Howl of Terror* (AoE fear) is PvP-only; it is never cast in PvE groups, and never inside a dungeon or raid (a feared mob pulls neighbouring packs).
  - Casts *Banish* on Demons and Elementals (other marks are skipped: banish only lands on those two types).
  - *Seduce* (Succubus) is a manual pet ability, not a bot CC executor: no CC-flagged seduction action exists, so `.bot action cc` never assigns it.

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
  - *Levels 10–35:* Fire/Shadow nuke path: *Cataclysm* (5/5 cost reduction), *Bane* (5/5 cast time reduction), *Shadowburn* (20 instant soul shard burst), *Devastation* (5/5 +5% crit), and *Ruin* (30 +100% crit damage bonus), per game data.
  - *Levels 35–45:* *Improved Immolate* (5/5) and *Conflagrate* (40 instant burst consuming Immolate), per game data.
  - *Levels 45–60:* Per the preset order, takes *Emberstorm* and *Demonic Swiftness* **before** the 7-point Affliction dip (*Improved Corruption* 5/5 instant cast + *Improved Life Tap* 2/2), then finishes deeper in Destruction. Avoids threat-increasing talents (*Improved Searing Pain*), per game data.

