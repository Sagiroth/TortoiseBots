---
id: classes-priest
title: Priest Bot Guide
category: classes
summary: Complete guide to Priest bot AI, healing ladders, Power Word Shield rules, threat drops, Shadow DPS, and Turtle WoW custom spells.
tags: [classes, priest, healer, dps, ranged]
relates_to:
  - classes-overview
---

# Priest Bot Guide

Priests provide comprehensive healing coverage through layered spell ladders, damage absorption shields, party dispels, and viable ranged Shadow DPS.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Holy** | Healer | Maximum throughput healing, group AoE heals, and spirit scaling. |
| **Discipline** | Healer / Support | Damage absorption shielding, mana efficiency, and support damage. |
| **Shadow** | Ranged DPS | Periodic Shadow damage, Mind Flay channels, and Vampiric Embrace party healing. |

---

## What the bot does in combat

### The Priest Healing Ladder
Priests select healing spells based on defined health bands (bands follow [Combat settings](../server/settings/combat.md)):
- **Critical (<20%):** *Power Word: Shield* + *Flash Heal* (missing *Flash Heal* rows fall through to *Greater Heal* / *Heal* / *Renew*).
- **Low (20%–50%):** *Power Word: Shield* + *Greater Heal* (+ *Heal* / *Lesser Heal* fallbacks at low level). The same heavy-damage and low-mana pace rule as the medium band applies here, so the pick never downgrades from fast *Flash Heal* to a slow cast as the bar crosses the band line; loose mobs on non-tanks keep *Flash Heal* ahead.
- **Medium (50%–70%):** *Power Word: Shield* + *Greater Heal* / *Heal* / *Lesser Heal* by deficit, *Renew* for light wounds. Heavy incoming damage (the bar would leave the band before a slow cast lands) switches the pick to fast *Flash Heal* while the healer's mana is comfortable; a low-mana healer keeps the efficient heals unless the member is about to fall into the critical band.
- **Almost-Full (70%–90%):** *Renew* (Discipline shields first).
- **Multiple Injured:** *Prayer of Healing* when 2 or more members in 30 yards are medium-hurt.

