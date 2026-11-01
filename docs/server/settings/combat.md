---
id: server-settings-combat
title: Combat & Reaction Thresholds
category: server
summary: Configuration thresholds for health, mana, engagement distances, casting pacing, pull rules, and strategy rotation.
tags: [config, combat, thresholds, health, mana, distance]
relates_to:
  - classes-overview
  - play-commands-party-actions
---

# Combat & Reaction Thresholds

These settings configure health and mana trigger percentages, engagement ranges, casting delays, tank pull mechanics, and pool-bot strategy rotation. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.AlmostFullHealth`](#aiplayerbotalmostfullhealth) | `90` | Health percentage ceiling above which bots stop casting heals to conserve mana. |
| [`AiPlayerbot.CriticalHealth`](#aiplayerbotcriticalhealth) | `20` | Health percentage considered an emergency, triggering survival cooldowns. |
| [`AiPlayerbot.LowHealth`](#aiplayerbotlowhealth) | `50` | Health percentage triggering prioritized heavy healing spells. |
| [`AiPlayerbot.MediumHealth`](#aiplayerbotmediumhealth) | `70` | Health percentage triggering maintenance heals. |
| [`AiPlayerbot.LowMana`](#aiplayerbotlowmana) | `15` | Mana floor percentage where casters recover mana or hold back expensive casts. |
| [`AiPlayerbot.MediumMana`](#aiplayerbotmediummana) | `40` | Mana percentage where bots transition toward conservative ability rotations. |
| [`AiPlayerbot.FollowDistance`](#aiplayerbotfollowdistance) | `1.5` | Standard following distance in yards between a follower bot and leader on foot. |
| [`AiPlayerbot.RaidFollowDistance`](#aiplayerbotraidfollowdistance) | `5.0` | Expanded following distance in yards used inside dungeons and raids. |
| [`AiPlayerbot.SightDistance`](#aiplayerbotsightdistance) | `60.0` | Perception distance in yards used to detect monsters and define guard leashes. |
| [`AiPlayerbot.SpellDistance`](#aiplayerbotspelldistance) | `26.0` | Maximum range in yards used for initiating ranged spell attacks. |
| [`AiPlayerbot.ReactDistance`](#aiplayerbotreactdistance) | `150.0` | Maximum distance in yards bots remain responsive to combat and leader calls. |
| [`AiPlayerbot.LootDistance`](#aiplayerbotlootdistance) | `25.0` | Maximum distance in yards from the master that corpses may be for bots to loot them. |
| [`AiPlayerbot.AoeRadius`](#aiplayerbotaoeradius) | `5.0` | Default yard radius for area-of-effect spells and cluster targeting. |
| [`AiPlayerbot.ShareTargets`](#aiplayerbotsharetargets) | `1` (built in) | Reuses nearby friendly bots' attacker lists for coordinated defense. |
| [`AiPlayerbot.GlobalCooldown`](#aiplayerbotglobalcooldown) | `1500` | Pacing delay in milliseconds enforced between rapid spell casts. |
| [`AiPlayerbot.RangedPull`](#aiplayerbotrangedpull) | `1` | Enables automatic ranged openings for commanded tank pulls. |
| [`AiPlayerbot.RangedPullMeleeApproach`](#aiplayerbotrangedpullmeleeapproach) | `20` | Walk-in approach threshold in yards before tank pulls open from range. |
| [`AiPlayerbot.PullDpsJoinDelay`](#aiplayerbotpulldpsjoindelay) | `10` | Seconds party DPS wait after a tank pull before joining combat. |
| [`AiPlayerbot.PullBackDpsJoinDelay`](#aiplayerbotpullbackdpsjoindelay) | `5` | Seconds party DPS wait after a tank pullback before engaging. |
| [`AiPlayerbot.PullBackMaxReturnTime`](#aiplayerbotpullbackmaxreturntime) | `15` | Maximum seconds a tank has to return to group before pullback aborts. |
| [`AiPlayerbot.MinRandomBotChangeStrategyTime`](#aiplayerbotminrandombotchangestrategytime) | `1800` (built in) | Minimum seconds a pool bot retains its strategy set before periodic refresh. |
| [`AiPlayerbot.MaxRandomBotChangeStrategyTime`](#aiplayerbotmaxrandombotchangestrategytime) | `7200` (built in) | Maximum seconds a pool bot retains its strategy set before periodic refresh. |
| [`AiPlayerbot.RandomBotForcePvp`](#aiplayerbotrandombotforcepvp) | `1` | Pool bots turn on the `/pvp` preference at every login so players can attack them anywhere. |
| [`AiPlayerbot.WorldPvpSeek`](#aiplayerbotworldpvpseek) | `2` | Free pool bots seek flagged enemy players (`1`) or players and bots (`2`); `0` off. |
| [`AiPlayerbot.WorldPvpSeekBotPercent`](#aiplayerbotworldpvpseekbotpercent) | `25` | Share of free pool bots holding the `world pvp` seek role. |
| [`AiPlayerbot.WorldPvpSeekRerollMinutes`](#aiplayerbotworldpvpseekrerollminutes) | `30` | Minutes between seek-role re-rolls, never while the bot is in combat. |
## Settings

### `AiPlayerbot.AlmostFullHealth`
Health percentage ceiling above which bots skip routine heals to conserve mana.
**Default:** `90` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a party member health exceeds this percentage, bots hold back routine healing casts and may cancel a direct heal already being cast on a target that no longer needs it.

Hybrid direct-plus-HoT spells (such as Regrowth) can still land for their heal-over-time part.

</details>

### `AiPlayerbot.CriticalHealth`
Health percentage considered an extreme emergency, triggering emergency cooldowns and heals.
**Default:** `20` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a bot or party member drops below this health percentage, bots deploy emergency responses on the triggers that watch it:
- Paladins cast *Divine Shield* or *Lay on Hands*.
- Protection warriors activate *Shield Wall* at low health and *Last Stand* at critical health.
- Healers fire their critical-tier direct heals (*Healing Wave*, *Holy Light*, *Flash Heal*, *Regrowth*).

Pool bots without player masters may also flee at critical health unless hold-ground settings are configured.

</details>

### `AiPlayerbot.LowHealth`
Health percentage triggering prioritized heavy healing spells.
**Default:** `50` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Healers deploy heavy direct heals on this trigger (*Greater Heal*, *Healing Wave*, *Regrowth*).

For safer dungeon runs or high-damage raid encounters, raising this value to `65` provides earlier healing intervention. (Suggested in the old tuning guide; the code only reads the value, so any number in range works.)

</details>

### `AiPlayerbot.MediumHealth`
Health percentage triggering routine maintenance healing and small direct heals.
**Default:** `70` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When group members fall below this line, healers fire their medium-tier heals (*Flash Heal*, *Regrowth*).

Utility actions such as Paladin Judgement upkeep are suspended while a healer's party member to heal remains below this threshold.

</details>

### `AiPlayerbot.LowMana`
Mana floor percentage where casters recover mana or hold back expensive casts.
**Default:** `15` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When caster mana falls below this percentage, bots react on the "low mana" trigger:
- Mages fire *Evocation*; warlocks use *Life Tap*.
- Several self-heal flee checks stay armed: bots with mana at or above this line hold ground instead of fleeing.

</details>

### `AiPlayerbot.MediumMana`
Threshold percentage where bots begin pacing expensive spell usage against mana longevity.
**Default:** `40` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

At this mana level, the "medium mana" trigger fires: buff casts are suppressed while it holds, and priests skip top-up-tier heals to save mana for emergencies. Paladin *Consecration* is also held back below this line.

Healer paladins also need at least this mana percentage (and no party member below medium health) to keep up offensive seals and judgements in groups.

</details>

### `AiPlayerbot.FollowDistance`
Standard following distance in yards between a follower bot and the leader on foot.
**Default:** `1.5` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the tight following distance used when travelling on foot in the open world.

When mounted out of combat and outside dungeons, bots add 3 yards to this radius to accommodate larger mount models and avoid clipping.

</details>

### `AiPlayerbot.RaidFollowDistance`
Expanded following distance in yards used inside dungeons and raids.
**Default:** `5.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Replaces standard follow distance inside instances to keep party members from stacking on the leader.

