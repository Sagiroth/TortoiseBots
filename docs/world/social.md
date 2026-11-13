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

## Passing buffs

A pool bot passing a real player in the world casts a class buff on them and keeps going, like a friendly player running past. Only instant casts: the bot never stops, turns around, chases or leaves its travel.

- **Who buffs:** masterless pool bots only (no group with the player, no master, out of combat, alive, not on a taxi, enough mana left after the cast). Targets are real players only (network session, not a bot), same faction, alive, not grouped with the bot, in line of sight and within spell range (~30 yd). Invisible and GM-hidden players are skipped.
- **Which buff:** the class party buff the bot already casts on its own group (priest Power Word: Fortitude, druid Mark of the Wild, mage Arcane Intellect for mana users, paladin one Blessing fitting the player's class), highest rank the bot knows that is allowed on the player's level. Only when the player lacks that buff and nothing stronger is on them (group version counts).
- **Chance:** 1 in 5 per meeting, then a 10-minute cooldown per bot-player pair.
- **Passing heal:** a player below 35% health (they may be fighting; the bot stays out of combat) gets Renew (priest) or Rejuvenation (druid) when they lack it. Chance 1 in 2, 60-second cooldown per pair. No resurrection.
- **Setting:** [`AiPlayerbot.PassingBuffs`](../server/settings/world-population.md#aiplayerbotpassingbuffs) (on by default; `0` disables the sweeps).

## Grouping & party invites

| What happens | Details | Setting |
| :--- | :--- | :--- |
| **Bots invite you** | Some pool bots (the ones that like to lead groups) invite a nearby player who is not in a group, is within two levels of them and stands within about spell range. Set `/dnd` and they stop inviting you. | [`AiPlayerbot.RandomBotInvitePlayer`](../server/settings/world-population.md#aiplayerbotrandombotinviteplayer) (off) |
| **Bots group with each other** | Off by default: pool bots quest alone, because bot leaders used to drag their followers around in useless follow chains. | [`AiPlayerbot.RandomBotGroupNearby`](../server/settings/world-population.md#aiplayerbotrandombotgroupnearby) (off) |
| **Food and water** | A mage in your group conjures food and water and trades it to members who have none (water only to mana users). Sharing through the trade window is on by default; giving a conjured item needs the giver to have mana to spare. | On by default |

A pool bot in your group stays with the fight: it only leaves an elite or boss pull when its bags are nearly full (over 80%) and it is about to walk to a vendor, and it never leaves a dungeon or raid to visit a vendor.

<a id="autonomous-guild-formation"></a>
## Guilds

- **Bots founding their own guilds:** off by default ([`AiPlayerbot.RandomBotFormGuild`](../server/settings/world-population.md#aiplayerbotrandombotformguild)). When on, an unguilded bot above level 10 with no invite and enough spare coin buys a charter named `<Bot> Guild`, gathers the core's signatures from nearby unguilded bots of its own faction, and registers the guild.
- **Bots stay in your guild:** a pool bot leaves a guild with a real player in it only when the guild reaches 1000 members. Bots do not invite anyone into a guild that already has [`AiPlayerbot.GuildMaxBotLimit`](../server/settings/world-population.md#aiplayerbotguildmaxbotlimit) (1000) members.
- **Blocked objects:** when a bot refuses to loot or gather for you because the object is blocked, it whispers once per master and object, at most once every 10 minutes, instead of repeating the message on every attempt. The refusal itself is unchanged.
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
