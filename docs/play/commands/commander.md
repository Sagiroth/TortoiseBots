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

> Work in progress. These features are not officially released and not supported yet. Enable them at your own risk. Until the official release, bug reports and feedback about them are not handled through the issue tracker; contact Hypeer directly.

The Remote Commander interface allows a player or GameMaster to lead a group of up to five bots from anywhere in the game world. Remote binds and the full order set need [`AiPlayerbot.RemoteCommander`](../../server/settings/performance.md#aiplayerbotremotecommander) (`1`); the `inparty` bind and `cmd enter`/`summon` in your own party need only [`AiPlayerbot.DungeonSelfClear`](../../server/settings/dungeons-raids.md#aiplayerbotdungeonselfclear) (`1`, on by default). You can issue tactical orders, navigate dungeons, open objects, and view a live perception feed.

## Commander Rules and Architecture

- **Binding Authority:** A GM, or anyone who controls every bot in the group (owner account, master, or claim). Even GMs cannot bind parties containing real players (unless they bind with `inparty`, below), raid groups, or another player's hires.
- **Party Leadership:** The group is led by a designated lead bot. Follower bots follow the lead bot and maintain threat backoff behind the tank (yielding on mobs hitting healer or damage bots; threat backoff is never stored with the bot).
- **Suppressed Behaviors:** Bound bots have open-world behaviors stripped: `grind`, `travel`, `travel once`, `tfish`, `rpg` (including craft, player, guild, and jump), and `explore`. Autonomous `wander` is explicitly not stripped. Autonomous alt-bots are never moved by `.bot cmd` (they are stripped from tactical orders; order them via whisper or party chat instead).
- **Hired Companions:** You can bind up to 5 of your own hired mercenary companions. While bound, the requirement that hires share a group with their hirer is suspended. Unbinding returns the hires to your group if you are online. Disbanding ends the binding and returns hires. Hirer logout initiates normal logout grace and regrouping. Server restarts end active hires.
- **Instance Preservation:** Instance progress is preserved when your party is kept (bot-only, all bots named); a rebuilt group starts with no instance bind until `cmd enter`.

## Summary

| Command | Permission | What It Does |
| :--- | :--- | :--- |
| [`.bot commander bind`](#bot-commander-bind) | Control All / GM | Binds a bot-only group to your remote control. |
| [`.bot commander unbind`](#bot-commander-unbind) | Bound Commander | Releases your remote control over the bound party. |
| [`.bot commander status`](#bot-commander-status) | Bound Commander | Displays members (dead bots marked) and lead bot. |
| [`.bot commander view`](#bot-commander-view) | Bound Commander | Toggles real-time perception feed stream. |
| [`.bot cmd attack`](#bot-cmd-attack) | Bound Commander | Orders party bots to attack a target GUID. |
| [`.bot cmd pull`](#bot-cmd-pull) | Bound Commander | Directs the tank to pull a target to the lead bot. |
| [`.bot cmd pullback`](#bot-cmd-pullback) | Bound Commander | Tank shoots target and retreats to lead bot anchor. |
| [`.bot cmd cc`](#bot-cmd-cc) | Bound Commander | Assigns crowd control on a target GUID. |
| [`.bot cmd focus`](#bot-cmd-focus) | Bound Commander | Marks Skull and commands all bots to focus GUID. |
| [`.bot cmd move`](#bot-cmd-move) | Bound Commander | Navigates the lead bot and party to 3D coordinates. |
| [`.bot cmd stay`](#bot-cmd-stay) | Bound Commander | Halts all bots in the commanded party in place. |
| [`.bot cmd follow`](#bot-cmd-follow) | Bound Commander | Resumes follower movement behind the lead bot. |
| [`.bot cmd rest`](#bot-cmd-rest) | Bound Commander | Orders all bots in the commanded group to rest. |
| [`.bot cmd revive`](#bot-cmd-revive) | Owner or GM | Releases a dead bot's spirit to run to corpse. |
| [`.bot cmd lootmethod`](#bot-cmd-lootmethod) | Bound Commander | Sets the loot method and threshold for the party. |
| [`.bot cmd summon`](#bot-cmd-summon) | Owner or GM | Teleports a bound bot directly to the lead bot. |
| [`.bot cmd enter`](#bot-cmd-enter) | Owner or GM | Moves party through a dungeon entrance trigger. |
| [`.bot cmd loot`](#bot-cmd-loot) | Owner or GM | Orders a bot to loot a corpse or container. |
| [`.bot cmd open`](#bot-cmd-open) | Owner or GM | Commands a bot to interact with or unlock an object. |
| [`.bot cmd use`](#bot-cmd-use) | Owner or GM | Commands a bot to use an item from its bags. |
| [`.bot cmd ritual`](#bot-cmd-ritual) | Owner or GM | Directs 2-5 bots to activate summoning rituals. |
| [`.bot cmd gossip`](#bot-cmd-gossip) | Owner or GM | Has a bot pick a gossip option of an allowlisted NPC (`AiPlayerbot.BotGossipNpcs`). |
| [`.bot cmd state`](#bot-cmd-state) | Bound Commander | Queries the current state of a world game object. |
| [`.bot run`](#run-leader-bot-run) | Bound Commander | Opt-in run leader: the bound party clears a stored dungeon plan by itself (`AiPlayerbot.DungeonSelfClear`). |

---

## Commander Management Commands

### `.bot commander bind`

Binds a bot-only party under your remote leadership.

```text
.bot commander bind <LeadBot> [<Bot> ...] [inparty]
```

The specified lead bot becomes party leader. If you specify additional bots, they are invited to the group. A groupless lead bot requires at least one named follower (refused with `no-members` if none given). An existing bot-only group is adopted as-is.

<details>
<summary>How binding works and refusal codes</summary>

- **Kept-Party Rule:** If the lead bot and named bots are already in your party, the party is kept intact: the lead bot takes leadership, you leave the group, and dungeon instances remain preserved.
- **Real players in the party (`inparty`, 2026-10-23):** `.bot commander bind <LeadBot> [<Bot> ...] inparty` kept the commander in the party with any other real players of it: 1 to 4 players and the rest bots, five in all, the lead bot always a bot. The word counted only as the last argument. The lead bot had to be in the commander's party (else `not-in-party`) and the commander had to lead that party (else `not-leader`, refused before anything changed: a plain member could not take the lead through a bind); the lead bot became the party's leader, the named bots outside joined (`party-full` when there was no room), every bot of the party had to be named and controlled by the commander as before, and the commander never left. A bot of a bound group no longer handed the lead to a real player of it (which would have ended the binding) and never took a real player of it as its master. The binding stood with players in it; a player taking the lead, a raid conversion or the commander leaving still ended it: when he left the party (left, was kicked, logged out or lost his connection) it ended at once and the binding's bots left that party, the lead going first to an online player of it; a commander who was offline when the binding ended counted as gone even while his character still held a group slot; when the lead bot logged out or was removed, the remaining bots left the players' party the same way (the bound group was found by its id, or through a remaining bot or the commander) (his hires went back to him or waited for his login, as on any release), so no other player became their master; when it ended with him still in the party (unbind) the bots stayed and the lead went back to him. From the bind on, another player's whispers and `.bot action` to the bound bots were refused (`run-bound`) and his party chat was not taken as a bot command. A classic binding (without the word) kept the old rules, except that its own commander coming back into the group no longer ended it.
- **Hired Companions:** You can bind up to 5 of your own hired companions. Hires belonging to other players cannot be bound.
- **Refusal Codes:** `disabled`, `already-bound`, `no-bot`, `group-has-player`, `group-raid`, `group-too-large`, `group-bound`, `no-members`, `member-grouped`, `bot-bound`, `party-unnamed`, `lead-bound`, `not-in-party`, `not-leader`, `party-full`, `hired`, `denied`, `invite-failed`, `inparty-only` (remote binds need `AiPlayerbot.RemoteCommander = 1`; with it off only an `inparty` bind of your own party works).
</details>

### `.bot commander unbind`

Ends your active remote commander session.

```text
.bot commander unbind
```

Releases the binding. Followers restore their standard follow targets. Hired companions automatically rejoin your party if you are online.

### `.bot commander status`

Prints a summary of the currently bound party in chat.

```text
.bot commander status
```

Lists each member's name (dead bots marked with `(dead)`) and sends `TBM:ACTION_ACK|commander status|party|<count>|<leadName>`.

### `.bot commander view`

Enables or disables the live perception feed.

```text
.bot commander view on|off
```

When enabled, the server streams structured perception frames (`TBM:VIEW`) detailing party vitals, nearby hostiles, and active threat tables. A new binding starts with the view off by default. Update frequency is governed by [`AiPlayerbot.RemoteCommanderViewMs`](../../server/settings/performance.md#aiplayerbotremotecommanderviewms).

---

## Tactical Remote Orders (`.bot cmd`)

Tactical orders use the prefix `.bot cmd`. Tactical targets are identified strictly by their 64-bit object GUID (in hex `0x...` or decimal). Object entry IDs (`entry:<id>`) are accepted by `loot`, `open`, `use`, `ritual`, and `state`. The per-bot player orders `loot`, `open`, `use` and `gossip` (and `revive`, `summon`, `enter`) stay owner-or-GM and are checked against the bot each time.

### `.bot cmd attack`

Orders bots in the commanded party to attack the target GUID.

```text
.bot cmd attack <guid> [bot]
```

Specifying a bot name directs only that bot to engage. Healers keep healing unless explicitly named in the command. If a tank is holding another mob, ACK returns `tank-stays:<name>` or `held:<name>` (held attacks can hold up to 20 seconds).

### `.bot cmd pull`

Directs the group tank to pull the target GUID, while other bots wait at the lead bot's coordinates.

```text
.bot cmd pull <guid> [seconds] [ranged]
```

Accepts an optional delay in seconds (defaults to 10 seconds, range 0 to 60) and an optional `ranged` flag (accepted in either order). Pull maintains target hold and non-tanks engage after the delay expires.

### `.bot cmd pullback`

Directs the tank to pull the target from range and immediately retreat to the lead bot's position.

```text
.bot cmd pullback <guid> [seconds] [ranged]
```

Defaults to a 5-second pull delay (accepted in either order with `ranged`). Non-tank bots hold at the lead anchor and engage only after the tank returns to the anchor spot and the delay expires.

### `.bot cmd cc`

Assigns a crowd control mark and target to a capable party bot.

```text
.bot cmd cc <mark> <guid> [bot]
```

Places the specified raid target icon on the target GUID and assigns the crowd control duty to a bot.

```text
.bot cmd cc clear <bot> [<mark> <guid>]
```

Takes a bot off its crowd control mark: its `rti cc` goes to `none`, so none of its crowd control rows fires on the mark. With a mark and a GUID, that raid icon is also cleared, but only while it is still on that unit. The run leader sends it for a Sap that did not land and for a Banish that never landed ([Dungeon Run Leader](../dungeon-run-leader.md)).

### `.bot cmd focus`

Applies the Skull raid mark to the specified target GUID and commands all party bots to focus damage.

```text
.bot cmd focus <guid>
```

### `.bot cmd move`

Directs the lead bot to pathfind to specific 3D coordinates; followers follow the lead bot.

```text
.bot cmd move <x> <y> <z> [direct]
```

Coordinates must be within `AiPlayerbot.ReactDistance` (default 150 yards) of the lead bot on walkable ground. Coordinates accept spaces, commas, or semicolons as separators (`x y z`, `x,y,z`, `x;y;z`, or with spaces). Fails with `too-far`, `unreachable`, or `off-mesh` (>5 yards off navmesh). After combat ends, bots return to this stay spot.

<details>
<summary>Direct movement mode</summary>

Adding the `direct` flag (e.g. `.bot cmd move <x> <y> <z> direct`) commands the party to walk a straight line instead of using navigation mesh paths (useful for unlinked ramps or ledges). Preconditions: horizontal distance within 25 yards, vertical within 8 yards (no more than 3 yards below or 0.5 yards above ground), and clear line of sight. Followers gather first, and nothing moves unless the lead bot and every follower pass all checks.

A follower with no way to the lead bot's spot (no complete path, and not near it on its floor in its sight) walks **its own straight line** to the point when it stands within 25 yd of the point horizontally, at most 1.5 yd above or below the lead bot (on its floor, not on a pit floor below the ramp) and sees the point (the case of a follower still on the line of the direct step before, over the navmesh hole). A follower that has none of these refuses the whole step with `too-far`, naming it: gather the party on the lead bot first (the run leader summons that bot, once per step, and sends the step again).

</details>

<details>
<summary>Path and floor checks</summary>

`unreachable` covers no path and an incomplete path that goes nowhere: for a point within 30 yd the path finder cannot be cut short by its length cap, so an incomplete path that ends more than 5 yd from the point, or where the lead bot already stands, means the point is on another part of the mesh (the far-end test is for a player's order only: the run leader's own hops and gathers accept an incomplete path, only a path that goes nowhere is refused for them too); Ragefire leg 4, 2026-10-17: a point 6 yd away on a ledge was acked three times and the lead bot never moved.

The point is first put on the map floor at its x,y: the floor at or up to 4 yd below the point comes first, and only when there is none there does a floor up to 6 yd above it count (a point a little under the floor), so a low ceiling over the point (Uldaman's tunnel north of the Sentinel's hall, 5 yd up) is never taken for the floor. The run leader's own gather on the lead bot's spot (within 3 yd of it) is always accepted; a player's order to a point that close is checked as any other. A point in open water (water or ocean) with no floor below keeps its height when that floor above is above the water surface and the lead bot has no usable path onto it; the reply then says `Move point kept in the water: x y z (surface …, the floor above it at … has no usable path: …)`. A floor above that the lead bot can reach (a pier, a dock) is still used.

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

Orders all bots in the commanded party to sit, eat food, and drink water until fully restored. Bots that are already full accept the order cleanly.

```text
.bot cmd rest
```

### `.bot cmd revive`

Releases the spirit of a dead companion bot so it can run back to its corpse and resurrect.

```text
.bot cmd revive <bot>
.bot cmd revive party
```

Requires Owner or GM permissions. With `party`, every dead bot of the party you control is released at once, and one ACK carries the count. Live bots are refused (`not-dead`; with `party`: none dead). Dead bots are released, run back to their corpse, and are revived by their own dead strategy.

### `.bot cmd lootmethod`

Configures the loot distribution method and threshold for the commanded party.

```text
.bot cmd lootmethod <ffa|group|rr|master|nbg> [threshold]
```

Supported methods: `ffa`, `group`, `rr`, `master` (lead bot is master looter), `nbg`. Threshold is kept when omitted; aliases include `common`/`white`, `uncommon`/`green`, `rare`/`blue`, `epic`/`purple`, `legendary`/`orange` (`0` is rejected). The acknowledgment reflects the method now in force (`failed` if unchanged).

### `.bot cmd summon`

Teleports a named bound bot directly to the lead bot's current coordinates.

```text
.bot cmd summon <bot>
```

Requires Owner or GM permissions. Takes 3 seconds, allowed out of combat only. Rebinds master and restores follow movement upon arrival.

### `.bot cmd enter`

Moves the commanded party through a dungeon entrance trigger.

```text
.bot cmd enter <area trigger id|near> [bot]
```

Requires Owner or GM permissions. Passing `near` picks the nearest entrance trigger within `ReactDistance` (lowest ID breaks ties). Bots must be on the trigger map and within `ReactDistance`. Checks instance requirements and lockouts (`requirement`, `instance-limit`; requirements apply to GM commanders as well since only a GM bot would skip; raid maps need a raid group, which a bound party cannot satisfy). Lead bot enters first (30-second budget), followed by followers (30-second budget). Replies with `(n of m got in)`.

---

## Object and Inventory Orders

These commands require Owner or GM permissions (except `.bot cmd state`, which is available to any bound commander).

### Mechanics: The Walk and The Act

- **The Walk:** The assigned bot walks the navmesh to within 1.5 yards of the target object (bounded by `ReactDistance`). Followers follow the actor bot if it is the lead bot. Stance and previous orders are restored upon completion.
- **The Act:** The bot stands still, its stay anchor updates, active spells drop, and it performs the interaction. Engaging in combat interrupts the act; in-place `use` continues if already started.

### `.bot cmd loot`

Directs a bot to walk to a corpse or game object container and loot its contents.

```text
.bot cmd loot <guid|entry:<id>[@x,y,z]> <bot> [itemId]
```

Corpse preference is applied when selecting targets. Without `itemId`, loots all items and money; with `itemId`, loots only that item (money is ignored). Prechecks locks; fails with `locked` if locked.

### `.bot cmd open`

Commands a bot to walk to a game object (chest, door, lever, altar) and interact with it.

```text
.bot cmd open <guid|entry:<id>[@x,y,z]> <bot>
```

Opening chests uses the bot's Opening spell (lockpicking or key, 5-second cast). A chest answers with its contents like `loot`; anything else answers `opened` once the object changes state. A door whose lock is only of the types a player opens with nothing (open, quick open, slow open: Uldaman's Temple Doors 124368 and 141869, lock 86, quick open) is opened with a plain use, as a player's click opens it; every other lock type (a key, lockpicking or another skill, blasting, Gahz'ridian, traps, the rest) is still refused; before, the bot looked for a spell of its own for that lock, found none and answered `failed` ("could not use it").

### `.bot cmd use`

Commands a bot to use an item from its inventory bags, optionally targeting a unit or object.

```text
.bot cmd use <itemId> <bot> [target-guid|entry:<id>[@x,y,z]]
```

### `.bot cmd ritual`

Coordinates between 2 and 5 bots to interact simultaneously with a summoning ritual object.

```text
.bot cmd ritual <guid|entry:<id>[@x,y,z]> <bot1> <bot2> [<bot3>...]
```

Checks required participant count vs named bots (`not-enough` if insufficient). The first named bot initiates the channel, and subsequent bots assist.

### `.bot cmd gossip`

Has one bot talk to an allowlisted NPC and pick one gossip option, the way a player does (issue #1012).

```text
.bot cmd gossip <npc entry> <bot> <option number | "option text">
```

<details>
<summary>How it works in detail</summary>

**Only NPCs listed in `AiPlayerbot.BotGossipNpcs`** (empty by default, so the order is off; see [`AiPlayerbot.BotGossipNpcs`](../../server/settings/dungeons-raids.md#aiplayerbotbotgossipnpcs) for the audited entries). The bot walks to the nearest living NPC of that creature entry within 40 yd (following it if it walks), talks to it and picks one gossip option, the way a player does: its session gets the client's own gossip packets, so the NPC's script reacts exactly as for a player (Zul'Farrak, with `7607,7604` listed: `.bot cmd gossip 7607 <bot> 1` has Weegli Blastfuse blow the End Door after the pyramid event; `.bot cmd gossip 7604 <bot> 1` starts the Sergeant Bly fight). An unlisted NPC is refused with `not-allowed` before the walk and before anything is sent to it (since fix/plan-gossip-allow, one exception: a bot of a running or paused dungeon run was let talk to the NPC its run's current gossip step names, read from the loaded plan), because a gossip option can do anything its script says (take money, give items, teleport). The option is a number (1 = the top of the menu, quest entries not counted) or a text: the exact text first, then without case, then a part of it without case. Even on a listed NPC, database service options (vendor, trainer, flight, bank, inn and the like), options with a vendor, flight, trainer, money or tabard icon, and options that ask for a typed code are refused. One menu only: an option that opens a further menu is picked, and the order ends there. ACK: `TBM:ACTION_ACK|cmd gossip|bot:<Name>|1|<option text>` (after a `TBM:ACTION_TARGET` with the NPC's GUID and a `TBM:ACTION_PENDING` while it walks). Failures: `not-allowed`, `no-npc` (none within 40 yd, or it died; when one of the entry lies dead within 40 yd the message goes on with `a dead one lies nearby: <name> (<entry>) lies dead <n> yd away and respawns in <time> (<delay> s spawn timer).`, or `(it has no respawn timer)` for a delay of 0), `no-option` (no such option, the menu is empty, or it is a service option; the message lists the menu), `ambiguous` (the text fits more than one option), `npc-busy` (the NPC will not talk now: its script, hostile, not selectable), `in-combat` (the bot or the NPC), and the walk's `busy`, `unreachable`, `timeout`, `unavailable`.

</details>

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

Needs `AiPlayerbot.DungeonSelfClear = 1` (default on), otherwise every `.bot run` answers `TBM:ACTION_ERR|run <verb>|disabled|…`. A bound party (`.bot commander bind`, `inparty` or system) starts a **dungeon plan** for its party and the bots clear it by themselves: they walk the plan's route, pull its packs, kill its bosses and do its object steps, through the same orders as `.bot cmd` (each one shows as its usual ACK/ERR line). The commander may watch from anywhere (with `.bot commander view on` behind `AiPlayerbot.RemoteCommander = 1`, the feed shows what the bots see). Plans, the step kinds and the safety rules: [Dungeon Run Leader](../dungeon-run-leader.md).

| Command | What it does |
| :--- | :--- |
| `.bot run start [plan] [from [leg\|step] <name\|number>]` | Starts the plan for your bound party (without a name: the one plan of the lead bot's map). The lead bot must be on the plan's map (`.bot cmd enter` first). ERR `not-bound`, `no-plan`, `wrong-map`, `busy` (a run is going). **Partway:** `from 4` or `from leg trogg_west` starts at the first step of that leg (a leg's number or its name from the plan), `from step 57` at step 57 (the number the status line shows, from 1), so a retry needs no run of `skip`. It is only for a party that already stands in the instance: the lead bot must be on an instance map (`not-in-instance`) and every commanded bot in its instance (`party-scattered`; `.bot cmd summon` first). A start that falls inside a scripted event (on its hold, or on the moves leading into it) is moved back to the ritual, open or use_item step that starts the event, so `from 11` in Uldaman starts at the Keepers' ritual. The reply says where the run starts and that the steps before it are neither done nor checked (doors not opened, items not fetched). ERR `no-leg`, `no-step` for a leg or step the plan does not have; `invalid` for words that do not parse. `speed=<1.0-3.0>` (a GameMaster's bot-only TEST run, 2026-10-25): the bots and their pets walk that many times as fast between fights (1.0 in a fight, a pull, a rest, an event and near a pack in sight); the status line ends `\|speed 2.0 (test)` and the done note starts with it, so it is never counted as a clear. ERR `forbidden` (not a GameMaster), `real-player` (a player in the party, or `player`). |
| `.bot run start [plan] [from ...] player [tank <Bot>]` | **Player-led run** (2026-10-24, behind `AiPlayerbot.DungeonRunPlayerLed = 1` on top of the key above): you lead your own party (not a raid) and walk; the run leader pulls the plan's packs near you with the tank bot. The start makes its own binding of your party: you stay its leader, the bots keep following you, the named tank (else the first tank-role bot) is the run's lead bot. Every bot must be yours to control; up to three other players may be in the party. ERR `disabled`, `not-grouped`, `raid`, `not-leader`, `already-bound` (unbind a bot-led binding first), `no-bots`, `no-tank`, `tank-not-in-party`, `tank-not-bot`, `hired`, `not-controllable`, `not-live`, `bot-bound`, and `wrong-map` when you are not in the plan's dungeon; a start refused after the bind released it again. A stopped, finished or switched-off player-led run released its binding. [Dungeon Run Leader](../dungeon-run-leader.md#player-led-runs). |
| `.bot run system bind [<Bot>...] \| cmd <order> \| start [plan] [from ...] \| pause \| resume \| skip \| stop \| status \| unbind` | **System run** (GameMaster only): the server's own run of a bot-only party of world bots (online, not owned, hired, claimed or an alt, and no player's companion; target the lead bot). Needs `AiPlayerbot.DungeonSelfClear = 1` and `AiPlayerbot.BotLfgGroups = 1`. Status lines go to the server log; `start`, `pause`, `resume`, `skip`, `stop` and `status` run as the lead bot, `cmd` sends one order while no run is going, `stop` keeps the binding and `unbind` ends both. `player` and `speed=` are refused. With a real player in the group (or the run still flagged as paused for one) `stop`, `status`, `pause` and `unbind` still work — the GM always has a way out — while `cmd`, `resume` and every other forwarded order wait for a bots-only group (`player-in-group`). A real player joining the group pauses the run until the group is bots only again (handing the run to him is not built yet). Known limit: a failed bind's rollback cannot restore a moved bot whose old group broke up meanwhile, or that led its old group; such a bot ends ungrouped and the GM regroups it by hand. ERR `forbidden`, `disabled`, `invalid`, `no-target`, `not-bound`, `busy`, `no-bot`, `refused`, `invite-failed`, and the bind codes `ok`, `no-members`, `group-too-large`, `group-has-player`, `group-bound`, `group-raid`, `not-leader`, `bot-bound`, `member-grouped`, `hired`. |
| `.bot run lfg list \| form [plan] [alliance\|horde] \| fill <id> \| disband <id>` | **Bot LFG groups** (GameMaster, in game or console). Needs `AiPlayerbot.DungeonSelfClear = 1` and `AiPlayerbot.BotLfgGroups = 1`. `list` shows every group (id, dungeon, faction, state, seconds in it, open role, leader, members, posts made, the player holding an invite). `form` forms one now: it ignores the 60 s cadence, the group count and the players-online rule, but keeps `AiPlayerbot.BotLfgMaxGroups`, one group per dungeon and faction, and five free bots in the band. `fill` gives a waiting group its bot now; with no free bot of the open role in the band (in combat, flying, dead) it says so and the service retries every 5 s. `disband` ends a group (with a player in it, the bots only let go). ERR `forbidden`, `disabled`, `invalid`, `no-plan`, `max-groups`, `busy`, `no-bots`, `form-failed`, `no-group`, `not-waiting`. |
| `.bot run pause` | Pauses the run; the bots hold what they do. |
| `.bot run resume` | Goes on with a paused run: the party regroups first (`cmd revive party` while anyone is dead, then it gathers on the lead bot), then the paused step starts over with fresh tries and ceilings. A pause for the lead bot off the plan's map, and a 5 min player-hold pause, go on by themselves once the cause is gone (5 s back, once a step; a player's own pause never does). A pause for a lead bot in another part of the instance is not resumed while the lead bot is still there (more than 300 yd from the plan): `ACTION_ERR` code `lead-other-part`, *the lead bot is still in another part of the instance (x y z), N yd away from the run: bring it to the party first*, and the run stays paused. |
| `.bot run skip` | Leaves out the current step (its pull target and pull counts too) and goes on running (from a pause too, after the regroup of a resume). An item the step failed to get is noted missing, and the later steps that need it are left out with a note. |
| `.bot run stop [reason]` | Ends the run; the bots stay where they are, still bound. The status note reads `stopped by .bot run stop`, or `stopped: <reason>` when a reason is given (a driver with its own limit passes it, e.g. `time limit reached (3h)`; at most 80 printable characters). |
| `.bot run status` | Sends the run's status line again. |
| `.bot run mode` | Says whether the run is player-led (and its tank) or bot-led, or that no run is going. |
| `.bot run list` | The loaded plans: name, map, steps, and how many steps are not supported yet. |
| `.bot run here` | The plan whose route passes nearest you on your map, for a dungeon with one plan per wing (Scarlet Monastery, Dire Maul, Maraudon, Stratholme): `Run plan here: <plan> (<N> yd from its route).` and `TBM:RUN_HERE\|<plan>\|<yd>` (`TBM:RUN_HERE\|-\|0` when the map has no plan). The addon picks the plan with it. |
| `.bot run reloadplans` | **GameMaster only**: account security `SEC_GAMEMASTER`, which is `SEC_DEVELOPER` = **3** on this core (in game, or the console / SOAP with such an account; works with the run leader off). Reads the plan tables and `ai_boss_tactic` again through the startup loaders and swaps them in only when every row passes; a refused row (leg rows included), a plan gone, a failed query or a table that changed while it was read keeps the old set (ERR `refused` with the reason), a non-GM gets ERR `forbidden`. Answers the boot log's `Loaded N dungeon plans, ...` line with `reloaded`, one line per changed plan and the boss spawn cache rebuild. A run in progress keeps the plan it started with. Since 2026-10-22: [Dungeon Run Leader](../dungeon-run-leader.md#reloading-the-plans-bot-run-reloadplans). |

**Time limit:** a run ends by itself `AiPlayerbot.DungeonRunMaxMinutes` minutes (default 180 = 3 h, `0` = none) after `run start`, a pause counting; the status line then reads `done` with the note `time limit reached (3h)`, and the bots stay bound.

**Any player order pauses the run:** every `.bot cmd` order but `state`, `summon` and `revive`, every `.bot action`, and every chat command a real player gives a bot of the party (a whispered `stay`, `follow`, `attack`, `.bot command`, ...) sets it to `paused` with the order named, until `.bot run resume`. Unbinding ends it. With real players in the party (2026-10-23) this was the commander's rule only: his `.bot run`, `.bot cmd`, `.bot action` and whispered bot commands paused the run as before; party chat to the bots while the run ran was chatting (not taken as a bot command, no pause); another player's `.bot action` or whisper to a bound bot was refused (from the bind on, a run or not) (`TBM:ACTION_ERR|...|run-bound|...` and a plain line naming the commander). A player's normal play (moving, attacking, casting, looting, targeting) never paused it. A player-led run (`run start ... player`) was never paused by the leading player's orders: they were obeyed and the run went on ([Dungeon Run Leader](../dungeon-run-leader.md#player-led-runs)).

**Status line** (to the commander, whenever the step, phase or note changes, and on `start`, `status`, `pause`, `resume`, `skip`, `stop`):

```text
TBM:RUN|<plan>|<state>|<step>/<steps>|<leg>|<kind>|<phase>|<note>
TBM:RUN|uldaman|running|7/41|baelog|pull|pull|pulling 4851
TBM:RUN|uldaman|paused|12/41|baelog|boss|begin|3 dead: regroup (cmd revive party), then .bot run resume (2 regroups by itself at this step did not hold)
```

Since 2026-10-23 a run that deferred packs it could not reach from the party's floor and left them alive adds an eighth field after the note, only then: `...|<note>|left <K>` (`TBM:RUN|windhorn_canyon|done|1890/1890|...|plan done (1 pack left alive (...): step 68 at ...)|left 1`); a reader of the seven fields reads it as part of the note. A GameMaster's bot-only test run above speed 1.0 (`.bot run start ... speed=<x>`) adds a last field after `left <K>` (after the note when there is no `left`), only then: `...|<note>[|left <K>]|speed 2.0 (test)`; it reads `speed 2.0 (test; 1.0 since a real player joined)` once a real player joined the party and the run went back to normal speed. `<state>` is `running`, `paused` or `done`; `<phase>` is `begin`, `gather`, `walk`, `pull`, `task`, `rest`, `wait` or `hold`; `<note>` says what it does, or why it paused. During a scripted event (a `hold` step: Uldaman's Stone Keepers, Archaedas) `<phase>` shows how far the event is instead: `hold <dead>/<seen> down` between fights and `fight <dead>/<seen> down` in one, counting the event's units (`hold 1/4 down`: one of the four Stone Keepers is dead), and `<note>` says the same in words (`event: fighting (1 of 4 down, 1 awake)`). A failed event (the encounter reset) pauses with `event failed (...)` and sets the run back to the step that starts it (the ritual), so `.bot run resume` regroups and starts it again:

```text
TBM:RUN|uldaman|running|178/204|keepers|hold|fight 1/4 down|event: fighting (1 of 4 down, 1 awake)
TBM:RUN|uldaman|paused|177/204|keepers_altar|ritual|begin|event failed (data 1 reads 2, fail: the encounter reset (2 of 4 down)): .bot run resume regroups and starts it again at step 177 (ritual)
```

## Target by Entry and Timing

Objects can be targeted by entry ID: `entry:<id>` (nearest within `ReactDistance`, default 150 yd) or `entry:<id>@<x>,<y>,<z>` (nearest within 30 yd of coordinates). Format: decimal integer $\le$ 16777215, coordinates comma- or semicolon-separated without spaces. Emits a preceding `TBM:ACTION_TARGET` line (except `state`, which answers at once).

### Asynchronous Execution Protocol

Commands that require walking or casting emit asynchronous lifecycle records:
1. `TBM:ACTION_PENDING|cmd <verb>|bot:<Name>|<budget seconds>|<guid>` (or `party` for rituals and enters). Budgets: walk is 10 s + distance / 4 yd/s (capped at 60 s); loot 8 s; open 4 s (8 s for chests); use 15 s. Immediate actions complete without PENDING.
2. `TBM:ACTION_ACK|cmd <verb>|bot:<Name>|<count>|<detail>`.
3. `TBM:ACTION_ERR|cmd <verb>|<code>|<message>`.

Catalog of error codes: `no-bot`, `denied`, `too-far`, `no-item`, `locked`, `busy`, `in-combat`, `invalid-target`, `not-found`, `unavailable`, `cancelled`, `timeout`, `invalid`, `failed`, `dead-target`, `friendly`, `no-target`, `unreachable`, `off-mesh`, `no-los`, `no-ground`, `not-enough`, `interrupted`, `requirement`, `instance-limit`, `not-bound`, `lead-unavailable`, `no-ai`.

---

## Perception Feed (`TBM:VIEW`)

When enabled via `.bot commander view on`, the server transmits periodic perception frames (version `v1`) approximately once per second (`AiPlayerbot.RemoteCommanderViewMs`).

- **Transport:** Frames use lines capped at 250 bytes, max 8 lines per world tick, skipping frames when behind. Sequence numbers start at 1, coordinates use 1- or 2-decimal precision, and frames are constructed entirely from cached memory values.

```text
TBM:VIEW|v1|<seq>|B|<lead>,<party>,<hostiles>,<threats>,<truncated>
TBM:VIEW|v1|<seq>|P|<party record>[;<party record>...]
TBM:VIEW|v1|<seq>|H|<hostile record>[;<hostile record>...]
TBM:VIEW|v1|<seq>|T|<threat record>[;<threat record>...]
TBM:VIEW|v1|<seq>|E|<lines>
```

- **B (Begin):** Sequence number, lead bot GUID, counts of party/hostile/threat records, and truncation flag (`0` or `1`).
- **P (Party):** `guid,name,hp,maxhp,power,maxpower,powertype,x,y,z,o,map,instance,target,spell,dead,state,action,result,reason`.
- **H (Hostile):** `guid,entry,x,y,z,hp%,target,spell,flags,reaction,kind`. Capped at 24 units.
- **T (Threat):** `hostileGuid,botGuid:pct[,botGuid:pct...]`. Top three threat holders per enemy.
- **E (End):** Total count of lines in the frame.

### Example Frame

```text
TBM:VIEW|v1|12|B|0x2A,1,1,1,0
TBM:VIEW|v1|12|P|0x2A,Tanky,4000,5000,35,100,1,-8913.2,554.5,93.1,1.50,0,0,0xF130000C5E0012AB,0,0,c,heroic strike,O,-
TBM:VIEW|v1|12|H|0xF130000C5E0012AB,3874,-8900.0,560.0,94.0,64,0x2A,0,-,h,-
TBM:VIEW|v1|12|T|0xF130000C5E0012AB,0x2A:100,0x2B:41
TBM:VIEW|v1|12|E|5
```
