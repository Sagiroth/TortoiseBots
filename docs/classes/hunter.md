---
id: class-hunter
title: Hunter Bot AI & Specs
category: classes
summary: Guide to Hunter bot ranged and melee combat, pet care, Aspect of the Viper mana recovery, and custom Turtle Survival skills.
tags: [class, hunter, dps, ranged, pet, melee]
relates_to:
  - class-overview
  - guide-player-controls
---

# Hunter Bot AI & Specs

Hunters excel at sustained single-target ranged DPS, pet off-tanking, snares, and crowd control. In Turtle WoW 1.18.1, Hunters also possess a fully supported melee Survival combat style.

## Supported Specs & Roles

- **Beast Mastery (Ranged DPS):** Focuses on pet empowerment, *Bestial Wrath*, and high sustained ranged output.
- **Marksmanship (Ranged DPS):** Heavy physical burst damage centered on *Aimed Shot*, *Multi-Shot*, and *Trueshot Aura*.
- **Survival (Melee / Ranged Hybrid):** Turtle WoW custom melee combat utilizing polearms/axes, *Carve*, *Lacerate*, and traps.

---

## Combat Rotations & Priorities

### 1. Ranged Combat (Beast Mastery & Marksmanship)
1. **Opener:** Casts *Hunter's Mark* on the primary target, orders pet to attack.
2. **Shot Priority:**
   - *Aimed Shot* / *Arcane Shot* on cooldown.
   - *Multi-Shot* when AoE is permitted and multiple enemies are engaged.
   - Keeps *Serpent Sting* ticking on high-health targets (skips on low-health mobs to conserve mana).
3. **Dead-Zone Handling:** If an enemy closes into the 8-yard minimum range, the bot smoothly executes *Disengage*, *Wing Clip*, or transitions into melee until distance is recovered.

### 2. Melee Survival Combat
- Closes into melee range with two-handed polearms or dual weapons.
- Casts *Carve* for front-cone cleave and *Lacerate* for stacking bleed damage.
- Applies *Wing Clip* and *Mongoose Bite* reactive counter-attacks.

---

## Turtle WoW 1.18.1 Custom Content

- **Aspect of the Viper (Spell ID 45651):**
  - Custom Turtle WoW aspect that regenerates mana on ranged attacks while reducing damage output.
  - The bot automatically toggles *Aspect of the Viper* when mana falls below 25%, and switches back to *Aspect of the Hawk* or *Aspect of the Monkey* once mana recovers above 80%.
- **Carve (Spell ID 51575):**
  - Custom Turtle WoW instant melee weapon attack hitting up to 3 nearby enemies. Integrated into Survival melee DPS and AoE packs.
- **Lacerate (Spell ID 48049):**
  - Custom Turtle WoW melee bleed ability providing sustained physical damage.

---

## Pet Management & Utility

- **Pet Lifecycle:**
  - Automatically summons pet out of combat.
  - Revives dead pets using *Revive Pet* and heals injured pets during combat via *Mend Pet*.
  - Feeds pet appropriate diet foods from inventory to maintain "Happy" loyalty status.
- **Pet Safety & CC Discipline:**
  - When a target is crowd-controlled (e.g. *Polymorph* or *Freezing Trap*), the bot's pet is prevented from attacking the CC'd mob, preventing accidental breaks.
- **Crowd Control:**
  - Deploys *Freezing Trap* when assigned CC via `.bot action cc <mark>`.
  - Uses *Concussive Shot* to snare fleeing targets.
