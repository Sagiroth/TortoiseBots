---
id: classes-druid
title: Druid Bot Guide
category: classes
summary: Complete guide to Druid bot AI, shapeshifting forms, tanking, healing, DPS rotations, and Turtle WoW custom abilities.
tags: [classes, druid, tank, healer, dps, hybrid]
relates_to:
  - classes-overview
---

# Druid Bot Guide

Druids fulfill Tank, Healer, Melee DPS, or Ranged Caster roles using flexible shapeshifting forms and hybrid spell kits.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Feral (Bear)** | Tank | Dire Bear Form threat generation, mitigation, and taunts. |
| **Feral (Cat)** | Melee DPS | Cat Form stealth openers, combo point builders, and bleed finishers. |
| **Restoration** | Healer | Heal-over-time maintenance, direct burst heals, and emergency saves. |
| **Balance** | Ranged DPS | Nature and Arcane ranged nukes with Moonkin Form aura support. |

---

## What the bot does in combat

### Feral (Bear Tank)
- Fights in Dire Bear Form throughout combat, generating threat using *Swipe* and *Demoralizing Roar*.
- Taunts loose mobs using *Growl* and *Challenging Roar*.
- Keeps *Maul* queued every swing from 15 rage instead of waiting for 40, and spends excess rage (or a free *Clearcasting* proc) on *Savage Bite* from level 58 (rotation reference: stevemcqueenz/turtle-knowledge-base-, Turtle WoW class guides).
- Uses *Faerie Fire (Feral)* and *Enrage* on cooldown.
- Saves *Challenging Roar* (10-min cooldown) for real emergencies: 3 or more loose mobs on non-tanks, not for a pack it already holds (rotation reference: stevemcqueenz/turtle-knowledge-base-, Turtle WoW class guides).
- Activates *Frenzied Regeneration* when health drops low.

### Feral (Cat Melee DPS)
- Enters Cat Form and opens from stealth using *Pounce* or *Ravage*.
- Builds combo points using *Claw*, *Rake*, and *Shred*, spending points on *Rip* and *Ferocious Bite*; fires *Tiger's Fury* as a damage buff.
- Drops threat with *Cower* at medium threat in party and raid encounters.
- Fires *Ferocious Bite* early on targets below 25% health with 1 or more combo points (against targets under 4000 health; bosses keep Rip), avoiding clipping active *Rip* bleeds.
- Out of combat or in battlegrounds, prowling cats maintain normal objective movement and routines.
- A prowling druid still ends a pull's join window on time.

