---
id: server-settings-dungeons-raids
title: Dungeon & Raid Settings
category: server
summary: Configuration flags for encounter mechanics, hazard avoidance, raid tactics, and creature auras.
tags: [config, dungeons, raids, tactics, bosses]
relates_to:
  - play-dungeons-and-raids
  - play-dungeon-reference
---

# Dungeon & Raid Settings

> Work in progress. These features are not officially released and not supported yet. Enable them at your own risk. Until the official release, bug reports and feedback about them are not handled through the issue tracker; contact Hypeer directly.

These settings configure boss encounter mechanics, environmental hazard evasion, raid positioning, and creature aura avoidance. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.AvoidCreatureAuras`](#aiplayerbotavoidcreatureauras) | `1` | Steers bots away from permanent damaging creature auras and hazardous monsters. |
| [`AiPlayerbot.CombatMonitor`](#aiplayerbotcombatmonitor) | `0` | Per-fight combat numbers of every bound group for the observability dashboard (needs `AiPlayerbot.Observability`). |
| [`AiPlayerbot.BombRunoutDistance`](#aiplayerbotbombrunoutdistance) | `30.0` | Distance in yards bomb debuff carriers run clear of the living group. |
| [`AiPlayerbot.BotAreaTriggers`](#aiplayerbotbotareatriggers) | *(empty)* | Audited allowlist of scripted area triggers a bot party may fire (off when empty; opt in with `962`). |
| [`AiPlayerbot.BotLfgChat`](#aiplayerbotbotlfgchat) | `1` | A waiting group posts in LookingForGroup and invites a player who whispers "inv". |
| [`AiPlayerbot.BotLfgFillAfterMinutes`](#aiplayerbotbotlfgfillafterminutes) | `10` | How long a group of four waits for a player before a bot takes the open slot. |
| [`AiPlayerbot.BotLfgGroups`](#aiplayerbotbotlfggroups) | `0` | World bots form groups of four that wait for a player, then take a fifth bot (needs `AiPlayerbot.DungeonSelfClear`). |
| [`AiPlayerbot.BotLfgGroupsPer100Bots`](#aiplayerbotbotlfggroupsper100bots) | `1` | How many groups exist at once per 100 bots online. |
| [`AiPlayerbot.BotLfgMaxGroups`](#aiplayerbotbotlfgmaxgroups) | `5` | The most groups at once. |
| [`AiPlayerbot.BotLfgOnlyWithPlayersOnline`](#aiplayerbotbotlfgonlywithplayersonline) | `1` | Groups form only while at least one real player is online. |
| [`AiPlayerbot.DungeonRunAggroCeiling`](#aiplayerbotdungeonrunaggroceiling) | `6` | Joiners above which the run leader rests the party before a pull (needs the estimate on). |
| [`AiPlayerbot.DungeonRunAggroEstimate`](#aiplayerbotdungeonrunaggroestimate) | `0` | The run leader estimates how many creatures a pull brings and rests first when it is many. |
| [`AiPlayerbot.DungeonRunBotKeysForPlayers`](#aiplayerbotdungeonrunbotkeysforplayers) | `0` | `1`: the bots carry the dungeon keys and open the doors for a party with real players too; `0`: players bring their own key, and the bots stop at the door and ask. |
| [`AiPlayerbot.DungeonRunFightForward`](#aiplayerbotdungeonrunfightforward) | `1` | After a won fight the next pack within 30 yd is pulled from where the tank stands when the way there is direct and clear. |
| [`AiPlayerbot.DungeonRunGiveItems`](#aiplayerbotdungeonrungiveitems) | `1` | A bot-only run gives the bots the keys and items its dungeon needs and takes them back when the run ends; `0` gives nothing. |
| [`AiPlayerbot.DungeonRunLightRest`](#aiplayerbotdungeonrunlightrest) | `1` | Before a small pull the party rests to light lines (60/35/25 to 75/60/45); before a boss, an event or a big pull the full ones. |
| [`AiPlayerbot.DungeonRunLootFfa`](#aiplayerbotdungeonrunlootffa) | `0` | A run leader loot step whose item is another member's under the group loot rules sets free-for-all for the carrier's loot, then puts the method back. |
| [`AiPlayerbot.DungeonRunMaxMinutes`](#aiplayerbotdungeonrunmaxminutes) | `180` | Time limit of a run leader run (`0` = none). |
| [`AiPlayerbot.DungeonRunPlayerLed`](#aiplayerbotdungeonrunplayerled) | `0` | Lets a player who leads his own party start a player-led run (`.bot run start ... player`): he walks, the run leader pulls the packs near him. |
| [`AiPlayerbot.DungeonRunPlayerLedAwayMinutes`](#aiplayerbotdungeonrunplayerledawayminutes) | `10` | How long the leading player of a player-led run may be out of the plan's dungeon before the run stops. |
| [`AiPlayerbot.DungeonRunPlayerLedPullYd`](#aiplayerbotdungeonrunplayerledpullyd) | `40` | How close the leading player stands to a plan pack or boss for the player-led run to pull it. |
| [`AiPlayerbot.DungeonRunRaid`](#aiplayerbotdungeonrunraid) | `0` | Lets the run leader bind the commander's raid of 6 to 40 bots and runs it with the raid rules (needs `AiPlayerbot.DungeonSelfClear` and `AiPlayerbot.RemoteCommander`). |
| [`AiPlayerbot.DungeonRunPullStand`](#aiplayerbotdungeonrunpullstand) | `0` | For a tank that pulls with a ranged weapon, the run leader orders `pullback` only when it can shoot from where it stands, else it walks the party to a firing stand first. |
| [`AiPlayerbot.DungeonRunRezHold`](#aiplayerbotdungeonrunrezhold) | `0` | The run leader's regroup waits for a living resurrector to raise the fallen (and for its mana) before it revives the party at the spirit healer. |
| [`AiPlayerbot.DungeonRunRetreatAt2Dead`](#aiplayerbotdungeonrunretreatat2dead) | `0` | `1`: with two dead in a fight the party is losing, the living pull back to the run's last stand once before a third death makes it a wipe. |
| [`AiPlayerbot.DungeonRunWalkBackRescue`](#aiplayerbotdungeonrunwalkbackrescue) | `1` | A stuck walk back, and a placed run's lead bot revived behind its start step, are teleported to a safe point on the run's way. |
| [`AiPlayerbot.DungeonRunWalkBackAfterWonFight`](#aiplayerbotdungeonrunwalkbackafterwonfight) | `0` | `1` walks the tank and party back to the stand after every guarded fight (the old way); `0` goes on from where the tank stands once the pack is cleared. |
| [`AiPlayerbot.DungeonSelfClear`](#aiplayerbotdungeonselfclear) | `1` | Turns on the run leader (`.bot run`) for in-party, player-led and server-owned runs. |
| [`AiPlayerbot.DungeonTactics`](#aiplayerbotdungeontactics) | `0` | Applies the per-boss tactics table (`ai_boss_tactic`) to every bot, not only commanded parties. |
| [`AiPlayerbot.EnableCustomRaidTactics`](#aiplayerbotenablecustomraidtactics) | `1` | Gates scripted encounter tactics and transition strategies for custom raids. |
| [`AiPlayerbot.HazardEvasionDistance`](#aiplayerbothazardevasiondistance) | `12.0` | Cap in yards on the step bots take when spreading for spacing. |
| [`AiPlayerbot.OnyxiaWhelpsAndEggs`](#aiplayerbotonyxiawhelpsandeggs) | `1` | Prioritizes whelp clearing and egg avoidance during Onyxia phase two. |

## Settings

### `AiPlayerbot.AvoidCreatureAuras`
Directs bots to maintain safe distances from dangerous creatures emitting lethal proximity auras or explosions.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, bots actively steer clear of hazardous creatures that damage or kill players standing near them:
- **Maraudon Creeping Sludge (12222):** Emits a permanent poison aura. Bots step out sideways when within 8 yards, and pathfinding routes stay at least 9 yards away. Non-tank bots engage from a ~9.5-yard stand-off hold ring. If a bot cannot attack effectively for 3 seconds, it sets the target aside for 30 seconds to focus on other mobs. Group tanks are exempt from the sludge ring during combat so they can hold mobs inside it.
- **Naxxramas Living Poison (16027):** Lethal floating ooze that detonates for massive damage within 5 yards. Because detonation is fatal, all bots (including tanks) step out 10 yards, path 11 yards away, and hold at an 11.5-yard stand-off ring. Living Poison cannot be selected or attacked; bots focus entirely on evasion.

Gap-closer abilities such as Charge, Intercept, and Feral Charge verify that the destination landing point is outside the hazard ring before executing. An explicit attack order on that target is never avoided, gap-closer-vetoed, or set aside. Positions are tracked via local proximity scans and periodic grid snapshots.

When set to `0`, bots treat hazardous creatures like standard mobs, chasing into melee range without avoiding dangerous proximity auras.

</details>

### `AiPlayerbot.CombatMonitor`
Collects per-fight combat numbers of every bound group for the observability dashboard.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Combat monitor:** per-fight combat numbers of every bound group for the observability dashboard: casts, damage, healing and overheal, damage taken, deaths with their last hits, and debuff uptime on the fought unit; one COMBAT_FIGHT datagram per fight and a rolling 10 s window. Costs a hash lookup per damage, heal and cast event while on. Needs AiPlayerbot.Observability.

</details>

### `AiPlayerbot.BombRunoutDistance`
Specifies how far bots carrying bomb debuffs must run from the center of the living group before stopping.
**Default:** `30.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

