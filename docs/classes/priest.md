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
Priests select healing spells based on health bands (bands follow [Combat settings](../server/settings/combat.md); all lines below use the defaults Critical 20 / Low 50 / Medium 70 / Almost-Full 90):
- **Critical (below 20%):** *Power Word: Shield* + *Flash Heal* (missing *Flash Heal* rows fall through to *Greater Heal* / *Heal* / *Lesser Heal*).
- **Low (20–50%):** *Power Word: Shield* + *Greater Heal* (+ *Heal* / *Lesser Heal* fallbacks at low level). The same heavy-damage and low-mana pace rule as the medium band applies here, so the pick never downgrades from fast *Flash Heal* to a slow cast as the bar crosses the band line; loose mobs on non-tanks keep *Flash Heal* ahead.
- **Medium (50–70%):** *Power Word: Shield* + *Greater Heal* / *Heal* / *Lesser Heal* by deficit (Holy also uses *Renew* for light wounds). Heavy incoming damage (the bar would leave the band before a slow cast lands) switches the pick to fast *Flash Heal* while the healer's mana is comfortable; a low-mana healer keeps the efficient heals unless the member is about to fall into the critical band.
- **Almost-Full (70–90%):** *Renew* (Discipline shields first).
- **Multiple Injured:** *Prayer of Healing* when 3 or more members of the priest's own party within 30 yards are hurt at or below 70%. The spell only reaches the priest's party, so hurt raid members in other parties do not trigger it.

