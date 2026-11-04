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
| [`.bot ah status`](#bot-ah-status) | GM & Console | Reports synthetic seller metrics. |
| [`.bot ah reload`](#bot-ah-reload) | GM & Console | Reloads custom prices and blacklists from the database. |
| [`.bot ah rebuild`](#bot-ah-rebuild) | GM & Console | Triggers a market evaluation pass. |
| [`.bot ah item`](#bot-ah-item) | GM & Console | Inspects, resets, or overrides item pricing rules. |
| [`.ahbot`](#ahbot) | GM & Console | Shorthand alias for `.bot ah` commands. |
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

These commands monitor and control the synthetic auction house seller engine. All `.bot ah` commands also work through the `.ahbot` alias.

Buyer participation and synthetic supply are configured in [Economy Settings](settings/economy.md#aiplayerbotahmarketbuyer) (`AiPlayerbot.AhMarketBuyer` and `AiPlayerbot.AhMarketSyntheticSupply`). There is no live `.bot ah buyer`, `.bot ah synthetic`, or `.bot ah reset` toggle subcommand.

### `.bot ah status`

Shows operational metrics for the synthetic seller: phase, synthetic auction counts, stock size, override count, slice timings, and cumulative listed/bought/expired/protected-bid counts.

```text
.bot ah status
```

### `.bot ah reload`

Reloads custom item pricing overrides and blacklists from the `ahbot_items` database table into memory.

```text
.bot ah reload
```

Use this after editing prices or blacklists in the database to apply the change without restarting.

### `.bot ah rebuild`

Marks a market evaluation pass pending. It runs on the next tick, not instantly.

```text
.bot ah rebuild [all]
```

Without arguments, rebuilds the market from the current stock. Passing `all` also expires every active unbid synthetic listing first, fully refreshing the stock. Listings with bids are protected even during a full rebuild.

### `.bot ah item`

Inspects, resets, or overrides custom pricing and posting rules for one item entry ID.

```text
.bot ah item <id>
.bot ah item <id> reset
.bot ah item <id> <value> [chance] [min] [max]
```

- `<id>`: reports whether the item is blacklisted (values are not shown).
- `<id> reset`: removes the custom row for that item, restoring baseline pricing.
- `<id> <value> [chance] [min] [max]`: sets a custom price in copper, posting chance percentage, and stack size bounds. Defaults: `chance` 100, `min` 1, `max` 1 (if `max` is omitted it equals `min`).
- **Blacklisting:** a row with `<value>` `0` or `[chance]` `0` (for example `.bot ah item 1234 0 0`) marks the item blacklisted, so the synthetic seller never posts it.

### `.ahbot`

Shorthand alias for `.bot ah` commands.

```text
.ahbot <status|reload|rebuild|item>
```

Accepts the same arguments as `.bot ah status`, `.bot ah reload`, `.bot ah rebuild`, and `.bot ah item`. Bare `.ahbot` prints the same help as `.bot ah`.

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