Direct heals are downranked to match target wounds per [shared downranking rules](overview.md#spell-downranking), using the lowest rank covering missing health. Full ranks are enforced whenever the target falls at or below 20% health — or when heavy incoming damage would push the target below it before the cast lands. *Desperate Prayer* serves as a self-only emergency heal.

### Power Word: Shield & Weakened Soul Refusal
The bot verifies that the target does not have the *Weakened Soul* debuff before casting *Power Word: Shield*. If present, the shield is skipped in favor of direct heals.

### Threat Drops & Loose Adds
- **Fade:** Casts *Fade* at high priority when accumulating high threat without holding aggro, or immediately when attacked by a mob in a group.
- **Loose Add Priority:** If loose mobs attack non-tanks, Holy and Discipline priests prioritize fast *Flash Heal* casts over slow *Greater Heal* casts to prevent drawing further aggro.

### Shadow DPS Rotation
1. Maintains *Shadowform*.
2. Keeps *Vampiric Embrace* active on primary targets to return party healing.
3. Applies and maintains *Shadow Word: Pain* (undead priests also keep *Devouring Plague*).
4. Casts *Mind Blast* on cooldown.
5. Channels *Mind Flay* as primary filler, stacking *Shadow Weaving* where talented.
6. Casts *Silence* to interrupt dangerous enemy spellcasters and healers.

<details>
<summary>Healer off-spec damage and wand stops</summary>

- **Off-spec DPS:** Enabled by default via the `offdps` strategy per [healer off-DPS rules](overview.md#healer-off-dps-offdps), casting *Shadow Word: Pain*, *Holy Fire*, *Smite*, *Starshards*, or *Mind Blast* (*Holy Nova* into packed melee) against engaged mobs.
- **Wand Stops:** Because wand shots incur global cooldown on Turtle WoW, the `wand blocks cast` trigger stops active wanding as soon as a party member requires healing or crowd control.

</details>

---

## Turtle WoW custom spells

- **Chastise (Spell ID 51478):** Custom talent providing a ranged disorient interrupt, locking enemy casters at high priority.
- **Ascendance (Spell ID 52962):** Holy capstone talent providing an emergency crowd control purge and massive healing throughput boost during intense phases.

---

## Utility & interrupts

- **Party Buffs:** Maintains *Power Word: Fortitude*, *Divine Spirit*, and *Shadow Protection*. Replaces single-target casts with group prayers (*Prayer of Fortitude*, etc.) when trained and reagents are stocked.
- **Fear Ward:** Casts *Fear Ward* on the party tank first to prevent fear-based wipes, re-applying once the cooldown clears.
- **Dispels:** Uses *Dispel Magic* to clear magic debuffs on allies, and *Cure Disease* on infected party members.
- **Crowd Control:** Casts *Shackle Undead* on assigned undead CC marks. *Psychic Scream* is restricted to PvP or solo open-world emergencies; it is never used in dungeons or raids to avoid pulling neighbouring packs.
- **Resurrection:** Revives fallen allies out of combat with *Resurrection* (dropping *Shadowform* first).

---

## Talent builds

TortoiseBots provides pre-configured talent progressions at 5-level checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Holy** | `5.0` | Healer | `14 / 37 / 0` | Spirit of Redemption, Ascendance, Spiritual Guidance, Inspiration, Inner Focus dip. |
| **Shadow** | `5.1` | Ranged DPS | `20 / 0 / 31` | Mind Flay, Shadowform, Vampiric Touch preset (never cast by the bot), Vampiric Embrace, Meditation dip. |
| **Discipline** | `5.2` | Healer / Support | `40 / 11 / 0` | Resurgent Shield, Enlighten, Chastise, Force of Will, Inner Focus, Holy dip. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Holy (`5.0`):**
  - *Levels 10–35:* Takes *Improved Renew* (3/3), *Holy Focus* (2/2), *Divine Fury* (5/5), *Inspiration* (3/3), *Improved Healing* (3/3), and *Spirit of Redemption* (35).
  - *Levels 35–45:* Takes *Spiritual Guidance* (5/5), *Spiritual Healing* (5/5), and *Ascendance* (45).
  - *Levels 45–60:* Dips into Discipline for *Mental Agility* (5/5), *Improved Power Word: Fortitude* (2/2), and *Inner Focus* (60).
- **Shadow (`5.1`):**
  - *Levels 10–40:* Takes *Improved Mind Blast* (5/5), *Shadow Focus* (5/5), *Mind Flay* (20), *Shadow Reach* (2/2), *Shadow Weaving* (5/5), *Vampiric Embrace* (30), and *Shadowform* (40).
  - *Levels 40–50:* Takes *Vampiric Touch* (2/2) and *Darkness* (5/5).
  - *Levels 50–60:* Dips into Discipline for *Mental Agility* (5/5), *Inner Focus*, *Improved Power Word: Shield* (3/3), and *Meditation* (3/3).
- **Discipline (`5.2`):**
  - *Levels 10–35:* Takes *Silent Resolve* (5/5), *Unbreakable Will* (5/5), *Inner Focus* (1/1), *Improved Power Word: Shield* (3/3), *Meditation* (3/3), and *Searing Light* (3/3).
  - *Levels 35–50:* Takes *Mental Strength* (3/3), *Enlighten* (45), *Resurgent Shield* (1/1), and *Chastise* (50).
  - *Levels 50–60:* Takes *Force of Will* (5/5) and 11 points in Holy (*Improved Renew* 3/3 + *Holy Focus* 2/2 + *Divinity* 5/5).

</details>

---

## Class settings

There are no class-specific configuration flags for Priests. General healing thresholds, mana reserves, and reaction times are configured in [Combat settings](../server/settings/combat.md).


## Local companion behavior

### Experimental Holy combat healing correction

Holy combat low/medium-health triggers now prefer Greater Heal and Flash Heal over Heal and Lesser Heal. Explicit lower-level candidates remain for unlearned or unusable spells. Critical-health Shield/Flash Heal priorities are unchanged. The non-combat low/medium ladder now uses the same Greater Heal / Flash Heal preference, so priests healing before their combat-engine transition retain the same direct-heal priorities. This corrects the captured raid Lesser Heal selection; it does not address CPU stalls or buff retries. Runtime confirmation requires a new combat log.

Patchwerk preventive soaker shields yield whenever a friendly, living tank in
combat on the same map is below the configured almost-full health threshold
(default 90%), including assigned main/soaker tanks. This check is independent
of the chosen patient, range, LOS and pending heals. Preventive shields also
rank below fast tank heals; healthy-soaker shielding resumes at the safe line.
