---
id: server-settings-performance
title: Performance & tick budget settings
category: server
summary: Configuration settings for AI time budgets, activity throttling, world tick targets, and core server tuning for large bot populations.
tags: [config, performance, tick, budget, optimization, mangosd]
relates_to:
  - server-settings-world-population
  - server-dashboard
---

# Performance & Tick Budget Settings

These settings control AI execution scheduling, per-tick time budgets, activity throttling, and core server optimizations for hosting large bot populations. Settings go in `aiplayerbot.conf` and `mangosd.conf`; see [How configuration works](../configuration.md).

The total bot population is the primary factor influencing server CPU and memory usage. When hosting hundreds or thousands of bots, adjusting per-tick budgets and core memory caching ensures smooth gameplay.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.DisableActivityPriorities`](#aiplayerbotdisableactivitypriorities) | `1` | Disables bot activity throttling so all bots remain fully reactive. |
| [`AiPlayerbot.botActiveAlone`](#aiplayerbotbotactivealone) | `10` | Percentage of isolated bots kept active when throttling is enabled. |
| [`AiPlayerbot.ForceActiveWhenNearPlayer`](#aiplayerbotforceactivewhennearplayer) | `0` | Forces bots within human visibility range to remain fully active. |
| [`AiPlayerbot.PoolTickBudgetUs`](#aiplayerbotpooltickbudgetus) | `10000` | Microsecond time budget per world tick for processing pool bots. |
| [`AiPlayerbot.CombatTickBudgetUs`](#aiplayerbotcombattickbudgetus) | `15000` | Microsecond time budget per world tick for processing combat turns. |
| [`AiPlayerbot.PoolBudgetWhenTickOverMs`](#aiplayerbotpoolbudgetwhentickoverms) | `0` | World tick latency threshold in milliseconds where budgets engage. |
| [`AiPlayerbot.TargetWorldTickMs`](#aiplayerbottargetworldtickms) | `100` | Target world tick duration used by the self-tuning budget controller. |
| [`AiPlayerbot.ValueRefreshAtInterval`](#aiplayerbotvaluerefreshatinterval) | `0` | Refreshes cached AI sensory values at full intervals to reduce CPU usage. |
| [`AiPlayerbot.TravelPickBudgetUs`](#aiplayerbottravelpickbudgetus) | `4000` | Microsecond budget limit per bot visit for travel destination calculations. |
| [`AiPlayerbot.AsyncTravelPartitions`](#aiplayerbotasynctravelpartitions) | `0` | Enables asynchronous partitioning of world travel destination lookups. |
| [`AiPlayerbot.GenerateItemCaches`](#aiplayerbotgenerateitemcaches) | `1` | Builds equipment and item candidate database tables on first boot. |
| [`AiPlayerbot.LogInGroupOnly`](#aiplayerbotlogingrouponly) | `1` | Restricts detailed per-action logging output to bots currently in groups. |
| [`AiPlayerbot.Enabled`](#aiplayerbotenabled) | `0` | Master kill-switch enabling or disabling bot artificial intelligence updates. |
| [`AiPlayerbot.PoolMeleeHoldGround`](#aiplayerbotpoolmeleeholdground) | `0` | Disables generic fleeing behaviors on pool melee classes during combat. |
| [`AiPlayerbot.FailedActionRetryBase`](#aiplayerbotfailedactionretrybase) | `250` | Base exponential backoff delay in milliseconds for repeated failed actions. |
| [`AiPlayerbot.FailedActionRetryMax`](#aiplayerbotfailedactionretrymax) | `2000` | Maximum exponential backoff cap in milliseconds for repeated failed actions. |
| [`AiPlayerbot.FailedActionCacheTtl`](#aiplayerbotfailedactioncachettl) | `30000` | Time-to-live in milliseconds for cached failed action backoff records. |
| [`AiPlayerbot.FailedActionCacheMaxEntries`](#aiplayerbotfailedactioncachemaxentries) | `64` (built in) | Maximum number of entries stored in the failed action cache. |
| [`AiPlayerbot.IterationsPerTick`](#aiplayerbotiterationspertick) | `100` | Maximum action evaluation iterations allowed per bot per world tick. |
| [`AiPlayerbot.MaxWaitForMove`](#aiplayerbotmaxwaitformove) | `3000` | Timeout in milliseconds to wait for a movement arrival before expiring. |
| [`AiPlayerbot.RemoteCommander`](#aiplayerbotremotecommander) | `0` | Enables remote commander commands for leading bot-only parties from afar. |
| [`AiPlayerbot.RemoteCommanderViewMs`](#aiplayerbotremotecommanderviewms) | `1000` | Milliseconds between visual perception updates sent to remote commanders. |

### Recommended Core Settings (`mangosd.conf`)

| Setting | Recommended | What it does |
| :--- | :---: | :--- |
| [`CleanupTerrain`](#cleanupterrain) | `0` | Keeps extracted continent terrain in memory, eliminating 2-second map stalls. |
| [`PlayerSave.Interval`](#playersaveinterval) | `300000` | Increases character save interval to 5 minutes to avoid mass write waves. |
| [`DynamicRespawn.MaxReductionRate` / `MinRespawnTime`](#dynamicrespawnmaxreductionrate--dynamicrespawnminrespawntime) | `0.75` / `15` | Speeds up mob respawn rates in crowded bot starter and grinding zones. |
| [`LootsLogFile` / `CharLogFile`](#lootslogfile--charlogfile) | `""` | Disables unrotated core character and loot log files to save disk space. |

## Settings

### `AiPlayerbot.DisableActivityPriorities`
Controls whether distant bots without nearby players are throttled or kept fully active.
**Default:** `1` · **Values:** `0` throttling on, `1` full activity · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Governs how the server handles bots roaming alone in empty zones:

- `1`: Default behavior. Every bot's AI runs at full capability regardless of whether a human player is nearby. All bots remain active, level, and gather naturally.
- `0`: Opt-in throttling mode. Bots near human players, in combat, in instances, or grouped with players remain fully active. "Alone" bots have their update rate significantly reduced.

All bots remain logged into the world in either mode; only their AI decision frequency is throttled.

</details>

### `AiPlayerbot.botActiveAlone`
Percentage of isolated bots kept fully active when activity throttling is enabled.
**Default:** `10` · **Values:** percentage (0..100) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When `DisableActivityPriorities = 0`, this setting defines what percentage of "alone" bots (bots with no human player nearby or in an empty zone) are selected to run full updates. The active subset rotates approximately 1% per minute.

For example, with 20 bots in an empty world, approximately 2 bots will be actively moving and grinding at any given moment.

</details>

### `AiPlayerbot.ForceActiveWhenNearPlayer`
Forces bots visible to human players to always run at maximum reaction rates.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When throttling is enabled, setting this to `1` ensures any bot within human visibility range is immediately promoted to full update frequency, preventing visual stutter or delayed reactions.

</details>

### `AiPlayerbot.PoolTickBudgetUs`
Microsecond execution budget ceiling per world tick for processing pool bots.
**Default:** `10000` · **Values:** microseconds (1000..100000, or 0) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the time budget allocated for updating random pool bots during each server world tick (10,000 microseconds = 10 milliseconds).

Player-owned bots and companions hired by online players always update first, on every tick, and are completely exempt from this budget. The random pool runs afterward as one turn queue, most-overdue-first; bots it does not reach run first on the next tick. The budget is checked between bots so a single pass can overshoot by one bot. Setting this to `0` removes the budget ceiling entirely.

If pool bots feel sluggish or unresponsive, raise `PoolTickBudgetUs` to `25000`–`50000` (25–50 ms).

</details>

### `AiPlayerbot.CombatTickBudgetUs`
Microsecond execution budget ceiling per world tick for processing bots in combat.
**Default:** `15000` · **Values:** microseconds (1000..100000, or 0) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Combines with `PoolTickBudgetUs` to form the overall module execution ceiling per server tick. The pool runs as one turn queue under this combined budget; combat turns are ordered by the scheduler's urgency (combat has a 400 ms cap, so combat bots naturally sort first when late).

Setting this to `0` removes the combat budget limit, executing all combat turns immediately.

</details>

### `AiPlayerbot.PoolBudgetWhenTickOverMs`
Latency threshold in milliseconds where per-tick time budgets begin to throttle pool turns.
**Default:** `0` · **Values:** milliseconds (0..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Specifies when the tick budget engages based on previous world tick latency:

- `0`: Budgets apply to every tick (recommended). The automated self-tuning controller reclaims unused budget on healthy ticks automatically.
- `>0`: Budgets only engage when the previous world tick exceeded this duration in milliseconds (for example `150`).

</details>

### `AiPlayerbot.TargetWorldTickMs`
Target world tick duration in milliseconds used by the automated self-tuning budget controller.
**Default:** `100` · **Values:** milliseconds (0..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

On every tick, the controller feeds the measured world tick through an 8-tick moving average and dynamically scales the effective pool budgets:

- If the smoothed reading exceeds `TargetWorldTickMs + 10 ms`, budgets shrink fast (x0.7 per tick) — but only when the pool turns are what keep the tick over the line. If the core and other services alone keep the tick over, budgets hold instead of starving bot logic.
- If the smoothed reading is at or under the target, budgets grow back slowly (+1 ms per tick) up to their configured ceilings.

The controller only trims bot processing time if doing so will successfully bring the tick below the target line; if the core engine itself is overloaded, bot budgets are preserved to prevent starving bot logic. Setting this to `0` disables dynamic adjustment and locks budgets to static ceilings.

The target trades player lag for bot speed: measured on 2,000 bots, target 50 gave player-lag p95 66 ms with 14.5% stalled turns, while target 100 gave 107 ms with 7.4% stalled turns.

</details>

### `AiPlayerbot.ValueRefreshAtInterval`
Refreshes cached AI sensory values at their full configured interval instead of half-intervals.
**Default:** `0` · **Values:** `0` half-interval, `1` full interval · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bots access world data (targets, inventory, quest states) through cached AI values. By default (`0`), caches refresh after half their interval (for example, a 2-second value refreshes every second).

Setting this to `1` refreshes values only after their full interval elapses, cutting calculation overhead in half. Critical combat and situational values remain on legacy rapid refresh cycles: attackers and attacker counts, aggro, possible attack targets and all targets, nearest adds, stealthed units, possible triggers, damaging traps, boss target, AoE heal, enemy players, free-move values, and grind target.

To evaluate performance gains safely, conduct an A/B test run of at least 20 minutes under each setting: compare `BOTPERF` metrics (`usPerTurn`, `passUs`, and `turnLateAvgMs`) and verify bot combat responsiveness.

</details>

### `AiPlayerbot.TravelPickBudgetUs`
Microsecond budget ceiling per bot visit for calculating long-distance travel destinations.
**Default:** `4000` · **Values:** microseconds (0..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bounds the CPU time spent evaluating candidate travel destinations for a single bot during an AI pass. If candidate evaluation exceeds 4,000 microseconds (4 ms), the calculation pauses and resumes on the bot's next update pass.

This bounds worst-case single-bot latency, preventing expensive path evaluations from creating tick latency spikes. Setting this to `0` runs the entire destination search in a single pass.

</details>

### `AiPlayerbot.AsyncTravelPartitions`
Enables asynchronous partitioning for world travel destination lookups.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Spreads long-range travel destination evaluations across partitioned background passes.

Keep `0` on cores without concurrent terrain loading; only enable on cores with concurrent terrain support to avoid crashes.

</details>

### `AiPlayerbot.GenerateItemCaches`
Builds equipment and item candidate database tables on first startup.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

On initial server boot, if the `ai_playerbot_equip_cache` and `ai_playerbot_rnditem_cache` character-database tables are empty, this setting generates them from the loaded item prototypes.

Once generated, the tables are loaded directly from SQL on subsequent restarts. Leave this setting enabled; if disabled with empty tables, bots cannot judge gear upgrades.

</details>

### `AiPlayerbot.LogInGroupOnly`
Restricts detailed per-action debug logging output to bots currently in a party.
**Default:** `1` · **Values:** `0` all bots, `1` grouped bots only · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When detailed engine logging is enabled, setting this to `1` filters out logging lines from roaming pool bots, writing action detail lines only for bots in an active party.

</details>

### `AiPlayerbot.Enabled`
Master switch enabling or disabling all bot artificial intelligence processing.
**Default:** `0` · **Values:** `0` disabled, `1` enabled · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Global enable switch for the playerbot AI subsystem. When enabled, periodic AI performance metrics are logged every 30 seconds at module log level 1 or higher (`TB_LOG_BASIC` / `TortoiseBots.LogLevel = 1`).

</details>

### `AiPlayerbot.PoolMeleeHoldGround`
Disables generic out-of-combat fleeing behaviors for masterless warrior and rogue pool bots.
**Default:** `0` · **Values:** `0` flee enabled, `1` hold ground · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `1`, masterless pool warriors and rogues are not granted the generic fleeing strategy at all. Instead, they stand their ground and fight using their own defensive abilities, matching classic melee behaviors.

Player-owned bots and party companions are not affected by this toggle.

</details>

### `AiPlayerbot.FailedActionRetryBase`
Base exponential backoff delay in milliseconds when an autonomous action repeatedly fails.
**Default:** `250` · **Values:** milliseconds (`0..2000`; `0` disables failure backoff) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When an autonomous bot action fails (such as an unreachable gathering node or a vendor out of line of sight), this value sets the initial delay before the action may be attempted again.

Combat, reactions, owner commands, and player-owned bots are never throttled. Setting base or max to `0` disables failure backoff entirely.

</details>

### `AiPlayerbot.FailedActionRetryMax`
Maximum exponential backoff delay ceiling in milliseconds for failed actions.
**Default:** `2000` · **Values:** milliseconds (`0..10000`; clamped >= RetryBase) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Caps the exponential backoff delay for persistently failing actions, preventing bots from spinning CPU cycles retrying impossible actions while ensuring they periodically re-check valid targets.

</details>

### `AiPlayerbot.FailedActionCacheTtl`
Time-to-live in milliseconds for cached failed action backoff entries.
**Default:** `30000` · **Values:** milliseconds (`1000..300000`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines how long a failed action state remains cached before being purged from memory.

</details>

### `AiPlayerbot.FailedActionCacheMaxEntries`
Maximum number of entries stored in the failed action cache per bot.
**Default:** `64` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** `1` to `256` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Limits memory footprint of the failed action backoff cache by capping the maximum number of unique tracked failing actions per bot.

Clamped internally between 1 and 256 entries.

</details>

### `AiPlayerbot.IterationsPerTick`
Maximum action engine evaluation iterations allowed per bot per world tick (multiplied by action queue size).
**Default:** `100` · **Values:** integer count (1..N) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets an upper limit on how many sequential candidate actions the bot strategy engine evaluates during a single world tick. The engine multiplies this limit by queue size and halves it for minimal moves.

Prevents infinite evaluation loops when complex trigger conditions or multiple falling-through action priorities occur.

</details>

### `AiPlayerbot.MaxWaitForMove`
Timeout in milliseconds to wait for a movement arrival before expiring the move action.
**Default:** `3000` · **Values:** milliseconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Specifies how long the bot waits for movement execution before declaring a movement action timed out.

If pathing fails to arrive at the destination within this duration, the move action expires so the bot can reconsider targets or recalculate paths.

</details>

### `AiPlayerbot.RemoteCommander`
Enables remote commander commands for leading parties from outside them, of up to five bots from another zone.
**Default:** `0` · **Values:** `0` disabled, `1` enabled · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Enables remote binds (`.bot commander bind` without `inparty`) and every other `.bot cmd` order typed by a player, and the commander view (`.bot commander view`). Without it (`0`), the `inparty` bind, `cmd enter` and `cmd summon`, and the run leader (`.bot run`, behind `AiPlayerbot.DungeonSelfClear = 1`) still work: the remote commander only drives parties from outside. When enabled, a GameMaster or the owner of every bot in it can bind a bot-only party of up to five and command their movement, targeting, and spellcasting by GUID or 3D coordinates from anywhere in the world.

When disabled (`0`), remote binds and the other commander orders report that the feature is off and any existing remote bindings terminate (inparty bindings stay while `AiPlayerbot.DungeonSelfClear = 1`).

</details>
---

## Core Server Settings (`mangosd.conf`)

When hosting hundreds of always-active bots across both continents, the core server configuration in `mangosd.conf` must be tuned to handle continuous world activity:

### `CleanupTerrain`
Controls whether unused continent terrain maps are unloaded from memory.
**Recommended:** `0` · **Core Default:** `1` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With the core default of `1`, terrain maps without active players are freed every 60 seconds. Because roaming bots frequently re-enter these tiles, the core is forced to reload terrain synchronously, creating 2-second world server freezes.

Setting `CleanupTerrain = 0` keeps extracted continent maps permanently in memory. This eliminates terrain-reload stalls completely, at the cost of approximately 2 GB of system RAM.

> [!NOTE]
> Keep `CleanupTerrain = 0` even with the upstream grid fix ([#588](https://github.com/tortoise-wow/tortoise-wow/pull/588)) on bot-heavy realms.

</details>

### `PlayerSave.Interval`
Defines the interval in milliseconds between periodic character database saves.
**Recommended:** `300000` (5 minutes) · **Core Default:** `60000` (1 minute) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Saving hundreds of active bots every 60 seconds produces severe database I/O write spikes. Increasing this interval to 5 minutes (300,000 ms) smooths database throughput. In the event of a crash, at most 5 minutes of character progress is lost.

</details>

### `DynamicRespawn.MaxReductionRate` / `DynamicRespawn.MinRespawnTime`
Controls dynamic creature respawn rate acceleration in crowded zones.
**Recommended:** `0.75` / `15` · **Core Defaults:** `0.25` / `25` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The core shortens creature respawn timers when groups of players gather in the same area. In bot-populated realms, starting valleys and popular grinding camps are cleared rapidly.

Increasing `MaxReductionRate` to `0.75` and lowering `MinRespawnTime` to `15` seconds allows creature spawns to keep pace with bot grinding demand without starving human players.

</details>

### `LootsLogFile` / `CharLogFile`
Specifies log files for recording all character logins and creature looting events.
**Recommended:** `""` (disabled) · **Core Defaults:** `loot.log` / `char.log` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Because the core treats every bot as a native player, every bot loot roll and login creates an entry in these files. With 1,000 bots active, `loot.log` accumulates over 450,000 lines (~50 MB) daily without log rotation.

Setting both filename keys to empty strings (`""`) disables these files, saving disk space and eliminating unnecessary file writes.

</details>

---

## AI Pass Telemetry (`BOTPERF`)

When AI is enabled (`AiPlayerbot.Enabled = 1`), the module outputs an AI performance pass summary line to the server log approximately once every 30 seconds at module log level 1 or higher (`TB_LOG_BASIC` / `TortoiseBots.LogLevel = 1`):

```text
TortoiseBots: BOTPERF passUs=812 playerBots=5 ownedBots=5 masterBots=0 poolBots=495 poolProcessed=4 due=9 budgetHit=1 maxUs=9820 ticks=62 poolBudgetUs=10000 combatBudgetUs=15000 turnLateAvgMs=180 turnLateMaxMs=1400 turns=410 usPerTurn=1900 coreMs=78 combatTurns=95 combatLateAvgMs=60 otherTurns=315 otherLateAvgMs=240
```

The sample above predates `routeReuse`, `routeRebuild`, `keepoutScans`, and `keepoutBudgetMiss`; current log lines always append those four fields.

<details>
<summary>Field Reference & Log Interpretation</summary>

- `passUs`: Average `UpdateBots` pass cost in the window in microseconds.
- `maxUs`: Worst single-tick bot update cost in the sample window.
- `ticks`: World ticks in the window.
- `playerBots`: Count of player-owned bots updated first and unbudgeted. Split into `ownedBots` (owner's own account) and `masterBots` (live real-player master).
- `poolBots`: Total registered pool bots in the background turn queue.
- `poolProcessed`: Pool turns run in the last pass of the window, out of `due` bots due.
- `due`: Number of bots due for evaluation when the window closed.
- `budgetHit`: Set to `1` when the tick budget halted pool updates before all due bots ran.
- `poolBudgetUs` / `combatBudgetUs`: Effective microsecond budgets active during that window. Their sum is the one budget; equal to the configured ceilings when the server is healthy.
- `turnLateAvgMs` / `turnLateMaxMs`: Average and maximum delay beyond a bot's scheduled turn time. This is the direct measure of how far behind the bot queue is running.
- `turns`: Total pool turns run in the window.
- `usPerTurn`: Average microseconds to run one pool turn (turn-loop wall time divided by turns).
- `coreMs`: Average world tick minus the average bot pass (the share of the tick spent by the core and other services, which cutting bots cannot reduce).
- `combatTurns` / `combatLateAvgMs`: Turn count and average lateness for pool turns whose situation was combat.
- `otherTurns` / `otherLateAvgMs`: Turn count and average lateness for all other pool turns.
- `routeReuse` / `routeRebuild`: Counts of movement path lookups answered by cached route versus expensive full recalculations.
- `keepoutScans` / `keepoutBudgetMiss`: Counts of grid visits made for shared creature-aura hazard snapshots versus lookups due a refresh after the tick's snapshot budget was spent.

**Tuning Guidance:**
- `budgetHit = 1` with a low `passUs` indicates the budget throttle is operating normally.
- High `turnLateAvgMs` with frequent `budgetHit = 1` indicates the pool budget is too tight for the active bot count; increase `PoolTickBudgetUs`.
- High overall tick latency with low `passUs` and high `coreMs` indicates the bottleneck is inside core server systems rather than the bot module.

</details>
