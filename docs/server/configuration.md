---
id: server-configuration
title: How Configuration Works
category: server
summary: Configuration files, templates, restart rules, recommended presets, settings index, and removed dead keys.
tags: [config, server, administration]
relates_to:
  - server-settings-party
  - server-settings-hiring
  - server-settings-class-behaviour
  - server-settings-dungeons-raids
  - server-settings-combat
  - server-settings-world-population
  - server-settings-economy
  - server-settings-queues
  - server-settings-performance
  - server-settings-chat
  - server-settings-logging
---

# How Configuration Works

TortoiseBots provides configuration files to customize bot intelligence, party behavior, world immersion, and server performance. This page explains how configuration files work, their lifecycle, recommended presets, and which dead settings were removed.

## Configuration philosophy

The default settings ship the intended vision of a living world and faithful vanilla gameplay. Bots level organically, use class abilities intelligently, and participate in the world without artificial shortcuts.

Alternate playstyles are supported through configuration options rather than unconfigurable hardcoding:
- Features that alter progression, such as faster bot experience rates, equipment seeding, synthetic auction house supply, teleportation travel, or activity throttling, live behind explicit configuration toggles.
- Systems such as mercenary hiring gear ladders and dungeon follow holds provide configurable knobs to tailor the experience to private realms or solo play.
- Principle: default configurations aim to preserve standard gameplay, while config toggles give server administrators explicit choice.

## Configuration files & lifecycle

Bot settings live in two files created during installation:

1. `aiplayerbot.conf`: located next to `mangosd.conf`. This file contains all gameplay flags, player quality-of-life toggles, artificial intelligence thresholds, and autonomous services. The shipped template is `ai/playerbot/aiplayerbot.conf.dist.in` (installed as `aiplayerbot.conf.dist`). A custom path can be specified using `AiPlayerbot.ConfigFile` in `mangosd.conf`.
2. `modules/tortoise_bots.conf`: located in the server configuration directory. This file contains module-level options such as logging verbosity and telemetry. The shipped template is `conf/tortoise_bots.conf.dist`.

### Installation and updates

The first server installation creates both configuration files from their respective `.dist` templates. Subsequent server updates never overwrite existing `.conf` files. When updating the server, compare your active files against the updated `.dist` templates to discover newly added keys. Any setting omitted from your active configuration file automatically falls back to its built-in default.

### Restart versus reload

Configuration settings are loaded once when the server boots. The console command `.reload config` re-reads only `TortoiseBots.LogLevel`. All other settings require a full server restart to take effect.

A line that is commented out with `#` in the configuration file sets nothing; the hardcoded C++ code default applies.

## Recommended presets

Once enabled (`AiPlayerbot.Enabled = 1`), a full living world runs 500 autonomous bots. The following recommended configuration in `aiplayerbot.conf` configures a balanced living world for an average PC:

```ini
# Master bot toggle
AiPlayerbot.Enabled = 1

# Random bot population pool
AiPlayerbot.RandomBotAutologin = 1
AiPlayerbot.RandomBotAutoCreate = 1
AiPlayerbot.MinRandomBots = 500
AiPlayerbot.MaxRandomBots = 500

# Fresh-bot level seed
AiPlayerbot.RandomBotStartLevelMin = 10
AiPlayerbot.RandomBotStartLevelMax = 15

# Social immersion
AiPlayerbot.RandomBotInvitePlayer = 1
AiPlayerbot.RandomBotGroupNearby = 0
AiPlayerbot.RandomBotFormGuild = 1
AiPlayerbot.EnableGreet = 1

# Optional living economy and queues
AiPlayerbot.AhMarketEnabled = 1
AiPlayerbot.RandomBotLftEnabled = 1
AiPlayerbot.RandomBotBgEnabled = 1
```

## Settings index

Every configuration setting is documented across specialized settings pages:

| Settings Category | Configuration File | What It Covers |
| :--- | :--- | :--- |
| [Party & quality-of-life](settings/party.md) | `aiplayerbot.conf` | Owned bot management, alt-bots, follow catching up, pack holds, and party loot. |
| [Mercenary hiring & recruiters](settings/hiring.md) | `aiplayerbot.conf` | Companion hiring costs, raid composition presets, gear tiers, and world buffs. |
| [Class behaviour](settings/class-behaviour.md) | `aiplayerbot.conf` | Class-specific AI rules for hunters, paladins, rogues, and warlocks. |
| [Dungeons & raids](settings/dungeons-raids.md) | `aiplayerbot.conf` | Creature aura hazard avoidance, bomb dispersal, hazard evasion, and custom raid tactics. |
| [Combat & reaction thresholds](settings/combat.md) | `aiplayerbot.conf` | Health and mana lines, follow distances, casting pacing, and commanded pulls. |
| [World population & immersion](settings/world-population.md) | `aiplayerbot.conf` | Pool sizing, level ladders, wander limits, greetings, guilds, and gear upkeep. |
| [Economy & auction house](settings/economy.md) | `aiplayerbot.conf` | Synthetic auction house market supply, bidding, buyouts, and trade limits. |
| [Dungeon finder & battlegrounds](settings/queues.md) | `aiplayerbot.conf` | Looking for Turtles (LFT) autofill and autonomous battleground queues. |
| [Performance & tick budget](settings/performance.md) | `aiplayerbot.conf` + `mangosd.conf` | Per-tick AI turn limits, self-tuning tick controllers, and core map caching. |
| [Bot chatter & broadcasts](settings/chat.md) | `aiplayerbot.conf` | Channel chat rates, emote lines, say-without-master, and broadcast chances. |
| [Diagnostic logging](settings/logging.md) | `aiplayerbot.conf` + `tortoise_bots.conf` | Module log levels, telemetry streams, CSV logs, and per-bot action log rotation. |

## Removed keys

`aiplayerbot.conf.dist.in` once carried keys that were read into configuration and never used; they are gone now. An older `aiplayerbot.conf` that still sets one of them keeps loading, because the config reader ignores unknown keys. Nothing these keys appeared to control ever changed, so there is nothing to migrate: delete the lines. The one pair from that list that now has an effect is `RandomBotShowHelmet` / `RandomBotShowCloak`: they hide the helmet and cloak of pool bots (default: shown).

Raid diagnostics are separately opt-in through
[`AiPlayerbot.RaidBossDiagnostics`](settings/logging.md#aiplayerbotraidbossdiagnostics)
(default `0`); enabling event CSV files alone does not enable boss observations.
