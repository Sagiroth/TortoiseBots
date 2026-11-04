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

Hunters deliver consistent ranged physical damage, pet management, crowd control via traps, and melee-weaving Survival combat.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Beast Mastery** | Ranged DPS | Pet empowerment, high physical damage, and enrage burst. |
| **Marksmanship** | Ranged DPS | Heavy ranged shot rotations, party attack power, and *Trueshot Aura*. |
| **Survival** | Ranged DPS | Front-line melee strikes, front-cone cleaves, and trap mastery (keeps the full ranged shot kit and weaves melee). |

---

## What the bot does in combat

### Beast Mastery & Marksmanship (Ranged DPS)
- Opens with *Hunter's Mark* and *Auto Shot*. With a living tank in the group the mark goes only on the mob the tank is fighting.
- Fires *Aimed Shot* (Marksmanship, once trained, within range) or *Arcane Shot* on cooldown.
- Uses *Steady Shot* as a filler timed to fit inside the Auto Shot swing window.
- Uses *Multi-Shot* only when the splash around the target catches a second fighting enemy, and never into a crowd-controlled mob standing beside it.
- Channels *Volley* against grouped mobs, cancelling the channel if fewer than 2 mobs remain in the area or a mob under the party's crowd control stands in it.
- Multi-Shot and Volley respect [party threat back-off](overview.md#shared-bot-behaviors) when holding 50% or more of the tank's threat.
- Keeps *Serpent Sting* ticking on high-health targets (skipping targets near death).
- Replaces Serpent Sting with *Viper Sting* when the hunter's mana is below 50% and the target has mana (10% or more); manual `sting viper` or `sting serpent` orders override this gate.
- Beast Mastery activates *Bestial Wrath* and *Kill Command* on cooldown. *Rapid Fire* fires for all specs on cooldown.
- Switches between *Aspect of the Hawk* for damage and *Aspect of the Viper* when low on mana.
- Threat handling (Feign Death, Disengage, multi-mob kiting): see [Threat drops](#threat-drops).

### Survival (Melee / Ranged Hybrid)
- Fights at range with the full shot kit and weaves melee strikes when enemies close in.
- Delivers front-cone melee cleave using *Carve* on AoE packs.
- Stacks bleed damage using *Lacerate* (manual action, only cast by player order).
- Uses *Raptor Strike* and *Mongoose Bite* on cooldown without requiring a dodge proc.
- Employs *Counterattack* after parrying incoming strikes (parry legality is enforced by the core).

### Dead-Zone & Melee Weaving
- If enemies close inside Auto Shot minimum range, ranged hunters trade into melee using *Wing Clip*, *Raptor Strike*, and *Mongoose Bite*, then step back out.
- The bot switches back to ranged attacks once the mob is pulled off the hunter by the tank or pet, or is rooted — not on distance alone.

### Threat drops
- When the mob the hunter is fighting is on the hunter itself (or about to be) and stands in melee reach, and a living tank is in the group, the hunter casts *Disengage* to drop off the mob's threat list so the tank can take it back. *Disengage* does not move the hunter and is not used as an escape.
- Drops threat more heavily with *Feign Death* upon reaching medium threat. The bot holds back Feign Death if the attacking mob has only the hunter on its threat list to avoid causing scripted encounters to evade.
- When two or more mobs are on the hunter itself, it stops trading: *Wing Clip* on the nearest attacker and a step back out to shooting range, then shoots the mobs chasing it (repeating until the tank takes them back or they die).

<details>
<summary>Pet management and lifecycle details</summary>

- **Lifecycle:** Hunters obtain their pet at level 10; pool hunters logging in below level 10 give up any previously seeded pet, while player-owned, hired, or adopted hunters keep theirs.
- Automatically summons, feeds, and revives pets out of combat.
- Sends pets to attack the primary target, holding them back for about two seconds after the group enters combat so a grouped tank can build a lead first (solo hunters and tankless groups send the pet in at once).
- Casts *Mend Pet* below 40% in combat (channeled, below rotation shots in priority) and below 70% out of combat.
- Controls pets in Defensive stance. Solo hunters keep *Growl* on autocast; when grouped with a tank, *Growl* is automatically disabled and the pet uses *Cower* on threat spikes — as soon as the pet's threat on the target passes the tank's, before the mob turns on the pet.
- When an enemy is crowd-controlled, pets are strictly prevented from attacking the controlled target (except a skull-marked focus target).

</details>

---

## Turtle WoW custom spells

- **Aspect of the Viper:** Custom aspect converting physical damage into mana regeneration while reducing total damage output. Automatically triggered when hunter mana falls below 20%, swapping back to Hawk once mana recovers above 60%.
- **Carve:** Front-cone melee strike for Survival hunters hitting multiple targets.
- **Lacerate:** Stacking melee bleed attack for sustained melee damage.

---

## Utility & interrupts

- **Enrage Dispel:** Dispels enrage with *Tranquilizing Shot* via the dispel-enrage trigger.
- **Crowd Control:**
  - Deploys *Freezing Trap* on assigned crowd control marks; without a mark it traps a loose extra attacker on its own — a mob nobody in the group is fighting yet, not dotted and not already held by another crowd control — and walks into reach to lay the trap.
  - Casts *Scare Beast* on hostile beasts.
- **Combat Traps:**
  - Lays *Immolation Trap*, *Frost Trap*, or *Explosive Trap* via *Feign Death*, avoiding melee suicide walks.
- **Snares:** Uses *Concussive Shot* and *Wing Clip* to slow fleeing or pursuing enemies.

---

## Talent builds

Bots learn pre-set talent orders at 5-level checkpoints (levels 10–60):

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Beast Mastery** | `3.0` | Ranged DPS | `45 / 6 / 0` | Bestial Wrath, Frenzy, Kill Command, Unleashed Fury, Scent of Blood. |
| **Marksmanship** | `3.1` | Ranged DPS | `9 / 42 / 0` | Aimed Shot, Mortal Shots, Lock and Load, Experimental Ammunition, Barrage. |
| **Survival** | `3.2` | Ranged DPS | `8 / 0 / 43` | Carve, Lacerate, Trap Mastery, Savage Strikes, pet-support dip. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Beast Mastery (`3.0`):** Early levels build pet damage and sustain (*Endurance Training*, *Thick Hide*, *Unleashed Fury*, *Ferocity*), picking up *Intimidation* (30), *Bestial Wrath* (40), *Spirit Bond*, *Frenzy*, and *Kill Command* (50), with a small Marksmanship dip.
- **Marksmanship (`3.1`):** Early levels build shot damage (*Efficiency*, *Lethal Shots*, *Aimed Shot* at 20, *Mortal Shots* at 30), then *Barrage*, *Experimental Ammunition*, *Ranged Weapon Specialization*, and *Lock and Load* (50), with a small Beast Mastery dip for pet support.
- **Survival (`3.2`):** Early levels build melee and trap strength (*Savage Strikes*, *Carve* at 35, *Trap Mastery*, *Lacerate* at 50), then *Lightning Reflexes* and *Untamed Trapper* (60), with a small Beast Mastery dip for pet support.

</details>

---

## Class settings

- [`AiPlayerbot.HunterViperOnLowMana`](../server/settings/class-behaviour.md#aiplayerbothunterviperonlowmana): Controls whether hunters swap to Aspect of the Viper when low on mana.
