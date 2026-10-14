---
id: class-mage
title: Mage Bot AI & Specs
category: classes
summary: Deep dive into Mage ranged elemental rotations, Polymorph CC, Counterspell interrupts, food/water conjuring, and Arcane Power safety.
tags: [class, mage, dps, ranged, cc]
relates_to:
  - class-overview
  - guide-player-controls
---

# Mage Bot AI & Specs

Mages provide premier ranged spell DPS, the game's most reliable crowd control (*Polymorph*), school-locking interrupts (*Counterspell*), and free party refreshments.

## Supported Specs & Roles

- **Frost (Ranged DPS):** Exceptional control and survivability. Leverages *Frostbolt*, *Frost Nova*, *Blizzard*, and *Ice Barrier*.
- **Fire (Ranged DPS):** Massive burst damage with *Fireball*, *Pyroblast*, *Scorch* (Fire Vulnerability) and *Combustion*.
- **Arcane (Ranged DPS):** High single-target burst with *Arcane Missiles*, *Arcane Power*, and *Presence of Mind*.

---

## Combat Rotations & Priorities

### 1. Frost Mage
- Opens at max range with *Frostbolt*.
- If enemies reach melee range, casts *Frost Nova* and *Blink* to reset distance.
  - When its target is attacking the mage and is within 8 yards, a mage of any spec turns its back to the target and then casts *Blink*, so it jumps away from the enemy instead of into it. A target fighting someone else (the tank) does not set this off, so the mage does not blink away from a mob it is walking up to. While rooted or stunned, *Blink* goes the way the mage already faces.
  - Skips *Frost Nova* when the target is already frozen or immune to the freeze, so the GCD goes to damage.
- Uses *Flamestrike* on 3+ grouped enemies, then *Blizzard* channeled on the burning ground while the pack holds. *Blizzard* also goes on top of a *Flamestrike* that is still burning (the flamestrike window).
- *Frost* adds *Cone of Cold* once 2+ enemies are attacking, only when the target is within 10 yd and in front of the mage. *Arcane* uses *Arcane Explosion* when an enemy is too close for its spells. *Fire* casts *Blast Wave* at interrupt priority when an enemy is within 10 yd.
- Stops the *Blizzard* channel early when the pack thins below two attackers.
- *Flamestrike*, *Blizzard*, *Cone of Cold* and *Arcane Explosion* are area spells for the party threat back-off: in a group with the `threat` strategy they wait while the mage holds 50% or more of the tank's threat on any mob it is fighting, loose adds included.
- Uses *Cold Snap* once any Frost cooldown (Frost Nova, Cone of Cold, Ice Barrier, Ice Block, Frost Ward) is spent.

### 2. Fire Mage
- Hurries a *Pyroblast* once *Hot Streak* reaches full stacks (highest-priority nuke).
- Refreshes *Pyroblast* in combat and consumes *Presence of Mind* with it.
- Weaves *Scorch* casts to maintain the Fire Vulnerability debuff (talent ranks and debuff are matched by spell id; Turtle has no spell named "Improved Scorch").
- Casts *Fireball* as main nuke and *Fire Blast* on the move or for finishing blows.
- Skips damage spells whose school the target is immune to (fire elementals, Molten Core adds, *Ice Block*): *Fireball* and *Scorch* fall through to *Frostbolt*, then the wand, instead of casting for zero damage. Arcane has no Frostbolt fallback row yet.

### 3. Arcane Mage
- Opens with *Arcane Rupture* (damage that strengthens missiles for 8s; recast on its cooldown), then channels *Arcane Missiles* while the buff holds, with mana management.
- Uses *Presence of Mind* for instant cast nukes.

---

## Turtle WoW 1.18.1 Custom Content & Safety Gates

- **Arcane Power Safety (Spell ID 12042):**
  - *Arcane Power* carries the `SPELL_ATTR_CANT_CANCEL` attribute, meaning that once activated, the 20-second mana drain cannot be stopped.
  - The bot implements an explicit **70% mana floor** and verifies that a live hostile target is in range before activating Arcane Power, preventing bots from draining themselves dry right before combat ends.
- **Evocation Channeling (Spell ID 12051):**
  - Automatically activates *Evocation* when out of mana.
  - Automatically cancels the channel once mana reaches 95% to immediately re-enter combat rather than standing idle.