Direct heals are downranked to match target wounds per [shared downranking rules](overview.md#shared-bot-behaviors), using the lowest rank covering missing health. Full ranks are used at or below critical health (20% default) — or when heavy incoming damage would push the target below it before the cast lands. *Desperate Prayer* serves as a self-only emergency heal.

### Power Word: Shield & Weakened Soul Refusal
The bot verifies that the target does not have the *Weakened Soul* debuff before casting *Power Word: Shield*. If present, the shield is skipped in favor of direct heals. Above 20% health the shield is also held off a warrior or bear tank that is fighting, since absorbed hits give no rage and starve the tank's threat; the shield still lands as the save at or below 20%.

In a Patchwerk fight with the opt-in tank roles ([Patchwerk roles](../play/dungeons-and-raids.md#patchwerk-opt-in-roles)) a priest in range pre-shields an engaged soaker below critical heals while no tank or Patchwerk-assigned member sits below almost-full health (90%).

### Threat Drops & Loose Adds
- **Fade:** Casts *Fade* when climbing toward the tank's threat (60% or more) on a mob the tank holds, or immediately when attacked by a mob in a group (the attacked row runs at the highest priority; the climbing-threat row sits below heal rows).
- **Loose Add Priority:** If loose mobs attack non-tanks, Holy and Discipline priests prioritize fast *Flash Heal* casts over slow *Greater Heal* casts to avoid drawing further aggro.

### Shadow DPS Rotation
1. Maintains *Shadowform*.
2. Keeps *Vampiric Embrace* active on primary targets (party healing comes from the server spell effect).
3. Applies and maintains *Shadow Word: Pain* (plus *Devouring Plague* wherever known — an undead racial, gated by the spellbook).
4. Casts *Mind Blast* on cooldown.
5. Channels *Mind Flay* as primary filler, stacking *Shadow Weaving* where talented.
6. Casts *Silence* to interrupt dangerous enemy spellcasters and healers.
7. Off-heals wounded allies (*Power Word: Shield*, *Greater Heal*, *Renew* by wound band) per [off-healing rules](overview.md#shared-bot-behaviors).

<details>
<summary>Healer off-spec damage and wand stops</summary>

- **Off-spec DPS:** Enabled by default via the `offdps` strategy per [healer off-DPS rules](overview.md#shared-bot-behaviors), casting *Shadow Word: Pain*, *Holy Fire*, *Smite*, *Starshards* (only above 50% mana and beyond 15 yards), or *Mind Blast* (*Holy Nova* into packed melee) against engaged mobs.
- **Wand Stops:** Because wand shots incur global cooldown on Turtle WoW, the `wand blocks cast` trigger stops active wanding as soon as a heal or a nuke needs the cast window. A separate `wand blocks cc` trigger stops wanding while a *Shackle Undead* cast is queued.

</details>

---

## Turtle WoW custom spells

- **Chastise:** Custom talent providing a ranged snare at interrupt priority, locking enemy casters at high priority.
- **Ascendance:** Holy capstone talent providing an emergency healing throughput boost during intense phases. The bot holds the cooldown until someone actually needs healing (a party member low or the group hurt), instead of firing it on a quiet tick.

---

## Utility & interrupts

- **Party Buffs:** Maintains *Power Word: Fortitude*, *Divine Spirit*, and *Shadow Protection*. Replaces single-target casts with group prayers (*Prayer of Fortitude*, etc.) when trained and reagents are stocked.
- **Fear Ward:** Casts *Fear Ward* on the party tank first to prevent fear-based wipes, re-applying once the cooldown clears.
- **Dispels:** Uses *Dispel Magic* on allies, and *Abolish Disease* (falling back to *Cure Disease*) on infected party members.
- **Crowd Control:** Casts *Shackle Undead* on assigned undead CC marks (in dungeons only on the assigned mark unless `auto cc` is on; open world and raids also free-pick). *Psychic Scream* is off by default in dungeons, raids, and real-player groups; re-enable with `co +aoe fear`.
- **Resurrection:** Revives fallen allies out of combat with *Resurrection* (dropping *Shadowform* first).

---

## Talent builds

Bots learn pre-set talent orders at 5-level checkpoints (levels 10–60):

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Holy** | `5.0` | Healer | `14 / 37 / 0` | Spirit of Redemption, Ascendance, Spiritual Guidance, Inspiration, Discipline dip. |
| **Shadow** | `5.1` | Ranged DPS | `20 / 0 / 31` | Mind Flay, Shadowform, Vampiric Embrace, Discipline dip. |
| **Discipline** | `5.2` | Healer / Support | `40 / 11 / 0` | Resurgent Shield, Enlighten, Chastise, Force of Will, Inner Focus, Holy dip. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Holy (`5.0`):** Early levels build healing throughput (*Improved Renew*, *Divine Fury*, *Inspiration*, *Improved Healing*), reaching *Spirit of Redemption* (40). Mid levels take *Spiritual Guidance*, *Spiritual Healing*, and *Ascendance* (50). Late levels dip into Discipline (*Mental Agility*, *Improved Power Word: Fortitude*, *Inner Focus*).
- **Shadow (`5.1`):** Early levels build Shadow damage (*Improved Mind Blast*, *Shadow Focus*, *Mind Flay* at 25, *Shadow Weaving*), reaching *Vampiric Embrace* (35). Mid levels take *Vampiric Touch* (35) and *Darkness*. Late levels dip into Discipline (*Mental Agility*, *Inner Focus*, *Improved Power Word: Shield*, *Meditation*).
- **Discipline (`5.2`):** Early levels build mitigation and efficiency (*Silent Resolve*, *Unbreakable Will*, *Inner Focus*, *Improved Power Word: Shield*, *Meditation*). Mid levels take *Mental Strength*, *Searing Light* (40), *Enlighten* (45), *Resurgent Shield*, and *Chastise* (50). Late levels take *Force of Will* plus 11 points in Holy (*Improved Renew*, *Holy Focus*, *Divinity*, *Divine Fury* 1/5).

</details>

The Shadow premade skips *Vampiric Touch* in play: the talent is taken in the build but the bot never casts it.

---

## Class settings

There are no class-specific configuration flags for Priests. General healing thresholds, mana reserves, and reaction times are configured in [Combat settings](../server/settings/combat.md).
