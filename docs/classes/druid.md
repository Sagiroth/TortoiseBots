---
id: class-druid
title: Druid Bot AI & Specs
category: classes
summary: Deep dive into Druid shapeshifting mechanics, Bear tanking, Cat melee DPS, Balance caster, and Restoration healing.
tags: [class, druid, tank, healer, dps, hybrid]
relates_to:
  - class-overview
  - guide-player-controls
---

# Druid Bot AI & Specs

Druids are the ultimate hybrid class, able to fulfill Tank, Healer, Melee DPS, or Ranged Caster roles through flexible shapeshifting forms.

## Supported Specs & Roles

- **Feral (Bear Tank):** Dire Bear Form tank specializing in *Growl*, *Maul*, *Swipe*, and *Demoralizing Roar*, with *Frenzied Regeneration*, *Challenging Roar*, *Mangle (Bear)*, *Faerie Fire (Feral)*, and *Enrage* also wired.
- **Feral (Cat Melee DPS):** Cat Form stealth and energy specialist utilizing *Claw*, *Rake*, *Shred* (with *Mangle (Cat)* as fallback), *Rip*, and *Ferocious Bite*, with *Pounce*, *Ravage*, and *Tiger's Fury* also wired.
- **Restoration (Healer):** HoT-focused healing with *Rejuvenation*, *Regrowth*, *Healing Touch*, and *Swiftmend*, with *Nature's Swiftness* and *Tranquility* also wired.
- **Balance (Ranged DPS):** Moonkin caster driving Nature and Arcane damage via *Moonfire*, *Wrath*, *Starfire*, and *Insect Swarm*.
- **Below level 10 (`leveling` kit):** Neither the Feral nor the Balance kit is affordable yet, so the bot runs the dedicated leveling set: it fights in melee (auto-attack is its default action), keeps *Moonfire* applied, and heals itself with *Healing Touch*/*Rejuvenation*. It closes distance like every other melee kit — an out-of-melee enemy is walked into contact, and *Wrath* lands whenever the bot cannot move (rooted, stunned, or a target it gave up reaching).

---

## Shapeshifting & Form Maintenance

The bot's shapeshifting engine maintains the appropriate form based on assigned party role:
- If designated as **Tank**, the bot stays in **Bear Form / Dire Bear Form**.
- If designated as **Melee DPS**, the bot stays in **Cat Form**.
- If designated as **Ranged DPS**, the bot stays in **Moonkin Form** (if talented) or Humanoid form.
- If designated as **Healer**, the bot stays in **Humanoid Form** or **Tree of Life Form**.
- **Caster Shifting:** The bot automatically shifts out of feral forms when out of combat to cast party buffs (*Mark of the Wild*, *Thorns*), dispel poisons (*Abolish Poison*, with *Cure Poison* as the fallback), or consume water.

---

## Turtle WoW 1.18.1 Custom Content

- **Tree of Life Form (Spell ID 45705, per game data):**
  - Custom Turtle WoW Restoration talent form.
  - Grants a spirit-scaling healing modifier with a party aura and polymorph immunity at the cost of movement speed, per game data.
- **Berserk (Spell ID 45708, per game data):**
  - Custom Feral talent. Per game data it branches Bear/Dire Bear into 45709 (+20% max health) and Cat into 45710; the bot fires it as a combat boost trigger.
- **Swiftmend HoT-Gating (Spell ID 18562):**
  - Consumes the shortest remaining active *Rejuvenation* or *Regrowth* HoT on an ally to deliver instantaneous burst healing.
  - The bot verifies that an active HoT exists on the target before attempting cast, preventing wasted cooldown triggers.

---

## Utility & Crowd Control

- **Crowd Control:**
  - Casts *Entangling Roots* outdoors to root melee mobs away from party casters.
  - Casts *Hibernate* when assigned CC (the bot applies no creature-type filter itself; it relies on core validation).
- **Interrupts & Stuns:**
  - Casts *Feral Charge* (Bear) as a gap-closer on any out-of-melee enemy.
  - Casts *Bash* (Bear) as an interrupt (also wired against enemy healers).
- **Combat Resurrection:**
  - Uses *Rebirth* (Battle Rez) on the first dead party member mid-fight (no tank/healer priority).
- **Innervate:**
  - Casts *Innervate* on the lowest-mana party healer below the `AiPlayerbot.LowMana` threshold (default 15%), falling back to self when solo or healers are healthy. Manual `.bot boost` assignments win over automation. Shifts to caster form first (required by Turtle 1.18.1 shapeshift rules).
- **Barkskin:**
  - Balance/Restoration cast *Barkskin* on own-health triggers (medium-health band in the Balance/Restoration combat sets, almost-full-health band in the generic set); never wired for Feral (Cat/Bear lose form bonuses under Turtle 1.18.1 attack-speed and shapeshift penalties).
- **Party Buffs:**
  - Maintains *Mark of the Wild* (armor, stats, resistances) and *Thorns* (reflective nature damage) on party members, upgrading *Mark of the Wild* to *Gift of the Wild* once known (the group version outbids the single-target cast and only targets a member that lacks both).

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots configures validated 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Balance** | `11.0` | Ranged DPS | `38 / 0 / 13` | Moonkin Form (35), Eclipse (45), Balance of All Things, Omen of Clarity, Subtlety -20% threat dip. |
| **Feral** | `11.1` | Tank / Melee DPS | `11 / 40 / 0` | Shared Bear/Cat build: Leader of the Pack (50), Berserk (40), Carnage, Heart of the Wild, Thick Hide, Omen of Clarity. |
| **Restoration** | `11.2` | Healer | `10 / 0 / 41` | Swiftmend (20), Nature's Swiftness (35), Tree of Life Form (50), Gift of Nature, Preservation, Balance dip. |

### Leveling Milestones & Progression Rationale

- **Balance (`11.0`):**
  - *Levels 10–35:* Arcane/Nature scaling: *Improved Wrath* (5/5), *Improved Moonfire* (2/2), *Natural Weapons* (3/3), *Moonfury* (3/3), *Omen of Clarity* (1/1 Clearcasting), *Vengeance* (5/5 crit damage), and *Moonkin Form* (35 signature armor & crit aura).
  - *Levels 35–45:* *Moonglow* (3/3 mana efficiency), *Owlkin Frenzy* (3/3), *Balance of All Things* (3/3), and *Eclipse* (45 custom Wrath/Starfire alternation engine).
  - *Levels 45–60:* Restoration threat dip into *Improved Mark of the Wild* (5/5) and *Subtlety* (5/5 for -20% threat reduction on balance nukes).
- **Feral (`11.1` - Shared Bear Tank & Cat DPS):**
  - *Rationale:* A single, robust Feral talent tree covers both Bear Tanking and Cat DPS without compromise. In combat, the bot's runtime stance engine switches between forms based on role (Tank -> Bear, DPS -> Cat).
  - *Levels 10–35:* Feral core: *Ferocity* (5/5 cost reduction), *Feral Instinct* (3/3 threat/stealth), *Thick Hide* (3/3 armor), *Feral Charge* (20 Bear interrupt/gap closer), *Sharpened Claws* (3/3 crit), *Primal Fury* (2/2 rage/combo on crit), and *Predatory Strikes* (3/3 AP scaling).
  - *Levels 35–50:* *Berserk* (40 custom burst), *Heart of the Wild* (5/5 +20% Stamina in Bear, +20% Strength in Cat), *Carnage* (2/2 bleed damage), and *Leader of the Pack* (50 party crit aura).
  - *Levels 50–60:* Balance dip into *Natural Weapons* (3/3 physical damage), *Natural Shapeshifter* (2/3), and *Omen of Clarity* (60 Clearcasting on melee attacks).
- **Restoration (`11.2`):**
  - *Levels 10–35:* HoT efficiency: *Improved Mark of the Wild* (5/5), *Improved Healing Touch* (5/5), *Swiftmend* (20 burst HoT consumption), *Gift of Nature* (5/5 healing throughput), and *Nature's Swiftness* (35 emergency instant heal).
  - *Levels 35–50:* *Tranquil Spirit* (5/5 cost reduction), *Preservation* (3/3), *Improved Regrowth* (5/5 crit), and *Tree of Life Form* (50 spirit aura & HoT efficiency form).
  - *Levels 50–60:* Balance dip into *Improved Wrath* (5/5) and *Sylvan Blessing* (2/2) for solo/dungeon questing support.

