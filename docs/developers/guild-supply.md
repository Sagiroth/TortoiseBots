---
id: developers-guild-supply
title: Guild supply chain
category: developers
summary: Supply ladder, persisted order progress, bank withdrawals and needitems recovery.
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

The `TBM:ORDER` line keeps its existing fields and appends parent, gold cap, step and need. This worker reports `-` for the parent; the sub-order store fills it when integrated. Step and need changes go through the normal world-thread update so list replies and live lines use one formatter.

## Refunds and retry rules

Unspent guild gold (`guildGoldHeld`) is repaid with `DepositMoney` through the same banker path. Finished and cancelled rows park (`cancelled`) instead of disappearing while gold is owed; the refund drain pays from any banker visit and the store clears the row once the gold is back. A new order for the same bot inherits the debt.

Bank table reads are hints: the core commits saves through the async delay queue, so a re-read right after a withdraw or deposit still shows the old value. The item drain reads `guild_bank` once per banker visit, iterates its rows, and treats only the bot's bag-count delta as truth; a failed row skips to the next instead of aborting the visit. The money drain never re-reads: a `WithdrawMoney` the core refuses leaves the bot's money unchanged, which the drain reports as `BankDenied`. An empty bank visit marks the item skipped for ten minutes, and a gold top-up is tried once per auction listing.
