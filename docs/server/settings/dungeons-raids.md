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
| [`AiPlayerbot.DungeonRunAggroRingYd`](#aiplayerbotdungeonrunaggroringyd) | `20` | How far a call for help carries in the ring estimate. |
| [`AiPlayerbot.DungeonRunAggroWalkRing`](#aiplayerbotdungeonrunaggrowalkring) | `0` | The estimate also counts what the walk, the stand and the fight point wake, and calls for help from every member. |
| [`AiPlayerbot.DungeonRunBossSpread`](#aiplayerbotdungeonrunbossspread) | `0` | `1`: the boss tactics keep their `spread` rows — the ranged raid keeps the row's yards apart while the boss fights (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunCharmYield`](#aiplayerbotdungeonruncharmyield) | `0` | `1`: a bot a creature charms yields its AI, and no area action lands on a charmed group member (needs no rows or tactics). |
| [`AiPlayerbot.DungeonRunFightForward`](#aiplayerbotdungeonrunfightforward) | `1` | After a won fight the next pack within 30 yd is pulled from where the tank stands when the way there is direct and clear. |
| [`AiPlayerbot.DungeonRunGiveItems`](#aiplayerbotdungeonrungiveitems) | `1` | A bot-only run gives the bots the keys and items its dungeon needs and takes them back when the run ends; `0` gives nothing. |
| [`AiPlayerbot.DungeonRunLightRest`](#aiplayerbotdungeonrunlightrest) | `1` | Before a small pull the party rests to light lines (60/35/25 to 75/60/45); before a boss, an event or a big pull the full ones. |
| [`AiPlayerbot.DungeonRunLinkedDeath`](#aiplayerbotdungeonrunlinkeddeath) | `0` | `1`: free DPS bots equalise a `linked_death` group's health, hold every member at its `param` % and burn them together (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunLootFfa`](#aiplayerbotdungeonrunlootffa) | `0` | A run leader loot step whose item is another member's under the group loot rules sets free-for-all for the carrier's loot, then puts the method back. |
| [`AiPlayerbot.DungeonRunMaxMinutes`](#aiplayerbotdungeonrunmaxminutes) | `180` | Time limit of a run leader run (`0` = none). |
| [`AiPlayerbot.DungeonRunNearWipeStop`](#aiplayerbotdungeonrunnearwipestop) | `2` | Wipe events within the window that pause a raid run once the fight is over (`0` = off). |
| [`AiPlayerbot.DungeonRunNearWipeStopMinutes`](#aiplayerbotdungeonrunnearwipestopminutes) | `10` | The near-wipe stop's window, in minutes of running time. |
| [`AiPlayerbot.DungeonRunNearWipeStopParty`](#aiplayerbotdungeonrunnearwipestopparty) | `0` | `1`: party bindings get the near-wipe stop too; `0`: raids only. |
| [`AiPlayerbot.DungeonRunPackClearMarginYd`](#aiplayerbotdungeonrunpackclearmarginyyd) | `10` | A pull step is done only when nothing of its entries is left within its radius plus this band (`0` = off). |
| [`AiPlayerbot.DungeonRunPackClearRepulls`](#aiplayerbotdungeonrunpackclearrepulls) | `3` | Residue pulls a pull step may send before it pauses for a human. |
| [`AiPlayerbot.DungeonRunStragglerPull`](#aiplayerbotdungeonrunstragglerpull) | `0` | The tank walks to and pulls a pack unit or patrol the step waited for and did not come, instead of pausing; a stand-pull row's patrol is waited for one loop of its path at most. |
| [`AiPlayerbot.DungeonRunPlayerLed`](#aiplayerbotdungeonrunplayerled) | `0` | Lets a player who leads his own party start a player-led run (`.bot run start ... player`): he walks, the run leader pulls the packs near him. |
| [`AiPlayerbot.DungeonRunPlayerLedAwayMinutes`](#aiplayerbotdungeonrunplayerledawayminutes) | `10` | How long the leading player of a player-led run may be out of the plan's dungeon before the run stops. |
| [`AiPlayerbot.DungeonRunPlayerLedPullYd`](#aiplayerbotdungeonrunplayerledpullyd) | `40` | How close the leading player stands to a plan pack or boss for the player-led run to pull it. |
| [`AiPlayerbot.DungeonRunRaid`](#aiplayerbotdungeonrunraid) | `0` | Lets the run leader bind the commander's raid of 6 to 40 bots and runs it with the raid rules (needs `AiPlayerbot.DungeonSelfClear` and `AiPlayerbot.RemoteCommander`). |
| [`AiPlayerbot.DungeonRunRaidBossRules`](#aiplayerbotdungeonrunraidbossrules) | `0` | `1`: a raid's boss fight runs up to 1200 s, survives its deaths while the boss still loses hp, and sends its adds to the off-tanks instead of the main tank. |
| [`AiPlayerbot.DungeonRunRaidHealerGuard`](#aiplayerbotdungeonrunraidhealerguard) | `0` | `1`: in a raid boss fight with the main tank on the boss, and in a raid trash fight once the main tank held its current target (kept on for the fight), an add on a healer goes to the nearest ranged dps within 35 yd of it (up to two per add), and the off-tanks take only such an add when no ranged dps is free. |
| [`AiPlayerbot.FankrissSwapStacks`](#aiplayerbotfankrissswapstacks) | `5` | Mortal Wound stacks at which an opt-in `fankriss tank` taunts Fankriss off the current tank (1-9). |
| [`AiPlayerbot.PatchwerkOpeningDelay`](#aiplayerbotpatchwerkopeningdelay) | `4` | Seconds the DPS hold Patchwerk after an opt-in `patchwerk main` engages him (`0` = off, else 3-5). |
| [`AiPlayerbot.DungeonRunPullGate`](#aiplayerbotdungeonrunpullgate) | `0` | `1`: the run leader never pulls a boss its `pull_gate` row gates while the boss wears the row's aura, and ends the run after the row's seconds (Hakkar needs all of this, seek, charm yield and boss spread, plus the aura tactics). |
| [`AiPlayerbot.DungeonRunStandClearance`](#aiplayerbotdungeonrunstandclearance) | `0` | Every firing stand the run leader picks (an authored stand-pull step's own pull, this key being what gives that step a stand rule, a generic pull's with `AiPlayerbot.DungeonRunPullStand`) is refused when another live pack (its patrol loop included) can reach it; none clear, the stand the key is off for is used (advisory). |
| [`AiPlayerbot.DungeonRunStandPullPreCc`](#aiplayerbotdungeonrunstandpullprecc) | `0` | A stand-pull step's crowd control goes out at the last point of the approach walk outside the pulled pack's own aggro range, before the party walks in. |
| [`AiPlayerbot.DungeonRunStandDiscipline`](#aiplayerbotdungeonrunstanddiscipline) | `0` | A pull row fires from the plan's authored approach only (never a closer firing stand), and every tank and melee is waited for at that stand or clear of the next pull's pack before the row fires. |
| [`AiPlayerbot.DungeonRunAutoCc`](#aiplayerbotdungeonrunautocc) | `0` | `1`: the run leader marks crowd control on a pack from what the raid has (casters first, the plan's `cc_priority`/`cc_always` as a hint), above the body threshold, and re-applies a mark that broke. |
| [`AiPlayerbot.DungeonRunAutoCcAbove`](#aiplayerbotdungeonrunautoccabove) | `6` | How many bodies a pull may bring before the automatic crowd control starts (crowd control from the body after it). |
| [`AiPlayerbot.DungeonRunAutoCcRisky`](#aiplayerbotdungeonrunautoccrisky) | `0` | `1`: the automatic crowd control may use Fear and Scare Beast, only with 35 yd of room and when no other family has a free bot. |
| [`AiPlayerbot.DungeonRunRezHold`](#aiplayerbotdungeonrunrezhold) | `0` | The run leader's regroup waits for a living resurrector to raise the fallen (and for its mana) before it revives the party at the spirit healer. |
| [`AiPlayerbot.DungeonRunSeekAura`](#aiplayerbotdungeonrunseekaura) | `0` | `1`: the boss tactics keep their `seek_aura` rows — the raid gathers in a dying source's cloud before the boss's cycle spell lands (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunWalkBackRescue`](#aiplayerbotdungeonrunwalkbackrescue) | `1` | A stuck walk back, and a placed run's lead bot revived behind its start step, are teleported to a safe point on the run's way. |
| [`AiPlayerbot.RunTeleportReturnYd`](#aiplayerbotrunteleportreturnyd) | `0` (recommended `120`) | A party bot the run brings back from farther than this (yards) is teleported to the lead bot with its combat dropped, never summoned by portal or left to walk back; `0` off. |
| [`AiPlayerbot.DungeonReviveHoldYd`](#aiplayerbotdungeonreviveholdyd) | `0` (recommended `100`) | A run's bot revived farther than this (yards) from the lead bot, and every bot the run is bringing back, stands still until it arrives (60 s at most); `0` off. |
| [`AiPlayerbot.DungeonRunWalkBackAfterWonFight`](#aiplayerbotdungeonrunwalkbackafterwonfight) | `0` | `1` walks the tank and party back to the stand after every guarded fight (the old way); `0` goes on from where the tank stands once the pack is cleared. |
| [`AiPlayerbot.DungeonSelfClear`](#aiplayerbotdungeonselfclear) | `1` | Turns on the run leader (`.bot run`) for in-party, player-led and server-owned runs. |
| [`AiPlayerbot.DungeonTactics`](#aiplayerbotdungeontactics) | `0` | Applies the per-boss tactics table (`ai_boss_tactic`) to every bot, not only commanded parties. |
| [`AiPlayerbot.EnableCustomRaidTactics`](#aiplayerbotenablecustomraidtactics) | `1` | Gates scripted encounter tactics and transition strategies for custom raids. |
| [`AiPlayerbot.HazardEvasionDistance`](#aiplayerbothazardevasiondistance) | `12.0` | Cap in yards on the step bots take when spreading for spacing. |
| [`AiPlayerbot.OnyxiaWhelpsAndEggs`](#aiplayerbotonyxiawhelpsandeggs) | `1` | Prioritizes whelp clearing and egg avoidance during Onyxia phase two. |
| [`AiPlayerbot.RaidEntryByCommander`](#aiplayerbotraidentrybycommander) | `1` | Waives a bot's failed entrance condition at a raid entrance with no area-trigger script when its commander is a real player who commands the raid (in the group, or through the binding of a bound raid) and meets it. |

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

**Combat monitor:** per-fight combat numbers of every bound group for the observability dashboard: casts, damage, healing and overheal, damage taken, deaths with their last hits, and debuff uptime on the fought unit; casts made in the 3 s after combat ends (meals, mounts, stance changes) are listed apart from the fight's own casts; one COMBAT_FIGHT datagram per fight and a rolling 10 s window. The live window is sent with the commanders snapshot but the dashboard does not read it yet (a later daemon task). While on, a damage, heal or cast event of a player outside every bound group costs one atomic bit test (no lock); a member's event takes the monitor's lock. A fight past 500 distinct engaged creatures counts the rest in `fight.dropped` (distinct creatures, the count stops at 4096). A fight too big for the 60 KB datagram is first sent with fewer spells and death-recap hits, then as a short form marked `"truncated":true` (texts cut to 48 bytes, names and classes to 24, at most 40 bot rows and 64 planned entries, no spells, engaged list, debuff times or deaths); a fight that fits in no form is dropped with an error line in the server log. Each finished fight is sent twice; a bound group's fights are recorded with or without a run (a run tick feeds the groups with a running run, a bound-group sampler feeds the rest, so a fight pulled by hand or after `.bot run stop` is still recorded); a fight whose lead stays away (out of the world across a map transition, no group) for 30 s is closed as `unfinished` (a wipe or a boss already down still wins), a boss death seen before the sampler picked the boss still makes the fight a kill, and turning the monitor off drops a finished fight not yet sent. Needs AiPlayerbot.Observability.

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

The leader of a group that waits for a player posts one line in its faction's LookingForGroup channel (the dungeon, the open role, the level band, "w me inv"), again every 3 minutes, never more than one post per 30 s per faction across all groups, and only while a real player of that faction is online. A real player who whispers the leader "inv" gets a party invite when his faction, level and class fit the open role; other whispers get one short answer. Answers: "inv sent" when the invite goes out; "already sent you an inv" when he still holds the invite; "someone's joining atm, w me again in a min" while another invite is open; "sorry, we're full now" when the group stopped waiting; "sorry, can't group with hardcore chars" for a hardcore character; "leave your group and w me inv again" while he is grouped; "sorry, looking for 17-24" outside the band; "sorry, need a tank" when his class or talents do not fit the open role; "need tank for Deadmines (17-24), w me inv to join" as the hint for a whisper that is no invite; "couldn't inv you, got another invite open?" when the invite fails.

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

**Pull aggro estimate:** before each pack pull the run leader counts the creatures that would come with the pulled one, by the core's own wake-up rules: the pulled creature's whole pack (`creature_groups` that aggro together, `creature_linking` rows), every creature whose aggro radius reaches the spot the party pulls from (detection range 20 yd, widened or narrowed by the level difference to the lowest party member, plus a few yards of drift), and one ring of family assistance (same faction, `CreatureFamilyAssistanceRadius` but never under 20 yd, called from every member of the pulled pack's spawn rather than only the pulled creature: the Zul'Gurub terrace's cross-pack wakes are spawn pairs 11.0-19.5 yd apart). When more than [`AiPlayerbot.DungeonRunAggroCeiling`](#aiplayerbotdungeonrunaggroceiling) would join, the status note reads `N joiners expected` and the party rests to its end lines once for that pull (at most 60 s, not counted toward the step's idle ceiling, at most three a step; after that the note reads `N joiners expected, rest budget used` and the pull goes out). It adds no movement and never skips a pull; the estimate is approximate (it can be high or low); with `0` nothing is estimated. Apart from the estimate, every pull's status note reports the joiners it really drew (`joiners N (entry <entry> x<count>, ...)`, `joiners 0` when none) from the first 10 s of its fight; that needs no key. Only read while `AiPlayerbot.DungeonSelfClear = 1`. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#aggro-estimate).

</details>

### `AiPlayerbot.DungeonRunAggroRingYd`
How far a call for help carries in the ring estimate, in yards.
**Default:** `20` · **Values:** `5` to `40` · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

Only read while [`AiPlayerbot.DungeonRunAggroWalkRing`](#aiplayerbotdungeonrunaggrowalkring) is `1`. A creature already counted calls every creature of its own faction template within this many yards of itself, for two hops, so a neighbour on the far side of the party's own family-assistance radius still comes. The server's `CreatureFamilyAssistanceRadius` is used instead when it is larger. The measured wakes that needed this were 11.0-19.5 yd apart while the server's radius is 10. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#aggro-estimate).

</details>

### `AiPlayerbot.DungeonRunAggroWalkRing`
Makes the pull aggro estimate count what the party's walk to the pull, the stand and the fight point wake, and lets every creature already counted call its neighbours.
**Default:** `0` · **Values:** `0` off (today's estimate), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

Only read while [`AiPlayerbot.DungeonRunAggroEstimate`](#aiplayerbotdungeonrunaggroestimate) is `1`. The estimate reads the navmesh walk from the lead bot to the pull's firing spot (at most 64 samples, its end always kept), the spot itself and the fight point, each widened by the party's spread (3 yd, 10 yd in a raid), and every creature counted calls its neighbours within [`AiPlayerbot.DungeonRunAggroRingYd`](#aiplayerbotdungeonrunaggroringyd) for two hops. A pull row's own bodies ceiling (the plan's `max_bodies`, the pulled creature included) replaces the config ceiling for that row, so a deliberately sized pull never rests; over it the note reads `pull estimate: N bodies over the ceiling M (plan|config): 0x...`. On the last move row before a pull the note also says once how many the coming pull would bring (`ahead: pull N brings ...`; `ahead: pull N estimate unavailable` when the row lists no creature on the map). Advice only: no order changes and no pause. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#aggro-estimate).

</details>

### `AiPlayerbot.DungeonRunAuraTactics`
Turns on the aura-based boss tactic rows and the per-row health and bot-aura gates.
**Default:** `0` · **Values:** `0` off (the six aura kinds are not read and every gated row is closed, so bots play as before), `1` on (needs the boss tactics armed and the rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Aura tactics:** the AI boss tactics table's six newer kinds (see [Boss Tactics](../../play/boss-tactics.md#kinds)): `dispel_priority` ranks and holds cures by the fight's rows (a `never` aura excludes its whole dispel type, a `clear` aura waits until the carrier is clear of the group); `dps_hold_after` holds free DPS off a subject for a row's seconds after a pull, a health-band opening, a listed cast or aura, or a blink; `flee_on_aura` moves a carrier out of the group (or to the main tank); `freeze_on_aura` stands a carrier still and vetoes every action while the aura lasts; `taunt_after_cast` sends a rotation tank to taunt a subject back inside a deadline; `swap_on_aura` rotates two tanks at a stack count. The same key opens the row gates: a row whose `hp_min`/`hp_max` band or `bot_aura` does not match the fight is ignored while the key is off, which keeps every pre-existing row's behaviour. With the key off, the six kinds are not scanned at all. This is the switch a raid owner flips after checking the rows on a live boss; it changes target picks, cures and movement, so it ships off. See [Boss Tactics](../../play/boss-tactics.md) for each kind's rule and the shipped Zul'Gurub rows.

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

### `AiPlayerbot.DungeonRunLinkedDeath`
Turns on the "die together" boss tactic: free DPS bots equalise a linked group's health, hold every member at the row's percent and burn them together.
**Default:** `0` · **Values:** `0` off (DPS bots pick as before), `1` on (needs the boss tactics on and `linked_death` rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Linked death:** the AI boss tactics table's `linked_death` kind (see [Boss Tactics](../../play/boss-tactics.md)) marks a group that must die together: an `ai_boss_tactic` row lists one partner of the group, its `boss_entry` being the anchor, and its `param` a hold percent. While more than one member lives, every free DPS bot (never a tank or a healer) takes the living member with the highest health instead of the tank's victim, so the raid spreads its damage and the members stay level. A member at or below `param` percent is held — it is never the pick, the AoE density leaves it out, and an area cast or DoT that would reach it (within 10 yd of the action's target, or the bot) is stood down — while another living member stands above that. The veto ends with the hold, so AoE is allowed again once the group is released. A tank's own area actions (Thunder Clap, Consecration, Swipe) and the passive cleave toggles (Sweeping Strikes, Blade Flurry) get the same veto while a held member is in reach, and a free DPS bot whose current target is held stops attacking instead of nuking it; DoTs already ticking keep ticking. When every living member is at or below `param` percent the group is released: the release burn follows the row's `burn_order` (all DPS on the first living member of the list, then the next), and a release is latched once a member dies, so a heal or a raised member above `param` percent does not re-hold the group for its release window (`window_s`: 15 s for Thekal's trio, 7 s by default). A group without a burn order keeps the old released pick (the lowest). The first use is Zul'Gurub's High Priest Thekal (14599) with the Zealots Lor'Khan (11347) and Zath (11348) at 2 %: each raises a dead sibling to full health 7 s after the death (with an 8 s fallback when its chosen raiser dies first), so the trio has to go down at once. The burn order is the raiser chain, Lor'Khan -> Zath -> Thekal, so every death kills the next corpse's raiser before its check; Lor'Khan's Greater Heal is an `interrupt` row. This is its own key because it changes every free DPS bot's target pick; the rows may exist with it off. An explicit `attack` order from a player still wins; a raid mark wins unless it names a member the hold is keeping alive, so a skull on a held member falls through to the equalise pick. A run leader's guard pickups move only the tank it orders, not the raid; and while the key is on the raid guard leaves the group's members alone (an `attack` order outranks the pick and would kill one alone).

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

### `AiPlayerbot.DungeonRunNearWipeStop`
How many wipe events within the window pause a raid run once the fight is over.
**Default:** `2` · **Values:** `0` off, `1`-`10` events · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Near-wipe stop:** only read while `AiPlayerbot.DungeonSelfClear = 1`, and only for a raid binding (`AiPlayerbot.DungeonRunRaid = 1`) unless `AiPlayerbot.DungeonRunNearWipeStopParty = 1`. A *wipe event* is one mark: it starts on the tick the party first reaches the wipe level (a raid: `ceil(40 % of the members)`, at least 3, or every tank dead, or more than half the healers dead), every bot dead, or a window wipe, and ends when none of those hold, so a near wipe that becomes a wipe and the revive after it are one mark. The marks older than `AiPlayerbot.DungeonRunNearWipeStopMinutes` are dropped; at this many the run stops once the fight is over (out of combat, or only a GameMaster pinned in a fight), before any regroup. During the fight the note reads `11 dead: fighting on, the run stops after the fight (2 near wipes within 10 min)`; the pause reads `14 dead: 2 near wipes within 10 min (steps 21 and 32, 5 min 50 s apart; 2 near wipes this run): the run stops here. Regroup (cmd revive party), then .bot run resume, or .bot run stop`. A fight that goes past its ceiling with the stop due pauses with the same line and `; the fight went past 180 s`. `.bot run resume` clears the marks. Zul'Gurub walk 2 (2026-10-29): near wipe 1 began at display 21, near wipe 2 at display 32, 5 min 50 s later; the second regrouped by itself (`1 of 2 at this step`) and the run walked on into the wipe at display 35. Party runs keep their self-regroup: the 2-in-10-min stop would turn many recoverable party runs into pauses, so it is opt-in for them.

</details>

### `AiPlayerbot.DungeonRunNearWipeStopMinutes`
The near-wipe stop's window, in minutes of running time.
**Default:** `10` · **Values:** `1`-`60` minutes · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**The window:** a mark older than this many minutes of the run's own clock is dropped before the count is compared, so two events exactly this far apart still count and one millisecond more does not. The clock counts running time only, so a pause does not age the marks. Only read while `AiPlayerbot.DungeonRunNearWipeStop` is above `0`. See [`AiPlayerbot.DungeonRunNearWipeStop`](#aiplayerbotdungeonrunnearwipestop).

</details>

### `AiPlayerbot.DungeonRunNearWipeStopParty`
Whether a party binding gets the near-wipe stop too.
**Default:** `0` · **Values:** `0` raids only (as before), `1` parties too · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Party opt-in:** in a party the wipe level is 3 of 5, and the self-regroup is the designed recovery that runs often in unattended runs; the per-step cap (2 regroups a step) already pauses a step that keeps failing. Nothing in the party evidence called for the stop: in walk 2 the trigger was the raid's costly 8+ dead events. So a party gets it only with this key. Only read while `AiPlayerbot.DungeonRunNearWipeStop` is above `0`.

</details>

### `AiPlayerbot.DungeonRunPackClearMarginYd`
The band around a pull step's radius in which a living unit of its entries still counts.
**Default:** `10` · **Values:** `0` off (the old radius-only rule), `1`-`30` yards · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Pack-cleared gate:** only read while `AiPlayerbot.DungeonSelfClear = 1`. A `pull` step is done only when nothing of its entries is left: no living unit in its radius; none in the band outside it (2D distance within radius + this margin, height within 10 yd of the step point, so a unit on another floor is no residue) unless another step's radius holds it; no waypoint patroller or formation follower of one; and no unit that spawned inside the radius still walking its loop (spawn ownership: its DB spawn row lies within radius + 1 yd, so it belongs to the step wherever its patrol has taken it, is listed up to 60 yd out, and is not left to a later step). A residue unit is pulled nearest first, up to `AiPlayerbot.DungeonRunPackClearRepulls`; then the run pauses with the units' entries, guids and distances. A residue unit whose pulls drew no fight is named in the clear note (`; 1 left within 30.5 yd that pulls drew no fight (11368 0x…)`); one only evading or stealthed is waited for (`waiting for the pack to settle (…)`). With `0` every part is off and the old rule runs byte for byte; a `boss` step is unchanged. A step cleared with no pull at all names its nearest unlisted unit (`; nearest of its units left: 14750 0x… 64.0 yd from the centre`). Zul'Gurub walk 2, display 31: the bat pack's leader was 52.1 yd out on its loop and its formation bats are not waypoint movers, so the step said `pack cleared` in 1 s with 3 bats and the rider alive, and the next move walked into them (14 dead).

</details>

### `AiPlayerbot.DungeonRunPackClearRepulls`
How many residue pulls a pull step may send before it pauses for a human.
**Default:** `3` · **Values:** `1`-`10` pulls · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**The cap:** each residue pull (the band's units, nearest first, with the same way back and `pullback` as the step's own) counts one. At the cap with residue still open the run pauses with `pack not cleared after 3 re-pulls: 2 left within 30.5 yd of the pull centre (11368 0x… 23.2 yd, …): clear it, then .bot run resume, or .bot run skip`. Only read while `AiPlayerbot.DungeonRunPackClearMarginYd` is above `0`. See [`AiPlayerbot.DungeonRunPackClearMarginYd`](#aiplayerbotdungeonrunpackclearmarginyyd).

</details>

### `AiPlayerbot.DungeonRunStragglerPull`
Pulls a pack unit or a patrol that the step waited for and did not come, instead of pausing the run.
**Default:** `0` · **Values:** `0` off (the run pauses as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**The straggler pull:** only read while `AiPlayerbot.DungeonSelfClear = 1`. Added 2026-11-03 for Zul'Gurub walks 32 and 33: the Arlokk pack paused with `pack not cleared: 11365 0x… of this pack (spawned 12.7 yd from the centre) is 12.7 yd out and did not come within 60 s`, and Jeklik's bat rider 14750 held its stand-pull row with `patrol pull: 14750 0x… did not come within 28.5 yd of the stand`, each until a human pulled it. With `1`, a unit a `pull` step waits for that did not come (a spawn-owned patroller outside the radius) is walked to and pulled by the acting tank after the 60 s wait: the lead bot while it lives and is a tank, else the off-tank that holds the main tank role. The tank walks to the unit and sends `attack <guid> <tank>`, the rest of the party staying with the lead bot at the stand. Each try has 45 s; a failed try — a refused order, a way with no path, a blocked way or the 45 s run out — goes to one untried off-tank, and only then the run pauses as before with the reason in the line. The way is read again right before each movement order and every 2 s while the tank walks or waits at the point: another pack not pulled yet that comes within its aggro reach plus 5 yd of the remaining way aborts that tank's try exactly like a blocked way, and a way that no longer has a path does the same. A stand-pull row's patrol is waited for one full loop of its own waypoint path at its walk speed (90 s when the loop cannot be read) instead of the row's 600 s, then the tank walks to the nearest point of that loop on the party's side and pulls it from there. A boss, a `kill_first` unit and a `linked_death` unit keep the old behaviour. One `TBM:RUN` note per try says which tank walks where.

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

### `AiPlayerbot.DungeonRunRaidBossRules`
A raid binding's boss fight rules: a longer fight ceiling, deaths that do not pause a fight still being won, and the adds sent to the off-tanks.
**Default:** `0` · **Values:** `0` off (a raid's boss fight keeps the dungeon rules), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Raid boss fight rules:** only read with `AiPlayerbot.DungeonRunRaid = 1`, on a raid binding. Added 2026-10-30 from Onyxia walk 1 (2026-10-28: the community's kills ran 408-806 s, past the 420 s boss ceiling, and the whelps took the healers while the main tank held her). A raid's boss fight is a `boss` step, or a plan boss engaged on any step, outside a scripted event. With `1`: its fight ceiling is 1200 s instead of 420 s, and once a fight is a raid boss fight it stays one until it ends, also while the boss cannot be attacked for a phase; past the ceiling, a member's death in the last 60 s does not pause the fight while a plan boss still lost hp in that window (adds dying do not count; a fight with no hp loss in 60 s still pauses, and the 1800 s hard cap still pauses); the raid's wipe level still regroups, and a near-wipe stop that is already due still pauses, past 420 s, as without the rules. The mobs that hit the raid (in melee reach of their victim, or standing and casting, within 100 yd of the lead bot) go to the living off-tanks (tanks other than the lead bot, within 100 yd of it, or anywhere on its map while it is dead, and out of a commander task): one on a healer at once, one on anyone else after it has hit for 4 s, as the dungeon guard waits; a caster hitting the raid from its own camp is left to the DPS. One `cmd attack <guid> <off-tank>` goes at a time, no sooner than 2 s after the last order and at most twice at the same mob in 20 s, with no 12-orders-a-fight cap. The off-tank is never one holding a plan boss; an off-tank nobody hits comes before one holding an add, in turn among them. A mob an off-tank holds already is left to it, and the boss is never one of them. The main tank (the lead bot) is never sent off the boss: on `stay` it is still sent at a mob on itself, and while it holds a plan boss only at her. An idle main tank on `stay` that holds no plan boss rejoins a plan boss an off-tank holds, after 8 s of the off-tank holding it and only within attack range (55 yd): one order per boss, 20 s apart, and never while it holds a plan boss itself. With the lead bot dead, the raid's attackers are read around a living off-tank (or another living bot); with it dead or no tank, the boss on a raid member who is no tank (the lead bot too) is taken by an off-tank at once. With no off-tank in range the dungeon guard applies, except that the main tank holding a plan boss is not sent at an add. With `0` the boss fight keeps the 420 s ceiling, a death in the window pauses it past the ceiling, and the guard sends the main tank. With `AiPlayerbot.DungeonRunRaidHealerGuard = 1`, the adds on a healer go to the nearest ranged dps first (see below). See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonRunRaidHealerGuard`
A raid's adds on healers (boss and trash fights): sent to the raid's ranged dps, not to the off-tanks.
**Default:** `0` · **Values:** `0` off (the off-tanks take the healers' adds), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Healer guard:** only read with `AiPlayerbot.DungeonRunRaid = 1`: inside a raid boss fight with `AiPlayerbot.DungeonRunRaidBossRules = 1` whose main tank (the lead bot) holds a plan boss, and inside a raid trash fight whose main tank held its current target when the guard turned on (the fight keeps the guard once it did, also while that target dies). ZG walk 26c (2026-11-02): at High Priestess Jeklik the off-tank Rodell was sent at Bloodseeker Bats (11368) four times while the main tank held her, the healers Butterfly and Tobian died three and two times in the run, and the raid wiped with the boss at about 4 %; most of the run's healer deaths were in the trash pulls before her (steps 10 and 33), where the off-tank was spent on bats off the healers. With `1`: an add that attacks a healer goes to the nearest ranged damage bot of the raid (a ranged bot that is no tank and no healer, and not one busy with a commander task) within 35 yd of the add, up to two per add, nearest first; a further order waits 2 s after the last one, so the guard never orders every tick. A bot farther from the add than that is no help with it and is not picked. An order counts only once the bot took it: one refused (the add beyond the bot's sight, say) covers nothing, and the next pick moves on to the next ranged bot. An assignment lasts while the bot's current target is the add; one whose target is not the add is over after 6 s, its bot is passed over for that add like a refused one, and the add has another ranged bot (or the off-tanks) instead of a bot that never went for it. The bot's own target choice returns it to the boss once the add dies. The add's pick passes over what the off-tank path does: a plan boss, an add the run already holds (a crowd-control order in flight, a run cc hold, or the fight's `kill_first` focus), a caster hitting from its own camp that the tank would walk to, and an add that had two orders this fight until 20 s after its last one. The off-tanks take such an add only when no ranged dps is free for it, and an off-tank holding the boss (the boss's victim) is never sent at an add; in a trash fight an off-tank holding its own add is kept there, and the round robin passes it over. A trash fight's main tank is not sent at a healer's add while it holds its target: with no ranged dps free for it and no free off-tank the add waits 8 s from when it went loose (the spacing after a recent order and the guard's budgets do not hold that clock back), and then the main tank takes it, at most once an add and three times a fight, only while its own target is an ordinary trash mob it can leave (not a plan boss, not the fight's kill_first focus); otherwise the add stays where it hits. Every add on a healer is checked, not only the nearest one, so a nearer add the ranged dps already cover does not hold a farther one back. Adds that attack another bot or a pet are left to them, so the off-tanks keep their post. The main tank losing the boss closes the guard at once (a pending order is dropped); a trash fight's guard stays on for the rest of the fight. Two limits are known: an off-tank whose add stands more than 40 yd from the main tank is outside the guard's read, so it looks free and can be sent at another add; and a healer's order goes out before the walk and swarm joins, so one of those can be held back by a 2 s spacing. With `0` the off-tanks take every add within 100 yd in turn, as before.

</details>
### `AiPlayerbot.DungeonRunBossSpread`
Whether the boss tactics widen the ranged raid's spacing while a `spread` row's boss fights.
**Default:** `0` · **Values:** `0` off (the ordinary 10 yd spacing, and a stay order keeps its bot in place), `1` on (needs the boss tactics and the rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Boss spread:** the AI boss tactics table's `spread` kind (see [Boss Tactics](../../play/boss-tactics.md#kinds)): while a row's boss is in the fight the ranged bots and the healers keep the row's `param` yards apart instead of the ordinary spacing, and a stay order no longer exempts a bot from spreading. The row's spell only documents the splash the spacing answers (Hakkar's row: Corrupted Blood 24328 hits 10 yd around a random attacker, the row keeps 12). Melee and tanks never spread.

</details>
### `AiPlayerbot.DungeonRunCharmYield`
Whether a bot a creature charms yields its AI, and area actions stand down near a charmed group member.
**Default:** `0` · **Values:** `0` off (bots play as before), `1` on (needs no rows or tactics) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Charm yield:** while the key is on, a bot a creature has charmed yields its AI to the charmer (no engine runs, no move goes out), so nothing it does fights the core's own charm control; a bot a player charms is the core's business and is left alone. On any map, while a group member has a non-player charmer, every area action (class area damage, thrown explosives, a tank area such as Thunder Clap, the Sweeping Strikes and Blade Flurry toggles, and damage-dealer area damage) whose target point — or the bot itself, for a self-centred one — lies within 10 yd of such a member stands down. Bots never take a charmed group member as a target, and healers keep healing: a hostile charmed tank cannot be healed or targeted by them anyway. Hakkar's Cause Insanity 24327 is why (his victim fights the raid for 10 s), and the same yield covers Will of Hakkar 24178, which only the Zul'Gurub trash-pile Guru casts.

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
### `AiPlayerbot.DungeonRunPullGate`
Whether the run leader honours the boss tactics' `pull_gate` rows: it never pulls a gated boss that wears its aura, and ends the run after the row's seconds.
**Default:** `0` · **Values:** `0` off (the run pulls as before), `1` on (needs the rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Pull gate:** a boss tactic row of the `pull_gate` kind (see [Boss Tactics](../../play/boss-tactics.md#kinds)) names the boss itself (`target_entry` 0), the aura it must not wear (`spell_id`) and the most stacks allowed (`priority`). While the living boss wears more, the run leader sends no pull and notes the gate (`boss gate: <entry> wears <spell> x<stacks> (<max> allowed); not pulling`); after the row's `param` seconds it ends the run instead of wiping on it. Hakkar's row: Hakkar Power 24692, 0 allowed, 30 s — each living High Priest leaves a stack on him, and with five up the Aspects wipe the raid in under 40 s. The gate also covers the run's own stand-pull boss engage; a planned boss the gate does not cover is pulled as before.

</details>

### `AiPlayerbot.DungeonRunStandClearance`
Makes the pull stand keep clear of every other live pack, its patrol loop included.
**Default:** `0` · **Values:** `0` off (the stand is picked as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Stand clearance:** only read while `AiPlayerbot.DungeonSelfClear = 1`. It applies to every firing stand the run leader picks: an authored stand-pull step's own pull (this key is what gives that step a stand rule at all: with both keys off it pulls as before, and a hop or walk-back pull of the step never takes one) and a generic stand pull's (with `AiPlayerbot.DungeonRunPullStand = 1`). Added 2026-11-02 for Zul'Gurub's Mar'li leg (walk 26b: the tank's firing stand for pack 1 stood 35.3 yd from the nearest pack-1 spawn but 20.0 yd from the loop of pack 0's patrolling leader 49202, which woke the pack as the party gathered). The stands the tank's navmesh path to the target offers are tried in the same order as before (the ranged reach's own point first, then the path's points nearer the target), and the first one clear of every other live pack is taken. Clear means at least the pack's aggro reach (its detection range minus the party's level advantage over it, the core's own rule) plus 4 yd away from it: the bots stand at the stand point, so only the pack's reach and the line of sight the core needs decide whether it can wake on the stand. A pack that patrols is measured along its whole loop (`creature_movement` / `creature_movement_template`, the path its movement generator walks; a loop the server does not hold, as a scripted escort path, stands in by the pack's spot plus 10 yd), so a stand beside a leg it will walk is refused too; a pack already in combat is not checked. The pack the pull itself takes (the step's own living units within its radius and everything walking in their formation) is not another pack, and neither is a creature its call for help takes (the core's 10 yd family assistance radius, one call out: only a creature that can answer it, by the core's own no-assist, control, invisibility, evasion, faction and line-of-sight conditions). On a boss step the boss fight is no other pack either, but only what the spawn proves: the boss himself, the creatures he summoned, the creatures the core links to him or that walk in his formation, and the ones already in a fight are not checked. A creature of one of his add entries is not excused by its entry alone (the bats Jeklik summons, 11368, are ordinary Zul'Gurub trash elsewhere), so only the summoner, the linking row and the formation are evidence. The stand must also stay the pulled pack's widest aggro reach away from every unit of it, so the party does not wake the pack on the way: a stand inside that reach is refused too. A unit that stays put more than 8 yd above or below the stand, with no line of sight to it, is on another storey and is not checked either (the Arlokk terrace's band: the floor between takes its sight as well as the party's); a patroller is judged over its whole loop's height range instead (the lowest and the highest point both out of the band are what excuses it), and no sight line is read for it (a point it has not reached yet), so a patrol now on another storey whose loop comes down to the stand's floor is still checked. The clearance is advisory: with no clear stand the tank walks to the stand the key is off for (the path's first candidate) and the note reads `stand_pull: no clear stand for <entry> (closest <entry> at <yd>); using the default stand`. Only a patrolling pack that alone refuses the reach's own stand is waited for, up to 20 s, to walk clear of it (then that stand is walked to); any other blocker, or the wait spent, goes on at once. The run never pauses or holds a pull for the clearance. The reading is one grid search around the lead bot, a path lookup per patroller and a sight line per refused stand and blocker on another storey, only on the tick a stand is read.

</details>

### `AiPlayerbot.DungeonRunStandPullPreCc`
Sends a stand-pull step's crowd control from the last point of the approach walk that is outside the pulled pack's own aggro range, before the party walks in.
**Default:** `0` · **Values:** `0` off (the walk in is as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Crowd control before the walk in:** only read while `AiPlayerbot.DungeonSelfClear = 1`. Added 2026-11-02 for Zul'Gurub's Mar'li leg: a stand-pull step's approach can lie inside the pulled pack's own aggro range (Mar'li's pack-1 approach is 15.0 yd from the Venombrood 49254, whose radius at the plan's lowest level is 22 yd), so the party woke the pack on the approach walk and the step's three Polymorphs went out 18 s later, into a pack already on the party. With `1`, the move row right before a stand-pull pull or boss row (the same row `stand_pull` reads its stand from) ends at the last point of the lead bot's navmesh path to the plan's approach that stands outside the pack's wake reach: the widest aggro reach of the pack's living units (detection range minus the party's level advantage over it, the aggro estimate's rule) plus the party's 10 yd spread plus 1 yd. The pack is the next row's own: its entries' spawns in its radius, by creature formation, alive or dead, wherever their loop has taken them. At that point the step's crowd control (`cc_priority` marks) goes out, the party waits up to 6 s for the marks to read held (or for the cast's time to pass), then walks to the plan's approach, reads its firing stand from there and pulls: the pack wakes after the marks, not before them. A walk with no such point (an approach inside the pack by design) walks the plan's point as before and the note reads `stand_pull: no pre-cc point for <entry>`; a point the glue has not read within 3 s is dropped the same way, so a read never holds the run. A stand-pull step whose approach is already clear, every other step and the key off are unchanged.

</details>

### `AiPlayerbot.DungeonRunStandDiscipline`
Fires a pull row from the plan's authored approach only, and waits for the tanks and melee to stand there (or clear of the next pack) first.
**Default:** `0` · **Values:** `0` off (the old stand search and pull), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**The plan's stand is the only stand:** only read while `AiPlayerbot.DungeonSelfClear = 1`. The move row right before a pull or boss row is the plan's own stand, whether or not the row is marked `stand_pull`. With `1`, a tank that cannot shoot the pulled unit from that stand (out of sight, or out of the shot's range) does not search a closer firing stand on its navmesh path: it walks back to the plan's approach if it drifted off and holds there, and the status note reads `no LOS from the authored stand; holding`. That hold lasts up to 20 s for each unit; then the tank searches a firing stand as it does with `0`, and the walk back to the plan's stand does not resume for that unit. The key works without `AiPlayerbot.DungeonRunPullStand`. Zul'Gurub's Arlokk terrace is why: the plan's upper-B approach had no line of sight to its target, the stand the tank's path offered stood 8.7 yd from the target and 24 yd nearer the middle packs than the authored approach, and those packs woke through the open ring as the melee walked in. Before the row fires, every living tank and melee (a member that is not a healer and not a ranged bot) must stand within 5 yd of that approach or be clear of the next pull's pack: farther than the next pull or boss row's radius plus the core's 20 yd aggro ring from its point. The run waits up to 10 s for them; then the row fires and its note ends in `out of place: <names>`. Zul'Gurub walk 28 step 323 is why: the off-tank was still 3 yd from the middle-west pack when the main tank pulled the lower pack, and that pack woke with no bot closer than 30.6 yd at the spawns. A hop pull and a walk-back pull never wait, and with `0` the stand search and the pull are exactly as before.
</details>

### `AiPlayerbot.DungeonRunAutoCc`
Marks crowd control on a pack from what the raid actually has, instead of only the plan's `cc_priority`/`cc_always` entries.
**Default:** `0` · **Values:** `0` off (the plan's list only, as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Automatic crowd control:** only read while `AiPlayerbot.DungeonSelfClear = 1`. Before a pull that brings more bodies than [`AiPlayerbot.DungeonRunAutoCcAbove`](#aiplayerbotdungeonrunautoccabove) (the row's own `max_bodies` lowers the threshold when it is smaller), the run marks the row's own pack, one order a tick, until the unmarked bodies fit the threshold. The order is casters and healers first, then the plan's rank (`cc_always`, then `cc_priority`, as a hint now, not a gate), then the class family's reliability (Polymorph, Shackle Undead, Banish, Hibernate, Seduction, Freezing Trap, Sap, Repentance, and the risky Fear and Scare Beast last), then the higher level, then the unit farther from the pull target. The families are the classes' own `* on cc` rows: a druid's Hibernate, a hunter's Freezing Trap, a warlock's Seduction with a live succubus, a paladin's Repentance. Never marked: the pull target, the tank's selection, a unit an off-tank holds, the fight's focus, a `kill_first` unit, a held unit, a unit the party hits, and an add the raid's healer guard has a bot on. In the fight the run marks a joiner while more unheld units fight than the threshold, two seconds apart, and re-sends a mark whose aura broke (twice a unit a step; a bot that died frees its hold). A tank and the lead bot are never crowd-control bots; a healer only while 4 healers (1 in a party) stay free, and it holds one unit. At most 24 auto orders a step pass. With the key on the run uses seven marks (moon, square, diamond, triangle, star, circle, cross; the skull stays the fight's focus); a player's icon is never moved.

</details>

### `AiPlayerbot.DungeonRunAutoCcAbove`
How many bodies a pull may bring before the automatic crowd control starts.
**Default:** `6` · **Values:** `2` to `40` (crowd control starts at the body after it; `6` starts at 7) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Threshold:** the run's body count is the ring estimate's count when it read the pull (`AiPlayerbot.DungeonRunAggroEstimate` and `AiPlayerbot.DungeonRunAggroWalkRing`), else the old joiner estimate with the target, else the row's own living pullable units. The threshold is the smaller of this key and the row's ceiling (`max_bodies`), so a deliberately sized pull still gets crowd control. Only read while [`AiPlayerbot.DungeonRunAutoCc`](#aiplayerbotdungeonrunautocc) is `1`.

</details>

### `AiPlayerbot.DungeonRunAutoCcRisky`
Lets the automatic crowd control use the fear families (Fear, Scare Beast).
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Risky families:** a feared mob runs. A fear family is only picked with this key on, with at least 35 yd between the unit and the nearest creature the read knows outside its pack, and only when no other family has a free bot for the unit. Only read while [`AiPlayerbot.DungeonRunAutoCc`](#aiplayerbotdungeonrunautocc) is `1`.


</details>
### `AiPlayerbot.DungeonRunSeekAura`
Whether the boss tactics keep their `seek_aura` rows: the raid gathers in a dying source's cloud before the boss's cycle spell lands.
**Default:** `0` · **Values:** `0` off (the rows are ignored, as before the kind existed), `1` on (needs `AiPlayerbot.DungeonTactics`, or a commander-bound group) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Seek aura:** a boss tactic row of the `seek_aura` kind (see [Boss Tactics](../../play/boss-tactics.md#kinds)) names the source creature whose death leaves the cloud (`target_entry`), the aura the raid must carry (`spell_id`), and the boss spell whose aura on a group member restarts the cycle clock (`cycle_spell`, every `cycle_s` seconds, the dose window opening `window_s` before it is due). The source is held and killed at the row's spot (`spot_x/y/z`): the DPS stop at its `priority` health % until the gather is ready (every seeker within `param` yd of it, 8 s at most, the burn forced near the end of the window), then burn it so the cloud lands on the gathered raid. A bot outside the fresh cloud walks in while it still lives; the cloud is exempt from every ground-hazard avoid while its row runs. Hakkar's row: one Son of Hakkar 11357 dies at B every 90 s before Blood Siphon 24324 so the raid carries Poisonous Blood 24321; the main tank and the first off-tank never seek, and the second off-tank pulls and holds the source.

</details>

### `AiPlayerbot.DungeonRunUseObject`
Whether the boss tactics keep their `use_object` rows: one claimed bot clicks the boss's game object at the right moment, and mode `tank_anchor` also walks the boss over to it.
**Default:** `0` · **Values:** `0` off (the rows are ignored, as before the kind existed), `1` on (needs `AiPlayerbot.DungeonTactics`, or a commander-bound group) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Use object:** a boss tactic row of the `use_object` kind names a game object to click (the row's target entry), the aura on the boss that calls for a click or a repeat window in seconds (or both), the clicker's role (free damage, healer or any non-tank), how far the boss may be from the object when the click goes out, and optionally the mode `tank_anchor`. The group picks one clicker by itself — the eligible bot nearest the object that is nearest the boss, the lowest guid on a tie — and the fight keeps the claim in one shared record: one clicker at a time, a claim nobody acted on for 30 s is released, at most two attempts per object and at least 8 s between them. The clicker walks in 15 s before the row is due and holds at the object until the boss stands still inside the row's range; only then does the click go out (the effect lands several seconds later, so a moving boss wastes it). With `tank_anchor`, the tank the boss is on walks the boss to a point short of the object during that same lead, so the click lands in range; the anchor never runs while the boss is on someone else, and it stays below every survival move. With this key off the rows are not read at all; with it on an Ossirian crystal row, the old crystal runner stands down. Logged to `bot_events.csv` as `BossTacticsUseObject` (claim, walk, click, skip) and `BossTacticsAnchor`.

</details>

### `AiPlayerbot.DungeonRunWalkBackRescue`
Whether the run leader teleports a stuck walk back's lead bot (and a placed run's lead bot revived behind its start step) to a safe point on the run's way.
**Default:** `1` · **Values:** `0` the old pause, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Walk back rescue:** only read while `AiPlayerbot.DungeonSelfClear = 1`. Added 2026-10-23 (Razorfen Kraul r27 slot 8: three walk backs climbed to a dead-end ledge and paused stuck; Gnomeregan r27 slot 4: a run placed at step 543 walked back from the entrance for 23 min). With `1`, a walk back whose lead bot stays stuck short of its point after the walk's tries is not paused: the lead bot is teleported (the stranded lead bot's rescue, the module's own teleport) to the last point it reached on the plan, or, when a live pack is near that point, to the latest point before it (back to the run's start step, never back over a direct step) out of every live pack's reach (its radius plus 10 yd), and with no such point the run pauses with that reason instead; and a placed run's lead bot that is up behind the run's start step (the plan point nearest it lies before the start step and none of the run's own points is within 60 yd) is teleported at once to such a safe point on the way it came in a party with no real player (with one, to the run's first such safe point). Out of combat only, once a step, three failed tries a step; the party left behind is summoned to it only out of combat. The walk back goes on from that point. A death that paused the run past its regroups by itself armed the rescue again for the walk back after the resume. A walk-back point with no walkable path from the spot where the hop off a navmesh island found no spot was rescued the same way in a party with no real player (Dire Maul North r31: revived on the entrance island, every point passed to a pause); with a real player in the party the walk back went on with him instead. In a party with no real player, a straight walk back that failed and whose replay of the plan's points would have walked 300 yd or more before that safe point was rescued instead of replayed (Stratholme r29, Maraudon r28b). A backoff of the straight walk whose plan points from its new target would have walked 300 yd or more before that safe point was rescued the same way (Shadowfang Keep r36: the second backoff landed on the entrance point where the lead bot was revived, and the walk back walked 307 yd of points without a rescue). See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.RunTeleportReturnYd`
How far (yards) a party bot may be from the lead bot before the run leader brings it back by a teleport that drops its combat, instead of the summon portal or a walk back.
**Default:** `0` (off; recommended `120`) · **Values:** `0` off (the summon portal, and a walk back when it fails), any distance in yards · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Teleport return:** only read while `AiPlayerbot.DungeonSelfClear = 1`, and only in a party with no real player. Added 2026-11-03 (Zul'Gurub walk 29: after each wipe the bots were revived 300-650 yd from the raid at the entrance of a placed walk, fought the packs there from the first second, and 57 of them died walking back through packs the walk never cleared; the summons skipped every bot in a fight, and the portal silently dropped the ones that started a fight in its 3 s). A bot the run brings back from farther than this comes by the run's own teleport: its combat and its pet's are dropped (off every threat list), then it is moved at once to the lead bot, in a fight or not. That covers a fallen bot alive again far away, a bot left behind after the stranded lead bot's rescue teleport, a raid member wedged on another floor, and any summon the portal dropped (the run now hears of a dropped summon). When the lead bot stands in the reach of a pack not pulled yet (Zul'Gurub walk 29 run d: all 20 were summoned onto the live Jin'do pack and wiped there again), the bot lands at the nearest point beside the lead bot, on its floor and in its sight, outside every such pack's reach. A bot not within 10 yd of that point 5 s later is sent again; after three tries the run's note names it (*Name is still N yd from the lead bot after 3 teleport returns in 15 s: bring it to the lead bot*), and it is never told to walk back. Not toward a dead lead bot or one in another part of the instance, not while the lead bot's own rescue teleport is due, not with a plan boss engaged. The stranded lead bot's summons now count a bot as brought only once it stands within 10 yd of the lead bot. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

</details>

### `AiPlayerbot.DungeonReviveHoldYd`
How far (yards) from the lead bot a run's bot may be revived before it stands still and waits to be brought back instead of walking.
**Default:** `0` (off; recommended `100`) · **Values:** `0` off, any distance in yards · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Known limit:** a held bot takes no hazard escapes either: while the hold is up it does not step out of a listed boss cast (`avoid_cast`), an area effect or a bomb runout. It waits where it stands and the run is what gets it out; a hold only lasts 60 s at most.

**Waiting to be brought back:** only read while `AiPlayerbot.DungeonSelfClear = 1`, for the bots of a run (`.bot run`); outside a run nothing changes. Added 2026-11-03 (Zul'Gurub walk 29: bots revived by hand far from the raid, with the run paused, walked the navmesh back and pulled every pack on the way). A bot of a run revived inside a dungeon farther than this from the lead bot (any revive: the run's, a player's or a GameMaster's, with the run running or paused) stands where it is for up to 60 s, waiting for a summon or a teleport. So does every bot the run has decided to bring back (a teleport return, a summon of a revived or wedged bot, a bot left behind after the lead bot's rescue teleport, a summon the portal dropped and that waits for its retry) and any bot with a summon pending from its commander, until it is within 10 yd of where it is brought. While it waits it does not follow, walk or chase; it fights back only from where it stands. After 60 s without arriving it is released and the run's note says so (*Name waited 60 s N yd from the lead bot for its summon or teleport: released*). A paused run leaves its own returns to the bots' own AI; the revive hold above still applies. See [Dungeon Run Leader](../../play/dungeon-run-leader.md).

**Known limit:** a held bot takes no hazard escapes either: while the hold is up it does not step out of a listed boss cast (`avoid_cast`), an area effect or a bomb runout. It waits where it stands and the run is what gets it out; a hold lasts 60 s at most.

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

### `AiPlayerbot.RaidEntryByCommander`
Waives a bot's failed entrance condition at a raid entrance that has no area-trigger script, when the commander who bound it is a real player who commands the bot's raid and meets that condition himself.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Some entrances ask for something a bot character never has. Onyxia's Lair asks for the Drakefire Amulet, so a bound bot raid was refused at the entrance even while its commander stood inside with the amulet.

With `1`, `.bot cmd enter` reads the entrance's condition for the commander too. A bot whose only failed check is that condition goes in when all of these hold:
- the entrance leads into a raid: a five-player dungeon's condition is never waived;
- the entrance trigger runs no area-trigger script (no script id is bound to it);
- its commander is a real player (not a bot) with a character in the world;
- he commands the bot's raid: he is a member of the bot's raid group when the bots stand at the entrance, or the commander of the bot's binding (`.bot commander bind ... raid` takes him out of the group, so a bound raid never has him listed as a member);
- the entrance's condition is met for him.

The other entrance checks are never waived: the content phase, the level, the raid group and the per-account instance limit still refuse the bot, and the core's own player teleport still checks the instance (full, locked by an encounter, the map's own refusal). A scripted entrance is refused as if the key were off: that fallback teleport skips the trigger's script, so a zone or instance script that denies entry on the trigger itself is not re-checked by it (only the map's own admission checks are).

The commander's condition is evaluated once per bot at the moment the bot sends the entrance trigger. With `0`, the bot's own condition decides, as before.

</details>

### `AiPlayerbot.FankrissSwapStacks`
Mortal Wound stacks at which an opt-in `fankriss tank` taunts Fankriss off the current tank.
**Default:** `5` · **Values:** `1`-`9` (clamped) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Only tanks given the `fankriss tank` strategy act on it ([Fankriss roles](../../play/dungeons-and-raids.md#fankriss-opt-in-roles)). When the boss's current tank carries at least this many Mortal Wound stacks, one assigned tank with **zero** stacks and a ready Taunt (warrior) or Growl (druid) taunts; the lowest character guid wins a tie, and a failed or resisted taunt is retried after 2 s. If Fankriss is attacking a raid member who is not a tank, an assigned clean tank taunts at once. Setting the key does not enable the role.

</details>

### `AiPlayerbot.PatchwerkOpeningDelay`
Seconds the DPS hold Patchwerk after an opt-in `patchwerk main` engages him.
**Default:** `4` · **Values:** `0` off, else `3`-`5` (clamped) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Only acts in a raid where a tank has the `patchwerk main` strategy ([Patchwerk roles](../../play/dungeons-and-raids.md#patchwerk-opt-in-roles)). The clock starts when the main's Patchwerk enters combat and is kept for the whole encounter, so a DPS that joins late is not held again. During an ordered pull the DPS also wait while the main walks in. Heals and buffs are never held. Setting the key does not enable the roles.

</details>
