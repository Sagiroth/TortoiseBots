---
id: class-hunter
title: Hunter Bot AI & Specs
category: classes
summary: Guide to Hunter bot ranged and melee combat, pet care, Aspect upkeep (Viper manual-only), and custom Turtle Survival skills.
tags: [class, hunter, dps, ranged, pet, melee]
relates_to:
  - class-overview
  - guide-player-controls
---

# Hunter Bot AI & Specs

Hunters excel at sustained single-target ranged DPS, pet off-tanking, snares, and crowd control. In Turtle WoW 1.18.1, Hunters also possess a partially automated melee Survival combat style (ranged-first hybrid; *Carve* fires on AoE packs, *Lacerate* is manual).

## Supported Specs & Roles

- **Beast Mastery (Ranged DPS):** Focuses on pet empowerment, *Bestial Wrath*, and high sustained ranged output.
- **Marksmanship (Ranged DPS):** Heavy physical burst damage centered on *Aimed Shot*, *Multi-Shot*, and *Trueshot Aura*.
- **Survival (Melee / Ranged Hybrid):** Turtle WoW custom melee combat utilizing traps, *Carve* (automatic on AoE packs), and manual *Lacerate*.

---

## Combat Rotations & Priorities

### 1. Ranged Combat (Beast Mastery & Marksmanship)
1. **Opener:** Casts *Hunter's Mark* on the primary target, orders pet to attack.
2. **Shot Priority:**
   - *Aimed Shot* when the MM talent is known, otherwise *Arcane Shot* (never both), on cooldown.
   - *Multi-Shot* when AoE is permitted and multiple enemies are engaged.
   - Keeps *Serpent Sting* ticking on high-health targets (skips on low-health mobs to conserve mana).
3. **Dead-Zone Handling:** Below level 10 a hunter with a loaded ranged weapon keeps its ranged kit instead of switching to melee: nothing switches it back before level 10 (`SwitchToRangedTrigger` — no snares or traps yet), and the switch removes the `ranged` strategy that gates the auto shot the kit keeps running, so one melee switch ends the hunter's sustained ranged attack for the rest of the level. The melee fallback (*Wing Clip*, *Mongoose Bite*/*Raptor Strike*, melee weapon swapping) is reserved for hunters that cannot shoot at all: no ranged weapon, no matching ammo, or a spent thrown-weapon stack. Inside the weapon's own minimum range (the dead zone, where no shot is possible and the generic `enemy too close for spell` flee is suppressed against a glued target) an armed hunter steps back out with `enemy too close for auto shot` → `flee`, then resumes shooting while the pet holds the target. From level 10 the bot swaps both ways (melee when a faster target is on it, back to ranged when it can kite); *Disengage* is not automated.
4. **Distance Band:** The ranged kit keeps its own band: `flee` moves the bot out when a target is inside it, and `enemy out of spell` → `reach spell` closes the distance again when the target leaves casting range.

### 2. Melee Survival Combat
- Closes into melee range.
- Casts *Carve* automatically on AoE packs; *Lacerate* is a manual action.
- Applies *Wing Clip* and *Mongoose Bite* reactive counter-attacks.

---

## Turtle WoW 1.18.1 Custom Content

- **Aspect of the Viper (Spell ID 45651):**
  - Custom Turtle WoW aspect that regenerates mana on ranged attacks while reducing damage output.
  - *Aspect of the Viper* is a manual action only; the bot auto-maintains only *Aspect of the Hawk*.
- **Carve (Spell ID 51575):**
  - Custom Turtle WoW instant melee weapon attack hitting up to 3 nearby enemies. Integrated into Survival AoE packs.
- **Lacerate (Spell ID 48049):**
  - Custom Turtle WoW melee bleed ability providing sustained physical damage. Manual action only.

---

## Pet Management & Utility

- **Pet Lifecycle:**
  - **Pets start at level 10.** *Tame Beast* (1515) and the *Call Pet* / *Revive Pet* / *Feed Pet* kit it unlocks are trainer tier 10 in the server data (`tw_world.spell_template`: `baseLevel` = `spellLevel` = 10), so a bot below that level owns no pet at all. The seam (`PlayerbotFactory::InitPet`) refuses to create one, and a pool hunter that logs in below the threshold gives up a pet an older pool seeded for it (`runtime/HunterPetPolicy.h`); player-owned, hired and adopted hunters are never touched.
  - At the threshold (on level-up, or logging in at 10+ without a pet) the bot is granted a random tameable pet matching its level, out of combat.
  - Automatically summons its pet out of combat. With no pet at all it will attempt to tame a nearby tameable beast.
  - Revives dead pets using *Revive Pet* and heals injured pets during combat via *Mend Pet*.
  - Restores an unhappy pet's happiness via the feed handler (no food items are consumed).
- **Pet Safety & CC Discipline:**
  - When a target is crowd-controlled (e.g. *Polymorph* or *Freezing Trap*), the bot's pet is prevented from attacking the CC'd mob, preventing accidental breaks.
- **Crowd Control:**
  - Deploys *Freezing Trap* when assigned CC via `.bot action cc <mark>`.
  - Uses *Concussive Shot* to snare fleeing targets.

---

## Diagnostics (`bot_events.csv`)

Hunter ammo stacks cannot tell whether a hunter is shooting: the random-bot item cheat refills the equipped ammo stack to its maximum on every update, so the stack sits at 200 either way. These rows (`AiPlayerbot.AllowedLogFiles` must list `bot_events.csv`) carry the decision instead — one row per bot per event per 5 s, with the distance to the target and the strategy flags, captured before any switch is applied:

| Event | Meaning | `info2` flags |
| :--- | :--- | :--- |
| `AutoShot` | The module (re)started the hunter's ranged auto-attack (the core then repeats it on its own until the target leaves the weapon's band) | `cast`, `ranged`, `close` |
| `SwitchToMelee` | The hunter traded its ranged kit for melee (`-ranged,+close`) | pre-switch `ranged`, `close` |
| `SwitchToRanged` | The hunter traded melee back for ranged (`-close,+ranged`) | pre-switch `ranged`, `close` |

Reading it: `AutoShot` rows with `ranged=1,close=0` are a hunter firing; a `SwitchToMelee` row with `ranged=1` followed by no `AutoShot` rows is a hunter that lost its ranged kit and, below level 10, cannot switch back (see the level gate in `SwitchToRangedTrigger`).

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

