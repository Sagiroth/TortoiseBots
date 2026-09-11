---
id: guide-player-controls
title: Available Bot Commands & Addon Controls
category: guides
summary: Complete and authoritative reference of all currently implemented .bot commands, tactical actions, and addon transport protocols.
tags: [guide, commands, player-controls, addon, tactics, cli]
relates_to:
  - guide-getting-started
  - class-overview
  - concept-strategy-engine
---

# Available Bot Commands & Addon Controls

TortoiseBots provides a native, intent-driven command suite (`.bot`) that powers both the **[TortoiseBotsManager](https://github.com/Sagiroth/TortoiseBotsManager)** (`/tbm`) addon and standard in-game chat commands.

All commands require the requesting player to be an in-game character owning the target bot (same account) or a GameMaster.

---

## 1. Tactical Party Actions (`.bot action <intent>`)

The modern control plane operates on **player intent**. Instead of micromanaging each bot, you issue high-level tactical commands; the server resolves target scope and selects appropriate bot executors:

| Command | Target Required | What It Does |
| :--- | :--- | :--- |
| `.bot action attack` | Hostile Target | Controllable party bots engage your current target immediately. |
| `.bot action interrupt` | Casting Hostile | Evaluates party bots and orders a capable bot in range with a ready interrupt (e.g. *Kick*, *Pummel*, *Earth Shock*, *Counterspell*) to interrupt the cast. |
| `.bot action stop` | None | Clears combat queues and stops current attacks. |
| `.bot action pull` | Hostile Target | Directs the party tank to pull your target with ranged attack/taunt while other bots hold damage until threat is established. |
| `.bot action pullback` | Hostile Target | Tank pulls the target and sprints back to the group's current coordinates. |
| `.bot action come` | None | All bots sprint directly to the player's exact coordinates. |
| `.bot action stay` *(or `hold`)* | None | Bots halt at their current position and hold ground. |
| `.bot action follow` | None | Bots break current movement and resume tight follow formation behind the leader. |
| `.bot action focus skull` | Enemy / None | Sets or targets the **Skull** raid icon; orders all party DPS bots to focus fire on that target. |
| `.bot action cc <mark>` | Marked Mob | Orders a capable bot (Mage *Polymorph*, Rogue *Sap*, Warlock *Seduce*, Priest *Shackle*, Druid *Hibernate*) to CC the target. |
| `.bot action aoe <on\|off>` | None | Toggles whether DPS bots cast high-damage AoE abilities (useful to toggle OFF around CC targets). |
| `.bot action ready` | None | Initiates a group ready check across all party bots. |

---

## 2. Roster & Lifecycle Commands

These commands manage the login, party membership, and presence of your owned bots:

| Command | Syntax | What It Does |
| :--- | :--- | :--- |
| **Roster Snapshot** | `.bot roster` | Returns an authoritative snapshot of all owned characters on your account and their online/party state (emits structured `TBM:ROSTER`). |
| **Login Bot** | `.bot add <Name>` | Logs in an owned character from your account as a headless bot. |
| **Logout Bot** | `.bot remove <Name>` *(or `logout`)* | Cleanly logs out an active headless bot on your account. |
| **Invite to Party** | `.bot invite <Name>` | Sends a party invite to an online bot on your account. |
| **Uninvite from Party**| `.bot uninvite <Name>` *(or `kick`)*| Removes an owned bot from your group. |
| **Summon** | `.bot summon [Name]` | Teleports your owned party bot(s) safely to your location out of combat. |

---

## 3. Direct Party & Movement Commands

Direct command shortcuts that operate on your targeted bot or all party bots:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot follow` | `[Name]` | Orders targeted bot (or all party bots) to follow the requester. |
| `.bot stay` | `[Name]` | Orders targeted bot (or all party bots) to stay at current location. |
| `.bot guard` | `[Name]` | Orders bot to guard its current position and engage nearby threats. |
| `.bot free` | `[Name]` | Releases bot from stay/guard back to free autonomous movement. |
| `.bot ready` | None | Checks if party bots are ready (health/mana full, buffs active). |
| `.bot attack` | `[Name]` | Orders targeted bot to attack your current hostile target. |
| `.bot pullback` | `[Name]` | Dispatches pullback maneuver on the specified bot or designated tank. |
| `.bot formation` | `<arrow\|queue\|near\|line\|circle\|shield>` | Sets the geometric follow formation around the party leader. |

---

## 4. Diagnostics & Inspection Commands

Commands for checking bot state, lifecycle, and fleet metrics:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot list` | None | Lists all online bots currently owned by your account (Name, lifecycle, random flag, AI status). |
| `.bot status` | `<Name>` | Displays detailed status of an owned bot: lifecycle (`starting`, `in world`, `removing`), AI attachment, active movement strategy (`follow`, `stay`, `guard`, `free`, `custom`), and owner. |
| `.bot stats` | None | Summarizes owned bot fleet: total online, random bots, and bots with active AI attached. |
| `.bot lease` | `[status]` | Reports autonomous activity lease counts (Idle, Grinding, Trading, LftQueued, BgQueued, PlayerMaster) and lists active lease timers. |

---

## 5. Mature AI Command Delegation & Whispers

In addition to high-level `.bot action` party commands, you can delegate commands directly to a specific bot either via chat whispers (`/w <BotName> <command>`) or through the `.bot command` CLI proxy:

```text
.bot command <BotName> <command>
# Example: .bot command Dudette trainer
```

### Comprehensive Whisper Command Cheat-Sheet

| Category | Command / Whisper | What It Does |
| :--- | :--- | :--- |
| **Inventory & Bags** | `c` / `items` / `inv` | Lists bag items and free bag slot count in chat. |
| | `e <item>` / `equip <item>` | Equips the specified item link or item name from bags. |
| | `ue <slot>` / `unequip <slot>` | Unequips gear from the specified equipment slot into bags. |
| | `u <item>` / `use <item>` | Uses an item from inventory (potions, quest items, bandages). |
| | `drop <item>` | Destroys an item from inventory to free up bag space. |
| **Vendors & Repairs** | `s [gray\|all\|<item>]` | Sells gray junk items or specific items when targeted vendor is open. |
| | `b <item>` | Purchases the specified item from the open merchant window. |
| | `bb <item>` | Buys back an accidentally sold item from the vendor. |
| | `repair` | Repairs damaged equipment at a blacksmith / armorer NPC. |
| | `t` / `trade` | Initiates or accepts a direct trade window with the player. |
| **Quests & NPCs** | `q` / `quests` | Prints active quests and current objective completion status. |
| | `accept` | Accepts an offered quest from a nearby quest giver. |
| | `talk` | Interacts / gossips with the current NPC target. |
| | `r [choice]` / `reward [choice]` | Selects the quest reward and turns in a completed quest. |
| | `share` | Shares eligible quests from the bot's quest log with party members. |
| **Training & Skills** | `trainer` | Automatically learns all currently available spells and ranks from a class trainer! |
| | `talents` | Prints spent talent points and tree distribution. |
| | `spells` | Lists known spells and highest learned spell ranks. |
| **Travel & Life** | `home` | Interacts with an innkeeper to bind the bot's Hearthstone. |
| | `taxi <destination>` | Purchases a flight path to the specified flight master destination. |
| | `release` | Releases spirit after dying. |
| | `corpse run` | Paths as a ghost back to the corpse location and resurrects. |
| | `revive` | Resurrects immediately at the Spirit Healer (with resurrection sickness). |
| **Combat Overrides** | `flee` / `runaway` | Drops combat anchors and retreats toward the master/tank. |
| | `tank attack` | Forces the party tank to prioritize and taunt your target. |
| | `pet` | Commands pet behavior (`pet follow`, `pet stay`, `pet attack`). |
| | `buff` | Prompts the bot to re-cast missing class buffs on party members. |
| | `grind` | Toggles solo autonomous grinding mode for the bot. |
| | `reset` | Flushes AI action queues and resets combat strategy states. |

---

## 6. Auction House Management (`.bot ah` / `.ahbot`)

*(GameMaster / Administrator only)*

Manage and monitor the autonomous synthetic Auction House engine in real time:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot ah status` | None | Displays synthetic AH engine status, inventory size, active listings, and telemetry. |
| `.bot ah reload` | None | Reloads price overrides and blacklist filters from the `ahbot_items` DB table. |
| `.bot ah rebuild` | `[all]` | Triggers an immediate market evaluation pass. Passing `all` expires active unbid synthetic listings to refresh the market. |
| `.bot ah item` | `<id>` | Checks current blacklist status or active price overrides for an item ID. |
| `.bot ah item` | `<id> reset` | Removes any custom price override for an item ID, restoring formula pricing. |
| `.bot ah item` | `<id> <value> [chance] [min] [max]` | Sets custom price (copper), posting chance (%), and stack bounds. Passing `0 0` **blacklists** the item from being posted. |
