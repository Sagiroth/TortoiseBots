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
| [`AiPlayerbot.RaidFollowDistance`](#aiplayerbotraidfollowdistance) | `4.0` | Expanded following distance in yards used inside dungeons and raids. |
| [`AiPlayerbot.SightDistance`](#aiplayerbotsightdistance) | `60.0` | Perception distance in yards used to detect monsters and define guard leashes. |
| [`AiPlayerbot.SpellDistance`](#aiplayerbotspelldistance) | `26.0` | Maximum range in yards used for initiating ranged spell attacks. |
| [`AiPlayerbot.ReactDistance`](#aiplayerbotreactdistance) | `150.0` | Maximum distance in yards bots remain responsive to combat and leader calls. |
| [`AiPlayerbot.LootDistance`](#aiplayerbotlootdistance) | `25.0` | Maximum distance in yards from the master that corpses may be for bots to loot them. |
| [`AiPlayerbot.AoeRadius`](#aiplayerbotaoeradius) | `5.0` | Default yard radius for area-of-effect spells and cluster targeting. |
| [`AiPlayerbot.ShareTargets`](#aiplayerbotsharetargets) | `1` (built in) | Reuses nearby friendly bots' attacker lists for coordinated defense. |
| [`AiPlayerbot.GlobalCooldown`](#aiplayerbotglobalcooldown) | `1500` | Pacing delay in milliseconds enforced between rapid spell casts. |
| [`AiPlayerbot.InterruptIgnoresThreat`](#aiplayerbotinterruptignoresthreat) | `0` | Pure interrupts (Kick, Pummel, Counterspell...) skip the threat back-off like heals. Recommended on. |
| [`AiPlayerbot.SpellFacingPrecheck`](#aiplayerbotspellfacingprecheck) | `0` | Bots skip casts the target's facing makes impossible (Gouge from behind, Backstab from the front) without probing them. Recommended on. |
| [`AiPlayerbot.ThreatCapCountsCastThreat`](#aiplayerbotthreatcapcountscastthreat) | `0` | The threat back-off counts a damage cast's own expected threat, so a slow nuke is held before it would land past the tank. Recommended on. |
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
When a party member health exceeds this percentage, bots hold back routine healing casts. A direct heal already being cast on a target at 90% or more is cancelled even if this setting is higher than 90.
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

Masterless pool bots outside PvP may also flee at critical health (having a real master, or being in PvP, holds them).

</details>

### `AiPlayerbot.LowHealth`
Health percentage triggering prioritized heavy healing spells.
**Default:** `50` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Healers deploy heavy direct heals on this trigger (*Greater Heal*, *Healing Wave*, *Regrowth*).

For safer dungeon runs or high-damage raid encounters, raising this value to `65` provides earlier healing intervention.

</details>

### `AiPlayerbot.MediumHealth`
Health percentage triggering routine maintenance healing and small direct heals.
**Default:** `70` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When group members fall below this line, healers fire their medium-tier heals (druid *Regrowth*; priest pace and fast-damage picks include *Flash Heal*).

Utility actions such as Paladin Judgement upkeep are suspended while a healer's party member to heal remains below this threshold.

</details>

### `AiPlayerbot.LowMana`
Mana floor percentage where casters recover mana or hold back expensive casts.
**Default:** `15` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When caster mana falls below this percentage, bots react on the "low mana" trigger:
- Mages fire *Evocation* (it also arms below medium mana, so it can fire above this line).
- Several self-heal flee checks stay armed: bots with mana at or above this line hold ground instead of fleeing.

</details>

### `AiPlayerbot.MediumMana`
Threshold percentage where bots begin pacing expensive spell usage against mana longevity.
**Default:** `40` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

At this mana level, the "medium mana" trigger fires: combat buff casts on the current target are suppressed while it holds, and priests skip top-up-tier heals to save mana for emergencies. Paladin *Consecration* is also held back below this line, and warlocks use *Life Tap*.

Healer paladins also need at least 50% mana (and no party member below medium health) to keep up offensive seals and judgements in groups.

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
**Default:** `4.0` · **Values:** positive float · **Restart:** needed

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

Sets the outer awareness envelope around the leader for follow targets and orders: a follow target beyond it is refused, and move orders past it are refused.

If a follower falls farther behind than this distance, it triggers [`AiPlayerbot.BoostFollow`](party.md#aiplayerbotboostfollow) mount checks to catch up.

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

### `AiPlayerbot.InterruptIgnoresThreat`
Whether pure interrupts skip the damage threat back-off, the way heals already do. Recommended on.
**Default:** `0` · **Values:** `0` off (an interrupt is vetoed at the 80 % high-threat line like any single-target spell), `1` on (Kick, Pummel, Shield Bash, Bash, Counterspell and Silence run while they would not pull the mob off the tank) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

A bot with the `threat` strategy in a group backs off single-target spell actions while its threat on the target reads at or above 80 % of the tank's (`ThreatMultiplier`, the `high threat` trigger); melee swings and heals are exempt. Interrupt rows are single-target spell actions too, so a damage dealer over the line let every enemy cast finish: in the Tortoise Trials Kick test a rogue stood idle at full energy in 21 of 50 interruptible-cast windows and kicked 0.42 of them (target 0.6). With the key on, every `CastInterruptSpellAction` whose `IgnoresThreatCap()` is true is exempt (`ThreatCapInterruptExempt`): Kick, Pummel, Shield Bash, Bash, Counterspell and Silence. The exemption stops at the core's aggro pull line: it holds only while the bot's threat on the interrupt's own target (the caster it would kick, which need not be its current target) plus the interrupt's flat threat from the core's spell threat table (Counterspell 300, Shield Bash 180, Kick none) is under 110 % of the tank's threat on that mob when the mob can reach the bot in melee, or under 130 % from range. That is the rule the core uses to switch a mob's target (`ThreatManager`, 110 % in melee reach, 130 % otherwise). At the line or over it the normal veto applies. The core measures against the mob's current victim; the bot measures against the top tank's threat on the mob, which is the same while the tank holds it. With no tank threat on the mob the veto stays as it is. The spell's damage threat is not counted (it is small for these spells), nor are threat modifiers. While the key is on, every vetoed interrupt writes a row at most once a second per bot: `ThreatCap interrupt spell= pct= flat= line= bot= tank=` (tag `THREAT` in the per-bot action log with `AiPlayerbot.EnableActionLog = 1`, and `ThreatCapInterrupt` in `bot_events.csv`), so a run shows why a Kick still waited. The Earth Shock and Death Coil interrupt rows are real nukes and keep the back-off, and the `... on enemy healer` rows (Counterspell, Kick, Pummel, Shield Bash or Silence on a healer that is not casting) stay gated as well: they are not interrupt actions. The AoE back-off is not touched. Range, line of sight and cooldowns still stop an interrupt.

</details>

### `AiPlayerbot.SpellFacingPrecheck`
Whether bots refuse a cast the target's facing makes impossible before they ask the core about it. Recommended on.
**Default:** `0` · **Values:** `0` off (the cast check probes the core as before), `1` on (Gouge from behind and from-behind-only spells such as Backstab from the front are refused first) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

The core refuses two casts because of where the target faces: a from-behind-only spell while the target has the caster in its front arc (`SPELL_FAILED_NOT_BEHIND`), and Gouge (spell attributes exactly `0x150010`) while it does not (`SPELL_FAILED_NOT_INFRONT`). No turn of the caster fixes either. The bot's cast check (`PlayerbotAI::CanCastSpell`) used to find out by probing the core's cast check, which sends a spell failure to every client nearby on each probe, and it read the Gouge answer as castable, so a rogue on the `behind` strategy tried Gouge about 20 times a second and none landed (Tortoise Trials Kick test: 0 Gouges in 4 reps, 338 failures in one). With the key on the bot runs the same two tests itself first (`TargetFacingRefuses`) and refuses the cast without the probe, so the engine moves on to its next action in the same tick. The precheck runs before the core's other cast checks (cooldown, range, power and so on), so the answer for such a cast becomes `NOT_INFRONT` or `NOT_BEHIND` where the core would sometimes have named another reason first. It can only refuse a cast that the core always refuses on facing; every other cast goes to the core's check as before.
</details>

### `AiPlayerbot.ThreatCapCountsCastThreat`
**Default:** `0` · **Values:** `0` off (only the bot's threat at cast start is checked against the 80 % high-threat line), `1` on (a damage cast is also held while the bot's threat plus the cast's expected threat reaches the tank's) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

A bot with the `threat` strategy in a group backs off single-target spell actions while its threat on the target reads at or above 80 % of the tank's (`ThreatMultiplier`, the `high threat` trigger). That check reads the threat the bot has when the cast starts. A slow nuke is one big step: a Fireball started at 79 % lands about 3 s later as one 400-690 threat hit, and on a fresh mob where the tank holds only two or three such hits that carries the caster past the core's 130 % ranged pull line, so the mob turns on it (Tortoise Trials, mages without fire talents: 48 of 87 aggro onsets came within 5 s of a Fireball hit).

With the key on, a `CastSpellAction` of the single-target threat type, cast by a bot that is not a tank at a mob whose victim is a tank, is held while the bot's threat on that mob plus the cast's expected threat reaches the victim's threat on it, capped by the top tank's (`ThreatCapCastReference`, `ThreatCapCastOvershoots`; an off-tank above the victim is no cover, since the core turns the mob on whoever passes its current victim). The line is 100 %, not the 110 / 130 % pull line, because the estimate is an average. Interrupt actions (Kick, Pummel, Shield Bash, Bash, Counterspell, Silence, Earth Shock) and heals are untouched, and with no tank threat on the mob the old veto stays as it is.

The expected threat (`ExpectedCastThreat`) follows the core:

- **Damage:** for each direct school-damage effect of the bot's rank of the spell, the middle of the core's damage roll with level growth (`ThreatCapAverageEffectPoints`, the arithmetic of `CalculateSpellDamage` without the roll) and the caster's spell mods, plus spell power and damage modifiers (`SpellDamageBonusDone`).
- **Channels** count the whole channel: a periodic damage or leech effect (Mind Flay, Drain Life) as one tick through `SpellDamageBonusDone` for damage over time, times the ticks (duration / amplitude); a periodic trigger (Arcane Missiles) as the triggered missile's direct damage times the ticks, with the missile's own threat multiplier.
- **Threat** (`ThreatCapCastThreat`): the damage times the spell threat table's multiplier (Mind Blast, Searing Pain, Earth Shock and Holy Shock are x2), plus the table's flat threat and attack-power bonus, all through the caster's threat spell mods and threat modifiers (`ApplyTotalThreatModifier`), as the core adds threat when the hit lands. A spell the core gives no harmful threat counts 0.

Limits (the estimate stays an average, on the low side):

- a DoT that is not channeled counts only its direct part: its threat comes a tick at a time, and every later cast is checked again;
- crits (x1.5 in vanilla) are not counted;
- the target's damage-taken modifiers (Fire Vulnerability, Shadow Weaving, Curse of the Elements and the like) are not counted;
- combo-point finishers count no combo-point damage (the points are added only at cast time);
- melee and ranged attack spells (Heroic Strike, Sinister Strike, Aimed Shot) go through the spell-damage bonus, not the weapon damage path, so their estimate is only their spell points.

Each held cast writes a row at most once every 2 s per bot: `ThreatCap cast spell= pct= cast= bot= tank=`, where `tank=` is the line the cast was measured against (tag `THREAT` in the per-bot action log with `AiPlayerbot.EnableActionLog = 1`, and `ThreatCapCast` in `bot_events.csv`).

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

A seeking bot attacks an enemy player only when every item below holds:
- not in a battleground
- target is an enemy-faction player who is PvP flagged
- target zone is not in `AiPlayerbot.PvpProhibitedZoneIds`
- target is within 20 yards (sight range when the bot has more health)
- height difference is under 30 yards
- neither stands within 40 yards of the graveyard they revive at
- level-gap odds allow it (never 5+ levels above; closer targets always)

Unflagged players, challenge-mode characters (except War Mode) and GMs with visibility off are never attacked. Owned and hired bots and bots grouped with a real player never seek. Battlegrounds keep their own logic.

A seeker chases a fleeing target for up to 60 seconds, camps a player it killed for up to 5 minutes and then ignores that player for 10 minutes, and joins a PvP fight within 30 yards that someone from its own faction is in.

One bot can be switched on with `nc +world pvp` (combat copy needs `co +world pvp`). `nc -world pvp` alone leaves the combat copy seeking, and the next auto-grant can add it back.

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