### Restoration (Healer)
- Maintains *Rejuvenation* and *Regrowth* HoTs (Heal-over-Time spells) on injured allies.
- Between 20% and 70% health, casts *Regrowth* first and falls through to *Healing Touch* while Regrowth's heal-over-time is still running on the target, or when the druid has not learned Regrowth yet (below level 12).
- Casts direct heals (*Healing Touch*) downranked to match target wounds per [shared downranking rules](overview.md#spell-downranking).
- Chains *Nature's Swiftness* into an instant *Healing Touch* when allies suffer critical damage (held for that emergency, never popped on a quiet tick). Rotation reference: stevemcqueenz/turtle-knowledge-base- (Turtle WoW class guides).
- Channels *Tranquility* only when the group is hurt together (never on two scratches). Rotation reference: stevemcqueenz/turtle-knowledge-base- (Turtle WoW class guides).
- Spends *Omen of Clarity* procs on free *Rejuvenation* casts.
- Supports party damage via the `+offdps` strategy per [healer off-DPS rules](overview.md#healer-off-dps-offdps), shifting out of Tree of Life Form to cast *Moonfire* and *Wrath*.

### Balance (Ranged DPS)
- Maintains *Moonkin Form* and attacks from range with *Moonfire*, *Wrath*, *Starfire*, and *Insect Swarm*.
- Casts *Hurricane* against packs of 3 or more clustered enemies.
- Honors [party threat back-off](overview.md#party-threat-back-off), pausing AoE spells if holding 50% or more of tank threat.

### Leveling (Below Level 10)
- Fights in melee using auto-attacks, keeping *Moonfire* applied and healing self with *Healing Touch* and *Rejuvenation*.
- Closes distance to enemies, using *Wrath* when immobilized.

<details>
<summary>Shapeshifting and form maintenance details</summary>

The bot maintains form based on assigned role:
- **Tank:** Stays in Bear Form or Dire Bear Form.
- **Melee DPS:** Stays in Cat Form.
- **Ranged DPS:** Stays in Moonkin Form (if talented) or Humanoid form.
- **Healer:** Stays in Humanoid Form or Tree of Life Form.

Movement impairs like daze or chill do not break form. Physical roots break form only when the combat target is out of melee reach or line of sight, allowing tanks to hold aggro without wasting mana.

</details>

---

## Turtle WoW custom spells

- **Tree of Life Form (Spell ID 45705):** Restoration form providing spirit-scaling healing power and polymorph immunity at reduced movement speed.
- **Berserk (Spell ID 45708):** Feral boost cooldown granting +20% maximum health in Bear Form.
- **Swiftmend HoT-Gating (Spell ID 18562):** Consumes an active *Rejuvenation* or *Regrowth* HoT for instantaneous healing, verifying HoT existence before casting.

---

## Utility & interrupts

- **Crowd Control:** Casts *Entangling Roots* outdoors to peel melee attackers. Casts *Hibernate* when assigned CC marks (no creature-type filter in the bot; the game validates the target).
- **Interrupts & Stuns:** Casts *Bash* in Bear Form to interrupt spells and stun dangerous casters. Uses *Feral Charge* to gap-close on distant targets.
- **Combat Resurrection:** Casts *Rebirth* on dead party members during combat.
- **Out-of-Combat Resurrection:** Uses *Rebirth* out of combat only when no priest, paladin, or shaman is available to cast a standard resurrection.
- **Innervate:** Casts *Innervate* on the lowest-mana party healer below the low-mana line (or assigned boost target), shifting to caster form first. Feral cats will shift out to feed an empty healer.
- **Barkskin:** Casts *Barkskin* during heavy incoming damage in Balance and Restoration specs; Feral cats and bears skip it to keep their form bonuses.
- **Party Buffs:** Maintains *Mark of the Wild* (and *Gift of the Wild*) and *Thorns* on allies, prioritizing tanks for *Thorns*. Multiple druids coordinate buff claims to prevent duplicate casts.
- **Solo Survival:** Solo druids level 10+ with mana at or above the low-mana line stand their ground and self-heal rather than fleeing when outnumbered.

---

## Talent builds

TortoiseBots provides pre-configured talent progressions at 5-level checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Balance** | `11.0` | Ranged DPS | `38 / 0 / 13` | Moonkin Form, Eclipse, Balance of All Things, Omen of Clarity, Subtlety -20% threat dip. |
| **Feral** | `11.1` | Tank / Melee DPS | `11 / 40 / 0` | Shared Bear/Cat build: Leader of the Pack, Berserk, Carnage, Heart of the Wild, Thick Hide. |
| **Restoration** | `11.2` | Healer | `10 / 0 / 41` | Swiftmend, Nature's Swiftness, Tree of Life Form, Gift of Nature, Preservation. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Balance (`11.0`):**
  - *Levels 10–35:* Takes *Improved Wrath* (5/5), *Improved Moonfire* (2/2), *Natural Weapons* (3/3), *Moonfury* (3/3), *Omen of Clarity* (1/1), *Vengeance* (5/5), and *Moonkin Form* (35).
  - *Levels 35–45:* Takes *Moonglow* (3/3), *Owlkin Frenzy* (3/3), *Balance of All Things* (3/3), and *Eclipse* (45).
  - *Levels 45–60:* Dips into Restoration for *Improved Mark of the Wild* (5/5) and *Subtlety* (5/5) for -20% threat reduction.
- **Feral (`11.1` - Shared Bear & Cat):**
  - *Levels 10–35:* Takes *Ferocity* (5/5), *Feral Instinct* (3/3), *Thick Hide* (3/3), *Feral Charge* (20), *Sharpened Claws* (3/3), *Primal Fury* (2/2), and *Predatory Strikes* (3/3).
  - *Levels 35–50:* Takes *Berserk* (40), *Heart of the Wild* (5/5), *Carnage* (2/2), and *Leader of the Pack* (50).
  - *Levels 50–60:* Dips into Balance for *Natural Weapons* (3/3), *Natural Shapeshifter* (2/3), and *Omen of Clarity* (60).
- **Restoration (`11.2`):**
  - *Levels 10–35:* Takes *Improved Mark of the Wild* (5/5), *Improved Healing Touch* (5/5), *Swiftmend* (20), *Gift of Nature* (5/5), and *Nature's Swiftness* (35).
  - *Levels 35–50:* Takes *Tranquil Spirit* (5/5), *Preservation* (3/3), *Improved Regrowth* (5/5), and *Tree of Life Form* (50).
  - *Levels 50–60:* Dips into Balance for *Improved Wrath* (5/5) and *Sylvan Blessing* (2/2).

</details>

---

## Class settings

There are no class-specific configuration flags for Druids. General combat, healing, and resting thresholds are configured in [Combat settings](../server/settings/combat.md).
