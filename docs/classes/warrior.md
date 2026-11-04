---
id: classes-warrior
title: Warrior Bot Guide
category: classes
summary: Complete guide to Warrior bot AI, stance dancing, tanking rotations, melee DPS, shouts, and Turtle WoW custom abilities.
tags: [classes, warrior, tank, dps, melee]
relates_to:
  - classes-overview
---

# Warrior Bot Guide

Warriors serve as primary dungeon tanks or powerful melee DPS, managing dynamic stance switches, rage expenditure, interrupt timing, and defensive cooldowns.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Protection** | Tank | Defensive Stance holding, Sunder Armor stacking, Shield Slam, and peel taunts. |
| **Arms** | Melee DPS | Battle Stance two-handed strikes, Mortal Strike, Overpower procs, and cleaves. |
| **Fury** | Melee DPS | Berserker Stance dual-wielding, Bloodthirst, Whirlwind, and Execute bursts. |

---

## What the bot does in combat

### Protection (Tank)
- **Pull & Engagement:** Opens with *Charge* on distant targets before swapping to Defensive Stance. Ranged pulls using bows, guns, or thrown weapons fire when line of sight or distance requires it.
- **Threat Generation:**
  - Maintains *Shield Block* on cooldown, which opens up *Revenge*.
  - Stacks *Sunder Armor* to 5 stacks, refreshing before expiry.
  - Casts *Shield Slam* on cooldown (or when procced by Improved Shield Slam).
  - Uses *Thunder Clap* against 2 or more melee attackers.
  - Opens with *Shield Slam* when it is ready and treats *Sunder Armor* as the filler while the stack builds.
  - On an opt-in Patchwerk tank role (`patchwerk main` or `patchwerk soak`, see [Patchwerk roles](../play/dungeons-and-raids.md#patchwerk-opt-in-roles)) it casts *Shield Slam* whenever it can and keeps using *Sunder Armor* past five stacks while Shield Slam and Revenge are unavailable, to hold a threat lead.
- **Mitigation & Taunts:**
  - Activates *Shield Wall* at low health (20–50%) and holds *Last Stand* for critical health emergencies (below 20%). Both lines are the `CriticalHealth` (20) and `LowHealth` (50) defaults.
  - Taunts with *Taunt* immediately if any attacker peels onto non-tank party members.
  - Intervenes to intercept attacks aimed at focused group members.
  - Falls back to *Mocking Blow* (from Battle Stance, back to Defensive after) when *Taunt* is on cooldown and the mob left the tank.
  - Saves *Challenging Shout* for real emergencies: 3 or more loose mobs on non-tanks, not for a pack it already holds.

### Arms & Fury (Melee DPS)
- **Arms:**
  - Fights in Battle Stance, maintaining *Rend*, casting *Mortal Strike* on cooldown and *Overpower* on dodges.
  - Cleaves packs with *Sweeping Strikes* and *Whirlwind* (dancing to Berserker Stance for Whirlwind). In raids, single-target Whirlwind does not dance unless already in Berserker Stance.
  - Dumps rage with *Master Strike* and uses *Heroic Strike* once rage reaches 40.
- **Fury:**
  - Fights in Berserker Stance, centering rage spend on *Bloodthirst* and *Whirlwind*. The build takes Dual-Wield Specialization.
  - Activates *Death Wish* and *Recklessness* in hard fights; uses *Retaliation* at high health against 2 or more melee attackers.
  - Rests in Berserker Stance between fights, so there is no Battle/Berserker swap per pull.
- **Snaring:** *Piercing Howl* only lands on a mob inside its own 10-yard radius (in a raid, only a runner beside the warrior); a refused Howl falls back to *Hamstring* in a dungeon, and both DPS specs skip it in a raid.
- **Execute Phase:** When targets drop below 20% health, *Execute* becomes top priority, consuming all available rage.
- **Finishing Runners:** Warriors in Berserker Stance with *Intercept* off cooldown charge after fleeing enemies within 25 yards per [shared runner rules](overview.md#shared-bot-behaviors).

---

## Turtle WoW custom spells

- **Master Strike:** Weapon-dispatched burst strike wired into Arms and Fury rotations as a high-damage physical nuke.
- **Defensive Tactics:** Passive Protection talent granting threat aura benefits while wielding a shield in Battle or Berserker Stance.

---

## Utility & interrupts

- **Interrupts:**
  - *Protection:* Relies strictly on *Shield Bash* to remain in Defensive Stance.
  - *Fury:* Uses *Pummel* (in Berserker Stance).
  - *Arms:* Uses *Pummel* first, falling back to *Shield Bash* without stance dancing.
  - Interrupts only fire on active cast bars or channels; delayed instants never trigger interrupts.
- **Shouts:** Maintains *Battle Shout* on party members (skipping if an equal or stronger *Blessing of Might* is active, or below 10 rage). Protection maintains *Demoralizing Shout*. *Intimidating Shout* is off in dungeons and raids unless the group opts in with `co +aoe fear`.
- **Rage gates:** *Disarm* is only cast on a target that carries a weapon and only with the 20 rage it costs; *Bloodrage* is held while its cooldown runs, below medium health (70%), or below 20 rage to spend. Unusable rows are skipped, so low-rage ticks go to *Sunder Armor* and *Revenge* instead.
- **Hold Ground Opt-In:** When [PoolMeleeHoldGround](../server/settings/performance.md#aiplayerbotpoolmeleeholdground) is enabled, pool warriors fight through low-health emergencies using *Shield Wall* and *Last Stand* rather than fleeing.

---

## Talent builds

Bots learn pre-set talent orders at 5-level checkpoints (levels 10–60):

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Fury** | `1.0` | Melee DPS | `17 / 34 / 0` | Death Wish, Flurry, Bloodthirst, Ravager, 17 Arms dip for Deep Wounds + Impale. |
| **Protection** | `1.1` | Tank | `7 / 0 / 44` | Shield Slam, Concussion Blow, Gag Order, Defensive Tactics, Tactical Mastery dip. |
| **Arms** | `1.2` | Melee DPS | `40 / 11 / 0` | Sweeping Strikes, Mortal Strike, Master Strike, Boundless Anger, 11 Fury dip. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Fury (`1.0`):**
  - *Levels 10–30:* Takes *Cruelty* (5/5), *Unbridled Wrath* (5/5), *Dual Wield Specialization* (5/5), *Piercing Howl* (1/1), *Improved Shouts* (4/5), and *Death Wish* (30).
  - *Levels 30–40:* Takes *Blood Craze* (2/2), *Flurry* (5/5) and *Bloodthirst* (40).
  - *Levels 40–60:* Takes *Ravager* (3/3) and *Improved Execute* (2/2), then dips into Arms for *Tactical Mastery* (5/5), *Rend* (2/2), *Deflection* (3/3), *Improved Overpower* (2/2), *Deep Wounds* (3/3), and *Impale* (2/2).
- **Protection (`1.1`):**
  - *Levels 10–30:* Takes *Shield Specialization* (5/5), *Anticipation* (3/3), *Toughness* (5/5), *Last Stand* (20), *Improved Taunt* (2/2), and *Improved Revenge* (3/3).
  - *Levels 30–50:* Takes *Defiance* (5/5), *Gag Order* (2/2), *Shield Slam* (40), *Improved Shield Slam* (2/2), *Reprisal* (2/2), and *Concussion Blow* (50).
  - *Levels 50–60:* Takes *Defensive Tactics* (3/3) and 7 points in Arms (*Tactical Mastery* 4/5 + *Improved Heroic Strike* 3/3).
- **Arms (`1.2`):**
  - *Levels 10–40:* Takes *Deflection* (5/5), *Deep Wounds* (3/3), *Two-Handed Weapon Specialization* (3/3), *Impale* (2/2), *Sweeping Strikes* (30), and *Mortal Strike* (40).
  - *Levels 40–60:* Takes *Boundless Anger* (3/3) and *Master of Arms* (5/5), followed by an 11-point Fury dip into *Cruelty* (5/5), *Unbridled Wrath* (5/5), and *Piercing Howl* (1/1).

</details>

---

## Class settings

There are no class-specific configuration flags for Warriors. General combat, tanking, and pull settings are configured in [Combat settings](../server/settings/combat.md).
