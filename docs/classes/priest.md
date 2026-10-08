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

The bot selects healing spells from static health bands (defaults: Critical 20 / Low 50 / Medium 70 / Almost-Full 90):

```text
Ally Health < 20% (critical) ──► PW:Shield + Flash Heal (Holy)
Ally Health 20%-50% (low)    ──► PW:Shield + Heal / Lesser Heal (Holy; Greater Heal in Discipline/off-spec ladders)
Ally Health 50%-70% (medium) ──► Heal / Lesser Heal
Ally Health 70%-90% (almost) ──► Renew
Multiple Injured              ──► Prayer of Healing (Party AoE heal)
```
*Desperate Prayer* is a self-only emergency heal and never lands on allies. A heal is never skipped for mana reasons on a target at or below the low-health line: danger always outbids efficiency. Above the medium line the bot refuses oversized or merely average-efficiency heals (a 50%-bar *Heal* on an 85% target waits for a cheaper window), and while its own mana sits below the medium-mana line it refuses mana-hungry heals (*Flash Heal*) in favor of efficient ones (*Renew*, *Power Word: Shield*). Tanks count the expected heal at two-thirds (bigger bars).

### Healer Off-Spec Damage & Wand
A grouped Holy priest only damages while **nobody in the party is below 90% health** and its mana is comfortable (85% reserve on easy pulls, 65% on normal ones, the medium-mana line on hard ones). Then it uses *Shadow Word: Pain*, *Holy Fire*, *Smite*, *Starshards* or *Mind Blast* at the lowest priority, so every heal outbids it, and *Holy Nova* when a pack stands in melee range. When the party is healthy but mana is not, it wands the target instead. A solo priest damages freely.

### Power Word: Shield & Weakened Soul Refusal
The bot checks for the *Weakened Soul* debuff (6788) before attempting *Power Word: Shield*. If the target already has Weakened Soul, the shield is skipped in favor of a direct heal, preventing wasted cast attempts.

### Threat Drop
Climbing toward the top of the threat table without holding aggro fires *Fade* at high priority in any group (solo priests have nobody to shed to, so they keep healing instead); raids keep the additional emergency-slot fade.
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

- **Chastise (Spell ID 51478, per game data):**
  - Custom Turtle WoW talent. Hostile Chastise is integrated as a ranged CC disorient at `INTERRUPT` priority, stopping enemy spellcasters in their tracks.
- **Ascendance (Spell ID 52962, per game data):**
  - Holy capstone talent providing an emergency CC purge and massive healing throughput boost during intense raid/dungeon phases, per game data.

---

## Buffs & Crowd Control

