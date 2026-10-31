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

Every bot in a guild holds the guild's bot rank so all bots share one rank the master can open bank tabs to.

- **Default rank:** the highest rank below the guild master (rank index 1). `AiPlayerbot.GuildOrderBotRank` overrides the default for guilds without a stored choice; `0` disables the feature. Rank 0 (guild master) is never assigned to bots.
- **Join:** `ClaimGuildAdapter::OnAddMember` rewrites the joining member's rank through `GuildOrders::JoinBotRank` before the core inserts the row, so a bot joins at the bot rank at once. The rank rule only counts bot characters (module-owned sessions, claimed bots, random-pool accounts); real players' own characters (including autonomous alt-bots) keep the core-assigned rank.
- **Startup:** `GuildOrders::ApplyBotRanksAtStartup` walks `guild_member` once and moves every bot to the guild's bot rank. Cheap by design: one query, no per-tick scans.
- **Change:** `.bot order rank <rankIndex>` (promote+demote right required) stores the choice in `tortoise_bots_guild_bot_rank` and moves every bot via `ApplyBotRankToGuild`.
- **Access line:** `GuildOrders::FormatAccessLine` reads the tab minimum ranks from `guild_bank_tabs` (the same table the core bank loads, so no new seam) and computes `TBM:ORDER_ACCESS` from the pure policy in `runtime/GuildOrderAccessPolicy.h`. The gold bit follows the core money rule (only ranks 0-1 may withdraw).
