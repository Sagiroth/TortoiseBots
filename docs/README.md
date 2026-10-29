---
id: docs-index
title: TortoiseBots Documentation
category: reference
summary: Start page, role-based navigation, task finder, and audience directory for the documentation catalog.
tags: [index, start, navigation, directory]
relates_to:
  - play-getting-started
  - play-features
  - play-commands
  - play-known-limitations
  - server-install
---

# TortoiseBots Documentation

Welcome to the TortoiseBots documentation catalog for Tortoise WoW 1.18.1. This knowledge base covers player commands, class mechanics, living world systems, server configuration, and developer architecture.

## Start Here

- [Getting started](play/getting-started.md) — install the addon, spawn your first alts, and build your party.
- [What bots can do](play/features.md) — complete catalog of bot capabilities, defaults, and feature flags.
- [Commands](play/commands/README.md) — full command directory for party actions, whispers, and roster management.
- [Known limitations](play/known-limitations.md) — engine boundaries, deliberate non-goals, and known quirks.
- [Glossary](play/glossary.md) — what the abbreviations and bot terms mean.
- [Install (for admins)](server/install.md) — build, compile, and configure TortoiseBots with the core server.

## I Want To...

| I want to... | Where to go |
| :--- | :--- |
| Set up the client and summon my first bot alt | [Getting started](play/getting-started.md) |
| See every bot feature and toggle flag in one place | [What bots can do](play/features.md) |
| Order my party to pull a pack and retreat around a corner | [Pullback tactics](play/commands/party-actions.md#bot-action-pullback) |
| Stop bots from breaking crowd control like Polymorph | [Crowd control discipline](play/commands/party-actions.md#bot-action-cc) |
| Direct my party to interrupt enemy spellcasters | [Interrupt command](play/commands/party-actions.md#bot-action-interrupt) |
| Hire a mercenary companion or full raid roster at an inn or capital recruiter | [Hiring mercenaries](play/commands/roster-and-hiring.md#capital-recruiter-services) |
| Learn spells automatically at a class trainer | [Trainer whisper](play/commands/whispers.md#w--trainer) |
| Inspect bot equipment, bags, trade windows, or combat stats | [Bot panel](play/commands/bot-panel.md#bot-panel-commands) |
| Check tactics, kill orders, and mechanics for 5-man dungeons | [Dungeon reference](play/dungeon-reference.md) |
| Handle wipe recovery and run back to the instance | [Wipe recovery](play/dungeons-and-raids.md#wipe-recovery--instance-navigation) |
| Level my companion alts automatically with my character | [Sync alt level](server/settings/party.md#aiplayerbotsyncaltleveltomaster) |
| Fill missing tank or healer roles in dungeon finder queues | [LFT autofill](world/queues.md#looking-for-turtles-lft-dungeon-autofill) |
| Send a guild bot to farm, buy or craft items for the guild bank | [Guild orders](play/guild-orders.md) |
| Enable bot trading and purchasing on the Auction House | [Living AH](world/economy.md#living-auction-house) |
| Blacklist or override an item price on the Auction House | [Admin commands](server/admin-commands.md#autonomous-auction-house-administration-bot-ah--ahbot) |
| Summon lost or stuck companion bots | [Summon alts](play/commands/roster-and-hiring.md#bot-summon) |
| Understand Turtle WoW custom spells and abilities | [Class AI](classes/overview.md) |
| Watch the bot fleet and stuck bots on a live web map | [Observability dashboard](server/dashboard.md) |
| Reset the random bot pool and re-seed character levels | [Bot pool reset](server/bot-pool.md) |
| Build and compile the module from source | [Server install](server/install.md) |
| Port a feature or contribute a bugfix | [Developer index](developers/README.md) |

## Players

| Page | What you find there |
| :--- | :--- |
| [Getting started](play/getting-started.md) | Client setup, `/tbm` addon, spawning owned alts, and first party setup. |
| [What bots can do](play/features.md) | Complete feature catalog, default states, and setting toggles per area. |
| [Command directory](play/commands/README.md) | Overview of all bot commands, permission levels, and channel syntax. |
| [Tactical party actions](play/commands/party-actions.md) | Direct combat intents (`attack`, `pullback`, `interrupt`, `cc`, formations). |
| [Remote commander](play/commands/commander.md) | Directing bot-only parties from anywhere in the world. |
| [Guild orders](play/guild-orders.md) | Sending guild bots to fetch items, order states, and bank delivery. |
| [Roster and hiring](play/commands/roster-and-hiring.md) | Adding alts, hiring mercenaries, recruiter buffs, and gear progression. |
| [Bot panel and inspection](play/commands/bot-panel.md) | Equipment and inventory management, behavior toggles, and diagnostic views. |
| [Whisper commands](play/commands/whispers.md) | Quick one-to-one bot orders grouped by combat, rest, trade, and town. |
| [Addon (TortoiseBotsManager)](play/commands/addon.md) | In-game UI guide and silent addon communication protocol. |
| [Dungeon and raid tactics](play/dungeons-and-raids.md) | Pull mechanics, boss tactics, hazard evasion, wipe runs, and loot rules. |
| [Dungeon reference](play/dungeon-reference.md) | Kill orders, creature IDs, pooled rares, and events for 18 dungeons. |
| [Dungeon run leader](play/dungeon-run-leader.md) | Opt-in `.bot run`: a bound party clears a stored dungeon plan by itself; plan tables and safety rules. |
| [Boss tactics table](play/boss-tactics.md) | Opt-in per-boss rows (`ai_boss_tactic`): interrupts, kill order, stay behind. |
| [Known limitations](play/known-limitations.md) | Engine limits, deliberate non-goals, targeting rules, and known quirks. |
| [Glossary](play/glossary.md) | Abbreviations and TortoiseBots terms in plain words. |
| [Classes overview](classes/overview.md) | Shared class AI, spec detection, combat roles, and interrupt logic. |
| [Warrior](classes/warrior.md) | Stance dancing, Protection tanking, Arms/Fury DPS, and shouts. |
| [Paladin](classes/paladin.md) | Holy healing, Protection tanking, Retribution DPS, and judgements. |
| [Hunter](classes/hunter.md) | Pet upkeep, ranged rotations, melee Survival, and Aspect of the Viper. |
| [Rogue](classes/rogue.md) | Stealth openers, combo finishers, poisons, and Feint/Vanish threat drops. |
| [Priest](classes/priest.md) | Holy/Discipline healing ladder, Power Word Shield rules, and Shadow DPS. |
| [Shaman](classes/shaman.md) | Four-element totem sets, Chain Heal triage, melee Enh, and shocks. |
| [Mage](classes/mage.md) | Frost, Fire, and Arcane rotations, Polymorph crowd control (CC), and Counterspell. |
| [Warlock](classes/warlock.md) | DoT spreading, demon pet selection by spec, Soul Shards, and Life Tap. |
| [Druid](classes/druid.md) | Bear tanking, Cat DPS, Restoration healing, and Tree of Life. |

## What Bots Do on Their Own

| Page | What you find there |
| :--- | :--- |
| [Living world](world/living-world.md) | Autonomous questing, grinding, level seeding, gear upkeep, and travel. |
| [Social](world/social.md) | Solo player invites, autonomous guilds, greetings, and player trading. |
| [Queues and matchmaking](world/queues.md) | Looking for Turtles dungeon autofill and PvP battleground matchmaking. |
| [Economy](world/economy.md) | Autonomous auction house market, vendor trips, repairs, and gold flow. |

## Server Admins

| Page | What you find there |
| :--- | :--- |
| [Installation](server/install.md) | CMake compilation, module dependencies, updates, and version verification. |
| [Configuration](server/configuration.md) | Configuration files, templates, quick presets, and live reload rules. |
| [Admin commands](server/admin-commands.md) | GameMaster commands for bot pools, AH market control, and account adoption. |
| [Bot pool management](server/bot-pool.md) | Managed pool registry, account structures, and safe pool reset procedures. |
| [Observability dashboard](server/dashboard.md) | Real-time web dashboard, 2D live map, Prometheus metrics, and issue tracker. |
| [Run trace](server/run-trace.md) | Opt-in recorder of a party's run (`.bot trace`), the `bt-trace` v1 schema, and the offline scorer. |
| [Party settings](server/settings/party.md) | Follow distances, summon rules, alt-bot persistence, and loot rolling. |
| [Hiring settings](server/settings/hiring.md) | Mercenary hiring fees, recruiter quests, gear tiers, and world buffs. |
| [Class behavior settings](server/settings/class-behaviour.md) | AI flags for Aspect of the Viper, Judgements, threat drops, and demon choices. |
| [Dungeon and raid settings](server/settings/dungeons-raids.md) | Boss encounter flags, bomb runout ranges, and creature aura avoidance. |
| [Combat settings](server/settings/combat.md) | Health and mana percentage thresholds, ranges, global cooldown (GCD) delays, and pull pacing. |
| [World population settings](server/settings/world-population.md) | Bot population limits, level ladder rotation, wandering, greetings, and trading. |
| [Economy settings](server/settings/economy.md) | Auction house market pacing, synthetic supply, and vendor batch sizes. |
| [Queue settings](server/settings/queues.md) | Dungeon finder backfill intervals and battleground queue parameters. |
| [Performance settings](server/settings/performance.md) | AI tick time budgets, activity throttling, and core server optimizations. |
| [Chat settings](server/settings/chat.md) | Automated public broadcasts, frequency throttles, and masterless say chat. |
| [Logging settings](server/settings/logging.md) | Module verbosity, structured CSV logs, action traces, and UDP telemetry. |
| [Hire gear sets](reference/hire-gear.md) | Static level-60 hire equipment sets per class and tier. |

## Developers

| Page | What you find there |
| :--- | :--- |
| [Developer index](developers/README.md) | Contributor workflows, coding standards, tools, and testing commands. |
| [Architecture invariants](developers/architecture.md) | Decoupled C++ modularity, headless session lifecycle, and zero core coupling. |
| [Strategy engine](developers/strategy-engine.md) | AI update loop, action baskets, triggers, multipliers, and failure backoffs. |
| [Bot mechanics and quirks](developers/bot-mechanics.md) | Targeting and interrupt logic, movement physics, gear evaluation, and technical quirks. |
| [Donor hierarchy](developers/donor-hierarchy.md) | Provenance precedence across shyalya, mod-playerbots, and upstream core. |
| [Host API contract](developers/host-api.md) | C++ generic seam interfaces, session hooks, and packet bridge contracts. |
| [Licensing audit](developers/licensing-audit.md) | AGPL and GPL lineage compatibility, donor tracking, and release gates. |
| [Data reference tables](developers/data/README.md) | Overview of TSV reference tables for bot data and donor mapping. |
| [Capabilities census](developers/data/capabilities.tsv) | Tab-separated inventory of donor features vs TortoiseBots implementation. |
| [Donor map](developers/data/donor-map.tsv) | Tab-separated file-by-file mapping of donor lineages and sources. |
| [Spell coverage](developers/data/spell-coverage.tsv) | Tab-separated census of game spells mapped to bot actions and strategies. |
| [Talent builds](developers/data/talent-builds.tsv) | Tab-separated reference of preset leveling talent allocations. |
