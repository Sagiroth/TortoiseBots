---
id: play-guild-orders
title: Guild Orders
category: play
summary: How to send guild bots to farm, buy or craft items for the guild bank, and what each order state means.
tags: [guild, orders, farm, craft, auction-house, guild-bank]
relates_to:
  - play-commands-roster-and-hiring
  - play-commands-addon
  - play-getting-started
---

# Guild Orders

In your own guild you can send a bot to fetch items for real: it farms, buys or crafts them and brings them to the guild bank. No cheats: the bot gathers, kills, shops and crafts the same way a player would.

## Giving an order

Open the TBM addon (`/tbm`), go to the **Guild** tab, press **Order** next to a bot, search for an item, choose Farm, Buy or Craft and an amount. Without the addon, the same orders work from chat:

```text
.bot order <BotName> farm <shift-clicked item link> <amount>
.bot order <BotName> buy <item id> <amount>
.bot order <BotName> craft <item id> <amount>
.bot order <BotName> cancel
.bot order list
.bot order search <BotName> farm <text>
```

- **One order per bot.** A new order replaces the old one. `cancel` removes it and the bot goes back to normal life.
- **Amount:** 1-200. Only items that can go to the bank can be ordered (no bind-on-pickup, quest or temporary items); those never show up in the search.
- **Search:** needs at least 3 characters and shows at most 30 matches, each with where that bot would farm it and its rough chance there.
- **The search needs the bot online;** giving an order does not. An order on an offline bot is kept and runs when it comes back online.

## Who may give orders

You must be a real player in the bot's guild, and your guild rank must be allowed to edit officer notes (set in the normal guild rank settings). Anyone else gets an error. Notes are only the permission check: orders themselves are given with the addon or the chat command, and anything written in officer notes is ignored.

## What the bot does

| Order | What the bot does |
| :--- | :--- |
| Farm | Gathers (herbs, ore, skinning) or kills mobs that drop the item, in one planned zone. |
| Buy | Buys with its own gold on the auction house first; whatever is still missing it farms. |
| Craft | Crafts from materials in its bags; missing materials are farmed first. |

- **An order beats normal life:** a bot with an order drops questing and grinding to work on it. Exception: in a player's group the order waits until the bot leaves the group.
- **The bot keeps ordered items:** it does not sell, auction or use them (or the materials of a craft order) while the order runs.
- **Level safety:** only sources (mobs, nodes) up to the bot's level + 3 count. Herb and ore orders need level 10. If there is nothing usable, the order ends at once.
- **Giving up:** if an hour of real work brings no new item, the bot moves once to the next best zone; another empty hour there, or 5 deaths during the order, ends it. Only time the bot actually works counts.
- **Full bags:** when the bags fill up, the bot delivers what it has and goes back to finish. Progress never drops.
- **Crafting:** the bot needs to know the recipe (hand it a recipe item and it learns it) and have real materials; vendor-only materials it buys itself. A recipe it cannot learn or materials it cannot get end the order.

## Delivery

When the amount is reached, the bot walks to a guild banker in a capital city and deposits into the guild bank. For that the guild needs an **unlocked guild bank** with **at least one tab the bots' rank may deposit into**. Otherwise the bot mails the items to the **guild master** instead, right from the city (mail works even when the guild master is offline). The bank may get a few more than ordered: deposits move whole stacks.

## States

`.bot order list` shows every order in your guild with have/want counts and the farm zone; the giver also gets live updates while it runs. A finished order stays visible until you cancel it or give a new one.

| State | Meaning |
| :--- | :--- |
| `working` | Working on the order, with progress (have / want). |
| `waiting` | The bot is in a player's group; the order resumes when it leaves. |
| `delivering` | Amount reached (or bags full), on the way to the guild banker. |
| `done` | Deposited in the guild bank. |
| `mailed` | The bank refused, mailed to the guild master. |
| `noskill` | The item needs a profession the bot lacks, or its skill is too low. |
| `cant` | No source in level range, no source at all, unknown recipe, or stuck after moving zone once. |

## Requirements

- The guild needs an unlocked guild bank and a tab the bots' rank may deposit into (otherwise items come by mail to the guild master). Bots usually have the lowest rank, so open a tab to that rank if you want bank deposits.
- Guild orders only work while `AiPlayerbot.AutoDoQuests = 1` (the default). With quest travel off, `.bot order` answers `disabled`.