- **Party Buffs:** Maintains *Power Word: Fortitude* (Stamina), *Divine Spirit* (Spirit), and *Shadow Protection*. Once known, the group versions (*Prayer of Fortitude*, *Prayer of Spirit*, *Prayer of Shadow Protection*) replace the per-member single-target casts once known, trained and stocked and at least three same-map members lack both auras, and the bot picks a member that still lacks both. Buffs expiring within 15 s count as missing, so they are refreshed before they drop (issue #468). Single-target *Power Word: Fortitude* is also allowed in combat at the lowest priority, so a priest following a master who chain-pulls still buffs the party in the quiet moments of a fight. Out of combat the bot also casts *Resurrection* (removing *Shadowform* first) and uses *Fade* for threat management (raid medium-threat and after *Psychic Scream*). *Prayer of Fortitude* has no trainer on this realm: a hired priest learns rank 1 at level 48 and rank 2 at 60, and the reagent seeding follows those rank levels (Holy Candle 17028 at levels 48–59, Sacred Candle 17029 from 60 — a level-60 priest always casts the highest known rank, so it never spends a Holy Candle). With several priests in one party, a short shared *buff claim* keeps them from duplicating each other: while one priest's cast is in flight the others stand down and wait for the aura instead of casting the same buff on the same member (issue #378).
- **Buff scheduling:** Out of combat, catching up to the master runs below the party buffs, so a pending buff whose target is in range wins the tick and follow resumes next tick (far behind, the buff target is out of range and follow still wins). Upkeep buffs wait for 40% mana (70% for charge buffs like *Inner Fire*); with a real player master the floors drop to 20%/40% — the master watches the bar, and heals keep their own reserve. A failed buff attempt no longer starts the retry window or the duplicate-cast claim; only a cast that actually starts does.
- **Dispels:** Proactively uses *Dispel Magic* on self and allies (to clear magic debuffs), and *Cure Disease* on diseased allies. The enemy-target dispel action is registered but has no trigger, so the bot never offensively dispels.
- **Crowd Control:**
  - Casts *Shackle Undead* when assigned CC on Undead targets.
  - *Psychic Scream* (AoE fear) is PvP-only: it is never cast in PvE groups, and never inside a dungeon or raid (a feared mob pulls neighbouring packs). Outdoors with no master to disrupt, a surrounded bot may still scream to make space, then follows up with *Fade* (not fleeing).

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots configures validated 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Holy** | `5.0` | Healer | `14 / 37 / 0` | Spirit of Redemption (35), Ascendance (45), Spiritual Guidance, Inspiration, Inner Focus dip. |
| **Shadow** | `5.1` | Ranged DPS | `20 / 0 / 31` | Mind Flay (20), Shadowform (40), Vampiric Touch (preset talent, never cast by the bot) + Vampiric Embrace, Meditation in-combat mana regen. |
| **Discipline** | `5.2` | Healer / Support | `40 / 11 / 0` | Resurgent Shield, Enlighten (preset talent, no bot wiring), Chastise (50), Force of Will, Inner Focus, Meditation, Holy dip. |

### Leveling Milestones & Progression Rationale

- **Holy (`5.0`):**
  - *Levels 10–35:* Throughput foundation: *Improved Renew* (3/3), *Holy Focus* (2/2), *Divine Fury* (5/5 cast time reduction), *Inspiration* (3/3 physical armor on crit heal), *Improved Healing* (3/3), and *Spirit of Redemption* (35).
  - *Levels 35–45:* *Spiritual Guidance* (5/5 Spirit-to-spellpower scaling), *Spiritual Healing* (5/5), and *Ascendance* (45 throughput capstone).
  - *Levels 45–60:* Transitions into Discipline for *Wand Specialization* (2/2), *Mental Agility* (5/5 instant spell cost reduction), *Improved Power Word: Fortitude* (2/2), and *Inner Focus* (60 free crit cooldown).
- **Shadow (`5.1`):**
  - *Levels 10–40:* Shadow ramping: *Improved Mind Blast* (5/5), *Shadow Focus* (5/5 hit cap), *Mind Flay* (20 signature channel), *Shadow Reach* (2/2), *Shadow Weaving* (5/5 Shadow vulnerability), *Vampiric Embrace* (30), and *Shadowform* (40).
  - *Levels 40–50:* *Vampiric Touch* (2/2 mana battery in the preset, but no bot action/trigger ever casts it), *Darkness* (5/5 shadow damage), per game data.
  - *Levels 50–60:* Discipline mana sustainability dip (*Mental Agility* 5/5 + *Inner Focus* + *Improved Power Word: Shield* 3/3 + *Meditation* 3/3 for 15% in-combat mana regeneration), per game data.
- **Discipline (`5.2`):**
  - *Levels 10–35:* Mitigation and shielding core: *Silent Resolve* (5/5 threat reduction), *Unbreakable Will* (5/5 stun/fear resistance), *Inner Focus* (1/1), *Improved Power Word: Shield* (3/3), *Meditation* (3/3), and *Searing Light* (3/3 Smite damage).
  - *Levels 35–50:* *Mental Strength* (3/3 max mana), *Enlighten* (45), *Resurgent Shield* (1/1 mana return on shield absorb), and *Chastise* (50 disorient CC).
  - *Levels 50–60:* *Force of Will* (5/5 crit/damage) and 11 Holy points into *Improved Renew* (3/3), *Holy Focus* (2/2), and *Divinity* (5/5) to operate as a capable party healer.

