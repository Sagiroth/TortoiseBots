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
- Keeps *Maul* queued every swing from 15 rage instead of waiting for 40, and spends excess rage (50 or more, or a free *Clearcasting* proc) on *Savage Bite* from level 58.
- Uses *Faerie Fire (Feral)* on cooldown and *Enrage* when healthy (at or above 70% health) and rage-starved (below 20 rage).
- Saves *Challenging Roar* for real emergencies: 3 or more loose mobs on non-tanks, not for a pack it already holds.
- Activates *Frenzied Regeneration* when health is low (20–50%; critical health uses direct heals instead).

### Feral (Cat Melee DPS)
- Enters Cat Form and opens from stealth using *Pounce* or *Ravage*.
- Builds combo points using *Claw*, *Rake*, and *Shred*, spending points on *Rip* and *Ferocious Bite*; fires *Tiger's Fury* as a damage buff.
- Drops threat with *Cower* at medium threat (60% or more) in party and raid encounters.
- Fires *Ferocious Bite* early on targets below 25% health with 1 or more combo points (against targets under 4000 health — bosses stay on Rip). The early execute bite ignores *Rip* and outranks it; the timed 5-combo-point bite only fires on a missing or healthy (over 10 seconds left) *Rip*, never clipping it.
- Out of combat or in battlegrounds, prowling cats keep normal objective movement and routines.
- A prowling druid still ends a pull's join window on time.
- Off-heals wounded allies (*Regrowth*, *Healing Touch* by wound band) per [off-healing rules](overview.md#shared-bot-behaviors).

### Restoration (Healer)
- Maintains *Rejuvenation* and *Regrowth* HoTs (Heal-over-Time spells) on injured allies.
- Between 20% and 70% health, casts *Regrowth* first and falls through to *Healing Touch* while Regrowth's heal-over-time is still running on the target, or when the druid has not learned Regrowth yet.
- While that target still needs no direct heal, keeps *Rejuvenation* on it; idles by blanketing *Rejuvenation* on hurt allies missing it (outbidding grenades), and saves raid *Faerie Fire* for bosses while holding 80% mana.
- Casts direct heals (*Healing Touch*) downranked to match target wounds per [shared downranking rules](overview.md#shared-bot-behaviors). Heal-over-time spells never downrank.
- Chains *Nature's Swiftness* into an instant *Healing Touch* when allies suffer critical damage (held for that emergency, never popped on a quiet tick).
- Channels *Tranquility* only when the group is hurt together (never on two scratches).
- Spends *Omen of Clarity* procs on free *Rejuvenation* casts.
- Supports party damage via the `+offdps` strategy per [healer off-DPS rules](overview.md#shared-bot-behaviors), shifting out of Tree of Life Form to cast *Moonfire*, *Wrath*, and *Starfire*.

### Balance (Ranged DPS)
- Maintains *Moonkin Form* (when talented) and attacks from range with *Moonfire*, *Wrath*, *Starfire*, and *Insect Swarm*.
- Casts *Hurricane* against packs of 3 or more clustered enemies.
- When mana falls to 5% or less, walks into melee and auto-attacks in caster or Moonkin form (no Bear or Cat Form needed), saving regenerated mana; returns to ranged casting at 30% mana. A cast in progress finishes first. Turn it off with `co -oom melee` (`co +oom melee` to turn it back on).
- Honors [party threat back-off](overview.md#shared-bot-behaviors), pausing AoE spells if holding 50% or more of tank threat.
- Off-heals wounded allies (*Regrowth*, *Healing Touch* by wound band) per [off-healing rules](overview.md#shared-bot-behaviors).

### Leveling (Below Level 10)
- Fights in melee using auto-attacks, keeping *Moonfire* applied (above 50% mana) and healing self with *Healing Touch* and *Rejuvenation*.
- Closes distance to enemies, using *Wrath* when immobilized.

<details>
<summary>Shapeshifting and form maintenance details</summary>

The bot maintains form based on assigned role:
- **Tank:** Stays in Bear Form or Dire Bear Form.
- **Melee DPS:** Stays in Cat Form.
- **Ranged DPS:** Stays in Moonkin Form (if talented) or Humanoid form.
- **Healer:** Stays in Humanoid Form or Tree of Life Form.

Movement slows like daze or chill do not break form. Physical roots break form only when the combat target is out of melee reach or line of sight, allowing tanks to hold aggro without wasting mana.

</details>

---

## Turtle WoW custom spells

- **Tree of Life Form:** Restoration form providing spirit-scaling healing power and polymorph immunity at reduced movement speed.
- **Berserk:** Feral boost cooldown granting extra maximum health in Bear Form.
- **Swiftmend HoT-Gating:** Consumes an active *Rejuvenation* or *Regrowth* HoT for instantaneous healing, verifying HoT existence before casting.

---

## Utility & interrupts

- **Crowd Control:** Casts *Entangling Roots* to peel melee attackers. Casts *Hibernate* when assigned CC marks (no creature-type filter in the bot; the game validates the target).
- **Interrupts & Stuns:** Casts *Bash* in Bear Form to interrupt spells and stun dangerous casters. Uses *Feral Charge* to gap-close on distant targets.
- **Combat Resurrection:** Casts *Rebirth* on dead party members during combat.
- **Out-of-Combat Resurrection:** Uses *Rebirth* out of combat only when no priest, paladin, or shaman is available to cast a standard resurrection.
- **Innervate:** Casts *Innervate* on the lowest-mana party healer below the low-mana line (15 default, must still be below it even when manually boosted), shifting to caster form first. Feral cats will shift out to feed an empty healer.
- **Barkskin:** Casts *Barkskin* at medium health (50–70%) in Balance and Restoration specs; Feral cats and bears skip it to keep their form bonuses, and it never fires in Tree of Life Form.
- **Party Buffs:** Maintains *Mark of the Wild* (and *Gift of the Wild*) and *Thorns* on allies, prioritizing tanks for *Thorns*. Multiple druids coordinate buff claims to prevent duplicate casts.
- **Solo Survival:** Solo druids level 10+ with mana at or above the low-mana line stand their ground and self-heal rather than fleeing when outnumbered.

---

## Talent builds

Bots learn pre-set talent orders at 5-level checkpoints (levels 10–60):

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Balance** | `11.0` | Ranged DPS | `38 / 0 / 13` | Moonkin Form, Eclipse, Balance of All Things, Omen of Clarity, Subtlety dip. |
| **Feral** | `11.1` | Tank / Melee DPS | `11 / 40 / 0` | Shared Bear/Cat build: Leader of the Pack, Berserk, Heart of the Wild, Thick Hide. |
| **Restoration** | `11.2` | Healer | `10 / 0 / 41` | Swiftmend, Nature's Swiftness, Tree of Life Form, Gift of Nature, Preservation. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Balance (`11.0`):** Early levels build Wrath and Moonfire damage (*Improved Wrath*, *Moonfury*, *Omen of Clarity*), reaching *Vengeance* and *Moonkin Form* (35). Mid levels take *Moonglow*, *Balance of All Things*, and *Eclipse* (45). Late levels dip into Restoration for cheaper marks and less threat.
- **Feral (`11.1`, shared Bear & Cat):** Early levels build hit and armor (*Ferocity*, *Feral Instinct*, *Thick Hide*, *Feral Charge* at 20, *Sharpened Claws*, *Predatory Strikes*). Mid levels take *Berserk* (40), *Heart of the Wild*, and *Leader of the Pack* (50). Late levels dip into Balance for shapeshifting cost and *Omen of Clarity*.
- **Restoration (`11.2`):** Early levels build heal efficiency (*Improved Healing Touch*, *Swiftmend* at 20, *Gift of Nature*), reaching *Nature's Swiftness* (35). Mid levels take *Tranquil Spirit*, *Preservation*, *Improved Regrowth*, and *Tree of Life Form* (50). Late levels dip into Balance for Wrath support.

</details>

---

## Class settings

There are no class-specific combat-behavior flags for Druids. General combat, healing, and resting thresholds are configured in [Combat settings](../server/settings/combat.md).
