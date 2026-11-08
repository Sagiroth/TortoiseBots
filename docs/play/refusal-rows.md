---
id: play-refusal-rows
title: Refusal and Abort Rows
category: play
summary: The inventory of log rows and run notes that record a decision not to act, the reason field each carries and the fixed word set behind it.
tags: [guide, observability, logging, dungeon, boss, tactics]
relates_to:
  - play-boss-tactics
  - play-dungeon-run-leader
  - server-configuration
---

# Refusal and Abort Rows

A row that records a decision **not to act** (a refusal, a hold, a skip, an abort, a "no X" scan result) must say why as a
code a script can count. Walk 52 of the Zul'Gurub raid had to reconstruct "why was there no puller" from frames, auras and
deaths because the row `seek pull: walk-out no puller stand=<x>,<y> reach=<yd>` said nothing about the 20 members the
pick rejected.

## The rule

> **Every refusal row carries a reason code.** A log row that records a decision NOT to act (a refusal, a hold, a skip, an
> abort, a "no X" scan result) must carry `reason=<code>` (or an existing fixed-word field such as `veto=`, `verdict=`,
> `trigger=`) from a small fixed word set documented next to the emit and in this file. When several candidates were
> rejected, the row carries per-code counts (`reason=a:n|b:n,members=m`). The reason is computed from values the decision
> already read; adding it must not change behaviour, add game-state reads or change an existing row's prefix. A behaviour
> brief that adds a new refusal path names its reason codes and the test that pins them.

"Prefix" is the text before the first `,` or the first `=` value an analysis script matches on (`seek pull: walk-out no
puller`, `seek pull: walk-out abort move failed`, ...). New fields are appended as `,key=value` pairs after the existing
text; an existing row never gets a shorter or reworded prefix.

