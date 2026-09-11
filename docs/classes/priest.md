---
id: class-priest
title: Priest Bot AI & Specs
category: classes
summary: Deep dive into Priest healing ladder, PW:Shield refusal rules, Shadow DPS, and custom Chastise/Ascendance abilities.
tags: [class, priest, healer, dps, ranged]
relates_to:
  - class-overview
  - guide-player-controls
---

# Priest Bot AI & Specs

Priests are the quintessential healers of Vanilla WoW, boasting an extensive healing ladder, damage shields, group dispels, and viable ranged Shadow DPS.

## Supported Specs & Roles

- **Holy (Healer):** Pure throughput healing with *Prayer of Healing*, *Heal*, *Greater Heal*, and *Renew*.
- **Discipline (Healer / Support):** Damage absorption via *Power Word: Shield*, *Inner Focus*, mana conservation, and support damage.
- **Shadow (Ranged DPS):** Shadowform damage dealer relying on *Shadow Word: Pain*, *Mind Flay*, *Mind Blast*, and *Vampiric Embrace*.

---

## The Priest Healing Ladder

The bot dynamically selects healing spells based on ally health percentage and damage velocity:

```text
Ally Health < 25%   ──► Flash Heal (Emergency) / Desperate Prayer / PW:Shield
Ally Health 25%-60% ──► Greater Heal / Heal (High efficiency throughput)
Ally Health 60%-85% ──► Renew / Lesser Heal (Maintenance)
Multiple Injured    ──► Prayer of Healing (Party AoE heal)
```

### Power Word: Shield & Weakened Soul Refusal
The bot checks for the *Weakened Soul* debuff (6788) before attempting *Power Word: Shield*. If the target already has Weakened Soul, the shield is skipped in favor of a direct heal, preventing wasted cast attempts.

---

## Shadow DPS Rotation

1. Activates and maintains *Shadowform*.
2. Casts *Vampiric Embrace* to siphon damage into party healing.
3. Applies and maintains *Shadow Word: Pain*.
4. Casts *Mind Blast* on cooldown.
5. Channels *Mind Flay* as the primary filler.
6. Casts *Silence* to interrupt dangerous enemy casters.

---

## Turtle WoW 1.18.1 Custom Content

- **Chastise (Spell ID 51478):**
  - Custom Turtle WoW talent. Hostile Chastise is integrated as a ranged CC disorient at `INTERRUPT` priority, stopping enemy spellcasters in their tracks.
- **Ascendance (Spell ID 52962):**
  - Holy capstone talent providing an emergency CC purge and massive healing throughput boost during intense raid/dungeon phases.
- **Enlighten (Spell ID 51476):**
  - Holy passive talent granting procs on Holy spell casts, fully handled by the core server and leveraged by bot heal frequency.

---

## Buffs & Crowd Control

- **Party Buffs:** Maintains *Power Word: Fortitude* (Stamina), *Divine Spirit* (Spirit), and *Shadow Protection*.
- **Dispels:** Proactively uses *Dispel Magic* on allies (to clear magic debuffs) and enemies (to strip shields/buffs), and *Cure Disease* on diseased allies.
- **Crowd Control:**
  - Casts *Shackle Undead* when assigned CC on Undead targets.
  - Casts *Psychic Scream* when overwhelmed by multiple melee attackers, then flees to safe casting distance.
