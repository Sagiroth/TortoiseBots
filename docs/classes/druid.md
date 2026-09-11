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

- **Feral (Bear Tank):** Dire Bear Form tank specializing in *Growl*, *Maul*, *Swipe*, and *Demoralizing Roar*.
- **Feral (Cat Melee DPS):** Cat Form stealth and energy specialist utilizing *Claw*, *Rake*, *Shred*, *Rip*, and *Ferocious Bite*.
- **Restoration (Healer):** HoT-focused healing with *Rejuvenation*, *Regrowth*, *Healing Touch*, and *Swiftmend*.
- **Balance (Ranged DPS):** Moonkin caster driving Nature and Arcane damage via *Moonfire*, *Wrath*, *Starfire*, and *Insect Swarm*.

---

## Shapeshifting & Form Maintenance

The bot's shapeshifting engine maintains the appropriate form based on assigned party role:
- If designated as **Tank**, the bot stays in **Bear Form / Dire Bear Form**.
- If designated as **Melee DPS**, the bot stays in **Cat Form**.
- If designated as **Ranged DPS**, the bot stays in **Moonkin Form** (if talented) or Humanoid form.
- If designated as **Healer**, the bot stays in **Humanoid Form** or **Tree of Life Form**.
- **Caster Shifting:** The bot automatically shifts out of feral forms when out of combat to cast party buffs (*Mark of the Wild*, *Thorns*), dispel poisons (*Cure Poison*), or consume water.

---

## Turtle WoW 1.18.1 Custom Content

- **Tree of Life Form (Spell ID 45705):**
  - Custom Turtle WoW Restoration talent form.
  - Grants a spirit-scaling healing aura that buffs all party members while reducing mana cost of healing spells.
- **Berserk (Spell ID 45708):**
  - Custom Feral talent. Cat form removes energy cost limitations for massive burst; Bear form eliminates *Growl* cooldown and spreads damage reduction.
- **Swiftmend HoT-Gating (Spell ID 18562):**
  - Consumes the shortest remaining active *Rejuvenation* or *Regrowth* HoT on an ally to deliver instantaneous burst healing.
  - The bot verifies that an active HoT exists on the target before attempting cast, preventing wasted cooldown triggers.

---

## Utility & Crowd Control

- **Crowd Control:**
  - Casts *Entangling Roots* outdoors to root melee mobs away from party casters.
  - Casts *Hibernate* when assigned CC on Beasts or Dragonkin.
- **Interrupts & Stuns:**
  - Casts *Feral Charge* (Bear) to root and interrupt distant casters.
  - Casts *Bash* (Bear) to stun melee targets.
- **Combat Resurrection:**
  - Uses *Rebirth* (Battle Rez) to revive a fallen party tank or healer mid-fight.
- **Innervate:**
  - Casts *Innervate* on the party healer when their mana drops below 20%.
- **Party Buffs:**
  - Maintains *Mark of the Wild* (armor, stats, resistances) and *Thorns* (reflective nature damage) on party members.
