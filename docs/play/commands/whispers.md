---
id: play-commands-whispers
title: Whisper Commands
category: play
summary: Direct bot delegation and command reference via chat whispers and the CLI proxy.
tags: [commands, whispers, delegation]
relates_to:
  - play-commands
  - play-commands-party-actions
  - play-commands-bot-panel
---

# Whisper Commands

In addition to group-wide party actions, you can issue commands directly to individual companion bots using in-game whispers (`/w <BotName> <command>`) or the command-line proxy `.bot command <BotName> <command>`. Whispers allow precise control over inventory, vendor transactions, quest handling, and talent allocation.

## Command Delegation Proxy & Permissions

Any whisper command can be sent through the `.bot command` proxy for a bot you control (owner account, master, claim, or GameMaster):

```text
.bot command <BotName> <command>
# Example: .bot command Tanky trainer
```

A bot answers on its next think step, even when it is idle. If you command from outside the group, the answer comes back as a whisper.

The prefix `do <action>` or `d <action>` (for example `/w Tanky do attack`) runs any AI action by name.

### Access Tiers

Whisper commands fall into distinct permission tiers:
- **Tactical / Info (Group Members):** Combat movement, target targeting, and read-only status queries. Any group member may issue these.
- **Owner or GM:** Economy, inventory, equipment, talents, trades, guild actions, and lasting setup changes. Only the bot's owner account or a GameMaster may use these. A refused whisper answers in chat (plus one addon error line); other chat types stay silent.
- **GM Only:** Diagnostic and cheat commands (`debug`, `cdebug`, `cs`, `cheat`, `teleport`, `log`, `set value`, `save ai`, `load ai`, `list ai`, `reset ai`). GameMaster privilege is required.

Multiple whisper commands can be chained in a single line using `\\` as a delimiter (e.g. `/w Tanky follow\\co +focus`).

---

<a id="inventory--items"></a>
## Inventory, Items & Banking

| Command / Whisper | Parameters | Tier | What It Does |
| :--- | :--- | :--- | :--- |
| `c` / `items` / `inv` | None | Tactical | Lists equipped and carried items with counts in chat. |
| `e` / `equip` | `<item link\|name>` | Owner or GM | Equips the specified item from bags into matching slot. |
| `ue` | `<slot>` | Owner or GM | Unequips gear from the specified equipment slot. |
| `u` | `<item link\|name>` | Owner or GM | Uses an item from inventory (potions, food, bandages). |
| `destroy` | `<item link\|name>` | Owner or GM | Destroys an inventory item (all bots; strangers refused). |
| `drop` | `<quest link\|name>` | Owner or GM | Abandons the specified quest (all bots; strangers refused). |
| `loot` | `[object link]` | Owner or GM | Loots nearby corpses, chests, and nodes immediately. With a link, loots that object. |
| `ll` | None | Owner or GM | Links or lists current loot items in chat. |
| `bank` | None | Owner or GM | Interacts with a nearby banker NPC. |
| `gbank` / `gb` | None | Owner or GM | Interacts with a nearby guild banker NPC. |
| `mail` / `sendmail` | `<recipient> [<item\|money>]` | Owner or GM | Sends mail with optional attached item link or money. |
| `craft` | `<item link>` | Owner or GM | Crafts an item using learned trade skills. |
| `outfit` | `<name> [item links]` | Owner or GM | Adds the linked items to the named set, or equips the set when no links are given. |
| `outfit <name> equip` | `<name>` | Owner or GM | Equips the items stored in the named outfit set. |
| `keep` | `<item link\|name>` | Owner or GM | Designates an item to keep and not sell. |
| `keep need` | `<item>` | Owner or GM | Forces Need rolls / retention for that item (bot tries to get more). |
| `roll <need\|greed\|pass\|auto\|emote>` | `[item]` | Owner or GM | Votes on current loot rolls (or all open rolls if omitted). |
| `roll feedback` | None | Owner or GM | Toggles chat feedback on roll decisions. |

---

## Vendors, Repairs & Economy

