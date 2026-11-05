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

Before you start, you need three things:
- A bot of yours in the guild: your own alts are already eligible. To add a pool bot, invite it like any player (`/ginvite <name>`); it accepts when it may (see [Guild bots](getting-started.md#guild-bots)). Hired companions always refuse.
- Your guild rank allowed to edit officer notes.
- Bank delivery (optional): an unlocked guild bank with a tab open to the bots' rank (see [Guild bank access](#guild-bank-access)). Without it, finished items go by mail.
- Bots found guilds of their own only when the server switches it on ([`AiPlayerbot.RandomBotFormGuild`](../server/settings/world-population.md#aiplayerbotrandombotformguild), off by default); invite them into yours.
- An order on an offline bot is kept and runs when it comes back online, except a craft order: its recipes cannot be checked while it is offline, so those are refused with `norecipe`.

Example: 20 Copper Ore from a bot:

1. Open the addon (`/tbm`), go to the **Guild** tab, press **Order** next to the bot. Search for "copper ore", pick the Farm order and set the amount to 20. From chat, the same order is `.bot order <BotName> farm <item link or id> 20`.
2. Before you press Send, the window shows a preview of what the bot will do: what it already has, what it will farm and where, what it may buy, what it asks other guild bots for, what you must send, a rough time and the gold cap. From chat, the same preview is `.bot order <BotName> preview farm <item link or id> 20`.
3. The bot checks its own bags and mailbox, looks at the auction house, then walks to a nearby zone it can reach and farms ore until it has 20.
4. When the 20 are together, it walks to a guild banker and deposits them into the guild bank. If the bank refuses the deposit, it mails them to the guild master instead.
5. `.bot order list` shows the progress as `12/20` while it runs, and you get live updates as it changes. A finished order shows `done` (or `mailed`).

## Order types

| Order | What the bot does |
| :--- | :--- |
| Farm | Gathers (herbs, ore, skinning, fish) or kills mobs that drop the item. Works the nearest planned zone on its own continent that it can walk to. A zone with no reachable spots is left alone for a while. |
| Buy | Checks its bags and mailbox, then buys on the auction house (own gold first, guild gold within your cap). Farms whatever is still missing. Bought items arrive by mail. Needs [`AiPlayerbot.GuildOrderPlayerBuy`](../server/settings/guild-orders.md#aiplayerbotguildorderplayerbuy) = 1 (off by default): a player's Buy order is refused without it. |
| Craft | Gets each missing material (see below), crafts what it can itself, and asks other guild bots to make or farm the rest. |

Recipe tools stay available as long as [`AiPlayerbot.CraftToolsAlways`](../server/settings/guild-orders.md#aiplayerbotcrafttoolsalways) is on (the default). A missing tool (Arclight Spanner, Blacksmith Hammer and the like) appears in the bags when the bot crafts. Bots never sell or auction tools.

## States

`.bot order list` shows every order in your guild with delivered/want counts and the farm zone, including sub-orders (indented under their parent by the addon). The giver also gets live updates while it runs.
- The count is what reached the guild bank (or the guild master by mail), not what the bot is still carrying.
- The addon can show the bags-and-mail part separately: live for an online bot while the row runs; stored once the row is over.
- Stored figures: `0` once the hand-off succeeded; `done` after the bank deposit or `mailed`; a `cant` row keeps the last scanned figure because the stack is still in the bags.
- A finished order stays visible until you cancel it or give a new one. Keep orders show `keep` after the hours field.

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
| `cant` | No source in level range, no source at all, only elite sources, a craft recipe the bot may not make (`norecipe` unknown, `enchant`, `bound` item, `needsprofession` item, or `cooldown` recipe), farm time ran out (`timeout`), no new item in 3 worked hours (`noprogress`), or stuck while crafting or delivering. |

## Requirements

- Bank delivery needs an unlocked guild bank and a tab the bots' rank may access. Deposits or withdrawals refused by the rank rules are skipped; finished items that cannot be deposited are mailed to the guild master. Bots in a guild with a real player hold the guild's bot rank (by default the rank below the guild master), so open a tab to that rank if you want bank access; all-bot guilds keep their own ranks.
- Guild orders only work while `AiPlayerbot.AutoDoQuests = 1` (the default). With quest travel off, `.bot order` answers `disabled`.
- Guild bank item withdrawals and guild bank gold use are controlled by [`AiPlayerbot.GuildOrderBankWithdraw`](../server/settings/guild-orders.md#aiplayerbotguildorderbankwithdraw) and [`AiPlayerbot.GuildOrderBankGold`](../server/settings/guild-orders.md#aiplayerbotguildorderbankgold); both default to `1`.

An order on a fish sends the bot to a fishing spot in the planned zone. The bot is granted a pole good enough for the zone plus lures, and re-applies the lure while fishing. If even the best pole and lure cannot reach the zone, the search answers `lowskill`.

## Where the bot gets items

For the ordered item itself (a farm or buy order), the bot checks its own bags and mailbox, then the auction house, then farms it. It never takes the finished item from the guild bank: the bank holds crafting materials.

For each missing crafting material, the bot works down a fixed list: its own bags and mailbox, then the guild bank (only the count still missing), then the auction house, then getting it itself (gathering, farming, crafting, or buying it from a shop), then asking another guild bot, and finally your need list (see below). Getting it itself means a craft step before a farm step.

Some materials every shop sells without limit (Empty Vial is one: the exact list comes from the vendor data). For those the bot skips the guild bank and the auction house and walks to a shop instead, paying with its own gold. It never asks another bot for those. If it cannot buy them, they land on your need list.

<details>
<summary>How it works in detail</summary>

For the auction house step the bot picks one auction house and walks there.
- One visit buys as many listings as it needs, then collects them from the mailbox once.
- A counter with nothing fitting the need or budget is left at once with reason `nolisting`.
- Overshoot goes back onto the auction house when that works. A relist that cannot go up (no free slot or deposit) goes to the guild bank with the rest instead. Sale gold or a returned stack is paid to the guild bank.
- A bank withdrawal takes only the count still needed. A rank refusal keeps that reason in the status. How the auction step retries and hands off is described in [Guild Supply Chain](../developers/guild-supply.md#auction-leg).

</details>

## Options

- **Craft rules:** a craft order needs a recipe the bot can really make and you can really use. Refused when:
  - The bot does not know the recipe, or it is an enchant (not orderable yet).
  - The item cannot go to the bank (bound when picked up, quest or temporary).
  - The item needs a profession to use (engineering goggles need Engineering).
  - The recipe has a long cooldown (transmutes, Mooncloth).
- The Professions view marks each recipe `ok` or with the reason; the search shows the same verdict.
- **Hours:** optional whole hours 1-24. No hours given (or `inf`) means no time limit. When the time runs out, the bot still brings what it has to the guild bank (or mails it to the guild master). The order ends as `cant` with reason `timeout`.
- **Guild gold cap:** add `gold=<copper>` after the other options. The bot spends its own gold first. Omit it for no cap; `gold=0` prevents guild gold use.
- **Materials I bring:** add `bring=<item>,<item>` after the other options on a craft order (or its preview). Tick the base materials you will deliver yourself: the bot never buys, withdraws, farms or delegates them, and the rest of each ticked material lands on your need list. Unticked materials work as before.
- **Search:** each order type lists only what fits it.
  - Farm: gatherables (herbs, ore, skins, fish) and mob-drop materials. Craft: profession recipes. Buy: materials, crafted items and gatherables.
  - Never shown: recipes, conjured items, random-drop gear. At most 30 matches.
  - Each match shows a farm zone and a difficulty for one item (Buy rows show `-`): easy (about 30 kills or gathers), medium (about 200), hard (more than that).
  - Every query word must match somewhere in the name, in any order.
  - `.bot order search <BotName> <farm|buy|craft> cat=<kind>` (herb, ore, leather, fish, cloth) browses a whole kind with no text; other kinds need at least 3 characters of text.
  - `.bot order search craft <text>` asks the whole guild instead of one bot: each match lists every online guild bot that knows an orderable recipe for it, with its profession skill, how many distinct reagents it already holds in full, and whether it already runs an order. At most 30 matches, same order as the per-bot craft search.
- **One order per bot.** A new order replaces the old one. `cancel` removes it (and its whole sub-order branch) and the bot goes back to normal life.

<details>
<summary>How it works in detail</summary>

Hours count worked time only, like the zone clock below.
- Every order, capped or not, also ends when the bot worked 3 hours without gaining a single order item (a craft order watches its current reagent). It still brings what it holds, ending as `cant` with reason `noprogress`.
- Any new item resets the 3 hours. A zone move does not. Waiting on a running sub-order does not count either.
- A craft reagent farm with no gain hands down to a sub-order or the need list after 20 minutes.
- The bot moves to the next planned zone after an hour worked there without a new item, or after dying three times there.
- A farm leg whose searches land nowhere across 8 zones moves on instead of cycling its whole plan.

</details>

## Sub-orders

When the bot cannot get a material itself, it asks another guild bot to make or farm it.
- A craft order missing several materials asks for several at once, each on its own sub-order row. Each delivery is collected from the guild bank as it arrives.
- Sub-orders show indented under the parent in the addon and deliver into the guild bank like normal orders (up to 3 levels deep). The status line shows what the parent itself works on while sub-orders show their own steps.
- Cancelling a parent cancels its whole branch, but a bot already carrying its share detaches and still delivers it. Whatever the bots already gathered still reaches the guild bank.
- If a sub-order fails, its item goes on the parent's need list: a craft order keeps working on the rest instead of failing, while a farm or buy order waits for the item. Hand a sub-order to a different bot with `reassign` (same permission as giving orders); only bots without a running or watched (`stocked`) order of their own qualify.

## Need list

If the bot and other guild bots cannot get one of the materials, that item goes on the need list right away. The bot tells the giver what it still needs.
- The order keeps working meanwhile: other materials, intermediates it can craft, and asked sub-orders all carry on.
- Only when nothing but the listed items is missing does the order wait with `needitems`. The status shows the item counts to bring.
- Bring items by trade, mail, or the guild bank. A guild bot takes member mail straight into its bags (a recipe in the bags gets learned) and returns mail from anyone else.
- The order resumes when enough appears in its bags or mailbox, or in an accessible guild bank. The bank is re-checked about every 10 seconds. A shop or auction house re-check runs about every 10 minutes (a full ladder pass every third check). This wait counts toward neither the time nor the no-progress limit.

## Delivery

When the amount is reached, the bot walks to a guild banker in a capital city and deposits into the guild bank.
- Needs an **unlocked guild bank** with **at least one tab the bots' rank may deposit into**. Otherwise the bot mails the items to the **guild master** instead, right from the city (mail works even when the guild master is offline; after 3 empty banker searches it mails).
- The bank may get a few more than ordered: deposits move whole stacks.
- Unspent guild gold comes back to the guild bank: when the order ends (or is cancelled), the bot deposits what it still holds at its next guild banker visit.
- A cancelled order disappears from the list at once; the repayment still happens behind the scenes.
- The addon status shows the current step (`bags`, `bank`, `ah`, `farm`, `craft`, `sub`, `need` or `deliver`), the optional guild gold cap, and any missing item list.
- Extra materials left after crafting go to the guild bank with the finished item, but only what the order itself gained; the bot's own materials stay in its bags.

## Enchant orders

An enchanter bot puts one enchant on your own item, from real materials, meeting you in its capital (Stormwind for the Alliance, Orgrimmar for the Horde).
- **Reserved:** while the order runs, its reagents are protected: the bot never sells, auctions, disenchants, mails away or uses them.
- **States:** `working` (gathering, step `bags`, `bank` or `ah`), `waiting` with reason `needitems`, `ready` (materials reserved, waiting for Meet). Cancelling, or running out of time, walks every reagent the bot still holds (up to one cast's worth) to the guild bank through a delivery trip, mailing what the bank cannot take; the row ends once nothing is due.

## Guild bank access

Every bot in a guild with at least one real player in it shares the guild's **bot rank** (by default the highest rank below the guild master).
- A bot that joins gets the bot rank at once; bots already in the guild are moved there once when the server starts.
- The founding guild-master rank is never rewritten. A guild of only a master with no other ranked members keeps its ranks.
- Your own characters are never touched: only bot characters get the bot rank. Bots in such guilds no longer promote, demote, kick or invite anyone.

```text
.bot order rank <rankIndex>
.bot order access
```

- **Change the bots' rank:** `.bot order rank 2` moves every bot to rank 2. Only a player whose own rank may promote and demote members may do this; anyone else gets an error.
- **See what the bots can reach:** `.bot order access` (and the top of `.bot order list`) shows the bot rank, which bank tabs it reaches, and whether it may take guild gold. Guilds without an unlocked bank show no tabs and no gold.
- The header shows red when anything is missing (a tab the bots cannot use, or no gold access). Open the tabs to the bot rank (or pick a higher rank) before giving orders that need the bank.
- **Turn it off:** setting `AiPlayerbot.GuildOrderBotRank = 0` leaves every rank alone; `.bot order access` then answers that the server does not manage bot ranks.

## Who may give orders

You must be a real player in the bot's guild, and your guild rank must be allowed to edit officer notes (set in the normal guild rank settings). Anyone else gets an error. Notes are only the permission check: orders themselves are given with the addon or the chat command, and anything written in officer notes is ignored.

## Commands

```text
.bot order <BotName> farm <item link or id> <amount> [hours|inf] [keep] [gold=<copper>]
.bot order <BotName> buy <item link or id> <amount> [hours|inf] [keep] [gold=<copper>]
.bot order <BotName> craft <item link or id> <amount> [hours|inf] [keep] [gold=<copper>] [bring=<item>,<item>]
.bot order <BotName> cancel
.bot order <BotName> preview <farm|craft> <item link or id> <amount> [bring=<item>,<item>]
.bot order <BotName> steps
.bot order list
.bot order search <BotName> <farm|buy|craft> [cat=<kind>] <text>
.bot order search craft <text>
.bot order reassign <SubOrderBot> <NewBot>
```
`.bot order <BotName> steps` lists the order's last 16 steps with their age (the same steps the status shows), read-only and memory only, so it starts empty after a restart, a new order or a reassign.

## Known limitations

- A bot that cannot reach the guild banker shops or asks other bots for items that are already in the guild bank, instead of waiting or walking there.
- A bot standing somewhere with no route to a counter, banker or farm spot gives up that step at once; move it somewhere it can travel from so it can continue.
- A shop trip can still take the bot on a long, dangerous walk; if it cannot reach any shop that sells the item, the item lands on your need list.
- A `bankdenied` reason can still appear even when the tab and gold access look right.
