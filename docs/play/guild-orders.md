---
id: play-guild-orders
title: Guild Orders
category: play
summary: How to send guild bots to fetch, buy or craft items, use guild supplies, and follow order progress.
tags: [guild, orders, farm, craft, auction-house, guild-bank]
relates_to:
  - play-commands-roster-and-hiring
  - play-commands-addon
  - play-getting-started
  - server-settings-guild-orders
---

# Guild Orders

In your own guild you can send a bot to fetch items for real: it farms, buys or crafts them and brings them to the guild bank. No cheats: the bot gathers, kills, shops and crafts the same way a player would. Give the order from the addon or from chat, then follow it in `.bot order list`.

## Quick start

Before you start, you need three things: an unlocked guild bank with at least one tab open to the bots' rank (see [Guild bank access](#guild-bank-access) below), your own guild rank allowed to edit officer notes, and the bot in your guild. An order on an offline bot is kept and runs when it comes back online.

Example: 20 Copper Ore from a bot:

1. Open the addon (`/tbm`), go to the **Guild** tab, press **Order** next to the bot. Search for "copper ore", pick the Farm order and set the amount to 20. From chat, the same order is `.bot order <BotName> farm <item link or id> 20`.
2. The bot checks its own bags and mailbox, looks at the auction house, then walks to a nearby zone it can reach and farms ore until it has 20.
3. When the 20 are together, it walks to a guild banker and deposits them into the guild bank. If the bank refuses the deposit, it mails them to the guild master instead.
4. `.bot order list` shows the progress as `12/20` while it runs, and you get live updates as it changes. A finished order shows `done` (or `mailed`).

## Order types

| Order | What the bot does |
| :--- | :--- |
| Farm | Gathers (herbs, ore, skinning, fish) or kills mobs that drop the item, in the nearest planned zone on its own continent that it can walk to. A zone with no reachable spots is left alone for a while while the bot farms the next one. |
| Buy | Checks its bags and mailbox, buys what it can on the auction house (its own gold first, guild bank gold within your cap), then farms whatever is still missing. Items bought at auction arrive by mail. |
| Craft | Gets each missing material (see below), crafts what it can itself, and asks other guild bots to make or farm the rest. Bots always have their recipe tools (Arclight Spanner, Blacksmith Hammer and the like): a missing tool appears in the bags when the bot crafts, and bots never sell or auction tools. |

An order on a fish sends the bot to a fishing spot in the planned zone. The bot is granted a pole good enough for the zone plus lures, and re-applies the lure while fishing. If even the best pole and lure cannot reach the zone, the search answers `lowskill`.

## Where the bot gets items

For the ordered item itself (a farm or buy order), the bot checks its own bags and mailbox, then the auction house, then farms it. It never takes the finished item from the guild bank: the bank holds crafting materials.

For each missing crafting material, the bot works down a fixed list: its own bags and mailbox, then the guild bank (only the count still missing), then the auction house, then getting it itself (gathering, farming, crafting, or buying it from a shop), then asking another guild bot, and finally your need list (see below).

Some materials every shop sells without limit (for example Empty Vial). For those the bot skips the guild bank and the auction house and walks to a shop instead, paying with its own gold. It never asks another bot for those. If it cannot buy them, they land on your need list.

<details>
<summary>How it works in detail</summary>

