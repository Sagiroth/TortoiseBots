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

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots provides validated 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Beast Mastery** | `3.0` | Ranged DPS | `45 / 6 / 0` | Bestial Wrath (40), Frenzy (45), Kill Command (50), Unleashed Fury, Scent of Blood, Efficiency dip. |
| **Marksmanship** | `3.1` | Ranged DPS | `9 / 42 / 0` | Aimed Shot (20), Mortal Shots (30), Lock and Load (50), Experimental Ammunition, Barrage, BM survivability dip. |
| **Survival** | `3.2` | Melee DPS | `8 / 0 / 43` | Carve (35), Lacerate (50), Untamed Trapper (60), Savage Strikes, Trap Mastery, BM pet health dip. |

### Leveling Milestones & Progression Rationale

- **Beast Mastery (`3.0`):**
  - *Levels 10–35:* Pet empowerment rush: *Swift Aspects* (5/5), *Endurance Training* (5/5), *Thick Hide* (3/3), *Unleashed Fury* (5/5 pet damage), *Ferocity* (5/5 pet crit), and *Intimidation* (30).
  - *Levels 35–45:* *Scent of Blood* (3/3), *Bestial Wrath* (40 signature enrage), *Frenzy* (5/5 pet attack speed), and *Spirit Bond* (2/2).
  - *Levels 45–60:* *Kill Command* (50 capstone instant pet strike) and 6 points in Marksmanship (*Efficiency* 5/5 + *Improved Stings* 1/5) for shot mana sustainability.
- **Marksmanship (`3.1`):**
  - *Levels 10–30:* Core shot burst: *Efficiency* (5/5), *Lethal Shots* (5/5 ranged crit), *Aimed Shot* (20 signature opener), *Swiftshot* (3/3), and *Mortal Shots* (30 +30% crit damage bonus).
  - *Levels 30–50:* *Barrage* (3/3 Multi-Shot damage), *Experimental Ammunition* (40), *Ranged Weapon Specialization* (5/5), and *Lock and Load* (50 capstone proc).
  - *Levels 50–60:* Completes Marksmanship utility and takes 9 Beast Mastery points (*Swift Aspects* 5/5 + pet survivability) to support group dungeon grinding.
- **Survival (`3.2`):**
  - *Levels 10–35:* Melee toolkit rush: *Improved Slaying* (3/3), *Resourcefulness* (5/5 trap cost/cooldown), *Savage Strikes* (2/2 Raptor/Mongoose crit), *Planning Ahead* (2/2), and *Carve* (35 front-cone cleave).
  - *Levels 35–50:* *Surefooted* (3/3 hit chance), *Killer Instinct* (3/3 crit), *Trap Mastery* (3/3), and *Lacerate* (50 stacking melee bleed).
  - *Levels 50–60:* *Lightning Reflexes* (5/5 Agility scaling), *Untamed Trapper* (60 capstone), and 8 Beast Mastery points (*Endurance Training* 5/5 + *Thick Hide* 3/3) to keep the melee hunter's pet resilient.