| Command / Whisper | Parameters | Tier | What It Does |
| :--- | :--- | :--- | :--- |
| `s` | `[gray\|all\|<item>]` | Owner or GM | Sells junk items or specific items at an open vendor. |
| `b` | `<item link\|name>` | Owner or GM | Buys the specified item from the active vendor window. |
| `bb` | `<item link\|name>` | Owner or GM | Buys back an accidentally sold item from the vendor. |
| `repair` | None | Owner or GM | Repairs damaged equipment at a repair merchant. |
| `t` / `nt` | None | Owner or GM | Initiates or accepts a direct trade window. |
| `wts` | None | Tactical | Advertises items for sale or checks trade availability. |
| `ah` | `[vendor\|itemlink money]` | Owner or GM | Auctions items at a nearby auction house. |
| `ah bid` | `[vendor\|itemlink money]` | Owner or GM | Bids on auction house items within the given budget. |
| `ah cancel` | `[itemlink\|all\|<auctionId>]` | Owner or GM | Cancels the bot's own auction house listings. |

---

## Quests & NPC Interaction

| Command / Whisper | Parameters | Tier | What It Does |
| :--- | :--- | :--- | :--- |
| `q` / `quests` | None | Owner or GM | Lists active quests and objective progress in chat. |
| `accept` | None | Owner or GM | Accepts an offered quest from a targeted NPC. |
| `talk` | None | Owner or GM | Interacts and gossips with the targeted NPC. |
| `r` / `reward` | `[reward index]` | Owner or GM | Selects quest reward and turns in completed quest. |
| `share` | None | Owner or GM | Shares eligible quests with other party members. |
| `doquest` | `[quest]` | Owner or GM | Directs the bot to focus travel on a specific quest objective. |
| `d` / `do` | `<action>` | Owner or GM | Generic action prefix that executes any AI action by name. |

---

## Training, Spells & Strategy Profiles

| Command / Whisper | Parameters | Tier | What It Does |
| :--- | :--- | :--- | :--- |
| `trainer` | None | Owner or GM | Trains all available, affordable class spells and ranks. |
| `talents` | None | Owner or GM | Prints spent talent points and tree distribution. |
| `talents shift` | `<talent link>` | Owner or GM | Allocates remaining talent points toward linked build. |
| `spells` | None | Tactical | Lists known spells. |
| `spell` | `<spell link\|name>` | Owner or GM | Queries information about a specific spell. |
| `cast <spell>` | `<spell link\|name>` | Owner or GM | Commands the bot to cast a known spell in combat. |
| `castnc <spell>` | `<spell link\|name>` | Owner or GM | Commands the bot to cast a known spell out of combat. |
| `portal [city]` | `[city name or prefix]` | Owner or GM | Lists the mage's learned portals, or casts that portal where it stands. |
| `ss` | `<spell link\|name>` | Owner or GM | Skips or toggles automatic usage of a specific spell. |
| `save ai <name>` | `<name>` | GM Only | Saves the current strategy setup under a name. |
| `load ai <name>` | `<name>` | GM Only | Loads a saved strategy setup. |
| `list ai` | None | GM Only | Lists saved strategy setups. |
| `reset ai` | None | GM Only | Resets strategies to the default AI. |
| `reset` | None | Owner or GM | Flushes AI action queues and resets combat strategies. |
| `co` | `[+|-|~strategy\|?]` | Owner or GM | Queries or toggles combat strategies (`co ?` to list). |
| `nc` | `[+|-|~strategy\|?]` | Owner or GM | Queries or toggles non-combat strategies (`nc ?` to list). |
| `de` | `[+|-|~strategy\|?]` | Owner or GM | Queries or toggles dead strategies (`de ?` to list). |
| `react` | `[+|-|~strategy\|?]` | Owner or GM | Queries or toggles reaction strategies (`react ?` to list). |
| `all` | `[+|-|~strategy\|?]` | Owner or GM | Queries or toggles strategies across all engines. |

---

## Travel, Group & Recovery

