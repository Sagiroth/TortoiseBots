---
id: server-settings-queues
title: Queue settings (LFT & battlegrounds)
category: server
summary: Configuration settings for Looking for Turtles (LFT) dungeon autofill and battleground auto-queue systems.
tags: [config, queues, lft, dungeon-finder, battlegrounds, pvp]
relates_to:
  - world-queues
  - server-settings-performance
---

# Queue Settings (LFT & Battlegrounds)

These settings configure automated Looking for Turtles (LFT) dungeon finder autofill and player-versus-player (PvP) battleground queue services. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

For general matchmaking behavior, dungeon portal pathing, and battleground mechanics, see [Queues & Matchmaking](../../world/queues.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.RandomBotLftEnabled`](#aiplayerbotrandombotlftenabled) | `1` | Enables random bots filling vacant slots in human LFT dungeon queues. |
| [`AiPlayerbot.RandomBotLftAllowRoleBorrow`](#aiplayerbotrandombotlftallowroleborrow) | `0` | Allows LFT to borrow and respec bots into roles they do not naturally fit. |
| [`AiPlayerbot.RandomBotLftUpdateInterval`](#aiplayerbotrandombotlftupdateinterval) | `15000` | Milliseconds between evaluation passes of the LFT autofill service. |
| [`AiPlayerbot.RandomBotLftMaxFillsPerInterval`](#aiplayerbotrandombotlftmaxfillsperinterval) | `1` | Maximum bots queued into dungeon groups per autofill service tick. |
| [`AiPlayerbot.RandomBotBgEnabled`](#aiplayerbotrandombotbgenabled) | `1` | Injects bots into Warsong Gulch, Arathi Basin, and Alterac Valley when players queue. |
| [`AiPlayerbot.RandomBotBgAutonomous`](#aiplayerbotrandombotbgautonomous) | `1` | Seeds bot-only Warsong Gulch matches when no human players are in queue. |
| [`AiPlayerbot.RandomBotBgAutonomousMaxInstances`](#aiplayerbotrandombotbgautonomousmaxinstances) | `1` | Maximum concurrent bot-only battleground instances allowed per level bracket. |
| [`AiPlayerbot.RandomBotBgQueueInterval`](#aiplayerbotrandombotbgqueueinterval) | `30000` | Milliseconds between autonomous battleground seeder queue evaluation passes. |
| [`AiPlayerbot.RandomBotBgMaxQueuePerInterval`](#aiplayerbotrandombotbgmaxqueueperinterval) | `1` | Number of bots queued into autonomous battlegrounds per seeder check. |

## Settings

### `AiPlayerbot.RandomBotLftEnabled`
Enables automatic bot backfill for players queued in the Looking for Turtles (LFT) tool.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Fill is demand-driven: with no human player waiting, bots queue nothing new (pending offers are still reconciled). Fill-owned bots accept the dungeon invite themselves; humans always accept their own. The fill service itself never teleports anyone: the formed party walks to the portal. (A separate follow catch-up may summon a bot whose human master is already inside a dungeon or raid.)

If you queue as a party with your own bots, they accept on the same tick even with `RandomBotLftEnabled = 0`. The toggle only gates random-pool backfill.
</details>

### `AiPlayerbot.RandomBotLftAllowRoleBorrow`
Allows the LFT autofill service to borrow and respec bots into missing group roles.
**Default:** `0` · **Values:** `0` strict spec matching, `1` allow borrowing · **Restart:** needed

<details>
<summary>How it works in detail</summary>

By default (`0`), role assignment is fail-closed on each bot's natural spec and gear. A tank slot needs a dedicated tank with a shield, a druid tank needs bear form, a shaman tank needs tank talents or the tank kit, and a healer needs healing talents and spells. If no matching bot is online, the slot waits.

Setting this to `1` lets the matcher pick a bot and rebuild it into the missing role (forced role plus talent rebuild). Even with `0`, a bot whose spec fits but whose installed kit does not (for example a feral druid defaulting to Cat queued as a tank) still gets the matching kit.

</details>
### `AiPlayerbot.RandomBotLftUpdateInterval`
Interval in milliseconds between LFT queue evaluation passes.
**Default:** `15000` · **Values:** milliseconds, clamped 5000..60000 in service · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines how often the LFT autofill service scans the matchmaking queue for waiting players needing bot companions.

</details>

### `AiPlayerbot.RandomBotLftMaxFillsPerInterval`
Maximum number of bots queued into dungeon groups per autofill service tick.
**Default:** `1` · **Values:** integer count (0 disables; above 5 capped at 5 in service) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Limits how many bots are queued into dungeon groups within a single update interval, spreading strategy reconfigurations smoothly across time.

</details>

### `AiPlayerbot.RandomBotBgEnabled`
Enables automatic bot matchmaking for battleground PvP queues when players queue.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Monitors PvP queues for Warsong Gulch (WSG), Arathi Basin (AB), and Alterac Valley (AV). When real human players queue, random bots of the matching level bracket queue on both factions toward the core's minimum player requirement plus one.

This launches battleground matches within seconds even on low-population private servers. When a match concludes, bots leave rather than waiting for instance shutdown timers.

Your own party bots can queue alongside you via "Join as Group" at battlemasters (supported for WSG and AB; AV needs solo queues in the core) and auto-accept match invites regardless of this toggle. Hired companions refuse battleground invites while `HireAllowPvP = 0`, and the service never queues a party that includes a hire then.

</details>
### `AiPlayerbot.RandomBotBgAutonomous`
Allows pool bots to initiate bot-only Warsong Gulch matches when no human players are queued.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When no human players are waiting in any PvP queue, the system seeds 10v10 Warsong Gulch matches in the most populated level bracket so bots have competitive matches of their own.

Human player demand always takes precedence: the autonomous seeder halts immediately whenever a real human queues for any battleground. This feature requires `AiPlayerbot.RandomBotBgEnabled = 1` to operate.

</details>

### `AiPlayerbot.RandomBotBgAutonomousMaxInstances`
Limits how many bot-only battlegrounds can run at once per level bracket.
**Default:** `1` · **Values:** integers (0..2) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Caps full bot-only matches per level bracket. A still-forming match keeps absorbing queued seeds and does not count; top-up stops once both sides reach 10 or a full match forms. The default of 1 suits average hardware. Raise to 2 only on fast dedicated servers.

</details>

### `AiPlayerbot.RandomBotBgQueueInterval`
Interval in milliseconds between queue checks for the autonomous battleground seeder.
**Default:** `30000` · **Values:** milliseconds, clamped 5000..600000 in service · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines how frequently the autonomous seeder checks whether an additional bot can be queued into an unfilled bot-only match.

Human demand is checked every 5 s (up to 10 bots per check); the interval and budget pace the autonomous seeder; setting `AiPlayerbot.RandomBotBgMaxQueuePerInterval = 0` disables both paths.

</details>

### `AiPlayerbot.RandomBotBgMaxQueuePerInterval`
Number of bots queued per interval check by the autonomous battleground seeder.
**Default:** `1` · **Values:** integer count 0..10 (0 disables both autonomous and human demand queues; above 10 capped at 10 in service) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

How many bots the autonomous seeder queues per check into a forming bot-only match.

Human-demand backfill always uses a fixed pace (10 bots per 5-second check, 5 per side); this knob only switches it off: setting it to `0` disables both autonomous seeding and human-demand backfill.

</details>