- **Wand only without mana (Shoot, Spell ID 5019):**
  - In a group the mage also wands while the `threat` strategy vetoes its spells (`high threat`, ≥ 80% of the tank's threat on the target, or the tank has not touched it yet). That is the threat back-off, the same rule as mod-playerbots' `ThreatMultiplier`: *Shoot* has no threat type, so it is what the nukes fall back to. Whisper `co -threat` to turn it off for that bot.
  - On Turtle every wand shot puts the global cooldown on the shooter for the wand's whole swing, and the next shot renews it, so a running wand keeps *Frostbolt*, *Fireball* and every other mage spell "not ready".
  - Before, a mage whose nuke fell back to the wand once (a GCD tick, a cast refused while moving) kept wanding at full mana until it moved or the fight ended.
  - Now the `wand blocks nuke` trigger stops the wand (`stop wand`) while *Frostbolt* or *Fireball* is held back by that GCD alone, waits out the last shot's GCD and casts again. With too little mana for either nuke, or below the 5% `no mana` line (where the `no mana` → *Shoot* row wands on purpose), the wand keeps going; in a raid group the wand also stays while the mage holds aggro (the raid `has aggro` → *Shoot* row). It also stays while the `threat` strategy holds the nukes back (`high threat`: the mage is near the tank's threat on its target, or the tank has none there yet): the nukes then fall back to the wand on purpose, and stopping it only restarted it a swing later (Uldaman, run 4: `stop wand` → *Shoot* again within 2 s at 85-100% mana). It also stays while the mage is silenced, locked out of the nuke's school (*Counterspell*), or out of line of sight of the target: the core reports the GCD before those checks, so the trigger reads them on the mage itself. The trigger is checked every 1-2 s, not every tick.
  - The same GCD trap blocks *Polymorph*: its row needs the spell ready. With the `cc` strategy, the `wand blocks cc` trigger stops the wand (`stop wand`, just above the *Polymorph* row) while the mage has a sheep target waiting within *Polymorph* range (its raid mark in a dungeon) and *Polymorph* is held back by that GCD alone. While that sheep waits, the mage does not start the wand at all, so a *Frostbolt* that falls back to *Shoot* during a GCD no longer restarts it. The same exceptions apply: below the 5% `no mana` line, in a raid group while the mage holds aggro, silenced or out of line of sight, the wand stays.
- **Icicles (Spell ID 52516):**
  - Custom Turtle Frost talent providing root and shatter synergy without breaking primary frost rotation loops.

---

## Utility, CC & Party Refreshments

