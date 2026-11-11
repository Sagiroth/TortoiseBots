---
id: classes-mage
title: Mage Bot Guide
category: classes
summary: Complete guide to Mage bot AI, elemental rotations, Polymorph CC, Counterspell interrupts, conjured refreshments, and Arcane Power safety.
tags: [classes, mage, dps, ranged, cc]
relates_to:
  - classes-overview
---

# Mage Bot Guide

Mages provide high-burst ranged spell DPS, dependable crowd control (*Polymorph*), school-locking interrupts (*Counterspell*), and party refreshments.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Frost** | Ranged DPS | Crowd control, survivability, and shattered critical strikes. |
| **Fire** | Ranged DPS | Massive spell burst, periodic Ignite burns, and instant Pyroblasts. |
| **Arcane** | Ranged DPS | High single-target burst, mana efficiency, and instant Presence of Mind casts. |

---

## What the bot does in combat

### Frost Mage
- Defaults to *Frostbolt* in combat; pulls open with Shoot (wand).
- If enemies close within 8 yards and attack the mage, turns away and casts *Blink* to reset distance.
- Roots attackers with *Frost Nova* — only when a mob within reach is attacking the mage itself, skipping targets already frozen or immune.
- Casts *Arcane Explosion* when 3 or more enemies cluster inside 10 yards and none of them is loose on the party.
- Casts *Flamestrike* on 3 or more grouped enemies, layering channeled *Blizzard* over the burning zone (6-second window). A Frost mage withholds *Flamestrike* for the first 10 seconds of a fight, the same opener gate *Blizzard* has, so it does not open a fresh pack before the tank holds it.
- Weaves *Cone of Cold* against 2 or more enemies at ranged scan, cast only inside 10 yards while facing the target.
- Activates *Cold Snap* when any Frost cooldown (*Frost Nova*, *Cone of Cold*, *Ice Barrier*, *Ice Block*, *Frost Ward*) is spent.
- Area spells (*Blizzard*, *Flamestrike*, *Cone of Cold*) pause during [party threat back-off](overview.md#shared-bot-behaviors) if holding 50% or more tank threat.
- Casts *Flamestrike* on 3 or more grouped enemies, layering channeled *Blizzard* over the burning zone (6-second window); a running *Blizzard* is cancelled when a mob under the party's crowd control stands in it.

### Fire Mage
- Casts *Fireball* as primary filler and weaves *Scorch* to maintain Fire Vulnerability debuffs (refreshing before the 7-second expiry).
- Rushes a fast (about 1 second) *Pyroblast* upon reaching full *Hot Streak* stacks.
- Consumes *Presence of Mind* with instant *Pyroblast*.
- Uses *Fire Blast* as an instant finishing blow below 20% target health.
- Casts *Arcane Explosion* when 3 or more enemies cluster inside 10 yards and none of them is loose on the party.
- Casts *Blast Wave* as point-blank area damage at interrupt priority on any enemy within 10 yards (no school-lock interrupt or daze logic).
- Automatically falls back to *Frostbolt* when facing fire-immune enemies (via the generic school-immunity veto).

### Arcane Mage
- Opens with *Arcane Rupture*, then channels *Arcane Missiles* while the empowerment buff holds.
- Uses *Presence of Mind* for instant burst spells.
- Casts *Arcane Explosion* at point-blank range when enemies close in (no density or crowd-control gate, unlike Frost and Fire).

<details>
<summary>Wand and mana recovery details</summary>

- When the primary filler is out of mana reach, the bot fires cheaper *Frostbolt* before falling back to the wand.
- On Turtle WoW, wand swings trigger the global cooldown. The bot uses a dedicated `wand blocks nuke` trigger to stop wanding as soon as filler spells are affordable again — keeping the wand only while out of mana or threat-vetoed.
- While out of mana (below 5%), or under high threat in raids, the bot relies on *Shoot* to keep damage safely. *Shoot* is threat-free, so it keeps running under single-target threat pressure where nukes pause.
- Wand swings immediately stand down when a *Polymorph* target is queued for crowd control.

</details>

---

## Turtle WoW custom spells

- **Arcane Power Safety:** The bot enforces a strict 70% mana floor and requires a live hostile target before activating, and never cancels the buff once active.
- **Evocation Channeling:** Arms below 40% mana, channels *Evocation* when out of spell reach, and cancels the channel at 95% mana to resume combat.
- **Icicles:** Custom Frost talent providing root and shatter synergy without disrupting primary spell rotations. The bot only casts it above 30% target health, at 60% or more own health, with no attackers on it.

---

## Utility & interrupts

- **Polymorph CC:** Casts *Polymorph* on targets assigned via `.bot action cc <mark>`. Stands still during casting and prevents AoE spells near sheeped mobs.
- **Interrupts:** Casts *Counterspell* immediately upon detecting dangerous enemy spell casts, locking the school.
- **Food & Drink Conjuration:** Conjures food and water out of combat, sharing stacks with party members in need. On every trade it conjures a fresh full stack on the spot: food for everyone, water for mana users, at the best rank the mage knows that the trader's level can use. Free and unlimited, whoever opened the window.
- **Food & Drink Conjuration:** Conjures food and water out of combat, sharing stacks with party members in need.
- **Portals:** Whisper `portal` to list the cities the mage knows, `portal <city>` to cast it (unique prefix works); `.bot service <Name>` reports the list for the addon and `.bot service <Name> portal <spellId>` casts it. Out of combat only, rune is free. Only the mage's group can use the portal, so invite the bot first.
- **Portal Follow:** After you step through a portal this mage cast, the party bots that follow you step through it too, this mage last.
- **Mana Gems & Runes:** Consumes the strongest mana gem held (Ruby down to Agate) below 40% mana in combat, re-conjuring gems between pulls. Uses *Dark Rune* below 40% mana when no ready gem is held (requires level 50).
- **Party Buffs:** Maintains *Arcane Intellect* (and *Arcane Brilliance*), refreshing expiring buffs (under 15 seconds) and coordinating buff claims with other mages.
- **Defensive Wards:** Casts *Mana Shield* at low health (also requires mana above medium) and *Ice Block* at critical health.
- **Decurse:** Casts *Remove Lesser Curse* on party members afflicted by debilitating curses.

---

## Talent builds

Bots learn pre-set talent orders at 5-level checkpoints (levels 10–60):

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Arcane** | `8.0` | Ranged DPS | `40 / 0 / 11` | Arcane Power, Presence of Mind, Temporal Convergence, Frost hit dip. |
| **Fire** | `8.1` | Ranged DPS | `10 / 41 / 0` | Hot Streak, Combustion, Master of Elements, Clearcasting + threat dip. |
| **Frost** | `8.2` | Ranged DPS | `10 / 0 / 41` | Shatter, Ice Block, Ice Barrier, Icicles, Flash Freeze, Clearcasting dip. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Arcane (`8.0`):** Early levels build missile damage and clearcasting (*Improved Arcane Missiles*, *Arcane Concentration*), reaching *Presence of Mind* (30) and *Arcane Power* (40). Late levels dip into Frost for hit (*Improved Frostbolt*, *Elemental Precision*).
- **Fire (`8.1`):** Early levels build fireball damage and crit (*Improved Fireball*, *Ignite*, *Pyroblast* at 20, *Master of Elements*, *Hot Streak* at 30). Mid levels take *Fire Power* and *Combustion* (40). Late levels take *Burning Soul* plus an Arcane dip for clearcasting.
- **Frost (`8.2`):** Early levels build frostbolt damage and crit (*Improved Frostbolt*, *Elemental Precision*, *Ice Shards*, *Cold Snap*, *Shatter* at 30). Mid levels take *Ice Block* (35), *Ice Barrier* (40), and *Icicles*. Late levels take *Flash Freeze* plus an Arcane dip for clearcasting.

</details>

---

## Class settings

There are no class-specific configuration flags for Mages. General combat and mana thresholds are configured in [Combat settings](../server/settings/combat.md).
