---
id: world-social
title: Social interaction & guilds
category: world
summary: How bots greet players, form groups, create autonomous guilds, and trade items.
tags: [world, social, groups, guilds, trading, greetings]
relates_to:
  - world-living-world
  - play-commands-roster-and-hiring
  - server-settings-world-population
---

# Social interaction & guilds

This page describes how autonomous bots interact socially with players and other bots, form groups, create guilds, and trade items. Social and population settings are configured in `aiplayerbot.conf`; see [World population & immersion settings](../server/settings/world-population.md).

## Greetings & emotes

In the open world and towns, bots add immersion by acknowledging passing players:

- **Greeting players:** A bot passing a human player greets them with a friendly text emote (once per encounter while the player stays in sight; leaving and returning counts as a new meeting). By default only bots with a live real-player master greet; pool-wide greeting is opt-in ([`AiPlayerbot.RandomBotGreet`](../server/settings/world-population.md#aiplayerbotrandombotgreet)).
- **Bot-to-bot silence:** Bots never greet other bots. With hundreds of bots active in shared zones, bot-to-bot greetings would flood local chat channels.
- **Encounter cooldown:** After greeting a player, the bot enters a per-player cooldown (`AiPlayerbot.GreetCooldown`, 600 s default) before it can greet that same player again, preventing repetitive emotes if the player walks in and out of range.
- **Settings:** Greetings fire through the `new player nearby` trigger and are enabled by [`AiPlayerbot.EnableGreet`](../server/settings/world-population.md#aiplayerbotenablegreet) and paced by [`AiPlayerbot.GreetCooldown`](../server/settings/world-population.md#aiplayerbotgreetcooldown).

## Grouping & party invites

Autonomous pool bots can participate in cooperative grouping:

| Feature | Behavior | Control |
| :--- | :--- | :--- |
| **Inviting solo players** | Bots invite nearby un-grouped players within spell range and two levels (same-map, no teleport). | [`AiPlayerbot.RandomBotInvitePlayer`](../server/settings/world-population.md#aiplayerbotrandombotinviteplayer) |
| **Bot-to-bot parties** | Disabled by default; pool bots stay solo to avoid leashing each other into idle follow chains. | [`AiPlayerbot.RandomBotGroupNearby`](../server/settings/world-population.md#aiplayerbotrandombotgroupnearby) |
| **Elite & boss veto** | Bots only veto pulls if actively about to leave (bags over 80% full with a sellable stock in the overworld). | See [Dungeons & raids](../play/dungeons-and-raids.md). |
| **Conjured item sharing** | Mages trade conjured food and water to group members who lack them (give-food/give-water triggers). | See [Sustenance](living-world.md#sustenance--resting). |

<details>
<summary>Player invites & DND in detail</summary>

When [`AiPlayerbot.RandomBotInvitePlayer = 1`](../server/settings/world-population.md#aiplayerbotrandombotinviteplayer) is active, an autonomous bot questing or grinding in the open world offers a party invite to a nearby un-grouped player within spell range and two levels (ownerless alts of active players are skipped).

If you prefer to play solo without interruptions, set your chat status to `/dnd`. Bots detect the `/dnd` status flag and skip outgoing party invitations to that player.

</details>

<details>
<summary>Bot-to-bot grouping & follow dynamics in detail</summary>

Controlled by [`AiPlayerbot.RandomBotGroupNearby`](../server/settings/world-population.md#aiplayerbotrandombotgroupnearby). When disabled, pool bots decline invitations from other pool bots and quest independently. This prevents bot leaders from leashing followers into unproductive travel loops.

Enabling the setting allows organic grouping among bots, where they band together to take on difficult elite quests and group objectives.

</details>

<details>
<summary>Instance stability & vendor trips during party play in detail</summary>

In an active group, elite and boss pulls are protected against disruption. A bot leaves the pull only when it is about to head to a vendor (bags over 80% full with sellable stock in the open world); boss fights additionally require more than three group members.

Inside dungeons or raids, the vendor purpose never fires (the trip need is overworld-only), so bots never leave the instance to visit a vendor. Party members remain focused on the instance until the group finishes or disbands.

</details>

## Autonomous guild formation

Bots establish player-like social organizations across the realm:

- **Guild charters & petition signing:** Unguilded bots with a non-solo grouper and guilder type travel to petitioner NPCs, purchase a Guild Charter for 1000 copper, and solicit signatures from nearby unguilded bots in sight range. Bots approached for a signature decline when already guilded, invited, hired, or an autonomous alt outside the owner's account.
- **Guild founding:** Once the core's required signature count is collected, the bot registers the charter, names the guild `<BotName>'s Guild`, and displays the guild tag above members' heads.
- **Member recruitment & caps:** Founded guilds actively invite both unguilded bots and interested human players to join. Bots leave guilds that exceed 1000 members or their preferred size; the guild-management invite path also refuses full guilds at [`AiPlayerbot.GuildMaxBotLimit`](../server/settings/world-population.md#aiplayerbotguildmaxbotlimit) (default `1000`).
- **Whisper guild management:** Human players in bot guilds can issue direct roster commands by whispering bot officers and guild leaders (`guild invite`, `guild join`, `guild leave`, `guild promote`, `guild demote`, `guild leader`, `guild remove`; see [Whisper Commands](../play/commands/whispers.md#guild-management)).
- **Configuration:** Guild creation is controlled by [`AiPlayerbot.RandomBotFormGuild`](../server/settings/world-population.md#aiplayerbotrandombotformguild).
- **Reset protection:** A pool reset aborts when a bot-led guild contains even one human player, so the reset never disbands player guilds. For details, see [Pool reset safety guarantees](../server/bot-pool.md#safety-guarantees--multi-phase-execution).

## Trading with pool bots

Players can interact with unmanaged pool bots through the trade window:

- **Trade permissions:** Direct trading with masterless pool bots is governed by [`AiPlayerbot.PoolBotTradeMode`](../server/settings/world-population.md#aiplayerbotpoolbottrademode) (out-of-range values fail closed to `0`):
  - `0`: Off. The window never opens for strangers (pool bots refuse the trade).
  - `1`: Trusted. Strangers are refused; the master's group, the hire master, and the owner account still trade.
  - `2`: BuyOnly. Strangers may open the window, but only player-gives trades complete (the bot never gives items or gold).
  - `3`: SellOnly. Strangers may open the window, but only bot-gives trades complete (the bot never takes items or gold).
  - `4`: On. Unrestricted trading with players.
- **Addon prefix suppression:** Trade-channel addon chatter (by default the `DBMv4` and `RPLL_H_` prefixes) never opens a trade window; the excluded prefixes are configured through [`AiPlayerbot.TradeActionExcludedPrefixes`](../server/settings/world-population.md#aiplayerbottradeactionexcludedprefixes) (default `RPLL_H_,DBMv4`).
