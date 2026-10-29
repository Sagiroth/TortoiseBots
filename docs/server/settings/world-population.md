---
id: server-settings-world-population
title: World population & immersion settings
category: server
summary: Configuration settings for random bot population, level seeding, wandering, lifecycle, and social interactions.
tags: [config, world, population, immersion]
relates_to:
  - world-living-world
  - world-social
  - server-bot-pool
---

# World Population & Immersion Settings

These settings configure autonomous random bots roaming the open world, including population limits, level distribution, wandering behavior, gear upkeep, and social interactions. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.MinRandomBots` / `MaxRandomBots`](#aiplayerbotminrandombots--maxrandombots) | `500` / `500` | Sets minimum and maximum active random bot population in the world. |
| [`AiPlayerbot.RandomBotAutologin`](#aiplayerbotrandombotautologin) | `1` | Automatically logs in random pool bots once the service starts. |
| [`AiPlayerbot.RandomBotLoginWithPlayer`](#aiplayerbotrandombotloginwithplayer) | `0` | Delays pool bot logins until a real human player enters the world. |
| [`AiPlayerbot.RandomBotLoginAtStartup`](#aiplayerbotrandombotloginatstartup) | `0` | Starts logging in random bots immediately at boot instead of waiting. |
| [`AiPlayerbot.RandomBotAutoCreate`](#aiplayerbotrandombotautocreate) | `1` | Automatically generates new bot accounts and characters if below minimum. |
| [`AiPlayerbot.RandomBotPoolReset`](#aiplayerbotrandombotpoolreset) | `off` | Controls full rebuilding and re-seeding of the managed bot pool. |
| [`AiPlayerbot.RandomBotAccountPrefix`](#aiplayerbotrandombotaccountprefix) | `RNDBOT` | Prefix string used to name newly created managed bot pool accounts. |
| [`AiPlayerbot.PinnedBots`](#aiplayerbotpinnedbots) | `(empty)` | Comma-separated bot character names pinned to stay online permanently. |
| [`AiPlayerbot.RandomBotStartLevelMin` / `RandomBotStartLevelMax`](#aiplayerbotrandombotstartlevelmin--randombotstartlevelmax) | `1` / `60` | Sets level range randomly assigned to a newly seeded bot on creation. |
| [`AiPlayerbot.LevelLadder`](#aiplayerbotlevelladder) | `1` | Rotates online pool bots across level bands instead of flat round-robin. |
| [`AiPlayerbot.LevelLadderBandSize`](#aiplayerbotlevelladderbandsize) | `5` | Number of character levels grouped into each ladder band. |
| [`AiPlayerbot.LevelLadderMaxLevelShare`](#aiplayerbotlevelladdermaxlevelshare) | `10` | Maximum percentage share of total online bot population at level 60. |
| [`AiPlayerbot.LevelLadderLogMinutes`](#aiplayerbotlevelladderlogminutes) | `5` | Interval in minutes between bot population ladder distribution logs. |
| [`AiPlayerbot.RandomBotTimedLogout`](#aiplayerbotrandombottimedlogout) | `1` | Enables automatic timed logout rotation for random pool bots. |
| [`AiPlayerbot.MaxRandomBotInWorldTime`](#aiplayerbotmaxrandombotinworldtime) | `21600` | Maximum duration in seconds a random bot stays online before session relog. |
| [`AiPlayerbot.MinRandomBotRandomizeTime` / `MaxRandomBotRandomizeTime`](#aiplayerbotminrandombotrandomizetime--maxrandombotrandomizetime) | `21600` / `86400` | Interval range in seconds between unseeded-gear backstop sweeps. |
| [`AiPlayerbot.RandomBotStallRelogMinutes`](#aiplayerbotrandombotstallrelogminutes) | `45` | Relogs sub-60 bots that fail to earn experience within this timeframe. |
| [`AiPlayerbot.RandomBotMaxLevel`](#aiplayerbotrandombotmaxlevel) | `60` | Caps the gear and item level tables that random bots roll from. |
| [`AiPlayerbot.minEnchantingBotLevel`](#aiplayerbotminenchantingbotlevel) | `10` | Minimum character level required for bots to receive gear enchantments. |
| [`AiPlayerbot.RandomBotInvitePlayer`](#aiplayerbotrandombotinviteplayer) | `1` | Allows open-world random bots to invite nearby solo players to groups. |
| [`AiPlayerbot.RandomBotGroupNearby`](#aiplayerbotrandombotgroupnearby) | `0` | Allows masterless pool bots to form spontaneous questing groups together. |
| [`AiPlayerbot.RandomBotRaidNearby`](#aiplayerbotrandombotraidnearby) | `1` | Allows masterless pool bots to form and invite nearby bots into raid groups. |
| [`AiPlayerbot.RandomBotGuildNearby`](#aiplayerbotrandombotguildnearby) | `1` | Allows masterless pool bots to invite nearby unguilded bots into their guild. |
| [`AiPlayerbot.InviteChat`](#aiplayerbotinvitechat) | `1` | Broadcasts group and raid invitation announcements in say or guild chat. |
| [`AiPlayerbot.RandomBotFormGuild`](#aiplayerbotrandombotformguild) | `1` | Allows random bots to buy guild charters and establish autonomous guilds. |
| [`AiPlayerbot.GuildMaxBotLimit`](#aiplayerbotguildmaxbotlimit) | `1000` | Maximum number of bots allowed to join a single autonomous bot guild. |
| [`AiPlayerbot.EnableGreet`](#aiplayerbotenablegreet) | `1` | Enables text emote greetings when a bot encounters a human player. |
| [`AiPlayerbot.GreetCooldown`](#aiplayerbotgreetcooldown) | `600` | Cooldown in seconds before a bot may greet the same player again. |
| [`AiPlayerbot.RandomBotGreet`](#aiplayerbotrandombotgreet) | `0` | Allows masterless pool bots to greet players in addition to owned bots. |
| [`AiPlayerbot.RandomBotShowHelmet` / `RandomBotShowCloak`](#aiplayerbotrandombotshowhelmet--randombotshowcloak) | `1` / `1` | Helmet/cloak visibility for pool bots; `0` hides them on the bots' next login. |
| [`AiPlayerbot.PoolBotTradeMode`](#aiplayerbotpoolbottrademode) | `0` | Governs whether masterless pool bots accept trade requests from players. |
| [`AiPlayerbot.TradeActionExcludedPrefixes`](#aiplayerbottradeactionexcludedprefixes) | `RPLL_H_,DBMv4` | Chat prefixes ignored by bots so addons do not trigger trade windows. |
| [`AiPlayerbot.TurtleMountAtLevel`](#aiplayerbotturtlemountatlevel) | `18` | Level where bots learn the custom Swift Riding Turtle slow mount. |
| [`AiPlayerbot.LevelUpMounts`](#aiplayerbotlevelupmounts) | `1` | Teaches standard racial mounts automatically when bots reach level 40 and 60. |
| [`AiPlayerbot.RandomBotShopMounts`](#aiplayerbotrandombotshopmounts) | `0` | Restricts bots to racial mounts, blocking cross-race and rare shop mounts. |
| [`AiPlayerbot.RepopAtGraveyard`](#aiplayerbotrepopatgraveyard) | `1` | Dead masterless bots release to nearest graveyard instead of starting zone. |
| [`AiPlayerbot.AvoidHostileTowns`](#aiplayerbotavoidhostiletowns) | `1` | Prevents masterless bots from traveling near enemy faction town guards. |
| [`AiPlayerbot.RelocateHopelessDeaths`](#aiplayerbotrelocatehopelessdeaths) | `1` (built in) | Relocates bots repeatedly killed near enemy guards or in death loops. |
| [`AiPlayerbot.UnstuckHearthLevelFit`](#aiplayerbotunstuckhearthlevelfit) | `1` | Requires bot hearthstone bind point to be within 10 levels of bot level. |
| [`AiPlayerbot.StuckTravelTeleport`](#aiplayerbotstucktravelteleport) | `1` | Teleports long-stuck masterless bots directly to their travel destinations. |
| [`AiPlayerbot.UnstuckHearthMinDistance`](#aiplayerbotunstuckhearthmindistance) | `300` | Minimum distance from bind point before an unstuck bot casts hearthstone. |
| [`AiPlayerbot.BotQuestLogUpkeep`](#aiplayerbotbotquestlogupkeep) | `1` | Enables autonomous quest log pruning, turn-in prioritization, and cleanup. |
| [`AiPlayerbot.LeaveOutgrownZones`](#aiplayerbotleaveoutgrownzones) | `1` | Forces bots to travel to level-appropriate zones when outleveling an area. |
| [`AiPlayerbot.AllowIsolatedCustomStartingZones`](#aiplayerbotallowisolatedcustomstartingzones) | `0` | Normalizes bots created in isolated custom islands to mainland start zones. |
| [`AiPlayerbot.EnableRandomTeleports`](#aiplayerbotenablerandomteleports) | `1` | Scatters newly seeded bots to level-appropriate overworld destinations. |
| [`AiPlayerbot.RandomBotEvenStartZones`](#aiplayerbotrandombotevenstartzones) | `1` | Distributes new character creation evenly across racial starter zones. |
| [`AiPlayerbot.RandomBotFactionRatio`](#aiplayerbotrandombotfactionratio) | `50` | Target percentage of created pool bots belonging to the Alliance faction. |
| [`AiPlayerbot.RandomBotNameStyleWeights`](#aiplayerbotrandombotnamestyleweights) | `55,20,25` | Weight distribution (three non-negative integers, positive sum) of race-flavor, compound, and fun name styles. |
| [`AiPlayerbot.RandomBotNamesFile`](#aiplayerbotrandombotnamesfile) | `(empty)` | Optional external text file defining custom names and name patterns. |
| [`AiPlayerbot.RandomBotStartZoneWeights`](#aiplayerbotrandombotstartzoneweights) | `(empty)` | Custom weighting per starting zone for newly generated characters. |
| [`AiPlayerbot.AllowRepairedTaxiPaths`](#aiplayerbotallowrepairedtaxipaths) | `0` | Disables broken client flight paths (paths 274 and 355) unless core repairs. |
| [`AiPlayerbot.AmmoStacksNonHunter`](#aiplayerbotammostacksnonhunter) | `2` | Number of ammo stacks stocked for ranged warriors and rogues. |
| [`AiPlayerbot.UseWanderAsDefaultFollowStrategy`](#aiplayerbotusewanderasdefaultfollowstrategy) | `1` | Sets masterless bots to wander freely instead of tightly following. |
| [`AiPlayerbot.WanderMinDistance`](#aiplayerbotwandermindistance) | `5.0` (built in) | Minimum yard radius bots roam from anchor while running wander strategy. |
| [`AiPlayerbot.WanderMaxDistance`](#aiplayerbotwandermaxdistance) | `50.0` (built in) | Maximum yard radius bots roam from anchor while running wander strategy. |
| [`AiPlayerbot.AutoLearnQuestSpells`](#aiplayerbotautolearnquestspells) | `1` | Automatically teaches spells rewarded by completed class quests. |
| [`AiPlayerbot.AutoLearnTrainerSpells`](#aiplayerbotautolearntrainerspells) | `0` | Grants free trainer spell sweeps on level-up for random pool bots. |
| [`AiPlayerbot.AutoLearnDroppedSpells`](#aiplayerbotautolearndroppedspells) | `0` | Grants free dungeon and raid book spells to random pool bots at level 60. |
| [`AiPlayerbot.RandomGearUpgradeEnabled`](#aiplayerbotrandomgearupgradeenabled) | `1` (built in) | Seeds newly created pool bots with level-appropriate starter equipment. |
| [`AiPlayerbot.RandomGearWhitelist`](#aiplayerbotrandomgearwhitelist) | `(empty)` (built in) | Comma-separated whitelist of item IDs pool bots are allowed to equip. |
| [`AiPlayerbot.RandomGearProgression`](#aiplayerbotrandomgearprogression) | `1` (built in) | Enables progressive gear upgrades matching level progression milestones. |
| [`AiPlayerbot.RandomGearBlacklist`](#aiplayerbotrandomgearblacklist) | `(empty)` | Comma-separated list of item IDs excluded from random equipment pools. |
| [`AiPlayerbot.AutoEquipUpgradeLoot`](#aiplayerbotautoequipupgradeloot) | `1` | Automatically equips stat upgrades acquired from questing or creature loot. |
| [`AiPlayerbot.EquipUpgradeThreshold`](#aiplayerbotequipupgradethreshold) | `1.1` | Stat weight multiplier required for an item to replace current equipment. |
| [`AiPlayerbot.AutoPickReward`](#aiplayerbotautopickreward) | `yes` | Controls how bots select equipment rewards when completing quests. |
| [`AiPlayerbot.AutoPickTalents`](#aiplayerbotautopicktalents) | `full` | Automates talent point allocation based on the bot's chosen specialization. |
| [`AiPlayerbot.AutoTrainSpells`](#aiplayerbotautotrainspells) | `yes` | Instructs bots to visit trainers and purchase available rank upgrades. |
| [`AiPlayerbot.XPRate`](#aiplayerbotxprate) | `3` | Multiplier applied to server experience rates for autonomous bots. |
| [`AiPlayerbot.RandomGearMaxSourceTier`](#aiplayerbotrandomgearmaxsourcetier) | `0` | Restricts seed and hire gear sources to world, dungeon, or raid tiers. |
| [`AiPlayerbot.RandomGearAllowReputation`](#aiplayerbotrandomgearallowreputation) | `0` | Allows or disallows reputation-gated items on newly seeded bots. |
| [`AiPlayerbot.RandomGearAllowPvP`](#aiplayerbotrandomgearallowpvp) | `0` | Allows or disallows PvP honor rank items on newly seeded bots. |
| [`AiPlayerbot.RandomGearSeedEpicChance`](#aiplayerbotrandomgearseedepicchance) | `0.02` | Probability per equipment slot that a fresh seed rolls a rare world epic. |
| [`AiPlayerbot.RandomGearMaxLevel`](#aiplayerbotrandomgearmaxlevel) | `100` | Maximum item level considered when generating random equipment. |
| [`AiPlayerbot.RandomGearMaxItemLevelAboveLevel`](#aiplayerbotrandomgearmaxitemlevelabovelevel) | `1` | Item-level allowance above a pool bot's own level for generated gear. |
| [`AiPlayerbot.ForceRebuffOnReadyCheck`](#aiplayerbotforcerebuffonreadycheck) | `0` | Delays ready check confirmation until all party buffs are refreshed. |
| [`AiPlayerbot.RpgDelay`](#aiplayerbotrpgdelay) | `10000` | Delay in milliseconds between autonomous roleplay interactions. |
| [`AiPlayerbot.RndBotCheats`](#aiplayerbotrndbotcheats) | `repair,breath,item` | Sustenance and repair cheats active on masterless random pool bots. |
| [`AiPlayerbot.RandomBotNonCombatStrategies`](#aiplayerbotrandombotnoncombatstrategies) | `+grind,+loot,... (full string below)` | Baseline non-combat strategy kit assigned to autonomous pool bots. |
| [`AiPlayerbot.RandomBotCombatStrategies`](#aiplayerbotrandombotcombatstrategies) | `+dps assist,-threat` | Baseline combat strategy modifications applied to masterless pool bots. |
| [`AiPlayerbot.CombatStrategies`](#aiplayerbotcombatstrategies) | `(empty)` | Global combat strategies applied to owned and real-mastered bots. |

## Settings

### `AiPlayerbot.MinRandomBots` / `MaxRandomBots`
Sets the target minimum and maximum population of active random bots roaming the world. Adjust both values upward on powerful dedicated servers, or lower them for modest hardware.
**Default:** `500` / `500` · **Values:** integer count (1..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

These settings govern the size of the active random bot pool. The default setting of 500 bots is balanced for modest hosts.

If the number of active pool bots falls below `MinRandomBots`, the bot manager logs in or creates additional characters to restore population. Above target, no new logins occur; the pool shrinks through timed logout and rotation.

</details>

### `AiPlayerbot.RandomBotAutologin`
Enables automatic login of random bots once the bot service has started. Disable this only if you want the world to start without autonomous bots until manually logged in.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, the server logs in characters residing on registered managed pool accounts once the service has started (immediately at boot only with `RandomBotLoginAtStartup = 1`, otherwise after the first human login).

If set to `0`, no random bots log in automatically. Player-owned bots and companions hired by players can still enter the world when requested.

</details>

### `AiPlayerbot.RandomBotLoginWithPlayer`
Controls whether pool bot logins are delayed until a human player logs in.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled (`1`), pool bots do not log into the world while the realm is devoid of human players; logins commence when the first real player enters the world.

When set to `0`, the pool logs in independently according to `RandomBotAutologin` and `RandomBotLoginAtStartup`.

</details>

### `AiPlayerbot.RandomBotLoginAtStartup`
Starts logging in the configured random bot pool immediately on server startup instead of waiting for player activity.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When disabled (`0`), the server waits for the first human player login before initiating pool bot logins.

Setting this to `1` starts logging in pool bots immediately at boot (still batched, 30 per interval) to prevent CPU spikes.

</details>

### `AiPlayerbot.RandomBotAutoCreate`
Allows the system to automatically generate new bot accounts and characters if active pool characters fall below `MinRandomBots`.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, the server creates new characters across registered pool accounts whenever there are not enough existing characters to meet `MinRandomBots`.

This setting is strictly required when using `RandomBotPoolReset`. If pool reset is requested while auto-create is disabled, the reset is refused so the world is never left completely unpopulated.

</details>

### `AiPlayerbot.RandomBotPoolReset`
Controls managed bot pool resets at server startup. Use with care, as resetting deletes all existing bot progression and equipment.
**Default:** `off` · **Values:** `off`, `once:<token>`, `always` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls whether the managed bot pool database is rebuilt during startup:

- `off`: Default for production servers. Pool characters keep their progression, gear, and guild memberships indefinitely.
- `once:<token>`: Performs a one-time pool reset on the next startup. The server marks `<token>` in `tortoise_bots_pool_state` as applied, preventing repeated resets on subsequent reboots.
- `always`: Resets the pool on every server restart. This is intended strictly for development and testing environments, as all bot progression is wiped every reboot.

Resetting requires `AiPlayerbot.RandomBotAutoCreate = 1`. For complete operational instructions, see [Resetting the managed bot pool](../bot-pool.md#reset-modes).

</details>

### `AiPlayerbot.RandomBotAccountPrefix`
Sets the username prefix used when auto-creating new managed pool accounts.
**Default:** `RNDBOT` · **Values:** string text · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the account name prefix assigned to newly created bot accounts (such as `RNDBOT004213`).

This prefix is also used at the server console to list legacy accounts available for adoption. Note that naming prefix alone does not grant pool status: an account must be registered in the `tortoise_bots_pool_account` database table to be recognized as a managed pool account.

</details>

### `AiPlayerbot.PinnedBots`
Comma-separated list of bot character names pinned to stay online permanently in the pool.
**Default:** `(empty)` · **Values:** comma-separated names · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Specifies character names that should remain logged into the world continuously, exempt from regular pool rotation and idle logouts.

Pinned bots log in when pool logins run (gated by `RandomBotLoginWithPlayer` when enabled) and remain in the world. Names that do not exist or do not belong to the managed bot pool are ignored with a log notice.

</details>

### `AiPlayerbot.RandomBotStartLevelMin` / `RandomBotStartLevelMax`
Defines the level range randomly assigned to a newly created pool bot on its very first login.
**Default:** `1` / `60` · **Values:** integers 1..60 · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Newly generated random bots receive a random level between `RandomBotStartLevelMin` and `RandomBotStartLevelMax` on their initial login, before starter skills, professions, and starter equipment kits are initialized. This ensures a fresh pool populates every level bracket immediately across the game world.

To test specific level brackets, you can narrow this range (for example `10` / `15`). To start all bots at level 1 and have them level naturally, set both keys to `1`.

</details>

### `AiPlayerbot.LevelLadder`
Rotates the active online bot population across structured level bands.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, the server ensures online pool bots are distributed evenly across level brackets (such as 1-5, 6-10, up to 56-59, with level 60 capped at a set percentage). This prevents high-level bots from crowding out low-level leveling zones.

Related tuning settings:

- `AiPlayerbot.LevelLadderBandSize` (default `5`): Width in levels for each ladder band.
- `AiPlayerbot.LevelLadderMaxLevelShare` (default `10`): Percentage ceiling for level 60 bots in the online pool.
- `AiPlayerbot.LevelLadderLogMinutes` (default `5`): Frequency in minutes for logging ladder distribution summaries.

Setting `LevelLadder = 0` reverts to ring rotation over the whole managed pool.

</details>

### `AiPlayerbot.LevelLadderBandSize`
Number of character levels grouped into each ladder band when calculating level distribution.
**Default:** `5` · **Values:** integer levels · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the width of each leveling bracket (for example, 5 levels groups characters into 1-5, 6-10, 11-15, etc.).

When bots log in, the level ladder evaluates shortfalls in each band to keep bot numbers balanced across leveling zones rather than clustering in one level range.

</details>

### `AiPlayerbot.LevelLadderMaxLevelShare`
Maximum percentage share of total online target bot population allocated to level 60 characters.
**Default:** `10` · **Values:** percentage (1..100) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Caps the proportion of max-level (level 60) characters permitted in the active online pool.

A ceiling of 10% ensures end-game bots do not crowd out leveling bots in open-world zones, while still keeping capital cities and high-level dungeons populated.

</details>

### `AiPlayerbot.LevelLadderLogMinutes`
Interval in minutes between log messages reporting active bot population distribution by level band.
**Default:** `5` · **Values:** minutes (minimum effective 1) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Emits a periodic summary table in the server log showing how many bots are currently online in each level band and how closely the ladder matches target quotas.

Values below 1 are clamped to a 1-minute cadence, so logging cannot be disabled entirely.

</details>

### `AiPlayerbot.RandomBotTimedLogout`
Enables timed logout rotation for random pool bots.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled (`1`), autonomous pool bots are periodically logged out and rotated once their online time exceeds `MaxRandomBotInWorldTime`.

Set to `0` to keep active pool bots online indefinitely until server restart or explicit logout.

</details>

### `AiPlayerbot.MaxRandomBotInWorldTime`
Maximum duration in seconds a random bot remains logged into the world before session relog.
**Default:** `21600` (6 hours) · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Configures the upper session duration limit (default 6 hours) an autonomous pool bot may stay online before being cycled out for another character from the offline pool.

Requires `AiPlayerbot.RandomBotTimedLogout = 1`.

</details>

### `AiPlayerbot.MinRandomBotRandomizeTime` / `MaxRandomBotRandomizeTime`
Minimum and maximum interval in seconds between unseeded-gear backstop sweeps.
**Default:** `21600` / `86400` · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines a randomized timer interval (default 6 to 24 hours) acting as an unseeded-gear backstop for pool bots.

When the timer fires, if an eligible pool bot (level 5+) entered the world unseeded, it initializes its initial gear and spells (`UpdateGearSpells`). It never overwrites earned equipment on seeded bots or shifts talent specializations.

</details>

### `AiPlayerbot.RandomBotStallRelogMinutes`
Relogs a sub-60 bot that earns no experience for this many minutes.
**Default:** `45` · **Values:** `0` disabled, `1`..N minutes · **Restart:** needed

<details>
<summary>How it works in detail</summary>

In a living world pool, roughly 5% of leveling bots may get stuck in idle states or pathing dead-ends after a level-up.

When enabled, any bot below level 60 that fails to gain experience within this time window is logged out and cycled back into the pool queue. Set to `0` to disable stall detection.

</details>

### `AiPlayerbot.RandomBotMaxLevel`
Caps the equipment and item lookup tables used when random bots roll for gear.
**Default:** `60` · **Values:** integers 1..60 · **Restart:** needed

<details>
<summary>How it works in detail</summary>

This key acts as a ceiling for the equipment cache lookups used by random bots. It does not cap the bot's actual character level, but restricts what tiers of items the bot AI can evaluate. Lower this value (for example `40`) when running specialized low-level realm experiments.

</details>

### `AiPlayerbot.minEnchantingBotLevel`
Sets the minimum character level at which bots automatically receive permanent gear enchantments.
**Default:** `10` · **Values:** integers 1..61 · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bots at or above this level automatically receive permanent item enchantments from `ai_playerbot_enchant_candidates` based on class, specialization, and stat weighting. Only tier-0 non-reputation enchant sources are selected.

Below this level, gear remains unenchanted. Setting this to `61` completely disables autonomous bot enchanting.

</details>

### `AiPlayerbot.RandomBotInvitePlayer`
Allows masterless pool bots in the open world to invite solo human players to questing groups.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, wandering random bots near a solo human player of similar level (within spell distance) will periodically send group invitations to form questing parties.

Set to `0` to prevent unprompted group invitations from random bots.

</details>

### `AiPlayerbot.RandomBotGroupNearby`
Allows masterless pool bots to spontaneously form questing parties and dungeon groups with one another.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

By default, random bots do not form autonomous bot-only groups in the open world. In bot-only groups, members follow a designated bot leader, which often causes followers to idle while waiting for the leader. Grouping invitations within autonomous guilds and raids are additionally supported via `RandomBotGuildNearby` (default `1`) and `RandomBotRaidNearby` (default `1`).

Setting this to `1` enables organic bot-to-bot grouping. Note that this switch does not affect player invitations, which are handled separately by `RandomBotInvitePlayer`.

</details>

### `AiPlayerbot.RandomBotRaidNearby`
Allows masterless pool bots to form and invite nearby bots into raid groups.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, bots forming groups can convert them to raid groups and invite nearby bots of matching level to raid parties.

</details>

### `AiPlayerbot.RandomBotGuildNearby`
Allows masterless pool bots to invite nearby unguilded bots into their guild.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, bot guild members will periodically invite nearby guildless bots into their guild.

</details>

### `AiPlayerbot.InviteChat`
Announces group and raid invitations in say or guild chat.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, bots announce their group and raid invitations with chat lines. Guild invites use this channel directly; the spoken join line additionally requires EnableBroadcasts = 1.

Announcements only trigger when inviting a real human player. Bot-to-bot invitations suppress chat announcements to prevent starting zones from being flooded with chatter during pool population.

</details>

### `AiPlayerbot.RandomBotFormGuild`
Allows autonomous random bots to buy guild charters, collect signatures from other bots, and found guilds.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, eligible random bots purchase guild charters from guild masters, seek out signatures from fellow bots, and establish autonomous bot guilds.

Once founded, bot guilds invite additional bots or human players, and display guild tags above members' heads.

</details>

### `AiPlayerbot.GuildMaxBotLimit`
Maximum membership limit of bot members allowed in a single autonomous bot guild.
**Default:** `1000` · **Values:** integer count (1..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Caps the total number of bots that can join a single autonomous bot guild.

If a guild exceeds this membership cap, bots will begin leaving the guild autonomously to maintain reasonable guild sizes and distribute bots across multiple guilds.

</details>

### `AiPlayerbot.EnableGreet`
Master switch for bot greetings when passing real players.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls whether bots acknowledge passing human players with friendly text emotes. Greetings trigger at most once per encounter.

Bots never greet other bots, preventing chat spam in crowded areas. Setting this to `0` completely silences all greetings across all bots.

</details>

### `AiPlayerbot.GreetCooldown`
Cooldown in seconds before a bot may greet the same human player again.
**Default:** `600` · **Values:** seconds (0..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Prevents repetitive greeting spam when a player repeatedly walks in and out of a bot's perception radius. A player is greeted at most once within this time window by any given bot.

</details>

### `AiPlayerbot.RandomBotGreet`
Allows masterless pool bots to greet players in addition to player-owned bots.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

By default (`0`), greeting emotes are restricted to bots with a live player master. This prevents hundreds of roaming pool bots in starting zones from overwhelming visitors with greeting emotes.

Set to `1` to allow all masterless open-world pool bots to greet players.

</details>

### `AiPlayerbot.RandomBotShowHelmet` / `RandomBotShowCloak`
Controls whether pool bots wear their helmet and cloak.
**Default:** `1` / `1` · **Values:** `0` hide, `1` show · **Restart:** needed (takes effect on the bots' next login after restart; no database reset required)

<details>
<summary>How it works in detail</summary>

Set to `0` and pool bots (the masterless bots of the shared pool) hide their helmet and/or cloak on their next login. The setting does not touch other characters: hired companions and a player's own bots keep the display flags of the character, which individual players manage per character with the `flag helm|cloak on|off|toggle` chat command. The setting lives in the character's flags, so it survives restarts and follows the bot.

</details>

### `AiPlayerbot.PoolBotTradeMode`
Controls whether masterless pool bots accept direct trade window requests from human players.
**Default:** `0` · **Values:** `0` off, `1` trusted, `2` buy-only, `3` sell-only, `4` unrestricted · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Governs player trading with masterless random bots:

- `0` (off): A masterless pool bot refuses trade windows opened by stranger players.
- `1` (trusted): The bot trades only with its party master, group members, or hire master.
- `2` (buy-only): The bot accepts items or gold from players but gives nothing back.
- `3` (sell-only): The bot gives items or gold to players but accepts nothing.
- `4` (unrestricted): Full open trading with any player.

Owned bots and hired companions always trade freely with their master regardless of this setting. Out-of-range values fail closed to `0`. Background bot-to-bot trading (roleplay item giveaways, enchanting services) remains active regardless of this setting.

</details>

### `AiPlayerbot.TradeActionExcludedPrefixes`
Defines chat prefixes that bots ignore so addon communications do not trigger trade interactions.
**Default:** `RPLL_H_,DBMv4` · **Values:** comma-separated prefix strings · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Specifies message prefixes from addons (by default `DBMv4` and `RPLL_H_`) that bots must ignore during chat parsing. This prevents automated addon broadcast lines from accidentally opening trade interactions with nearby bots.

In the Trade channel, bots only respond when explicitly addressed by name.

</details>

### `AiPlayerbot.TurtleMountAtLevel`
Grants the custom Swift Riding Turtle slow mount to random bots once they reach this level.
**Default:** `18` · **Values:** `0` disabled, integer level (1..39) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Random pool bots between this level and level 39 automatically learn the Swift Riding Turtle mount (associated with quest 40302 and spell 30174) during character seeding and level-up updates.

Setting this to `0` disables early turtle mounting. Standard mounts at level 40 (60% speed) and level 60 (100% speed) function independently of this setting.

</details>

### `AiPlayerbot.LevelUpMounts`
Automatically awards standard racial riding skills and mounts when bots reach level 40 and 60.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, a pool bot that levels up to 40 automatically learns apprentice riding and its race's 60% speed ground mount. At level 60, it learns journeyman riding and its race's 100% epic mount.

This award is idempotent and will not duplicate mounts already owned. If set to `0`, bots leveled up from 1 keep only their turtle mount until manually re-seeded.

</details>

### `AiPlayerbot.RandomBotShopMounts`
Restricts random bots and hired companions to mounts matching their own character race.
**Default:** `0` · **Values:** `0` racial only, `1` any mount · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `0`, random bots and hired companions ride strictly standard vendor mounts that correspond to their race (normal mounts at 40, epic mounts at 60). Cross-race mounts, store mounts, and rare raid or dungeon mount drops are omitted.

Setting this to `1` restores older behavior, allowing bots to ride any mount including cross-race and rare drop mounts. Player-owned alternate characters are never restricted by this setting.

</details>

### `AiPlayerbot.RepopAtGraveyard`
Causes dead masterless pool bots to release spirit at the nearest graveyard instead of racial starter zones.
**Default:** `1` · **Values:** `0` starter zone, `1` nearest graveyard · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, dead pool bots without a live player master release to the nearest suitable graveyard (falling back to starting area coordinates). If disabled (`0`), dead bots repopulate at their race's default starting coordinates.

</details>

### `AiPlayerbot.AvoidHostileTowns`
Prevents masterless random bots from entering towns and travel paths guarded by hostile faction guards.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, masterless bots reject travel destinations, grind hubs, and movement paths that pass within aggro distance of hostile faction town guards. Neutral town bruisers (such as Booty Bay or Gadgetzan) do not block travel, but bots will avoid starting fights in those zones.

This setting also enables the town death-loop rescue (requiring [`AiPlayerbot.RelocateHopelessDeaths = 1`](#aiplayerbotrelocatehopelessdeaths)): a bot killed near hostile town guards twice within 30 minutes is safely relocated to a friendly area on its next revive.

</details>

### `AiPlayerbot.RelocateHopelessDeaths`
Rescues bots stuck in fatal death loops by relocating them to a safe nearby hub.
**Default:** `1` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a masterless bot suffers repeated deaths in the same hostile area or becomes trapped in an inescapable graveyard loop, the unstuck engine teleports the bot's spirit or corpse to its starting area (< level 10) or a level-fitting overworld point.

</details>

### `AiPlayerbot.UnstuckHearthLevelFit`
Requires a bot's homebind location to match its current level bracket before allowing hearthstone unstuck casts.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a masterless bot is stuck, it will only cast its Hearthstone if its bound inn is within 10 levels of the bot's current character level. If the bind point is outgrown, the bot repopulates at a local graveyard instead.

Setting this to `0` allows a stuck bot to hearth to its bound inn regardless of how severely under-leveled that location is.

</details>

### `AiPlayerbot.StuckTravelTeleport`
Safely teleports masterless bots to their travel destination if pathfinding makes no progress after multiple attempts.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

If a masterless pool bot fails to make progress on a long-distance travel trip for six consecutive movement updates, it teleports directly to the destination point or the nearest route node within 200 yards.

Teleporting only functions on the same map, never fires while any human player is near either the bot or the destination, is rate-limited to once per 10 minutes per bot, and never applies to player-owned bots or hired companions (autonomous alts still qualify for the same-map recovery). This frees bots stranded swimming in ocean geometry.

Setting this to `0` causes the bot to abandon the destination and pick a new target instead.

</details>

### `AiPlayerbot.UnstuckHearthMinDistance`
Minimum yard distance between a stuck bot and its homebind location required before casting Hearthstone.
**Default:** `300` · **Values:** yards (0..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A stuck bot must be at least this many yards away from its bound innkeeper before using its Hearthstone. If closer than this threshold, the unstuck handler repopulates the bot locally rather than burning a 30-minute cooldown to move a trivial distance.

Set to `0` to allow hearthstone unstuck casting at any distance.

</details>

### `AiPlayerbot.BotQuestLogUpkeep`
Enables autonomous quest log cleaning and quest progression prioritization on masterless pool bots.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, masterless pool bots actively manage their quest logs:

- Completed quests are prioritized for turn-in until none remain.
- Grey quests lacking gear upgrades are skipped.
- Obsolete or failed quests are abandoned when the quest log nears capacity.
- War-effort item turn-ins and banned joke quests are declined.

Banned quests are dropped across all bots including owned companions. Setting this to `0` restores legacy unmanaged questing behavior. Owned bots and alts are otherwise unaffected.

</details>

### `AiPlayerbot.LeaveOutgrownZones`
Directs leveling bots to migrate out of low-level zones into level-appropriate areas.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A masterless bot at level 10 or higher that outlevels its current non-capital zone by 5 or more levels will automatically walk to a grind hub matching its level. Bots idling in capitals without pending trainer, vendor, repair, mount, mailbox, or auction errands (after a 10-minute grace period or upon an empty grind streak) will similarly depart for level-appropriate zones.

</details>

### `AiPlayerbot.AllowIsolatedCustomStartingZones`
Controls whether newly created random bots are permitted to spawn in custom isolated starting zones.
**Default:** `0` · **Values:** `0` normalize to mainland, `1` allow custom zones · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `0`, newly created random bots generated for custom starting islands (such as Blackstone Island or Alah'Thalas) are automatically normalized and relocated to standard mainland racial starting zones.

Set to `1` to permit random bots to spawn directly into custom island zones.

</details>

### `AiPlayerbot.EnableRandomTeleports`
Scatters newly seeded random bots at level 10 and above to validated level-fitting overworld destinations on first login.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, random bots at level 10 and above scatter to a validated level-fitting overworld spot upon initial login instead of lingering in their racial starting zone.

Level appropriateness is validated via zone level tables and area data. Pinned bots and dead bots stay with their corpse and are skipped.

</details>

### `AiPlayerbot.RandomBotEvenStartZones`
Distributes newly created pool bots evenly across all standard racial starter zones.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, the character auto-creation routine evaluates character counts across all six starter zones and generates new bots in the least-populated zones, keeping starting valleys balanced.

Setting this to `0` uses uniform random race rolls without population balancing.

</details>

### `AiPlayerbot.RandomBotFactionRatio`
Target percentage of newly created random bots assigned to the Alliance faction.
**Default:** `50` · **Values:** percentage (0..100) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Specifies the faction split for newly generated pool characters. A value of `50` balances Alliance and Horde populations equally. Set to `90` for an Alliance-dominated realm, or `10` for Horde dominance. Values outside 0..100 log an error and clamp to the nearest edge (0 or 100).

The level ladder service uses this ratio as a soft preference when filling online level bands (without forced logouts).

</details>

### `AiPlayerbot.RandomBotNameStyleWeights`
Weight distribution of character naming styles used when generating new bot names.
**Default:** `55,20,25` · **Values:** three non-negative integers with a positive sum (e.g. `55,20,25`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the distribution among three distinct naming styles for pool bots and hires:

1. **Race-flavored (55%):** Names generated using letter frequency chains trained on lore-appropriate racial names.
2. **Compound (20%):** Two-word names combining class and fantasy roots.
3. **Fun / Casual (25%):** Creative puns, jokes, and memorable casual character names.

Every generated name passes standard server character-name validation filters (length, profanity, duplicate checks). Use `100,0,0` for a strict roleplay realm, or `0,0,100` for a meme-heavy realm.

</details>

### `AiPlayerbot.RandomBotNamesFile`
Specifies an optional external text file containing custom name lists for new bot characters.
**Default:** `(empty)` · **Values:** file path string · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Provides an optional path to a text file containing structured name lists (e.g. `[rp:human:male]`, `[fun:mage]`, `[pet:hunter]`). Names loaded from this file supplement the built-in naming pools.

Read once at the first name pick; restart to reload. If the file cannot be opened or parsed, an error is logged and built-in name generators are used.

</details>

### `AiPlayerbot.RandomBotStartZoneWeights`
Specifies custom population weights for each racial starting zone.
**Default:** `(empty)` · **Values:** comma-separated name=weight pairs · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Allows administrators to weight specific starter zones during bot creation (for example `Durotar=100,Elwynn=50`). If left empty, bots are distributed evenly across starter zones via `RandomBotEvenStartZones`.

Malformed (or all-zero) text logs an error and keeps the even spread; weights only control where level-1 bots start (level 10+ seeds still scatter).

</details>

### `AiPlayerbot.AllowRepairedTaxiPaths`
Enables or disables flight paths known to have missing waypoint data in client data.
**Default:** `0` · **Values:** `0` disabled, `1` enabled · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Certain taxi paths in game data (such as path 355 from Shimmerstar Lake to Valormok, and path 274) have missing nodes that crash world servers if flown by bots without node reconstruction.

Leaving this at `0` prevents bots from taking these routes while leaving all standard flight paths active. Set to `1` only if running a core build that rebuilds missing nodes on startup.

</details>

### `AiPlayerbot.AmmoStacksNonHunter`
Number of 200-round ammunition stacks stocked by warriors and rogues using ranged weapons.
**Default:** `2` · **Values:** integers 1..10 · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Determines how many stacks of arrows or bullets a warrior or rogue carries for pulling and ranged attacks. Clamped between 1 and 10 stacks. Hunters are governed separately by dedicated ammunition stocking rules.

</details>

### `AiPlayerbot.UseWanderAsDefaultFollowStrategy`
Assigns the wander strategy by default to masterless bots after strategy resets.
**Default:** `1` · **Values:** `0` follow, `1` wander · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, a masterless bot that undergoes a strategy rebuild or respawn receives the `wander` strategy, allowing it to explore the surrounding area within its leash distance.

Bots with a live human player master always rebuild with `follow` so they stay anchored to their party leader.

</details>

### `AiPlayerbot.WanderMinDistance`
Minimum yard radius masterless bots roam from their anchor while wandering.
**Default:** `5.0` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** distance in yards · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the minimum free-move wander radius for bots running the `wander` strategy.

</details>

### `AiPlayerbot.WanderMaxDistance`
Maximum yard radius masterless bots roam from their anchor while wandering.
**Default:** `50.0` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** distance in yards · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the outer free-move boundary radius for bots running the `wander` strategy. Candidates for gathering, grinding, and roleplay interactions outside this radius are rejected.

</details>

### `AiPlayerbot.AutoLearnQuestSpells`
Automatically teaches class spells awarded upon completing specific class quests.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Automatically teaches abilities rewarded by completed class quests (such as paladin resurrection spells, warlock pet summon rituals, or shaman totems).

</details>

### `AiPlayerbot.AutoLearnTrainerSpells`
Grants random pool bots free class trainer spells immediately upon leveling up.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, masterless pool bots automatically learn all eligible trainer spells for free upon gaining a level.

When disabled (`0`), bots do not receive free sweeps, but instead visit class trainers and spend their earned gold to purchase spells. Player-owned bots never receive free spells either way.

</details>

### `AiPlayerbot.AutoLearnDroppedSpells`
Teaches dungeon and raid dropped ability books to high-level random pool bots for free.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `1`, random pool bots automatically learn dungeon and raid spell book ranks for free once they reach the required level (entries at levels 48, 50, 56, and 60 depending on class). When `0`, these spells must be acquired through normal item drops.

</details>

### `AiPlayerbot.RandomGearUpgradeEnabled`
Seeds newly created pool characters with a full set of level-appropriate starter equipment.
**Default:** `1` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

On a newly created bot's first login (played time 0), this feature equips the character with a complete set of level-appropriate armor, weapons, and accessories suited to its specialization.

This routine only fills initial equipment slots and never overwrites gear earned by active veteran bots.

</details>

### `AiPlayerbot.RandomGearWhitelist`
Comma-separated list of item IDs that pool bots are explicitly allowed to equip.
**Default:** `(empty)` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** comma-separated item IDs · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines a whitelist of specific item IDs that bypass candidate filters. Whitelisted items are always considered valid equipment candidates when bots evaluate item upgrades. An ID that is also on the blacklist stays blocked: the blacklist wins.

</details>

### `AiPlayerbot.RandomGearProgression`
Enables progressive gear upgrades matching level bands and progression milestones.
**Default:** `1` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls whether pool bot equipment generation follows tiered item level progression matching dungeon and content milestones across leveling bands.

When enabled (`1`), equipment rolls align with progressive tier caps. Setting this to `0` allows bots to roll any item fitting their level without progression gating.

</details>

### `AiPlayerbot.RandomGearBlacklist`
Excludes specific item IDs from being generated by random gear seeding or upgrade systems.
**Default:** `(empty)` · **Values:** comma-separated item IDs · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines a blacklist of item IDs (such as `12345,23456`, IDs on database.turtle-wow.org) that no auto-gear path hands out: random seeds, hire gear sets, owned-bot `.bot autogear`, the `.autogear` player command and upgrade suggestions. The ban also covers the empty-slot fallback and shirts/tabards, and wins over the whitelist. A pool or hired bot already wearing a blocked item gets it replaced or removed on its next gear pass; real players and owned bots never have their own gear taken off, and items a bot finds itself (loot, quest rewards, auction house) are unaffected.

</details>

### `AiPlayerbot.AutoEquipUpgradeLoot`
Directs bots to automatically equip better gear acquired from looting or quest rewards.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, bots continuously evaluate items placed in their inventory from looting, trades, or quest rewards. If an item surpasses their currently equipped gear by the upgrade threshold, the bot equips it.

</details>

### `AiPlayerbot.EquipUpgradeThreshold`
Minimum stat weight advantage required before a bot replaces an equipped item.
**Default:** `1.1` · **Values:** float multiplier (1.0..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines how much better an item must be before a bot swaps it in. A value of `1.1` requires an item to beat the current item's stat score by at least 10%, preventing constant gear swapping over trivial stat differences.

A value of `1.0` allows any marginal stat gain to trigger an equipment swap.

</details>

### `AiPlayerbot.AutoPickReward`
Automates quest reward item selection when bots complete quests.
**Default:** `yes` · **Values:** `yes`, `no`, `ask` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls how alt bots handle a quest with a choice of rewards. Other bots always pick themselves.

- `yes`: The bot always picks the best item itself.
- `no`: The bot always lists every reward in chat and waits for you to choose with `r [item]`.
- `ask`: The bot takes the reward itself when only one is useful to it, and lists the useful ones and waits for `r [item]` only when more than one is useful. In the same party, some bots may ask while others pick right away.

</details>

### `AiPlayerbot.AutoPickTalents`
Automates talent point spending based on the bot's specialization.
**Default:** `full` · **Values:** `full`, `semi`, `no` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `full`, bots automatically spend available talent points upon leveling up according to predefined spec talent templates.

`semi` applies talent points only if exactly one spec matches the bot's current spec. `no` disables automatic talent allocation.

</details>

### `AiPlayerbot.AutoTrainSpells`
Directs bots to purchase available spell ranks when visiting class trainers.
**Default:** `yes` · **Values:** `yes`, `free`, `no` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `yes`, bots visiting class trainers automatically spend accumulated gold to train all available spells and ability upgrades.

`free` auto-trains spells with no gold cost; `no` only lists available spells.

</details>

### `AiPlayerbot.XPRate`
Multiplier applied to the server's base experience rate specifically for bots.
**Default:** `3` · **Values:** float multiplier · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the bot experience gain multiplier relative to the core server XP rate (`Server XP Rate * AiPlayerbot.XPRate`) for masterless pool bots and hired companions. Autonomous alts and player-owned bots use the standard server experience rate directly.

</details>

### `AiPlayerbot.RandomGearMaxSourceTier`
Restricts the highest loot source tier permitted when generating seed or hire equipment.
**Default:** `0` · **Values:** `0` base/world, `1` endgame dungeons, `2` raids · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls item sourcing tiers for fresh bot seeds and hired companions:

- `0`: Base tier only (world drops, vendors, normal quests, crafting recipes).
- `1`: Base tier plus end-game dungeons (e.g. Stratholme, Scholomance, Blackrock Spire).
- `2`: Base and dungeon tiers plus raid content.

</details>

### `AiPlayerbot.RandomGearAllowReputation`
Allows reputation-gated items to appear in fresh bot equipment seed rolls.
**Default:** `0` · **Values:** `0` disallowed, `1` allowed · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `0`, items requiring specific reputation standings (from vendors or faction quests) are excluded from random starter equipment pools. Set to `1` to allow reputation items on newly created bots.

</details>

### `AiPlayerbot.RandomGearAllowPvP`
Allows PvP honor rank gear to appear in fresh bot equipment seed rolls.
**Default:** `0` · **Values:** `0` disallowed, `1` allowed · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `0`, PvP honor rank rewards and PvP-set items are excluded from seed gear generation. Hired bots similarly skip PvP-flagged items. Does not strip earned PvP gear.

</details>

### `AiPlayerbot.RandomGearSeedEpicChance`
Probability per equipment slot that a fresh seed rolls a rare world epic item.
**Default:** `0.02` · **Values:** float probability (0.0..1.0) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Level 30+ only; below level 30 the roll never fires. Sets the per-slot chance (default 2%) that a newly seeded bot rolls a Bind-on-Equip world epic item instead of standard green or blue equipment. If no eligible epic exists for that slot, it falls back to standard gear.

</details>

### `AiPlayerbot.RandomGearMaxLevel`
Caps the maximum item level considered when selecting random equipment.
**Default:** `100` · **Values:** integer item level · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the absolute item level ceiling for random equipment pools, accommodating high-tier end-game items.

</details>

### `AiPlayerbot.RandomGearMaxItemLevelAboveLevel`
Caps generated pool-bot gear relative to the bot's own level.
**Default:** `1` · **Values:** integer item levels · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Generated item level must not exceed `bot level + this value`, and never exceeds `AiPlayerbot.RandomGearMaxLevel`. The default `1` means a level-60 pool bot tops out at item level 61, roughly what its own drops and dungeons give.

It applies to fresh seeding and to every later re-gear of a wandering pool bot. Gear a bot earns itself — loot, quest rewards, vendor and auction purchases — is organic and is not capped. `0` keeps `AiPlayerbot.RandomGearMaxLevel` as the only ceiling. Standard (tier 0) hired companions use [`AiPlayerbot.HireGearMaxItemLevelAboveLevel`](hiring.md#aiplayerbothiregearmaxitemlevelabovelevel) instead, and the curated tier 1-7 hire kits are unaffected.

</details>

### `AiPlayerbot.ForceRebuffOnReadyCheck`
Delays ready check confirmation until out-of-combat buffs are refreshed.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, a bot receiving an out-of-combat ready check immediately reports its pending status but delays final confirmation until all expiring party buffs are refreshed (with an 8-second grace window after casting or a 30-second total safety cap).

When disabled (`0`), bots confirm ready immediately and may begin buffing during the pull.

</details>

### `AiPlayerbot.RpgDelay`
Delay in milliseconds between autonomous roleplay interactions.
**Default:** `10000` · **Values:** milliseconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Controls the delay cadence between autonomous roleplay (RPG) interactions.

</details>

### `AiPlayerbot.RndBotCheats`
List of convenience cheats granted to masterless random pool bots and hired companions.
**Default:** `repair,breath,item` · **Values:** comma-separated cheat names · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Enables baseline cheats for autonomous pool bots and hired companions to sustain themselves in the open world:

- `repair`: No durability loss, including resurrection sickness.
- `breath`: Grants underwater breathing to prevent pathing bots from drowning during water navigation.
- `item`: Grants basic conjured sustenance items (water, food, ammo) as needed.

Player-owned alts use `BotCheats` instead.

</details>

### `AiPlayerbot.RandomBotNonCombatStrategies`
Baseline non-combat strategy kit assigned to autonomous pool bots.
**Default:** `+grind,+loot,+custom::say,+tfish,+wander,+rpg craft` · **Values:** strategy change string · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the default non-combat behavior package assigned to masterless pool bots. These strategies govern autonomous gathering, grinding, looting, fishing, wandering, and crafting.

When a pool bot enters a dungeon or raid, or when it is invited into a party by a human player, autonomous wandering, grinding, and fishing strategies are automatically stripped so the bot faithfully follows its party master.

</details>

### `AiPlayerbot.RandomBotCombatStrategies`
Default combat strategy modifications applied to masterless pool bots.
**Default:** `+dps assist,-threat` · **Values:** strategy change string · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines baseline combat strategy adjustments applied to pool bots after their specialization defaults are initialized.

Note that `-threat` only applies to masterless pool bots; once a bot joins a human player's party, standard threat management is restored. Tank bots maintain tank-assist behavior regardless of these settings.

</details>

### `AiPlayerbot.CombatStrategies`
Global combat strategies applied to owned and player-mastered bots across the server.
**Default:** `(empty)` · **Values:** strategy change string · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Allows server administrators to specify universal combat strategy modifiers that apply to player-owned and player-mastered bots on the realm; masterless pool bots use `RandomBotCombatStrategies`.

</details>
