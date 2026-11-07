---
id: play-dungeon-run-leader
title: Dungeon Run Leader
category: play
summary: The run leader (.bot run, AiPlayerbot.DungeonSelfClear, on by default, work in progress) that lets a bound party clear a stored dungeon plan by itself, its safety rules, supported steps, hot reload and player-led runs, and the plan tables it reads.
tags: [guide, dungeon, plan, commander, self-clear, uldaman, events]
relates_to:
  - play-boss-tactics
  - play-dungeons-and-raids
  - play-commands
  - server-configuration
  - developers-architecture
---

# Dungeon Run Leader

> Work in progress: this page describes an unfinished feature that is still changing.

`.bot run` lets a bound party clear a stored dungeon plan by itself.
It needs `AiPlayerbot.DungeonSelfClear = 1` (default `1`, on) and a binding (`.bot commander bind` for a bot-led party, `inparty` for your own party, or the server's own system binding).
It sends at most one order per second, with the same checks as the commander's own `.bot cmd` orders.

### Reloading the plans (`.bot run reloadplans`)

Only a GameMaster can reload the plans (in game, on the console, over remote access, or over SOAP), and it works with the run leader off. The gate is account security `SEC_GAMEMASTER`, which is `SEC_DEVELOPER`, which is **3** on this core (player 0, observer 1, moderator 2, developer 3, administrator 4, console 6).
It re-reads `ai_dungeon_plan`, `ai_dungeon_leg`, `ai_dungeon_step`, `ai_dungeon_boss`, and `ai_boss_tactic`, and swaps the new set in only when every row passes.
A refused row, a missing plan, a failed query, or a table that changed while it was read keeps the old set.
A run in progress keeps the plan it started with.

## Tables

A plan is the route of one dungeon written as data: where to walk, which packs to pull, which bosses to kill, which objects to open, and what to wait for.
Plans are read at startup into a cache, one pass per table, and nothing reads them per tick.

### `ai_dungeon_plan`

| Column | Meaning |
| :--- | :--- |
| `plan` | Lower-case plan name, primary key. |
| `map` | Instance map id. |
| `cc_priority` | Comma entry list, `''` for none. |
| `cc_always` | Comma entry list, `''` for none. |
| `kill_first` | Comma entry list, `''` for none. |
| `cc_never` | Comma entry list, `''` for none: entries the automatic crowd control under `AiPlayerbot.DungeonRunAutoCc` never marks (trivial non-elite units need no list, their rank excludes them, unless the plan itself lists them in `cc_priority`/`cc_always`). |
| `source` | Where the rows came from, for the reader. |
| `comment` | Free text. |

Each of the four lists is read on its own: a list with more than 16 entries keeps its first 16, any other bad list is dropped by itself, and the rest are kept. The server log names every cut or dropped list.

### `ai_dungeon_leg`

| Column | Meaning |
| :--- | :--- |
| `plan`, `leg` | Plan and leg number, primary key. |
| `name` | Leg name shown in the status line. |
| `comment` | Free text. |

### `ai_dungeon_step`

One row is one thing to do, run in `(leg, seq)` order with primary key `(plan, leg, seq)`, and `seq` need not be dense.

| Column | Meaning |
| :--- | :--- |
| `kind` | What to do (see Step kinds). |
| `entry` | Creature, game object, or item entry, by kind. |
| `x`, `y`, `z` | Point on the plan's map, by kind. |
| `radius` | Yards, by kind, at most 100. |
| `item` | Item entry, by kind, `0` for none. |
| `param` | By kind: `wait_object` state to wait for, `ritual` bot count (`0` = 3), `gossip` menu option (1-15), `hold` done value. |
| `flags` | `1` ambush, `2` Alliance, `4` Horde; `64` exact waypoint on `move`; `256` gone-is-done on `loot`, `open`, and `wait_object`; `512` dead-is-done on `gossip`; `8` data, `16` object state, `32` encounter state, `16384` reset-to-gossip, and `32768` dead-gossip-ok on `hold`; `2048` living-creature proof on `open_proof`; `1024` on `pull` never defers unreachable but pauses instead; `4096` on `boss` ends skipped not paused when out of reach; `8192` pulls from the prior move stand. |
| `timeout_s` | `0` means 60, at most 600 s, and hold steps up to 1800 s. |
| `entries` | Comma creature entry list, at most 16, by kind. |
| `comment` | Free text. |

Defaults by kind: pull radius `0` means 15, boss radius `0` means 30, hold radius `0` means 30, and `ritual` `param` `0` means 3 bots.
`wait_object` states are `0` ready/closed, `1` active/open, and `2` active_alt.
`move_hop_exact` is its own kind for a direct hop after an exact move.
`open_proof` needs a proof radius over 0 and at most 20.
`use_on` reads no flag or param.

The loader skips rows it cannot use and names them in the server log: an unknown kind, a `pull` without entries, a `boss`, `loot`, `open`, `use_item`, `wait_object`, `ritual`, `gossip`, or `use_on` without `entry`, a `gossip` with `param` outside 1-15 or any flag but 512, a `use_on` with any flag or param, a `use_on` without `item` or without a point, a negative or over-100 radius, a `wait_clear` without a radius, a timeout over the cap, a `wait_object` state above 2, a stand pull with `param` over 60, a pull with `item` over 80, an `open_proof` with proof equal to entry, with `param` or timeout set, or with 2048 misused, or the exact flag on a non-`move` step.

### Step kinds

| Kind | Columns used | What it means |
| :--- | :--- | :--- |
| `move` | Point, exact flag 64, entry | Walks the navmesh to the point, `entry` fires that area trigger once, and with flag 64 the lead bot must stand on the point (1.5 yd, still 2 s) before the next step. |
| `move_direct` | Point, entry | Walks a straight line over a navmesh gap, `entry` fires that area trigger once. |
| `move_hop_exact` | Point | Direct hop launched from the exact point before it, only on proof. |
| `pull` | Centre, radius, entries, param, item | Every living unit of `entries` within `radius` of the centre, with flag 1 for a stealthed pack, flag 8192 pulls from the prior move stand with `param` as patrol-clear yards and `item` as body-count ceiling. |
| `boss` | Entries first, then boss entry | Kills `entries` in order, then the boss entry, with flag 4096 ending skipped not paused when out of reach. |
| `loot` | Entry, near point, item | Loots and the item must be in the bags after, with flag 256 counting gone as done. |
| `open` | Entry, near point, item, param | Uses the object with the item its lock needs, `param` is a second item in the same bags, with flag 256 counting gone as done. |
| `open_proof` | Object plus separate proof entry and radius | Uses the object and finishes when the proof stands near the point, with flag 2048 for a living-creature proof. |
| `use_item` | Entry, item, param | Uses the item from the bags and the made item must appear, `param` is a second item in the same bags. |
| `use_on` | Object, near point (required), item | The carrier's item is used ON the object. |
| `wait_object` | Entry, near point, state, timeout | Waits until the object reads the wanted state, with flag 256 counting gone as done. |
| `wait_clear` | Point, radius, timeout | Waits until no living hostile stands in the radius, and past the timeout bystanders on another floor finish it with a note. |
| `rest` | None | Rests even when nobody is low. |
| `ritual` | Entry, near point, bot count | Several bots use the object together, and its answer starts the event or pauses. |
| `hold` | Spot, radius, units, done condition, timeout | Stands on the spot and fights what comes until the done condition reads true (see Events). |
| `gossip` | NPC entry, near point, option 1-15 | Talks to the NPC and picks one menu option, searched within 40 yd after the lead bot walks within 30 yd first. |

## Joining a bot LFG group

While a bot LFG group waits for its fifth member, its leader posts an invite line in your faction's LookingForGroup channel about every 3 min (`kPostIntervalMs` 180 s).
Whisper the leader `inv` to join.
You get the invite when you share its faction, sit inside its level band, and your class and talents fit the open role (damage fits everything, while tank or healer needs the class plus leaning talents unless you spent fewer than 10 talent points).
Otherwise the leader answers once with one refusal line: group full, someone else joining, leave your group, wrong level band, wrong role, hardcore refused, or invite fell through.
One player is invited at a time, the invite waits 60 s (`kInviteHoldMs`), and once the slot is taken the posts stop.
- `AiPlayerbot.BotLfgMaxGroups` caps bot LFG groups.

## Run leader

The run leads the bound party and nobody else's, and the commands and status line are under [Run leader](commands/commander.md#run-leader-bot-run).

### Starting partway, stopping early, and time limit

- `from leg|step` starts the run at the first step of a leg or at a step number instead of the start.
- `until boss|step|leg` ends the run before that boss, step, or leg instead of running to the end.
- `AiPlayerbot.DungeonRunMaxMinutes` (default 180, `0` for none) ends a run that has lasted that long with a note, and paused time counts.

- `.bot run stop [reason]` keeps the reason in the end note.

### Test speed

`start ... speed=<1.0-3.0>` is a GameMaster bot-only test speed: it walks faster between fights and drops back to 1.0 in a fight, pull, rest, event, or near a pack.

### Console and LFG control

- `.bot run for <Commander> <verb>`: a GameMaster (in game, console, remote, or SOAP) drives another commander's run (`start`, `pause`, `resume`, `skip`, `stop`, `status`) as that commander, with that commander's binding; the answers go to the caller. A start never carries the player-led word. See [Run leader](commands/commander.md#run-leader-bot-run).
- A GameMaster `run system` drives its own bot-only binding, which needs `DungeonSelfClear` plus bot LFG groups: `system bind [<Bot>...] | cmd <order> | start | pause | resume | skip | stop | status | unbind`.
- A GameMaster `run lfg` lists, forms, fills, and disbands bot LFG groups.
- `.bot run here` names the nearest plan on your map.
- `.bot run mode` shows bot-led vs player-led.

### How it plays

Once a second the run reads the party and the current step's surroundings and answers with at most one order, sent exactly as the commander's own `.bot cmd` order.
Fights use the bots' own combat AI plus the bound party's boss tactics (see [boss tactics](boss-tactics.md)).

| Step | What the run does |
| :--- | :--- |
| Walks | Approach stands ring 15-25 yd, defer-off-floor packs, split too-far in 120 yd legs, walk back stops at the last cleared fight; island hops off twice per step, stranded rescue teleports once per step, 3-miss rewind; a direct step needs its own ground-and-height gather test; movement counts as walk progress up to 120 s. |
| `move`, `move_direct` | Done within 3 yd, or within 6 yd after 3 s stalled (12 s for `move_direct`), where 6 yd is measured on the ground plane when within 4.5 yd of height and in 3D beyond that. |
| `pull` | Walks in hops toward the centre until within 60 yd, gathers the party within 20 yd (30 s), then sends pullback, where a pull with no fight in 25 s is retried once and then that unit is passed, and a unit surviving 3 pulled fights is left as out of reach. |
| `boss` | Runs like a pull in kill order, pulls a neutral boss and leaves only a friendly one alone, needs the boss's death in sight (a dead unit in the step radius plus 60 yd, or within 60 yd of the lead bot once seen alive), and waits `timeout_s` for an absent boss before pausing. |
| Team flags | Skips an other-team pull unless the party already stands within 60 yd and a unit of it is not friendly, and always walks to an other-team boss and skips it only when every unit there is friendly. |
| `loot` | Counts the step done when any party bot already carries the item, else sends `cmd loot` and pauses when the item is still missing after, where a loot-rule refusal pauses except with `AiPlayerbot.DungeonRunLootFfa = 1` (default `0`), which allows one free-for-all detour per step. |
| `open` | Pauses when the carrier lacks the key item, else sends the open order and waits for its task. |
| `use_item`, `use_on` | Sends the matching commander order once a carrier with the items is found, where a `use_on` ACK alone does not end the step because the object must change state or the boss must show within 30 s. |
| `wait_object`, `wait_clear` | Waits as the Step kinds table says, bounded by `timeout_s`. |
| `rest`, `ritual` | Rests through the rest gate, and a ritual answer starts the event while an error or no answer pauses. |
| `hold` | See Events. |

### Exact waypoints

Plain walk arrival only starts the tighten, then the lead bot must be within 1.5 yd of the point and still for 2 s.
The tighten uses at most 3 moves onto the point, pauses when no proof comes within 36 s, sends the hop only on proof, and a fight or a pause clears the proof.

### Safety rules

The run protects itself with these rules:

- It gathers the party before a pull and pulls only what is near.
- It guards from the stand, attacking mobs that hit the party.
- A unit in its respawn nest (it comes back within a minute and stands within 22 yd of its spawn point) that hits nobody in the party does not hold a fight open: a bot whose only enemies are such units is not in a fight, the regroup starts and the step goes on without it. A nest unit that attacks a party member is always a real fight and a guard target, and a unit the plan's own boss, hold or pull step names is never a nest. `AiPlayerbot.DungeonRunRespawnNest` (default `0`, off) switches it.
- A gather never stands within 30 yd of a nest: the party gathers at the current step's point instead of on a dead tank's body at the nest's rim.
- It never pulls what a later step owns.
- It ends a pull or boss step past the 180 s fight ceiling once the game confirms that step's own units are dead or gone from the map (a unit that merely dropped out of the run's reads is not dead) and lets the walk go on, even while unrelated mobs (respawning trash, a patrol that walked in) still fight: those are fought by the bots' ordinary combat and do not hold the step. A boss step ends as soon as the boss's corpse is confirmed, whatever keeps the party in the combat afterwards. Younger fights, a combat flag with nothing engaged, and a row with its own units left keep the rules as before.
- It judges no unseen pack or boss, only what the lead bot sees within 60 yd.
- A healer on stay is held 9 yd behind the tank and heals only.
- A reaching pull ends once any creature has a party victim.
- A big pack (8+ joiners) stand walk is watched every tick.
- It regroups by itself after 3 dead or a wipe, within the fight ceiling of 180 s (420 s on a boss), by reviving the party, gathering it, and walking back.
- It waits for every fallen member until that bot is back alive within 20 yd of the lead bot.
- It recovers a lead bot that is back at the entrance alone by regrouping on it and walking back over the plan's own points.
- It waits 15 s for a resurrection in the party before releasing the dead, while `AiPlayerbot.DungeonRunRezHold = 1` (default `0`) holds for the resurrection instead.
- It summons a revived bot that is over 40 yd away or over 8 yd of height, while out of combat, once per revive.
- It regroups on resume by reviving, gathering over 60-180 s, and walking back.
- It gathers back on the run's anchor when a fight left the lead bot off it.
- It passes hop pulls that draw no fight for the rest of the run.
- It walks back over the plan's own points across a navmesh hole.
- It sends no step while a member is dead and regroups first.
- It retries a refused gather once and then skips it.
- It summons a straggler at the gather ceiling (30 s, 60 s on regroup), once per bot per step.
- It first walks a straggler by path and summons it when it is still out.
- It never summons a member onto a lead bot that stands off the floor: more than 3 yards of air under it, read the way the teleport landing reads the floor (the map's floor, and the feet probe under the feet, the higher one winning), so a lead bot left hovering in mid-air, as Jin'do's Banish does, is no summon point and its own landing or rescue comes first. A wedged member's teleport return is no summon and still runs: it lands beside another member, not at the lead bot's feet. A lead bot on a game-object platform or an elevator (a transport under it, or a surface within 2 yards under its feet that the core's height read finds) counts as on the floor. A lead bot left hanging that way for 30 seconds pauses the run with both heights (the lead bot's and the floor's), whichever summon (the gather's, a runaway's, a revived or stuck member's) was refused, instead of the gather starting over every window, and a lead bot that lands inside those 30 seconds carries on as before.
- It runs a member with attackers back to the party as a passive follower within 120 yd.
- It pauses once on a missing item and then skips the dependent steps.
- It still treats a boss that changed entry mid-fight as the boss.
- It rejoins a bot stuck in another copy of the dungeon or raid instance, at most 3 times per step and 15 s apart.
- It uses the raid boss fight ceiling of 1200 s with `AiPlayerbot.DungeonRunRaidBossRules = 1` (default `0`).
- With `AiPlayerbot.DungeonRunLinkedDeath = 1` (default `0`), linked deaths run the die-together tactic with a `fighting: linked` line.
- It bounds every wait (gather 30 s, summon 10 s, walk back 90 s, pull 25 s, fight 180 s and boss 420 s, ritual answer 5 s, rest 60 s, task 90 s, step 300 s out of combat, and at most 20 pulls per step).
- It pauses at ceilings and nothing loops forever.
- It applies the player-led and real-player rules below.
- Orders are bots-only, offline holds, a player hold lasts 5 min, and summon/revive never pause.
- A rest walk is resent once when the lead gains under 2 yd in 3 s.
- A patroller within 30 yd past the radius is waited up to 60 s.
- **A pull the tank cannot start from range is retried once as a walk-in pull.** When the tank's shot at a boss or pack cannot get it moving (a target hovering above the floor, a firing spot out of reach), the run orders a melee pull at the target's ground position before it searches for a standing spot; only when that starts no fight either does the standing search, the deferral and the pause apply. The standing search around a boss now looks 15-25 yd out like any other (it was 35-25 yd for the default boss radius, so it never found anything and a boss pull that failed always paused the run).
- **A refused pull whose stand search found none with the lead bot on a navmesh island hopped it off first, not paused** (scarlet_graveyard step 32, 2026-11-04: the step-31 fight left the tank on a 3 yd island of the mesh at the foot of a tomb slab, 1796.5 1324.3 z 19.4, a floor sliver that connects to nothing; with the pack in sight its firing stand read, the reach pull and all 36 stand candidates came back NOPATH from that start polygon, the pullback was refused *unreachable* and the run paused *not reachable from this floor: no safe stand 15.0-25.0 yd from it the lead bot has a path to* four times, each resume standing in place, until the stop rule ended it; the island check ran only on the approach hops, the walk backs and the move steps, so an in-sight pull had no way off an island). From then on the glue's stand search, when it found nothing, read the lead bot's own path to the step's point into the answer, and a refusal with the lead bot on an island (no path, or a partial one ending where it stands) reset the stand state and the firing stand's tries (*no firing stand found* had spent them), set the island suspicion and hopped the lead bot off toward the pack on the next tick (`IslandEscape`); the pull then read a firing stand afresh from the mesh the hop ended on. The hops' caps stay: past `kIslandEscapes` (2, or `kStepEscapes` 4 a step), or past `kIslandRounds` (3) rounds that a fight's *wait* answer refunded, a pack that may be deferred is still deferred, as it was before the hops; only a pack that cannot be is paused, and the note names the island (*... move it off, then .bot run resume*). Not an island (a path that gets somewhere): the deferral and the old pause stayed as they were.

- `AiPlayerbot.DungeonRunAutoCcRisky = 1` (default `0`) adds the risky fear families, only with room to run.
- `AiPlayerbot.DungeonRunNearWipeStopParty` gives parties the near-wipe stop too (raid default).
- `AiPlayerbot.DungeonRunPackClearMarginYd` (default 10) is the pack-clear band, and `AiPlayerbot.DungeonRunPackClearRepulls` (default 3) the repulls before pausing.
- `AiPlayerbot.DungeonRunRaidHealerGuard = 1` (default `0`) sends a healer-guard at a loose add on a healer.

Retreat at 2 dead is opt-in with `AiPlayerbot.DungeonRunRetreatAt2Dead = 1` (default `0`), see the [dungeon and raid settings](../server/settings/dungeons-raids.md#aiplayerbotdungeonrunretreatat2dead).
Walk back to the stand after every guarded fight is opt-in with `AiPlayerbot.DungeonRunWalkBackAfterWonFight = 1` (default `0`). A stuck walk back is rescued by default with `AiPlayerbot.DungeonRunWalkBackRescue = 1` (default `1`).
The dry walk is opt-in with `AiPlayerbot.DungeonRunDryWalk = 1` (default `0`), see the [dungeon and raid settings](../server/settings/dungeons-raids.md#aiplayerbotdungeonrundrywalk): the lead bot's approach hops and firing stands come from a ground-only navmesh path, a walk-back shortcut's swim chord is refused so the walk back replays the plan's points, and a walk whose own way would swim after leaving its pool goes in dry legs first, so the party no longer swims into a pool it cannot leave. A point in deep water keeps the old way: an intended swim, a wet firing stand for a pack in the water, and a straggler standing in water are unchanged; a dry straggler whose way would swim is not pulled and its pull can pause the run.

### Combat reads

- A stale flag gets 20 s grace, then the ceiling runs.
- An unattackable-only fight is let go, with no ceiling.
- A pause for the fight ceiling goes on by itself when only such idle nest units keep the party in combat.
- Charmed and fake-death units read as cannot-attack.
- A game-event-gated boss is skipped, not paused.
- Off-plan-map bots never hold fights, and bots never leave the plan map by follow, flee, or trigger.
- A raid wedges a member by summon, never on a boss or plan boss.
- An alive-by-spot-but-unpulled boss pauses at once with distance and height.
- Health progress holds the ceiling, a 1-hp/1%-held unit is watched.

### Rest gate

The rest gate runs after every fight and before every step.
It rests a living bot under 80% health, a healer under 50% mana, or other mana under 35% mana until 90% health, 80% healer mana, or 60% other mana, or for 60 s, and checks again every 20 s.
With `AiPlayerbot.DungeonRunLightRest = 1` (default `1`), before a plain pull or with no fight ahead it starts only under 60% health, 35% healer mana, or 25% other mana, and ends at 75% health, 60% healer mana, or 45% other mana.
With `AiPlayerbot.DungeonRunFightForward = 1` (default `1`), the next pack within 30 yd is pulled from where the tank stands.
- With `AiPlayerbot.DungeonRunStragglerPull = 1` (default `0`), a waited-for unit that never comes is walked to and pulled by the acting tank instead of pausing the run.
- A fight held open only by the run's own crowd control is broken one held unit at a time, without re-applying the hold.
- A charmed group member is never a fight's unit: orders at it are refused and the reads leave it out.

### Aggro estimate

Opt-in aggro estimate (`AiPlayerbot.DungeonRunAggroEstimate = 1`, default `0`, ceiling `AiPlayerbot.DungeonRunAggroCeiling` default 6) counts what the pull would wake before the pullback goes out, and it is approximate advice only. Its walk ring is opt-in (`AiPlayerbot.DungeonRunAggroWalkRing = 1`, default `0`) with ring size `AiPlayerbot.DungeonRunAggroRingYd` (default 20).

### Pull stand

Opt-in pull stand (`AiPlayerbot.DungeonRunPullStand = 1`, default `0`) covers ranged-weapon tanks and orders pullback only when the tank can shoot from where it stands.
Related opt-ins, one line each:

- `AiPlayerbot.DungeonRunStandClearance`: a firing stand is refused when another living pack can reach it, so stands keep clear of other packs.
- `AiPlayerbot.DungeonRunStandPullPreCc`: the plan's marks go out at the last approach point outside the pack's wake reach, before the walk in wakes it.
- `AiPlayerbot.DungeonRunStandDiscipline`: the plan's own stand is the only stand a pull fires from, and tanks and melee are waited for there.
- `AiPlayerbot.DungeonRunAutoCc`: marks crowd control from what the raid has when the pack is over the body count, with `AiPlayerbot.DungeonRunAutoCcAbove` (default 6) setting it. A fight held open only by the run's own crowd control is broken one held unit at a time, without re-applying the hold. A charmed group member is never a fight's unit: orders at it are refused and the reads leave it out.

### Totem focus

Opt-in (`AiPlayerbot.DungeonRunTotemFocus = 1`, default `0`). A totem on the plan's kill-first list dies in 1-2 s, so the run focuses it first in the fight, before the guard, the crowd control or any other order can spend the tick. Only the damage dealers go: no tank, no healer. A totem a boss summons is read as soon as it stands, even when no bot has it on its attackers list. The damage dealers cut their own cast or channel first, but only an offensive one on an enemy — never a heal, a shield, a buff or a defensive cooldown, never an Auto Shot or a wand's shot, and never a crowd-control spell of the run, a resurrect or a hearthstone; they open with an instant where their class has one, and take the attack by GUID, so the skull stays on the boss and nobody follows it to the totem. While the party is pulling back from a lost fight, the totem waits. The kill-first check runs every second while the key is on, and the focus is sent again every 3 s while its totem lives and fewer than three damage dealers are attacking it (every living one, when the party has fewer), so the damage dealers that left it after one burst come back to finish it; the re-send goes out ahead of the crowd control and the guard, after the fight ceiling and the retreat (a kill-first unit that is no totem is focused once, not repeated). A bot carrying Delusions of Jin'do is never sent at a totem and is not counted as one of its attackers: its damage belongs to the Shade that curse names. The server log carries a `TBM:KILLFIRST|<plan>|<lead>|<instance>|<seen|dead|gone>|<entry>|<guid>[|<ms>]` line when a kill-first unit is first seen and when it dies.

### Events

A scripted event is a start (`ritual`, `open`, `use_item`, `gossip`, or `use_on`) plus a `hold`.
The hold walks the party to the spot, stands there, sends no pulls of its own, and fights what comes, with ranged and healer stands kept and the party walked back when the lead bot drifts over 8 yd off the spot.
It finishes by flag `8` when the instance data reads `param` (with flag `32` reading encounter states 0/1/2/3, failing on 2 or reset, and on 30 s with no start), by flag `16` when the object within 30 yd reads `param`, and otherwise when all its units are dead after one was seen awake (ending 10 s after an all-dead hold whose units died before it).
`timeout_s` bounds every hold, and a failed event goes back to its start step and pauses.
- A hold places the party, returns on fall, regroups on pause, self-resumes, and fails when it cannot start.
- `reset_to_gossip` re-arms once then pauses at gossip, and `dead_gossip_ok` ends when none is hostile if the start NPC was dead.


| Event | Start | End |
| :--- | :--- | :--- |
| Keepers | Altar ritual | 4 keepers dead, then the door opens. |
| Archaedas | Altar ritual | Waves fought, then the vault opens. |
| Keystone | Open with the Staff | The door opens. |
| Seal | Wait step | The seal reads open. |
| Temple Doors | Open | The doors read open. |

### Keys and doors

The plan lists each locked door with its key and, where one works, the Lockpicking skill that opens it instead (`ai_dungeon_leg_need`).
In a bot-only party with `AiPlayerbot.DungeonRunGiveItems = 1` (default `1`), bots carry the keys and you do nothing.
With players in the party and `AiPlayerbot.DungeonRunBotKeysForPlayers = 0` (default `0`), you bring the key or the Lockpicking skill, the start warns once, the bots hold about 25 yd before the closed door, and the run goes on by itself once the door reads open, where resume and skip both work.
With `= 1`, bots carry the keys for players too.
A leg without its item is skipped with a note.
At a closed door the lead bot asks on say, and the run note and status show it too.

### Player-led runs

Player-led runs (`AiPlayerbot.DungeonRunPlayerLed = 1`, the default) sit on top of `AiPlayerbot.DungeonSelfClear = 1`.
The party leader starts `.bot run start [plan] [from ...] player [tank <Bot>]` from his own party (not a raid, every bot controllable, up to 3 other players).
The player walks and the run leader pulls near him (within `AiPlayerbot.DungeonRunPlayerLedPullYd`, default 40).
Player orders never pause the run (only pause and stop do).
Staying away from the dungeon or raid instance for over `AiPlayerbot.DungeonRunPlayerLedAwayMinutes` (default 10) stops the run.
- Orders are move, fight, or control; the step is the adopted fight else the nearest pull within 40 yd on the same floor and team; object steps gate on their leg order.

### Raids

A raid bind takes 6-40 bots from your own raid, with you taken out and the lead-bot tank as main tank.
It applies the raid layout at the start and restores it at the end: with `AiPlayerbot.RaidHealerSeatsByClass = 1` the healer seats go by class instead of member order, so the raid seats take a group healer first (priest, druid, shaman) and the paladin is not always the lone off-tank healer; with `AiPlayerbot.RaidTankHealerAssist = 1` the second healer on a tank helps the raid while the tank is quiet (not on a boss, 90%+ after incoming damage) and only with a raid member in sight and in heal range below 70%.
Gather and wipe rules scale to raid size.
A near-wipe stops the run with `AiPlayerbot.DungeonRunNearWipeStop = 2` and `AiPlayerbot.DungeonRunNearWipeStopMinutes = 10`, pausing after the fight.
- It runs `AiPlayerbot.DungeonRunRaid = 1` for the raid bind.
- Offline bots stay out of the counts, and leash/direct steps wait for every bot.

### Plans on the server

Uldaman and Zul'Farrak are live-checked, Ragefire Chasm and Wailing Caverns are mapped, and the rest are drafts. `.bot run list` shows the loaded plans.

### Cost

A running run ticks once per second with at most one local grid search near the lead bot within 60 yd.
A hold done by data reads one value a tick, attacker sets come from the core, and nothing touches the database after startup.
Not supported:

- Avoid boxes are unknown.
- `gm_cross` steps are unsupported.
- Open-water moves are unsupported.
- Plan `skip` is unsupported.
- Per-leg `rest` is unsupported.
- `companions_loot` is unsupported.
- `area_trigger` is exported with a warning but never runs.
With `AiPlayerbot.DecisionCapture.Enabled = 1` (off by default, test servers only) every tick's facts, run and order are also written to a rotating capture: see [Decision capture](../server/decision-capture.md).
