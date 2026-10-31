---
id: developers-guild-supply
title: Guild Supply Chain
category: developers
summary: "How guild orders use the guild bank as the crafting store: ladder, refunds, bot rank and bank access, sub-orders."
tags: [developer, guild-orders, guild-bank, supply]
relates_to:
  - play-guild-orders
  - developers-architecture
  - developers-host-api
---

# Guild Supply Chain

How guild orders use the guild bank as the crafting store. Each section below is owned by one worker of issue #1384.

## Supply ladder

Each active order tracks its current supply step and item in the character database. Craft orders resolve the first missing material recursively: a known intermediate recipe is considered before farming that intermediate. For each resulting item the action sequence is bags and mailbox, guild bank, auction house, self gathering/farming/crafting, then `RequestSupply`; a false sub-order result becomes `needitems`.

Bank messages run through `GuildBank::HandleAddonMessages` on the world thread. Map-thread actions enqueue plain bot GUIDs; the world-thread drain re-resolves the order and banker, checks current inventory capacity, and withdraws no more than the order's missing count. A refusal advances the ladder and remains visible as the order reason. Guild gold uses the same queue and core handler, with the order cap and previously withdrawn amount stored on the order row.

`needitems` is a waiting state, so its timer and no-progress clock stay stopped. The store checks bags and mailbox each update and checks the guild bank on a ten-second interval; the order becomes working again when a listed missing amount can be supplied. The whispered need token is persisted to prevent repeating the same list after another source replan.

The `TBM:ORDER` line keeps its existing fields and appends parent, gold cap, step and need. Step and need changes go through the normal world-thread update so list replies and live lines use one formatter.

## Refunds and retry rules

Unspent guild gold (`guildGoldHeld`) is repaid with `DepositMoney` through the same banker path. Finished and cancelled rows park (`cancelled`) instead of disappearing while gold is owed; the refund drain pays from any banker visit and the store clears the row once the gold is back. A new order for the same bot inherits the debt.

Bank table reads are hints: the core commits saves through the async delay queue, so a re-read right after a withdraw or deposit still shows the old value. The item drain reads `guild_bank` once per banker visit, iterates its rows, and treats only the bot's bag-count delta as truth; a failed row skips to the next instead of aborting the visit. The money drain never re-reads: a `WithdrawMoney` the core refuses leaves the bot's money unchanged, which the drain reports as `BankDenied`. An empty bank visit marks the item skipped for ten minutes, and a gold top-up is tried once per auction listing. A leg whose travel search comes back empty three times in a row moves down the ladder the same way (mailbox: skip the mail leg for ten minutes and continue; bank: bank-refused; auctioneer or vendor: next leg), so an unreachable destination never parks the order. The auction leg also commits to the first picked city (faction auction house first, else neutral; other cities are filtered out) and counts arrived trips that end with no auctioneer in reach on the leg, so re-picks between cities neither reset the search streak (a leg or item change restarts it, not a landed pick) nor flap the destination; at `AiPlayerbot.GuildOrderAuctionVisits` such visits (default 3, 0 disables) the leg moves down the ladder by itself.

## Sub-orders

`RequestSupply(parentBot, item, count)` runs on a bot's map thread: under the store lock it only checks the toggle, the parent row, and an already-running row for the same item in the branch, then records the request in a per-(parent, item) queue slot. The world-thread `Update` drains due requests: it still holds the parent row, enforces the depth limit (`AiPlayerbot.GuildOrderSubOrderDepth`, top-level order counts 1) and the loop guard (same item never twice in one branch, both in `GuildOrderSubOrderPolicy.h`), enumerates guild members by one `guild_member` query per due request, keeps durable module bots (pool-account or claimed, never alts or hires) that are online in the order's guild, and ranks them makers-first through the policy pick. Busy candidates re-queue on a 45 s timer; terminal failures (no candidate, depth/loop refused, cant, player cancel) all report through `NoteSubOrderFailed`, which merges the item onto the parent's need list.

Sub-order rows carry `parentBotGuidLow` and `rootGiverGuidLow` (player who gave the root order), persisted in `parent_bot`/`root_giver` columns. Live `TBM:ORDER` lines go to the root giver, with `<parent>` holding the parent bot name, so the addon indents by parent. Each sub-order inherits the parent's gold cap value, and every withdrawal on the branch counts against each ancestor's cap up to the root (one shared budget, per-branch-row inherited values).

Cancelling a parent cancels its whole branch (`BranchBots`, parked refund stubs excluded); children keep what they gathered because the bank deposit path stands alone per order. `reassign` moves a sub-order row to a free guild bot with the same recipe-or-farm gate, recomputing craft-vs-farm for the new bot, allowed for the same ranks that may give orders; offline targets are refused.

## Bot rank and bank access

Every bot in a guild with at least one real player member holds the guild's bot rank so all bots share one rank the master can open bank tabs to. All-bot pool guilds are out of scope: they keep their own ranks (including a bot guild master, which is never moved), nobody gives orders there, and their bots keep managing themselves. The whole feature is an on/off switch (`AiPlayerbot.GuildOrderBotRank`, default on); when it is off the server applies no ranks and `TBM:ORDER_ACCESS` carries no rank information (`-|-|-|-|0|0`).

- **Default rank:** the highest rank below the guild master (rank index 1). Guilds without a stored choice use the default; the switch being off disables everything. Rank 0 (guild master) is never moved by any rank path, and an incoming rank 0 (guild creation) is never rewritten on join.
- **Join:** `ClaimGuildAdapter::OnAddMember` rewrites the joining member's rank through `GuildOrders::JoinBotRank` before the core inserts the row, so a bot joins at the bot rank at once. The rule uses one durable bot definition online and offline (claimed into a guild or on a random-pool account, never an autonomous alt-bot); real players' own characters keep the core-assigned rank. The hook runs on the world thread and recomputes the cached management verdict as a side effect (the live joiner counts on top of the stored roster).
- **Scope:** `GuildHasRealPlayerMember(guildId, excludeGuidLow)` decides from the database on the world thread only — join, leave, rank command, startup. The exclude parameter covers the remove hook, where the leaver's row is already gone from the in-memory roster but its database delete is still queued. AI ticks never run this query.
- **Change:** `.bot order rank <rankIndex>` (promote+demote right required) stores the choice in `tortoise_bots_guild_bot_rank` (always a real rank 1..lowest) and moves every bot via `ApplyBotRankToGuild`, which reads the roster once for both the scope check and the moves and refreshes the verdict.
- **Management gate:** `GuildOrders::BotRankBlocksManagement` is a cache read only (`BotRankStore`, unknown means not blocked), so map-thread AI ticks never touch the roster or the database. The world thread keeps it warm: join/leave hooks, rank command, disband, and the startup sweep. It is true exactly when the guild has a real player member and the bot rank is active. The four guild management actions (invite, promote, demote, remove) report not-useful there, as does `guild manage nearby` (which also returns early from `Execute`).
- **Access line:** `GuildOrders::FormatAccessLine` reads the tab minimum ranks from `guild_bank_tabs` (the same table the core bank loads, so no new seam) and computes `TBM:ORDER_ACCESS` from the pure policy in `runtime/GuildOrderAccessPolicy.h`. No bank row means no tabs and no gold. The gold bit follows the core money rule (only ranks 0-1 may withdraw) once a bank exists. The red header rule is the contract: any denied tab, or no gold.
