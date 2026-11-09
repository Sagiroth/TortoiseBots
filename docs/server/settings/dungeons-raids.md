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
| [`AiPlayerbot.CombatTimeline`](#aiplayerbotcombattimeline) | `0` | The boss timeline in each fight record: health every 2 s, victim changes, watched auras (needs `AiPlayerbot.CombatMonitor`). |
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
| [`AiPlayerbot.DungeonRunRespawnNest`](#aiplayerbotdungeonrunrespawnnest) | `0` | `1`: a fight held only by fast-respawning units at their spawn, none hitting the party, is no fight; a regroup never gathers on one. |
| [`AiPlayerbot.DungeonRunAuraGatedTargets`](#aiplayerbotdungeonrunauragatedtargets) | `0` | `1`: a unit a `needs_aura` boss-tactic row gates is left alone by bots without the aura (target picks, taunts, guard and attack orders). |
| [`AiPlayerbot.DungeonRunAvoidCastAll`](#aiplayerbotdungeonrunavoidcastall) | `0` | `1`: the boss tactics keep their `all` rows — no bot holds a listed caster while its spell is cast, so every tank walks out of that blast too (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunBossSpread`](#aiplayerbotdungeonrunbossspread) | `0` | `1`: the boss tactics keep their `spread` rows — the ranged raid keeps the row's yards apart while the boss fights (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunCharmYield`](#aiplayerbotdungeonruncharmyield) | `0` | `1`: a bot a creature charms yields its AI, no action (area or targeted) lands on a charmed group member, and pets stand down while one is up (needs no rows or tactics). |
| [`AiPlayerbot.DungeonRunImmuneBossVictimEscape`](#aiplayerbotdungeonrunimmunebossvictimescape) | `0` | `1` (recommended on): a boss that cannot be taunted and turns on a damage dealer or healer makes that member escape with its own ability, or a nearby paladin protect it (needs no rows or tactics). |
| [`AiPlayerbot.DungeonRunFightForward`](#aiplayerbotdungeonrunfightforward) | `1` | After a won fight the next pack within 30 yd is pulled from where the tank stands when the way there is direct and clear. |
| [`AiPlayerbot.DungeonRunGiveItems`](#aiplayerbotdungeonrungiveitems) | `1` | A bot-only run gives the bots the keys and items its dungeon needs and takes them back when the run ends; `0` gives nothing. |
| [`AiPlayerbot.DungeonRunKillFirstGrid`](#aiplayerbotdungeonrunkillfirstgrid) | `0` | `1`: the boss tactics find a kill-first unit (a ward on the boss, a Shade) by a grid search when no bot's attacker list shows it. |
| [`AiPlayerbot.DungeonRunLightRest`](#aiplayerbotdungeonrunlightrest) | `1` | Before a small pull the party rests to light lines (60/35/25 to 75/60/45); before a boss, an event or a big pull the full ones. |
| [`AiPlayerbot.DungeonRunLinkedDeath`](#aiplayerbotdungeonrunlinkeddeath) | `0` | `1`: free DPS bots equalise a `linked_death` group's health, hold every member at its `param` % and burn them together (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunLootFfa`](#aiplayerbotdungeonrunlootffa) | `0` | A run leader loot step whose item is another member's under the group loot rules sets free-for-all for the carrier's loot, then puts the method back. |
| [`AiPlayerbot.DungeonRunMaxMinutes`](#aiplayerbotdungeonrunmaxminutes) | `180` | Time limit of a run leader run (`0` = none). |
| [`AiPlayerbot.DungeonRunNearWipeStop`](#aiplayerbotdungeonrunnearwipestop) | `2` | Wipe events within the window that pause a raid run once the fight is over (`0` = off). |
| [`AiPlayerbot.DungeonRunNearWipeStopMinutes`](#aiplayerbotdungeonrunnearwipestopminutes) | `10` | The near-wipe stop's window, in minutes of running time. |
| [`AiPlayerbot.DungeonRunNearWipeStopParty`](#aiplayerbotdungeonrunnearwipestopparty) | `0` | `1`: party bindings get the near-wipe stop too; `0`: raids only. |
| [`AiPlayerbot.DungeonRunPackClearMarginYd`](#aiplayerbotdungeonrunpackclearmarginyd) | `10` | A pull step is done only when nothing of its entries is left within its radius plus this band (`0` = off). |
| [`AiPlayerbot.DungeonRunPackClearRepulls`](#aiplayerbotdungeonrunpackclearrepulls) | `3` | Residue pulls a pull step may send before it pauses for a human. |
| [`AiPlayerbot.DungeonRunStragglerPull`](#aiplayerbotdungeonrunstragglerpull) | `0` | The tank walks to and pulls a pack unit or patrol the step waited for and did not come, instead of pausing; a stand-pull row's patrol is waited for one loop of its path at most. |
| [`AiPlayerbot.DungeonRunTotemFocus`](#aiplayerbotdungeonruntotemfocus) | `0` | A totem the plan kills first is focused by the damage dealers alone, and before the guard, so its 1-2 s kill never waits. |
| [`AiPlayerbot.DungeonRunPlayerLed`](#aiplayerbotdungeonrunplayerled) | `0` | Lets a player who leads his own party start a player-led run (`.bot run start ... player`): he walks, the run leader pulls the packs near him. |
| [`AiPlayerbot.DungeonRunPlayerLedAwayMinutes`](#aiplayerbotdungeonrunplayerledawayminutes) | `10` | How long the leading player of a player-led run may be out of the plan's dungeon before the run stops. |
| [`AiPlayerbot.DungeonRunPlayerLedPullYd`](#aiplayerbotdungeonrunplayerledpullyd) | `40` | How close the leading player stands to a plan pack or boss for the player-led run to pull it. |
| [`AiPlayerbot.DungeonRunRaid`](#aiplayerbotdungeonrunraid) | `0` | Lets the run leader bind the commander's raid of 6 to 40 bots and runs it with the raid rules (needs `AiPlayerbot.DungeonSelfClear` and `AiPlayerbot.RemoteCommander`). |
| [`AiPlayerbot.DungeonRunRaidBossRules`](#aiplayerbotdungeonrunraidbossrules) | `0` | `1`: a raid's boss fight runs up to 1200 s, survives its deaths while the boss still loses hp, and sends its adds to the off-tanks instead of the main tank. |
| [`AiPlayerbot.DungeonRunRaidHealerGuard`](#aiplayerbotdungeonrunraidhealerguard) | `0` | `1`: in a raid boss fight with the main tank on the boss, and in a raid trash fight once the main tank held its current target (kept on for the fight), an add on a healer goes to the nearest ranged dps within 35 yd of it (up to two per add), and the off-tanks take only such an add when no ranged dps is free. |
| [`AiPlayerbot.FankrissSwapStacks`](#aiplayerbotfankrissswapstacks) | `5` | Mortal Wound stacks at which an opt-in `fankriss tank` taunts Fankriss off the current tank (1-9). |
| [`AiPlayerbot.PatchwerkOpeningDelay`](#aiplayerbotpatchwerkopeningdelay) | `4` | Seconds the DPS hold Patchwerk after an opt-in `patchwerk main` engages him (`0` = off, else 3-5). |
| [`AiPlayerbot.RaidHealerSeatsByClass`](#aiplayerbotraidhealerseatsbyclass) | `0` | `1`: a raid binding's healer seats go by class — group healers on the raid, then the main tanks, the off-tanks whoever is left. |
| [`AiPlayerbot.RaidTankHealerAssist`](#aiplayerbotraidtankhealerassist) | `0` | `1`: a layout tank healer's second seat on its tank heals a raid member in sight and in range below 70% while the tank is quiet (not on a boss, 90%+ after incoming damage). |
| [`AiPlayerbot.DungeonRunPullStand`](#aiplayerbotdungeonrunpullstand) | `0` | For a tank that pulls with a ranged weapon, the run leader orders `pullback` only when it can shoot from where it stands, else it walks the party to a firing stand first. |
| [`AiPlayerbot.DungeonRunPullGate`](#aiplayerbotdungeonrunpullgate) | `0` | `1`: the run leader never pulls a boss its `pull_gate` row gates while the boss wears the row's aura, and ends the run after the row's seconds (Hakkar needs all of this, seek, charm yield and boss spread, plus the aura tactics). |
| [`AiPlayerbot.DungeonRunStandClearance`](#aiplayerbotdungeonrunstandclearance) | `0` | Every firing stand the run leader picks (an authored stand-pull step's own pull, this key being what gives that step a stand rule, a generic pull's with `AiPlayerbot.DungeonRunPullStand`) is refused when another live pack (its patrol loop included) can reach it; none clear, the stand the key is off for is used (advisory). |
| [`AiPlayerbot.DungeonRunStandPullPreCc`](#aiplayerbotdungeonrunstandpullprecc) | `0` | A stand-pull step's crowd control goes out at the last point of the approach walk outside the pulled pack's own aggro range, before the party walks in. |
| [`AiPlayerbot.DungeonRunStandDiscipline`](#aiplayerbotdungeonrunstanddiscipline) | `0` | A pull row fires from the plan's authored approach only (never a closer firing stand), and every tank and melee is waited for at that stand or clear of the next pull's pack before the row fires. |
| [`AiPlayerbot.DungeonRunAutoCc`](#aiplayerbotdungeonrunautocc) | `0` | `1`: the run leader marks crowd control on a pack from what the raid has (casters first, the plan's `cc_priority`/`cc_always` as a hint), above the body threshold, and re-applies a mark that broke. |
| [`AiPlayerbot.DungeonRunAutoCcAbove`](#aiplayerbotdungeonrunautoccabove) | `6` | How many bodies a pull may bring before the automatic crowd control starts (crowd control from the body after it). |
| [`AiPlayerbot.DungeonRunAutoCcRisky`](#aiplayerbotdungeonrunautoccrisky) | `0` | `1`: the automatic crowd control may use Fear and Scare Beast, only with 35 yd of room and when no other family has a free bot. |
| [`AiPlayerbot.DungeonRunRezHold`](#aiplayerbotdungeonrunrezhold) | `0` | The run leader's regroup waits for a living resurrector to raise the fallen (and for its mana) before it revives the party at the spirit healer. |
| [`AiPlayerbot.DungeonRunRetreatAt2Dead`](#aiplayerbotdungeonrunretreatat2dead) | `0` | `1`: with two dead in a fight the party is losing, the living pull back to the run's last stand once before a third death makes it a wipe. |
| [`AiPlayerbot.DungeonRunTankSwap`](#aiplayerbotdungeonruntankswap) | `0` | `1`: the boss tactics keep their `swap_on_aura` rows — the tanks of the row's rotation trade the boss at a stack count, and the tank that was swapped off stops attacking it for a while (needs the aura tactics and the boss tactics). |
| [`AiPlayerbot.DungeonRunUseObject`](#aiplayerbotdungeonrunuseobject) | `0` | `1`: the boss tactics keep their `use_object` rows — one claimed bot clicks the boss's game object, and mode `tank_anchor` walks the boss over to it (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunSeekAura`](#aiplayerbotdungeonrunseekaura) | `0` | `1`: the boss tactics keep their `seek_aura` rows — the raid gathers in a dying source's cloud before the boss's cycle spell lands (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunSeekPullCycle`](#aiplayerbotdungeonrunseekpullcycle) | `0` | `1`: the boss tactics keep their `seek_pull` rows — the seek walk-out pulls and kills one source in every cycle, up to the row's walk-outs a cycle (needs `DungeonRunSeekAura` and the rows). |
| [`AiPlayerbot.DungeonRunSeekSonMeet`](#aiplayerbotdungeonrunseeksonmeet) | `0` | `1`: a pulled Son meets a waiting off-tank, so it lands on a tank and is taunted on arrival instead of chasing the ranged raid (needs `DungeonRunSeekPullCycle` and a `seek_pull` row). |
| [`AiPlayerbot.DungeonRunSeekTakeEdge`](#aiplayerbotdungeonrunseektakeedge) | `0` | `1`: a tank takes a Son of Hakkar standing outside the raid's circle when the Son's victim stands inside it (plus 2 yd) and the Son is on the platform (z 46.5 or higher). |
| [`AiPlayerbot.DungeonRunSeekMeetNoHeal`](#aiplayerbotdungeonrunseekmeetnoheal) | `0` (recommended `1`) | `1`: while a pulled Son of Hakkar's meet is open, bots that heal members (healers and off-heal hybrids) skip its puller as a heal target unless its health after the next hit is under 40 %, so a heal's threat does not peel the Son onto a healer (needs `DungeonRunSeekSonMeet`). |
| [`AiPlayerbot.DungeonRunSeekWalkoutFocus`](#aiplayerbotdungeonrunseekwalkoutfocus) | `0` | `1` (recommended): the Hakkar walk-out puller's combat actions are held until its shot lands (not below 40 % health; reaction actions still run), it turns to the Son one tick before it casts, and a hunter opens with Arcane Shot when it can. |
| [`AiPlayerbot.DungeonRunSeekWalkoutHoldCombat`](#aiplayerbotdungeonrunseekwalkoutholdcombat) | `0` | `1` (recommended, needs `DungeonRunSeekWalkoutFocus`): the live, unshot Hakkar walk-out puller stays on the combat engine while Hakkar fights, and the pick skips a bot off the combat engine or eating or drinking. |
| [`AiPlayerbot.DungeonRunSeekWalkoutFaceCast`](#aiplayerbotdungeonrunseekwalkoutfacecast) | `0` | `1` (recommended): the Hakkar walk-out puller turns to the Son and casts in one tick, its per-update facing no longer turns it back to Hakkar, and the 6 s no-shot clock counts only the time in shot position. |
| [`AiPlayerbot.DungeonRunSeekWalkoutStandLog`](#aiplayerbotdungeonrunseekwalkoutstandlog) | `0` | `1` (diagnostic, safe on for test walks): one `seek pull: walk-out stand` row a second while the Hakkar walk-out puller walks out, and a `castrefused` row for every refused pull cast. |
| [`AiPlayerbot.DungeonRunSeekWalkoutStandSight`](#aiplayerbotdungeonrunseekwalkoutstandsight) | `0` | `1` (recommended): the Hakkar walk-out's stand search tests sight the way the bot does, and a refused move on the stand re-plans it instead of aborting. |
| [`AiPlayerbot.DungeonRunPlaceUnstick`](#aiplayerbotdungeonrunplaceunstick) | `0` | `1` (recommended): a bot in a dungeon or raid cut off on a navmesh island (every move fails NOPATH for 10 s and a navmesh query proves its spot cut off) is teleported beside its master, combat kept, at most once per 30 s. |
| [`AiPlayerbot.DungeonRunSeekNearestTake`](#aiplayerbotdungeonrunseeknearesttake) | `0` | `1` (recommended): the first off-tank takes a Son of Hakkar on a non-tank at once when the receiving tank is too far or its taunt is on cooldown and the first off-tank's taunt reaches. |
| [`AiPlayerbot.DungeonRunSeekNearestTakeReach`](#aiplayerbotdungeonrunseeknearesttakereach) | `0` | `1` (recommended): the first off-tank also takes a walked Son of Hakkar on a non-tank from up to 20 yd when the receiving tank is more than 15 yd from it or its meet was just lost (needs `DungeonRunSeekNearestTake`). |
| [`AiPlayerbot.DungeonRunSeekMeetPrePlace`](#aiplayerbotdungeonrunseekmeetpreplace) | `0` | `1` (recommended): when a Hakkar walk-out starts, the receiving third tank walks to the tanks' circle edge on the puller's way back and waits there for the pulled Son (needs `DungeonRunSeekSonMeet`). |
| [`AiPlayerbot.DungeonRunSeekMeetPrePlaceLate`](#aiplayerbotdungeonrunseekmeetpreplacelate) | `0` | `1` (recommended, still being tested): the pre-placed third tank stays on Hakkar until the puller is within 10 yd of its stand or in position, then walks (needs `DungeonRunSeekMeetPrePlace`). |
| [`AiPlayerbot.DungeonRunSeekShotWaitsReceiver`](#aiplayerbotdungeonrunseekshotwaitsreceiver) | `0` | `1` (recommended): the Hakkar walk-out shot waits until the pre-placed receiver is within 5 yd of its point, at most 6 s, and not when the next Blood Siphon is 15 s or less away (needs `DungeonRunSeekMeetPrePlace`). |
| [`AiPlayerbot.DungeonRunSeekMeetPrePlaceHold`](#aiplayerbotdungeonrunseekmeetpreplacehold) | `0` | `1` (recommended on with `DungeonRunSeekMeetPrePlace`): once its pre-place walk began, the pre-placed third tank's reach actions and warrior gap closer skip the anchored boss, so its own rotation no longer drags it back into Hakkar's melee. |
| [`AiPlayerbot.DungeonRunBurnMana`](#aiplayerbotdungeonrunburnmana) | `0` | `1`: the boss tactics keep their `burn_mana` rows — a few eligible bots drain the boss's mana while it is above the row's start percent (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunRangedOnly`](#aiplayerbotdungeonrunrangedonly) | `0` | `1`: the boss tactics keep their `ranged_only` rows — melee bots leave a subject that stands the row's gap above them and take the adds (needs the boss tactics and the rows). |
| [`AiPlayerbot.DungeonRunWalkBackRescue`](#aiplayerbotdungeonrunwalkbackrescue) | `1` | A stuck walk back, and a placed run's lead bot revived behind its start step, are teleported to a safe point on the run's way. |
| [`AiPlayerbot.DungeonRunDryWalk`](#aiplayerbotdungeonrundrywalk) | `0` | The run leader's route picks stay off deep water: a walk whose own way would swim goes in dry legs first, its approach hops and firing stand come from a ground-only navmesh path, and a walk-back shortcut's swim chord is refused. |
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

### `AiPlayerbot.CombatTimeline`
Adds the boss timeline to each finished fight record, so a fight can be replayed against the boss's health and its targets.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Needs `AiPlayerbot.CombatMonitor = 1` and `AiPlayerbot.Observability`. The fight record (the JSON behind the dashboard's fight detail and the `combat-fight-*.json` files a walk saves) gains three arrays, all times in ms since the fight's start:

- `boss_hp`: `[t_ms, hp_pct, phase_entry]`, the fought boss's health each 2 s (one decimal) with the entry it reads then; at most 600 samples.
- `victims`: `[t_ms, guid, name, role]`, one row per change of the boss's current victim; `guid` is the unit's low guid (the same value as a bot row's `guid`), and `role` is `tank`, `healer`, `dps`, or `none` for a player, a pet or nobody. At most 400 rows.
- `auras`: `[t_ms, bot guid, spell id, 1/-1]`, an apply (`1`) or remove (`-1`) of a watched aura on a bot of the group. The watched spells are the rows of the world table `ai_combat_aura_watch` (one pass at startup), never spell ids in code. At most 1000 rows.

Each cap drops the oldest row. Existing fields keep their place; a reader that does not know the arrays ignores them. A fight whose payload has to shrink drops the timeline before it shrinks below its other detail. The shipped rows are Zul'Gurub's Mark of Arlokk, Delusions of Jin'do, Banish, Poisonous Blood, Cause Insanity and Corrupted Blood; more auras are added with a migration.

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
- `taunt_after_cast`: sends a rotation tank to taunt a subject back, and retries every window while the subject stays off a tank; one tank holds a window even while its taunt cools down after the cast, and only a tank that lost control or is silenced hands it over, while the pick of a new window skips a tank whose taunt is on cooldown; a failed taunt hands the next window to another tank; only mode `until_tank` acts, `blink` and spell-keyed rows in other modes are loaded and ignored
- `swap_on_aura`: rotates two tanks at a stack count
- the same key opens the row gates: a row whose `hp_min`/`hp_max` or `bot_aura` does not match is ignored while off
- the `until_tank` mode runs on this key alone; a creature-charmed tank only leaves the order while `AiPlayerbot.DungeonRunCharmYield` is also on

</details>

### `AiPlayerbot.DungeonRunAuraGatedTargets`
Leaves a unit a `needs_aura` boss-tactic row gates to the bots that carry the row's aura.
**Default:** `0` · **Values:** `0` off (every bot may target it, as before), `1` on (needs the boss tactics and the rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Aura-gated targets:** the `needs_aura` kind (see [Boss Tactics](../../play/boss-tactics.md#kinds)): `boss_entry` is the unit and `spell` the aura it takes damage from. A bot without the aura does no damage to it, so with this key on the target picks, boss-tactic taunts and the run's guard leave it out, and an explicit attack order on it is refused.

</details>

### `AiPlayerbot.DungeonRunRespawnNest`
Keeps the run leader out of fights that only a fast-respawning spawn holds open.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Respawn nest:** a unit that respawns within 60 s and stands within 22 yd of its own spawn point (ZG Jin'do's pit: ten Sacrificed Trolls, 10 s). A combat that only such units hold, none of them hitting a party bot or pet, is no fight: the run orders the bots back to the step. A regroup also gathers at the step point instead of within 30 yd of one. A nest unit that attacks the party is always a real fight and a guard target, and a unit the plan's own boss, hold or pull step names is never a nest.

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

### `AiPlayerbot.DungeonRunKillFirstGrid`
Lets the boss tactics find a kill-first unit no bot's attacker list shows, by searching the grid around the bot.
**Default:** `0` · **Values:** `0` off (only the units the bots already see), `1` on (needs the boss tactics and the rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Kill-first grid:** the boss tactics' kill-first focus is read from the bots' cached attacker lists, which never show a ward standing on the boss or a Shade no uncursed bot can see. With this key on, the read also searches each row's `target_entry` within 40 yd around the bot (one grid search per bot and entry per second), and a unit of the fight joins the candidates when it fights itself or through an owner in the fight (a totem's summoner). It is a fallback only: the attacker lists and the plan's own kill_first list keep their picks.

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

**The cap:** each residue pull (the band's units, nearest first, with the same way back and `pullback` as the step's own) counts one. At the cap with residue still open the run pauses with a `pack not cleared after N re-pulls` note naming the residue. Only read while `AiPlayerbot.DungeonRunPackClearMarginYd` is above `0`. See [`AiPlayerbot.DungeonRunPackClearMarginYd`](#aiplayerbotdungeonrunpackclearmarginyd).

</details>

### `AiPlayerbot.DungeonRunStragglerPull`
Pulls a pack unit or a patrol that the step waited for and did not come, instead of pausing the run.
**Default:** `0` · **Values:** `0` off (the run pauses as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**The straggler pull:** only read while `AiPlayerbot.DungeonSelfClear = 1`. Added 2026-11-03 for Zul'Gurub walks 32 and 33: the Arlokk pack paused with `pack not cleared: 11365 0x… of this pack (spawned 12.7 yd from the centre) is 12.7 yd out and did not come within 60 s`, and Jeklik's bat rider 14750 held its stand-pull row with `patrol pull: 14750 0x… did not come within 28.5 yd of the stand`, each until a human pulled it. With `1`, a unit a `pull` step waits for that did not come (a spawn-owned patroller outside the radius) is walked to and pulled by the acting tank after the 60 s wait: the lead bot while it lives and is a tank, else the off-tank that holds the main tank role. The tank walks to the unit and sends `attack <guid> <tank>`, the rest of the party staying with the lead bot at the stand. Each try has 45 s; a failed try — a refused order, a way with no path, a blocked way or the 45 s run out — goes to one untried off-tank, and only then the run pauses as before with the reason in the line. The way is read again right before each movement order and every 2 s while the tank walks or waits at the point: another pack not pulled yet that comes within its aggro reach plus 5 yd of the remaining way aborts that tank's try exactly like a blocked way, and a way that no longer has a path does the same. A stand-pull row's patrol is waited for one full loop of its own waypoint path at its walk speed (90 s when the loop cannot be read) instead of the row's 600 s, then the tank walks to the nearest point of that loop on the party's side and pulls it from there. A boss, a `kill_first` unit and a `linked_death` unit keep the old behaviour. One `TBM:RUN` note per try says which tank walks where.

</details>

### `AiPlayerbot.DungeonRunTotemFocus`
Focuses a totem the plan kills first on the damage dealers alone, and before the guard's order.
**Default:** `0` · **Values:** `0` off (the totem is an ordinary add), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Totem focus:** only read while `AiPlayerbot.DungeonSelfClear = 1`. A creature of type totem (11) whose entry is on the plan's kill_first list — Jin'do's Brain Wash Totem, a healing ward — dies in 1-2 s and holds nobody, so with this key the run focuses it first in the fight, before the guard, the crowd control or any other order can spend the tick, and sends only the damage dealers at it: no tank, no healer. A totem a boss summons is read the moment it stands, even when no bot's attackers list carries it. On the focus the damage dealers cut their own current cast or channel — only an offensive one on an enemy, never a heal, a shield, a buff or a defensive cooldown, never an Auto Shot or a wand's shot, and never a run crowd-control spell, a resurrect or the hearthstone — open with an instant where the class has one, and take the attack by GUID: the skull stays on the boss, so tanks and healers never follow it. A retreat in progress keeps the tick: the totem waits until the living are back. The kill-first check runs every second while the key is on, and the focus is sent again every 3 s while its totem lives and no damage dealer is attacking it, so a damage dealer that left it after one burst comes back to finish it (a kill-first unit that is no totem is focused once, not repeated). The guard and the engage on arrival leave such a totem alone while the key is on, so the focus is not starved for a tick. A totem the plan does not kill first is an ordinary add and stays the guard's, as before. The DPS-only rule is the run leader's own order only: a player's explicit focus on a totem still arms every bot. The server log carries a `TBM:KILLFIRST|<plan>|<lead>|<instance>|<seen|dead|gone>|<entry>|<guid>[|<ms>]` line (the run's lead bot and instance id name its run in a log shared by every run), so a walk measures a kill-first unit's lifetime. See [Dungeon Run Leader](../../play/dungeon-run-leader.md#totem-focus).

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

### `AiPlayerbot.RaidHealerSeatsByClass`
A raid binding's healer seats go by class instead of member order.
**Default:** `0` · **Values:** `0` off (member order), `1` on · **Restart:** not needed (`.reload config` applies it; a raid already bound keeps its seats until it is bound again)

<details>
<summary>How it works in detail</summary>

**Healer seats by class:** the seats are only read with `AiPlayerbot.DungeonRunRaid = 1`, when a raid binding applies its layout:
- the raid seats take a group healer first: priest, then druid, then shaman, then any other healer; the raid keeps at least a third of its healers, rounded up, and the tanks take the rest up to two each
- the main-tank seats take the next healers, any class; a healer the raid seats do not hold back (a paladin) comes before a remaining group healer, so the fixed priest, priest, paladin hire puts the paladin on a main tank and keeps a priest on the raid
- the off-tank seats take whoever is left
- a mid-raid hire joining a bound raid is seated by the joiner rule (the tank with the fewest healers), not this layout
- with `0` the seats are handed out in member order, as before

</details>

### `AiPlayerbot.RaidTankHealerAssist`
A tank healer seated by a raid binding's layout helps the raid while its tank has nothing to heal.
**Default:** `0` · **Values:** `0` off (the seat keeps its focus targets), `1` on · **Restart:** not needed (`.reload config` applies it at once)

<details>
<summary>How it works in detail</summary>

**Quiet seat assist:** only read with `AiPlayerbot.DungeonRunRaid = 1`, and only for a tank healer the bind's layout seated (a player-set focus list outside the layout and a mid-raid joiner do not get it):
- the healer's focus list is still exactly the layout's (the tank and the healer itself); a player who adds or drops a member keeps the tank healer but the seat no longer helps the raid
- the healer's tank is found and alive, is not fighting a boss (an `ai_dungeon_boss` row or a plan boss step, or a world boss), and its health after the damage it is taking is at or above 90%
- only the tank's second (or later) seat may leave; the first seat never does, so the tank keeps one healer
- it then treats any group member below 70% that is in sight and inside heal range; it never walks into sight for one
- the tank and the healer itself keep the ordinary focus treatment
- with `0` the tank healers keep their focus targets only

</details>

### `AiPlayerbot.DungeonRunBossSpread`
Whether the boss tactics widen the ranged raid's spacing while a `spread` row's boss fights.
**Default:** `0` · **Values:** `0` off (the ordinary 10 yd spacing, and a stay order keeps its bot in place), `1` on (needs the boss tactics and the rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Boss spread:** the AI boss tactics table's `spread` kind (see [Boss Tactics](../../play/boss-tactics.md#kinds)): while a row's boss is in the fight the ranged bots and the healers keep the row's `param` yards apart instead of the ordinary spacing, and a stay order no longer exempts a bot from spreading. The row's spell only documents the splash the spacing answers (Hakkar's row: Corrupted Blood 24328 hits 10 yd around a random attacker, the row keeps 12). Melee and tanks never spread.

</details>
### `AiPlayerbot.DungeonRunAvoidCastAll`
Whether an `avoid_cast` row in mode `all` also clears the tanks while its spell is cast.
**Default:** `0` · **Values:** `0` off (the row holds like a default row: a tank the caster is on stays in the blast), `1` on (needs the boss tactics and the rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Avoid-cast all:** the AI boss tactics table's `avoid_cast` mode `all` (see [Boss Tactics](../../play/boss-tactics.md#kinds)): while the row's spell is in its cast window no bot holds the caster, so every tank walks out of that blast too. The Zul'Gurub Bat Rider instakills itself, so there is no threat to keep. A binary without the mode turns the word into an invalid row and skips it, so the row ships in the same change as the code.

</details>
### `AiPlayerbot.DungeonRunBurnMana`
Whether the boss tactics keep their `burn_mana` rows: a few eligible bots drain the boss's mana while it stands above the row's start percent.
**Default:** `0` · **Values:** `0` off (the rows are ignored, as before the kind existed), `1` on (needs `AiPlayerbot.DungeonTactics`, or a commander-bound group) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Burn mana:** a boss tactic row of the `burn_mana` kind (see [Boss Tactics](../../play/boss-tactics.md#kinds)) names the start percent of the boss's mana (`param`) and the most burners (`priority`, `0` = every eligible bot). Eligible means alive, in the group, in combat, not a tank, not a healer (a shadow priest counts as damage), knowing `mana burn`, `drain mana` or `viper sting`, and able to cast it right now: the bot must be in control (not stunned, feared, confused, charmed, silenced or pacified), its spell must be off cooldown, the boss must stand in that spell's own range (`mana burn` 30 yd, `drain mana` 20 yd, `viper sting` 8 to 35 yd) and the bot must have the cast's mana. Every bot ranks the members that run the boss tactics and can cast right now by guid and takes its own place, so an unavailable member never holds a slot and the same few bots burn without a vote. A global cooldown, an in-flight cast or movement is not a reason to move a slot: the burner keeps its place and casts when free. While the boss is in the fight, alive and at or above the start percent, a burner casts its first castable of the three on the boss, above its ordinary damage rotation and below every survival and avoid action; a hunter refreshes Viper Sting only when the boss lacks it, and a boss in the stone phase (Moam's aura 25685) is not burned at all: the burners go back to the `kill_first` Mana Fiends. The `BossTacticsBurnMana` row (`spell=<id>, boss_mana_pct=<n>`) goes to `bot_events.csv` when that log is on. Moam's Drain Mana 25676 fills him from the raid's own casts and Arcane Eruption 25672 fires at full mana (boss_moam.cpp:65,166-170,209-215), so the burners keep him from a full bar outside the stone phase.

</details>
### `AiPlayerbot.DungeonRunRangedOnly`
Whether the boss tactics keep their `ranged_only` rows: melee bots leave a subject that stands the row's gap above them and take the adds.
**Default:** `0` · **Values:** `0` off (the rows and the held target are ignored, as before the kind existed), `1` on (needs `AiPlayerbot.DungeonTactics`, or a commander-bound group) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Ranged only:** a boss tactic row of the `ranged_only` kind (see [Boss Tactics](../../play/boss-tactics.md#kinds)) names the subject (`target_entry`, `0` = the row's boss) and the vertical gap in yards (`param`, 3 to 40). A melee bot (melee damage or a tank) standing `param` yd or more below the subject skips it in the free-damage and tank picks and drops it if it already holds it, then takes a `kill_first` pick or the nearest attacker that is not held; a ranged bot and a healer are never held. An explicit attack order beats the hold: an ordered bot keeps the subject until the order is cleared (as for `hold_kill`). The heights are the two units' own z, never the navmesh. When the subject lands the gap closes and the hold ends by itself. The `BossTacticsRangedOnly` row (`subject=<entry>, gap=<yd>`) goes to `bot_events.csv` at most once per 5 s per bot. Ayamiss climbs 20 yd for her air phase (boss_ayamiss.cpp:193-205).

</details>
### `AiPlayerbot.DungeonRunCharmYield`
Whether a bot a creature charms yields its AI, and no action lands on a charmed group member.
**Default:** `0` · **Values:** `0` off (bots play as before), `1` on (needs no rows or tactics) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Charm yield:** while the key is on, a bot a creature has charmed yields its AI to the charmer (no engine runs, no move goes out), so nothing it does fights the core's own charm control; a bot a player charms is the core's business and is left alone. On any map, while a group member has a non-player charmer, every area action (class area damage, thrown explosives, a tank area such as Thunder Clap, the Sweeping Strikes and Blade Flurry toggles, and damage-dealer area damage) whose target point — or the bot itself, for a self-centred one — lies within 10 yd of such a member stands down. The same member is never a target: the damage-dealer and tank picks skip it (the attacker list, the possible targets, an explicit order and a raid mark), an attack order on it is refused, a pet is never sent at it, and every pet and guardian of the group's bots is parked passive with its attack stopped for as long as such a member is up (a pet that reacts to the member on its own, or keeps an attack started before the charm, cannot strike it; the stances come back when none is up), its cast is never interrupted, and a harmful single-target action aimed at it (a swing, a taunt, a damage, interrupt or crowd-control cast, a wand shot) stands down. A target already held is dropped on the next tick. Healing, buffing, curing and protecting the member stay allowed. Hakkar's Cause Insanity 24327 is why (his victim fights the raid for 10 s, and five charmed damage dealers were killed by their own raid within 3-7 s), and the same yield covers Will of Hakkar 24178, which only the Zul'Gurub trash-pile Guru casts.

</details>

### `AiPlayerbot.DungeonRunImmuneBossVictimEscape`
Makes the victim of a boss that cannot be taunted escape at once when it is not a tank. Recommended on.
**Default:** `0` · **Values:** `0` off (bots play as before), `1` on (needs no rows or tactics) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Victim escape:** a boss the game makes immune to taunts that also carries a `threat_successor` boss-tactic row (Hakkar and Jin'do today; another boss opts in by such a row, and fixate bosses such as Buru stay out) cannot be taken back by a tank once it turns on a damage dealer or a healer, as Hakkar does after each Cause Insanity. With the key on, that victim escapes at once with its own ability when the bot's full cast check passes for it (silence, stun, pacify, reagents such as Vanish's Flash Powder, category cooldowns and power all count; the bot's own cast in progress and its global cooldown do not, since the escape breaks that cast): a hunter Feign Death, a rogue Vanish, a mage Ice Block (Frost talent only), a paladin Divine Shield (no Forbearance). With none castable (a warlock, a warrior, a druid, a shaman, a priest, a real player, or a cooldown or a silence), a paladin within 30 yd whose Blessing of Protection (Hand of Protection on this core) passes its own cast check on the victim casts it, unless the victim carries Forbearance. The game passes over a target immune to the boss's melee, so the boss turns to the next member on its threat list; Feign Death and Vanish drop the victim's threat. Feign Death can be resisted: an escape counts only once a later read sees it land (the feign-death state, or the victim immune to the boss's melee); one that did not land holds the victim for 1 s, then the paladin may protect. Fade is not used: on this core it moves no threat.

The rules: never on a tank; one landed escape (own or protection) per victim per 6 s; never when the victim is the only living non-tank on the boss's threat list (the boss would come straight back); never while the victim holds a duty (`reason=duty`): the Son walk-out puller, a member holding a pulled Son for its meet, or a member that another live, engaged hostile creature that is not a boss (a Son, an add) has as its victim, since Feign Death or Vanish would wipe that creature's threat too. No boss is named in the code. The combat-engine copy rides on the always-on `dungeon` strategy and the reaction-engine copy on `avoid aoe`: a fight strategy that drops `avoid aoe` (Vaelastrasz) keeps only the combat-engine copy, which waits for the bot's own cast to end. Each escape writes a `BossTacticsVictimEscape` row (`boss=<entry>,victim=<name>,class=<class>,spell=<id>,result=ok|refused`, `reason=own|protect|cast|duty|only_target`) to `bot_events.csv`; a refused row at most once per victim and writer per 6 s.

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
- never marked: pull target, tank selection, held add, fight focus, `kill_first`, held unit, hit unit, healer-guard add, a non-elite (NORMAL-rank) unit the plan does not list, a `cc_never` entry of the plan, and a DB-spawned unit that respawns within a minute (it would be marked again on every respawn; a temporary summon's lifetime is not a respawn) — and such a respawning unit is not counted as a body of the pull either
- a non-elite unit neither counts toward the pre-pull body count nor the fight's engaged total that starts a mark, and neither does its hold: only elites, rares and bosses do; a `cc_never` unit still counts as a body (it is dangerous even though it cannot be held)
- the plan's `cc_never` list (`ai_dungeon_plan`) excludes those entries from the automatic pick entirely, before the pull, in the fight and on a re-send; the plan's own list with the key off is unchanged
- in the fight: marks joiners two seconds apart; re-sends a broken mark twice per unit; a dead bot frees its hold
- tanks and the lead bot never do crowd control; a healer only while 4 healers (1 in a party) stay free, holding one unit
- at most 24 auto orders a step; uses seven marks (skull stays the fight focus); a player's icon is never moved
- a fight held open only by the run's own crowd control is broken one unit at a time: the tank takes the hold that ends soonest, the party kills it, then the next; never two at once, and the broken hold is never re-applied

</details>

### `AiPlayerbot.DungeonRunAutoCcAbove`
How many bodies a pull may bring before the automatic crowd control starts.
**Default:** `6` · **Values:** `2` to `40` (crowd control starts at the body after it; `6` starts at 7) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Threshold:** the run's body count is the elite bodies of the pull's estimate when it read the pull (either the ring estimate under `AiPlayerbot.DungeonRunAggroEstimate` and `AiPlayerbot.DungeonRunAggroWalkRing`, or the joiner estimate with the target), else the row's own living pullable units that are not non-elite (a NORMAL-rank unit is no body, and no mark unless the plan lists it). The threshold is the smaller of this key and the row's ceiling (`max_bodies`), so a deliberately sized pull still gets crowd control. Only read while [`AiPlayerbot.DungeonRunAutoCc`](#aiplayerbotdungeonrunautocc) is `1`.

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

**Seek aura:** a boss tactic row of the `seek_aura` kind (see [Boss Tactics](../../play/boss-tactics.md#kinds)) names the source creature whose death leaves the cloud (`target_entry`), the aura the raid must carry (`spell_id`), and the boss spell whose aura on a group member restarts the cycle clock (`cycle_spell`, every `cycle_s` seconds, the dose window opening `window_s` before it is due and staying open for up to 8 s past the due time when a sighting is missed, after which the estimate rolls one `cycle_s` forward and the window waits for the rolled cycle's own opening; the first siphon is due one `cycle_s` after the boss is first seen in combat with the group, so cycle 1 gets the same window). The source is held and killed at the row's spot (`spot_x/y/z`): the DPS stop at its `priority` health % until the gather is ready (every seeker within `param` yd of it, 8 s at most, the burn forced near the end of the window), then burn it so the cloud lands on the gathered raid. Pets and guardians leave the siphon too: a hunter's pet for a few seconds around the cycle and a warlock's demon from the engage on (its summons are held all fight), because each is one more undosed unit. Only the one picked source is ever damaged: another source stays held, a source farther than 35 yd from the spot is never chased unless it stands inside the boss's `anchor` circle, and with the dose window shut the pick is left at whatever health it has for the next cycle. A bot outside the fresh cloud walks in while it still lives; the cloud is exempt from every ground-hazard avoid while its row runs. Hakkar's row: one Son of Hakkar 11357 dies at B every 90 s before Blood Siphon 24324 so the raid carries Poisonous Blood 24321; the main tank and the first off-tank never seek, and the second off-tank pulls and holds the source (a hunter pulls it when it is not there).

</details>

### `AiPlayerbot.DungeonRunSeekPullCycle`
Whether the boss tactics keep their `seek_pull` rows: the seek walk-out pulls and kills one source in every cycle.
**Default:** `0` · **Values:** `0` off (the walk-out keeps its old one-walk-out-per-cycle rules), `1` on (needs `AiPlayerbot.DungeonRunSeekAura` and the boss tactics, or a commander-bound group) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Seek pull:** a `seek_pull` row names the seek source (`target_entry`), the most walk-outs started in one cycle (`priority`, 1-5), the kill lead in seconds (`param`, 10-85) and the last start in seconds (`window_s`, 10-85); it carries no spell, spot, cycle columns, gate or mode word, and the walk-out reads it straight from the table. With the key on and a row for the fight, each source is walked again in every cycle (the walked list is per cycle), up to the row's walk-outs start a cycle while the siphon is at least `window_s` seconds away, a failed walk-out reopens the cycle (a puller-side abort frees its source and leaves that puller out of the picks for 60 s; a no-stand abort, a vanished source and a timeout keep it walked for the cycle), the stand search fans 10 and 20 degrees either side of the source's bearing and falls back to a hunter's reach (hunter only) when nothing fits at a shorter one, a puller that stands at its stand 6 s without landing the shot aborts unspent, and an engaged source is burned from `param` seconds before the siphon. With the key off, or no row for the fight, the walk-out keeps its old rules: one walk-out per cycle, no retry, one fixed stand ray and the 30 s late-engaged lead.

</details>

### `AiPlayerbot.DungeonRunSeekSonMeet`
Whether the pulled Son meets a waiting off-tank instead of walking back to the tanks' circle edge with the puller.
**Default:** `0` · **Values:** `0` off (the walk back ends at the circle's edge, as before), `1` on (needs `AiPlayerbot.DungeonRunSeekPullCycle` and a `seek_pull` row) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Son meet:** when the walk-out's shot lands, the raid's third tank (else the second off-tank, never the main tank) is picked as the receiver if it is alive, uncharmed, not the boss's victim, not already holding a Son and inside the tanks' circle; the choice and its refusals (`BossTacticsSonMeet`, `reason=dead:1|busy:1`) are logged. The receiver keeps running its normal tank AI; the puller's walk back follows the receiver's live position (clamped into the circle) and ends within 8 yd of it (or at the old circle-edge point, whichever comes first) instead of at the circle's edge, and the receiver takes the Son as soon as it is within 8 yd or on it, so the taunt is in melee range; the take row says why it did or did not taunt (`reason=chase_refused|no_taunt|attack|taunt,meet=1`). The receiver waits (no other Son, pull or drag) only while the Son's victim is the puller, the returner or nobody (`event=wait,reason=receiver_wait`); a Son on anyone else closes the meet as `peeled` and the normal take branches handle it. Every update revalidates the receiver: charmed (`receiver_charmed`), Hakkar's victim (`receiver_victim`) or holding another Son (`receiver_busy`) ends the meet and the puller walks to the circle edge again. The meet also ends when the Son or the receiver dies or leaves (`son_dead`, `receiver_dead`), when the Son's victim is any player tank (`event=taken`), or 20 s after the shot (`timeout`).

**Held for the tank:** while the meet is open and no tank has the Son, the damage dealers hold it: the 5 s non-tank release, the 20 s cap and the late-engaged forced burn do not apply to it, and its puller and returner count on neither non-tank clock (so the second tank does not take it off the puller). The `peeled` row names who took the Son (`victim=`, `victim_role=`, `son_yd=`).

</details>

### `AiPlayerbot.DungeonRunSeekTakeEdge`
Whether a tank takes a Son of Hakkar that stands just outside the raid's circle when the Son's victim stands inside it.
**Default:** `0` · **Values:** `0` off (the take refuses every Son outside the raid circle, as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Take edge:** the anchored take never chased a Son out of the raid's 30 yd circle (Taunt and Growl are melee), but the bots a Son hits stand clamped on the circle's edge, so the Son stood at 31-45 yd and walk 54 refused 56 % of its take rows (`reason=chase_refused`). With the key on, the take chases a Son outside the circle only when all of these hold: the Son's victim stands inside the raid circle (plus 2 yd), the Son's z is 46.5 or higher (above Hakkar's 45.8 evade line), the Son is within 8 yd of the circle (38 yd from the spawn) and within 10 yd of that victim. Anything else stays refused so the tank is never led far off the circle (`chase_refused`; `edge_far` when only a distance fails). A Son inside the circle is never refused. Only a tank that is not Hakkar's victim takes. Applies to every take of a Son (the healer take, the pickup take, the meet's take).

</details>

### `AiPlayerbot.DungeonRunSeekMeetNoHeal`
Whether healers leave the pulled Son's puller alone while the Son's meet is open.
**Default:** `0` (recommended `1`) · **Values:** `0` off (the puller is healed as any member), `1` on (needs `AiPlayerbot.DungeonRunSeekSonMeet`) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Why:** walk 55 peeled 24 of 27 meet Sons onto a healer. The puller holds only one shot of threat; a heal on it adds threat on every mob that has it on its list (heal threat x0.5, split over Hakkar and the Son), 250-375 on the Son per 1-1.5k heal. **Rule:** while the meet is open (at most 20 s), the heal-target pick (`party member to heal`) skips the meet's puller, unless its health after the hits its attackers land in one swing is under 40 %. The pick reads the meet once, so every heal, HoT and Power Word: Shield a bot aims at a member is covered. The meet closing lifts it. The returner is the same bot as the puller in the normal path and is held with it; an unrelated walk-out puller is not.

**Who is affected:** every bot that heals a member through this value, so off-heal hybrids (a retribution paladin with `offheal`, for one) skip the puller too, not only healers. A bot's own heal of itself does not go through this value and is unchanged. With the key off, or while no meet is open anywhere on the server, the pick takes no lock (one atomic count is read).

**Limits:** a group heal centred on another member (Prayer of Healing, Chain Heal bounces) can still land on the puller. A Renew already ticking on the puller before the meet opened keeps ticking, and each tick still adds threat. The puller's pet is not held. The incoming-damage estimate counts only attackers already in melee reach, so while the Son is still running at the puller it reads 0 and the floor sees the plain health. At the 40 % line the healers' triage picks its biggest heal, which carries the most threat; that is the trade.

</details>

### `AiPlayerbot.DungeonRunSeekWalkoutFocus`
Whether the Hakkar walk-out puller keeps to its walk-out until its shot lands. Recommended on.
**Default:** `0` · **Values:** `0` off (other actions may take the puller's ticks, and it turns and casts in one tick, as before), `1` on (needs `AiPlayerbot.DungeonRunSeekPullCycle` and a `seek_pull` row for the facing) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Walk-out focus:** walk 55 lost 15 cycle-1 walk-outs to a shot that never went out: the puller stood in range and sight of its Son but ran `follow`, `hunter step out`, `wait for attack` or another action, and in 10 of 13 no-shot aborts it was not facing the Son. With the key on, these change while this bot is the live walk-out puller and its shot has not landed:
- Combat actions are held; reaction actions still run. Every combat-engine action except the walk-out (`boss tactics seek tank`) and the pet's dismissal (`boss tactics seek pet`) is held, Feign Death included. Reaction-engine actions (flee, avoid cast, tank swap) are not held: they are the puller's survival moves, the walk-out already aborts on threat, on becoming Hakkar's victim and on a charm, and walk 55's frames show no reaction action preempting a puller.
- Below 40 % health nothing is held, so potions, healthstones and Mend Pet run as usual; the walk-out's own aborts are unchanged.
- A dead or charmed puller holds nothing, nor does a task 25 s old or older. Once the shot lands the hold lifts, so the walk back is not blocked.
- A bot off a `seek_aura` map, and every bot while no walk-out task runs (a walk back does not count), skips the hold without taking the walk-out lock.
- The first held action of each walk-out logs `seek pull: walk-out preempted action=<name>` (one row per walk-out). The engine asks the hold only about an action it is about to run, so the name is the action that would have run that tick, had the walk-out not been chosen or had it returned false.
- At the stand, a puller that does not face the Son stops, turns this tick and casts on the next one, instead of casting in the same tick as the turn.
- A hunter opens with Arcane Shot when it can cast it now, else Auto Shot (never a DoT, so never Serpent Sting). Every other class keeps its shot (wand Shoot, Wrath, Lightning Bolt).
- For 2 s after the pull cast the puller waits for the Son to join the fight instead of casting again, and the 6 s stall does not abort it as `no shot` (the instant Arcane Shot leaves no cast behind while it travels).

</details>

### `AiPlayerbot.DungeonRunSeekWalkoutHoldCombat`
Whether the Hakkar walk-out puller stays in combat until its shot lands. Recommended on.
**Default:** `0` · **Values:** `0` off (the puller may drop to the non-combat engine mid-task, as before), `1` on (needs `AiPlayerbot.DungeonRunSeekWalkoutFocus`: without it nothing is held) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Hold combat:** walk 56 lost 5 of its 6 `timeout shot=0` walk-outs to the puller's own AI leaving the combat engine inside the task: the walk-out is a combat-engine action, so on the non-combat engine nothing ran it and `follow`, food or drink walked the puller off its stand until the 25 s ran out. With the key on:
- It needs `AiPlayerbot.DungeonRunSeekWalkoutFocus` on as well: the focus keeps the held puller's other combat actions (a target pick on Hakkar, its rotation) from running. With the focus off nothing is held and the pick is unchanged.
- `combat end` does not fire while the bot is the live, unshot walk-out puller: recorded as the instance's puller, alive, not charmed, inside the task's 25 s, the shot not landed, and Hakkar alive, in combat and not evading. The hold ends with the task however it ends (shot, timeout, abort, death, charm) and with the fight (a wipe, an evade or a kill); the walk back is not held.
- The pick skips a candidate whose AI is not on the combat engine, or that is eating or drinking (`no_combat` in the no-puller row). The bot's own engine is read, never its pet's combat.
- Only the instance's own record is read, so two raids in different instances never hold each other's bots; with no task running the check costs no lock.

</details>

### `AiPlayerbot.DungeonRunSeekWalkoutFaceCast`
Whether the Hakkar walk-out puller turns to its Son and casts in the same tick. Recommended on.
**Default:** `0` · **Values:** `0` off (with `AiPlayerbot.DungeonRunSeekWalkoutFocus` the turn takes one tick and the cast the next, as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Face and cast:** walk 56 had 8 of its 11 no-shot aborts stopped in range and sight of the Son but not facing it. The bot's own per-update facing (`PlayerbotAI::UpdateFaceTarget`, every 500 ms) turns a stopped bot in combat to its current target, Hakkar, and it runs before the walk-out, so the walk-out's turn on one tick was undone before its cast on the next. With the key on:
- At the stand the puller stops, turns to the Son with the instant forced turn (an orientation set plus a heartbeat, not a spline) and casts in the same tick.
- `UpdateFaceTarget` leaves the live, unshot walk-out puller's facing alone. Every other bot, and the puller once its shot landed (the walk back), is turned as before.
- The 6 s `no shot` clock (a `seek_pull` row's stall rule) counts only the time the puller is in shot position: the Son in shot range and sight, the puller on the stand's floor (z 46.5 or higher) and not moving. Walk 56's Sons patrolled out of sight under the ledge while the wall-time clock ran.
  - Each read of the puller's own walk-out tick adds the whole gap since the previous read when both were in shot position, at most 2 s a gap (the cast's 1.5 s retry delay plus the react delay; the cap only guards against a clock jump), so slow ticks still abort at about 6 s of in-shot time.
  - A wall-time backstop: 12 s after the puller first stood in shot position in this task, the `no shot` abort is due whatever the count, so a walk-out tick that stops running still aborts. The 25 s task timeout bounds the walk-out as before.
- With the key on, the stop and the forced turn apply to any walk-out, even one without a `seek_pull` row (off, the turn needs the row). Only Hakkar has a walk-out today.
- After the stop, a spline that has not finished yet may rewrite the orientation on the next unit update; the cast in the same tick runs first, and the pull shots are instant or auto-repeat.
- The stand's floor (z 46.5) is a Hakkar value (above his 45.8 evade line). It already gates the stand and the pick, and it moves into the row data before a second `seek_pull` boss is added.

</details>

### `AiPlayerbot.DungeonRunSeekWalkoutStandLog`
Whether the Hakkar walk-out writes its diagnostic stand rows. Diagnostic: changes no behaviour, safe to turn on for test walks.
**Default:** `0` · **Values:** `0` off, `1` on (rows go to `bot_events.csv`) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Stand rows:** walk 56 could not tell from its frames why a puller in range and sight never shot: the frames carry no facing and no cast refusals. With the key on, while the walk-out puller walks out unshot:
- One `seek pull: walk-out stand branch=<word> state=<c|n> inrange=<0|1> los=<0|1> front=<0|1> stopped=<0|1> cast=<spell id> refuse=<reason> tick_ms=<ms>` row a second (the walk-out's first read writes one at once, so a 25 s task writes at most 26). The branch is the one its tick took; `tick_ms` is the time since its previous walk-out tick, so a gap shows a tick that did not run.
- Every pull cast that `CastSpell` refuses logs `seek pull: walk-out castrefused spell=<id> reason=<reason>` at once, at most 10 a walk-out.
- The words are listed in [`refusal-rows.md`](../../play/refusal-rows.md).

</details>

### `AiPlayerbot.DungeonRunSeekWalkoutStandSight`
Whether the Hakkar walk-out's stand search tests sight the way the bot does. Recommended on.
**Default:** `0` · **Values:** `0` off (the search aims from the floor + 2 at the Son's z + 2, and every refused move counts toward the abort, as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Stand sight:** 12 of walk 56's 13 `walk-out abort move failed` rows were pullers already standing on their stand (`len` 0.6 yd or less, `why=dispatch-short` or `arrived-recalc`). The stand search tested sight from the floor + 2 yd to the Son's z + 2, but the bot's own test (`IsWithinLOSInMap`) aims from its z + its collision height to the Son's z + the Son's collision height (`Unit::GetLosCheckPosition`); the Sons stand 11-18 yd below the ledge, so the lip could block the bot's line at a stand the search had passed. Out of shot position on its own stand, the puller asked to move 0.5 yd, the mover refused, and three refusals aborted the walk-out. With the key on:
- The stand search aims from the floor + the bot's collision height to the Son's z + the Son's own collision height, the core's own sight heights for two units.
- The start scan plans the first stand with the eye height of whichever raid bot reads first (the puller is picked afterwards); the puller's own re-plan corrects it within 1 s.
- A refused outbound move within 3 yd of the stand does not count toward the three refusals; the stand is re-planned at once instead. A task gets 3 such free re-plans; after that refusals at the stand count again, so a stand the re-plan keeps finding still ends in `move failed` and frees the Son. Real path failures farther out always count.

</details>

### `AiPlayerbot.DungeonRunPlaceUnstick`
Whether a bot cut off on a navmesh island inside a dungeon or raid is teleported to its master. Recommended on.
**Default:** `0` · **Values:** `0` off (such a bot stays where it is, as before), `1` on · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**The snag:** a teleport, summon, knockback or a placement spread can put a bot on a small piece of navmesh with no path to the rest of the mesh (ZG walk 55: the second seat of group 1 and the fourth of group 4 at the Jin'do place point, every raid). Every move then asks the navmesh for a route, gets `NOPATH`, and the mover refuses the lone fallback point: the action log shows `why=dispatch-short ... path=0x8` every tick and the bot never moves. The follow's own rescue walks with the same mover and fails the same way.

With the key on (`PlaceUnstickPolicy.h`):
- **The clock:** every refused move of the bot inside a dungeon or raid that is a dispatch-short with a `NOPATH` route feeds a clock. A move that launches stops it; any other refusal (a clipped real path, a hazard, already arrived) neither feeds nor stops it. The clock starts again after a gap of more than 5 s between two such refusals, on a new map or instance, and when the bot has moved 3 yd or more from where the clock started. Refusals in the open world never count.
- **The gates** after 10 s on the clock: the bot is in a dungeon or raid (never a battleground); its master (its player, or the bot it follows; else the group's master) is alive on the same map more than 20 yd away; the bot is alive, not charmed, not possessing another unit (Mind Control, Eyes of the Beast, Eye of Kilrogg; its own pet or an enslaved demon does not count) and not casting; the master is not on a transport; the master is not fighting the bot's own target within 5 yd of the bot (a fight, not a snag); no teleport in the last 30 s.
- **The cap:** the master must also be within the sight distance (`AiPlayerbot.SightDistance`, the bound `.bot summon` uses to teleport a bot that cannot walk to its player), unless the bot's group is driven by a dungeon run or raid walk commander.
- **The island proof:** the core's `NOPATH` does not say which end failed: a bot on the main mesh chasing a mob on a ledge, or following a master on a crate, gets it too. So one navmesh query runs from the bot to its master (the core's own poly pick, ground and water). The bot is cut off when no walkable poly lies under it, or when the path to the master is not complete and either stops within 2 yd of the bot, or starts on an enclosed region: the polygons connected to the bot's within a 15 yd circle (at most 128, the search not truncated) all lie within 14 yd of it, so the region never reaches the circle's edge. A several-yard island whose partial path still gets a few yards toward the master is caught that way; the main mesh always reaches the edge. A complete path, or a partial one on a large region, proves nothing; the clock then starts again, so the query runs at most once every 10 s.
- **Known gaps:** a master standing off the mesh (on a crate, a rock the navmesh skips) proves nothing about the bot's start, so a bot cut off while its master stands there is not rescued until the master steps back onto the mesh. A bot that slides 3 yd or more on its island (a knockback, a fear) starts its clock again from there.
- **The teleport:** the bot lands beside its master, on the spot `.bot summon` would pick (the master's floor, in its sight), wherever the master stands, inside a boss room too. Unlike the summon it keeps the bot in combat (its targets, threat and attackers stay), takes its pet along only when the pet is stuck too (alive and within 5 yd of the island start, or cut off itself by the same query from its own spot; a pet on the main mesh may be tanking and walks after its owner by itself), whispers nobody and leaves the stay and guard points alone, except one within 5 yd of the island start, which is cleared so the bot does not walk back onto it.
- **The rate limit:** at most one teleport per bot every 30 s; a refused teleport waits for the cooldown and a fresh 10 s clock too.
- **The row:** each try writes one `BotUnstick` row (info1 the master's name, info2 `reason=nopath_island secs=<n> master_yd=<n> result=teleported|refused`).

</details>

### `AiPlayerbot.DungeonRunSeekNearestTake`
Whether the first off-tank takes a Son of Hakkar off a non-tank at once when its taunt reaches and the receiving tank's does not. Recommended on.
**Default:** `0` · **Values:** `0` off (the first off-tank takes only after the Son sat 3 s on the non-tank, as before), `1` on (needs the live `anchor` row) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Nearest take:** walk 55's Sons on a non-tank were taken almost only by the receiver (the third tank), mostly with `attack` (walking, the taunt out of reach or on cooldown), while 1-3 other tanks stood free a median 12.9 yd away. With the key on, the first off-tank (tanks[1]) takes a Son at once, without the 3 s clock, when all of these hold:
- the Son fights a non-tank, and it is not the open meet's Son;
- the Son's victim stands inside the raid circle (plus 2 yd) of the live `anchor` row (no live anchor: no nearest take, and the 3 s rule applies silently);
- the receiver is more than 8 yd from the Son, or its taunt is not ready;
- the first off-tank is within 8 yd of the Son with its taunt ready, is not Hakkar's victim and holds no Son.

"Taunt ready" is an explicit read with the taunt-after pick's own guards: in control (not stunned, feared, confused, hexed or charmed), not silenced or pacified, a taunt known (Taunt, Growl, or Turtle's Hand of Reckoning for a paladin), that spell's own cooldown ready, a warrior in Defensive Stance and a druid in Bear Form or Dire Bear Form. The global cooldown alone does not make a tank "not ready", and range is the 8 yd test. With the key on, the take also casts that known taunt, so a paladin taunts with Hand of Reckoning. A human (non-bot) tank always reads as not ready, so a human receiver never holds the first off-tank back. The main tank never takes. While the key is on, the receiver also leaves a fighting Son whose victim is the first off-tank, so it does not walk over and taunt back a Son the first off-tank just took; a Son on the main tank or any other tank is still the receiver's to take. Otherwise the receiver keeps its take and the 3 s rule still applies. The take row is the usual `seek take: ... reason=<word>`.

</details>

### `AiPlayerbot.DungeonRunSeekNearestTakeReach`
Whether the first off-tank reaches a walked Son of Hakkar on a non-tank from up to 20 yd, not only 8 yd. Recommended on.
**Default:** `0` · **Values:** `0` off (the 8 yd nearest take only, as before), `1` on (needs `DungeonRunSeekNearestTake` and the live `anchor` row) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Reach:** in walk 57 the identified Son kills were mostly walked Sons whose meet was peeled (1.02 a fight) or that were met and then sat on a non-tank (0.69 a fight), while the 8 yd nearest take fired only 20 times in 58 fights. With the key on, on top of the 8 yd rule, the first off-tank (tanks[1]) takes a Son at once when all of these hold:
- the nearest take's own Son rules hold (on a non-tank, not the open meet's Son, its victim inside the raid circle plus 2 yd);
- the Son is a walked one: the walk-out walked it this cycle, or its meet was just lost;
- the receiver (tanks[2]) is more than 15 yd from the Son, or the Son's meet closed as `peeled` or `receiver_victim` in the last 10 s and the receiver cannot take it itself (it is not within 8 yd with its taunt ready: two tanks never taunt one Son);
- the first off-tank is within 20 yd of the Son (the take's Attack closes the gap) with its taunt ready, is not Hakkar's victim and holds no Son;
- its leaving Hakkar does not collapse the damage dealers' threat cap: the cap's own reference pick (the `threat_successor` pick, with `DungeonRunThreatSuccessorNotHolder` as set, read from the very tank list the cap uses) is re-run with the first off-tank marked off Hakkar, and the take is held back when that pick has no off-tank reference left, or one under half of today's. That holds whoever holds Hakkar (the main tank, an off-tank or nobody); a pick that has no off-tank reference today has none to lose.

With the key on, the same cap guard also holds back the first off-tank's 3 s healer-clock take of a Son (the branch then has no take that tick), but only until the Son has sat on that non-tank for 6 s, or at once when its victim is under 40 % health: then the clock takes it whatever the cap; key off, the healer clock is unchanged. A guarded take (`nearest_reach` or `healer_clock`) that really started (its attack or chase command went out, the tank was already after it, or its taunt was cast) is latched for its Son in the fight's walk-out record, so it is not re-checked (and abandoned) mid-walk. The latch holds 10 s; a refused or failed take clears it, and so does that Son dying or reaching any tank, so a later peel of it checks again; another Son or another tank checks again too (with no walk-out record yet there is no latch, and each tick checks).

The take row says `src=nearest_reach` when only the reach let the take through (`src=nearest` when the 8 yd rule did).

Diagnostics (always on with the key, walk 58 F3: the guard logged nothing, so its effect could not be measured): a `BossTacticsSeekPull` row `seek take held son=<g> taker=<name> reason=cap_guard ref=<name> share=<pct> src=nearest_reach|healer_clock victim=<name> on_nontank_ms=<ms> victim_hp=<pct>` whenever the collapse guard holds back a reach take (the reach would take the Son, the guard alone stops it) or a healer-clock take, once per tank and Son per 2 s. `ref` is today's cap reference tank, `share` the reference left after the leave as a percent of today's (0: no off-tank reference left); `victim`, `on_nontank_ms` (the Son's time on that non-tank, the fight's non-tank clock) and `victim_hp` say what the hold cost. A held row and a take row can come from the same tick (a held reach take while another path takes the Son, or the healer clock's later override): join them on time, not by order.

Limits (documented, not fixed): "walked" is scoped to the current Blood Siphon cycle (the walk-out record forgets the Sons of earlier cycles), so a Son walked last cycle and never lost counts as one that joined by itself.

</details>

### `AiPlayerbot.DungeonRunSeekMeetPrePlace`
Whether the receiving third tank walks to the puller's way back when a Hakkar walk-out starts, so the pulled Son meets it sooner. Recommended on.
**Default:** `0` · **Values:** `0` off (the receiver stands wherever its tank AI left it until the meet, as before), `1` on (needs `DungeonRunSeekSonMeet` and a `seek_pull` row) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Pre-place:** in walk 56 the receiver stood a median 47.5 yd from the puller when the shot landed, so the Son spent 5-10 s on the hunter among the healers before a tank reached it, and 8 of 21 peels were the puller dying. With the key on:
- when the walk-out starts, the third tank (tanks[2]) is pre-placed if it is alive, not charmed, not Hakkar's victim, holds no Son and stands inside the tanks' circle; otherwise there is no pre-place and nothing changes;
- it walks to a point 2 yd inside the tanks' circle edge (23 yd for Hakkar's 25 yd tank radius) on the line from the anchor's centre toward the puller's stand, so a small step past it still counts as inside the circle at the shot; a point whose floor is off the platform's level (the anchor clamp's own floor read) gives no pre-place and a `event=preplace_skip,reason=floor` row;
- its own walk to the point runs on until it is within 1 yd; from then on, within 4 yd of the point, it holds there until the meet starts (then while it waits for the Son) or the pre-place ends: any other walk (its tank AI's chase) is stopped, and otherwise its rotation keeps the tick, so it does not walk off the point. A melee tank at the point stands out of Hakkar's melee reach, so its threat on him comes only from ranged or self-cast abilities: walk 57 watches tanks[2]'s threat;
- a Son that already fights near the spot (on a healer or anyone else) comes first: tanks[2] takes it as today and leaves the point. That is deliberate: a Son on a healer outranks the pre-place. If tanks[2] still holds that Son at the shot, the meet's pick refuses it as `busy`, falls back to the second off-tank as today, and the pre-place ends;
- the meet's receiver pick at the shot is today's (the third tank first), so the pre-placed tank is normally the receiver; the puller's walk back then ends at it as before;
- the pre-place ends when its meet closes, when the task ends without a meet for it, when another tank is picked at the shot, or when the tank dies, is charmed or becomes Hakkar's victim;
- a `BossTacticsSonMeet` row `event=preplace` names the tank, its distance to the puller and to the point when the task starts; the shot's `event=meet` row keeps `receiver_yd=` (the receiver's distance to the puller at the shot).
- a new walk-out that starts while an older pre-placed meet is still open replaces the pre-place: the old meet runs on under today's rules (its receiver waits and takes without the hold), and the tank may be pre-placed again at the new task's point. The start scan refuses a start while any Son fights near the anchor, so this is rare.

Known limits (not fixed):
- the point is set from the stand at the start and does not follow the stand's re-plan while the Son patrols (the puller's walk back still ends at the receiver's live position, only longer);
- while it is pre-placed, the third tank's own pull of a Son that wanders into the tanks' reach waits until the pre-place ends;
- a point on the north half of the circle stands up to 40 yd from the gather spot, so a Son fighting at the spot is a longer run for tanks[2] (the first off-tank's nearest take and healer clock still cover a Son on a non-tank);
- a dodge (`avoid aoe`, `boss tactics avoid cast`) outranks the hold while the tank stands in the effect, but once it is just outside, the hold may stop the dodge's walk at the effect's edge, and a dodge that carried it more than 4 yd off is walked back toward the point;
- the move's height is re-derived from the ground at the point (the walk's ground snap), not taken from the floor the start checked.

</details>

### `AiPlayerbot.DungeonRunSeekMeetPrePlaceLate`
Whether the pre-placed third tank at Hakkar waits on Hakkar until the walk-out's puller nears its stand, instead of walking when the walk-out starts. Recommended on (its value is still being tested on the A/B arm).
**Default:** `0` · **Values:** `0` off (the walk starts with the walk-out, as before), `1` on (needs `DungeonRunSeekMeetPrePlace`) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Late walk:** in walk 57 the pre-place started a median 11 s before the shot, the third tank's threat on Hakkar at Cause Insanity fell 24 %, and it still stood a median 42.8 yd from the puller at the shot. With the key on, the pre-placed tank keeps its normal tank work (the task is left as with no pre-place) until the puller's walk left to its stand is no longer than the tank's own walk to its point plus 4 yd (both walk at the same speed, so the tank reaches its point about when the shot lands; walk 57's median tank stood 31 yd from its point, so it walks with the puller still about 35 yd out), at the latest when the puller is within 10 yd of its stand, or when the puller is in position (it stood at its stand, reached shot position, or the shot landed); then it walks to its point as before. A stand not known (the puller's task not the pre-place's) waits for the puller to be in position. Once begun the walk is not undone. The point, the stand and the pre-place's generation come from one locked read, and the move is published only after the same generation is confirmed again under the lock, so a pre-place replaced in between never sends the tank to the old point.

Limits (documented, not fixed): the late walk keeps tanks[2] on Hakkar longer, so it is Hakkar's victim more often when the pre-place starts or runs; walk 58 measures the `preplace_end` / meet `reason=receiver_victim` count against walk 57's 13.

Diagnostics (always on with `DungeonRunSeekMeetPrePlace`, key or not): a `BossTacticsSonMeet` row `event=preplace_arrive,receiver=<name>,after_ms=<ms>,point_yd=<yd>` once per pre-place when the tank first comes within 4 yd of its point (`after_ms` since its walk began), and the shot's `event=meet` row gains `receiver_point_yd=` (the receiver's distance to its pre-place point; `-1.0` when the receiver is not the pre-placed tank). Walk 57 could not tell a tank pulled back by its melee reach from one still walking.

</details>

### `AiPlayerbot.DungeonRunSeekShotWaitsReceiver`
Whether the Hakkar walk-out puller waits with its shot until the pre-placed receiver stands at its point. Recommended on.
**Default:** `0` · **Values:** `0` off (the shot goes as soon as the puller is in shot position, as before), `1` on (needs `DungeonRunSeekMeetPrePlace`) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Why:** in walk 58 (late walk on) the pre-placed third tank stood a median 19.1 yd from its point when the shot landed, within 4 yd in only 3 of 26 shots, and the Son ran a median 43 yd to its puller before a tank met it.

**The wait:** with the key on and a live pre-place for this walk-out, the puller, once in shot range, sight and on the stand's floor, stops and holds its shot until the pre-placed tank is within 5 yd of its pre-place point. The 5 yd line is its own constant: the pre-place's hold ring, where the tank stops walking and holds (and the `preplace_arrive` row), stays 4 yd, so a tank 4-5 yd out is still walking in when the shot goes. It shoots anyway after 6 s of waiting, as soon as the next Blood Siphon is 15 000 ms or less away (compared in milliseconds; the cycle's dose comes first; an unknown siphon time never triggers it), or once the walk-out task is 19 s old, so the wait never eats the last 6 s before the task's 25 s timeout. With no pre-place this cycle (`DungeonRunSeekMeetPrePlace` off, no third tank fit to pre-place, or the pre-place skipped) there is no wait. A pre-place that ends during the wait (the tank dies, is charmed, becomes Hakkar's victim) lets the shot go at once. The wait happens once per walk-out: a shot refused after it does not wait again.

**With the late walk** (`DungeonRunSeekMeetPrePlaceLate`): a waiting puller counts as in position, so the late tank's walk is due on the first tick of the wait; the two never wait for each other, and the 6 s cap bounds the wait in any case.

**The no-shot stall:** the 6 s no-shot abort is held from the wait's start until the wait's own end (the puller's tick that lets the shot go), however slow that tick is, and its clock restarts when the wait ends, so it measures the shot's own time. A puller that stops ticking mid-wait is ended by the task's 25 s timeout.

Diagnostics (always on with the key): one `BossTacticsSonMeet` row per walk-out when the wait ends, `son=<g>,event=shot_wait,receiver=<name>,receiver_point_yd=<yd>,waited_ms=<ms>,reason=arrived|timeout|siphon|late|ended` (`late`: the task's 19 s age clause).

Limits (documented, not fixed): the wait adds up to 6 s to the cycle, so the Son lands up to 6 s closer to the siphon.

</details>

### `AiPlayerbot.DungeonRunSeekMeetPrePlaceHold`
Whether the pre-placed receiver stops chasing the anchored boss while it walks to and holds its pre-place point. Recommended on with `DungeonRunSeekMeetPrePlace`.
**Default:** `0` · **Values:** `0` off (the tank's rotation chases the boss as before), `1` on (needs `DungeonRunSeekMeetPrePlace`) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Why:** in walk 59 the pre-placed third tank started its walk on time, but its own Protection rotation kept chasing Hakkar back into melee: `enemy out of melee` (charge, then `reach melee`, at 37), the gap-closer Intercept (42) and the melee casts' `reach melee` prerequisites all outrank the pre-place walk (`boss tactics seek tank`, 26). Only the anchor's chase gate held them, and only while Hakkar stood more than 30 yd from his spawn: inside that line 2 of 18 receivers reached their point.

**The hold:** with the key on, while a live pre-place names this bot and its walk has begun, its reach actions (`reach melee`, charge's reach alternative and the reach spells, also as prerequisites or alternatives) and its reach casts (Charge, Feral Charge and any other cast that moves the caster to its target) get nothing on the boss of its live anchor row, and its warrior gap closer skips that boss (no Intercept, no Charge, and no Berserker or Battle Stance swap for one). The walk then wins the tick and the pre-place's hold keeps the tank on its point. No boss is named in code: the boss is the live anchor row's.

**Untouched:** chases and reaches of a Son, the take's own reach (through `AttackAction`), flee, avoid-cast, freeze, seek, taunts (they do not move), and every bot that is not the pre-placed receiver. Before the walk is due (`DungeonRunSeekMeetPrePlaceLate`) nothing is held. The hold ends with the pre-place, not with the walk-out task: it carries on past the puller's walk back while the meet is open, and ends when its meet closes, the task ends without a meet, or the tank dies, is charmed or becomes Hakkar's victim.

Diagnostics (always on with the key): one `BossTacticsSonMeet` row per pre-place, the first time the hold applies, `son=<g>,event=preplace_hold,receiver=<name>,boss=<entry>,action=reach|charge|gapclose,point_yd=<yd>` (`charge`: a reach cast such as Charge or Feral Charge).

Limits (documented, not fixed): the third tank builds no threat on the boss for longer (from the walk's start to the meet's close); a warrior receiver's Intervene onto an ally (a friendly target, so not the boss) is not held and can still move it toward Hakkar.

</details>

### `AiPlayerbot.DungeonRunTankSwap`
Whether the boss tactics keep their `swap_on_aura` rows: the tanks of the row's rotation trade the boss at a stack count, and the tank that was swapped off stops attacking it for a while.
**Default:** `0` · **Values:** `0` off (the rows are ignored, as before the kind existed), `1` on (needs `AiPlayerbot.DungeonRunAuraTactics` and `AiPlayerbot.DungeonTactics`, or a commander-bound group) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Swap:** a row names the aura (`spell_id`) and the stack count (`param`), and its `priority` is the size of the tank rotation (the raid's tank order, main tank first, at least two tanks). While the boss is on a rotation tank carrying at least that many stacks, the next rotation tank after it that is alive, knows a taunt it can cast on the boss and carries fewer than half the stacks, taunts the boss off it (Taunt, Growl or Hand of Reckoning, the first it can cast; Taunt and Growl are melee-range, Hand of Reckoning is Turtle's ranged paladin taunt, Righteous Defense is TBC and no 1.12 spell, and a tank with no taunt is skipped). The fight keeps the swap in one shared record per instance, boss and aura, so every bot reads the same history. The tank the swap moved off stands down while the boss is on the tank the swap moved it to, for 20 s or until its own stacks are gone, whichever comes first: it stops attacking the boss and every action aimed at that boss is held — any class's threat builder, melee swing or cast, whatever its name (Sunder Armor, Maul, a paladin's attacks) — as is area damage, which reaches the boss from any pick (Consecration, a cleave), and no taunt of any class goes out (Taunt, Growl, Mocking Blow, Challenging Roar/Shout, the paladin taunts), so a druid or paladin tank cannot keep building threat on the boss and take it back. The stand-down ends at once when the boss is no longer on the tank the swap moved it to (the taunt was resisted, the new tank died or lost the boss), so the old tank's rescue taunt is free, and an explicit attack order from a player on the boss clears it for that bot. Actions aimed elsewhere still run: it may fight adds, and its self-buffs, heals and defensive cooldowns keep working. Each swap writes a `BossTacticsTankSwap` row to `bot_events.csv` (`taunt=<boss guid>, stacks=<n>, from=<guid>`). First row: Kurinnaxx's Mortal Wound (25646) at 5 stacks between the main tank and off-tank 1.

</details>

### `AiPlayerbot.DungeonRunShadeHealerCover`
Whether the Delusions carriers at Jin'do cover the healers a Shade attacks: a cursed off-tank that does not hold the boss takes a Shade on a healer, and a cursed DPS carrier ranks a Shade on a healer ahead of the other Shades. Recommended on.
**Default:** `0` · **Values:** `0` off (today's carrier pick and strike), `1` on (the off-tank half needs `AiPlayerbot.DungeonRunCarrierStrike`) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

A Shade of Jin'do copies the boss's threat list, where heal threat is top, so it walks to a healer; in walk 56 Shades were the first death in 5 of 11 wipes, 50 of 59 healers a Shade killed were not cursed (they cannot hurt it), and in 56 % of the frames with a live Shade no cursed bot attacked it. With the key on the carrier pick (`PickCarrierOverride`, used by a DPS under the run's own order, the carrier strike and the run's focus skip) ranks a Shade whose victim is a healer of the group, that stands within 30 yd of the carrier and on its floor (no more than 6 yd above or below, so nobody is sent into the troll pit), ahead of any other Shade, the nearer one first; past 30 yd or on another floor the nearest Shade wins as before. A free DPS already ranks a Shade on a healer first (`PickKillFirst`). On a fight with a `threat_successor` row the carrier strike keeps every off-tank on the boss; with the key on a cursed off-tank that does not hold the boss is let through when its pick is a Shade on a healer within 30 yd of it and on its floor and its own threat on the boss is above 0 and at least half the highest tank's (below that line, after a Hex wiped it, it stays on the boss as the successor reference), and keeps that Shade as its pick while the Shade's victim is the off-tank itself (for up to 15 s after the Shade was last on a healer, or until it dies or the curse ends). The lowest-member line (`AiPlayerbot.LowHealth`) leaves out only the healer the Shade is eating: the off-tank's own health and everyone else's still hold it back, and so does the main-tank line (60 %). Pair it with `AiPlayerbot.DungeonRunThreatSuccessorKeepsAway`, which keeps a cursed tank on the successor list by the same half-share line, so an off-tank on its Shade stays the successor; without it the off-tank is no successor while it is on the Shade. An off-tank the boss is hitting stays, the main tank never leaves, a cursed healer keeps all of its own guards, and an uncursed bot never picks a Shade. The `BossTacticsShadeCover` row ([Boss Tactics](../../play/boss-tactics.md#shade-cover-row)) names who still leaves such a Shade alone.

</details>

### `AiPlayerbot.DungeonRunCarrierHold`
Whether a Delusions carrier standing on its Shade of Jin'do keeps it: the "not dps target active" trigger stays quiet for it, so "dps assist" no longer pulls it to the boss, and the kill-first trigger checks every second.
**Default:** `0` · **Values:** `0` off (today's rules), `1` on (needs `AiPlayerbot.DungeonRunAuraTactics` for the needs_aura rows) · **Restart:** not needed (`.reload config` applies it on the next world tick)

### `AiPlayerbot.DungeonRunThreatSuccessorNotHolder`
Whether, on a boss with a `threat_successor` row (Hakkar, Jin'do), the damage dealers are capped against a tank the boss is **not** hitting. Recommended on.
**Default:** `0` · **Values:** `0` off (the reference is the highest off-tank that hits the boss, even when the boss is hitting it), `1` on (the reference is the real threat of the highest main tank or off-tank that is hitting the boss and is not the one it is hitting; with no such tank, the old pick stands exactly) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

With the off-tank holding Jin'do the old reference was the holder's own threat, so the damage dealers were allowed up to 80 % of it while the main tank (whose threat a Hex wiped) sat below them; after a Hex on the holder a damage dealer inherited Jin'do 39 times out of 40 in walk 55. With the key on the cap stands at the other tank's threat, so the boss falls to a tank. Only a tank that is hitting the boss counts: a Hakkar off-tank on a Son, a Jin'do tank held on its Shade or down in the pit is no successor, while a tank wearing Jin'do's Hex counts as on the boss (it cannot hit while polymorphed), and when none qualifies (or only the holder is on the boss) the old rules run unchanged, including the half-share rule for away off-tanks. The key changes only bosses with a `threat_successor` row, and only while a non-holding tank is on the boss.

The price: after a Hex on **either** tank the wiped tank has about 0 threat and becomes the reference, and the damage dealers' single-target attacks are held (the 80 % line) until that tank has rebuilt past them. That takes as long as the fight's threat needs, tens of seconds is possible, and a second Hex on the new holder before then still hands the boss to a damage dealer; the damage dealers keep working on adds (Shades, pit trolls, totems) meanwhile. Rogue Vanish and priest Fade on threat read a 0 reference as "the tank holds nothing" and stay quiet, and hunter pets cower (`ShouldPetCower`) against it too, so pets are held during the stall as well. Each `BossTacticsThreatCap` row carries `holder=<main|off|none>` (the tank the boss is hitting) so the next walk can measure the stall.

</details>

### `AiPlayerbot.DungeonRunCureIgnoresThreat`
Whether party cures and dispels skip the damage threat back-off, the way heals already do. Recommended on.
**Default:** `0` · **Values:** `0` off (a cure is vetoed at the 80 % high-threat line like any single-target spell), `1` on (every cure on a party member runs whatever the healer's threat) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

A cure on a party member (`dispel magic`, `cure poison`, `cure disease`, `abolish poison`, `abolish disease`, `remove curse`, `remove lesser curse`, `cleanse` and `purify` on a party member: every `CurePartyMemberAction`) is a single-target spell action, and the threat back-off (`ThreatMultiplier`) vetoed it whenever the healer's own threat on its current target read at or above the 80 % line, or 100 % on a mob no tank has touched. Heals were exempt already; cures were not. At Jin'do that left the Hex (17172) on the tank for its full 5 s in 50 of 91 tank Hexes in walk 56, while a dispeller had been picked within a second: the failing frames read `dispel magic on party` with result `I` (vetoed) in 10 of 12. With the key on the cure is exempt like a heal: a cure pulls no mob off the tank. It covers every boss and every party cure; self cures and offensive dispels on an enemy keep the back-off. A cure can still fail for range or line of sight (that also reads `I`). Walk-57 measures: tank Hexes cured within 1.5 s from 29 of 91 (32 %) to at least 70 %; Jin'do-on-DPS seconds after tank Hexes from 252 s over 30 fights to at most 120 s; the `I` share of `dispel magic on party` after a Hex from 10 of 12 to at most 20 %.

</details>

### `AiPlayerbot.DungeonRunThreatSuccessorStrict`
Whether the 80 % line on a boss whose `threat_successor` row carries the mode word `strict` holds: no projection escape, and white damage stops near the line. The shipped data sets `strict` on Jin'do's row only; Hakkar's row stays plain. Recommended on, together with `AiPlayerbot.DungeonRunThreatSuccessorNotHolder`.
**Default:** `0` · **Values:** `0` off (today's escape, white damage never stopped), `1` on (on a boss whose `threat_successor` row carries `strict` only: no projection escape; the melee swing and the auto-repeat stop at 92 % of the reference) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

The high-threat back-off vetoes a damage dealer's single-target attacks at 80 % of the reference tank's threat, but `HighThreatTrigger` lets it through while its threat, rising at the last 5 s rate, projects under the tank's in 5 s. On a successor boss the reference is the tank the boss is not hitting, which builds threat slowly, so the damage dealers rode just under it: in walk 56 every Hex on a tank that went to a tank found the top damage dealer at 0.89-0.99 of the successor, and every one that went to a damage dealer at 1.00 or more. With the key on there is no escape on such a boss (`HighThreatProjectionEscapes`).

Second, white damage. A running melee swing and a running auto-repeat (a wand's Shoot, a hunter's Auto Shot) keep going without passing the veto again, and the melee and wand actions carry no threat type at all, so the veto never bound them. With the key on, the `successor white cap` trigger stops the swing and interrupts the auto-repeat itself at 92 % of the reference, in the trigger pass at the start of the decision cycle (`SuccessorWhiteStopNow`): a queued stop action could lose that cycle to any higher action, a heal or a move, while the swing went on. Only white damage on the successor itself is stopped: a swing or shot that an action moved onto a Shade, an add or a totem is left alone (`SuccessorWhiteOnTarget`), and each mode is judged on its own target (`SuccessorWhiteStopsOf`): the swing stops only when its victim is the successor, the auto-repeat only when its target is. The trigger is checked on minimal (throttled) ticks too: its node carries a never-queued no-op handler at relevance 100, the engine's line for minimal ticks. While the per-bot latch (`SuccessorWhiteLatch`) holds, the trigger stops them again on every pass and the threat multiplier keeps the white-damage actions from starting. Melee and wand start again under 85 %. A hunter's Auto Shot is a single-target action that the 80 % veto still gates, so it resumes under 80 %. Tanks are never stopped, and a target without a `strict` successor row clears the latch. While the reference is 0 (a tank Hex just wiped the successor, which reads 100 % for every damage dealer) the white damage is never stopped and the latch clears; the specials stay held by the 0-reference veto as before. DoT ticks already on the boss cannot be stopped; only their re-cast is vetoed at 80 %. Known limit: another attack action (a reach, a target switch, a strategy's own attack) can start the swing again during a hold; the trigger stops it again on its next pass, so a few swings can land in between. Each `BossTacticsThreatCap` row (one per damage dealer per 10 s) gains `white=held|free ref0=0|1`, so a walk can split the Hex-window hold from the 92 % stop.

The row opt-in (`tactic_mode = 'strict'` on a `threat_successor` row) ships in `data/sql/world/20261108220000_world.sql`, on Jin'do's row only. **Rollback:** a binary from before the `strict` word refuses that row entirely and so loses Jin'do's whole successor cap, not only the strict line. Before starting such a binary on a migrated database, run `tools/sql/rollback/20261108220000_world.rollback.sql` by hand (it lives outside `data/sql`, so the updater never applies it): it sets the row's `tactic_mode` back to empty and restores its comment. Moving forward again, re-run the migration's UPDATE by hand, since the updater has already recorded it.

A 65 % white stop was removed in 405801e86 because it cost a fifth of the raid's damage; this one aims at damage dealers between 80 % and 92 % of the successor instead of 89-100 % and above. Walk-57 measures: top damage dealer / successor at a tank Hex, median, from about 1.05 to 0.90 or less; Hexes on a tank holder that go to a damage dealer from 66 of 91 to 35 % or less (off-tank holder: 76 % to 35 % or less); pure-overtake time with Jin'do on a damage dealer from 227 s to 80 s or less; the guard: raid damage per second on Jin'do at 92 % of walk 56 or more.

</details>

### `AiPlayerbot.DungeonRunThreatSuccessorKeepsAway`
Whether the successor pick on a boss with a `threat_successor` row keeps a tank cursed by Delusions of Jin'do, which stands on its Shade by design. Recommended on, together with `AiPlayerbot.DungeonRunThreatSuccessorNotHolder`.
**Default:** `0` · **Values:** `0` off (a cursed tank that is not hitting the boss drops out of the pick), `1` on (a tank wearing Delusions 24306 counts as on the boss while its threat is above 0 and at least half the highest tank's) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

With `AiPlayerbot.DungeonRunThreatSuccessorNotHolder` the reference is the highest tank that hits the boss and is not the one it is hitting. In walk 56 that pick lost the main tank in 12 of the 39 off-tank Hexes that went to a damage dealer (against 1 of 10 that went to a tank), so the reference fell back to the holder's own threat and the damage dealers climbed to 80 % of it. One cause was a main tank cursed by Delusions (24306) on its Shade: it stands there by design while its Jin'do threat stays. With the key on it counts as on the boss, as a Hexed tank already does (`ThreatCapCursedOnBoss`), but only while its threat is above 0 and at least half the highest tank's: Delusions is never cured and a cursed tank's Jin'do threat does not climb on its Shade, so a cursed tank with little or no threat would otherwise be a near-0 reference that holds every damage dealer for the whole curse. A charmed tank (Brain Wash, Cause Insanity) is still skipped: its old threat would lift the cap above anything the boss can turn to while it is off the threat list (walk 41 on Hakkar). The key acts through the not-holder pick; with that key off it changes practically nothing, since a cursed off-tank at half the highest tank or more already passes the old away floor. A main tank that stays idle after a charm (the `select new target` and `reach spell` loops of walk 56) is a tank-targeting question this key does not touch. Known limit: a cursed tank at half the holder's threat or more stays the reference while its Jin'do threat stands still on its Shade, so the damage dealers are held at 80 % of it (and, with `AiPlayerbot.DungeonRunThreatSuccessorStrict`, their white damage at 92 %) until the holder has doubled it and the cursed tank drops below half; that can take tens of seconds. It is the not-holder design working (the cursed tank really is the next victim after a Hex on the holder). Walk 57 splits those rows out of the damage guard: `BossTacticsThreatCap` rows with `ref=offtank holder=main offtank_target=Shade of Jin'do white=held`. Walk-57 measures: pre-Hex `ref=offtank holder=off` rows in off-holder windows from 132 of 661 (20 %) toward 5 %; off-holder Hexes with `mt_threat` 0 from 10 of 47 toward 3 (the charmed and idle main tanks are not covered, so these targets may not be met by this key alone).

</details>

### `AiPlayerbot.DungeonRunFactJudgeCharmedLead`
Whether the run leader judges its fight by an uncharmed bot while the lead bot is charmed, so a Brain Wash Totem stays a kill-first fact, the fight stays read and the guard keeps its eyes while the main tank is Brain-Washed. Recommended on.
**Default:** `0` · **Values:** `0` off (the charmed lead judges as before), `1` on (while the lead is charmed, the first living, uncharmed commanded bot on its map judges) · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

The run reads its fight against the lead bot. A charmed lead (Jin'do's Brain Wash, any Mind Control or Dominate Mind) is friendly to its charmer's side, so the kill-first read dropped the Brain Wash Totem's owner Jin'do as not engaged, the engaged units came back empty (the fight read as stale and its clock stopped), the raid guard measured and filtered its attackers from the charmed lead, and the run's own focus order was refused as "friendly"; the totem focus (`AiPlayerbot.DungeonRunTotemFocus`) went silent while the main tank stayed charmed (ZG walk 57: 16 of 19 main-tank charm windows of 10 s or more had no focus row; median charm 86 s, meleeing the raid). With the key on, while the lead is charmed and a fit judge exists (`RunJudge`: the first living, uncharmed commanded bot on the lead's map, the judge `GroupCanAttack` already used; real players are never the judge), it replaces the lead (`RunFactJudgeReplacesLead`) in the kill-first friendliness test and the owner's engagement, the engaged units read, the crowd-control hold read, the raid guard's anchor (`RaidGuardAnchor`: the off-tank or first living bot the dead-lead path already picks, skipping charmed bots), and the order's target check (`CommanderOrder::judge`, which also serves `cmd attack`, `cmd cc` and `cmd state`). With the key on a dead lead's order judge and guard anchor also skip charmed bots. With the whole party charmed, or the lead not charmed, nothing changes. Generic: any charm on the lead, every raid and dungeon, no boss id. The per-member engaged read (`ReadMemberEngaged`, the combat drop) and the party's attacker list for a party run still judge against the lead.

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

**Walk back rescue:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- with `1`, a stuck walk back is not paused: the lead bot is teleported to the last point it reached on the plan
- when a live pack is near that point: to the latest earlier point out of every live pack's reach (radius plus 10 yd)
- never back before the start step or over a direct step; with no such point the run pauses
- a point held by live hostiles is never used alone: the lead bot joins the biggest group of the party out of combat instead
- that group must be on the part of the route the run has already walked (a reached row within 60 yd); a group before the start step or only near rows not yet walked is not joined
- between the start step and the target point, earlier clear points are tried first; joining the party is the last option, and it never turns into a pause
- a placed run's lead bot behind the start step is teleported to a safe point on the way it came
- out of combat only, once a step; left-behind party is summoned only out of combat

</details>

### `AiPlayerbot.DungeonRunDryWalk`
Whether the run leader keeps its own route picks off deep water instead of swimming through it.
**Default:** `0` · **Values:** `0` the core's path, water included, `1` a ground-only way where one reaches the point · **Restart:** not needed (`.reload config` applies it on the next world tick)

<details>
<summary>How it works in detail</summary>

**Dry walk:** only read while `AiPlayerbot.DungeonSelfClear = 1`:
- the core's own player path may cross deep water on its surface, so an approach hop or a firing stand picked on it can land in the water; the core's straight swim chord can then hold the lead bot against a bank
- with `1`, the run leader asks the navmesh for a ground-only path (deep water, magma and slime left out; wading kept) and picks its approach hops and its firing stand there
- the walk-back shortcut is not re-pathed: its straight swim chord is refused as no navmesh route, so the walk back replays the plan's points; the leg-on-path read and the too-far legs are unchanged
- when the walk's own way would swim after the lead bot leaves its own water, it goes in dry legs first: 8-40 yd along the dry path, at most 24 a walk, each at a point with no deep water at it and with a dry core path of its own
- the point itself must be dry and the dry way at most 3 times the way the run would walk; otherwise the old behaviour runs unchanged, so intended swims still work
- a point in deep water keeps the core's swim chord, also for a walk-back point: a straggler standing in deep water is walked to as before, and a firing stand for a pack in the water may stand in it
- a straggler whose own point is dry but whose way would swim has no path that ends at the point: its pull moves to the next tank and can pause the run
- a lead bot more than 25 yd from any ground polygon gets no dry path; followers still use their own paths

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
- never onto a lead bot that stands off the floor (more than 3 yards of air under it, read with the teleport landing's own floor: the map's floor and the feet probe, the higher one winning): the bot is brought to the nearest member out of combat instead, or holds; a lead bot on a game-object platform or elevator (a transport under it, or a surface within 2 yards under its feet that the core's height read finds) counts as on the floor; a lead bot off the floor for 30 seconds pauses the run at whichever summon was refused, and names both heights

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