- **Polymorph CC:** Casts *Polymorph* (Sheep) on the target assigned via `.bot action cc <mark>` and stands still through the cast bar (the engine sleeps for the cast time, so the sheep is no longer restarted several times a second). Stops sheeping its mark once only CC-marked mobs are left fighting the group (a stunned, rooted or feared unmarked mob still counts as an enemy, so a Cheap Shot or Frost Nova does not end the sheep). Avoids damaging or AoEing sheeped targets: *Frost Nova*, *Cone of Cold*, *Arcane Explosion*, *Flamestrike* and *Blizzard* are skipped when their area would reach a sheeped mob or the mage's own mark (while other enemies are left, sheeped or not, since she sheeps it next), and *Flamestrike*/*Blizzard* also near any CC mark about to be sheeped. In the open world the assigned mark always wins her CC pick; a free pick never replaces it. She starts *Polymorph* only in line of sight and within 25 yd (5 yd inside its range) and walks closer first (`reach cc`), so a mark that moves during the cast bar does not fail the cast. Under `stay` she cannot walk closer, so she starts no sheep at 25-30 yd. With `AiPlayerbot.EnableActionLog = 1`, a failed sheep is written to the bot action log as a `CC_CAST_FAILED` row (spell, failure code, distance to the target, moving).
- **Interrupts:** Casts *Counterspell* immediately when an enemy begins casting a dangerous spell, locking out that spell school for up to 10 seconds.
- **Food & Drink Conjuration:** Automatically conjures food and water out of combat, sharing stacks with party members who need mana or health.
- **Mana Gem Priority:** Consumes the strongest gem in bags first (Ruby → Citrine → Jade → Agate) so low-level gems are never wasted ahead of raid gems.
- **Mana Gem Timing:** Burns the gem early in the fight (below 40% mana, in combat only) so the mana lands while it still matters; *Evocation* stays the below-15% rescue.
- **Buffs:** Maintains *Arcane Intellect* on all mana-using party members (upgrading to *Arcane Brilliance* once known, trained and stocked, once at least three same-map members lack both auras, outbidding the single-target cast). Buffs expiring within 15 s count as missing, so they are refreshed before they drop (issue #468) and self *Mage Armor* / *Ice Armor*. Single-target *Arcane Intellect* is also allowed in combat at the lowest priority, so a mage following a master who chain-pulls still buffs the party in the quiet moments of a fight. Casts *Mana Shield* at low health and *Ice Block* at critical health. With several mages in one party, a short shared *buff claim* keeps them from duplicating each other: while one mage's cast is in flight the others stand down and wait for the aura instead of casting the same buff on the same member (issue #378).
- **Buff scheduling:** Out of combat, catching up to the master runs below the party buffs (a pending buff in range wins the tick, follow resumes next). Upkeep buffs wait for 40% mana, 20% with a real player master. A failed buff attempt no longer starts the retry window or the duplicate-cast claim; only a cast that actually starts does.
- **Curses:** Uses *Remove Lesser Curse* on party members affected by debilitating curses.

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots provides verified 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Arcane** | `8.0` | Ranged DPS | `40 / 0 / 11` | Arcane Power (40), PoM (30), Temporal Convergence, Accelerated Arcana, Frost hit cap dip. 0 points in Wand Spec. |
| **Fire** | `8.1` | Ranged DPS | `10 / 41 / 0` | Hot Streak (30), Combustion (40), Master of Elements, 5/5 Clearcasting + -40% threat dip. 0 points in Wand Spec. |
| **Frost** | `8.2` | Ranged DPS | `10 / 0 / 41` | Shatter (30), Ice Block (35), Ice Barrier (40), Icicles, Flash Freeze, Clearcasting + threat dip. 0 points in Wand Spec. |

### Leveling Milestones & Progression Rationale

- **Arcane (`8.0`):**
  - *Levels 10–35:* Burst and mana foundation: *Arcane Subtlety* (2/2 -40% threat), *Improved Arcane Missiles* (5/5 anti-pushback), *Arcane Concentration* (5/5 Clearcasting), *Presence of Mind* (30 instant cast), and *Temporal Convergence* (3/3 haste).
  - *Levels 35–45:* *Accelerated Arcana* (1/1), *Arcane Power* (40 +30% spell damage burst), and *Resonance Cascade* (5/5).
  - *Levels 45–60:* Frost dip into *Improved Frostbolt* (5/5) and *Elemental Precision* (3/3 providing 6% spell hit cap and threat reduction) to ensure high raid damage without resisting.
- **Fire (`8.1`):**
  - *Levels 10–35:* Fire nuke ramping: *Improved Fireball* (5/5 cast time reduction), *Ignite* (5/5 +40% periodic crit burn), *Pyroblast* (20 opener), *Master of Elements* (3/3 mana refund on crit), and *Hot Streak* (30 instant Pyroblast procs).
  - *Levels 35–45:* *Fire Power* (5/5 fire spell damage) and *Combustion* (40 critical strike engine).
  - *Levels 45–60:* *Burning Soul* (2/2 pushback resistance) and 10 Arcane points (*Arcane Subtlety* 2/2 for threat reduction + *Arcane Concentration* 5/5 for Clearcasting free spells).
- **Frost (`8.2`):**
  - *Levels 10–35:* Shatter and control engine: *Improved Frostbolt* (5/5), *Elemental Precision* (3/3 spell hit cap), *Ice Shards* (5/5 +100% crit damage bonus), *Cold Snap* (25 cooldown reset), and *Shatter* (30 +50% crit on frozen targets).
  - *Levels 35–45:* *Ice Block* (35 emergency immunity), *Ice Barrier* (40 damage absorb shield), *Icicles* (1/1), and *Winter's Chill* (5/5 stacking frost crit debuff).
  - *Levels 45–60:* *Flash Freeze* (2/2 instant Frostbolt procs) and 10 Arcane points into *Arcane Subtlety* (2/2) and *Arcane Concentration* (5/5 Clearcasting).

