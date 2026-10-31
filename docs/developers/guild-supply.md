---
id: developers-guild-supply
title: Guild Supply Chain
category: developers
summary: How guild orders use the guild bank as the crafting store, including the bot rank and bank access rule.
tags: [developer, guild-orders, guild-bank]
relates_to:
  - play-guild-orders
  - developers-host-api
---

# Guild Supply Chain

How guild orders use the guild bank as the crafting store. Each section below is owned by one worker of issue #1384.

## Bot rank and bank access

Every bot in a guild with at least one real player member holds the guild's bot rank so all bots share one rank the master can open bank tabs to. All-bot pool guilds are out of scope: they keep their own ranks (including a bot guild master, which is never moved), nobody gives orders there, and their bots keep managing themselves.

- **Default rank:** the highest rank below the guild master (rank index 1). `AiPlayerbot.GuildOrderBotRank` overrides the default for guilds without a stored choice; `0` disables the feature. Rank 0 (guild master) is never moved by any rank path.
- **Join:** `ClaimGuildAdapter::OnAddMember` rewrites the joining member's rank through `GuildOrders::JoinBotRank` before the core inserts the row, so a bot joins at the bot rank at once. The rule uses one durable bot definition online and offline (claimed into a guild or on a random-pool account, never an autonomous alt-bot); real players' own characters keep the core-assigned rank. The join also drops the cached management verdict for the next query.
- **Startup:** `GuildOrders::ApplyBotRanksAtStartup` walks `guild_member` once, groups members per guild in memory, and moves every bot to the guild's bot rank only in guilds with a real player member, seeding each guild's cached management verdict. Cheap by design: one query, no per-tick scans.
- **Change:** `.bot order rank <rankIndex>` (promote+demote right required) stores the choice in `tortoise_bots_guild_bot_rank` and moves every bot via `ApplyBotRankToGuild`, which likewise only touches guilds with a real player member and marks the management verdict stale.
- **Management gate:** `GuildOrders::BotRankBlocksManagement` is true exactly when the guild has a real player member and the bot rank is active. The four guild management actions (invite, promote, demote, remove) report not-useful there, as does `guild manage nearby` (which also returns early from `Execute`). The verdict is cached per guild (`BotRankStore`) and recomputed only when the roster or stored rank changes, so AI ticks never hit the database.
- **Access line:** `GuildOrders::FormatAccessLine` reads the tab minimum ranks from `guild_bank_tabs` (the same table the core bank loads, so no new seam) and computes `TBM:ORDER_ACCESS` from the pure policy in `runtime/GuildOrderAccessPolicy.h`. The gold bit follows the core money rule (only ranks 0-1 may withdraw).
