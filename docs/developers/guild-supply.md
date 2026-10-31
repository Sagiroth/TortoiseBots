---
id: developers-guild-supply
title: Guild supply chain
category: developers
summary: Sub-orders between guild bots (shared file; this section covers the sub-order store).
tags: [developer, guild-orders, guild-bank, supply]
relates_to:
  - play-guild-orders
  - developers-architecture
---

# Guild Supply Chain

## Sub-orders

`RequestSupply(parentBot, item, count)` runs on a bot's map thread: under the store lock it only checks the toggle, the parent row, and an already-running row for the same item in the branch, then records the request in a per-parent queue slot. The world-thread `Update` drains the queue: it still holds the parent row, enforces the depth limit (`AiPlayerbot.GuildOrderSubOrderDepth`, top-level order counts 1) and the loop guard (same item never twice in one branch, both in `GuildOrderSubOrderPolicy.h`), enumerates guild members by one `guild_member` query, keeps module-owned bots that are online in the order's guild, and ranks them makers-first through the policy pick. Busy candidates keep the request queued for the next pass; no candidate returns false so the ladder falls back to the need list.

Sub-order rows carry `parentBotGuidLow` and `rootGiverGuidLow` (player who gave the root order), persisted in `parent_bot`/`root_giver` columns. Live `TBM:ORDER` lines go to the root giver, with `<parent>` holding the parent bot name, so the addon indents by parent. The parent's gold cap is inherited through `rootGiverGuidLow` at order resolution time only insofar as the root order owns the cap row; sub-orders withdraw under their own row, and the lead merges the ladder's cap columns when integrating.

Cancelling a parent cancels its whole branch (`BranchBots`); children keep what they gathered because the bank deposit path stands alone per order. A sub-order ending in `cant` (or cancelled) routes through `NoteSubOrderFailed`, which returns the parent to `working` so the ladder re-plans and puts the item on the parent's need list. `reassign` moves a sub-order row to a free guild bot with the same recipe-or-farm gate, allowed for the same ranks that may give orders.
