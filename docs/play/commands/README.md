---
id: play-commands
title: Bot Commands Overview
category: play
summary: Master index of all player bot commands, permission tiers, and control channels.
tags: [commands, player-controls, cli]
relates_to:
  - play-commands-party-actions
  - play-commands-commander
  - play-commands-roster-and-hiring
  - play-commands-bot-panel
  - play-commands-whispers
  - play-commands-addon
  - server-admin-commands
---

# Bot Commands Overview

TortoiseBots provides an intent-driven command suite to control companion bots in the field, manage your roster, and configure party behaviors. You can issue orders through native `.bot` chat commands, direct whispers, or the graphic user interface.

## Control Channels

Players interact with bots through three distinct interfaces:

- **Chat Commands (`.bot`):** Native slash commands typed directly into chat or bound to macros. Most party-wide tactical orders use `.bot action <intent>`.
- **Whispers (`/w <BotName> <command>`):** Direct commands sent via in-game whisper, or proxied through `.bot command <BotName> <command>`.
- **Addon Interface (`/tbm`):** The [TortoiseBotsManager](addon.md) graphical interface, which transmits commands over a silent communication channel without cluttering chat frames.

### Command Chaining via `\\`

Multiple sequential whisper commands (or commands sent via the `.bot command` proxy) can be chained together in a single chat line separated by `\\`:

```text
/w Tanky follow\\co +focus
.bot command Tanky follow\\co +focus
```

The mature whisper processor splits the message along the `\\` delimiter and executes each command in sequence on the same world tick. (Note: native `.bot` CLI commands do not support `\\` chaining).

## Security & Ownership

Every bot command evaluates permission gates before executing:

| Tier | Who Can Use | Permitted Operations |
| :--- | :--- | :--- |
| **GM Only** | GameMasters with session security above player level | Diagnostics, engine cheats, AI profiles (`save/load/list/reset ai`), `.bot pool`, `.bot ah`, `.reload config`. |
| **Owner or GM** | Account owner of the bot, or any GM | Trading, inventory, gear equipping, talent points, trainer learning, and lifecycle commands. |
| **Group Member** | Anyone currently grouped with the bot | Tactical combat actions, movement orders, ready checks, whisper `hire` (random bots only), and inspection queries. |
| **Any Player** | Any in-game player | Companion recruitment (`.bot hire`, subject to costs, caps, and `AiPlayerbot.HireAnywhere`). |

### Alt-Bots and Pool Bots

- **Autonomous Alt-Bots:** When an owned alt plays autonomously in the world, other players may invite it and issue tactical combat orders. Only the account owner (or a GM) may trade with it, equip gear, spend talents, manage quests, or log it out. Note that [`.bot cmd`](commander.md) orders never move autonomous alt-bots; order them with whispers or party chat instead.
- **Pool-Bot Trading:** By default, masterless random-pool bots refuse trade requests from players. Owned alts and hired companions always trade freely with their master.
- **Fail-Closed Security:** Unrecognized commands default to requiring Owner or GM permissions. Refused `.bot action` answers with `TBM:ACTION_ERR`; only non-whisper mature-chat denials fail silently (whisper denials answer in chat plus one addon error line).

## Master Command Index

The table below lists all primary commands and links to their authoritative documentation pages; for detailed whisper variations, see [Whisper Commands](whispers.md).

