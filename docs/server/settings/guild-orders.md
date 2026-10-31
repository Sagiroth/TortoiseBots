---
id: server-settings-guild-orders
title: Guild order settings
category: server
summary: Configuration switches for sub-orders between guild bots.
tags: [config, guild, orders, sub-orders]
relates_to:
  - play-guild-orders
  - server-settings-economy
---

# Guild Order Settings

These settings control sub-orders: when a bot cannot get a material itself, it asks another bot in the guild to make or farm it. They go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.GuildOrderSubOrders`](#aiplayerbotguildordersuborders) | `1` | Lets orders ask another guild bot to make or farm a missing item. |
| [`AiPlayerbot.GuildOrderSubOrderDepth`](#aiplayerbotguildordersuborderdepth) | `3` | How deep sub-orders may nest. |

## Settings

### `AiPlayerbot.GuildOrderSubOrders`

Allows working orders to place sub-orders on other guild bots when the ordering bot can neither make nor farm a missing item.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

When off, the supply step reports no candidate and the item lands on the parent's need list instead.

### `AiPlayerbot.GuildOrderSubOrderDepth`

How many levels a sub-order branch may hold, counting the top-level order as 1.
**Default:** `3` · **Values:** `1`-`16` (out of range falls back to `3`) · **Restart:** needed

The same item never appears twice in one branch, whatever the depth.
