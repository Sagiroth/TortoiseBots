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

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots configures validated 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Holy** | `5.0` | Healer | `14 / 37 / 0` | Spirit of Redemption (35), Ascendance (45), Spiritual Guidance, Inspiration, Inner Focus dip. |
| **Shadow** | `5.1` | Ranged DPS | `20 / 0 / 31` | Mind Flay (20), Shadowform (40), Vampiric Touch + Vampiric Embrace, Meditation in-combat mana regen. |
| **Discipline** | `5.2` | Healer / Support | `40 / 11 / 0` | Resurgent Shield, Enlighten, Chastise (50), Force of Will, Inner Focus, Meditation, Holy dip. |

### Leveling Milestones & Progression Rationale

- **Holy (`5.0`):**
  - *Levels 10–35:* Throughput foundation: *Improved Renew* (3/3), *Holy Focus* (2/2), *Divine Fury* (5/5 cast time reduction), *Inspiration* (3/3 physical armor on crit heal), *Improved Healing* (3/3), and *Spirit of Redemption* (35).
  - *Levels 35–45:* *Spiritual Guidance* (5/5 Spirit-to-spellpower scaling), *Spiritual Healing* (5/5), and *Ascendance* (45 throughput capstone).
  - *Levels 45–60:* Transitions into Discipline for *Wand Specialization* (2/2), *Mental Agility* (5/5 instant spell cost reduction), *Improved Power Word: Fortitude* (2/2), and *Inner Focus* (60 free crit cooldown).
- **Shadow (`5.1`):**
  - *Levels 10–40:* Shadow ramping: *Improved Mind Blast* (5/5), *Shadow Focus* (5/5 hit cap), *Mind Flay* (20 signature channel), *Shadow Reach* (2/2), *Shadow Weaving* (5/5 Shadow vulnerability), *Vampiric Embrace* (30), and *Shadowform* (40).
  - *Levels 40–50:* *Vampiric Touch* (2/2 mana battery), *Darkness* (5/5 shadow damage).
  - *Levels 50–60:* Discipline mana sustainability dip (*Mental Agility* 5/5 + *Inner Focus* + *Improved Power Word: Shield* 3/3 + *Meditation* 3/3 for 15% in-combat mana regeneration).
- **Discipline (`5.2`):**
  - *Levels 10–35:* Mitigation and shielding core: *Silent Resolve* (5/5 threat reduction), *Unbreakable Will* (5/5 stun/fear resistance), *Inner Focus* (1/1), *Improved Power Word: Shield* (3/3), *Meditation* (3/3), and *Searing Light* (3/3 Smite damage).
  - *Levels 35–50:* *Mental Strength* (3/3 max mana), *Enlighten* (45), *Resurgent Shield* (1/1 mana return on shield absorb), and *Chastise* (50 disorient CC).
  - *Levels 50–60:* *Force of Will* (5/5 crit/damage) and 11 Holy points into *Improved Renew* (3/3), *Holy Focus* (2/2), and *Divinity* (5/5) to operate as a capable party healer.

