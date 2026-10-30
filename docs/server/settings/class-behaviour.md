---
id: server-settings-class-behaviour
title: Class Behaviour Settings
category: server
summary: Configuration flags controlling class-specific AI decisions for hunters, paladins, rogues, and warlocks.
tags: [config, classes, ai, spells]
relates_to:
  - classes-overview
  - classes-hunter
  - classes-paladin
  - classes-rogue
  - classes-warlock
---

# Class Behaviour Settings

These settings configure class-specific artificial intelligence behaviors, ability swaps, and threat management for bot companions. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.EnableOffSpecStrategies`](#aiplayerbotenableoffspecstrategies) | `1` | Gives hybrid bots their off-spec kit by default: healers deal damage, DPS specs off-heal. |
| [`AiPlayerbot.HunterViperOnLowMana`](#aiplayerbothunterviperonlowmana) | `1` | Switches hunters to Aspect of the Viper when low on mana. |
| [`AiPlayerbot.PaladinJudgementUpkeep`](#aiplayerbotpaladinjudgementupkeep) | `1` | Assigns paladins to maintain Judgements of Wisdom and Light on bosses. |
| [`AiPlayerbot.RogueDungeonThreatDrops`](#aiplayerbotroguedungeonthreatdrops) | `1` | Causes dungeon rogues to cast Feint and Vanish when pulling high threat. |
| [`AiPlayerbot.WarlockPetBySpec`](#aiplayerbotwarlockpetbyspec) | `1` | Selects warlock demon summons based on active talent specialization. |

## Settings

### `AiPlayerbot.EnableOffSpecStrategies`
When new bots are created, hybrid-class bots get their off-spec strategy on top of their spec kit: healing specs (Discipline/Holy priests, Restoration shamans and druids, Holy paladins) get `offdps`, and hybrid damage specs (Shadow priests, Elemental/Enhancement shamans, Retribution paladins, Cat/Balance druids) get `offheal`. See [healer off-DPS](../../classes/overview.md#healer-off-dps-offdps) and [off-healing](../../classes/overview.md#dps-off-healing-offheal) for what each kit does.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The toggle only sets the default on newly created bots. Players can still switch either kit on or off per bot at any time (`/w <BotName> co +offheal`, `/w <BotName> co -offdps`, or [`.bot strategy`](../../play/commands/party-actions.md#bot-strategy)), regardless of this setting.

When set to `0`, hybrid bots fight with their spec kit only: healers cast no damage spells, and DPS specs never heal. This does not touch solo priest healers in the open-world pool, which keep `offdps` so they can fight alone.

</details>

### `AiPlayerbot.HunterViperOnLowMana`
Allows hunters to switch into Aspect of the Viper during combat when their mana is low. Leave this enabled to keep hunter bots firing steadily in long encounters.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A hunter that knows Aspect of the Viper switches to it during combat when its mana falls below 20%.

Once its mana recovers to 60% or higher, the hunter switches back to Aspect of the Hawk. Between 20% and 60% mana, the hunter maintains its current aspect to avoid unnecessary aspect flipping.

Player-assigned aspect strategies (such as Aspect of the Cheetah or Pack) are never overridden, and hunters currently running in utility aspects are left undisturbed. When set to `0`, Aspect of the Viper remains a manual player action and the bot remains in Aspect of the Hawk.

</details>

### `AiPlayerbot.PaladinJudgementUpkeep`
Coordinates paladins in a group to maintain Judgement of Wisdom and Judgement of Light on bosses and elite targets.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

In a group, paladin bots coordinate to maintain debuffs on bosses, elite targets, or the primary mob engaged by the tank:
- Retribution paladins prioritize Judgement of Wisdom.
- Holy paladins prioritize Judgement of Light.
- If two paladins of the same specialization are present, the second takes the alternate debuff so two paladins never judge the same debuff. A third non-tank paladin maintains none.
- Protection paladins serving as tanks never own a group judgement (keeps its own Seal for threat generation).

Paladins refresh their assigned judgement when ~4 seconds remain on the target debuff. Healers only judge while their mana is at least 50% and no group member is below medium health. Paladins require at least medium mana to judge. Judgements are never cast on targets below 25% health or on enemy players. A still-fresh Judgement (whoever applied it, including a player paladin) is left alone until ~4 s remain.

When set to `0`, paladins use their default individual rotations without group coordination.

</details>

### `AiPlayerbot.RogueDungeonThreatDrops`
Instructs rogues to use Feint and Vanish to drop threat in 5-player dungeon groups when their threat climbs toward the tank's.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

In 5-player dungeons with a living main tank holding the mob, rogues monitor their relative threat:
- Casts *Feint* when threat reaches medium levels (60% or more of the tank threat).
- Casts *Vanish* when threat reaches high levels (80% and climbing).

This behavior applies only while a living main tank other than the rogue is in the fight and holds the mob (it is the target's victim or has threat on it), so rogues do not blow cooldowns on unengaged loose adds or while solo, without an active tank, or in the open world. Raid rogues keep their own Feint/Vanish rows regardless of this toggle. Vanish fires only while the mob is held by the tank or the rogue.

Vanish is prioritized below Feint so a ready Feint is always attempted first. When set to `0`, the dungeon rows are gone and only the base low-health chain (Evasion, then Vanish, then Feint) remains.

</details>

### `AiPlayerbot.WarlockPetBySpec`
Directs warlocks to summon the demon best matching their talent specialization rather than defaulting to the Voidwalker.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

In PvE situations (open world and dungeons, excluding raids and PvP), warlocks summon demons tailored to their active talent spec:
- Affliction and Demonology warlocks summon the Felhunter.
- Destruction warlocks summon the Imp.

If the warlock has not yet learned its spec demon, it falls through the available summon hierarchy: Felhunter, Succubus, Voidwalker, and Imp. If the warlock is in a party that has no tank, it keeps the Voidwalker summoned to act as the group tank.

The demon choice evaluates when the warlock has no active pet. Pool bots also swap an existing default demon when shards permit. Player pet strategy commands (such as `.bot strategy +pet imp`) or active pets on owned bots are never replaced. When set to `0`, warlocks summon the Voidwalker once learned, or the Imp before learning it.

</details>
