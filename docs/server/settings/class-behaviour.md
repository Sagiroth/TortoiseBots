---
id: server-settings-class-behaviour
title: Class Behaviour Settings
category: server
summary: Configuration flags controlling class-specific AI decisions for hunters, paladins, rogues, warlocks, and warriors.
tags: [config, classes, ai, spells]
relates_to:
  - classes-overview
  - classes-hunter
  - classes-paladin
  - classes-rogue
  - classes-warlock
  - classes-warrior
---

# Class Behaviour Settings

These settings configure class-specific artificial intelligence behaviors, ability swaps, and threat management for bot companions. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.AnnounceSunderStacks`](#aiplayerbotannouncesunderstacks) | `0` | Warriors say in party or raid chat when they have five Sunder Armor stacks on an elite or boss. |
| [`AiPlayerbot.EnableOffSpecStrategies`](#aiplayerbotenableoffspecstrategies) | `1` | Gives hybrid bots their off-spec kit by default: healers deal damage, DPS specs off-heal. |
| [`AiPlayerbot.HunterCarveInReachOnly`](#aiplayerbothuntercarveinreachonly) | `0` | Survival hunters cast Carve only on a target already in melee reach, never walking in for it. |
| [`AiPlayerbot.HunterViperOnLowMana`](#aiplayerbothunterviperonlowmana) | `1` | Switches hunters to Aspect of the Viper when low on mana. |
| [`AiPlayerbot.PaladinJudgementUpkeep`](#aiplayerbotpaladinjudgementupkeep) | `1` | Assigns paladins to maintain Judgements of Wisdom and Light on bosses. |
| [`AiPlayerbot.RogueDungeonThreatDrops`](#aiplayerbotroguedungeonthreatdrops) | `1` | Causes dungeon rogues to cast Feint and Vanish when pulling high threat. |
| [`AiPlayerbot.SapApproachAvoidPack`](#aiplayerbotsapapproachavoidpack) | `0` | Makes rogues sneak round a pack to Sap the marked mob, or skip the Sap when there is no safe way in. |
| [`AiPlayerbot.WarlockPetBySpec`](#aiplayerbotwarlockpetbyspec) | `1` | Selects warlock demon summons based on active talent specialization. |
| [`AiPlayerbot.WarlockRaidRotation`](#aiplayerbotwarlockraidrotation) | `0` | Stops warlock DoTs, curses and Drain Soul from wasting casts on targets that are about to die. |
| [`AiPlayerbot.WarriorGapCloser`](#aiplayerbotwarriorgapcloser) | `0` | Warriors Charge to open a fight and tanks Intercept to rescue a party member, swapping stance for it. |

## Settings

### `AiPlayerbot.AnnounceSunderStacks`
A warrior bot, tank or damage dealer, that sees five *Sunder Armor* stacks on its elite or boss target says "5 Sunders on <name>" once in party chat (raid chat in a raid group).
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- Each warrior bot says it once per target, so two warriors on the same boss both speak.
- Only elite and boss creatures count; normal mobs and players never trigger it.
- Only a group with at least one real player hears it; a group of bots only stays silent.
- Any Sunder Armor on the target counts, including one applied by another player.

</details>

### `AiPlayerbot.EnableOffSpecStrategies`
When new bots are created, hybrid-class bots get their off-spec strategy on top of their spec kit: healing specs (Discipline/Holy priests, Restoration shamans and druids, Holy paladins) get `offdps`, and hybrid damage specs (Shadow priests, Elemental/Enhancement shamans, Retribution paladins, Cat/Balance druids) get `offheal`. See [healer off-DPS](../../classes/overview.md#healer-off-dps-offdps) and [off-healing](../../classes/overview.md#dps-off-healing-offheal) for what each kit does.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The toggle only sets the default on newly created bots. Players can still switch either kit on or off per bot at any time (`/w <BotName> co +offheal`, `/w <BotName> co -offdps`, or [`.bot strategy`](../../play/commands/party-actions.md#bot-strategy)), regardless of this setting.

When set to `0`, hybrid bots fight with their spec kit only: healers cast no damage spells, and DPS specs never heal. This does not touch solo priest healers in the open-world pool, which keep `offdps` so they can fight alone.

</details>

### `AiPlayerbot.HunterCarveInReachOnly`
Makes Survival hunters cast Carve only on a target that is already within melee reach, instead of walking onto it. It is off by default (new behaviour flags ship off) but recommended on: turn it on so a hunter at a tank-held boss stays at range instead of walking in and being carried back out.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Carve is a melee cone that shares its 10 second cooldown with Multi-Shot. With the setting at `0`, the Carve row walks the hunter to its target first. At a tank-held boss that put the hunter inside the shot's dead zone, where the dead-zone step-out carried it back out, and the loop repeated every 10 seconds, costing shots and Auto Shot time.

With `1`, Carve is cast only when the target is within 5 yards edge to edge. A mob on the hunter stays in reach, so Carve still fires in the solo and dungeon melee case. A manual `carve` order can still walk.

</details>

### `AiPlayerbot.HunterViperOnLowMana`
Allows hunters to switch into Aspect of the Viper during combat when their mana is low. Leave this enabled to keep hunter bots firing steadily in long encounters.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A hunter that knows Aspect of the Viper switches to it during combat when its mana falls below 20%.

Once its mana recovers to 60% or higher, the hunter switches back to Aspect of the Hawk. Between 20% and 60% mana, the hunter maintains its current aspect to avoid unnecessary aspect flipping.
Player-assigned aspect strategies (such as Aspect of the Hawk, Monkey, Cheetah, Pack, Beast or Wild) are never overridden, and hunters running Aspect of the Cheetah or Pack are left undisturbed. When set to `0`, Aspect of the Viper remains a manual player action and the bot remains in Aspect of the Hawk.

</details>

### `AiPlayerbot.PaladinJudgementUpkeep`
Coordinates paladins in a group to maintain Judgement of Wisdom and Judgement of Light on bosses and elite targets.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

In a group, paladin bots coordinate to maintain debuffs on bosses, elite targets, or the tank's current target inside dungeons and raids:
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

### `AiPlayerbot.SapApproachAvoidPack`
Makes a rogue that Saps its crowd control mark before the pull approach from the side away from the mob's pack-mates instead of walking straight through them.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- The rogue only walks in while Stealth is up; with Stealth on cooldown it waits.
- It picks a spot in Sap reach of the marked mob, as far as possible from the other hostile mobs within 30 yards of it.
- The walk there must stay clear of each pack-mate by its stealth detection range (the core's own formula, which grows when the mob is several levels above the rogue), plus the 5 yards in which a mob turns to look, plus a 2-yard margin. If the direct path does not, the rogue tries other spots around the mob, then a detour round the outside of the pack.
- When no spot and path are clear, the rogue skips that Sap for 30 seconds, writes one line to the server log, and the tank pulls as usual.

When set to `0`, the rogue walks straight to its own side of the mob, as before.

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

### `AiPlayerbot.WarlockRaidRotation`
Stops the warlock rotation from spending casts on a target that is about to die, so the Shadow Bolt filler goes out instead of another DoT or a Drain Soul channel.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- A DoT or curse (*Corruption*, *Siphon Life*, *Immolate*, the curse rows) only starts on a target at 35% health or more. A DoT spread to another attacker only lands while that attacker is above 50% health. Bosses (world and raid bosses, instance-binding bosses, and the loaded dungeon plan's bosses) and the raid-utility curses (*Curse of the Elements*, *Curse of Recklessness*, *Curse of Shadow*) are never gated by either line.
- In a converted raid group (not a five-player party) each of the five "on attacker" spread rows — *Corruption*, *Siphon Life*, *Immolate*, *Curse of the Elements*, and the *Curse of Agony* spread — keeps at most two extra targets dotted besides the current target. The count is per row and counts the attackers already carrying that row's DoT or curse.
- *Drain Soul* only harvests from trash, never from a boss, and only while the warlock holds fewer than two Soul Shards. The shard row also sits below *Shadowburn*, so a dying trash mob takes the instant first.
- In combat *Life Tap* only fires when two *Shadow Bolts* cannot be paid for, instead of at the 40% medium-mana line. The health floor and the out-of-combat top-up band are unchanged.

Manual curse strategies a player turns on (`.bot strategy +curse …`) are player orders and are never gated. When set to `0`, every row behaves as before.

</details>

### `AiPlayerbot.WarriorGapCloser`
Lets warriors close distance with *Charge* and *Intercept*, swapping stance for it, instead of walking in.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- Out of combat, with its target 8-25 yards away (the spell's own range, measured edge to edge like the server does) and in line of sight, a warrior of any spec swaps to Battle Stance if needed and casts *Charge*. A tank swaps back to Defensive Stance as soon as the fight starts; Fury goes back to Berserker Stance.
- A tank picks a mob that is already hitting a party member first (a pack a rogue or a healer pulled), then its own target. A crowd-controlled mob (a Sap, a Polymorph) or a held crowd-control mark is never charged.
- In combat a tank uses *Intercept* (Berserker Stance, 10 rage) on a mob hitting the healer or another non-tank member 8-25 yards away, but only when the rage left after the stance swap pays for it (Tactical Mastery keeps 5 rage per rank across a swap). It then swaps back to Defensive Stance and taunts. With too little rage it runs in.
- Inside a dungeon or raid an unengaged mob is charged only when it is the target of a melee pull order or an attack order; the run's own pulls open from range and stay that way.
- A bot told to stay, or held by a pull's wait, does not charge.

When set to `0`, warriors only Charge from the stance they are already in as the walk in closes (Arms), and tanks never leave Defensive Stance to close a gap.

</details>