| Command / Whisper | Parameters | Tier | What It Does |
| :--- | :--- | :--- | :--- |
| `home` | None | Owner or GM | Binds the bot's Hearthstone at the current innkeeper. |
| `taxi ?` | None | Owner or GM | Lists numbered flight taxi routes available. |
| `taxi <number>` | `<number>` | Owner or GM | Selects a numbered flight taxi destination. |
| `release` | None | Owner or GM | Releases spirit to the graveyard after dying. |
| `corpse run` | None | Owner or GM | Paths back to corpse location in spirit form. |
| `revive` | None | Owner or GM | Resurrects at Spirit Healer with resurrection sickness. |
| `invite` | `[player]` | Owner or GM | Sends a party invitation to a player. |
| `join` | None | Owner or GM | Accepts a pending group invitation. |
| `leave` | None | Owner or GM | Leaves the current group. |
| `lfg` | `[5\|10\|20\|25\|40]` | Owner or GM | Forms or joins a group of that size (default 5). |
| `summon` | None | Owner or GM | Teleports the bot to you. |
| `logout` | None | Owner or GM | Initiates clean logout for the bot session. |
| `bg free` | None | Owner or GM | Leaves the active battleground. |
| `give leader` | None | Owner or GM | Promotes you when the bot leads the group. |

---

## Target Assignments

Bots support explicit target-assignment lists using the `+name` / `-name` parameter grammar:

| Command / Whisper | Parameters | Tier | What It Does |
| :--- | :--- | :--- | :--- |
| `focus heal` | `<+name\|-name>` | Owner or GM | Adds or removes prioritized healing targets. |
| `buff target` | `<+name\|-name>` | Owner or GM | Adds or removes specific targets for class buffs. |
| `revive target` | `<+name\|-name>` | Owner or GM | Adds or removes targets for resurrect spells. |
| `boost target` | `<+name\|-name>` | Owner or GM | Sets prioritized targets for offensive boosts. |
| `follow target` | `<+name\|-name>` | Tactical | Sets or resets a custom follow target. |

---

## Tactical & Combat Overrides

| Command / Whisper | Parameters | Tier | What It Does |
| :--- | :--- | :--- | :--- |
| `attack` | None | Tactical | Attacks your current target and breaks active stay. |
| `tank attack` | None | Tactical | Orders the tank bot to attack your target. |
| `flee` / `runaway` | None | Tactical | Retreats toward master in passive mode. |
| `end pull` | None | Owner or GM | Aborts an active pull maneuver; tank returns. |
| `rebuff` | None | Tactical | Refreshes expiring party buffs out of combat. |
| `buff` | None | Owner or GM | Re-casts missing class buffs on party members. |
| `pet` | `[aggressive\|defensive\|passive]` | Owner or GM | Sets pet stance (aggressive, defensive, passive). |
| `pet autocast` | `<spell name>` | Owner or GM | Toggles autocast on a specific pet ability. |
| `rti <mark>` | `<mark>` | Tactical | Assigns raid target icon focus or CC mark. |
| `attack rti <mark>` | `<mark>` | Tactical | Orders bot to attack enemy marked with raid icon. |
| `pull rti <mark>` | `<mark>` | Tactical | Orders tank bot to pull enemy marked with raid icon. |
| `stance` | `[stance name]` | Tactical | Queries or sets warrior/druid combat stance or form. |
| `save mana` | `[on\|off]` | Tactical | Toggles conservative mana-saving ability rotations. |
| `max dps` | None | Tactical | Temporarily prioritizes maximum damage output. |
| `possible attack targets`| None | Tactical | Queries list of hostile targets in sensory range. |
| `attackers` | None | Tactical | Prints count and names of enemies attacking bot. |
| `position` | None | Tactical | Reports the bot's current coordinates and orientation. |
| `who` | None | Tactical | Prints character summary, spec, and status in chat. |
| `where` | None | Tactical | Reports current map, zone, and location coordinates. |
| `los` | None | Tactical | Reports line-of-sight status to current target. |
| `wait <seconds>` | `<seconds>` | Tactical | Pauses AI internal updates (max 20 seconds). |
| `wait for attack time` | None | Tactical | Holds attacks until main-hand swing timer synchronizes. |
| `jump` | None | Tactical | Triggers an immediate jump action. |
| `warning` | None | Tactical | Prints active combat or environmental warnings. |
| `grind` | None | Owner or GM | Toggles solo autonomous grinding mode for the bot. |
| `skill` | `[name\|unlearn <name>]` | Owner or GM | Lists skills or unlearns a primary profession. |
| `faction` | `[name\|±atwar <name>]` | Owner or GM | Lists factions or toggles at-war status. |
| `rep` | None | Owner or GM | Reports reputation standings. |
| `range` | `[distance]` | Owner or GM | Adjusts or reports combat engagement range. |
| `flag` | `<cloak\|helm\|pvp\|sheathe> <on\|off\|toggle\|?>` | Owner or GM | Sets or queries visual and PvP flags. |
| `speak` | `<text\|/channel text>` | Owner or GM | Makes the bot speak in chat or specified channel. |
| `chat` | `[channel]` | Owner or GM | Sets or reports the bot's current chat channel. |
| `go` | `[target\|x;y\|where\|travel <target>]` | Owner or GM | Orders the bot to move or travel to a destination. |
| `move style` | `[style]` | Owner or GM | Sets bot movement pathing preference. |