| Command | Category | Description |
| :--- | :--- | :--- |
| [`.bot action attack`](party-actions.md#bot-action-attack) | Tactical Action | Orders party bots to engage your hostile target. |
| [`.bot action interrupt`](party-actions.md#bot-action-interrupt) | Tactical Action | Directs the nearest capable bot to interrupt a cast. |
| [`.bot action stop`](party-actions.md#bot-action-stop) | Tactical Action | Clears combat queues and stops current attacks. |
| [`.bot action flee`](party-actions.md#bot-action-flee) | Tactical Action | Orders party bots to disengage and follow passively. |
| [`.bot action pull`](party-actions.md#bot-action-pull) | Tactical Action | Directs the tank to pull while other bots delay. |
| [`.bot action pullback`](party-actions.md#bot-action-pullback) | Tactical Action | Tank pulls from range and returns to anchor position. |
| [`.bot action come`](party-actions.md#bot-action-come) | Tactical Action | Commands all bots to sprint to your coordinates. |
| [`.bot action stay`](party-actions.md#bot-action-stay) | Tactical Action | Halts bots at their current positions. |
| [`.bot action follow`](party-actions.md#bot-action-follow) | Tactical Action | Breaks current movement and resumes follow formation. |
| [`.bot action focus skull`](party-actions.md#bot-action-focus-skull) | Tactical Action | Assigns Skull mark and orders focused damage. |
| [`.bot action cc`](party-actions.md#bot-action-cc) | Tactical Action | Assigns crowd control marks to capable bots. |
| [`.bot action cc clear`](party-actions.md#bot-action-cc-clear) | Tactical Action | Clears crowd control mark assignments. |
| [`.bot action auto cc`](party-actions.md#bot-action-auto-cc) | Tactical Action | Toggles autonomous crowd control on loose adds. |
| [`.bot action sap`](party-actions.md#bot-action-sap) | Tactical Action | Configures rogue pre-pull Sap behavior on CC marks. |
| [`.bot action aoe`](party-actions.md#bot-action-aoe) | Tactical Action | Toggles area-of-effect abilities across the party. |
| [`.bot action loot`](party-actions.md#bot-action-loot) | Tactical Action | Toggles corpse looting across scoped bots. |
| [`.bot action repair`](party-actions.md#bot-action-repair) | Tactical Action | Directs party bots to repair at a nearby vendor. |
| [`.bot action sell`](party-actions.md#bot-action-sell) | Tactical Action | Directs party bots to sell gray junk items. |
| [`.bot action rest`](party-actions.md#bot-action-rest) | Tactical Action | Orders party bots to consume food and drink. |
| [`.bot action release`](party-actions.md#bot-action-release) | Tactical Action | Commands dead bots to release spirit to graveyard. |
| [`.bot action corpse run`](party-actions.md#bot-action-corpse-run) | Tactical Action | Commands ghost bots to run back to their corpse. |
| [`.bot action learn`](party-actions.md#bot-action-learn) | Tactical Action | Commands bots to train spells at nearby trainers. |
| [`.bot action trade`](party-actions.md#bot-action-trade) | Tactical Action | Opens a trade window with the targeted companion. |
| [`.bot action ready`](party-actions.md#bot-action-ready) | Tactical Action | Performs a readiness check across all party bots. |
| [`.bot action raid status`](party-actions.md#bot-action-raid-status) | Tactical Action | Reports active raid encounter strategy states. |
| [`.bot action raid tankface`](party-actions.md#bot-action-raid-tankface) | Tactical Action | Orders tank bots to face dragon bosses away. |
| [`.bot action raid douse`](party-actions.md#bot-action-raid-douse) | Tactical Action | Commands bots to douse nearby Molten Core runes. |
| [`.bot action raid custom status`](party-actions.md#bot-action-raid-custom-status) | Tactical Action | Reports custom Turtle raid strategy state. |
| [`.bot action raid custom on`](party-actions.md#bot-action-raid-custom-on) | Tactical Action | Enables custom Turtle raid transition strategies. |
| [`.bot action raid custom off`](party-actions.md#bot-action-raid-custom-off) | Tactical Action | Disables custom Turtle raid transition strategies. |
| [`.bot follow`](party-actions.md#bot-follow) | Direct Movement | Orders a specific bot to follow you. |
| [`.bot stay`](party-actions.md#bot-stay) | Direct Movement | Orders a specific bot to stay in place. |
| [`.bot guard`](party-actions.md#bot-guard) | Direct Movement | Orders a bot to guard its current position. |
| [`.bot free`](party-actions.md#bot-free) | Direct Movement | Releases a bot from stay/guard to free movement. |
| [`.bot ready`](party-actions.md#bot-ready) | Direct Party | Runs a readiness check on a single bot. |
| [`.bot attack`](party-actions.md#bot-attack) | Direct Combat | Directs a specific bot to attack your target. |
| [`.bot interrupt`](party-actions.md#bot-interrupt) | Direct Combat | Directs a specific bot to interrupt its target. |
| [`.bot pullback`](party-actions.md#bot-pullback) | Direct Combat | Triggers a pullback maneuver on a designated tank. |
| [`.bot formation`](party-actions.md#bot-formation) | Direct Movement | Sets party follow formation geometry. |
| [`.bot cc`](party-actions.md#bot-cc) | Tactical Action | Assigns crowd control marks (accepted form of `.bot action cc`). |
| [`.bot strategy`](party-actions.md#bot-strategy) | Direct Strategy | Adjusts strategies by selected bot, party role or bot name. |
| [`.bot loot`](party-actions.md#bot-loot) | Direct Party | Toggles corpse looting on targeted bot or party. |
| [`.bot repair`](party-actions.md#bot-repair) | Direct Party | Orders targeted bot or party to repair gear. |
| [`.bot sell`](party-actions.md#bot-sell) | Direct Party | Orders targeted bot or party to sell gray junk. |
| [`.bot rest`](party-actions.md#bot-rest) | Direct Party | Orders bots to sit and consume food and drink. |
| [`.bot downtime`](party-actions.md#bot-downtime) | Direct Party | Orders own alt bots to run town errands (trainer, junk, repair). |
| [`.bot commander bind`](commander.md#bot-commander-bind) | Remote Commander | Binds a bot-only party to remote leadership (with `inparty`: you and up to three other players stay in the party). |
| [`.bot commander unbind`](commander.md#bot-commander-unbind) | Remote Commander | Releases your current remote commander binding. |
| [`.bot commander status`](commander.md#bot-commander-status) | Remote Commander | Displays commanded party status and roster. |
| [`.bot commander view`](commander.md#bot-commander-view) | Remote Commander | Toggles live perception data feed frames. |
| [`.bot cmd attack`](commander.md#bot-cmd-attack) | Remote Commander | Orders commanded party to attack a target by GUID. |
| [`.bot cmd pull`](commander.md#bot-cmd-pull) | Remote Commander | Executes tank pull order to lead bot anchor. |
| [`.bot cmd pullback`](commander.md#bot-cmd-pullback) | Remote Commander | Executes ranged pullback to lead bot anchor. |
| [`.bot cmd cc`](commander.md#bot-cmd-cc) | Remote Commander | Assigns crowd control on target GUID. |
| [`.bot cmd focus`](commander.md#bot-cmd-focus) | Remote Commander | Marks Skull and focuses party damage on GUID. |
| [`.bot cmd move`](commander.md#bot-cmd-move) | Remote Commander | Walks lead bot and party to 3D world coordinates. |
| [`.bot cmd stay`](commander.md#bot-cmd-stay) | Remote Commander | Commands all bots in the bound party to hold ground. |
| [`.bot cmd follow`](commander.md#bot-cmd-follow) | Remote Commander | Restores party follow formation on the lead bot. |
| [`.bot cmd rest`](commander.md#bot-cmd-rest) | Remote Commander | Commands bound party bots to consume food/water. |
| [`.bot cmd revive`](commander.md#bot-cmd-revive) | Remote Commander | Releases spirit of a dead bound bot. |
| [`.bot cmd lootmethod`](commander.md#bot-cmd-lootmethod) | Remote Commander | Configures party loot rules and threshold. |
| [`.bot cmd summon`](commander.md#bot-cmd-summon) | Remote Commander | Teleports a bound bot to the lead bot position. |
| [`.bot cmd enter`](commander.md#bot-cmd-enter) | Remote Commander | Walks party into a dungeon entrance trigger. |
| [`.bot cmd loot`](commander.md#bot-cmd-loot) | Remote Commander | Orders a bot to loot a corpse or container. |
| [`.bot cmd open`](commander.md#bot-cmd-open) | Remote Commander | Interacts with or unlocks a world game object. |
| [`.bot cmd use`](commander.md#bot-cmd-use) | Remote Commander | Uses an item from a commanded bot's inventory. |
| [`.bot cmd ritual`](commander.md#bot-cmd-ritual) | Remote Commander | Coordinates multiple bots to activate ritual objects. |
| [`.bot cmd state`](commander.md#bot-cmd-state) | Remote Commander | Queries current state of a world game object. |
| [`.bot run`](../dungeon-run-leader.md#run-leader) | Remote Commander | Starts, pauses and stops a dungeon run of the bound party. |
| [`.bot trace`](../../server/run-trace.md#start-stop-status) | Remote Commander | Records a party run as a trace file plus a summary. |
| [`.bot roster`](roster-and-hiring.md#bot-roster) | Roster & Hiring | Emits snapshot of owned alts and CC assignments. |
| [`.bot add`](roster-and-hiring.md#bot-add) | Roster & Hiring | Logs in an owned alt character as a bot. |
| [`.bot remove`](roster-and-hiring.md#bot-remove) | Roster & Hiring | Logs out an owned bot or dismisses a hired companion. |
| [`.bot logout`](roster-and-hiring.md#bot-remove) | Roster & Hiring | Safely logs out an owned bot without dismissing it (alias of `.bot remove` for owned alts). |
| [`.bot hire`](roster-and-hiring.md#bot-hire) | Roster & Hiring | Recruits a mercenary companion at recruiters. |
| [`.bot hire raid`](roster-and-hiring.md#bot-hire-raid) | Roster & Hiring | Recruits up to 39 companions in one command. |
| [`.bot role`](roster-and-hiring.md#bot-role) | Roster & Hiring | Configures party role overrides for bots or self. |
| [`.bot invite`](roster-and-hiring.md#bot-invite) | Roster & Hiring | Invites an online owned bot to your party. |
| [`.bot uninvite`](roster-and-hiring.md#bot-uninvite) | Roster & Hiring | Removes an owned bot or dismisses a hire from group. |
| [`.bot summon`](roster-and-hiring.md#bot-summon) | Roster & Hiring | Teleports an owned bot or hire to your location. |
| [`.bot maintenance`](roster-and-hiring.md#bot-maintenance) | Roster & Hiring | Refreshes kit items, reagents, ammo, and repairs. |
| [`.bot autogear`](roster-and-hiring.md#bot-autogear) | Roster & Hiring | Equips level-appropriate gear up to quality caps. |
| [`.autogear`](roster-and-hiring.md#autogear) | Roster & Hiring | Gears a real player like a spawned bot (GM, or self with `PlayerAutogearForAll`). |
| [`.bot release`](roster-and-hiring.md#bot-release) | Roster & Hiring | Commands dead bots to release spirit to graveyard. |
| [`.bot corpse run`](roster-and-hiring.md#bot-corpse-run) | Roster & Hiring | Commands ghost bots to run back to their corpse. |
| [`.bot corpserun`](roster-and-hiring.md#bot-corpse-run) | Roster & Hiring | One-word form of `.bot corpse run`. |
| [`.bot learn`](roster-and-hiring.md#bot-learn) | Roster & Hiring | Commands bots to train spells at nearby trainers. |
| [`.bot trade`](roster-and-hiring.md#bot-trade) | Roster & Hiring | Opens a trade window with the targeted companion. |
| [`.bot list`](bot-panel.md#bot-list) | Inspection | Lists online bots you control. |
| [`.bot status`](bot-panel.md#bot-status) | Inspection | Displays lifecycle and AI state of a controlled bot. |
| [`.bot stats`](bot-panel.md#bot-stats) | Inspection | Summarizes controlled bot fleet statistics. |
| [`.bot lease`](bot-panel.md#bot-lease) | Inspection | Reports active autonomous activity leases. |
| [`.bot version`](bot-panel.md#bot-version) | Inspection | Displays module version, protocol line, and credits. |
| [`.bot features`](bot-panel.md#bot-features) | Inspection | Lists the server's player-facing switches (the addon's Info window). |
| [`.bot help`](bot-panel.md#bot-help) | Inspection | Prints bot system version header. |
| [`.bot inv`](bot-panel.md#bot-inv) | Bot Panel | Emits inventory, equipped gear, and money details. |
| [`.bot lootlog`](addon.md#protocol-grammar--semantics) | Bot Panel | Replays the last day of loot rows for controllable party bots. |
| [`.bot item <Name> trade`](bot-panel.md#bot-item-trade) | Bot Panel | Opens direct trade window with a bot. |
| [`.bot item <Name> equip`](bot-panel.md#bot-item-equip) | Bot Panel | Equips an item from the bot's bags into gear slots. |
| [`.bot item <Name> unequip`](bot-panel.md#bot-item-unequip) | Bot Panel | Moves an equipped item into inventory bags. |
| [`.bot item <Name> give`](bot-panel.md#bot-item-give) | Bot Panel | Offers a bag item in an open trade window. |
| [`.bot behavior`](bot-panel.md#bot-behavior) | Bot Panel | Toggles persistent behaviors (loot, AoE, mimic...). |
| [`.bot totem`](bot-panel.md#bot-totem) | Bot Panel | Configures and drops shaman totem loadouts. |
| [`/w <BotName> trainer`](whispers.md#w--trainer) | Whisper Command | Trains all available class spells at open trainer. |
| [`/w <BotName> talents`](whispers.md#w--talents) | Whisper Command | Reports spent talent points and allocation tree. |
| [`/w <BotName> rebuff`](whispers.md#w--rebuff) | Whisper Command | Prompts bot to refresh party buffs out of combat. |
| [`/w <BotName> end pull`](whispers.md#w--end-pull) | Whisper Command | Aborts active pull and returns tank to follow. |
| [`/tbm`](addon.md) | Addon | Opens or toggles the TortoiseBotsManager GUI. |
| [`.bot pool status`](../../server/admin-commands.md#bot-pool-status) | Admin & GM | Displays managed bot pool health and reset status. |
| [`.bot pool accounts`](../../server/admin-commands.md#bot-pool-accounts) | Admin & GM | Lists managed bot accounts (console only). |
| [`.bot pool adopt preview`](../../server/admin-commands.md#bot-pool-adopt-preview) | Admin & GM | Previews unmanaged bot accounts for adoption (console). |
| [`.bot pool adopt confirm`](../../server/admin-commands.md#bot-pool-adopt-confirm) | Admin & GM | Enrolls unmanaged bot accounts into pool (console). |
| [`.bot ah status`](../../server/admin-commands.md#bot-ah-status) | Admin & GM | Reports synthetic auction house telemetry and counts. |
| [`.bot ah reload`](../../server/admin-commands.md#bot-ah-reload) | Admin & GM | Reloads auction house item pricing rules from DB. |
| [`.bot ah rebuild`](../../server/admin-commands.md#bot-ah-rebuild) | Admin & GM | Forces evaluation and refreshing of market listings. |
| [`.bot ah item`](../../server/admin-commands.md#bot-ah-item) | Admin & GM | Inspects, resets, or overrides item auction prices. |
| [`.ahbot`](../../server/admin-commands.md#ahbot) | Admin & GM | Shorthand alias for `.bot ah` commands. |
| [`.reload config`](../../server/admin-commands.md#reload-config) | Admin & GM | Reloads core configuration files. |
