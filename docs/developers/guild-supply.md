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

Every bot in a guild with at least one real player member holds the guild's bot rank so all bots share one rank the master can open bank tabs to. All-bot pool guilds are out of scope: they keep their own ranks (including a bot guild master, which is never moved), nobody gives orders there, and their bots keep managing themselves. The whole feature is an on/off switch (`AiPlayerbot.GuildOrderBotRank`, default on); when it is off the server applies no ranks and `TBM:ORDER_ACCESS` carries no rank information (`-|-|-|-|0|0`).

- **Default rank:** the highest rank below the guild master (rank index 1). Guilds without a stored choice use the default; the switch being off disables everything. Rank 0 (guild master) is never moved by any rank path, and an incoming rank 0 (guild creation) is never rewritten on join.
- **Join:** `ClaimGuildAdapter::OnAddMember` rewrites the joining member's rank through `GuildOrders::JoinBotRank` before the core inserts the row, so a bot joins at the bot rank at once. The rule uses one durable bot definition online and offline (claimed into a guild or on a random-pool account, never an autonomous alt-bot); real players' own characters keep the core-assigned rank. The hook runs on the world thread and recomputes the cached management verdict as a side effect (the live joiner counts on top of the stored roster).
- **Scope:** `GuildHasRealPlayerMember(guildId, excludeGuidLow)` decides from the database on the world thread only — join, leave, rank command, startup. The exclude parameter covers the remove hook, where the leaver's row is already gone from the in-memory roster but its database delete is still queued. AI ticks never run this query.
- **Change:** `.bot order rank <rankIndex>` (promote+demote right required) stores the choice in `tortoise_bots_guild_bot_rank` (always a real rank 1..lowest) and moves every bot via `ApplyBotRankToGuild`, which reads the roster once for both the scope check and the moves and refreshes the verdict.
- **Management gate:** `GuildOrders::BotRankBlocksManagement` is a cache read only (`BotRankStore`, unknown means not blocked), so map-thread AI ticks never touch the roster or the database. The world thread keeps it warm: join/leave hooks, rank command, disband, and the startup sweep. It is true exactly when the guild has a real player member and the bot rank is active. The four guild management actions (invite, promote, demote, remove) report not-useful there, as does `guild manage nearby` (which also returns early from `Execute`).
- **Access line:** `GuildOrders::FormatAccessLine` reads the tab minimum ranks from `guild_bank_tabs` (the same table the core bank loads, so no new seam) and computes `TBM:ORDER_ACCESS` from the pure policy in `runtime/GuildOrderAccessPolicy.h`. No bank row means no tabs and no gold. The gold bit follows the core money rule (only ranks 0-1 may withdraw) once a bank exists. The red header rule is the contract: any denied tab, or no gold.
