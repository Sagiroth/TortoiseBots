---
id: classes-hunter
title: Hunter Bot Guide
category: classes
summary: Complete guide to Hunter bot AI, pet management, ranged rotations, melee Survival, and Aspect of the Viper logic.
tags: [classes, hunter, dps, ranged, pet, melee]
relates_to:
  - classes-overview
---

# Hunter Bot Guide

Hunters deliver consistent ranged physical damage, pet management, crowd control via traps, and dedicated melee Survival combat.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Beast Mastery** | Ranged DPS | Pet empowerment, high physical damage, and enrage burst. |
| **Marksmanship** | Ranged DPS | Heavy ranged shot rotations, party attack power, and *Trueshot Aura*. |
| **Survival** | Melee / Ranged Hybrid | Front-line melee strikes, front-cone cleaves, and trap mastery (ranged-first hybrid; keeps the ranged shot kit). |

---

## What the bot does in combat

### Beast Mastery & Marksmanship (Ranged DPS)
- Maintains maximum ranged distance from hostiles, opening with *Hunter's Mark* and *Auto Shot*. With a living tank in the group the mark goes only on the mob the tank is fighting.
- Fires *Aimed Shot* (MM) or *Arcane Shot* on cooldown.
- Uses *Steady Shot* as a filler timed cast fitting directly into the Auto Shot swing window.
- Uses *Multi-Shot* only when the splash around the target catches a second fighting enemy, and never into a crowd-controlled mob standing beside it.
- Channels *Volley* against grouped mobs, cancelling the channel if fewer than 2 mobs remain in the area or a mob under the party's crowd control stands in it.
- Multi-Shot and Volley respect [party threat back-off](overview.md#party-threat-back-off) when holding ≥50% of the tank's threat.
- Keeps *Serpent Sting* ticking on high-health targets (skipping targets near death).
- Replaces Serpent Sting with *Viper Sting* when the hunter's mana is below 50% and the target has mana (≥10%); manual `sting viper` or `sting serpent` commands override this gate.
- BM activates *Bestial Wrath* and *Kill Command* on cooldown. MM triggers *Rapid Fire*.
- Switches between *Aspect of the Hawk* for damage and *Aspect of the Viper* when low on mana.
- Threat handling (Feign Death, Disengage, multi-mob kiting): see [Threat drops](#threat-drops).

### Survival (Melee / Ranged Hybrid)
- Fights primarily in melee reach while keeping full access to the ranged shot kit.
- Delivers front-cone melee cleave using *Carve* (Spell ID 51575) on AoE packs.
- Stacks bleed damage using *Lacerate* (manual action, Spell ID 48049).
- Uses *Raptor Strike* and *Mongoose Bite* on cooldown without requiring a dodge proc.
- Employs *Counterattack* after parrying incoming strikes.

### Dead-Zone & Melee Weaving
- If enemies close inside Auto Shot minimum range (DBC-driven), ranged hunters trade into melee using *Wing Clip*, *Raptor Strike*, and *Mongoose Bite*, then step back out.
- When space opens or the mob is pulled off the hunter by the tank or pet (or is rooted), the bot switches back to ranged attacks.

### Threat drops
- When the mob the hunter is fighting is on the hunter itself (or about to be) and stands in melee reach, and a living tank is in the group, the hunter casts *Disengage* to drop off the mob's threat list so the tank can take it back. *Disengage* does not move the hunter and is not used as an escape.
- Drops threat more heavily with *Feign Death* upon reaching medium threat. The bot holds back Feign Death if the attacking mob has only the hunter on its threat list to avoid causing scripted encounters to evade.
- When two or more mobs are on the hunter itself, it stops trading: *Wing Clip* on the nearest attacker and a step back out to shooting range, then shoots the mobs chasing it (repeating until the tank takes them back or they die).

<details>
<summary>Pet management and lifecycle details</summary>

- **Lifecycle:** Hunters obtain their pet at level 10; pool hunters logging in below level 10 give up any previously seeded pet, while player-owned, hired, or adopted hunters retain theirs.
- Automatically summons, feeds, and revives pets out of combat.
- Sends pets to attack the primary target, holding them back for about two seconds after the group enters combat so a grouped tank can build a lead first (bot-led parties included; solo hunters and tankless groups send the pet in at once).
- Casts *Mend Pet* below 40% in combat (channeled, when no rotation shot is ready) and below 70% out of combat.
- Controls pets in Defensive stance. Solo hunters keep *Growl* on autocast; when grouped with a tank, *Growl* is automatically disabled and the pet uses *Cower* on threat spikes — as soon as the pet's threat on the target passes the tank's, before the mob turns on the pet.
- When an enemy is crowd-controlled, pets are strictly prevented from attacking the controlled target.

</details>

---

## Turtle WoW custom spells

- **Aspect of the Viper (Spell ID 45651):**
  - Custom aspect converting physical damage into mana regeneration while reducing total damage output.
  - Automatically triggered when hunter mana falls below threshold, swapping back to Hawk once mana recovers.
- **Carve (Spell ID 51575):** Front-cone melee strike for Survival hunters hitting multiple targets.
- **Lacerate (Spell ID 48049):** Stacking melee bleed attack for sustained melee damage.

---

## Utility & interrupts

- **Enrage Dispel:** Dispels enrage with *Tranquilizing Shot* via the dispel-enrage trigger.
- **Crowd Control:**
  - Deploys *Freezing Trap* on assigned crowd control marks; without a mark (a dungeon run with none set), it traps a loose extra attacker on its own — a mob nobody in the group is fighting yet, not dotted and not already held by another crowd control — and lays the trap where the mob will walk into it.
  - Casts *Scare Beast* on hostile beasts, withholding area attacks until the target is controlled.
- **Combat Traps:**
  - Lays *Immolation Trap*, *Frost Trap*, or *Explosive Trap* via *Feign Death*, avoiding melee suicide walks.
- **Snares:** Uses *Concussive Shot* and *Wing Clip* to slow fleeing or pursuing enemies.
- **Low Ammo Warning:** Grouped hunters out of combat with under 100 rounds remaining notify the group in local chat.

---

## Talent builds

TortoiseBots provides pre-configured talent progressions at 5-level checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Beast Mastery** | `3.0` | Ranged DPS | `45 / 6 / 0` | Bestial Wrath, Frenzy, Kill Command, Unleashed Fury, Scent of Blood. |
| **Marksmanship** | `3.1` | Ranged DPS | `9 / 42 / 0` | Aimed Shot, Mortal Shots, Lock and Load, Experimental Ammunition, Barrage. |
| **Survival** | `3.2` | Melee DPS | `8 / 0 / 43` | Carve, Lacerate, Untamed Trapper, Savage Strikes, Trap Mastery. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Beast Mastery (`3.0`):**
  - *Levels 10–35:* Takes *Swift Aspects* (5/5), *Endurance Training* (5/5), *Thick Hide* (3/3), *Unleashed Fury* (5/5), *Ferocity* (5/5), and *Intimidation* (30).
  - *Levels 35–45:* Takes *Scent of Blood* (3/3), *Bestial Wrath* (40), *Frenzy* (5/5), and *Spirit Bond* (2/2).
  - *Levels 45–60:* Takes *Kill Command* (50) and 6 points in Marksmanship (*Efficiency* 5/5 + *Improved Stings* 1/5).
- **Marksmanship (`3.1`):**
  - *Levels 10–30:* Takes *Efficiency* (5/5), *Lethal Shots* (5/5), *Aimed Shot* (20), *Swiftshot* (3/3), and *Mortal Shots* (30).
  - *Levels 30–50:* Takes *Barrage* (3/3), *Experimental Ammunition* (40), *Ranged Weapon Specialization* (5/5), and *Lock and Load* (50).
  - *Levels 50–60:* Takes remaining Marksmanship talents and 9 points in Beast Mastery (*Swift Aspects* 5/5 + pet survival).
- **Survival (`3.2`):**
  - *Levels 10–35:* Takes *Improved Slaying* (3/3), *Resourcefulness* (5/5), *Savage Strikes* (2/2), *Planning Ahead* (2/2), and *Carve* (35).
  - *Levels 35–50:* Takes *Surefooted* (3/3), *Killer Instinct* (3/3), *Trap Mastery* (3/3), and *Lacerate* (50).
  - *Levels 50–60:* Takes *Lightning Reflexes* (5/5), *Untamed Trapper* (60), and 8 Beast Mastery points.

</details>

---

## Class settings

- [`AiPlayerbot.HunterViperOnLowMana`](../server/settings/class-behaviour.md#aiplayerbothunterviperonlowmana): Controls whether hunters swap to Aspect of the Viper when low on mana.
