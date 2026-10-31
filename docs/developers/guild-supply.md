---
id: developers-guild-supply
title: Guild supply chain
category: developers
summary: Supply ladder, persisted order progress, bank withdrawals, needitems recovery, and sub-orders between guild bots.
tags: [developer, guild-orders, guild-bank, supply]
relates_to:
  - play-guild-orders
  - developers-architecture
---

# Guild Supply Chain

## Supply ladder

Each active order tracks its current supply step and item in the character database. Craft orders resolve the first missing material recursively: a known intermediate recipe is considered before farming that intermediate. For each resulting item the action sequence is bags and mailbox, guild bank, auction house, self gathering/farming/crafting, then `RequestSupply`; a false sub-order result becomes `needitems`.

Bank messages run through `GuildBank::HandleAddonMessages` on the world thread. Map-thread actions enqueue plain bot GUIDs; the world-thread drain re-resolves the order and banker, checks current inventory capacity, and withdraws no more than the order's missing count. A refusal advances the ladder and remains visible as the order reason. Guild gold uses the same queue and core handler, with the order cap and previously withdrawn amount stored on the order row.

`needitems` is a waiting state, so its timer and no-progress clock stay stopped. The store checks bags and mailbox each update and checks the guild bank on a ten-second interval; the order becomes working again when a listed missing amount can be supplied. The whispered need token is persisted to prevent repeating the same list after another source replan.

The `TBM:ORDER` line keeps its existing fields and appends parent, gold cap, step and need. Step and need changes go through the normal world-thread update so list replies and live lines use one formatter.

## Refunds and retry rules

Unspent guild gold (`guildGoldHeld`) is repaid with `DepositMoney` through the same banker path. Finished and cancelled rows park (`cancelled`) instead of disappearing while gold is owed; the refund drain pays from any banker visit and the store clears the row once the gold is back. A new order for the same bot inherits the debt.

Bank table reads are hints: the core commits saves through the async delay queue, so a re-read right after a withdraw or deposit still shows the old value. The item drain reads `guild_bank` once per banker visit, iterates its rows, and treats only the bot's bag-count delta as truth; a failed row skips to the next instead of aborting the visit. The money drain never re-reads: a `WithdrawMoney` the core refuses leaves the bot's money unchanged, which the drain reports as `BankDenied`. An empty bank visit marks the item skipped for ten minutes, and a gold top-up is tried once per auction listing.

## Sub-orders

`RequestSupply(parentBot, item, count)` runs on a bot's map thread: under the store lock it only checks the toggle, the parent row, and an already-running row for the same item in the branch, then records the request in a per-(parent, item) queue slot. The world-thread `Update` drains due requests: it still holds the parent row, enforces the depth limit (`AiPlayerbot.GuildOrderSubOrderDepth`, top-level order counts 1) and the loop guard (same item never twice in one branch, both in `GuildOrderSubOrderPolicy.h`), enumerates guild members by one `guild_member` query per due request, keeps durable module bots (pool-account or claimed, never alts or hires) that are online in the order's guild, and ranks them makers-first through the policy pick. Busy candidates re-queue on a 45 s timer; terminal failures (no candidate, depth/loop refused, cant, player cancel) all report through `NoteSubOrderFailed`, which merges the item onto the parent's need list.

Sub-order rows carry `parentBotGuidLow` and `rootGiverGuidLow` (player who gave the root order), persisted in `parent_bot`/`root_giver` columns. Live `TBM:ORDER` lines go to the root giver, with `<parent>` holding the parent bot name, so the addon indents by parent. Each sub-order inherits the parent's gold cap value, and every withdrawal on the branch counts against each ancestor's cap up to the root (one shared budget, per-branch-row inherited values).

Cancelling a parent cancels its whole branch (`BranchBots`, parked refund stubs excluded); children keep what they gathered because the bank deposit path stands alone per order. `reassign` moves a sub-order row to a free guild bot with the same recipe-or-farm gate, recomputing craft-vs-farm for the new bot, allowed for the same ranks that may give orders; offline targets are refused.
