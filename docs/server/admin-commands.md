---
id: server-admin-commands
title: Administrator & GM Commands
category: server
summary: GameMaster and server console commands for managing the bot pool, synthetic auction house, and server state.
tags: [admin, gm, commands, bot-pool, ahbot]
relates_to:
  - server-bot-pool
  - server-configuration
  - server-settings-economy
  - server-settings-world-population
---

# Administrator & GM Commands

Admin commands manage the managed bot pool, the synthetic auction house seller, and config reloading. Pool inspection needs GameMaster rank or the server console; adoption and account listing need the server console. All `.bot ah` commands also work through the `.ahbot` alias.

## Summary

| Command | Available To | What It Does |
| :--- | :--- | :--- |
| [`.bot pool status`](#bot-pool-status) | GM & Console | Shows pool reset status, generation, and account totals. |
| [`.bot pool accounts`](#bot-pool-accounts) | Console Only | Lists managed accounts and character counts. |
| [`.bot pool adopt preview`](#bot-pool-adopt-preview) | Console Only | Previews unmanaged prefix-matching accounts. |
| [`.bot pool adopt confirm`](#bot-pool-adopt-confirm) | Console Only | Enrolls previewed accounts into the managed pool. |
| [`.bot ah status`](#bot-ah-status) | GM & Console | Reports AH bot and organic market metrics. |
| [`.bot ah reload`](#bot-ah-reload) | GM & Console | Reloads AH bot configuration, item pool, and pricing. |
| [`.bot ah empty`](#bot-ah-empty) | GM & Console | Removes all AH bot listings and refunds active bidders. |
| [`.bot ah update`](#bot-ah-update) | GM & Console | Triggers an immediate AH bot sell and buy cycle. |
| [`.ahbot`](#ahbot) | GM & Console | Shorthand alias for `.bot ah` commands. |
| [`.bot camp`](#bot-camp) | GM (In-Game) | Validates, previews, and removes autonomous bot camp spots. |
| [`.reload config`](#reload-config) | GM & Console | Reloads core configuration (module keys need a restart, with exceptions). |

---

## Managed Bot Pool Administration (`.bot pool`)

These commands manage the autonomous world population and account registration. For the full pool lifecycle, see [Resetting the Managed Bot Pool](bot-pool.md).

Pool refill is automatic: the random-bot service creates bots toward the configured target, bounded per tick, gated by `RandomBotAutoCreate`, up to the minimum of the configured pool size. Reset modes are configured at startup via `AiPlayerbot.RandomBotPoolReset` (see [World Population Settings](settings/world-population.md#aiplayerbotrandombotpoolreset)). There is no live `.bot pool` reset subcommand.

### `.bot pool status`

Shows the health, account counts, and reset state of the managed bot pool. Works in-game as a GameMaster and at the server console.

```text
.bot pool status
```

Reports the configured reset mode, the requested one-time generation token, the applied generation, the managed account total (and whether validated), the managed character total, the current reset phase, progress (settled auctions or deleted characters), pool availability, and the last failure reason if a reset halted.

### `.bot pool accounts`

Lists every managed pool account in the bot manager.

```text
.bot pool accounts
```

**Server console only.** Shows each managed pool account ID, account name, registration source, and character count.

### `.bot pool adopt preview`

Scans the character database for accounts matching the configured bot account prefix that are not yet managed.

```text
.bot pool adopt preview
```

**Server console only.** Read-only. Shows matching accounts and character counts, then generates a confirmation token (valid for 5 minutes).

### `.bot pool adopt confirm`

Enrolls the previewed accounts into the managed pool registry.

```text
.bot pool adopt confirm <challenge>
```

**Server console only.** Requires the token from `.bot pool adopt preview` (valid for 5 minutes). Idempotent: already-registered accounts are counted, not errored. Registers accounts only; characters and accounts are never modified, moved, or deleted.

---

## Autonomous Auction House Administration (`.bot ah` / `.ahbot`)

These commands monitor and control the AH bot service (`AiPlayerbot.AhBot.*`) and inspect the organic market service (`AiPlayerbot.AhMarket*`). All `.bot ah` commands also work through the `.ahbot` alias.

AH bot settings are configured in [Economy Settings](settings/economy.md#aiplayerbotahbotenabled) (`AiPlayerbot.AhBot.Enabled` and `AiPlayerbot.AhBot.Buyer.Enabled`). Item filtering, list proportions, and pricing overrides live in `aiplayerbot.conf` rather than the database; the legacy `ahbot_items` table has been removed.

### `.bot ah status`

Shows operational metrics for the AH bot (enabled state, seller account health, item pool counts, per-house auction counts vs targets, pending queues, listed/bid/buyout/expiry counts, slice timings) and the organic market service.

```text
.bot ah status
```

### `.bot ah reload`

Reloads `AiPlayerbot.AhBot.*` configuration from `aiplayerbot.conf`, rebuilds the AH bot item pool, and refreshes pricing rules and vendor caches.

```text
.bot ah reload
```

Use this after editing `aiplayerbot.conf` to apply AH bot changes without restarting.

### `.bot ah empty`

Removes all active AH bot listings (and any legacy owner-0 listings) across all auction houses. Active bidders are refunded via the core auction-cancel mail path. Player and pool-bot listings are unaffected.

```text
.bot ah empty
```

### `.bot ah update`

Triggers an immediate AH bot sell and buy cycle (subject to per-tick budgets; requires `AiPlayerbot.AhBot.Enabled = 1`).

```text
.bot ah update
```

### `.ahbot`

Shorthand alias for `.bot ah` commands.

```text
.ahbot <status|reload|empty|update>
```

Accepts the same arguments as `.bot ah status`, `.bot ah reload`, `.bot ah empty`, and `.bot ah update`. Bare `.ahbot` prints the same help as `.bot ah`.

---

## Autonomous Camp Spot Management (`.bot camp`)

In-game GameMasters can add, preview, and remove autonomous bot camp spots stored in `ai_playerbot_camp_spot` and loaded into `CampMgr`.

### `.bot camp`

Lists the available `.bot camp` subcommands.

```text
.bot camp
```

### `.bot camp add [name]`

Validates and saves a camp spot (`source = 1`) at your current position and facing. Defaults the name to `<zone name> camp` if omitted.

```text
.bot camp add [name]
```

Before saving, checks that no existing spot is within 25 yd, that all four layout points (tent, cast, fire, seat) are outdoors, out of liquid, and reachable on the navmesh, and that the 4-point elevation spread is within 3 yd. On success, inserts the row into the world database and adds it to `CampMgr` live without a restart.

### `.bot camp show`

Finds the nearest camp spot within 60 yd and spawns temporary visuals (faction tent, Basic Campfire, and a seat marker) where a bot places them. Visuals despawn automatically after 3 minutes and are never saved to the database. Replies with the spot id, name, distance, and measured 4-point Z spread.

```text
.bot camp show
```

### `.bot camp remove`

Removes the nearest camp spot within 10 yd (any source) from the world database and `CampMgr` live. Note that a removed `source = 0` spot returns on the next server boot unless also deleted from `data/sql/world/20261115180000_world.sql`.

```text
.bot camp remove
```

---

## System & Diagnostic Commands

### `.reload config`

Reloads server configuration files without restarting.

```text
.reload config
```

Available to GameMasters and the server console. Core configuration (`mangosd.conf`) reloads. Module gameplay settings in `aiplayerbot.conf` load once at startup and need a restart, except the run leader keys, the dashboard stream keys, and `TortoiseBots.LogLevel` (see [Restart versus reload](configuration.md#restart-versus-reload)).

---

## GM AI Inspection and Cheats (Whisper Commands)

GameMasters can inspect bot internals, alter combat state, and adjust debug logging with whisper commands (`/w <BotName> <command>`). These whisper commands are GameMaster-only. In-game GMs can also forward a command to an owned online bot with:

```text
.bot command <BotName> <Playerbot command>
```

This forwards the whisper command to that bot; it cannot be used from the server console (a player requester is required). Action success is not guaranteed.

- `cheat`: toggles bot cheat flags used for testing.
- `debug` / `cdebug` / `cs`: combat engine diagnostics (strategies, action priorities, activity evaluations).
- `log`: reports or changes the bot's own log verbosity level.
- `set value <variable> <value>`: modifies runtime AI memory values (supports `group size`, `guild size`, and `rpg style`).
- `teleport`: uses a nearby spellcaster game object portal, or replays the bot's last area trigger.
- `load ai <profile>` / `save ai <profile>`: loads or saves strategy presets to and from the `ai_playerbot_db_store` database table (empty profile means the default preset).
- `list ai`: lists the presets stored for that bot in `ai_playerbot_db_store`.
- `reset ai` / `reset strats` / `reset values`: resets internal AI trees and values (`reset ai` also clears stored values; `reset strats` and `reset values` reset strategies or values respectively).