---

<a id="guild-management"></a>
## Guild Management Whispers

Commands for managing bot guilds through chat whispers. Only the bot's owner account (or a GM) may issue these commands; for invite, promote, demote, leader and remove, the bot must itself hold the matching guild privilege in its guild rank (any member can leave):

| Command / Whisper | Parameters | Tier | What It Does |
| :--- | :--- | :--- | :--- |
| `guild invite` | `<player>` | Owner or GM | Invites the named character to the bot's guild. |
| `guild join` | None | Owner or GM | Directs the bot to accept an outstanding guild invite. |
| `guild leave` | None | Owner or GM | Orders the bot to leave its current guild. |
| `guild promote` | `<player>` | Owner or GM | Promotes a guild member to the next higher rank. |
| `guild demote` | `<player>` | Owner or GM | Demotes a guild member to the next lower rank. |
| `guild leader` | `<player>` | Owner or GM | Transfers guild leadership to the named member. |
| `guild remove` | `<player>` | Owner or GM | Kicks the named member from the guild. |

Note: Autonomous alt-bots accept guild invitations only from their owner's account (or a GM) and never leave their guild on their own.

---

## Detailed Whisper Reference

<a id="w--portal"></a>
### `/w <BotName> portal`

Bare `portal` whispers back the cities the mage knows (`Portals: Ironforge, Stormwind, ...`). With a city it casts that portal where the bot stands, then confirms.

```text
/w <BotName> portal
/w <BotName> portal iron
```

City matching is case-insensitive; a unique prefix (`iron` for Ironforge) works. Out of combat only; a moving bot stops first, then casts. The rune is free (the bot gets one if missing, so the cast never fails for lack of a rune). Refusals name the reason: in combat, not a mage, portal not learned or unknown city, dead, or busy casting.

<a id="w--trainer"></a>
### `/w <BotName> trainer`

Makes the bot learn, in one go, every spell and rank it can afford from the trainer **you have selected**. The bot pays with its own gold.

```text
/w <BotName> trainer
```

Select the trainer first; it must be a trainer of the bot's class (the bot does not have to stand next to it). Only the bot's owner (or a GM) may use it. Pool bots and hired companions learn their class spells for free on level-up and do not need this.

<a id="w--talents"></a>
### `/w <BotName> talents`

Prints the bot's current talent tree distribution and unspent talent points to chat.

```text
/w <BotName> talents
/w <BotName> talents shift <talent link>
```

Passing `shift <link>` allocates available talent points toward the talent build encoded in the link. Talent links support at most three trees separated by `-`; extra trees are rejected before parsing.

<a id="w--rebuff"></a>
### `/w <BotName> rebuff`

Prompts the bot to refresh all expiring party buffs out of combat.

```text
/w <BotName> rebuff
```

Performs a 2-minute expiration check and applies missing buffs. Healing actions temporarily yield while rebuffing runs. (The same pass also runs as `.bot action rebuff`.)

