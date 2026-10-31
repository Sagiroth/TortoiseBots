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

In your own guild you can send a bot to fetch items for real: it farms, buys or crafts them and brings them to the guild bank. No cheats: the bot gathers, kills, shops and crafts the same way a player would.

## Giving an order

Open the TBM addon (`/tbm`), go to the **Guild** tab, press **Order** next to a bot, search for an item, choose Farm, Buy or Craft and an amount. Without the addon, the same orders work from chat:

```text
.bot order <BotName> farm <item link or id> <amount> [hours|inf] [keep] [gold=<copper>]
.bot order <BotName> buy <item link or id> <amount> [hours|inf] [keep] [gold=<copper>]
.bot order <BotName> craft <item link or id> <amount> [hours|inf] [keep] [gold=<copper>]
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
- **Guild gold cap:** add `gold=<copper>` after the other options to limit how much guild bank money this order may use on auction house purchases. The bot spends its own gold first. Omit it for no cap; `gold=0` prevents guild gold use.
- **Search:** shows at most 30 matches, each with where that bot would farm it, its rough chance there, and a difficulty estimate for one item: easy (about 30 kills or gathers), medium (about 200), hard (more than that). Order rows show the same estimate for the full amount; craft orders use the hardest material still missing, and buy orders show none. Every word of the query must match somewhere in the name in any order, and the best name matches come first. `.bot order search <BotName> <farm|buy|craft> cat=<kind>` (herb, ore, leather, fish, cloth) browses a whole kind with no text; the big kinds (drop, other) and a search without a kind need at least 3 characters of text. Each row shows the item's kind and drop rate, so a rare drop that may take many hours is visible before you order.
- **The search needs the bot online;** giving an order does not. An order on an offline bot is kept and runs when it comes back online. Craft orders are the exception: the bot's recipes can only be checked while it is online, so a craft order on an offline bot is refused (`norecipe`).

## Who may give orders

You must be a real player in the bot's guild, and your guild rank must be allowed to edit officer notes (set in the normal guild rank settings). Anyone else gets an error. Notes are only the permission check: orders themselves are given with the addon or the chat command, and anything written in officer notes is ignored.

## Guild bank access

Every bot in a guild with at least one real player in it holds the guild's **bot rank**: by default the highest rank below the guild master, so all bots share one rank you can open bank tabs to. A bot that joins the guild gets the bot rank at once; bots already in the guild are moved there once when the server starts. Guilds made up only of bots keep their own ranks (including a bot guild master, which is never moved). Your own characters are never touched: only bot characters get the bot rank, and bots in such guilds no longer promote, demote, kick or invite anyone.

```text
.bot order rank <rankIndex>
.bot order access
```

- **Change the bots' rank:** `.bot order rank 2` moves every bot to rank 2. Only a player whose own rank may promote and demote members may do this; anyone else gets an error.
- **See what the bots can reach:** `.bot order access` (and the top of `.bot order list`) shows the bot rank, which bank tabs it reaches and which it does not, and whether it may take guild gold (guilds without an unlocked bank show no tabs and no gold). The header shows red when anything is missing — a tab the bots cannot use, or no gold access: open the tabs to the bot rank (or pick a higher rank) before giving orders that need the bank.
- **Turn it off:** setting `AiPlayerbot.GuildOrderBotRank = 0` leaves every rank alone; `.bot order access` then answers that the server does not manage bot ranks.

## What the bot does

| Order | What the bot does |
| :--- | :--- |
| Farm | Gathers (herbs, ore, skinning, fish) or kills mobs that drop the item, in one planned zone. |
| Buy | Checks its bags and mailbox, takes what it can from the guild bank, then buys with its own gold first on the auction house. Guild bank money covers the shortfall within the order's cap. Items bought at auction arrive by mail. Anything still missing is farmed. |
| Craft | Checks bags and mailbox for each missing material, then the guild bank and auction house. It farms, gathers or crafts intermediate materials when it can, then asks a guild bot with a matching profession for a sub-order. |

For every missing item, the supply ladder is bags and mailbox, guild bank, auction house, then the bot's own gathering, farming or crafting. A bank withdrawal only takes the remaining amount the order needs. The core applies the bot's guild rank and bank tab permissions; if it refuses a withdrawal, the bot skips the bank step and keeps that reason in the order status. Farm and buy orders never withdraw their own finished item from the bank; the bank step serves craft reagents. An empty bank step is not retried until the bank changes or ten minutes pass.

If the bot and other guild bots cannot get an item, the order waits with `needitems`. The status shows the item IDs and counts to bring, and the bot whispers the giver once for that list. Trade the items to the bot, mail them, or put them in the guild bank. The order resumes when enough appears in the bot's bags or mailbox, or in an accessible guild bank. This wait does not count toward the order's time or no-progress limit.

Guild gold the bot withdrew but did not spend comes back to the guild bank: when the order ends (or is cancelled), the bot deposits the unspent gold at its next guild banker visit. A cancelled order disappears from the list at once; the repayment still happens behind the scenes.

The addon status shows the current step (`bags`, `bank`, `ah`, `farm`, `craft`, `sub`, `need` or `deliver`), the optional guild gold cap, and any missing item list. Extra materials left after crafting are sent to the guild bank with the finished item, but only what the order itself gained; the bot's own materials stay in its bags.

- **Fishing orders:** an order on a fish sends the bot to a fishing spot in the planned zone, on its own continent. It fishes there until the amount is reached, counting its pole and lure: the bot is granted the cheapest pole that still reaches the zone's skill and a stack of lures, and reapplies the lure while fishing. When even the best pole and lure cannot reach the zone, the search answers `lowskill` (or names a lower zone with the same fish).

## Delivery

When the amount is reached, the bot walks to a guild banker in a capital city and deposits into the guild bank. For that the guild needs an **unlocked guild bank** with **at least one tab the bots' rank may deposit into**. Otherwise the bot mails the items to the **guild master** instead, right from the city (mail works even when the guild master is offline). The bank may get a few more than ordered: deposits move whole stacks.

## States

`.bot order list` shows every order in your guild with have/want counts and the farm zone, including sub-orders (indented under their parent by the addon); the giver also gets live updates while it runs. A finished order stays visible until you cancel it or give a new one. Keep orders show `keep` after the hours field; other orders show `-` there.

| State | Meaning |
| :--- | :--- |
| `working` | Working on the order, with progress (have / want). |
| `waiting` | The bot is in a player's group; the order resumes when it leaves. |
| `waiting` with reason `needitems` | The bot needs the listed materials from the giver. It resumes when they appear in its bags, mailbox or accessible guild bank. |
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
