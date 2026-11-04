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
| [`AiPlayerbot.DungeonRunAuraTactics`](#aiplayerbotdungeonrunauratactics) | `0` | `1`: the boss tactics read the aura rows (`dispel_priority`, `dps_hold_after`, `flee_on_aura`, `freeze_on_aura`, `taunt_after_cast`, `swap_on_aura`) and honour the health-band and bot-aura row gates. |
| [`AiPlayerbot.DungeonRunBotKeysForPlayers`](#aiplayerbotdungeonrunbotkeysforplayers) | `0` | `1`: the bots carry the dungeon keys and open the doors for a party with real players too; `0`: players bring their own key, and the bots stop at the door and ask. |
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
| [`AiPlayerbot.DungeonRunPlayerLed`](#aiplayerbotdungeonrunplayerled) | `0` | Lets a player who leads his own party start a player-led run (`.bot run start ... player`): he walks, the run leader pulls the packs near him. |
| [`AiPlayerbot.DungeonRunPlayerLedAwayMinutes`](#aiplayerbotdungeonrunplayerledawayminutes) | `10` | How long the leading player of a player-led run may be out of the plan's dungeon before the run stops. |
| [`AiPlayerbot.DungeonRunPlayerLedPullYd`](#aiplayerbotdungeonrunplayerledpullyd) | `40` | How close the leading player stands to a plan pack or boss for the player-led run to pull it. |
| [`AiPlayerbot.DungeonRunRaid`](#aiplayerbotdungeonrunraid) | `0` | Lets the run leader bind the commander's raid of 6 to 40 bots and runs it with the raid rules (needs `AiPlayerbot.DungeonSelfClear` and `AiPlayerbot.RemoteCommander`). |
| [`AiPlayerbot.DungeonRunRaidBossRules`](#aiplayerbotdungeonrunraidbossrules) | `0` | `1`: a raid's boss fight runs up to 1200 s, survives its deaths while the boss still loses hp, and sends its adds to the off-tanks instead of the main tank. |
| [`AiPlayerbot.DungeonRunRaidHealerGuard`](#aiplayerbotdungeonrunraidhealerguard) | `0` | `1`: in a raid boss fight with the main tank on the boss, and in a raid trash fight once the main tank held its current target (kept on for the fight), an add on a healer goes to the nearest ranged dps within 35 yd of it (up to two per add), and the off-tanks take only such an add when no ranged dps is free. |
| [`AiPlayerbot.FankrissSwapStacks`](#aiplayerbotfankrissswapstacks) | `5` | Mortal Wound stacks at which an opt-in `fankriss tank` taunts Fankriss off the current tank (1-9). |
| [`AiPlayerbot.PatchwerkOpeningDelay`](#aiplayerbotpatchwerkopeningdelay) | `4` | Seconds the DPS hold Patchwerk after an opt-in `patchwerk main` engages him (`0` = off, else 3-5). |
| [`AiPlayerbot.DungeonRunPullStand`](#aiplayerbotdungeonrunpullstand) | `0` | For a tank that pulls with a ranged weapon, the run leader orders `pullback` only when it can shoot from where it stands, else it walks the party to a firing stand first. |
| [`AiPlayerbot.DungeonRunStandClearance`](#aiplayerbotdungeonrunstandclearance) | `0` | Every firing stand the run leader picks (an authored stand-pull step's own pull, this key being what gives that step a stand rule, a generic pull's with `AiPlayerbot.DungeonRunPullStand`) is refused when another live pack (its patrol loop included) can reach it; none clear, the stand the key is off for is used (advisory). |
| [`AiPlayerbot.DungeonRunStandPullPreCc`](#aiplayerbotdungeonrunstandpullprecc) | `0` | A stand-pull step's crowd control goes out at the last point of the approach walk outside the pulled pack's own aggro range, before the party walks in. |
| [`AiPlayerbot.DungeonRunStandDiscipline`](#aiplayerbotdungeonrunstanddiscipline) | `0` | A pull row fires from the plan's authored approach only (never a closer firing stand), and every tank and melee is waited for at that stand or clear of the next pull's pack before the row fires. |
| [`AiPlayerbot.DungeonRunAutoCc`](#aiplayerbotdungeonrunautocc) | `0` | `1`: the run leader marks crowd control on a pack from what the raid has (casters first, the plan's `cc_priority`/`cc_always` as a hint), above the body threshold, and re-applies a mark that broke. |
| [`AiPlayerbot.DungeonRunAutoCcAbove`](#aiplayerbotdungeonrunautoccabove) | `6` | How many bodies a pull may bring before the automatic crowd control starts (crowd control from the body after it). |
| [`AiPlayerbot.DungeonRunAutoCcRisky`](#aiplayerbotdungeonrunautoccrisky) | `0` | `1`: the automatic crowd control may use Fear and Scare Beast, only with 35 yd of room and when no other family has a free bot. |
| [`AiPlayerbot.DungeonRunRezHold`](#aiplayerbotdungeonrunrezhold) | `0` | The run leader's regroup waits for a living resurrector to raise the fallen (and for its mana) before it revives the party at the spirit healer. |
| [`AiPlayerbot.DungeonRunRetreatAt2Dead`](#aiplayerbotdungeonrunretreatat2dead) | `0` | `1`: with two dead in a fight the party is losing, the living pull back to the run's last stand once before a third death makes it a wipe. |
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

**Combat monitor:** per-fight combat numbers of every bound group for the dashboard:
- casts, damage, healing and overheal, damage taken, deaths with last hits, debuff uptime
- casts made in the 3 s after combat ends are listed apart
- each finished fight is sent twice with the same id (UDP loss; the daemon dedupes)
- a rolling 10 s window; the live window rides the `COMMANDERS` snapshot
- a player outside every bound group costs one atomic bit test (no lock); a member event takes the lock
- past 500 distinct engaged creatures, the rest counts in `fight.dropped` (distinct guids; stops at 4096)
- needs `AiPlayerbot.Observability`

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
**Default:** *(empty)* · **Values:** comma-separated area trigger ids; empty = listed triggers off (a run's own `move.entry` trigger still arms); recommended `962` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Scripted area triggers for bots** (audited allowlist, off by default):
- a bot has no game client, so it never reports walking into a trigger; the script never ran for it
- a living grouped bot in a dungeon runs a listed trigger it walks into, once per entry, as a client would
- only when its party is bound to a remote commander (every run-leader run is) or has a human member within 40 yd
- raids are left out until bots are tested there; entrances, exits, taverns, and battleground entrances never fire
- each bot tests its own map's listed triggers twice a second, no world scan
- Opt in with `AiPlayerbot.BotAreaTriggers = 962`
- audited: **962** (Zul'Farrak, `at_zumrah`): a living Zum'rah within 30 yd turns hostile, yells, and his encounter starts; nothing else
- read a trigger's script and document it here before adding its id
- a run's own `move`/`move_direct` step with an `entry` arms that trigger for its bots (running or paused) until it fires once; other unlisted triggers stay off
- a refused script stays armed and retries every 2 s; it counts as fired only once its script runs
- a run started past the trigger's step never arms it

</details>

### `AiPlayerbot.BotGossipNpcs`
Allowlist of creature entries the `.bot cmd gossip` order may talk to.
**Default:** *(empty)* · **Values:** comma-separated creature entries; empty = listed orders refused (a run's current gossip step still allowed) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Bot gossip allowlist:** comma-separated creature entries that `.bot cmd gossip` may talk to.
- a gossip option can do anything its script says (money, items, teleport): list only NPCs whose scripts you read
- empty: every listed order is refused with `not-allowed`, except a run's own current gossip step (its `entry` only)
- audited: `7604` Sergeant Bly and `7607` Weegli Blastfuse, so `7607,7604` opens both
- see [`.bot cmd gossip`](../../play/commands/commander.md#bot-cmd-gossip)

</details>


### `AiPlayerbot.BotLfgGroups`
World bots form groups of four that wait for a player, then take a fifth bot.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Bot LFG groups:** online, idle same-faction bots in a proven dungeon level band (`ai_bot_lfg_plan`) form a group of four:
- tank, healer, damage, plus one role left open for a player
- while waiting it looks in LookingForGroup ([`AiPlayerbot.BotLfgChat`](#aiplayerbotbotlfgchat))
- no player in time + free in-band bot: a bot of the open role joins (else retries in 5 min); a held invite blocks it
- travel is not built: a full group breaks up after two minutes
- count scales with bots online (per [`AiPlayerbot.BotLfgGroupsPer100Bots`](#aiplayerbotbotlfggroupsper100bots), at most [`AiPlayerbot.BotLfgMaxGroups`](#aiplayerbotbotlfgmaxgroups))
- only while a real player is online by default; grouped bots from a past session are skipped
- needs [`AiPlayerbot.DungeonSelfClear`](#aiplayerbotdungeonselfclear); the remote commander is not needed

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

The waiting leader posts in LookingForGroup:
- what: the dungeon, the open role as `dps`, the band, "w me inv" (text rotates between three variants)
- when: every 3 minutes, at most one post per 30 s per faction, only while a real player of that faction is online
- a short invite whisper (like "inv") from a fitting player gets an invite
- other whispers: at most one answer per 10 s per sender (non-invite hints every 5 minutes)
- answers: "inv sent", "already sent you an inv", "someone's joining atm", "sorry, we're full now", "sorry, can't group with hardcore chars", and short refusals (grouped, band, role, failed invite)

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

**Pull aggro estimate:** before each pack pull the run leader counts what would come, by the core wake-up rules:
- the pulled creature's whole pack
- every creature whose aggro radius reaches the pull spot (20 yd detection, widened or narrowed by level gap, plus drift)
- one ring of family assistance (same faction, at least 20 yd, called from every pack-spawn member)
- over [`AiPlayerbot.DungeonRunAggroCeiling`](#aiplayerbotdungeonrunaggroceiling): the party rests once before the pull
- adds no movement, never skips a pull; the estimate is approximate
- every pull note also reports the joiners really drawn (first 10 s of the fight); that needs no key

</details>

### `AiPlayerbot.DungeonRunAggroRingYd`
How far a call for help carries in the ring estimate, in yards.
**Default:** `20` · **Values:** `5` to `40` · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

Only read while [`AiPlayerbot.DungeonRunAggroWalkRing`](#aiplayerbotdungeonrunaggrowalkring) is `1`. A creature already counted calls every creature of its own faction template within this many yards of itself, for two hops. The server's `CreatureFamilyAssistanceRadius` is used instead when it is larger. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#aggro-estimate).

</details>

### `AiPlayerbot.DungeonRunAggroWalkRing`
Makes the pull aggro estimate count what the party's walk to the pull, the stand and the fight point wake, and lets every creature already counted call its neighbours.
**Default:** `0` · **Values:** `0` off (today's estimate), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

Only read while [`AiPlayerbot.DungeonRunAggroEstimate`](#aiplayerbotdungeonrunaggroestimate) is `1`:
- the estimate reads the navmesh walk (at most 64 samples), the spot, and the fight point, widened by party spread (3 yd, 10 yd in a raid)
- every counted creature calls neighbours within [`AiPlayerbot.DungeonRunAggroRingYd`](#aiplayerbotdungeonrunaggroringyd) for two hops
- a row's own `max_bodies` (target included) replaces the config ceiling, so a sized pull never rests
- over it the note reads `pull estimate: N bodies over the ceiling M (plan|config)`; the last move row previews the coming pull
- advice only: no order changes, no pause

</details>

### `AiPlayerbot.DungeonRunAuraTactics`
Turns on the aura-based boss tactic rows and the per-row health and bot-aura gates.
**Default:** `0` · **Values:** `0` off (the six aura kinds are not read and every gated row is closed, so bots play as before), `1` on (needs the boss tactics armed and the rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Aura tactics:** the six newer boss-tactic kinds (see [Boss Tactics](../../play/boss-tactics.md#kinds)):
- `dispel_priority`: ranks and holds cures by the rows (`never` excludes its whole dispel type; `clear` waits until the carrier is clear)
- `dps_hold_after`: holds free DPS off a subject for a row's seconds after a pull, health-band opening, cast, aura, or blink
- `flee_on_aura`: moves a carrier out of the group (or to the main tank)
- `freeze_on_aura`: stands a carrier still and vetoes every action while the aura lasts
- `taunt_after_cast`: sends a rotation tank to taunt a subject back inside a deadline
- `swap_on_aura`: rotates two tanks at a stack count
- the same key opens the row gates: a row whose `hp_min`/`hp_max` or `bot_aura` does not match is ignored while off

</details>

### `AiPlayerbot.DungeonRunBotKeysForPlayers`
Whether the bots carry the dungeon keys for a party that has real players in it.
**Default:** `0` · **Values:** `0` players bring their own key, `1` the bots carry the keys and open the doors · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Keys with players:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- with `0`: a run with a real player gives the bots no keys; the start warns once, naming each key and where to get it
- at a locked door the bots stop and ask the player to open it (key or Lockpicking), then go on by themselves
- `.bot run resume` works once the door reads open
- with `1`: the bots carry the keys and open the doors, as in a bot-only run

</details>

### `AiPlayerbot.DungeonRunFightForward`
Whether the run leader pulls the next pack close by from where the tank stands after a won fight.
**Default:** `1` · **Values:** `0` the plan's rows as before, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Fight forward:** only read while `AiPlayerbot.DungeonSelfClear = 1` and `AiPlayerbot.DungeonRunWalkBackAfterWonFight = 0`:
- after a cleared `pull` or `boss` step, with at most 4 plain `move` rows of the same leg to the team's next `pull`
- within 30 yd of the tank (8 yd up or down), not an ambush, not uncleared, nothing deferred, no live plan pack on the line
- then the run pulls the next pack from where the tank stands, when the way is direct and clear

</details>

### `AiPlayerbot.DungeonRunGiveItems`
Whether a bot-only run gives the bots the dungeon keys and items it needs.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Giving run items:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- bot-only run start: every bot gets each needed item it lacks (dungeon keys and plan items)
- items are soulbound (no trade, mail, or sale); a bot's own copy is never taken
- taken back when the run ends or stops, is erased, a bot leaves, or a real player joins
- instances are recorded in `ai_run_given_item`, so a crash leaves none with the bots
- with `0` nothing is given; a bot-only run pauses at a locked door until `.bot run skip`

</details>

### `AiPlayerbot.DungeonRunLightRest`
Whether the run leader rests the party only to light lines before a small pull.
**Default:** `1` · **Values:** `0` the full lines everywhere (as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Light rest:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- small pull (plain `pull` in the next 8 steps, or nothing ahead): rests only under 60% health, 35% healer mana, 25% other mana; ends at 75 / 60 / 45
- a bot still drinking above those lines holds the next order only while the lead bot is seated or eating
- big pull (`boss`, `hold`, `ritual`, event, 8+ estimated joiners, or 5+ units in sight with the estimate off): full lines
- a big pack gets one rest to the full lines before its pull

</details>

### `AiPlayerbot.DungeonRunLinkedDeath`
Turns on the "die together" boss tactic: free DPS bots equalise a linked group's health, hold every member at the row's percent and burn them together.
**Default:** `0` · **Values:** `0` off (DPS bots pick as before), `1` on (needs the boss tactics on and `linked_death` rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Linked death** (`linked_death` rows, see [Boss Tactics](../../play/boss-tactics.md)): a group that must die together:
- each row lists one partner; `boss_entry` is the anchor, `param` is the hold percent
- while more than one member lives, free DPS (never tanks or healers) takes the living member with the highest health
- a member at or below `param` is held: never picked, left out of AoE, area casts and DoTs that would reach it stood down
- when every living member is at or below `param`, the group releases and burns in row `burn_order`
- an explicit player `attack` order still wins

</details>

### `AiPlayerbot.DungeonRunLootFfa`
Lets the run leader's loot step get an item the group's loot rules gave to another member, by switching the group to free-for-all for that one loot.
**Default:** `0` · **Values:** `0` the loot step tries the other bots in turn, `1` free-for-all for the carrier's loot · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Loot step free-for-all:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- problem: a plain-drop boss quest item belongs to the round-robin member's turn, so the lead bot is refused with *nothing this bot may loot*
- with `1`: on that refusal, once per step, the lead bot sets free-for-all (`cmd lootmethod ffa`), the carrier loots, and the method goes back on ACK or ERR
- restore retries once a second, at most three times; if all are refused the run pauses with a note naming the group, the key, and the order to give by hand
- also restored when the run pauses or stops, the binding ends, or the run leader is switched off; never touched if a player changed the method since
- switching the key off mid-detour does not stop the restore; a busy carrier waits at most 20 s, then the step goes the old way
- saved method is the name only (threshold kept); master loot comes back with the lead bot as master looter
- limits: a hand-set free-for-all looks like its own and is put back; run state is gone after a restart (loot-method survival unverified)
- skipped when the step has no `item`, someone carries the item, the item left the corpse, the group is already free-for-all or method-less, or the lead bot does not lead
- with `0` nothing is read and nothing changes

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

**Near-wipe stop:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- only for a raid binding (`AiPlayerbot.DungeonRunRaid = 1`), unless `AiPlayerbot.DungeonRunNearWipeStopParty = 1`
- a *wipe event* starts when the party first reaches the wipe level (raid: 40% of members, at least 3; or every tank dead; or more than half the healers dead; or every member dead; or a window wipe)
- it ends when none of those holds; marks older than `AiPlayerbot.DungeonRunNearWipeStopMinutes` are dropped
- at the count, the run stops after the fight (out of combat, or only a GM pinned in a fight), before any regroup
- `.bot run resume` clears the marks

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

**Party opt-in:** in a party the wipe level is 3 of 5, and the per-step cap (2 regroups a step) already pauses a step that keeps failing. So a party gets it only with this key. Only read while `AiPlayerbot.DungeonRunNearWipeStop` is above `0`.

</details>

### `AiPlayerbot.DungeonRunPackClearMarginYd`
The band around a pull step's radius in which a living unit of its entries still counts.
**Default:** `10` · **Values:** `0` off (the old radius-only rule), any distance in yards · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Pack-cleared gate:** only read while `AiPlayerbot.DungeonSelfClear = 1`. A `pull` step is done when:
- no living unit of its entries in its radius
- none in the band outside it (2D distance within radius + margin, height within 10 yd, so another floor is no residue), unless another step's radius holds it
- no waypoint patroller or formation follower of one; no spawn of the radius still walking its loop
- residue is pulled nearest first, up to `AiPlayerbot.DungeonRunPackClearRepulls`; then the run pauses with entries, guids, distances

</details>

### `AiPlayerbot.DungeonRunPackClearRepulls`
How many residue pulls a pull step may send before it pauses for a human.
**Default:** `3` · **Values:** `1`-`10` pulls · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**The cap:** each residue pull (the band's units, nearest first, with the same way back and `pullback` as the step's own) counts one. At the cap with residue still open the run pauses with a `pack not cleared after N re-pulls` note naming the residue. Only read while `AiPlayerbot.DungeonRunPackClearMarginYd` is above `0`. See [`AiPlayerbot.DungeonRunPackClearMarginYd`](#aiplayerbotdungeonrunpackclearmarginyyd).

</details>

### `AiPlayerbot.DungeonRunPlayerLed`
Lets a player who leads his own party of bots start a player-led run of a dungeon plan.
**Default:** `0` · **Values:** `0` refused, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>
**Player-led runs:** `.bot run start [plan] player [tank <Bot>]`:
- from a player leading a party (not a raid) whose every bot is his to control; up to three other players may be in it
- its own binding: the player stays leader, the bots keep following him
- the named tank bot (else the first tank-role bot) is the lead bot and puller
- needs `AiPlayerbot.DungeonSelfClear = 1`; plain `.bot run start` stays bot-led

</details>

### `AiPlayerbot.DungeonRunPlayerLedAwayMinutes`
How long the leading player of a player-led run may be out of the plan's dungeon before the run stops.
**Default:** `10` · **Values:** minutes, `1` to `60` · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Player-led away limit:** while the player is out of the plan's dungeon:
- bots inside hold where they stand (`cmd stay`, once); the note counts the minutes left
- back in time: bots follow again (`cmd follow`, once) and the run goes on
- past the limit: the run stops with a note naming the minutes away (bots stay where they hold), binding ends
- only read while `AiPlayerbot.DungeonRunPlayerLed = 1`

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

**Raid boss fight rules:** only read with `AiPlayerbot.DungeonRunRaid = 1`, on a raid binding:
- a raid boss fight is a `boss` step, or a plan boss engaged on any step, outside a scripted event
- with `1`: fight ceiling is 1200 s instead of 420 s; a raid boss fight stays one until it ends
- past the ceiling, a death in the last 60 s does not pause the fight while a plan boss still lost hp; the 1800 s hard cap still pauses
- hitting mobs go to living off-tanks one at a time, round-robin; the main tank never leaves a held plan boss
- with `0` the boss fight keeps the dungeon rules; with HealerGuard, healer adds go to ranged dps first

</details>

### `AiPlayerbot.DungeonRunRaidHealerGuard`
A raid's adds on healers (boss and trash fights): sent to the raid's ranged dps, not to the off-tanks.
**Default:** `0` · **Values:** `0` off (the off-tanks take the healers' adds), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Healer guard:** only read with `AiPlayerbot.DungeonRunRaid = 1`:
- boss fight (needs `=1` + main tank holding a plan boss) or trash fight (main tank held its target when the guard turned on)
- with `1`: a healer add goes to the nearest ranged dps (up to two per add, within 35 yd, nearest first)
- off-tanks take it only when no ranged dps is free
- with `0`: off-tanks take the healers' adds as before

</details>

### `AiPlayerbot.DungeonRunPullStand`
Makes the run leader put the tank where its ranged weapon can fire before it orders `pullback`.
**Default:** `0` · **Values:** `0` off (the run leader pulls exactly as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Pull stand:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- covers only a tank whose pull is a ranged weapon shot (bow, crossbow, gun, thrown weapon, wand)
- other pulls go out as before: spell pull, body pull (no ranged weapon, skill, shoot spell, or ammo), or shot range of 5 yd or less
- with `1`, before each `pullback` the run checks three things: target in sight, within shot range less 5 yd, tank standing still (moving tank waited for up to 3 s)
- if any fails: reads a firing stand on the tank's navmesh path (walked back from the target to the first point in range that sees it), walks the party there, then orders the pull
- at most 2 stands per target and step; a stand walk with no progress for 12 s, or over 36 s, is given up (`firing stand unreachable: ordering from here`); the run never pauses for it
- kill-first ties break toward a target the tank can shoot from where it stands

</details>

### `AiPlayerbot.DungeonRunRezHold`
Makes the run leader's regroup wait for a living priest, paladin or shaman to resurrect the fallen where they lie, instead of a fixed 15 s.
**Default:** `0` · **Values:** `0` the old fixed wait, `1` the rez hold · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Rez hold:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- with `0`: an out-of-combat priest, paladin, or shaman waits 15 s for a resurrection, then the run sends `cmd revive party`
- with `1`: the run asks the rez hold every second while someone is dead; the resurrector gets no order (its own AI picks whom to raise)
- one order only: a `move` of the lead bot onto the body, once per regroup, after 12 s with a resurrector able, no cast under way, body over 8 yd away
- holds up to 60 s from when a resurrector could act (plus 12 s per extra body, at most 120 s; a cast in progress may finish)
- waits for mana up to 90 s; sends `cmd revive party` at once when no living bot can resurrect, every body is over 80 yd away, or time is up
- the 240 s the dead get to be up starts when the hold gives up; a druid Rebirth counts only with no priest, paladin, or shaman in sight, off cooldown, seed in bags
- everyone dead is a wipe: the old wipe recovery takes it

</details>

### `AiPlayerbot.DungeonRunRetreatAt2Dead`
Whether the run leader pulls the living back to the last stand when two party members are dead and the fight is being lost.
**Default:** `0` · **Values:** `0` the party fights on (as before), `1` one retreat per fight · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Retreat at two dead:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- with `1`, in a party (never a raid): two dead, lead bot alive, fight being lost (no engaged unit lost hp in 15 s)
- the run pulls the living back to its last stand once per fight; the retreat ends there
- a refused move ends it without a pause; a third death or lead-bot death ends it too
- never inside a scripted event, on steps leading into one, on a `hold` step, or on an event boss step
- whether mobs follow the party back is the game's own rule (the module adds no leash)

</details>

### `AiPlayerbot.DungeonRunStandClearance`
Makes the pull stand keep clear of every other live pack, its patrol loop included.
**Default:** `0` · **Values:** `0` off (the stand is picked as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Stand clearance:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- applies to every firing stand the run leader picks (authored stand-pull pulls and generic stand pulls with `AiPlayerbot.DungeonRunPullStand = 1`)
- stands are tried in path order (ranged reach point first, then nearer path points); the first one clear of every other live pack wins
- clear means: at least the pack's aggro reach plus 4 yd away, with the line of sight the core needs
- a patrolling pack is measured along its whole loop; a pack already in combat is not checked
- the pulled pack itself, its formation, and its called help are not other packs; only spawn proof counts on a boss step
- a stand inside the pulled pack's widest reach is refused; another storey (over 8 yd up or down, no sight line) is not checked
- advisory: with no clear stand the run uses the default and notes it; only a lone patrolling blocker is waited for (up to 20 s)
- the run never pauses or holds a pull for the clearance

</details>

### `AiPlayerbot.DungeonRunStandPullPreCc`
Sends a stand-pull step's crowd control from the last point of the approach walk that is outside the pulled pack's own aggro range, before the party walks in.
**Default:** `0` · **Values:** `0` off (the walk in is as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Crowd control before the walk in:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- with `1`, the move row before a stand-pull pull or boss row ends at the last approach point outside the pack's wake reach
- reach: widest aggro reach of the pack's living units plus the party's 10 yd spread plus 1 yd
- at that point the step's `cc_priority` marks go out; the party waits up to 6 s for them to hold, then walks in and pulls
- no such point (approach inside the pack): walks the plan's point, note `stand_pull: no pre-cc point`; a point unread within 3 s is dropped the same way

</details>

### `AiPlayerbot.DungeonRunStandDiscipline`
Fires a pull row from the plan's authored approach only, and waits for the tanks and melee to stand there (or clear of the next pack) first.
**Default:** `0` · **Values:** `0` off (the old stand search and pull), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**The plan's stand is the only stand:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- the move row before a pull or boss row is the plan's own stand, marked `stand_pull` or not
- with `1`, a tank that cannot shoot from that stand holds there (walks back to the approach if it drifted off)
- status note: `no LOS from the authored stand; holding`; hold lasts up to 20 s per unit, then the tank searches as with `0`
- needs no `AiPlayerbot.DungeonRunPullStand`
- before the row fires, every living tank and melee must stand within 5 yd of the approach or stay clear of the next pull (radius + 20 yd ring)
- the run waits up to 10 s; then fires, noting `out of place: <names>`; hop and walk-back pulls never wait
</details>

### `AiPlayerbot.DungeonRunAutoCc`
Marks crowd control on a pack from what the raid actually has, instead of only the plan's `cc_priority`/`cc_always` entries.
**Default:** `0` · **Values:** `0` off (the plan's list only, as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Automatic crowd control:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- before a pull over [`AiPlayerbot.DungeonRunAutoCcAbove`](#aiplayerbotdungeonrunautoccabove) (or the row's smaller `max_bodies`), the run marks the row's own pack, one order a tick
- order: casters and healers first, then plan rank (`cc_always`, then `cc_priority`), then family reliability, then higher level, then farther from target
- risky Fear and Scare Beast go last and only with this key's risky flag on
- never marked: pull target, tank selection, held add, fight focus, `kill_first`, held unit, hit unit, healer-guard add
- in the fight: marks joiners two seconds apart; re-sends a broken mark twice per unit; a dead bot frees its hold
- tanks and the lead bot never do crowd control; a healer only while 4 healers (1 in a party) stay free, holding one unit
- at most 24 auto orders a step; uses seven marks (skull stays the fight focus); a player's icon is never moved

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

### `AiPlayerbot.DungeonRunWalkBackRescue`
Whether the run leader teleports a stuck walk back's lead bot (and a placed run's lead bot revived behind its start step) to a safe point on the run's way.
**Default:** `1` · **Values:** `0` the old pause, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Walk back rescue:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- with `1`, a stuck walk back is not paused: the lead bot is teleported to the last point it reached on the plan
- when a live pack is near that point: to the latest earlier point out of every live pack's reach (radius plus 10 yd)
- never back before the start step or over a direct step; with no such point the run pauses
- a placed run's lead bot behind the start step is teleported to a safe point on the way it came
- out of combat only, once a step; left-behind party is summoned only out of combat

</details>

### `AiPlayerbot.RunTeleportReturnYd`
How far (yards) a party bot may be from the lead bot before the run leader brings it back by a teleport that drops its combat, instead of the summon portal or a walk back.
**Default:** `0` (off; recommended `120`) · **Values:** `0` off (the summon portal, and a walk back when it fails), any distance in yards · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Teleport return:** only read while `AiPlayerbot.DungeonSelfClear = 1`, and only in a party with no real player:
- a bot brought back from farther than this comes by the run's own teleport: combat and pet combat are dropped, then it moves to the lead bot at once
- covers a revived bot far away, a left-behind bot, a bot wedged on another floor, and a dropped portal summon
- when the lead bot stands in an unpulled pack's reach, the bot lands at the nearest clear point beside it (same floor, in sight, outside every pack's reach)
- not within 10 yd after 5 s: sent again; after three tries the run names it and never tells it to walk back
- not toward a dead or separated lead bot, not while the lead bot's own rescue is due, not with a plan boss engaged

</details>

### `AiPlayerbot.DungeonReviveHoldYd`
How far (yards) from the lead bot a run's bot may be revived before it stands still and waits to be brought back instead of walking.
**Default:** `0` (off; recommended `100`) · **Values:** `0` off, any distance in yards · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Known limit:** a held bot takes no hazard escapes either: while the hold is up it does not step out of a listed boss cast (`avoid_cast`), an area effect or a bomb runout. It waits where it stands and the run is what gets it out; a hold only lasts 60 s at most.

**Waiting to be brought back:** only read while `AiPlayerbot.DungeonSelfClear = 1`, for the bots of a run:
- a bot revived farther than this from the lead bot stands still up to 60 s, waiting for a summon or teleport
- the same hold covers every bot the run is bringing back and any bot with a commander summon pending, until within 10 yd of its target
- while held it does not follow, walk, or chase; it fights back only from where it stands
- after 60 s without arriving it is released with a note; a paused run leaves its own returns to the bots' AI

</details>

### `AiPlayerbot.DungeonRunWalkBackAfterWonFight`
Whether the run leader walks the tank and the party back to the guard's stand point after a fight it has won.
**Default:** `0` · **Values:** `0` no walk back once the step's pack is cleared, `1` the walk back after every guarded fight (the old way) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Walk back after a won fight:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- the run notes the stand the tank left during a fight
- with `0`: after the fight the walk back is left out on a cleared `pull`/`boss` step (next step starts from the tank) and on a `move` step
- kept on steps with units left and on waits, rests, and object or item steps
- nothing changes during a fight (ordered only out of combat) or in a scripted event
- with `1`: the walk back goes out after every guarded fight, as before

</details>
### `AiPlayerbot.DungeonSelfClear`
Turns on the run leader (`.bot run`) for in-party, player-led and server-owned runs.
**Default:** `1` · **Values:** `0` off (every `.bot run` refused), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>
**Dungeon self-clear (run leader):** a `.reload config` re-reads this key and the `AiPlayerbot.DungeonRun*` keys on the next world tick:
- only those, plus `AiPlayerbot.ObservabilityRaids`; everything else still needs a restart
- set to `0` by a reload: every run ends with a last status line
- with `1`: a bound party may start `.bot run start [plan]` and walk the plan by itself (pull, fight, rest, loot, doors)
- with 3 or more dead it regroups by itself once the fight is over
- with `0`: every `.bot run` is refused

</details>

### `AiPlayerbot.DungeonTactics`
Applies the per-boss rows of the world table `ai_boss_tactic` (interrupt, kill first, stay behind) while bots fight that boss.
**Default:** `0` · **Values:** `0` only bound groups, `1` every bot on a map with rows · **Restart:** needed

<details>
<summary>How it works in detail</summary>

**Dungeon boss tactics:** per-boss rows of `ai_boss_tactic` while bots fight that boss:
- interrupt a listed spell (*Pummel*, *Shield Bash*, *Kick*, *Counterspell*, *Earth Shock*)
- kill a listed add before anything else, in order (DPS only, never tanks or healers)
- non-tanks stay out of a frontal attack's front (tanks never step aside)
- off by default: `0` gives it only to bound groups (remote, in-party, run-leader); `1` to every bot on a map with rows
- an explicit player `attack` order wins the kill-first pick

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
- Ranged damage bots (mages, warlocks, hunters, and shamans and druids that count as ranged — druids: not cat or bear form; never tanks or healers) automatically switch target to the nearest Onyxian Whelp within 40 yards and kill it before returning to the boss. Whelps located within 10 yards of unbroken eggs are skipped to prevent collateral hatching.
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

Not waived, even with `1`:
- content phase, level, raid group, and per-account instance limit still refuse the bot
- the core's own teleport still checks the instance (full, locked, map refusal)
- a scripted entrance is refused as if the key were off; its trigger script is skipped, only map admission checks run

The commander's condition is evaluated once per bot at the moment the bot sends the entrance trigger. With `0`, the bot's own condition decides, as before.

</details>

### `AiPlayerbot.FankrissSwapStacks`
Mortal Wound stacks at which an opt-in `fankriss tank` taunts Fankriss off the current tank.
**Default:** `5` · **Values:** `1`-`9` (clamped) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Only tanks given the `fankriss tank` strategy act on it ([Fankriss roles](../../play/dungeons-and-raids.md#fankriss-opt-in-roles)):
- when the current tank carries at least this many Mortal Wound stacks, one assigned tank with **zero** stacks and a ready Taunt (warrior) or Growl (druid) taunts
- lowest character guid wins a tie; a failed or resisted taunt is retried after 2 s
- if Fankriss attacks a non-tank raid member, an assigned clean tank taunts at once
- setting the key does not enable the role

</details>

### `AiPlayerbot.PatchwerkOpeningDelay`
Seconds the DPS hold Patchwerk after an opt-in `patchwerk main` engages him.
**Default:** `4` · **Values:** `0` off, else `3`-`5` (clamped) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Only acts in a raid where a tank has the `patchwerk main` strategy ([Patchwerk roles](../../play/dungeons-and-raids.md#patchwerk-opt-in-roles)). The clock starts when the main's Patchwerk enters combat and is kept for the whole encounter, so a DPS that joins late is not held again. During an ordered pull the DPS also wait while the main walks in. Heals and buffs are never held. Setting the key does not enable the roles.

</details>
