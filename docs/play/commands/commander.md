---
id: play-commands-commander
title: Remote Commander Commands
category: play
summary: Remote commander mode and intent commands for directing bot-only parties from anywhere.
tags: [commands, commander, remote-control]
relates_to:
  - play-commands
  - play-commands-party-actions
  - play-commands-addon
  - server-settings-performance
---

# Remote Commander Commands

The remote commander leads a group of up to 5 bots from anywhere. Remote binds and the full order set need `AiPlayerbot.RemoteCommander = 1`. The `inparty` bind and `cmd enter` / `summon` in your own party need only `AiPlayerbot.DungeonSelfClear = 1` (default 1, on). You get tactical orders, dungeon opening, and a live feed.

## Commander Rules and Architecture

- **Binding authority:** a GameMaster, or anyone who controls every bot (owner account, master, claim). Even GameMasters cannot bind parties with real players (except `inparty`), raid groups, or another player's hires.
- **Party leadership:** the party is led by the designated lead bot. Followers follow the lead bot.
- **Suppressed behaviors:** bound bots lose `grind`, `travel`, `travel once`, `tfish`, `rpg` (including craft, player, guild, jump), and `explore`. `wander` is not stripped. Alt-bots are never moved by `.bot cmd` (order them via whisper or party chat).
- **Hires:** your own hires can bind within the 5-bot group. Unbinding returns them to your party if you are online, otherwise they wait for your login.
- **Instance preservation:** when the party is kept (bot-only, all bots named) the instance bind is kept. A rebuilt group has no instance bind until `cmd enter`.
- **Bound-state rules:** threat backoff yields on mobs hitting healer or damage bots and is never stored. The hires' share-a-group requirement is suspended while bound. A hirer's logout gives normal logout grace plus regrouping. Server restarts end active hires; disbanding ends the binding and returns hires; a restart drops the binding while stored subgroups, main tank, and assistants stay as the core left them.

## Summary

