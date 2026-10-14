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

A forced healer role (`.bot role <Name> healer` or a dungeon-finder heal slot) keeps a Discipline priest on `discipline`; Holy stays `holy`, and a Shadow priest switches to `holy` (the priest `heal` strategy alias). A forced `dps` role switches Holy and Discipline to `shadow`.

---

## The Priest Healing Ladder

The bot selects healing spells from static health bands (defaults: Critical 20 / Low 50 / Medium 70 / Almost-Full 90):

```text
Ally Health < 20% (critical) ──► PW:Shield + Flash Heal
Ally Health 20%-50% (low)    ──► PW:Shield + Flash Heal + Greater Heal + Heal / Lesser Heal
Ally Health 50%-70% (medium) ──► Greater Heal + Heal / Lesser Heal (Discipline shields first)
Ally Health 70%-90% (almost) ──► Renew (Discipline shields first)
Multiple Injured              ──► Prayer of Healing (Party AoE heal)
```
The ally is the party member with the lowest health percent (shared by every healer class, as in mod-playerbots): a cloth member at 60% goes before a tank at 76%, even though the tank is missing more hit points. A grouped healer outside raids also holds its ground instead of stepping away from a mob in melee range that is attacking someone else while any groupmate is below the medium line, unless its own health is below the low line. A mob attacking the healer itself still makes it back off, and raid healers keep their spacing.
Group heals fire on headcount, not fixed pairs: at least 5 party members within 30 yards, then 3 hurt in a 5-man scaling up to 15 hurt in a full raid (medium band for Prayer of Healing, almost-full band for shields).
*Desperate Prayer* is a self-only emergency heal and never lands on allies. A heal is never skipped for mana reasons on a target at or below the low-health line: danger always outbids efficiency. Above the medium line the bot refuses oversized or merely average-efficiency heals (a 50%-bar *Heal* on an 85% target waits for a cheaper window), and while its own mana sits below the medium-mana line it refuses mana-hungry heals (*Flash Heal*) in favor of efficient ones (*Renew*, *Power Word: Shield*). Tanks count the expected heal at two-thirds (bigger bars).
*Desperate Prayer* is a self-only emergency heal and never lands on allies. At medium mana or below with someone hurt, Holy and Discipline pre-cast *Inner Focus* after shielding so the next direct heal crits (3 min cooldown per DBC data, so it only fires when a heal target exists; shields can't crit, hence the shield-first order). Discipline also heals out of combat now (shield-first ladder plus *Renew*); previously it only moved to hurt members. A heal is never skipped for mana reasons on a target at or below the low-health line: danger always outbids efficiency. Above the medium line the bot refuses oversized or merely average-efficiency heals (a 50%-bar *Heal* on an 85% target waits for a cheaper window), and while its own mana sits below the medium-mana line it refuses mana-hungry heals (*Flash Heal*) in favor of efficient ones (*Renew*, *Power Word: Shield*). Tanks count the expected heal at two-thirds (bigger bars).

### Healer Off-Spec Damage & Wand
Holy and Discipline bots deal off-spec damage by default (the `offdps` strategy ships on; turn it off per bot with `.bot strategy -offdps`). A grouped healer only damages while **nobody in the party is below 90% health** and its mana is above a reserve that depends on the fight (`HealerDpsManaFloor` in `HealerManaPolicy.h`): 85% when the enemies clearly outweigh the party, 70% on an even or easy fight (a full party on one or two elites is easy). The 70% deliberately differs from mod-playerbots' 65% / 40%, so a healer does not nuke itself dry before the damage comes. Then it uses *Shadow Word: Pain*, *Holy Fire*, *Smite*, *Starshards* or *Mind Blast* at the lowest priority, so every heal outbids it, and *Holy Nova* when a pack stands in melee range. When the party is healthy but mana is not, it wands the target instead. A grouped healer's damage and wand only go on a mob that is **already fighting the party** (another member or the priest itself): it never opens on a fresh mob (before, it wanded a mob the tank had not touched yet while the commander held the party back for the opener, took it, and spent the next seconds on *Fade* instead of heals). The *Shoot* action itself asks the same healer rule (`HealerMayWand` in `WandPolicy.h`), so a wand order queued a moment earlier, while the party was still healthy, can no longer start the wand after somebody drops below 90% (queued actions live up to `ExpireActionTime`, 5 s). Below the 5% `no mana` line the wand is all a healer has left, so it may then wand an engaged mob whoever is hurt (a mob on the priest itself included). With no other group member within `SightDistance` (the mod-playerbots escape) the priest fights like a solo bot, and a `cmd attack <guid> <Priest>` that names it makes it attack that mob whatever the rule says (a party-wide `cmd attack` still leaves healers healing). A solo priest damages freely. Pool Holy and Discipline priests keep their own spec in combat; only Shadow-talented priests run the Shadow rotation (*Mind Flay* filler, *Shadowform* upkeep).

### Wand Stops for Heals and Nukes (Shoot, Spell ID 5019)
On Turtle every wand shot puts the global cooldown on the shooter for the wand's whole swing, and the next shot renews it, so a running wand keeps every heal and nuke "not ready". Before, a priest that started wanding (the healer wand row above, the shadow `no mana` → *Shoot* row, or *Smite* falling back to the wand after a GCD tick) kept wanding until it moved: heals for a dropping party member or for itself never went out, and a shadow priest wanded at full mana. Now the `wand blocks cast` trigger stops the wand (`stop wand`, above every heal row and *Fade*: those all fail while the wand runs, and in the old slot below the HIGH tier `dps assist` ran first, succeeded and ended the tick, so the stop never came while the tank died) while a wanted spell is held back by that GCD alone, waits out the last shot's GCD and casts. A grouped Holy or Discipline priest also stops the wand as soon as the healer rule above no longer allows it (a party member below 90%, the priest below the low line, or the mob no longer fighting the party), whatever the spell probes say:
- **Heals:** the priest itself below the low-health line (every spec); for Holy and Discipline a party member below the medium line, or below the almost-full line without *Renew*; for Shadow with `offheal` a party member below the medium line. Below the low or medium line the probe is *Flash Heal* / *Lesser Heal*; between the medium and almost-full line it is *Renew*, the only heal that row casts. The wand stops even below the 5% `no mana` line as long as that heal is affordable (heal rows outrank that wand row).
- **Nukes:** Shadow always (*Mind Flay*, or *Smite* outside *Shadowform*); Holy and Discipline only with `offdps` and only while the off-spec damage condition above holds, so the healer's mana-saving wand keeps going.
- The wand keeps going with too little mana for any wanted spell, or (nukes only) below the 5% `no mana` line. It also keeps going while the priest is silenced or locked out of the spell's school, while the target is out of line of sight, and (nukes only) in a form that forbids the spell: the core reports the GCD before those checks, so the trigger reads them on the priest itself. For nukes only, the wand also keeps going while the `threat` strategy holds the nukes back (`high threat`: near the tank's threat on the target, or the tank has none there yet): *Smite* would fall back to the wand anyway. Heals are never threat-gated, so a wanted heal still stops the wand.
- **Shackle Undead:** with the `cc` strategy, the `wand blocks cc` trigger stops the wand (`stop wand`, just above the *Shackle Undead* row) while the priest has an undead CC target waiting within *Shackle Undead* range (its raid mark in a dungeon) and *Shackle Undead* is held back by that GCD alone. While that target waits, the priest does not start the wand at all. Same exceptions as the nukes (5% `no mana` line, silence, school lockout, wrong form, line of sight).
- The trigger is checked every 1-2 s, not every tick, because each probe builds a spell check.