A row is **yes** in the tables below only when a script can count its reason from a fixed word set. Free text alone
(an entry id, a player name, the module's prose) is **partly**.

## The walk-out pick's codes

`FindSeekWalkoutPuller` fills a `WalkoutRejectTally` (`ai/playerbot/SeekWalkoutPolicy.h`) for the members it rejected and
the no-puller row carries it as its info2:

```
reason=away:1|dead:2|threat:3|no_path:1,members=20 top=4200 low=3100
```

`top` is the threat reference the pick compared against (the live top tank's threat with a `seek_pull` row, 0 when that
comparison is off) and `low` is the lowest threat among the candidates that passed every cheap test; both stay `-1` when
the pick never measured them.

| Code | Meaning |
| :--- | :--- |
| `away` | Off the bot's map, or not in the world (the group slot the loop could not see in place). |
| `no_ai` | The member has no `PlayerbotAI`. |
| `hunter_only` | The hunter-reach pass ran and the member is not a hunter. |
| `dead` | Not alive. |
| `no_role` | No ranged damage role (a melee damage dealer). |
| `tank` | A tank. |
| `healer` | A healer. |
| `victim` | The boss's current victim. |
| `threat` | At `kSeekWalkoutThreatShare` of the reference or more. Without a `seek_pull` row the reference is the main tank (`WalkoutThreatTooHigh`, 0.7 of it); with one it is the live top tank (`WalkoutPickThreatTooHigh`). |
| `cc` | Charmed, or carrying Cause Insanity. |
| `no_shot` | Its own opener (`WalkoutShotSpell`) cannot be cast at the Son now. With the row's cast-later rule (`WalkoutShotReadyLater`) a cast already in progress or a spell on cooldown is not a refusal. |
| `outside` | It stands outside the anchor (the "reach" half of the pick). |
| `bench` | Left out of the picks: its last task's moves failed less than `kSeekWalkoutExcludeMs` ago. |
| `no_path` | It has no navmesh path to the stand. |

The codes come in `WalkoutEligible`'s order (dead beats tank, tank beats threat), except that `tank` and `healer` are
checked before `no_role`: the role is classified from the ranged-damage check alone (`rangedDps`), so a melee tank or a
healer reads `WalkoutRole::None` and would otherwise count as `no_role`. `threat` and the cast half of `no_shot` are only
reached after every cheap test passed (alive, role, tank/healer, victim, charm, platform, bench): the threat lookup and the
cast check are the pick's costly questions, asked last. `no_path` is the only code added after the ranking, because the
navmesh path is the costliest question the walk-out asks; the members the loop could not even make a candidate of are
`away`, `no_ai` and `hunter_only`. There is no line-of-sight or busy code: the sight test is the stand's
(`FindSeekWalkoutStand`), not the pick's.

## Rows

The rows are grouped by event source. `BossTacticsActions.cpp` is `ai/playerbot/strategy/actions/BossTacticsActions.cpp`.
The Emit column names the function that builds the row (the `logEvent` call or the shared helper it goes through), so a
ref survives line drift.

### Seek pull (event `BossTacticsSeekPull`)

| # | Row | Row text / info1 shape | Emit | Reason today | This branch |
| ---: | :--- | :--- | :--- | :--- | :--- |
| 1 | walk-out timeout | `seek pull: walk-out timeout shot=%d dist=%.1f retry=%d spell=%u los=%d z=%.1f stand=%.1f` | LogSeekWalkoutTimeout | yes: `retry=`, `shot=`, `los=` | keep |
| 2 | walk-out end | `seek pull: walk-out <back\|timeout\|abort>` | LogSeekWalkoutEnd | yes: the end word | keep |
| 3 | walk-out abort | `seek pull: walk-out abort <reason> dist=%.1f stand=.. moved=%d path=%u len=%.1f why=%s` | LogSeekWalkoutAbort | yes: the `<reason>` word plus `why=` | keep |
| 4 | abort reset | same shape, `reason` = `reset`, then ` cause=<repull\|evade>` after `why=` | ForgetSeekWalkout | no (a re-pull and an evade look alike) | now `cause=repull` / `cause=evade` |
| 5 | abort cast failed | same shape, `reason` = `cast failed` | AbortSeekWalkoutUnspent | yes | keep |
| 6 | abort move failed | same shape, `reason` = `move failed` | NoteSeekWalkoutMove | yes | keep |
| 7 | abort no shot | same shape, `reason` = `no shot`; info2 ` los=<0\|1> inrange=<0\|1> spell=<id> onson=<0\|1> front=<0\|1> moving=<0\|1> dz=<yd>` | UpdateSeekWalkout | yes | keep |
| 8 | abort puller problem / son gone | same shape, `reason` ∈ `dead,charmed,insanity,victim,threat,gone,son gone` | UpdateSeekWalkout | yes | keep |
| 9 | abort no stand | same shape, `reason` = `no stand` | UpdateSeekWalkout | yes | keep |
| 10 | late | `seek pull: walk-out late due=%u need=%u` | UpdateSeekWalkout | implicit (the start needs `firstStartMinMs`) | now `need=<firstStartMinMs/1000>` |
| 11 | no stand | `seek pull: walk-out no stand sons=%d reach=%.0f hunter_pass=%d` | UpdateSeekWalkout | no (not how many Sons, at what reach) | now the scan's counts |
| 12 | no puller | `seek pull: walk-out no puller stand=%.1f,%.1f reach=%.0f` | UpdateSeekWalkout | no (the 20 rejected members are invisible) | now info2 `reason=<code>:<n>\|...,members=<m> top=<threat> low=<threat>`; info1 unchanged |
| 13 | no son | `seek pull: walk-out no son walked=%d` | UpdateSeekWalkout | yes: every Son near is `walked` this cycle (the count is the reason) | keep |
| 14 | ranged walk-out start | `seek pull: ranged walk-out <name> -> <son> due=%u try=%d reach=%.0f ray=%.0f` (with a `seek_pull` row also ` tr=%.2f`, the bot's threat over the live top tank's) | UpdateSeekWalkout | not a refusal (the decision to act) | document |
| 15 | skip anchor | `seek pull: skip anchor` | LogSeekPullSkipAnchor | yes: constant meaning "no Son stands inside the live anchor" | keep, documented here |
| 16 | ranged from anchor | `seek pull: ranged from anchor` | LogSeekPullRangedFromAnchor | not a refusal (the anchored shot replaced the walk) | document |
| 17 | per-bot pull step | `seek pull: <name> -> <son> <yd> <step>` | LogSeekPull | not a refusal (the step is `walk`, `hit` or `done`) | document |
| 18 | seek take | `seek take: <name> -> <son> victim=<name>` | LogSeekTake | not a refusal (the off-tank takes the Son) | document |

### Other boss-tactics rows

| # | Event | Row text / info1 shape | Emit | Reason today | This branch |
| ---: | :--- | :--- | :--- | :--- | :--- |
| 19 | `BossTacticsDpsHold` | `subject=<entry>`; info2 `trigger=<engage\|band\|spell\|blink\|none>,held_ms=<n>` | LogDpsHold | yes: `trigger=` | keep |
| 20 | `BossTacticsHoldKill` pick | `held=<e>:<g>,hp=<pct>,held_ms=<n>`; info2 `blockers=..,to=..` | LogHoldKillPick | not a refusal (the hold's pick) | document |
| 21 | `BossTacticsHoldKill` veto | `veto=<assist\|aoe\|area\|dot\|sole>` | LogHoldKillVeto | yes: `veto=` | keep |
| 22 | `BossTacticsDispel` pick | `aura=<id>,carrier=<guid>`; info2 `mode=..,rank=..,ally_yd=..,verdict=pick` | LogDispel | yes: `verdict=` | keep |
| 23 | `BossTacticsDispel` excluded / wait | same shape, `verdict=excluded` / `verdict=wait` | LogDispel | yes: `verdict=` | keep |
| 24 | `BossTacticsSonHold` start | `son=<e>,event=start,reason=tick,held_ms=..` | PushSonHoldRow | yes: `reason=` | keep |
| 25 | `BossTacticsSonHold` release | `son=<e>,event=release,reason=<cap20\|nontank\|selected_burn\|dosed\|held>,...` | PushSonHoldRow | yes: `reason=` | keep |
| 26 | `BossTacticsSonHold` end | `son=<e>,event=end,reason=<died\|gone\|unticked\|unseen>,...` | PushSonHoldRow | yes: `reason=` | keep |
| 27 | `BossTacticsSonDeath` | `son=..,event=death,phase=<phase>,...` | PushSonDeathRow | yes: `phase=` names the hold state | keep |
| 28 | `BossTacticsSiphon` | `cycle=..,at=..,dosed=..,undosed=..,...` | LogSiphon | not a refusal (a measurement) | document |
| 29 | `BossTacticsSeek` | `phase=<cloud\|hold\|gather>,aura=..,dist=..,due_in=..,cycle=..` | LogSeek | not a refusal (the seek task's phase) | document |
| 30 | `BossTacticsLinkedWindow` | `first_ms=..,last_ms=..,window_ms=..,inside=%d` | ScanBossLinkedDeath | not a refusal (the window fired) | document |
| 31 | `BossTacticsFreeze` | `aura=<id>`; info2 `ms=<n>,moved_yd=<d>` | LogFreeze | not a refusal (an episode's end) | document |
| 32 | `BossTacticsFlee` | `aura=<id>,mode=<away\|to_tank>`; info2 `ally_yd=..,radius=..,walk=<started\|none>` | LogFlee | partly: `walk=none` has no cause (the walk's own path refusal is only known as a bool) | inventory only |
| 33 | `BossTacticsKillFirst` | `reason=<not_read\|not_attackable\|beyond_reach\|reattack\|player_order>`; with `AiPlayerbot.DungeonRunCarrierScan` on `<no_unit\|ineligible\|beyond_reach\|no_los\|invalid_target\|not_pickable\|reattack\|player_order\|attack_refused>`, and the not-a-refusal word `scan_over` (info2 `pick=<entry>:<guid low>,over=<entry>,mode=<carrier\|free>`: the scan's Shade took the pick over a ward or totem, one row per bot per 5 s) (`attack_refused`: any carrier pick `Attack()` refuses, the scan's or the ordinary one), and info2 gains `,codes=<word>:<n>[\|<word>:<n>],grid=<hits>,pickable=<0\|1>,mode=<carrier\|free\|none>` after `current=` (`codes=` counts each grid hit by the check that refused it, and `none:<n>` counts hits that passed every check; `mode=none` is a player's order) | ai::LogCarrierNoSwitch | yes: `reason=` | keep |
| 34 | `BossTacticsCarrierStrike` | `verdict=<strike\|not_carrier\|no_shade\|main_tank\|threat_successor\|holds_boss\|guard_low_member\|guard_tank\|guard_healers\|guard_mana\|guard_dispel>,role=..` | ai::LogCarrierStrike | yes: `verdict=`; the emit's comment omits `threat_successor` | comment fix |
| 35 | `BossTacticsTankSwap` | `taunt=<guid>,stacks=<n>`; info2 `from=<guid>` | ai::NoteBossTankSwapCast | not a refusal (the swap was cast) | document |
| 36 | `BossTacticsAvoidCast` | `caster=<e>:<g>,spell=<id>`; info2 `dist=..,radius=..,dest=..,stopped=..,walk=<started\|deferred\|none>` | LogAvoidCast | partly: the site knows whether no spot was found (`count==0`) or every point's `MoveTo` said no, without a new read | inventory only |
| 37 | `BossTacticsUseObject` skip | `phase=skip,...,reason=<no_object\|window\|moving\|far>` | LogUseObject | yes: `reason=` | keep |
| 38 | `BossTacticsUseObject` claim / walk / click | `phase=<claim\|walk\|click>,go=..,boss=..,...` | LogUseObject | not a refusal (progress) | document |
| 39 | `BossTacticsAnchor` | `boss=..,go=..,from_yd=..,arrived_ms=<n\|none>` | LogUseObjectAnchor | not a refusal (start / arrival) | document |
| 40 | `BossTacticsTauntAfter` | `subject=<entry>`; info2 `trigger=until_tank,tank=..,regained_ms=..,taunted=%d` | LogTauntAfter | not a refusal (a window's outcome; `taunted=0` is the miss) | document |
| 41 | `BossTacticsBurnMana` | `spell=<id>,boss_mana_pct=<pct>` | LogBurnMana | not a refusal (the cast went out) | document |
| 42 | `BossTacticsRangedOnly` | `subject=<entry>,gap=%.1f` | ai::LogBossRangedOnly | not a refusal (the hold is active) | document |
| 43 | `BossTacticsThreatCap` | `threat cap: ref=<main\|offtank> <threat> offtank_target=<name\|none>` | LogThreatCap (`ai/playerbot/strategy/values/ThreatValues.cpp`) | not a refusal (the reference a cap is measured against) | document |
| 44 | `BossTacticsTauntGate` | `reason=<needs_aura\|taunt_immune>`; info2 `target=<entry>:<guid low>,spell=<spell>` | ai::TauntRefusedByGate (`ai/playerbot/strategy/actions/BossTacticsActions.cpp`) | yes: `reason=` | keep |

### Healer

| # | Event | Row text / info1 shape | Emit | Reason today | This branch |
| ---: | :--- | :--- | :--- | :--- | :--- |
| 45 | `HealSkip` | `out-of-range` / `line-of-sight` / `no-power` / `casting` / `moving` / `target-invalid` / `not-in-control` / `not-ready` / `not-known` / `cast-check-failed`; info2 `spell=.. result=.. ...` | `HealerDiagnostics::CheckFailed` | yes: the info1 word | keep |
| 46 | `HealSkip` | `execute-refused` | `HealerDiagnostics::CastAttempt` | no (the execute API returns a bool only) | inventory only |
| 47 | `HealState` | `snapshot` | `HealerDiagnostics::Sample` | not a refusal (a state sample) | document |

### Unstuck

| # | Event | Row text / info1 shape | Emit | Reason today | This branch |
| ---: | :--- | :--- | :--- | :--- | :--- |
| 48 | `LongStuckFallback` | `graveyard` / `homebind` | `LongStuckFallbackTeleport` | not a refusal (the fallback moved the bot) | document |
| 49 | `ReachGiveUp` | info1 the target's name; info2 `combat-stuck\|entry=..\|dist=..\|los=..` | `GiveUpCombatStuckTarget` | yes: the `combat-stuck` prefix | keep |
| 50 | `UnstuckTrip` | info1 the stuck source; info2 the trips swallowed | `UnstuckAction::Execute` | not a refusal (an episode starts) | document |

### Dungeon run notes

These are the run's `r.note` / `r.stragglerFail` texts (and the `Pause` / `NextStep` notes that carry them), not
`bot_events.csv` rows: the module's status shows them, and each carries the module's answer after `(`. They are listed
because a "why did the run stop" script reads them the same way. `runtime/DungeonRunPolicy.h` below.

| # | Row family | Row text | Emit | Reason today | This branch |
| ---: | :--- | :--- | :--- | :--- | :--- |
| 51 | walk to the lead refused | `the walk of <bot> to the lead was refused (<answer>)` | `StragglerWalkRefused` | yes: the module's answer | keep |
| 52 | cmd refused, no point | `cmd <order> refused (<answer>), no reachable point near it either` | `RefusedFromIsland` | yes: the answer | keep |
| 53 | straggler order refused | `its <order> was refused (<answer>)` | `OrderRefused` | yes: the answer | keep |
| 54 | retreat refused | `retreat refused (<answer>): fighting` | `OrderRefused` | yes: the answer | keep |
| 55 | test-speed hold refused | `test speed: the hold for the party was refused (<answer>): walking on` | `OrderRefused` | yes: the answer | keep |
| 56 | teleport failed | `the teleport of the stranded lead bot failed (<why>)` | `StrandFailed` | yes: the glue's `why` | keep |
| 57 | island hop not made | `the straight hop off the navmesh island was not made (<answer>)` | `OrderRefused` | yes: the answer | keep |
| 58 | revive summon refused | `... summoned to it, refused (<answer>)` | `OrderRefused` | yes: the answer | keep |
| 59 | free-for-all refused | `loot: free-for-all refused (<answer>): looking for another bot to take it` | `OrderRefused` | yes: the answer | keep |
| 60 | order refused | `<order> refused: <answer>` (and the `: tried again in .. s` / `: the focus is picked again` variants) | `OrderRefused` | yes: the answer | keep |
| 61 | walk to the body refused | `regroup: the walk to the body was refused (<answer>)` | `OrderRefused` | yes: the answer | keep |
| 62 | gather refused | `gather refused (<answer>): <sending it once more\|going on without it>` | `OrderRefused` | yes: the answer | keep |
| 63 | stand point refused | `back to the stand point refused (<answer>): <going on from here\|...>` | `OrderRefused` | yes: the answer | keep |
| 64 | pull refused | `pull refused (<answer>): that unit is left out` | `OrderRefused` | yes: the answer | keep |
| 65 | point refused (hop / walk back) | `<point> refused (<answer>) ...` | `OrderRefused` | yes: the answer | keep |
| 66 | use_item refused | `use_item <entry> refused (<answer>): an earlier item is missing` | `OrderRefused` | yes: the answer | keep |
| 67 | leg refused | `a leg toward <x> <y> <z> was refused too (<answer>)` | `TooFarLegTaken` | yes: the answer | keep |
| 68 | move to the plan's point refused | `step <n>: cmd move to the plan's point <p> refused (<answer>)` | `MoveRefusedUnreachable` | yes: the answer | keep |
| 69 | fight forward refused | `fight forward: not to the pack of step <n> (<refused>)` | `FightForwardTick` | yes: the reason word | keep |
| 70 | boss gate | `<BossGateNote>; not pulling` | `BossGateTick` | yes: the gate's own note | keep |
| 71 | party member dead | `<WaitNote>: no pull while a party member is dead` | `PullUnit` | yes: constant | keep |
| 72 | skipped, not cleared | `<why>: skipped, not cleared (<counts>)` | `StandPullGiveUp`, `FightStep` | yes: the note's own `why` | keep |
| 73 | skipped step | `skipped step <n>` / `leg <x> skipped: it needs item <id>` | `Skip`, `LegNeedGate` | yes: the step's own reason | keep |

## Rows still without a fixed-word reason

Three sites know the outcome but not a scriptable cause without a new read; they are recorded here, not changed in this
branch:

- `BossTacticsAvoidCast` `walk=none`: the action knows whether `AvoidCastSpots` found no point or every point's `MoveTo`
  refused, but the row does not say which. Adding it is a one-word change to the log call; it was left out of this
  branch's scope.
- `BossTacticsFlee` `walk=none`: `RunOutFromGroup` returns a bool; its own refusal is not broken down where the row is
  built.
- `HealSkip execute-refused`: the execute API returns a bool; there is no failure code to log without a new read.

## Tests

- `tools/test_seek_walkout_policy.cpp` pins `WalkoutRejectOf` (one candidate per code, the precedence, and a melee tank and a
  ranged healer classified through `WalkoutRoleOf` reading `tank` and `healer` rather than `no_role`) and `WalkoutRejectText`
  (the exact text and the members count).
- `tools/test_seek_walkout_wiring.py` pins the glue: the scan's tally is passed to the pick, the no-puller row passes the
  text as info2 with its info1 literal unchanged, the pick adds no game-state read (its member counters, the post-loop
  `WalkoutRejectOf` pass and the `no_path` add included), and `ForgetSeekWalkout`'s two callers name `repull` and `evade`.
