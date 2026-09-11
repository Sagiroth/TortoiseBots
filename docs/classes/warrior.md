---
id: class-warrior
title: Warrior Bot AI & Specs
category: classes
summary: Deep dive into Warrior bot combat behavior, stance transitions, tanking priorities, and Turtle WoW custom abilities.
tags: [class, warrior, tank, dps, melee]
relates_to:
  - class-overview
  - guide-player-controls
---

# Warrior Bot AI & Specs

Warriors serve as primary dungeon tanks or powerful melee DPS. The bot manages rage generation, dynamic stance switching, interrupt priority, and defensive cooldowns.

## Supported Specs & Roles

- **Protection (Tank):** Operates primarily in **Defensive Stance**. Prioritizes threat generation via *Sunder Armor*, *Revenge*, *Shield Slam*, and *Taunt*.
- **Arms (Melee DPS):** Uses two-handed weapons in **Battle Stance** or **Berserker Stance**. Centers on *Mortal Strike*, *Overpower* on dodges, and *Sweeping Strikes* for cleaving packs.
- **Fury (Melee DPS):** Dual-wields in **Berserker Stance**. Drives high rage spend into *Bloodthirst*, *Whirlwind*, and *Execute*.

---

## Combat Rotations & Priorities

### 1. Protection (Tank)
1. **Pull & Engagement:** Charges in Battle Stance (or shoots ranged weapon on `.bot action pull`), immediately swaps to Defensive Stance.
2. **Threat Generation:**
   - Keeps *Shield Block* active on cooldown to enable *Revenge*.
   - Weaves *Sunder Armor* up to 5 stacks on primary target, spreading sunders to secondary mobs in multi-mob packs.
   - Casts *Shield Slam* or *Heroic Strike* as rage dump.
3. **Emergency Mitigation:**
   - *Last Stand* (12975) triggers when health < 25%.
   - *Shield Wall* triggers under severe incoming damage.
   - *Taunt* immediately targets mobs that peel off to attack healers or casters.

### 2. Arms / Fury (DPS)
1. **Opener:** *Charge* from range when available.
2. **Rage Spenders:**
   - *Overpower* triggers within 5 seconds of enemy dodge.
   - *Mortal Strike* (Arms) or *Bloodthirst* (Fury) on cooldown.
   - *Whirlwind* when 2+ targets are nearby.
3. **Execute Phase:** Below 20% enemy health, *Execute* becomes highest priority, consuming all available rage.

---

## Turtle WoW 1.18.1 Custom Content

- **Master Strike (Spell ID 54023):**
  - Custom Turtle WoW weapon-dispatched strike (30s cooldown, 20 rage).
  - Wired into both Arms and Fury combat strategies as a high-damage burst nuke when equipped with a polearm or two-handed weapon.
- **Defensive Tactics (Spell ID 51606):**
  - Passive talent providing threat aura benefits while wielding a shield in Battle or Berserker stances. Fully supported by the bot's core stance logic.

---

## Utility & Interrupts

- **Interrupts:** Casts *Shield Bash* (Defensive/Battle stance with shield) or *Pummel* (Berserker stance) instantly when an enemy begins casting an interruptible spell.
- **Shouts:** Automatically maintains *Battle Shout* on party members and applies *Demoralizing Shout* to debuff melee packs.
- **CC & Snares:** Casts *Piercing Howl* (AoE snare) or *Hamstring* on fleeing mobs. Uses *Concussion Blow* (Protection) as a 5-second stun on priority targets.
