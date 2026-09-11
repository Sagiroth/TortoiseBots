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

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots ships with pre-configured talent progressions at 5-level intervals (levels 10–60) designed for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Fury** | `1.0` | Melee DPS | `17 / 34 / 0` | Death Wish (30), Flurry (35), Bloodthirst (40), Ravager (-3s Whirlwind CD), 17 Arms dip for Deep Wounds 3/3 + Impale 2/2 (+20% crit damage bonus). |
| **Protection** | `1.1` | Tank | `7 / 0 / 44` | Shield Slam (40), Concussion Blow (50), Gag Order (silence on Shield Bash / dispel on Shield Slam), Defensive Tactics, Tactical Mastery 4/5. |
| **Arms** | `1.2` | Melee DPS | `40 / 11 / 0` | Sweeping Strikes (30), Mortal Strike (40), Master Strike, Boundless Anger, 11 Fury dip for Cruelty 5/5 + Piercing Howl. |

### Leveling Milestones & Progression Rationale

- **Fury (`1.0`):**
  - *Levels 10–30:* Pure Fury path rush to *Cruelty* (5/5 crit), *Dual Wield Specialization* (5/5), *Unbridled Wrath* (5/5), *Piercing Howl* (1/1 AoE snare), and *Death Wish* (30).
  - *Levels 30–40:* *Flurry* (5/5 attack speed) into *Bloodthirst* (40 capstone).
  - *Levels 40–60:* Takes *Ravager* (3/3 Cleave rage / Whirlwind CD) and *Improved Execute* (2/2), then transitions into Arms for *Tactical Mastery* (5/5), *Improved Overpower* (2/2), *Deep Wounds* (3/3), and *Impale* (2/2), maximizing critical strike burst.
- **Protection (`1.1`):**
  - *Levels 10–30:* Defensive core rushing *Shield Specialization* (5/5), *Anticipation* (3/3), *Toughness* (5/5), *Last Stand* (20), *Improved Taunt* (2/2), and *Improved Revenge* (3/3).
  - *Levels 30–50:* *Defiance* (5/5 threat), *Gag Order* (2/2 silence utility), *Shield Slam* (40), *Improved Shield Slam* (2/2), *Reprisal* (2/2), and *Concussion Blow* (50).
  - *Levels 50–60:* *Defensive Tactics* (3/3) and 7 points in Arms (*Tactical Mastery* + *Improved Heroic Strike*) to retain rage across stance dancing.
- **Arms (`1.2`):**
  - *Levels 10–40:* Focuses on two-handed power: *Deflection* (5/5), *Deep Wounds* (3/3), *Two-Handed Weapon Specialization* (3/3), *Impale* (2/2), *Sweeping Strikes* (30), and *Mortal Strike* (40).
  - *Levels 40–60:* Custom Turtle talents *Boundless Anger* (3/3) and *Master of Arms* (5/5), followed by an 11-point Fury dip into *Cruelty* (5/5) and *Piercing Howl* (1/1).

