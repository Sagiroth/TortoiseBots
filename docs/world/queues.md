---
id: world-queues
title: Dungeon & battleground queues
category: world
summary: How bots participate in Looking for Turtles dungeon matchmaking and PvP battlegrounds.
tags: [world, queues, lft, battlegrounds, pvp, dungeons]
relates_to:
  - world-living-world
  - play-dungeons-and-raids
  - server-settings-queues
---

# Dungeon & battleground queues

This page explains how bots fill dungeon parties through Looking for Turtles (LFT) and take part in player-versus-player battlegrounds. Queue settings and intervals live in `aiplayerbot.conf`; see [Queue & matchmaking settings](../server/settings/queues.md).

## Looking for Turtles (LFT) dungeon autofill

The Looking for Turtles autofill service monitors dungeon queue requests and backfills missing roles with eligible open-world bots:

| Feature | Behavior |
| :--- | :--- |
| **Human demand gating** | Random bots only queue when real players are waiting in LFT; bots never queue on their own. |
| **Role composition** | Autofill provides the exact missing roles (Tank, Healer, DPS) needed to form a complete 5-player party. |
| **Strict spec matching** | Tanks must equip a shield (or Bear Form for Druids); Healers must know core healing spells. |
| **Party travel** | Formed groups walk to the dungeon portal together; there is no automatic teleport or summon. |

<details>
<summary>Demand accounting & role ratios in detail</summary>

LFT autofill calculates missing roles from the waiting party setup:
- A solo **Tank** receives a Healer and three DPS bots.
- A solo **Healer** receives a Tank and three DPS bots.
- A solo **DPS** receives a Tank, a Healer, and two DPS bots.
- Groups of 2 to 4 players receive only the specific roles missing from their party.

Player role signups match standard core logic: multi-role selections take the first available vacant slot in order (Tank, Healer, DPS). If a queued player party fits no valid slot combination, the fill service pauses until the queue state resolves rather than stranding bots.

</details>

<details>
<summary>Strict role validation & spec borrowing in detail</summary>

Role matching is fail-closed based on each bot's talent spec and equipped gear:
- A **Tank** slot requires a tanking specialization with a shield equipped (Warrior, Paladin, Shaman). The bot equips the best shield from its bags before queueing.
- Druid tanks must have Bear Form. A Shaman tank needs its key talents (*Totemic Alignment* or *Spirit Armor*, from level 25) or the installed tank kit.
- A 7.3 Shaman below that level without the kit is offered as DPS instead, never as a tank.
- A **Healer** slot requires a healing specialization with core healing spells trained.
- If no eligible bot can be found, the slot stays empty with a logged reason rather than assigning an unsuitable spec.