### Power Word: Shield & Weakened Soul Refusal
The bot checks for the *Weakened Soul* debuff (6788) before attempting *Power Word: Shield*. If the target already has Weakened Soul, the shield is skipped in favor of a direct heal, preventing wasted cast attempts.

### Threat Drop
Climbing toward the top of the threat table without holding aggro fires *Fade* at high priority in any group (solo priests have nobody to shed to, so they keep healing instead); raids keep the additional emergency-slot fade. In a group, a mob that targets the priest makes it cast *Fade* before its next heal (above every heal row; once per 30 s cooldown), as approved by the owner on 2026-10-14.

While the tank does not hold the whole pack (two or more attackers on someone other than a tank), Holy and Discipline healers cast *Flash Heal* ahead of *Greater Heal* on low- and medium-health allies, and on themselves at low health. The slow, expensive cast was losing the race against the damage and drawing threat from every loose mob (Zul'Farrak hire run, 2026-10-14). *Greater Heal* stays the fallback when *Flash Heal* cannot be cast.
---

## Shadow DPS Rotation

1. Activates and maintains *Shadowform*.
2. Casts *Vampiric Embrace* to siphon damage into party healing.
3. Applies and maintains *Shadow Word: Pain* (spread across attackers when AoE is on, skipping a held CC mark while other enemies fight, plus *Devouring Plague* on attackers for undead priests).
4. Casts *Mind Blast* on cooldown.
5. Channels *Mind Flay* as the primary filler, falling back to *Smite* then the wand when mana runs dry. A priest with the *Shadow Weaving* talent also raises *Mind Flay* above the normal rotation while the target lacks the Shadow Weaving debuff (from any priest, at any stack count), to rebuild the stacks; without the talent that boost never fires.
6. Casts *Silence* to interrupt dangerous enemy casters, preferring enemy healers.

---

## Turtle WoW 1.18.1 Custom Content

- **Chastise (Spell ID 51478, per game data):**
  - Custom Turtle WoW talent. Hostile Chastise is integrated as a ranged CC disorient at `INTERRUPT` priority, stopping enemy spellcasters in their tracks.
- **Ascendance (Spell ID 52962, per game data):**
  - Holy capstone talent providing an emergency CC purge and massive healing throughput boost during intense raid/dungeon phases, per game data.

---

## Buffs & Crowd Control

- **Party Buffs:** Maintains *Power Word: Fortitude* (Stamina), *Divine Spirit* (Spirit), and *Shadow Protection*. Once known, the group versions (*Prayer of Fortitude*, *Prayer of Spirit*, *Prayer of Shadow Protection*) replace the per-member single-target casts once known, trained and stocked and at least three same-map members lack both auras, and the bot picks a member that still lacks both. Buffs expiring within 15 s count as missing, so they are refreshed before they drop (issue #468). Single-target *Power Word: Fortitude* and *Divine Spirit* are also allowed in combat at the lowest priority, so a priest following a master who chain-pulls still buffs the party in the quiet moments of a fight. Out of combat the bot also casts *Resurrection* (removing *Shadowform* first) and…
- **Buff scheduling:** Out of combat, catching up to the master runs below the party buffs, so a pending buff whose target is in range wins the tick and follow resumes next tick (far behind, the buff target is out of range and follow still wins). Upkeep buffs wait until the mana left after the cast stays at or above 40% (70% for charge buffs like *Inner Fire*); with a real player master the floors drop to 20%/40% — the master watches the bar, and heals keep their own reserve. The floor holds whenever the non-combat engine runs, even while the combat flag lingers after the kill, so a priest that ends a fight low drinks before it re-buffs the party instead of spending its last mana on *Fortitude* / *Shadow Protection*. A failed buff attempt no longer starts the retry window or the duplicate-cast claim; only a cast that actually starts does.
- **Dispels:** Proactively uses *Dispel Magic* on self and allies (to clear magic debuffs), and *Cure Disease* on diseased allies. The enemy-target dispel action is registered but has no trigger, so the bot never offensively dispels.
- **Fear Ward:** In combat the bot wards the party tank first — the tank row outranks the generic ward by relevance, so the tank keeps fear immunity in fear dungeons instead of the bot warding itself. Re-wards after the ward is consumed once the 30 s cooldown is ready.
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