In raid encounters featuring bomb or explosive dispersal mechanics (such as Baron Geddon Living Bomb), bots carrying the debuff run clear of the group.

The afflicted bot runs straight away from the center of the living group until no party or raid member stands within this distance, then holds position until the debuff ends.

</details>

### `AiPlayerbot.BotAreaTriggers`
Audited allowlist of scripted area triggers that a bot in a commanded or player-led party runs when it walks into them.
**Default:** *(empty)* · **Values:** comma-separated area trigger ids; empty = off (the default); recommended `962` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Scripted area triggers for bots (audited allowlist, off by default):** a bot has no game client, so it never reports walking into an area trigger and a trigger script never ran for it (Zul'Farrak's Witch Doctor Zum'rah stayed friendly before an all-bot party). A living bot in a group inside a dungeon runs the script of a listed trigger it walks into, once per entry, as a player's client would, but only when its party is bound to a remote commander (every run-leader run is) or has a human member within 40 yd, so a follower running ahead of its player cannot start an event alone. Raids are left out until bots are tested there. Instance entrances and exits, taverns and battleground entrances never fire. Each bot tests its own map's listed triggers twice a second, no world scan. Comma-separated ids; empty (the default) turns it off, as new automation ships off. Opt in with `AiPlayerbot.BotAreaTriggers = 962`. Audited ids: **962** (Zul'Farrak, `at_zumrah`): if Witch Doctor Zum'rah is alive within 30 yd he turns hostile, yells and his encounter is marked in progress; nothing else. Not every trigger script is safe to start from a bot (`at_ring_of_law` starts the Blackrock Depths arena, the UBRS stadium trigger starts that event, `at_shadowforge_bridge` spawns guards): read a trigger's script and document what it does here before adding its id. Since fix/plan-gossip-allow a dungeon run did not need this list for the triggers its own plan walks into: a `move` or `move_direct` step whose `entry` names a trigger armed that trigger for the run's bots (running or paused) from that step on until it had fired once for the run, read from the loaded plan, so Zul'Farrak's plan fired `962` with the list empty; any other unlisted trigger stayed off, in a run or out of one. A trigger whose script refused (it returned false: at_zumrah while Zum'rah stood farther than 30 yd) stayed armed and each bot inside tried it again every 2 s; it counted as fired only once its script ran. A run started past the trigger's move step (`.bot run` from a later step) never armed it: when the event had not happened, the run was started again from before that step.

