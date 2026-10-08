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
1. **Pull & Engagement:** Charges on enemy-out-of-melee, then swaps to Defensive Stance (no Battle Stance prep in code).
2. **Threat Generation:**
   - Keeps *Shield Block* active on cooldown to enable *Revenge*.
   - Re-applies *Sunder Armor* unconditionally on the current target (no stack counting, no spreading to secondary mobs; AoE threat is *Challenging Shout*).
   - Casts *Shield Slam* or *Heroic Strike* as rage dump.
3. **Emergency Mitigation:**
   - *Last Stand* (12975, per game data) triggers on the critical-health trigger (default 20).
   - *Shield Wall* triggers under severe incoming damage.
   - *Taunt* fires on the current target whenever it peels onto any non-tank member (not just healers/casters).

### 2. Arms / Fury (DPS)
1. **Opener:** *Charge* from range when available.
2. **Rage Spenders:**
   - *Overpower* is wired for Arms (and via the Protection stance-dance); Fury has no Overpower wiring, and the dodge window is core spell data.
   - *Mortal Strike* (Arms) or *Bloodthirst* (Fury) on cooldown, plus the instant-*Slam* proc, *Rend* upkeep, and the *Master Strike* weapon nuke.
   - *Whirlwind* is used on cooldown above 20% target health; 2+ nearby targets only raises its priority.
   - Cooldowns: Fury fires *Death Wish* and *Recklessness* as boosts; Arms fires *Recklessness* (*Death Wish* exists in Arms only as a fallback alternative on the berserker-rage node).
3. **Execute Phase:** Below 20% enemy health, *Execute* becomes highest priority, consuming all available rage.

---

## Turtle WoW 1.18.1 Custom Content

- **Master Strike (Spell ID 54023, per game data):**
  - Custom Turtle WoW weapon-dispatched strike (30s cooldown, 20 rage, per game data).
  - Wired into both Arms and Fury combat strategies as a high-damage burst nuke whenever a main-hand weapon is equipped (core enforces cooldown/rage/weapon rules).
- **Defensive Tactics (Spell ID 51606, per game data):**
  - Passive talent providing threat aura benefits while wielding a shield in Battle or Berserker stances, per game data. It needs no bot wiring, so no `ai/` references exist.

---

## Utility & Interrupts

- **Interrupts:** Protection: *Shield Bash* (+enemy-healer row) · Fury: *Pummel* (auto-Berserker) · Arms/Fury base: *Pummel* first, *Shield Bash* as the no-stance-dance fallback (both at ACTION_INTERRUPT, plus enemy-healer rows). Pummel stance-dances via its berserker-stance node; shield bash has no stance gate, so one of the two always fires without per-cast dancing.
- **Shouts:** Automatically maintains *Battle Shout* on party members (all specs); only Protection wires *Demoralizing Shout*. *Intimidating Shout* (AoE fear) never fires inside a dungeon or raid, or for a bot grouped with a real player (a feared mob pulls neighbouring packs); outdoors with no master to disrupt, a critical-health bot may still shout to make space.

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots ships with pre-configured talent progressions at 5-level intervals (levels 10–60) designed for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Fury** | `1.0` | Melee DPS | `17 / 34 / 0` | Death Wish (30), Flurry (35), Bloodthirst (40), Ravager (-3s Whirlwind CD, per game data), 17 Arms dip for Deep Wounds 3/3 + Impale 2/2 (+20% crit damage bonus, per game data). |
| **Protection** | `1.1` | Tank | `7 / 0 / 44` | Shield Slam (40), Concussion Blow (50), Gag Order (silence on Shield Bash / dispel on Shield Slam, per game data), Defensive Tactics, Tactical Mastery 4/5. |
| **Arms** | `1.2` | Melee DPS | `40 / 11 / 0` | Sweeping Strikes (30), Mortal Strike (40), Master Strike (20 rage, per game data), Boundless Anger, 11 Fury dip for Cruelty 5/5 + Piercing Howl. |

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