Free-move picks also use it: `follow` bots roam within this radius in dungeons and raids.

</details>

### `AiPlayerbot.SightDistance`
Perception distance in yards used to detect monsters and define guard leashes.
**Default:** `60.0` (yards) · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines how far bots scan their surroundings for hostile targets and game objects.

This value also sets the guard-stance leash: `guard` bots roam within this radius of their guard spot.

</details>

### `AiPlayerbot.SpellDistance`
Maximum distance in yards used for initiating ranged spell attacks.
**Default:** `26.0` (yards) · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Establishes the default engagement distance for ranged casters and several reach checks (dispels, totems, snare and healer targets).

Bots close toward targets until within this distance before beginning cast sequences.

</details>

### `AiPlayerbot.ReactDistance`
Maximum distance in yards at which bots remain responsive to combat and leader calls.
**Default:** `150.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the outer awareness envelope around the leader: follow targets beyond it count as unsafe, and graveyard and object scans use it as their range.

If a follower falls farther behind than this distance, it triggers [`AiPlayerbot.BoostFollow`](party.md#aiplayerbotboostfollow) mount checks to catch up (BoostFollow itself is off by default).

</details>

### `AiPlayerbot.LootDistance`
Maximum distance in yards from the master that corpses may be for bots to loot them.
**Default:** `25.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Corpses farther from the master (the pull requester) than this radius are rejected from the loot list. Group members without an active player master instead use `GroupMemberLootDistance` (15 yd).