</details>

### `AiPlayerbot.BotGossipNpcs`
Allowlist of creature entries the `.bot cmd gossip` order may talk to (issue #1012).
**Default:** *(empty)* · **Values:** comma-separated creature entries; empty = every gossip order refused · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Bot gossip allowlist (issue #1012):** comma-separated creature entries that `.bot cmd gossip` may talk to. A gossip option can do anything its script says (take money, give items, teleport), so list only NPCs whose scripts you have read. Empty: every gossip order is refused with `not-allowed` before anything is sent to the NPC, except a dungeon run's own gossip step: since fix/plan-gossip-allow a bot of a running or paused run was let talk to the NPC entry its run's current gossip step names (the step's `entry` only), read from the loaded plan, so a shipped plan did not need its NPC listed here (Shadowfang Keep r27 had stopped on Deathstalker Adamant, 3849). Audited: `7604` Sergeant Bly (starts his fight after the Zul'Farrak pyramid event) and `7607` Weegli Blastfuse (blows the End Door after it), so `7607,7604` opens both. See [`.bot cmd gossip`](../../play/commands/commander.md#bot-cmd-gossip).

</details>


### `AiPlayerbot.BotLfgGroups`
World bots form groups of four that wait for a player, then take a fifth bot.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Bot LFG groups:** online, idle world bots of one faction and of a proven dungeon's level band (table ai_bot_lfg_plan; the band is the LFT addon's) form a group of four, tank, healer and damage, with one role left open for a player. While it waits it looks for that player in the LookingForGroup channel ([`AiPlayerbot.BotLfgChat`](#aiplayerbotbotlfgchat)). When no player joins within [`AiPlayerbot.BotLfgFillAfterMinutes`](#aiplayerbotbotlfgfillafterminutes), a bot of the open role joins. Getting the group to the dungeon is not built yet, so a full group breaks up again after two minutes. The count scales with the bots online (one group per [`AiPlayerbot.BotLfgGroupsPer100Bots`](#aiplayerbotbotlfggroupsper100bots) bots, at most [`AiPlayerbot.BotLfgMaxGroups`](#aiplayerbotbotlfgmaxgroups)), and by default groups form only while a real player is online. Every member is one faction: ragefire_chasm is Horde only, stormwind_stockade and stormwind_vault are Alliance only. A crashed server leaves its bot-only LFG groups saved in the characters database; those bots log in grouped and are skipped until a group cleanup exists. Needs [`AiPlayerbot.DungeonSelfClear`](#aiplayerbotdungeonselfclear) (`0` there keeps the groups dormant); the remote commander is not needed. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.BotLfgFillAfterMinutes`
How long a group of four waits for a player before a bot takes the open slot.
**Default:** `10` · **Values:** `1` to `60` minutes · **Restart:** not needed (`.reload config` applies it on the next world tick)

Only read while `AiPlayerbot.BotLfgGroups = 1`.

### `AiPlayerbot.BotLfgGroupsPer100Bots`
How many groups exist at once per 100 bots online.
**Default:** `1` · **Values:** `0` forms none by itself, up to `20` · **Restart:** not needed (`.reload config` applies it on the next world tick)

Only read while `AiPlayerbot.BotLfgGroups = 1`.

### `AiPlayerbot.BotLfgMaxGroups`
The most groups at once.
**Default:** `5` · **Values:** `0` to `20` · **Restart:** not needed (`.reload config` applies it on the next world tick)

Only read while `AiPlayerbot.BotLfgGroups = 1`.

### `AiPlayerbot.BotLfgOnlyWithPlayersOnline`
Whether groups form only while at least one real player is online.
**Default:** `1` · **Values:** `0` also on an empty server, `1` only with a player online · **Restart:** not needed (`.reload config` applies it on the next world tick)

Only read while `AiPlayerbot.BotLfgGroups = 1`.

### `AiPlayerbot.BotLfgChat`
A waiting group posts in LookingForGroup and invites a player who whispers "inv".
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

Only read while `AiPlayerbot.BotLfgGroups = 1`.

<details>
<summary>How it works in detail</summary>

The leader of a group that waits for a player posts one line in its faction's LookingForGroup channel (the dungeon, the open role, the level band, "w me inv"), again every 3 minutes, never more than one post per 30 s per faction across all groups, and only while a real player of that faction is online. A real player who whispers the leader "inv" gets a party invite when his faction, level and class fit the open role; other whispers get one short answer. Not limited by `AiPlayerbot.EnableBroadcasts`. Answers: "inv sent" when the invite goes out; "already sent you an inv" when he still holds the invite; "someone's joining atm, w me again in a min" while another invite is open; "sorry, we're full now" when the group stopped waiting; "sorry, can't group with hardcore chars" for a hardcore character; "leave your group and w me inv again" while he is grouped; "sorry, looking for 17-24" outside the band; "sorry, need a tank" when his class or talents do not fit the open role; "need tank for Deadmines (17-24), w me inv to join" as the hint for a whisper that is no invite; "couldn't inv you, got another invite open?" when the invite fails.

</details>

### `AiPlayerbot.DungeonRunAggroCeiling`
The number of joiners (creatures besides the one pulled) above which the run leader rests the party before a pull.
**Default:** `6` · **Values:** `1` and up · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

Only read while [`AiPlayerbot.DungeonRunAggroEstimate`](#aiplayerbotdungeonrunaggroestimate) is `1`. A pull whose estimate is exactly the ceiling goes out as before; one more joiner holds it once. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#aggro-estimate).

</details>

### `AiPlayerbot.DungeonRunAggroEstimate`
Lets the run leader (`.bot run`) estimate how many creatures a pull would bring and rest the party first when that is many.
**Default:** `0` · **Values:** `0` off (the run leader pulls exactly as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Pull aggro estimate:** before each pack pull the run leader counts the creatures that would come with the pulled one, by the core's own wake-up rules: the pulled creature's whole pack (`creature_groups` that aggro together, `creature_linking` rows), every creature whose aggro radius reaches the spot the party pulls from (detection range 20 yd, widened or narrowed by the level difference to the lowest party member, plus a few yards of drift), and one ring of family assistance (same faction, 10 yd, `CreatureFamilyAssistanceRadius`). When more than [`AiPlayerbot.DungeonRunAggroCeiling`](#aiplayerbotdungeonrunaggroceiling) would join, the status note reads `N joiners expected` and the party rests to its end lines once for that pull (at most 60 s, not counted toward the step's idle ceiling, at most three a step; after that the note reads `N joiners expected, rest budget used` and the pull goes out). It adds no movement and never skips a pull; the estimate is approximate (it can be high or low); with `0` nothing is estimated. Only read while `AiPlayerbot.DungeonSelfClear = 1`. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#aggro-estimate).

</details>

### `AiPlayerbot.DungeonRunBotKeysForPlayers`
Whether the bots carry the dungeon keys for a party that has real players in it.
**Default:** `0` · **Values:** `0` players bring their own key, `1` the bots carry the keys and open the doors · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Keys with players:** only read while `AiPlayerbot.DungeonSelfClear = 1`. With `0` (the default) a run with a real player in the party gives the bots no keys. The start warns once, naming each key and where to get it. At a locked door the bots stop before it and ask the player to open it with the key or Lockpicking; they go on by themselves once the door reads open, and `.bot run resume` works once it is open. With `1` the bots carry the keys and open the doors for the players, as in a bot-only run. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#keys-and-doors).

</details>

### `AiPlayerbot.DungeonRunFightForward`
Whether the run leader pulls the next pack close by from where the tank stands after a won fight.
**Default:** `1` · **Values:** `0` the plan's rows as before, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Fight forward:** only read while `AiPlayerbot.DungeonSelfClear = 1`, and only with `AiPlayerbot.DungeonRunWalkBackAfterWonFight = 0`. Added 2026-10-23 (1,909 pulls of that day walked 28.2 yd each back to the plan's stand and forward again). After a `pull` or `boss` step whose pack the party fought and cleared, when at most 4 plain `move` rows of the same leg lead to a `pull` of the party's team within 30 yd of the tank (8 yd up or down), not an ambush and not left uncleared, with no deferred pull pending and no other live plan pack on the straight line, the run asks the server for the way first: the tank's navmesh path to the pack had to reach it (it may end at most 10 yd short of the point and 6 yd above or below it) and be at most 1.3 times the straight line, the tank had to see the pack's point, no living hostile creature other than the pack's own could stand within its aggro radius plus 5 yd of that path, and none within 15 yd of a move row left out. Then the rows were left out and the pull went out from where the tank stood. The pull's own rules stayed: its gather, the firing stand, crowd control and the aggro estimate. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonRunGiveItems`
Whether a bot-only run gives the bots the dungeon keys and items it needs.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Giving run items:** only read while `AiPlayerbot.DungeonSelfClear = 1`. At the start of a run with no real player in the party, every bot gets each item the plan needs and does not carry (the Mallet of Zul'Farrak, the keys of the locked doors). The items are soulbound, so they cannot be traded, mailed or sold. The run takes them back when it ends or stops, when it is erased, when a bot leaves the party, and when a real player joins. The exact item instances are recorded in the characters database (`ai_run_given_item`), so a crash does not leave them with the bots. A bot's own copy is never taken. With `0` nothing is given, and a bot-only run pauses at a locked door until `.bot run skip` passes it. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#keys-and-doors).

</details>

### `AiPlayerbot.DungeonRunLightRest`
Whether the run leader rests the party only to light lines before a small pull.
**Default:** `1` · **Values:** `0` the full lines everywhere (as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Light rest:** only read while `AiPlayerbot.DungeonSelfClear = 1`. Added 2026-10-23 (203 min of rest in 31 runs). Before a plain `pull` within the next 8 steps, or with no fight ahead, a rest starts only under 60 % health, 35 % healer mana or 25 % other mana and ends at 75 / 60 / 45; a bot still drinking above those lines holds the next order only while the lead bot itself is seated or eating. Before a `boss`, a `hold`, a `ritual`, a scripted event, a pull whose aggro estimate reads 8 joiners or more, or (with the estimate off) a pull whose pack in sight has 5 units or more, the full lines stand; such a big pack gets one rest to the full lines before its pull. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonRunLootFfa`
Lets the run leader's loot step get an item the group's loot rules gave to another member, by switching the group to free-for-all for that one loot.
**Default:** `0` · **Values:** `0` the loot step tries the other bots in turn, `1` free-for-all for the carrier's loot · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Loot step free-for-all:** only read while `AiPlayerbot.DungeonSelfClear = 1`. A boss's quest item that is a plain drop (Revelosh's Shaft of Tsol, Uldaman) belongs to the round-robin member's turn under group or round-robin loot, so the lead bot, who needs it for the next steps, is refused with *nothing this bot may loot* and the old way is to try the other bots, which leaves the item in a bag the lead bot cannot use. With `1`, on that refusal, once per step and before any other bot is tried, the lead bot (the leader of the bound group) sets the group's loot method to free-for-all (`cmd lootmethod ffa`), the carrier loots the item, and when that loot is answered (ACK or ERR) the method goes back to the one the group had (`cmd lootmethod group`, `rr`, `master` or `nbg`; the loot threshold is kept). The method goes back on any later tick of the running run too, whatever the step (a wipe, a lead death or a fight cap that sends the run back through a regroup in between does not leave it on free-for-all): one `cmd lootmethod <saved method>` a second, at most three tries, until the group reads a method that is not free-for-all; if all three are refused (for instance the lead bot lost the group's leadership meanwhile) the run pauses with a note naming the lead bot's group, this key and the order to give by hand, and the module tries once more as the run pauses. The method also goes back when the run pauses or stops in between, when the binding ends and when the run leader is switched off; it is not touched if a player has set another method since. Switching `AiPlayerbot.DungeonRunLootFfa` off by a config reload while a detour is under way does not stop the restore. The wait for a busy carrier before the free-for-all loot is limited to 20 s: then the method goes back and the step goes on the old way. The saved method is the group's method name only (the loot threshold is kept as it is): a group that was on master loot comes back on master loot with the lead bot as master looter, not the member who held that role before. Known limits: if a player sets free-for-all by hand during the detour, the module cannot tell it from its own and puts the saved method back; the run's state is gone after a server restart, and whether the group's loot method survives a restart was not checked (unverified). Not used when the step has no `item`, when anyone already carries the item, when the item is no longer on the corpse, when the group is free-for-all already or has no loot method to restore, or when the lead bot does not lead the group; a refused `lootmethod ffa` order sends the step down the old path. With `0` nothing is read and nothing changes. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonRunMaxMinutes`
How long a run leader run (`.bot run`) may last before it ends by itself.
**Default:** `180` · **Values:** minutes; `0` = no limit · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Run leader time limit:** a `.bot run` that has lasted this many minutes since `run start` (a pause counts) ends with the status note `time limit reached (3h)`, the bots staying bound. `0` = no limit. Only read while `AiPlayerbot.DungeonSelfClear = 1`. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonRunPlayerLed`
Lets a player who leads his own party of bots start a player-led run of a dungeon plan.
**Default:** `0` · **Values:** `0` refused, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>
**Player-led runs:** `.bot run start [plan] player [tank <Bot>]` from a player who leads a party (not a raid) whose every bot is his to control; up to three other real players may be in it. The start makes its own binding: the player stays the party's leader and the bots keep following him, the named tank bot (else the first tank-role bot) is the run's lead bot and puller. Needs `AiPlayerbot.DungeonSelfClear = 1` too. Plain `.bot run start` stays bot-led. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#player-led-runs).

</details>

### `AiPlayerbot.DungeonRunPlayerLedAwayMinutes`
How long the leading player of a player-led run may be out of the plan's dungeon before the run stops.
**Default:** `10` · **Values:** minutes, `1` to `60` · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Player-led away limit:** while the player is out of the plan's dungeon the bots that stayed inside hold where they stand (`cmd stay`, once) and the status note counts the minutes left; back in time, the bots follow him again (`cmd follow`, once) and the run goes on. Past the limit the run stops with the note *stopped: you were away from the plan's dungeon for 10 min* and its binding ends; the bots keep holding. Only read while `AiPlayerbot.DungeonRunPlayerLed = 1`. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#player-led-runs).

</details>

### `AiPlayerbot.DungeonRunPlayerLedPullYd`
How close the leading player of a player-led run stands to a plan pack or boss before the run pulls it.
**Default:** `40` · **Values:** yards, `5` to `100` · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Player-led pull range:** a straight-line distance from the player to the step's point. Only read while `AiPlayerbot.DungeonRunPlayerLed = 1`. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#player-led-runs).

</details>

### `AiPlayerbot.DungeonRunRaid`
Raid bindings for the run leader: `.bot commander bind <LeadBot> raid` binds the commander's own raid of 6 to 40 bots.
**Default:** `0` · **Values:** `0` off (every raid bind refused), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Raid bindings for the run leader:** `.bot commander bind <LeadBot> raid` binds the commander's own raid of 6 to 40 bots (he leaves it; the lead bot is the main tank), and the run leader uses the raid rules (wipe level, gathering and rest scaled to the raid's size). Needs AiPlayerbot.DungeonSelfClear = 1 and AiPlayerbot.RemoteCommander = 1. Switching it off releases every raid binding. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonRunPullStand`
Makes the run leader put the tank where its ranged weapon can fire before it orders `pullback`.
**Default:** `0` · **Values:** `0` off (the run leader pulls exactly as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Pull stand:** only read while `AiPlayerbot.DungeonSelfClear = 1`. In the Wailing Caverns runs of 2026-10-21 a tank ordered from a spot it could not shoot from walked to a firing position by itself and did not fire from there (the pull timed out and the same order came again about 26 s later), while every pull ordered with the tank standing in range and in sight fired at once. The rule only covers a tank whose pull is a ranged weapon shot (bow, crossbow, gun, thrown weapon or wand). Any other pull goes out as before: a spell pull, a body pull (no ranged weapon, skill, shoot spell or ammo), or a shot range of 5 yd or less. With `1`, before each `pullback` the run leader checks three things: the target is in the tank's sight, it is within the shot's range less 5 yd (the range is read from the tank's own pull), and the tank stands still (a moving tank is waited for up to 3 s). If any of them fails, it reads a firing stand on the tank's navmesh path to the target (the path walked back from the target to the first point in range that sees it). It walks the party there, then orders the pull; at most 2 stands per target and step. A stand walk that gains no ground for 12 s, or lasts more than 36 s, is given up and the order goes out from where the tank is (`firing stand unreachable: ordering from here`): the run never pauses for it. No stand, the stands used up, or a scripted event: the order goes out as before. A fight, a gather or a regroup that comes in between ends the wait; the stands already walked to stay counted for that target in that step, so the rule is applied again only with the stands left. A stand walk that is given up is followed by one tick that checks the way back to the last stand point (a tank stuck on another floor first gathers back there) before the order goes out. Among units of the same kill-first rank, one the tank can shoot from where it stands is pulled before a nearer one it cannot. The status note of each order names the target's guid, the how-many-th order to it this step and how its shot stood (`pulling 3636 0x... (order 2; shot from firing stand 1)`). With `0` nothing is read and nothing changes. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#pull-stand).

</details>

### `AiPlayerbot.DungeonRunRezHold`
Makes the run leader's regroup wait for a living priest, paladin or shaman to resurrect the fallen where they lie, instead of a fixed 15 s.
**Default:** `0` · **Values:** `0` the old fixed wait, `1` the rez hold · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Rez hold:** only read while `AiPlayerbot.DungeonSelfClear = 1`. With `0` the regroup behaves as before: when a priest, paladin or shaman is out of combat it waits 15 s for a resurrection, then sends `cmd revive party`. With `1` the run asks the rez hold (`runtime/RezHoldPolicy.h`) every second while someone is dead. It sends the resurrector no order (the bots' own AI decides who raises whom, and a resurrector short of mana drinks by itself); the one order it does send is a `move` of the lead bot onto the body, once per regroup, after 12 s of hold with a resurrector able, no cast under way and the body more than 8 yd from the lead bot. It **holds** up to 60 s from the moment a resurrector could act (plus 12 s for each body beyond the resurrectors, at most 120 s, and a resurrection being cast may finish: 12 s more, once), **waits for mana** while the only resurrectors are short of their spell's cost (90 s in all), and sends `cmd revive party` at once when no living bot can resurrect, when every body lies more than 80 yd from the party, or when the time is up. The 240 s the dead get to be up after `revive party` starts when the hold gives up. A druid's Rebirth counts only when no priest, paladin or shaman is alive on the map within sight, and only off cooldown with its seed in the bags. Everyone dead is a wipe: the old wipe recovery takes it. See [Dungeon Run Leader](../../play/dungeon-run-leader.md). Derived from jrad7/mod-dungeon-clear (AGPL-3.0-or-later).

</details>

### `AiPlayerbot.DungeonRunRetreatAt2Dead`
Whether the run leader pulls the living back to the last stand when two party members are dead and the fight is being lost.
**Default:** `0` · **Values:** `0` the party fights on (as before), `1` one retreat per fight · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Retreat at two dead:** only read while `AiPlayerbot.DungeonSelfClear = 1`. Added 2026-10-23 for the wipes of that day (30 notes *3 dead: fighting on, a regroup follows*, each one a wipe and a corpse walk back of 4-28 min). With `1`, in a fight with two members dead (one fewer than the 3 that make the run regroup by itself), the lead bot alive, and the fight being lost (the fight ceiling's hp watch had read the engaged units for at least 15 s and none of them set a new low in that time), the run sends one `cmd move` to its last stand (the last point it walked the party to, at most 40 yd away; again once after 5 s), note *retreat: 2 dead and no engaged unit lost hp in 15 s: the living pull back to the last stand at x y z*. The retreat ends at the stand, after 20 s, at a third death or when the lead bot dies, and the fight is the bots' own again; once per fight. A refused move ends it without a pause. Never inside a scripted event, on the steps leading into one, on a `hold` step or on a boss step that is an event (an encounter, or done by instance data or an object). Whether the mobs follow the party back or let it go is the game's (the module adds no leash); this was not proven in a live run when it was added. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonRunWalkBackRescue`
Whether the run leader teleports a stuck walk back's lead bot (and a placed run's lead bot revived behind its start step) to a safe point on the run's way.
**Default:** `1` · **Values:** `0` the old pause, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Walk back rescue:** only read while `AiPlayerbot.DungeonSelfClear = 1`. Added 2026-10-23 (Razorfen Kraul r27 slot 8: three walk backs climbed to a dead-end ledge and paused stuck; Gnomeregan r27 slot 4: a run placed at step 543 walked back from the entrance for 23 min). With `1`, a walk back whose lead bot stays stuck short of its point after the walk's tries is not paused: the lead bot is teleported (the stranded lead bot's rescue, the module's own teleport) to the last point it reached on the plan, or, when a live pack is near that point, to the latest point before it (back to the run's start step, never back over a direct step) out of every live pack's reach (its radius plus 10 yd), and with no such point the run pauses with that reason instead; and a placed run's lead bot that is up behind the run's start step (the plan point nearest it lies before the start step and none of the run's own points is within 60 yd) is teleported at once to such a safe point on the way it came in a party with no real player (with one, to the run's first such safe point). Out of combat only, once a step, three failed tries a step; the party left behind is summoned to it only out of combat. The walk back goes on from that point. A death that paused the run past its regroups by itself armed the rescue again for the walk back after the resume. A walk-back point with no walkable path from the spot where the hop off a navmesh island found no spot was rescued the same way in a party with no real player (Dire Maul North r31: revived on the entrance island, every point passed to a pause); with a real player in the party the walk back went on with him instead. In a party with no real player, a straight walk back that failed and whose replay of the plan's points would have walked 300 yd or more before that safe point was rescued instead of replayed (Stratholme r29, Maraudon r28b). A backoff of the straight walk whose plan points from its new target would have walked 300 yd or more before that safe point was rescued the same way (Shadowfang Keep r36: the second backoff landed on the entrance point where the lead bot was revived, and the walk back walked 307 yd of points without a rescue). See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonRunWalkBackAfterWonFight`
Whether the run leader walks the tank and the party back to the guard's stand point after a fight it has won.
**Default:** `0` · **Values:** `0` no walk back once the step's pack is cleared, `1` the walk back after every guarded fight (the old way) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Walk back after a won fight:** only read while `AiPlayerbot.DungeonSelfClear = 1`. When the tank leaves its stand during a fight to take a mob off the party (or engages one on its arrival), the run notes that stand. Up to 2026-10-22 it walked the tank, and the party with it, back to the stand after every such fight (status note `back to the stand point after picking up the party's attackers`), then on to the next pack or boss: in the Frostmane Hollow run r20-19z39 of 2026-10-22 the party ran 2-3 s back from each pack and boss and then forward again. With `0`, once the fight is over the walk back is left out when the step is a pull or a boss with none of its units left to pull (the step says `pack cleared` or `boss down` and the next one starts from where the tank stands; its own approach, firing stand or gather places the party, and a tank left on another floor or without a path still gathers back at the last stand) and on a move step (its walk goes on forward). It is kept when a pull or boss step has units left (they are pulled from the same stand) and on every other step (a wait, a rest, an object or item step: those act where the party stood). Nothing changes during a fight (the walk back is only ordered out of combat) or in a scripted event (a hold returns to its own spot). With `1` the walk back goes out after every guarded fight, as before. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>
### `AiPlayerbot.DungeonSelfClear`
Turns on the run leader (`.bot run`) for in-party, player-led and server-owned runs.
**Default:** `1` · **Values:** `0` off (every `.bot run` refused), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>
**Dungeon self-clear (run leader):** a `.reload config` reads this key and the `AiPlayerbot.DungeonRun*` keys again from `aiplayerbot.conf` on the next world tick (only those: every other bot setting, `AiPlayerbot.RemoteCommander` included, still needs a restart; until 2026-10-22 none of them was read again before a restart). Set to `0` by a reload, every run ends with a last status line. With `1` a bound party may start `.bot run start [plan]`: its party walks a dungeon plan from the world tables `ai_dungeon_plan`/`ai_dungeon_leg`/`ai_dungeon_step` and pulls, fights, rests, loots and opens doors by itself, using the commander's own `.bot cmd` orders. With 3 or more dead it regroups by itself once the fight is over (the safety rules are in the run leader page); it pauses on any player order and at every wait's ceiling. With `0` every `.bot run` is refused. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonTactics`
Applies the per-boss rows of the world table `ai_boss_tactic` (interrupt, kill first, stay behind) while bots fight that boss.
**Default:** `0` · **Values:** `0` only parties a remote commander has bound, `1` every bot on a map with rows · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Dungeon boss tactics:** applies the per-boss rows of the world table `ai_boss_tactic` while bots fight that boss: interrupt a listed spell (*Pummel*, *Shield Bash*, *Kick*, *Counterspell*, *Earth Shock*), kill a listed add before anything else and in a listed order (DPS only, never tanks or healers), and stay out of the front of a boss with a frontal attack (everyone but the bot the boss attacks). Uldaman up to Ironaya and the Obsidian Sentinel so far; see [Boss Tactics](../../play/boss-tactics.md). It changes how bots play, so it is off by default: with `0` only bots in a group a remote commander has bound get it, with `1` every bot on a map that has rows does. An explicit `attack` order from a player always wins.

</details>

### `AiPlayerbot.EnableCustomRaidTactics`
Master toggle for scripted encounter tactics across custom raid instances.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Enables automated raid tactical handling across custom raid encounters, including Emerald Sanctum (map 807), Lower Karazhan (map 532), and Karazhan Crypt (map 800).

When enabled, entering one of those maps swaps in its strategy with that raid's encounter scripts. When disabled, custom-raid tactics stay off; vanilla raid tactics are unaffected.

</details>

### `AiPlayerbot.HazardEvasionDistance`
Cap in yards on the step bots take when spreading for spacing.
**Default:** `12.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Step length when spreading: just past the trigger radius (`radius+1`), capped by this setting (default 12 yd).

It caps the spread step used for raid spacing; a longer step would overshoot out of melee and ping-pong back.

</details>

### `AiPlayerbot.OnyxiaWhelpsAndEggs`
Enforces whelp target prioritization and egg cluster avoidance during the airborne phase of the Onyxia encounter.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Active on map 249 (Onyxia's Lair) only while the `onyxia` fight strategy is active and the boss is airborne during phase two:
- Ranged damage bots (mages, warlocks, hunters, and ranged shaman and druid specs; never tanks or healers) automatically switch target to the nearest Onyxian Whelp within 40 yards and kill it before returning to the boss. Whelps located within 10 yards of unbroken eggs are skipped to prevent collateral hatching.
- All bots in the encounter back away toward the nearest cave mouth (`-10,-180` north, `-16,-250` south) if they find themselves within 12 yards of unbroken Onyxia eggs.

An explicit attack order always takes precedence over automated whelp targeting. When set to `0`, bots use standard target selection and ignore egg proximity.

</details>