- Admins can optionally let bots respec into missing roles using [`AiPlayerbot.RandomBotLftAllowRoleBorrow`](../server/settings/queues.md#aiplayerbotrandombotlftallowroleborrow).
- When a bot whose default kit differs from its queued role is adopted (such as a Feral Druid queued as a Tank), the engine assigns the matching role kit (Bear kit instead of Cat kit).
- This rebuilds the strategies on the spot — the same rebuild [`.bot role`](../play/commands/roster-and-hiring.md#bot-role) runs.
- Clearing the role when the fill ends rebuilds the bot's own kit again, so a borrowed role never outlives the queue.

</details>

<details>
<summary>Dungeon portal travel & entry in detail</summary>

Once a group forms, bots accept the dungeon invite and adopt the party master. Bots walk toward the dungeon portal on foot with their party.

There is no automatic dungeon teleportation or summoning. Players must guide their group to the instance portal or use [`.bot summon`](../play/commands/roster-and-hiring.md#bot-summon) to bring straggling bots directly to the entrance.

</details>

### LFT with your own party bots
When you queue for LFT with owned alts or hired companions in your party:
- The core automatically answers the role check for your party bots: named combat specs decide (assist strategies are ignored); with nothing set, automatic spec/gear detection applies ([`.bot role`](../play/commands/roster-and-hiring.md#bot-role)).
- Your party bots automatically accept dungeon offers on the fill service tick cadence, so offers do not expire while they wait.
- Party auto-accept runs whether [`AiPlayerbot.RandomBotLftEnabled`](../server/settings/queues.md#aiplayerbotrandombotlftenabled) is enabled or disabled.

## Battleground auto-queue (human demand)

The battleground service fills PvP matches so players get active battlegrounds:

- **Supported battlegrounds:** Warsong Gulch (WSG), Arathi Basin (AB), Alterac Valley (AV), Sunnyglade Valley (SV) and the Blood Ring arena (BR). SV and BR are queued only for their own maps; Blood Ring takes max-level bots only.
- **Demand-driven backfill:** When human players enter a battleground queue, the service scans every 5 seconds, queueing up to 10 bots per check (5 per faction) toward match minimums. Setting [`AiPlayerbot.RandomBotBgMaxQueuePerInterval = 0`](../server/settings/queues.md#aiplayerbotrandombotbgmaxqueueperinterval) disables both interval scans and bot backfills.
- **Match launch threshold:** The system fills each faction to the match minimum plus one. This launches matches promptly without accumulating surplus bots that could trigger a separate, unwanted bot-only match.
- **Prompt exit:** When a battleground match concludes, bots leave the instance rather than waiting for the server instance shutdown timer.
- **Enabled by default:** Controlled by [`AiPlayerbot.RandomBotBgEnabled`](../server/settings/queues.md#aiplayerbotrandombotbgenabled).

<details>
<summary>Battleground objectives & map mechanics in detail</summary>

Bots run dedicated objective strategies inside battlegrounds:
- **Warsong Gulch:** Flag carriers head for their home base; other bots guard or follow the friendly carrier, chase the enemy carrier, or patrol midfield.
- **Arathi Basin:** Bots capture neutral or enemy banners within reach, ignoring friendly banners. Banner interactions are rate-limited to once every 4 seconds.
- **Alterac Valley:** Bots navigate toward Alterac Valley map points (graveyards, towers, bunkers, captains, and end bosses).
- **Sunnyglade Valley:** Bots read the node world states and capture a neutral or enemy node, skipping the banners their own team already holds.
- **Blood Ring:** Bots seek the nearest enemy and fight; the arena has no waypoints, so the navmesh walks them straight in.
- **Warsong Gulch roles:** with [`AiPlayerbot.WsgFlagRoles`](../server/settings/queues.md#aiplayerbotwsgflagroles) on, each team's bots split into attackers, defenders and midfielders instead of all guarding or chasing.
- **Healer preservation:** Healers (Holy/Discipline Priests, Restoration Shamans/Druids, Holy Paladins) keep their healing talent specs in battlegrounds. A global damage-dealer directive cannot swap them onto a damage spec there; an explicit strategy change or forced role still wins.

</details>

## Autonomous bot-only matches

To keep battlegrounds lively when no human players are queued, pool bots can run self-contained matches:

- **Autonomous seeding:** When queue demand from real players is zero, pool bots seed their own Warsong Gulch matches in the most populated level bracket. Only ungrouped bots seed; both factions must have enough eligible bots to field a match.
- **Player priority:** Human queue demand takes absolute priority. Autonomous seeding pauses whenever a real player waits in any battleground queue (players already invited or inside a match don't count).
- **Instance concurrency:** Autonomous seeds add one bot per tick by default until a 10v10 match forms. Concurrency is capped to one full match per bracket by default via [`AiPlayerbot.RandomBotBgAutonomousMaxInstances`](../server/settings/queues.md#aiplayerbotrandombotbgautonomousmaxinstances).
- **Settings:** Governed by [`AiPlayerbot.RandomBotBgAutonomous`](../server/settings/queues.md#aiplayerbotrandombotbgautonomous) and [`AiPlayerbot.RandomBotBgAutonomousMaxInstances`](../server/settings/queues.md#aiplayerbotrandombotbgautonomousmaxinstances). Needs [`AiPlayerbot.RandomBotBgEnabled`](../server/settings/queues.md#aiplayerbotrandombotbgenabled).

## Battlegrounds with your own party bots

Players can queue for battlegrounds alongside their own alts. Hired companions are for PvE: by default they leave you when you enter a battleground ([`AiPlayerbot.HireAllowPvP`](../server/settings/hiring.md#aiplayerbothireallowpvp) = `1` lets them come along).

- **Join as Group:** At the battlemaster, select **Join as Group**. Party bots pass group qualification checks (present in world, correct level bracket, no Deserter debuff) and enter the queue with you.
- **Supported maps:** Group queueing works for Warsong Gulch and Arathi Basin. The core rejects group queues for Alterac Valley, so queue AV solo alongside your bots.
- **Auto-accepting invites:** Party bots automatically accept the battleground entry invite, drop follow, and run map-specific PvP strategies for the duration of the match.
- **Queue isolation:** The service only cancels queue entries it queued itself; party bot entries are never cancelled or modified by reconciliation.
