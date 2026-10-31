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
  - server-settings-guild-orders
---

# Guild Orders

In your own guild you can send a bot to fetch items for real: it farms, buys or crafts them and brings them to the guild bank. No cheats: the bot gathers, kills, shops and crafts the same way a player would.

## Giving an order

Open the TBM addon (`/tbm`), go to the **Guild** tab, press **Order** next to a bot, search for an item, choose Farm, Buy or Craft and an amount. Without the addon, the same orders work from chat:

```text
.bot order <BotName> farm <item link or id> <amount> [hours|inf] [keep]
.bot order <BotName> buy <item link or id> <amount> [hours|inf] [keep]
.bot order <BotName> craft <item link or id> <amount> [hours|inf] [keep]
.bot order <BotName> cancel
.bot order list
.bot order search <BotName> <farm|buy|craft> [cat=<kind>] <text>
.bot order reassign <SubOrderBot> <NewBot>
```
- **One order per bot.** A new order replaces the old one. `cancel` removes it (and its whole sub-order branch) and the bot goes back to normal life.
- **Sub-orders:** when the bot cannot get a material itself, it asks another bot in the guild to make or farm it. These show indented under the parent in the addon and deliver into the guild bank like normal orders, up to 3 levels deep (see [sub-order settings](../server/settings/guild-orders.md)). Cancelling a parent cancels its whole branch, but whatever the bots already gathered still reaches the guild bank. If a sub-order fails, its item goes on the parent's need list instead of failing the parent. Hand a sub-order to a different bot with `reassign` (same permission as giving orders).
- **Amount:** 1-200. Only items that can go to the bank can be ordered (no bind-on-pickup, quest or temporary items); those never show up in the search.
- **Hours:** optional whole hours 1-24; no hours given (or `inf`: `... 20 inf`, or `... 20 inf keep` with keep) means no time limit: the bot farms until it has the amount, however long it takes. A capped order farms that long in total per round (worked time only, like today's clock); when the time runs out without reaching the amount, the bot still brings what it has to the guild bank (or mails it to the guild master) and the order ends as `cant` with reason `timeout`. Bots farm like players: large amounts or rare drops can take very long or fail, so give reasonable orders.
- **No progress:** every order, capped or not, ends when the bot worked 3 hours without gaining a single order item (a craft order watches its current reagent): the bot still brings what it holds to the guild bank (or mails it to the guild master) and the order ends as `cant` with reason `noprogress`. Any new item resets the 3 hours; a zone move does not.
- **Keep:** add the word `keep` at the end (`... 20 keep`, or `... 20 3 keep` with hours) and the order does not end after delivery: it watches the guild bank and, when the bank holds fewer than the ordered amount, the bot brings the missing amount again. Each new round gets the full hours again. Keep needs a working bank: if a round ends mailed (the bank refused), `cant` or `noskill`, the keep ends with it.
- **Search:** shows at most 30 matches, each with where that bot would farm it, its rough chance there, and a difficulty estimate for one item: easy (about 30 kills or gathers), medium (about 200), hard (more than that). Order rows show the same estimate for the full amount; craft orders use the hardest material still missing, and buy orders show none. Every word of the query must match somewhere in the name in any order, and the best name matches come first. `.bot order search <BotName> <farm|buy|craft> cat=<kind>` (herb, ore, leather, fish, cloth) browses a whole kind with no text; the big kinds (drop, other) and a search without a kind need at least 3 characters of text. Each row shows the item's kind and drop rate, so a rare drop that may take many hours is visible before you order.
- **The search needs the bot online;** giving an order does not. An order on an offline bot is kept and runs when it comes back online. Craft orders are the exception: the bot's recipes can only be checked while it is online, so a craft order on an offline bot is refused (`norecipe`).

## Who may give orders

You must be a real player in the bot's guild, and your guild rank must be allowed to edit officer notes (set in the normal guild rank settings). Anyone else gets an error. Notes are only the permission check: orders themselves are given with the addon or the chat command, and anything written in officer notes is ignored.

## What the bot does

| Order | What the bot does |
| :--- | :--- |
| Farm | Gathers (herbs, ore, skinning, fish) or kills mobs that drop the item, in one planned zone. |
| Buy | Buys with its own gold on the auction house first, collecting bought items from the mailbox (they arrive by mail); whatever is still missing it farms. |
| Craft | Crafts from materials in its bags; missing materials are farmed first. |

- **Fishing orders:** an order on a fish sends the bot to a fishing spot in the planned zone, on its own continent. It fishes there until the amount is reached, counting its pole and lure: the bot is granted the cheapest pole that still reaches the zone's skill and a stack of lures, and reapplies the lure while fishing. When even the best pole and lure cannot reach the zone, the search answers `lowskill` (or names a lower zone with the same fish).

## Delivery

When the amount is reached, the bot walks to a guild banker in a capital city and deposits into the guild bank. For that the guild needs an **unlocked guild bank** with **at least one tab the bots' rank may deposit into**. Otherwise the bot mails the items to the **guild master** instead, right from the city (mail works even when the guild master is offline). The bank may get a few more than ordered: deposits move whole stacks.

## States

`.bot order list` shows every order in your guild with have/want counts and the farm zone, including sub-orders (indented under their parent by the addon); the giver also gets live updates while it runs. A finished order stays visible until you cancel it or give a new one. Keep orders show `keep` after the hours field; other orders show `-` there.

| State | Meaning |
| :--- | :--- |
| `working` | Working on the order, with progress (have / want). |
| `waiting` | The bot is in a player's group; the order resumes when it leaves. |
| `delivering` | Amount reached (or bags full), on the way to the guild banker. |
| `done` | Deposited in the guild bank. |
| `stocked` | Keep order: delivered, the bank holds the ordered amount, watching it (checks about every 10 minutes). |
| `mailed` | The bank refused, mailed to the guild master. |
| `noskill` | The item needs a profession the bot lacks, or its skill is too low. |
| `cant` | No source in level range, no source at all, only elite sources, unknown recipe, farm time ran out (`timeout`), no new item in 3 worked hours (`noprogress`), or stuck while crafting or delivering. |

## Requirements

- The guild needs an unlocked guild bank and a tab the bots' rank may deposit into (otherwise items come by mail to the guild master). Bots usually have the lowest rank, so open a tab to that rank if you want bank deposits.
- Guild orders only work while `AiPlayerbot.AutoDoQuests = 1` (the default). With quest travel off, `.bot order` answers `disabled`.
