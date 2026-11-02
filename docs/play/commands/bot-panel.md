---
id: play-commands-bot-panel
title: Bot Panel & Inspection Commands
category: play
summary: Commands for inspecting bot state, managing gear and inventory, and toggling runtime behaviors.
tags: [commands, inspection, bot-panel, gear]
relates_to:
  - play-commands
  - play-commands-addon
---

# Bot Panel & Inspection Commands

TortoiseBots includes a structured command interface for inspecting companion vitals, managing equipped gear and inventory bags, and adjusting runtime behavior toggles. These commands power the in-game bot panel in [TortoiseBotsManager](addon.md#tortoisebotsmanager-addon-tbm) and can also be executed directly via chat.

## Summary

| Command | Parameters / Syntax | What It Does |
| :--- | :--- | :--- |
| [`.bot list`](#bot-list) | None | Lists all online bots you control. |
| [`.bot status`](#bot-status) | `<Name>` | Displays detailed lifecycle and movement state for a bot. |
| [`.bot stats`](#bot-stats) | None | Summarizes online counts for your controllable bots. |
| [`.bot trace`](#bot-trace) | `start <run_id> [party\|bot <Name>] [driver=...]` / `stop [<run_id>]` / `status` | Opt-in run recorder (`AiPlayerbot.RunTrace.Enabled`). |
| [`.bot lease`](#bot-lease) | `[status]` | Reports autonomous activity leases and active timers. |
| [`.bot version`](#bot-version) | None | Displays module version, protocol string, and author credits. |
| [`.bot features`](#bot-features) | None | Lists the server's player-facing switches and their values. |
| [`.bot help`](#bot-help) | None | Prints the current bot system version header. |
| [`.bot inv`](#bot-inv) | `<Name>` | Emits detailed inventory, equipped gear, and coin count. |
| [`.bot item <Name> trade`](#bot-item-trade) | None | Prompts the bot to open a direct trade window with you. |
| [`.bot item <Name> equip`](#bot-item-equip) | `<bag> <slot>` | Equips an item from the bot's bags into gear slots. |
| [`.bot item <Name> unequip`](#bot-item-unequip) | `<bag> <slot>` | Moves an equipped item into the bot's inventory bags. |
| [`.bot item <Name> give`](#bot-item-give) | `<bag> <slot>` | Places an inventory item into an active trade window. |
| [`.bot behavior`](#bot-behavior) | `<Name\|scope> <key> <on\|off>` | Sets persistent bot behaviors (loot, AoE, mimic...). |
| [`.bot totem`](#bot-totem) | `<scope> <slot\|mode\|drop> [...]` | Configures and drops shaman totem loadouts. |

---

## Diagnostic and Inspection Commands

### `.bot list`

Lists all online bots you control (owned alts, claimed bots, or bots you master).

```text
.bot list
```

Displays each bot's character name, online lifecycle state (`starting` or `in world`), random-pool flag, and whether its AI brain is currently attached.

### `.bot status`

Displays detailed runtime diagnostic status for an individual bot you control.

```text
.bot status <CharacterName>
```

Shows the bot's current lifecycle stage, AI attachment, active movement strategy (`follow`, `stay`, `guard`, `free`, `custom`), and whether you or another player masters it.

### `.bot stats`

Summarizes metrics for the bots you control.

```text
.bot stats
```

Outputs the total count of online controllable bots, how many are random-pool bots, and how many currently have an active AI controller attached.

### `.bot trace`

Opt-in recorder for tests and tuning: it writes what a party's bots did during one run to a JSONL file, and `tools/trace_score.py` scores it offline. Off by default: needs `AiPlayerbot.RunTrace.Enabled = 1`, otherwise `.bot trace start` answers `TBM:ACTION_ERR|trace start|disabled|…`. Schema, files and oracles: [run trace](../../server/run-trace.md).

| Command | What It Does |
| :--- | :--- |
| `.bot trace start <run_id> [party\|bot <Name>] [driver=<model>:<effort>:<run>]` | Starts a run. `party` (default) records the bots of your party (a bound [remote commander](commander.md) records the group it commands); `bot <Name>` records one bot you control. The run id (1-64 letters, digits, `.`, `_`, `-`) is stamped on every record, and names the files; an id that already has a trace is refused (`refused`). With `AiPlayerbot.RunTrace.GmOnly = 1` (the default) only a GameMaster may start a run (`forbidden`); with `0` a GameMaster may trace any party and everyone else only a party whose bots they control or command (`forbidden`). `driver=<model>:<effort>:<run>` (anywhere after the run id, once) says what drove the run, for example `driver=sonnet:medium:zf-ab-1-s2`; it goes into the summary as `driver`, self-reported and never verified (1-96 letters, digits and `. _ - : +`; otherwise `invalid`). Left out, the summary says `human` for a run nobody commands and `unknown` for a commanded or run-leader run. One run at a time. ACK: `TBM:ACTION_ACK|trace start|party\|bot:<Name>|<bots>|<run_id>`. |
| `.bot trace stop [<run_id>]` | Ends the run (with a run id, only that run's trace: another run's id answers `other-run` and nothing is stopped); the end record and the summary are written a moment later (a `start` in that moment is refused: `finishing`). Only a GameMaster or the player who started it (`forbidden`); `not-running` without one. ACK: `TBM:ACTION_ACK|trace stop|stopped|1|<run_id>`. |
| `.bot trace status` | Prints the running run's id, scope, elapsed time, record counts, bytes written and lines dropped; the id and scope only to a GameMaster and the player who started it (anyone else sees that a run is going, with an empty run id). ACK: `TBM:ACTION_ACK|trace status|running\|finishing\|idle|<0\|1>|<run_id>`. |

The permission is re-checked every frame, so bots of someone else who join your party are not recorded. A run ends on its own when the size cap is reached, when the party has had no bots you may record for 10 s (also when you log out), when the feature or the bot AI is switched off or at server shutdown; the summary says why (`end_reason`).

### `.bot lease`

Reports autonomous activity leases across the bot population.

```text
.bot lease [status]
```

Breaks down active bot activities across categories: `Idle`, `Grinding`, `Trading`, `LftQueued`, `BgQueued`, and `PlayerMaster`. Lists remaining durations for active leases.

### `.bot version`

Displays module version details, addon protocol handshake line, and author credits.

```text
.bot version
```

Prints `TortoiseBots <BuildVersion>`, the `TBM:VERSION|<BuildVersion>` protocol line, and the project attribution line. The same credit line is sent to every real (networked game client) player on login and written to the server startup log. Aliases include `.bot v`, `.bot about`, and `.bot credits`. Available to all players.

### `.bot features`

Lists the server's player-facing switches: dungeon runs, player-led runs, the remote commander, boss tactics, bot LFG groups, hiring and its limits, world buffs, autogear for every player, alt bots, the auction house market, the world bot count and the run trace.

```text
.bot features
```

Prints one `Server features: ...` line and a `TBM:FEATURE|<key>|<value>|<label>` line per switch (`on`, `off` or a number). The addon's Info window reads them. Available to all players.

`.bot features all <text>` searches every setting in the server's bot config file whose name holds `<text>` (at least two letters, any case) and lists it with its value: a `Config: N keys match` line, then `TBM:CONFIG_BEGIN|<matches>`, one `TBM:CONFIG|<key>|<value>` per setting (the `AiPlayerbot.` prefix left out; at most 200) and `TBM:CONFIG_END|<shown>|<matches>`. Values of API keys, passwords, tokens and secrets read `***`. Players use it to tell a server owner what their server has set.

### `.bot help`

Prints the module version header to chat.

```text
.bot help
```

Alias: `.bot h`.

---

## Bot Panel Commands

Bot panel commands work on any online bot you control (owned alts and hired mercenary companions). They emit structured `TBM:` protocol lines that drive the graphical addon interface.

### `.bot inv`

Generates a snapshot of a bot's equipped gear, carried bag items, free bag slots, and carried money.

```text
.bot inv <CharacterName>
```

Alias: `.bot inventory <CharacterName>`.

<details>
<summary>Protocol format</summary>

Emits structured protocol records:
- `TBM:INV_BEGIN|<bot>|<copper>`: Starts the inventory stream with the bot's carried coin.
- `TBM:INV_EQ|<slot>|<itemId>|<durability>|<max>`: One record per equipped gear slot.
- `TBM:INV_ITEM|<bag>|<slot>|<itemId>|<count>|<flags>`: One record per carried bag item. Flags:
  - `e`: The bot can equip the item.
  - `eu`: The item is an equip upgrade (the `u` flag only appears together with `e`, never alone).
  - `b`: The item cannot be traded to you right now.
  - `-`: None / standard item.
- `TBM:INV_END|<bot>|<freeSlots>|<totalSlots>`: Concludes stream with bag capacity.

</details>

### `.bot item <Name> trade` <a id="bot-item-trade"></a>

Commands the bot to initiate a direct trade session with you.

```text
.bot item <CharacterName> trade
```

Place items or gold into the trade window and click accept; the companion bot accepts the trade automatically on its own. Responds with `TBM:ACTION_ACK|item trade|bot:<Name>|1|open`.

### `.bot item <Name> equip` <a id="bot-item-equip"></a>

Instructs the bot to equip an item from its inventory bags into the appropriate equipment slot.

```text
.bot item <CharacterName> equip <bag> <slot>
```

Specify the bag index and slot number as reported by `.bot inv`. On success (and when the item is no longer there after the operation), a refreshed inventory snapshot follows.

### `.bot item <Name> unequip` <a id="bot-item-unequip"></a>

Instructs the bot to remove an equipped item and place it into its inventory bags.

```text
.bot item <CharacterName> unequip <bag> <slot>
```

Equipped items use bag index `255` and slot indices `0` through `18`. Fails with `failed` if the bot's inventory bags are full. On success, a refreshed inventory snapshot follows.

### `.bot item <Name> give` <a id="bot-item-give"></a>

Places an item from the bot's inventory into an active trade window with the player.

```text
.bot item <CharacterName> give <bag> <slot>
```

If no trade window is currently open, the bot opens one and returns a `pending` status; repeat the command once the window shows (the addon does this automatically). Items the core would refuse in trade are refused upfront (`no-trade`) because the core would cancel the entire trade.

<details>
<summary>Action Protocol Replies</summary>

Replies use the structured action protocol:
- `TBM:ACTION_ACK|item <op>|bot:<Name>|1|<itemId>[ pending]` (trade ACKs detail `"open"`)
- `TBM:ACTION_ACK|behavior <key>|bot:<Name>|1|<on|off>`
- `TBM:ACTION_ACK|behavior <key>|party|<N>|<on|off>`
- `TBM:ACTION_ERR|<intent>|<code>|<message>`

The ACK scope is `party` or `bot:<Name>`, the detail never contains `|`, and a fresh `TBM:BOTSTATE` line (or lines for party scope) accompanies successful behavior changes.

</details>

### `.bot behavior`

Configures persistent behavioral toggles for an individual bot or the entire group.

```text
.bot behavior <CharacterName|party|raid> <key> <on|off>
```

Alias: `.bot behaviour`. Settings are persisted per bot across sessions.

<details>
<summary>Available behavior keys</summary>

- `loot`: Autonomous corpse looting (on by default).
- `aoe`: Multi-target and area-of-effect abilities (on by default).
- `autocc`: Smart crowd control on loose hostile adds (off by default).
- `savemana`: Mana conservation that holds back expensive casts.
- `boost`: Offensive cooldown usage (for example warrior *Death Wish* or mage *Arcane Power*).
- `threat`: Threat back-off that eases off when the bot nears the tank's threat.
- `potions`: Automatic consumption of healing and mana potions through the reaction engine.
- `spread`: Opt-in tactical spacing to avoid stacking in combat (not the default raid survival spread).
- `mimic`: Mimics consumable usage out of combat (food, drink, elixirs, flasks, and scrolls). The bot resolves a class-, spec-, and level-appropriate equivalent and casts it virtually (no inventory item needed). When more than 4 bots mimic at once, individual chat confirmations are suppressed in favor of a single `[Raid Mimic]` summary. Supports party-wide toggles: `.bot behavior party mimic on`.

</details>

### `.bot totem`

Configures and manages shaman totem loadouts and casting policies.

```text
.bot totem <bot|party|raid> <slot> <totem name|auto|none>
.bot totem <bot|party|raid> mode <auto|prepull|manual>
.bot totem <bot|party|raid> drop
.bot totem <bot|party|raid> status
```

<details>
<summary>Totem parameters and modes</summary>

- **Slot Pinning:** Assign a specific totem to an element slot (`earth`, `fire`, `water`, `air`). Passing `auto` restores automatic selection (subgroup-aware for earth, water, and air; falls back to spec-row behavior for fire). Passing `none` stores an empty order identical to unset, so the slot falls back to the automatic pick (auto behavior). An order chooses *which* totem the slot places; *when* is still the drop mode, so use `drop` to place an ordered set immediately.
- **Casting Modes:**
  - `auto`: Bot drops totems in combat and in the pre-pull window (ready check / rebuff) only — never otherwise out of combat, so a following bot leaves no totems in town (default).
  - `prepull` (or `pre-pull`): The same rule as `auto`, kept as an explicit choice: drops totems out of combat when combat is imminent and refreshes them in combat.
  - `manual`: Suppresses automatic drops; totems are dropped only via `.bot totem drop` or explicit strategies.
- **Immediate Drop:** `.bot totem <scope> drop` queues the configured totems now, in or out of combat.
- **Recall and Modes:** *Totemic Recall* refunds a standing set once the bot and its group are out of combat. Because the automatic drops are quiet out of combat (they only fire in combat or the pre-pull window), the recalled set stays recalled until the next fight — the refund waits only while the pre-pull window is open or *Mana Tide Totem* is down.
- **Status Query:** `.bot totem <bot|party|raid> status` reports current loadout and mode via `TBM:TOTEM` records followed by a trailing `TBM:ACTION_ACK|totem status|<scope>|<n>|-`.

</details>