</details>

### `AiPlayerbot.AoeRadius`
Default radius in yards used for calculating area-of-effect spells and hostile cluster density.
**Default:** `5.0` · **Values:** distance in yards · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the default evaluation radius for area attacks, ground-targeted spells (such as Blizzard or Rain of Fire), and multi-target ability decisions. Several checks scale it (up to 1.5x–2x) for their own scans.

When bot casters calculate whether enough hostiles are grouped to warrant an AoE ability, enemies within this yard radius are counted.

</details>

### `AiPlayerbot.ShareTargets`
Reuses nearby friendly bots' attacker lists for coordinated defense.
**Default:** `1` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, a bot reuses the attacker list of nearby friendly bots (within 10 yd), enabling coordinated target acquisition and group defense.

Set to `0` to force each bot to detect and track attacking enemies independently.

</details>

### `AiPlayerbot.GlobalCooldown`
Pacing delay in milliseconds enforced between rapid spell casts.
**Default:** `1500` (milliseconds) · **Values:** positive integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls the delay between consecutive spell casts. Several item, potion and aura actions pace themselves on it.

Prevents bot AI from queuing multiple instant abilities in the same engine tick. (The template comment calls it the delay between two short-time spell casts.)

</details>

### `AiPlayerbot.RangedPull`
Enables automatic ranged weapon or spell opening for commanded tank pulls.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

During commanded pulls (`.bot action pull` or `.bot action pullback`), tanks open from range using a bow, gun, crossbow, thrown weapon, or pull spell if the mob is in sight and in shot range and either unreachable by direct pathing (cliffs, ledges) or farther than `AiPlayerbot.RangedPullMeleeApproach`.

Setting this to `0` disables automatic ranged pulls for plain pull commands (the tank walks into melee range instead). Explicit `ranged` orders and pullbacks walk to a firing position and open from range regardless of this toggle. The tank's own automatic dungeon pulls always open from range.

</details>

### `AiPlayerbot.RangedPullMeleeApproach`
Walk-in approach threshold in yards before commanded pulls open from range.
**Default:** `20` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

If the intended target of a commanded pull is farther away than this distance (and in sight and in shot range), the tank shoots from range rather than walking in, avoiding accidental body-pulls of nearby monster packs.

Requires `AiPlayerbot.RangedPull = 1`. Lower this distance to trigger ranged pulling more aggressively; raise it to encourage tanks to walk closer before opening.

</details>

