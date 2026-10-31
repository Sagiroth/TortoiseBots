---
id: server-settings-guild-orders
title: Guild order settings
category: server
summary: Configuration switches for guild bank supplies, guild bank gold and sub-orders between guild bots.
tags: [config, guild, orders, guild-bank, gold, sub-orders]
relates_to:
  - play-guild-orders
  - server-settings-economy
---

# Guild Order Settings

These settings control how orders use the guild bank and sub-orders between guild bots. They go in `aiplayerbot.conf`; see [How configuration works](../configuration.md). Guild bank rank and tab permissions still apply.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.GuildOrderBankWithdraw`](#aiplayerbotguildorderbankwithdraw) | `1` | Lets orders withdraw only the missing item or reagent count from the guild bank before shopping or farming. |
| [`AiPlayerbot.GuildOrderBankGold`](#aiplayerbotguildorderbankgold) | `1` | Lets orders withdraw the shortfall from guild bank money after using the bot's own gold. |
| [`AiPlayerbot.GuildOrderSubOrders`](#aiplayerbotguildordersuborders) | `1` | Lets orders ask another guild bot to make or farm a missing item. |
| [`AiPlayerbot.GuildOrderSubOrderDepth`](#aiplayerbotguildordersuborderdepth) | `3` | How deep sub-orders may nest. |

## Settings

### `AiPlayerbot.GuildOrderBankWithdraw`

Allows guild orders to take only the missing item or crafting material count from accessible guild bank tabs.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

When off, orders skip the guild bank item step and continue to the auction house or the bot's own gathering and crafting. The core guild bank still checks rank access and records each withdrawal.

### `AiPlayerbot.GuildOrderBankGold`

Allows guild orders to use guild bank money to cover an auction house purchase after the bot's own gold.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

An order's optional `gold=<copper>` cap limits its total guild bank withdrawals. If omitted, there is no order cap. The core guild bank checks money access and records each withdrawal.

### `AiPlayerbot.GuildOrderSubOrders`

Allows working orders to place sub-orders on other guild bots when the ordering bot can neither make nor farm a missing item.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

When off, the supply step reports no candidate and the item lands on the parent's need list instead.

### `AiPlayerbot.GuildOrderSubOrderDepth`

How many levels a sub-order branch may hold, counting the top-level order as 1.
**Default:** `3` · **Values:** `1`-`16` (out of range falls back to `3`) · **Restart:** needed

The same item never appears twice in one branch, whatever the depth.