For the auction house step the bot picks one auction house and walks there. A counter it reaches is searched once: if nothing listed fits what the order still needs or may spend, it moves on at once with reason `nolisting`. On one visit it buys as many listings as it needs, then collects them from the mailbox once. Anything bought beyond the need goes back onto the auction house, and the sale gold or the returned stack is paid to the guild bank. A bank withdrawal only takes the count the order still needs; if the rank rules refuse it, the bot moves on and keeps that reason in the status. How the auction step retries and hands off is described in [Guild Supply Chain](../developers/guild-supply.md#auction-leg).

</details>

## Options

- **Amount:** 1-200. Only items that can go to the bank can be ordered (no bind-on-pickup, quest or temporary items); those never show up in the search.
- **Hours:** optional whole hours 1-24; no hours given (or `inf`) means no time limit: the bot farms until it has the amount, however long it takes. When the time runs out, the bot still brings what it has to the guild bank (or mails it to the guild master) and the order ends as `cant` with reason `timeout`.
- **Keep:** add the word `keep` at the end and the order does not end after delivery: it watches the guild bank and, when the bank holds fewer than the ordered amount, the bot brings the missing amount again. Each new round gets the full hours again. Keep needs a working bank: if a round ends mailed, `cant` or `noskill`, the keep ends with it.
- **Guild gold cap:** add `gold=<copper>` after the other options to limit how much guild bank money this order may use on auction house purchases. The bot spends its own gold first. Omit it for no cap; `gold=0` prevents guild gold use.
- **Search:** each order type lists only what fits it: Farm shows items a bot can gather (herbs, ore, skins, fish) and materials mobs drop; Craft shows items a profession recipe makes; Buy shows materials and crafted items. Recipes, conjured items and random-drop gear never show. It shows at most 30 matches, each with where that bot would farm it and a difficulty estimate for one item: easy (about 30 kills or gathers), medium (about 200), hard (more than that). Every word of the query must match somewhere in the name in any order. `.bot order search <BotName> <farm|buy|craft> cat=<kind>` (herb, ore, leather, fish, cloth) browses a whole kind with no text; other kinds need at least 3 characters of text. The search needs the bot online; giving the order does not. Craft orders are the exception: the bot's recipes can only be checked while it is online, so a craft order on an offline bot is refused (`norecipe`).
- **One order per bot.** A new order replaces the old one. `cancel` removes it (and its whole sub-order branch) and the bot goes back to normal life.

<details>
<summary>How it works in detail</summary>

Hours count worked time only, like the zone clock below. Every order, capped or not, also ends when the bot worked 3 hours without gaining a single order item (a craft order watches its current reagent): the bot still brings what it holds and the order ends as `cant` with reason `noprogress`. Any new item resets the 3 hours; a zone move does not, and waiting on a running sub-order does not count either. A craft reagent farm with no gain hands down to a sub-order or the need list after 20 minutes. The bot moves to the next planned zone after an hour worked in one zone without a new item, or after dying three times in the zone; a farm leg whose searches land nowhere across 8 zones moves on instead of cycling its whole plan.

</details>

## Sub-orders

When the bot cannot get a material itself, it asks another guild bot to make or farm it. A craft order missing several materials asks for several at once, each on its own sub-order row, and collects each delivery from the guild bank as it arrives. These show indented under the parent in the addon and deliver into the guild bank like normal orders, up to 3 levels deep. The status line shows what the parent itself works on while sub-orders show their own steps. Cancelling a parent cancels its whole branch, but whatever the bots already gathered still reaches the guild bank. If a sub-order fails, its item goes on the parent's need list: a craft order keeps working on the rest instead of failing, while a farm or buy order waits for the item. Hand a sub-order to a different bot with `reassign` (same permission as giving orders); only bots without an order of their own qualify.

## Need list

If the bot and other guild bots cannot get one of the materials, that item goes on the need list right away and the bot tells the giver what it still needs. The order keeps working meanwhile: other materials, intermediates it can craft, and sub-orders it already asked for all carry on. Only when nothing but the listed items is missing does the order wait with `needitems`. The status shows the item counts to bring. Trade the items to the bot, mail them, or put them in the guild bank. A guild bot takes items mailed by members of its own guild straight into its bags (a recipe in the bags gets learned) and returns items mailed by anyone else. The order resumes when enough appears in the bot's bags or mailbox, or in an accessible guild bank; every few minutes it also re-checks whether it can get the item from a shop or the auction house instead, and resumes on its own when it can. This wait does not count toward the order's time or no-progress limit.

## Delivery

When the amount is reached, the bot walks to a guild banker in a capital city and deposits into the guild bank. For that the guild needs an **unlocked guild bank** with **at least one tab the bots' rank may deposit into**. Otherwise the bot mails the items to the **guild master** instead, right from the city (mail works even when the guild master is offline). The bank may get a few more than ordered: deposits move whole stacks. Guild gold the bot withdrew but did not spend comes back to the guild bank: when the order ends (or is cancelled), the bot deposits the unspent gold at its next guild banker visit. A cancelled order disappears from the list at once; the repayment still happens behind the scenes. The addon status shows the current step (`bags`, `bank`, `ah`, `farm`, `craft`, `sub`, `need` or `deliver`), the optional guild gold cap, and any missing item list. Extra materials left after crafting are sent to the guild bank with the finished item, but only what the order itself gained; the bot's own materials stay in its bags.

## Guild bank access

Every bot in a guild with at least one real player in it holds the guild's **bot rank**: by default the highest rank below the guild master, so all bots share one rank you can open bank tabs to. A bot that joins the guild gets the bot rank at once; bots already in the guild are moved there once when the server starts. Guilds made up of only bots keep their own ranks (including a bot guild master, which is never moved). Your own characters are never touched: only bot characters get the bot rank, and bots in such guilds no longer promote, demote, kick or invite anyone.

```text
.bot order rank <rankIndex>
.bot order access
```

- **Change the bots' rank:** `.bot order rank 2` moves every bot to rank 2. Only a player whose own rank may promote and demote members may do this; anyone else gets an error.
- **See what the bots can reach:** `.bot order access` (and the top of `.bot order list`) shows the bot rank, which bank tabs it reaches and which it does not, and whether it may take guild gold (guilds without an unlocked bank show no tabs and no gold). The header shows red when anything is missing — a tab the bots cannot use, or no gold access: open the tabs to the bot rank (or pick a higher rank) before giving orders that need the bank.
- **Turn it off:** setting `AiPlayerbot.GuildOrderBotRank = 0` leaves every rank alone; `.bot order access` then answers that the server does not manage bot ranks.

## Who may give orders

You must be a real player in the bot's guild, and your guild rank must be allowed to edit officer notes (set in the normal guild rank settings). Anyone else gets an error. Notes are only the permission check: orders themselves are given with the addon or the chat command, and anything written in officer notes is ignored.

## Commands

```text
.bot order <BotName> farm <item link or id> <amount> [hours|inf] [keep] [gold=<copper>]
.bot order <BotName> buy <item link or id> <amount> [hours|inf] [keep] [gold=<copper>]
.bot order <BotName> craft <item link or id> <amount> [hours|inf] [keep] [gold=<copper>]
.bot order <BotName> cancel
.bot order <BotName> steps
.bot order list
.bot order search <BotName> <farm|buy|craft> [cat=<kind>] <text>
.bot order reassign <SubOrderBot> <NewBot>
```

`.bot order <BotName> steps` lists the order's last 16 steps with their age (the same steps the status shows), read-only and memory only, so it starts empty after a restart, a new order or a reassign.

## States

`.bot order list` shows every order in your guild with delivered/want counts and the farm zone, including sub-orders (indented under their parent by the addon); the giver also gets live updates while it runs. The count is what reached the guild bank (or the guild master by mail), not what the bot is still carrying: the addon can show the bags-and-mail part separately (live for an online bot while the row runs, and the stored figure once the row is over: `0` once the hand-off succeeded, `done` after the bank deposit or `mailed`, and a `cant` row keeps the last scanned figure because the stack is still in the bags). A finished order stays visible until you cancel it or give a new one. Keep orders show `keep` after the hours field.

| State | Meaning |
| :--- | :--- |
| `working` | Working on the order, with progress (delivered / want). It may already carry a missing-material list the bot is working around. |
| `waiting` | The bot is in a player's group; the order resumes when it leaves. |
| `waiting` with reason `needitems` | Nothing else is left to source: the bot needs the listed materials from the giver. It resumes when they appear in its bags, mailbox or accessible guild bank. |
| `delivering` | Amount reached (or bags full), on the way to the guild banker. |
| `done` | Deposited in the guild bank. |
| `stocked` | Keep order: delivered, the bank holds the ordered amount, watching it (checks about every 10 minutes). |
| `mailed` | The bank refused, mailed to the guild master. |
| `noskill` | The item needs a profession the bot lacks, or its skill is too low. |
| `cant` | No source in level range, no source at all, only elite sources, unknown recipe, farm time ran out (`timeout`), no new item in 3 worked hours (`noprogress`), or stuck while crafting or delivering. |

## Requirements

- The guild needs an unlocked guild bank and a tab the bots' rank may access. Deposits or withdrawals refused by the rank rules are skipped; finished items that cannot be deposited are mailed to the guild master. Bots in a guild with a real player hold the guild's bot rank (by default the rank below the guild master), so open a tab to that rank if you want bank access; all-bot guilds keep their own ranks.
- Guild orders only work while `AiPlayerbot.AutoDoQuests = 1` (the default). With quest travel off, `.bot order` answers `disabled`.
- Guild bank item withdrawals and guild bank gold use are controlled by [`AiPlayerbot.GuildOrderBankWithdraw`](../server/settings/guild-orders.md#aiplayerbotguildorderbankwithdraw) and [`AiPlayerbot.GuildOrderBankGold`](../server/settings/guild-orders.md#aiplayerbotguildorderbankgold); both default to `1`.

## Known limitations

- A bot that cannot reach the guild banker shops or asks other bots for items that are already in the guild bank, instead of waiting or walking there.
- A bot standing somewhere with no route to a counter, banker or farm spot gives up that step at once; move it somewhere it can travel from so it can continue.
- A shop trip can still take the bot on a long, dangerous walk; if it cannot reach any shop that sells the item, the item lands on your need list.
- A `bankdenied` reason can still appear even when the tab and gold access look right.
