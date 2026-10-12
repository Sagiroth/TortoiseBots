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

- **Beast Mastery (Ranged DPS):** Focuses on pet empowerment, *Bestial Wrath*, *Kill Command*, and *Intimidation* on cooldown, with high sustained ranged output.
- **Marksmanship (Ranged DPS):** Heavy physical burst damage centered on *Aimed Shot*, *Multi-Shot*, and *Trueshot Aura*.
- **Survival (Melee / Ranged Hybrid):** Turtle WoW custom melee combat utilizing traps, *Carve* (automatic on AoE packs), and manual *Lacerate*.

---

## Combat Rotations & Priorities

### 1. Ranged Combat (Beast Mastery & Marksmanship)
1. **Opener:** Casts *Hunter's Mark* on the primary target, orders pet to attack.
2. **Shot Priority:**
   - *Aimed Shot* when the MM talent is known, otherwise *Arcane Shot* (never both), on cooldown.
   - *Steady Shot* ([Turtle], taught by the hunter trainer at levels 36-60; highest known rank) as the filler below Aimed/Arcane (`ACTION_NORMAL - 1`, above plain Auto Shot). It is a timed cast with no cooldown. The core pauses the repeating Auto Shot during a timed cast and raises the ranged timer to a 500 ms windup on resume, so the `steady shot` trigger and action fire only when the ranged timer exceeds the real cast time + 500 ms + a 50 ms (one world tick) margin (`HunterSteadyShotFitsAutoShotWindow`). The real cast is the base 1 s scaled by ranged haste plus the flat 500 ms the core adds to every ranged-attribute cast, so in practice Steady Shot fits only right after an Auto Shot and only with an effective ranged speed above about 2 s. Otherwise the bot keeps auto-shooting and Auto Shot is not delayed. No toggle: it completes the existing ranged kit, like Arcane Shot.
   - *Multi-Shot* when AoE is permitted and multiple enemies are engaged.
   - Keeps *Serpent Sting* ticking on high-health targets (skips on low-health mobs to conserve mana).
   - *Serpent Sting* is the default sting. *Viper Sting* (drains the target's mana to the hunter over time) replaces it only when the hunter's own mana is below 50% and the target has mana (>= 10%); at healthy mana a caster mob still gets Serpent Sting. A manual `sting viper` order skips the own-mana gate, and a manual `sting serpent` order always allows Serpent Sting.
3. **Threat Drop:** Climbing toward the top of the threat table without holding aggro fires *Feign Death* (medium threat); high threat feigns in raids too. The bot stands back up on its own via the feign-threat action node.
4. **Dead-Zone Handling:** Inside the weapon's own minimum range no shot is possible, and the generic `enemy too close for spell` flee is suppressed against a fast target. A mob glued there is answered by trading into melee (`-ranged,+close`, melee auto-attack + *Raptor Strike*; *Wing Clip*/*Mongoose Bite* once known) at any level — `SwitchToMeleeTrigger` has no level gate, mirroring the donor. The switch pair uses the mob's victim as hysteresis so a mob pacing the boundary cannot flip the kit every tick: melee when the mob is on the bot and inside 8 yd, ranged back as soon as the mob is off the bot (pet, tank or anyone else holds it) at any distance, or is rooted. `SwitchToRangedTrigger` (also ungated) hands the ranged kit back on those terms; it no longer waits for 8 yd of room, which the melee kit's own `reach melee` never gave, so a hunter whose pet took the mob back kept meleeing it for the rest of the fight. In the ranged kit the 5 yd *Raptor Strike*/*Wing Clip* row (`hunter enemy is close`) fires only for a mob on the hunter itself. A pet- or tank-held mob inside the dead zone gets one step out per mob (`hunter dead zone step out` → `hunter step out` at `ACTION_MOVE`): a point move straight back (fanned up to 90° to either side when blocked) that lands 3 yd past the dead-zone edge, held while it runs and never re-dispatched; then the hunter shoots. If that step cannot get it out (no reachable spot, it stops still inside, or 3 s pass), the hunter trades into melee instead of fleeing again, and holds the melee kit while that mob is still in the dead zone, for up to 10 s from the step; after that a fresh step is allowed. The generic `enemy too close for spell` flee no longer fires for a hunter whose mob is on someone else: for pool bots it was a chase at flee range that the core turns into standing still next to a stationary mob, so it re-fired every tick. Every kit switch waits until the current kit has been held 4 s (out of ammo and the failed step excepted), so pet and hunter trading aggro inside 8 yd cannot rebuild the strategies every few seconds. The melee kit keeps the 5 yd row unchanged. When the mob is on the hunter and the switch leaves the bot alone (target rooted or too slow, or no melee weapon) the `enemy too close for auto shot` → `flee` step-back at `ACTION_MOVE − 1` still moves it out, after which it resumes firing.
5. **Distance Band:** The ranged kit keeps its own band: `flee` moves the bot out when a target is inside it, and `enemy out of spell` → `reach spell` closes the distance again when the target leaves casting range. While the mob is on someone else (pet, tank, another player), the hunter's `enemy too close for spell` flee does not fire at 40 % of spell range (70 % in raids) any more, so a hunter whose pet holds the mob stands and shoots instead of running; inside the dead zone (*Auto Shot* minimum range) the one-shot step-out above moves it instead (melee reach is the fallback only when Auto Shot has no range data); mod-playerbots gates the same step-out on melee range. When the mob is on the hunter itself, the 60 % band (70 % in raids) and the slow-kite speed guard apply as before.

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
  - Mid-fight recovery: calls a dismissed pet back instantly, and revives a dead pet when nobody is hitting the bot (the 10s channel never starts under fire). *Tame Beast* stays as last resort for the genuine no-call-pet case.
  - Revives dead pets using *Revive Pet* and heals injured pets via *Mend Pet*. Mend Pet is a channel on 1.12/Turtle and stops the shots while it runs, so in combat the bot mends only below 40% and only when no rotation shot is ready (it still outranks auto shot); out of combat it also tops the pet up below 70%.
  - Restores an unhappy pet's happiness via the feed handler (no food items are consumed).
  - **Autonomous upkeep:** out of combat the bot sweeps the pet's known spells and enables autocast on every non-passive one except *Prowl*, *Cower*, *Spell Lock* and *Devour Magic* (deliberate-cast abilities), and pins the pet plus guardians to *Defensive* after each (re)tame or summon. Explicit `.bot pet aggressive|defensive|passive` orders still apply immediately.
  - **Pet ranks:** the pet learns the highest rank its family and level allow on tame and on the periodic initialize-pet tick — *Growl* plus the family kit (*Bite*/*Claw*, boar *Charge*, cat/wolf *Dash*, bird/bat *Dive*, bat/owl *Screech*, wolf *Furious Howl*, cat *Prowl*, scorpid poison, crocolisk Death Roll, raptor Savage Rend, Turtle custom Serpent/Fox/Moth abilities, ...), ranks replacing lower ones automatically. The tick teaches a live pet only (never re-creates or kills it) and walks the same table as the missing-rank check, so an unteachable row can never retrigger it. Pool bots never visit a pet trainer, so without this they would keep rank 1 forever.
  - **Pet taunts follow the group:** *Growl* stays autocast-on while the pet must hold aggro (solo, or grouped with no tank) and turns off when grouped with a real tank (player or bot tank role) so the pet stops peeling off the tank. This also holds in a player's party and for hired hunters, where the sweep touches only *Growl* (every other autocast stays the player's) and stops for good once the player orders `.bot pet autocast Growl`. The change lands on the next out-of-combat sweep tick. If the pet holds aggro anyway while stood down, the bot orders *Cower* (on cooldown, while the pet holds aggro) to drop threat (autocast stays off).
- **Pet Safety & CC Discipline:**
  - When a target is crowd-controlled (e.g. *Polymorph* or *Freezing Trap*), the bot's pet is prevented from attacking the CC'd mob, preventing accidental breaks.
- **Crowd Control:**
  - Deploys *Freezing Trap* when assigned CC via `.bot action cc <mark>`.
  - When its mark is a beast it can *Scare Beast*, it never hits that mark with an area attack (*Multi-Shot*, *Volley*, ...) while other enemies are left, because it CCs that mob next. Once only CC marks are left, AoE on them is allowed.
  - Uses *Concussive Shot* to snare fleeing targets.

---

## Diagnostics (`bot_events.csv`)

Hunter ammo stacks cannot tell whether a hunter is shooting: the random-bot item cheat refills the equipped ammo stack to its maximum on every update, so the stack sits at 200 either way. These rows (`AiPlayerbot.AllowedLogFiles` must list `bot_events.csv`) carry the decision instead — one row per bot per event per 5 s, with the distance to the target and the strategy flags, captured before any switch is applied:

| Event | Meaning | `info2` flags |
| :--- | :--- | :--- |
| `AutoShot` | The module (re)started the hunter's ranged auto-attack (the core then repeats it on its own until the target leaves the weapon's band) | `cast`, `ranged`, `close` |
| `SwitchToMelee` | The hunter traded its ranged kit for melee (`-ranged,+close`) | pre-switch `ranged`, `close`, firing branch (`why=M,...`) |
| `SwitchToRanged` | The hunter traded melee back for ranged (`-close,+ranged`) | pre-switch `ranged`, `close`, firing branch (`why=R,...`) |

Reading it: `AutoShot` rows with `ranged=1,close=0` are a hunter firing; a `SwitchToMelee` row with `ranged=1` followed by a `SwitchToRanged` row and new `AutoShot` rows is a hunter that traded into melee for a glued mob and re-opened fire once it could.

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

