---
id: world-social
title: Social interaction & guilds
category: world
summary: How bots greet players, invite them to groups, join guilds, and trade items.
tags: [world, social, groups, guilds, trading, greetings]
relates_to:
  - world-living-world
  - play-commands-roster-and-hiring
  - server-settings-world-population
---

# Social interaction & guilds

This page describes how pool bots greet players, invite them to groups, join guilds and trade. Social and population settings live in `aiplayerbot.conf`; see [World population & immersion settings](../server/settings/world-population.md).

## Greetings & emotes

In the open world and towns, bots acknowledge passing players:

- **Greeting players:** A bot passing a human player greets them with a friendly text emote (once per encounter while the player stays in sight; leaving and returning counts as a new meeting). By default only bots with a live real-player master greet; pool-wide greeting is opt-in ([`AiPlayerbot.RandomBotGreet`](../server/settings/world-population.md#aiplayerbotrandombotgreet)).
- **Bot-to-bot silence:** Bots never greet other bots. With hundreds of bots active in shared zones, bot-to-bot greetings would flood local chat channels.
- **Encounter cooldown:** After greeting a player, the bot enters a per-player cooldown (`AiPlayerbot.GreetCooldown`, 600 s default) before it can greet that same player again, preventing repetitive emotes if the player walks in and out of range.
- **Settings:** Greetings fire through the `new player nearby` trigger and are enabled by [`AiPlayerbot.EnableGreet`](../server/settings/world-population.md#aiplayerbotenablegreet) and paced by [`AiPlayerbot.GreetCooldown`](../server/settings/world-population.md#aiplayerbotgreetcooldown).

## Grouping & party invites

| What happens | Details | Setting |
| :--- | :--- | :--- |
| **Bots invite you** | Some pool bots (the ones that like to lead groups) invite a nearby player who is not in a group, is within two levels of them and stands within about spell range. Set `/dnd` and they stop inviting you. | [`AiPlayerbot.RandomBotInvitePlayer`](../server/settings/world-population.md#aiplayerbotrandombotinviteplayer) (on) |
| **Bots group with each other** | Off by default: pool bots quest alone, because bot leaders used to drag their followers around in useless follow chains. | [`AiPlayerbot.RandomBotGroupNearby`](../server/settings/world-population.md#aiplayerbotrandombotgroupnearby) (off) |
| **Food and water** | A mage in your group conjures food and water and trades it to members who have none (water only to mana users). Sharing through the trade window is on by default; giving a conjured item needs the giver to have mana to spare. | On by default |

A pool bot in your group stays with the fight: it only leaves an elite or boss pull when its bags are nearly full (over 80%) and it is about to walk to a vendor, and it never leaves a dungeon or raid to visit a vendor.

<a id="autonomous-guild-formation"></a>
## Guilds

- **Invite bots to your guild:** a pool bot accepts a normal guild invite (`/ginvite <name>`) when it has no guild. Hired companions always refuse. Your own alts that play on their own (alt-bots) accept only invites from your account (GMs can also invite them). Bots in your guild can then take [guild orders](../play/guild-orders.md).
- **Bots founding their own guilds:** the code for it is there and switched on ([`AiPlayerbot.RandomBotFormGuild`](../server/settings/world-population.md#aiplayerbotrandombotformguild)): a bot buys a guild charter, collects signatures from other bots and names the guild `<BotName>'s Guild`. On test realms with 1000 bots this did not happen in practice (no charter was bought), so do not expect bot guilds to appear.
- **Bots stay in your guild:** a pool bot leaves a guild with a real player in it only when the guild reaches 1000 members. Bots do not invite anyone into a guild that already has [`AiPlayerbot.GuildMaxBotLimit`](../server/settings/world-population.md#aiplayerbotguildmaxbotlimit) (1000) members.
- **Guild whispers:** the owner of a bot (or a GM) can whisper it `guild invite <name>`, `guild join`, `guild leave`, `guild promote <name>`, `guild demote <name>`, `guild leader <name>` and `guild remove <name>`; inviting, promoting, demoting and removing need the bot to hold the matching rank right, passing leadership needs the bot to be guild leader, and joining or leaving need no rank right. Other players cannot. See [Whisper Commands](../play/commands/whispers.md#guild-management).
- **Pool resets keep player guilds:** a pool reset stops when a guild led by a bot has even one real player in it. See [Pool reset safety](../server/bot-pool.md#safety-guarantees--multi-phase-execution).

## Trading with pool bots

Players can interact with unmanaged pool bots through the trade window:

- **Trade permissions:** Direct trading with masterless pool bots is governed by [`AiPlayerbot.PoolBotTradeMode`](../server/settings/world-population.md#aiplayerbotpoolbottrademode) (out-of-range values fail closed to `0`):
  - `0`: Off. The window never opens for strangers (pool bots refuse the trade).
  - `1`: Trusted. Strangers are refused; the master's group, the hire master, and the owner account still trade.
  - `2`: BuyOnly. Strangers may open the window, but only player-gives trades complete (the bot never gives items or gold).
  - `3`: SellOnly. Strangers may open the window, but only bot-gives trades complete (the bot never takes items or gold).
  - `4`: On. Unrestricted trading with players.
- **Addon prefix suppression:** Trade-channel addon chatter (by default the `DBMv4` and `RPLL_H_` prefixes) never opens a trade window; the excluded prefixes are configured through [`AiPlayerbot.TradeActionExcludedPrefixes`](../server/settings/world-population.md#aiplayerbottradeactionexcludedprefixes) (default `RPLL_H_,DBMv4`).