| Command | Permission | What It Does |
| :--- | :--- | :--- |
| [`.bot commander bind`](#bot-commander-bind) | Control All / GM | Binds a bot-only group to your remote control. |
| [`.bot commander unbind`](#bot-commander-unbind) | Bound Commander | Releases your remote control over the bound party. |
| [`.bot commander status`](#bot-commander-status) | Bound Commander | Displays members (dead bots marked) and lead bot. |
| [`.bot commander view`](#bot-commander-view) | Bound Commander + `RemoteCommander = 1` | Toggles real-time perception feed stream. |
| [`.bot cmd attack`](#bot-cmd-attack) | Bound Commander of a remote binding | Orders party bots to attack a target GUID. |
| [`.bot cmd pull`](#bot-cmd-pull) | Bound Commander of a remote binding | Directs the tank to pull a target to the lead bot. |
| [`.bot cmd pullback`](#bot-cmd-pullback) | Bound Commander of a remote binding | Tank shoots target and retreats to lead bot anchor. |
| [`.bot cmd cc`](#bot-cmd-cc) | Bound Commander of a remote binding | Assigns crowd control on a target GUID. |
| [`.bot cmd focus`](#bot-cmd-focus) | Bound Commander of a remote binding | Marks Skull and commands focus damage on a GUID. |
| [`.bot cmd move`](#bot-cmd-move) | Bound Commander of a remote binding | Navigates the lead bot and party to 3D coordinates. |
| [`.bot cmd stay`](#bot-cmd-stay) | Bound Commander of a remote binding | Halts all bots in the commanded party in place. |
| [`.bot cmd follow`](#bot-cmd-follow) | Bound Commander of a remote binding | Resumes follower movement behind the lead bot. |
| [`.bot cmd rest`](#bot-cmd-rest) | Bound Commander of a remote binding | Orders all bots in the commanded group to rest. |
| [`.bot cmd revive`](#bot-cmd-revive) | Owner or GM | Releases a dead bot's spirit to run to corpse. |
| [`.bot cmd lootmethod`](#bot-cmd-lootmethod) | Bound Commander of a remote binding | Sets the loot method and threshold for the party. |
| [`.bot cmd summon`](#bot-cmd-summon) | Owner or GM | Teleports a bound bot directly to the lead bot. |
| [`.bot cmd enter`](#bot-cmd-enter) | Owner or GM | Moves party through a dungeon entrance trigger. |
| [`.bot cmd loot`](#bot-cmd-loot) | Owner or GM | Orders a bot to loot a corpse or container. |
| [`.bot cmd open`](#bot-cmd-open) | Owner or GM | Commands a bot to interact with or unlock an object. |
| [`.bot cmd use`](#bot-cmd-use) | Owner or GM | Commands a bot to use an item from its bags. |
| [`.bot cmd ritual`](#bot-cmd-ritual) | Owner or GM | Directs 2-5 bots to activate summoning rituals. |
| [`.bot cmd gossip`](#bot-cmd-gossip) | Owner or GM | Has a bot pick a gossip option of an allowlisted NPC. |
| [`.bot cmd state`](#bot-cmd-state) | Bound Commander of a remote binding | Queries the current state of a world game object. |
| [`.bot run`](#run-leader-bot-run) | Bound Commander | Opt-in run leader: the bound party clears a stored dungeon plan by itself. |

In your own party with `RemoteCommander = 0`, only `enter` and `summon` work. The rest of the tactical set comes from the run leader.

---

## Commander Management Commands

### `.bot commander bind`

Binds a bot-only party under your remote leadership.

```text
.bot commander bind <LeadBot> [<Bot> ...] [inparty]
.bot commander bind <LeadBot> raid
```

- The named lead bot becomes party leader. Extra named bots are invited.
- A groupless lead bot needs at least one named follower (refused `no-members` without one).
- An existing bot-only group is adopted as-is.
- **Kept-party rule:** if the lead bot and named bots are already in your party, the party is kept: the lead bot takes leadership and you leave the group, keeping the instance bind.
- **In-party rule:** with `inparty` as the last word you stay in your own party: 1-4 players plus bots, 5 in all, and the lead bot is always a bot. The lead bot must be in your party (`not-in-party`) and you must lead that party (`not-leader`). Named bots outside the party join (`party-full` when there is no room). You never leave.
- **Hires:** only your own hires can bind.
- **Raid bind:** `.bot commander bind <LeadBot> raid` binds your own raid of 6-40 bots (the usage text still says 39). It needs `AiPlayerbot.DungeonRunRaid = 1` on top of `AiPlayerbot.DungeonSelfClear = 1`. The commander is taken out of the raid. It applies the raid layout (subgroups, main tank, assistants, tank-healer focus targets) and release restores the ones it changed. With `AiPlayerbot.RaidHealerSeatsByClass = 1` the healer seats go by class (group healers on the raid, then the main tanks, the off-tanks whoever is left) and a tank healer whose targets are all nearly full helps the raid. A layout that cannot be planned only warns (`TBM:RAID_LAYOUT|warning`) and never blocks the bind.
- **Hiring into a raid binding:** while a raid is bound, hiring joins the new companion to the lead bot's raid (up to 40 bots in all); see [`.bot hire raid`](roster-and-hiring.md#bot-hire-raid).

- **Inparty gates:** a plain member cannot take the lead (refused before anything changes); every bot must be named and controlled.
- **Leave handling:** the commander leaving (left, kicked, logged out, lost connection) ends the binding at once, with the lead going first to an online player. An offline commander counts as gone even while holding a group slot. A bound group is found by id or through a remaining bot or the commander. Unbinding with the commander in the party leaves the bots in place and returns the lead to him.
- **Standing rules:** a bound bot never hands the lead to, nor takes, a real player as master. Taking the lead, raid conversion, or the commander leaving still ends it; classic binding no longer ends on the commander's return.
- **Run-bound:** another player's whispers or actions are refused, while party chat keeps chatting.
- **Raid bind:** hiring into a raid places only the new bot in the layout, nobody else moves, following the lead bot.
<details>
<summary>Refusal codes</summary>

- `disabled`, `already-bound`, `no-bot`, `group-has-player`, `group-raid`, `group-too-large`, `group-bound`, `no-members`, `member-grouped`, `bot-bound`, `party-unnamed`, `lead-bound`, `not-in-party`, `not-leader`, `party-full`, `hired`, `denied`, `invite-failed`, `inparty-only` (remote binds need `AiPlayerbot.RemoteCommander = 1`; with it off only an `inparty` bind of your own party works).
- Raid: `raid-off`, `raid-inparty`, `raid-not-raid`, `raid-lead-not-tank`, `raid-too-small` (below 6 bots), `group-has-player` when another real player is in the raid.
- `raid-layout` is a warning only, not a refusal.

</details>

### `.bot commander unbind`

Ends your active remote commander session.

```text
.bot commander unbind
```

Releases the binding. Followers restore their standard follow targets. Hired companions rejoin your party if you are online.

### `.bot commander status`

Prints a summary of the currently bound party in chat.

```text
.bot commander status
```

Lists each member's name (dead bots marked with `(dead)`) and sends the status ACK line with the count and lead name.

### `.bot commander view`

Enables or disables the live perception feed.

```text
.bot commander view on|off
```

- A new binding starts with the view off.
- Needs `AiPlayerbot.RemoteCommander = 1` (refused `disabled` without it).
- Update frequency follows `RemoteCommanderViewMs`.

---

## Tactical Remote Orders (`.bot cmd`)

Tactical orders use the prefix `.bot cmd`. Targets are 64-bit GUIDs (hex or decimal). Object entry IDs (`entry:<id>`) are accepted by `loot`, `open`, `use`, `ritual`, and `state`, plus `entry:<id>@x,y,z` for the nearest within 30 yards. The per-bot orders `loot`, `open`, `use`, and `gossip` (plus `revive`, `summon`, `enter`) stay owner-or-GM and are checked against the bot each time.

### `.bot cmd attack`

Orders bots in the commanded party to attack the target GUID.

```text
.bot cmd attack <guid> [bot]
```

- Naming a bot sends only that bot. Without a name the party goes.
- Healers keep healing unless named.
- ACK detail names held targets: `tank-stays:<name>` and/or `held:<A>[,<B>...]`, joined by `;`.
- Held attacks hold up to 20 s.

### `.bot cmd pull`

Directs the group tank to pull the target GUID, while other bots wait at the lead bot's coordinates.

```text
.bot cmd pull <guid> [seconds] [ranged]
```

- Delay defaults to 10 seconds, range 0-60.
- Non-tanks hold, then engage after the delay.
- The `seconds` and `ranged` words are accepted in either order.

### `.bot cmd pullback`

Directs the tank to pull the target from range and immediately retreat to the lead bot's position.

```text
.bot cmd pullback <guid> [seconds] [ranged]
```

- Delay defaults to 5 seconds.
- The tank returns to the lead anchor. Non-tanks engage after the return plus the delay.
- The `seconds` and `ranged` words are accepted in either order.

### `.bot cmd cc`

Assigns a crowd control mark and target to a capable party bot.

```text
.bot cmd cc <mark> <guid> [bot] [family]
```

- The family (`polymorph`, `seduction`, `fear`, and similar) pins the row the run leader's auto-CC adds.
- A hand order without a family leaves the bot's usual choice.

```text
.bot cmd cc clear <bot> [<mark> <guid>]
```

- Takes a bot off CC: its `rti cc` goes to `none`.
- With a mark and GUID, that icon is cleared too, but only while it is still on that unit.

- The run leader sends `cc clear` for a Sap not landed or a Banish never landed.

### `.bot cmd focus`

Applies the Skull raid mark to the specified target GUID and commands focus damage.

```text
.bot cmd focus <guid>
```

Except on a `tank_hold` target, where tanks keep their own pick.

### `.bot cmd move`

Directs the lead bot to pathfind to specific 3D coordinates; followers follow the lead bot.

```text
.bot cmd move <x> <y> <z> [direct]
```

- Must be within `ReactDistance` (default 150 yards).
- Fails with `too-far`, `unreachable`, or `off-mesh` (more than 5 yards off the navmesh).
- After the move the lead holds and the others follow it.
- Coordinates accept spaces, commas, or semicolons. Bots return to the stay spot after combat.
- A direct pit-floor point stays on the lead's floor, not a pit floor below a ramp.
- Length-capped paths cap at 30 yards with a 5-yard/no-progress incomplete rule; the far-end test is player-orders-only. The player's own close (3-yard) gather is checked like any other.
- An open-water point keeps its height, while a pier or dock floor is still used.

<details>
<summary>Direct movement mode</summary>

Adding `direct` walks a straight line instead of navmesh paths:

- Horizontal within 25 yards, vertical within 8 yards (no more than 3 yards below ground, no more than 0.5 yards above ground), with line of sight.
- Followers gather first. All pass or nothing moves.
- A follower with no way to the spot walks its own straight line when within 25 yards horizontally, at most 1.5 yards above or below the lead, and it sees the point.
- A follower with none of these refuses the whole step with `too-far`. The run leader summons that bot (at most 3 per step, 30 seconds or more apart or after death) and retries.

</details>

<details>
<summary>Path and floor checks</summary>

- `unreachable` means no path, or an incomplete path going nowhere.
- Floor at or under the point is preferred, up to 4 yards below first, then up to 6 yards above.
- The run leader's own gather within 3 yards is always accepted.

</details>

### `.bot cmd stay`

Commands all bots in the party, including the lead bot, to hold their current positions.

```text
.bot cmd stay
```

### `.bot cmd follow`

Orders followers to resume follow formation behind the lead bot. The lead bot keeps its current position (even if mid-movement).

```text
.bot cmd follow
```

### `.bot cmd rest`

Orders all bots in the commanded party to sit, eat food, and drink water until fully restored.

```text
.bot cmd rest
```

### `.bot cmd revive`

Releases the spirit of a dead companion bot so it can run back to its corpse and resurrect.

```text
.bot cmd revive <bot>
.bot cmd revive party
```

- Requires owner or GameMaster.
- Live bots are refused (`not-dead`; with `party`: none dead).
- Dead bots are released, run back, and are revived by their own dead strategy.
- With `party`, every dead bot you control is released at once and one ACK carries the count.
- A headless bot's release completes at once instead of waiting for the graveyard repop: inside a dungeon it comes back at the entrance, and no later resurrect leaves a pending repop.

### `.bot cmd lootmethod`

Configures the loot distribution method and threshold for the commanded party.

```text
.bot cmd lootmethod <ffa|group|rr|master|nbg> [threshold]
```

- `master` means the lead bot is master looter.
- Threshold is kept when omitted. Aliases: `common` / `white`, `uncommon` / `green`, `rare` / `blue`, `epic` / `purple`, `legendary` / `orange`. `0` is rejected.
- The ACK reflects the method now in force (`failed` if unchanged).

### `.bot cmd summon`

Teleports a named bound bot directly to the lead bot's current coordinates.

```text
.bot cmd summon <bot>
```

- Requires owner or GameMaster.
- Takes 3 seconds through a portal, out of combat only.
- Refused when the bot is dead, in combat, on a taxi, teleporting, or busy, when the lead is unavailable, or when the bot is the lead.
- Rebinds the master and restores follow on arrival.

### `.bot cmd enter`

Moves the commanded party through a dungeon entrance trigger.

```text
.bot cmd enter <area trigger id|near> [bot]
```

- Requires owner or GameMaster.
- `near` picks the nearest entrance within `ReactDistance` (lowest ID breaks ties).
- Bots must be on the trigger map and within `ReactDistance`.
- Checks instance requirements and lockouts (`requirement`, `instance-limit`; requirements apply to GM commanders as well since only a GM bot would skip; a raid map needs the bots in a raid group, which a bound raid satisfies).
- The lead enters first on a 30 s budget, then followers on 30 s, with the tally in the reply.
- A bot that fails only the entrance's own condition (Onyxia's Lair asks for the Drakefire Amulet) goes in when the entrance leads into a raid, its trigger runs no area-trigger script, and the commander is a real player who commands the raid (a member of it, or the commander of the bots' binding, whom a raid binding keeps outside the group) and meets the entrance's own condition.

---

## Object and Inventory Orders

These commands require owner or GameMaster permissions (except `.bot cmd state`, which is available to any bound commander).

### Mechanics: The Walk and The Act

- **The Walk:** the assigned bot walks the navmesh to within 1.5 yards of the target, bounded by `ReactDistance`. Time budget applies.
- **The Act:** the bot stands still and performs the interaction. Combat interrupts a walking act. An in-place use continues if already started.
- Followers follow the actor when it is the lead; stance and previous orders are restored after.
- The stay anchor updates on arrival; close targets keep a 1.5-yard backoff.

### `.bot cmd loot`

Directs a bot to walk to a corpse or game object container and loot its contents.

```text
.bot cmd loot <guid|entry:<id>[@x,y,z]> <bot> [itemId]
```

- Without `itemId`, loots everything group rights allow.
- With `itemId`, loots only that item.
- Corpse preference applies. With `itemId` only that item is taken and money is ignored.
- Prechecks locks; fails with `locked` if locked.

### `.bot cmd open`

Commands a bot to walk to a game object (chest, door, lever, altar) and interact with it.

```text
.bot cmd open <guid|entry:<id>[@x,y,z]> <bot>
```

- Chests use the Opening spell (5-second cast) and answer with contents.
- Anything else answers `opened` once the object changes state.
- An object whose lock has no key or skill row the bot lacks is used with plain use, else refused.
- Plain use is allowlisted to open, quick-open, and slow-open locks (such as the Uldaman Temple Doors); every other lock type is refused.

### `.bot cmd use`

Commands a bot to use an item from its inventory bags, optionally targeting a unit or object.

```text
.bot cmd use <itemId> <bot> [target-guid|entry:<id>[@x,y,z]]
```

### `.bot cmd ritual`

Coordinates 2-5 bots to interact simultaneously with a summoning ritual object.

```text
.bot cmd ritual <guid|entry:<id>[@x,y,z]> <bot1> <bot2> [<bot3>...]
```

- Needs the object's required participant count (`not-enough` if short of it).
- The first named bot initiates the channel, and the rest assist.

### `.bot cmd gossip`

Has one bot talk to an allowlisted NPC and pick one gossip option, the way a player does.

```text
.bot cmd gossip <npc entry> <bot> <option number 1-15 | "option text">
```

- Only NPCs listed in `AiPlayerbot.BotGossipNpcs` (empty by default, so the order is off).
- The bot walks to the nearest living NPC of that entry within 40 yards, follows it if it walks, talks, and picks one option. For example: `.bot cmd gossip 1234 <bot> 1`.
- An unlisted NPC is refused with `not-allowed` before the walk.
- Run-step exception: a running or paused run's bot may talk to its current gossip step's NPC.
- Option 1 is the top entry and quest entries are not counted. Matching goes exact, then caseless, then substring.
- Database-service, vendor, flight, trainer, money, tabard-icon, and typed-code options are refused.
- Failures: `no-npc` (a dead one nearby names its respawn), `no-option` (lists the menu), `ambiguous`, `npc-busy`, `in-combat`; walking can end `busy`, `unreachable`, `timeout`, or `unavailable`. One menu per order.

### `.bot cmd state`

Queries the current animation and interaction state of a world game object. Available to any bound commander.

```text
.bot cmd state <guid|entry:<id>[@x,y,z]>
```

Reports three fields:

- `state:<active|ready|alternative>`
- `loot:<not-ready|ready|activated|just-deactivated>`
- `flags:<none|in-use|locked|interact-cond|no-interact>`

---

## Run Leader (`.bot run`)

Needs `AiPlayerbot.DungeonSelfClear = 1` (default on), otherwise every `.bot run` answers with `disabled`. A bound party starts a dungeon plan for its party and the bots clear it by themselves: they walk the route, pull packs, kill bosses, and do object steps through the same orders as `.bot cmd`. Plans, step kinds, and safety rules: [Dungeon Run Leader](../dungeon-run-leader.md).

| Command | What it does |
| :--- | :--- |
| `.bot run start [plan] [from [leg\|step] <name\|number>] [until boss\|step N\|leg <name\|N>]` | Starts the plan for your bound party (without a name: the one plan of the lead bot's map). The lead bot must be on the plan's map (`.bot cmd enter` first). ERR `not-bound`, `no-plan`, `wrong-map`, `busy`. **Partway:** `from 4` or `from leg <name>` starts at that leg, `from step 57` at step 57. Only for a party that already stands in the instance: the lead bot must be on an instance map (`not-in-instance`) and every commanded bot in its instance (`party-scattered`; `.bot cmd summon` first). **Event fallback:** starting inside an event goes back to its ritual/open/use_item step; earlier steps are neither done nor checked. Start ERR codes: `no-leg`, `no-step`, `invalid`. **Until:** `until boss|step 29|leg <name>` stops before the first boss step at/after the start (boss or hold-with-plan-boss), goes back to the event start, and trash walk stops at the boss stand; the done note names the clause; works with `from`/`speed`, refused with a player. ERR `no-boss`/`no-leg`/`no-step`/`until-at-start`. |
| `.bot run start [plan] [from ...] player [tank <Bot>]` | **Player-led run:** you lead your own party (not a raid) and walk; the run leader pulls the plan's packs near you with the tank bot. You stay its leader and the bots keep following you. The named tank (else the first tank-role bot) is the run's lead bot. Every bot must be yours to control; up to three other players may be in the party. ERR `disabled, not-grouped, raid, not-leader, already-bound, no-bots, no-tank, tank-not-in-party, tank-not-bot, hired, not-controllable, not-live, bot-bound, wrong-map`. Stopping, finishing, or switching off releases the binding. |
| `.bot run system bind [<Bot>...] \| cmd <order> \| start [plan] [from ...] \| pause \| resume \| skip \| stop \| status \| unbind` | **System run** (GameMaster only): the server's own run of a bot-only party of world bots. Needs `AiPlayerbot.DungeonSelfClear = 1` and `AiPlayerbot.BotLfgGroups = 1`. Status lines go to the server log. `stop` keeps the binding and `unbind` ends both. `player` and `speed=` are refused. World bots are online bots that are not owned, hired, claimed, an alt, or any player's companion. `cmd` sends one order while no run is going. With a real player in the group, `stop`/`status`/`pause`/`unbind` still work while `cmd`/`resume` wait. A failed bind rolls back within limits. |
| `.bot run lfg list \| form [plan] [alliance\|horde] \| fill <id> \| disband <id>` | **Bot LFG groups** (GameMaster, in game or console). Needs `AiPlayerbot.DungeonSelfClear = 1`, `AiPlayerbot.BotLfgGroups = 1`, and `AiPlayerbot.BotLfgMaxGroups` for the form cap. `list` fields: id, dungeon, faction, state, seconds, open role, leader, members, posts, invite holder. `form` ignores cadence, count, and online-rule but keeps `BotLfgMaxGroups`, one group per dungeon and faction, five free bots. `fill` retries every 5 s and says when no free bot fits. `disband` lets player-held bots go. |
| `.bot run pause` | Pauses the run; the bots hold what they do. |
| `.bot run resume` | Goes on with a paused run: the party regroups first, then the paused step starts over. A pause for the lead bot off the plan's map, and a short player-hold pause, go on by themselves once the cause is gone. A short player-hold pause auto-resumes after 5 minutes, and a fresh pause within 5 s goes on by itself; a player's own pause never auto-resumes. A lead 300 yards away in another part keeps `lead-other-part`. A pause for a lead bot in another part of the instance is not resumed while the lead bot is still there: ERR code `lead-other-part`, message names where the lead bot was revived, how far it is from the run, and asks to bring it to the party first. The run stays paused. |
| `.bot run skip` | Leaves out the current step (its pull target and pull counts too) and goes on running. An item the step failed to get is noted missing, and later steps that need it are left out with a note. Works from a pause too, after resume's regroup. |
| `.bot run stop [reason]` | Ends the run; the bots stay where they are, still bound. The status note reads `stopped by .bot run stop`, or `stopped: <reason>` when a reason is given (at most 80 printable characters). |
| `.bot run status` | Sends the run's status line again. |
| `.bot run for <Commander> <start [plan] [from <leg\|step N>] [until boss\|step N\|leg <name\|N>] [speed=<x>]\|pause\|resume\|skip\|stop\|status>` | **GameMaster only** (in game, or console / SOAP with such an account; works with the run leader off): drives another commander's run as that commander. The gate runs first (non-GM gets ERR `forbidden`), then the argument parse (`invalid` with the usage), then the commander lookup (offline gets `no-commander`). `start` never carries the player-led word. |
| `.bot run mode` | Says whether the run is player-led (and its tank) or bot-led, or that no run is going. |
| `.bot run list` | The loaded plans: name, map, steps, and how many steps are not supported yet. |
| `.bot run here` | The plan whose route passes nearest you on your map. Answers `TBM:RUN_HERE` with the plan and distance. The addon picks the plan with it. One plan per wing (SM/DM/Mara/Strat); `-|0` when there is none. |
| `.bot run reloadplans` | **GameMaster only** (account security `SEC_GAMEMASTER`, which is `SEC_DEVELOPER` = **3** on this core; in game, console, or SOAP; works with the run leader off). Reads the plan tables again and swaps them in only when every row passes. A refused row keeps the old set (ERR `refused` with the reason) and a non-GM gets ERR `forbidden`. A run in progress keeps the plan it started with. A gone plan, a failed query, or a changed table keeps the old set too. The boot line reports the reload with per-plan lines and rebuilt boss spawns. |

**Speed:** `start ... speed=<1.0-3.0>` is a GameMaster bot-only test speed with a speed label. It runs faster only between fights (1.0 in fight, pull, rest, events, and near packs); the done note starts with it and it never counts as a clear. ERR `forbidden`/`real-player`.

**Time limit:** a run ends by itself `AiPlayerbot.DungeonRunMaxMinutes` minutes (default 180, `0` = none) after `run start`, a pause counting. It lands in the done state with a note; the bots stay bound.

**Any player order pauses the run:** every `.bot cmd` order except `state`, `summon`, and `revive`, every `.bot action`, and every chat command a real player gives a bot of the party sets it to `paused` until `.bot run resume`. Unbinding ends it. The pause names the order. With players in the group, the commander's orders pause but party chat does not, and another player's orders are refused. Normal play never pauses, and a player-led run never pauses on the leading player's orders.

**Status line** (to the commander, whenever the step, phase, or note changes, and on `start`, `status`, `pause`, `resume`, `skip`, `stop`): seven fields naming plan, state, step count, leg, kind, phase, and note. A run that left packs alive adds an eighth field `left K`. A test run above speed 1.0 adds a trailing speed label.

A pull over its ceiling notes the over-ceiling estimate. A pull's line also names the joiners it drew in.
- The note also carries the ring estimate; `left K` appears only when packs were left.
- The speed label notes a real player, and the until label drops after a stop. State and phase enums plus event hold/fight counts are included.

## Target by Entry and Timing

Objects can be targeted by entry ID: `entry:<id>` (nearest within `ReactDistance`, default 150 yards) or `entry:<id>@<x>,<y>,<z>` (nearest within 30 yards of coordinates). Format: decimal integer at most 16777215, coordinates comma- or semicolon-separated without spaces. Emits a preceding target line (except `state`, which answers at once).

### Asynchronous Execution Protocol

Commands that require walking or casting emit asynchronous lifecycle records:

1. Pending with the budget in seconds and the target (or `party` for rituals, and party or bot plus trigger ID for enters). Budgets: walk is 10 seconds plus distance over 4 yards per second (capped at 60 seconds); loot 8 seconds; open 4 seconds (8 seconds for chests); use 15 seconds.
2. ACK on success with count and detail.
3. ERR on failure with code and message.
- Immediate actions (like `state`) answer at once and skip PENDING.

The object-task error catalog is a subset: `no-bot`, `denied`, `too-far`, `no-item`, `locked`, `busy`, `in-combat`, `invalid-target`, `not-found`, `unavailable`, `cancelled`, `timeout`, `invalid`, `failed`, `dead-target`, `friendly`, `no-target`, `unreachable`, `off-mesh`, `no-los`, `no-ground`, `not-enough`, `interrupted`, `requirement`, `instance-limit`, `not-bound`, `lead-unavailable`, `no-ai`. The bind, gossip, and run sections above list their own extra codes.

---

## Perception Feed (`TBM:VIEW`)

When enabled via `.bot commander view on`, the server transmits periodic perception frames (version `v1`) about once per second.

```text
TBM:VIEW|v1|<seq>|B|<lead>,<party>,<hostiles>,<threats>,<truncated>
TBM:VIEW|v1|<seq>|P|<party record>[;<party record>...]
TBM:VIEW|v1|<seq>|H|<hostile record>[;<hostile record>...]
TBM:VIEW|v1|<seq>|T|<threat record>[;<threat record>...]
TBM:VIEW|v1|<seq>|E|<lines>
```

- **B (Begin):** lead bot GUID, counts of party / hostile / threat records, and truncation flag.
- **P (Party):** guid, name, health, power, position, map, target, spell, dead flag, state, action, result, reason.
- **H (Hostile):** guid, entry, position, health percent, target, spell, flags, reaction, kind. Capped at 24 units.
- **T (Threat):** hostile GUID plus top three threat holders per enemy with percent.
- **E (End):** total count of lines in the frame.
- Lines cap at 250 bytes, 8 per tick, sequence from 1, built from cached memory.
- Watching works from anywhere with the view on.

### Example Frame

```text
TBM:VIEW|v1|12|B|0x2A,1,1,1,0
TBM:VIEW|v1|12|P|0x2A,Tanky,4000,5000,35,100,1,-8913.2,554.5,93.1,1.50,0,0,0xF130000C5E0012AB,0,0,c,heroic strike,O,-
TBM:VIEW|v1|12|H|0xF130000C5E0012AB,3874,-8900.0,560.0,94.0,64,0x2A,0,-,h,-
TBM:VIEW|v1|12|T|0xF130000C5E0012AB,0x2A:100,0x2B:41
TBM:VIEW|v1|12|E|5
```

---

## `.bot scout` (GM, test tooling)

**GameMaster only, with GM mode on** (`.gm on`); anyone else gets `TBM:ACTION_ERR|scout|gm-only|…`. A test tool for checking a dungeon plan's points on a live server before a walk: it checks the run leader's own move rules from any start, walks the GM as the bots' mover walks, and reads the GM's server position. It hires, binds, moves and orders no bot, and the only world change is the GM's own position for `walk`. Never part of a normal play session; the driver that uses it runs on test servers only.

`#<id>` is a request id of 1 to 9 digits, echoed on the reply (digits only, without the `#`), so the driver can match answers that come back out of order.

```text
.bot scout cfg #<id>
.bot scout path #<id> <x> <y> <z> [from <fx> <fy> <fz>]
.bot scout walk #<id> <x> <y> <z>
.bot scout where #<id>
```

| Form | Does | Answers |
| :--- | :--- | :--- |
| `cfg` | Reads the live aggro numbers. | `TBM:SCOUT_CFG\|<id>\|<reactDistance>\|<rateCreatureAggro>\|<familyAssistRadius>` (`%.2f` each) |
| `path` | The run leader's move check — the same floor snap and `CheckMove` as `.bot cmd move` — from `from` (default: the GM) to the point, on the GM's map. Nobody moves. | `TBM:SCOUT_PATH\|<id>\|<ok\|too-far\|unreachable\|off-mesh>\|0x<path type>\|<length>\|<ex> <ey> <ez>\|<endOffset>\|<snapped z>\|<startSwim>\|<endSwim>` |
| `walk` | Starts one server-moved walk of the GM to the point, with the bots' own mover call (`MOVE_RUN_MODE \| MOVE_PATHFINDING`, the one the lead bot's stay walk ends with). | `TBM:SCOUT_WALK\|<id>\|started\|0x<path type>` |
| `where` | The GM's server position and movement state. | `TBM:SCOUT_POS\|<id>\|<x> <y> <z>\|<moving>\|<inWater>\|<swimming>` |

The path type is the path finder's raw bits: `0x81` is the core's straight two-point swim line (`0x80` underwater with a normal path), which a move across water takes when both ends are swimmable — the plan walk then never walks the mesh. `startSwim`/`endSwim` say whether the origin and the snapped point are swimmable (core terrain, not the navmesh). A `wrong-map` error means the point or the start is outside the map's coordinate bounds: the command names no map and always paths on the caller's (the scout's GM stands in the plan's dungeon). The parser also refuses any coordinate past `|17066|` (`invalid`, the usage line), so `path` and `walk` never reach the path finder or the mover with a point outside the core's bound.