### `AiPlayerbot.PullDpsJoinDelay`
Seconds party DPS bots wait after a tank initiates a pull before joining combat.
**Default:** `10` · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

During commanded party pulls, DPS bots pause their attack routines for this duration while the tank establishes initial threat; a per-command `[seconds]` (0–60) overrides the default. The window runs from the pull landing.

Gives the tank time to build threat before damage dealers join.

</details>

### `AiPlayerbot.PullBackDpsJoinDelay`
Seconds party DPS bots wait during a pullback maneuver before engaging.
**Default:** `5` · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls the engagement pause enforced during `.bot action pullback` line-of-sight maneuvers; a per-command `[seconds]` (0–60) overrides the default. The window runs from the tank back at the anchor, when it takes the fight over.

Party bots hold their fire while the tank retreats to the anchor.

</details>

### `AiPlayerbot.PullBackMaxReturnTime`
Maximum seconds a tank has to return to the group anchor during a pullback maneuver.
**Default:** `15` · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Safety timeout for commanded tank pullbacks: the party hold window covers this return leg plus the join delay.

If the tank cannot successfully path back to the group anchor within this window, the maneuver ends and standard combat rules resume.

</details>

### `AiPlayerbot.MinRandomBotChangeStrategyTime`
Minimum duration in seconds a pool bot retains its active strategy configuration before refreshing.
**Default:** `1800` (30 minutes; built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the minimum interval before a random pool bot recalculates its strategies. Each bot picks a random refresh time between the minimum and maximum bounds.

Prevents rapid strategy churning while ensuring bots rotate through diverse gameplay styles over time.

</details>

### `AiPlayerbot.MaxRandomBotChangeStrategyTime`
Maximum duration in seconds a pool bot retains its active strategy configuration before refreshing.
**Default:** `7200` (2 hours; built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the upper bound for periodic pool bot strategy updates.

Together with the minimum timer, each bot picks a randomized refresh time between these two bounds.

</details>

### `AiPlayerbot.RandomBotForcePvp`
Pool bots turn on the same PvP preference `/pvp` sets at every login, so players can attack them anywhere.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

On its own this does not make bots hunt anyone: it only flags them. Owned and hired bots are never affected. With `0`, no bot sets `/pvp` and behaviour matches a realm without open-world PvP.

Turtle's War Mode is not used, so bots get no XP bonus from this.

</details>

### `AiPlayerbot.WorldPvpSeek`
A share of free pool bots actively look for PvP-flagged enemy players nearby and attack them.
**Default:** `2` · **Values:** `0` off, `1` real players only, `2` players and enemy bots · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A seeking bot attacks an enemy player only when all of these are true: the bot is not in a battleground, the target is an enemy-faction player who is PvP flagged, the target is not in a zone from `AiPlayerbot.PvpProhibitedZoneIds`, the target is within 20 yards (or within sight range when the bot has more health), the height difference is under 30 yards, and the level-gap odds allow it (never 5+ levels above; closer targets always).

Unflagged players, challenge-mode characters (except War Mode) and GMs with visibility off are never attacked. Owned and hired bots and bots grouped with a real player never seek. Battlegrounds keep their own logic.

A seeker chases a fleeing target for up to 60 seconds, camps a player it killed for up to 5 minutes and then ignores that player for 10 minutes, and joins a PvP fight within 30 yards that someone from its own faction is in.

One bot can be switched on or off with `nc +world pvp` / `nc -world pvp`.

</details>

### `AiPlayerbot.WorldPvpSeekBotPercent`
Share of free pool bots that get the `world pvp` seek strategy.
**Default:** `25` · **Values:** `0` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The role is handed out when a bot logs in or resets its strategies, and rolled again about every `AiPlayerbot.WorldPvpSeekRerollMinutes`. Ignored while seeking is off. `0` gives it to none.

</details>

### `AiPlayerbot.WorldPvpSeekRerollMinutes`
Minutes between re-rolls of the `world pvp` seek role.
**Default:** `30` · **Values:** minutes · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Each re-roll carries a few minutes of jitter so the pool does not swap roles in lockstep, and a bot in combat keeps its role until the fight ends.

</details>