<a id="w--end-pull"></a>
### `/w <BotName> end pull`

Aborts a pull maneuver currently underway.

```text
/w <BotName> end pull
```

Whisper this to the tank bot if an enemy evades, paths abnormally, or gets stuck. The tank immediately drops the pull target and returns to follow formation instead of waiting for the 15-second timeout.

### `/w <BotName> attack`

Orders the bot to attack your current hostile target.

```text
/w <BotName> attack
```

Like party-wide attacks, this ends any active stay hold on the bot. If you have no target, the bot replies that you have no target and maintains its position.

### `/w <BotName> flee`

Orders the bot to drop combat and retreat toward you in passive mode.

```text
/w <BotName> flee
```

Alias: `runaway`. Any subsequent attack or movement command restores active combat.

### `/w <BotName> loot`

Orders the bot to loot nearby corpses, mineral nodes, herbs, or chests immediately.

```text
/w <BotName> loot [object link]
```

Passing an object link instructs the bot to interact specifically with that linked object. A direct order from you retries even if the bot gave up on that corpse before. To see what your bots picked up later, use [`.bot lootlog`](bot-panel.md#bot-lootlog).

### `/w <BotName> s`

Sells gray junk items or specific bag items when standing near an open merchant window.

```text
/w <BotName> s [gray|all|<item link>]
```

`s gray` sells poor-quality vendor trash; `s all` sells all sellable bag items (including useful items); linking an item sells only that specific item.

### `/w <BotName> destroy`

Permanently destroys the specified item from the bot's bags to free up inventory space.

```text
/w <BotName> destroy <item link|name>
```

Restricted to the bot's owner account or a GM across all bot types. Strangers are always refused.

### `/w <BotName> drop`

Abandons the specified quest from the bot's quest log.

```text
/w <BotName> drop <quest link|name>
```

Restricted to the bot's owner account or a GM across all bot types.

### `/w <BotName> mail` / `sendmail`

Interacts with a nearby mailbox to check received mail or send mail with attached items or money.

```text
/w <BotName> mail
/w <BotName> sendmail <recipient> [<item link>|<money>]
```

Requires the bot to stand within interaction range of a mailbox.

### `/w <BotName> taxi`

Queries or purchases flight taxi transport from a flight master NPC.

```text
/w <BotName> taxi ?
/w <BotName> taxi <number>
```

Whispering `taxi ?` lists available flight routes and numbers; whispering `taxi <number>` purchases the flight.

<a id="w--release"></a>
### `/w <BotName> release`

Orders a dead companion bot to release spirit to the graveyard.

```text
/w <BotName> release
```

### `/w <BotName> corpse run`

Commands a companion bot in ghost form to path back to its corpse and resurrect.

```text
/w <BotName> corpse run
```

### `/w <BotName> revive`

Orders a companion bot in ghost form to resurrect immediately at the Spirit Healer.

```text
/w <BotName> revive
```

### `/w <BotName> co` / `nc` / `de` / `react` / `all`

Inspects or modifies strategy trees across different execution states: combat (`co`), non-combat (`nc`), dead (`de`), reaction (`react`), or all engines (`all`).

```text
/w <BotName> co <+|-|~strategy>
/w <BotName> nc <+|-|~strategy>
/w <BotName> de <+|-|~strategy>
/w <BotName> react <+|-|~strategy>
/w <BotName> all <+|-|~strategy>
/w <BotName> co ?
```

Typing `co ?` prints the bot's currently active combat strategies. Prefixing a strategy name with `+` enables it, `-` disables it, and `~` toggles it. For example, `/w <BotName> co -offdps` stops a healer's damage spells and `/w <BotName> co +offheal` lets a DPS hybrid heal (see [off-spec rules](../../classes/overview.md#healer-off-dps-offdps)).

The `world pvp` strategy makes one bot seek PvP-flagged enemy players in the open world (`nc +world pvp` to switch it on, `nc -world pvp` for off). It is a pool-bot role: owned and hired bots ignore the pool roll, but the toggle still works on one bot.
