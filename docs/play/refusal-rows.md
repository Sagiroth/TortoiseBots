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
| `no_combat` | With `DungeonRunSeekWalkoutHoldCombat` only: its AI is not on the combat engine, or it is eating or drinking (the walk-out is a combat-engine action). Its own engine is read, never its pet's combat. |
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

## The walk-out stand row's words

`DungeonRunSeekWalkoutStandLog` (diagnostic) writes the stand row once a second while the walk-out puller walks out
unshot. `state=` is the puller's AI engine (`c` combat, `n` non-combat), but `state=n` cannot appear: the row is written
only from the walk-out action, which runs only on the combat engine. A missing row (a gap with no `branch=` at all) is the
signal that the tick did not run. `tick_ms=` is the time since the walk-out's previous
tick (0 on its first).

| `branch=` | Meaning |
| :--- | :--- |
| `out` | Walking out: not within 3 yd of the stand and not in shot position. |
| `inpos` | At the stand (within 3 yd) but not in shot position: range, the stand's floor or the sight line is missing. |
| `wait` | A pull shot sent under 2 s ago is waited for (the Son not in combat yet). |
| `face` | Turned to the Son with the cast left to the next tick (the walk-out focus without `DungeonRunSeekWalkoutFaceCast`). |
| `castwait` | Its own cast on the Son is still running (`cast=` names it). |
| `checkfail` | The shot's cast check refused; the `abort cast failed` row follows. `refuse=check_<n>` is the cast check's noted result (0: none noted). |
| `cast` | The pull cast went out (`cast=` is its spell id). |
| `castrefused` | `CastSpell` returned false (`refuse=` as in the castrefused row). |

The `refuse=` and castrefused `reason=` words, from `WalkoutCastRefuseWord`: `spell_fail_<n>` (the core's or the cast
check's own SpellCastResult, noted by `CastSpell`: spell in progress, or `prepare()` refused), else the `CastSpell` gate
the puller's state just before the call points at: `flying`, `stand_delay` (it was sitting), `facing_delay` (moving and
not facing the Son), `moving` (moving with no master), else `gate` (the self-harm, dispel-hold or pet-redirect gate).
`-` in the stand row: no refusal.

## The take's reason codes

An off-tank's take of a Son of Hakkar (`LogSeekTake`) says what it did about the taunt this tick. `taunt` is the cast
going out; the other four are refusal codes (the take did not taunt this tick):

```
seek take: Vanthas -> 49033 victim=Mira reason=chase_refused meet=0 src=receiver
```

| Code | Meaning |
| :--- | :--- |
| `chase_refused` | The anchored take refuses to chase the Son (it stands outside the raid's circle) and this take cannot taunt it from where it stands, so `Attack` alone is left. With `DungeonRunSeekTakeEdge` on a Son outside the circle is still chased when its victim stands inside the raid circle (plus 2 yd), the Son's z is 46.5 or higher, the Son is within 8 yd of the circle and within 10 yd of that victim; the word then only comes for a Son whose victim is outside the circle or that stands below the platform. |
| `edge_far` | `DungeonRunSeekTakeEdge` is on, the Son's victim is inside the raid circle and the Son is on the platform, but the Son stands more than 8 yd past the circle or more than 10 yd from that victim, so the take does not chase it (`Attack` would walk the tank off the platform). Only distance failed; counted with `chase_refused` as a refusal. |
| `no_taunt` | The tank's class has no taunt (a paladin in 1.12, a shaman, a hunter, ...). |
| `attack` | The tank has a taunt but cannot cast it now (cooldown, range, silenced), so only the attack order goes out. |
| `taunt_gated` | The tank could cast the taunt, but `AiPlayerbot.DungeonRunTauntGate` refused it at this Son (a taunt-immune creature, or a needs_aura subject at a bot that lacks the aura, or a Shade at the main tank holding Jin'do); the gate's own `BossTacticsTauntGate` row says which. |
| `taunt` | Not a refusal: the taunt (Taunt or Growl) is cast this tick. |

`meet=1` marks a take from the pulled Son's meet (`DungeonRunSeekSonMeet`); every other take is `meet=0`.

`src=` names the take path that produced the task (always logged, no key):

| Word | Path |
| :--- | :--- |
| `meet` | The pulled Son's receiver takes it on arrival (`DungeonRunSeekSonMeet`). |
| `nearest` | The first off-tank's nearest take (`DungeonRunSeekNearestTake`). |
| `nearest_reach` | The first off-tank's nearest take that only the 20 yd reach let through (`DungeonRunSeekNearestTakeReach`). |
| `healer_clock` | The 3 s healer clock: the first off-tank (three tanks) or the pickup tank (two tanks) takes a Son that sat on a non-tank. |
| `receiver` | The third tank (the receiver) takes a fighting Son near the spot. |
| `pickup` | With two tanks, the first off-tank takes a fighting Son near the spot. |
| `pull_join` | The third tank's own pull target joined the fight before the pull finished. |
| `other` | Any take without its own word. |

`+edge` follows the word (e.g. `nearest+edge`) when the Son stood outside the raid circle and only the take edge
(`DungeonRunSeekTakeEdge`) let the take chase it.

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
| 13a | walk-out preempted | `seek pull: walk-out preempted action=<name>` | BossTacticsWalkoutFocusMultiplier | yes: `action=` names the first combat action the walk-out focus held (`DungeonRunSeekWalkoutFocus`): the action the engine was about to run that tick; one row per walk-out | new |
| 13b | walk-out stand | `seek pull: walk-out stand branch=<word> state=<c\|n> inrange=<0\|1> los=<0\|1> front=<0\|1> stopped=<0\|1> cast=<spell id> refuse=<reason> tick_ms=<ms>` | LogSeekWalkoutStand | yes: `branch=` and `refuse=` (below); one a second per walk-out (its first tick at once, at most 26 a task), only with `DungeonRunSeekWalkoutStandLog` | new |
| 13c | walk-out castrefused | `seek pull: walk-out castrefused spell=<id> reason=<reason>` | BossTacticsSeekTankAction (walk-out) | yes: `reason=` (below); every refused pull cast, at most 10 a walk-out, only with `DungeonRunSeekWalkoutStandLog` | new |
| 14 | ranged walk-out start | `seek pull: ranged walk-out <name> -> <son> due=%u try=%d reach=%.0f ray=%.0f` (with a `seek_pull` row also ` tr=%.2f`, the bot's threat over the live top tank's) | UpdateSeekWalkout | not a refusal (the decision to act) | document |
| 15 | skip anchor | `seek pull: skip anchor` | LogSeekPullSkipAnchor | yes: constant meaning "no Son stands inside the live anchor" | keep, documented here |
| 16 | ranged from anchor | `seek pull: ranged from anchor` | LogSeekPullRangedFromAnchor | not a refusal (the anchored shot replaced the walk) | document |
| 17 | per-bot pull step | `seek pull: <name> -> <son> <yd> <step>` | LogSeekPull | not a refusal (the step is `walk`, `hit` or `done`) | document |
| 18 | seek take | `seek take: <name> -> <son> victim=<name> reason=<word> meet=<0\|1> src=<word>` | LogSeekTake | yes: `reason=` from the take's words (`chase_refused`, `edge_far`, `no_taunt`, `attack`, `taunt`) (the walk-out pick's codes above are a different set) | now `reason=` (the take's taunt state), `meet=` and `src=` (the take path) |

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
| 33 | `BossTacticsKillFirst` | `reason=<not_read\|not_attackable\|beyond_reach\|reattack\|player_order>`; with `AiPlayerbot.DungeonRunCarrierScan` on `<no_unit\|ineligible\|beyond_reach\|no_los\|invalid_target\|not_pickable\|reattack\|player_order\|attack_refused>`, the not-a-refusal word `switch` (`reason=switch`, info2 `pick=<entry>:<guid low>,from=<entry|0>,since_ms=<ms>`: a carrier turned onto a needs_aura Shade, `since_ms` = server time since this bot's pick first saw that Shade; one row per bot per Shade, no flag), and the not-a-refusal word `scan_over` (info2 `pick=<entry>:<guid low>,over=<entry>,mode=<carrier\|free>`: the scan's Shade took the pick over a ward or totem, one row per bot per 5 s) (`attack_refused`: any carrier pick `Attack()` refuses, the scan's or the ordinary one), and info2 gains `,codes=<word>:<n>[\|<word>:<n>],grid=<hits>,pickable=<0\|1>,mode=<carrier\|free\|none>` after `current=` (`codes=` counts each grid hit by the check that refused it, and `none:<n>` counts hits that passed every check; `mode=none` is a player's order) | ai::LogCarrierNoSwitch | yes: `reason=` | keep |
| 34 | `BossTacticsCarrierStrike` | `verdict=<strike\|not_carrier\|no_shade\|main_tank\|threat_successor\|holds_boss\|guard_low_member\|guard_tank\|guard_healers\|guard_mana\|guard_dispel>,role=..` | ai::LogCarrierStrike | yes: `verdict=`; the emit's comment omits `threat_successor` | comment fix |
| 35 | `BossTacticsTankSwap` | `taunt=<guid>,stacks=<n>`; info2 `from=<guid>` | ai::NoteBossTankSwapCast | not a refusal (the swap was cast) | document |
| 36 | `BossTacticsAvoidCast` | `caster=<e>:<g>,spell=<id>`; info2 `dist=..,radius=..,dest=..,stopped=..,walk=<started\|deferred\|none>` | LogAvoidCast | partly: the site knows whether no spot was found (`count==0`) or every point's `MoveTo` said no, without a new read | inventory only |
| 37 | `BossTacticsUseObject` skip | `phase=skip,...,reason=<no_object\|window\|moving\|far>` | LogUseObject | yes: `reason=` | keep |
| 38 | `BossTacticsUseObject` claim / walk / click | `phase=<claim\|walk\|click>,go=..,boss=..,...` | LogUseObject | not a refusal (progress) | document |
| 39 | `BossTacticsAnchor` | `boss=..,go=..,from_yd=..,arrived_ms=<n\|none>` | LogUseObjectAnchor | not a refusal (start / arrival) | document |
| 40 | `BossTacticsTauntAfter` | `subject=<entry>`; info2 `trigger=until_tank,tank=..,regained_ms=..,taunted=%d` | LogTauntAfter | not a refusal (a window's outcome; `taunted=0` is the miss) | document |
| 41 | `BossTacticsBurnMana` | `spell=<id>,boss_mana_pct=<pct>` | LogBurnMana | not a refusal (the cast went out) | document |
| 42 | `BossTacticsRangedOnly` | `subject=<entry>,gap=%.1f` | ai::LogBossRangedOnly | not a refusal (the hold is active) | document |
| 43 | `BossTacticsThreatCap` | `threat cap: ref=<main\|offtank> <threat> offtank_target=<name\|none> holder=<main\|off\|none>` | LogThreatCap (`ai/playerbot/strategy/values/ThreatValues.cpp`) | not a refusal (the reference a cap is measured against) | document |
| 44 | `BossTacticsTauntGate` | `reason=<needs_aura\|taunt_immune\|holding_boss>` (`needs_aura`: the bot lacks the row's aura; a carrier of it is not refused unless it is the boss's current victim, `holding_boss`; one row per bot and reason per 5 s); info2 `target=<entry>:<guid low>,spell=<spell>` | ai::TauntRefusedByGate (`ai/playerbot/strategy/actions/BossTacticsActions.cpp`) | yes: `reason=` | keep |
| 44a | `BossTacticsHex` | `holder=<main\|off\|dps> ot_threat=<n> mt_threat=<n> top_dps=<name>:<threat>` (`holder` = the member who wore the Hex, found by aura among the group once per second per boss; `ot_threat` = the wearer's when an off-tank wore it, else the first off-tank's; `-1` = the raid's tank order has no such tank; top_dps the highest non-tank, non-healer) | LogHexSnapshot (`ai/playerbot/strategy/values/ThreatValues.cpp`) | not a refusal (the state right after Jin'do's Hex 17172; one row per member and Hex, no flag) | document |
| 44b | `BossTacticsShadeCover` | `reason=<dead\|charmed\|other_shade\|out_of_range\|threat_gate\|guard\|on_boss\|other_target\|no_target>,role=<dps\|offtank>`; info2 `shade=<entry>:<guid low>,healer=<name>,dist=<yd>,target=<entry>:<guid low>\|none` (a Shade of Jin'do 14986 on a healer of the group, and a cursed damage dealer or a cursed off-tank that does not hold the boss that is not on it; `threat_gate` = an off-tank kept on the boss by the `threat_successor` rule of the carrier strike (never while `AiPlayerbot.DungeonRunShadeHealerCover` and `AiPlayerbot.DungeonRunCarrierStrike` are both on), `guard` = with both keys on, an off-tank the carrier strike's guards hold back (the main tank under 60 % or its own health under `AiPlayerbot.LowHealth`), `out_of_range` = more than 30 yd from the Shade or more than 6 yd above or below it, `dead` only for a member that was already missing the Shade when it died, since the curse drops at death, and once per Shade; run by a Delusions carrier over the whole group and logged under the member it names, after a 2 s miss and at most once per member and Shade per 5 s) | ai::LogShadeCover | yes: `reason=` (no flag) | document |

### The pulled Son's meet (event `BossTacticsSonMeet`)

One row per event per meet (the wait row is throttled to one per receiver and 5 s), from `UpdateSeekWalkout` and the
receiver's tank pick when `DungeonRunSeekSonMeet` and a `seek_pull` row are live.

| # | Row | Row text / info1 shape | Emit | Reason today | This branch |
| ---: | :--- | :--- | :--- | :--- | :--- |
| 43a | meet | `son=%u,event=meet,receiver=<name>,tank_idx=%d,receiver_yd=%.1f,due_in=%u,receiver_point_yd=%.1f` | UpdateSeekWalkout | not a refusal (a receiver was picked; `receiver_point_yd` is its distance to its pre-place point, `-1.0` when it is not the pre-placed tank) | document |
| 43b | no meet | `son=%u,event=no_meet,reason=<code>:<n>\|...,tanks=%d` | UpdateSeekWalkout | no (no tank could wait, and why) | added |
| 43c | taken | `son=%u,event=taken,receiver=<name>,by=<name>,after_ms=%u` | UpdateSeekWalkout | not a refusal (a player tank has the Son) | document |
| 43d | lost | `son=%u,event=lost,reason=<word>,after_ms=%u` (`peeled` adds `,victim=%u,victim_role=<tank\|healer\|dps\|pet>,son_yd=%.1f`) | UpdateSeekWalkout, FindBossSeekTank (`peeled`) | yes: the lost word | added |
| 43e | wait | `son=%u,event=wait,reason=receiver_wait,victim=<name\|none>,son_yd=%.1f,after_ms=%u` | FindBossSeekTank | yes: the receiver took no other Son this tick, it waits for the walker's | added |
| 43f | pre-place | `son=%u,event=preplace,receiver=<name>,receiver_yd=%.1f,point_yd=%.1f` | UpdateSeekWalkout | not a refusal (`DungeonRunSeekMeetPrePlace`: the walk-out started and the third tank walks to the circle edge on its way back; `receiver_yd` to the puller, `point_yd` to that point) | new |
| 43g | pre-place end | `son=%u,event=preplace_end,receiver=<name\|none>,reason=<receiver_dead\|receiver_charmed\|receiver_victim>` | UpdateSeekWalkout | yes: the pre-placed tank can no longer wait, so the pre-place ends and the shot's pick is today's | new |
| 43h | pre-place skip | `son=%u,event=preplace_skip,receiver=<name>,reason=floor` | UpdateSeekWalkout | yes: at the start, the point's floor is off the platform's level, so no pre-place began | new |
| 43i | pre-place arrive | `son=%u,event=preplace_arrive,receiver=<name>,after_ms=%u,point_yd=%.1f` | FindBossSeekTank (SeekMeetPrePlaceTask) | not a refusal (once per pre-place: the tank came within 4 yd of its point; `after_ms` since its walk began, which `DungeonRunSeekMeetPrePlaceLate` delays) | new |
| 43j | shot wait | `son=%u,event=shot_wait,receiver=<name>,receiver_point_yd=%.1f,waited_ms=%u,reason=arrived\|timeout\|siphon\|ended` | BossTacticsSeekTankAction (the walk-out shot) | not a refusal (with `DungeonRunSeekShotWaitsReceiver`, once per walk-out when the shot's wait for the pre-placed receiver ends: within 5 yd of its point, 6 s waited, the siphon 15 s or less away, or the pre-place ended) | new |

The no-meet row's codes come in `MeetRefusalWhy`'s order and count the raid tanks at indices 1 and up (the main tank,
index 0, never waits): `dead` (not alive or in the world; near-unreachable in practice, because `GetRaidTankRoles`
already drops dead tanks), `charmed`, `victim` (the boss's current victim), `busy` (a living Son already has it as its
victim, the fight's whole Son set) and `outside` (it stands outside the tanks' circle). `none` means no tank was
rejected other than the main tank. `taken` logs once per meet, then the meet is cleared.

The lost row's words, read per reason (the meet is cleared at the first one that applies):

| Word | Meaning |
| :--- | :--- |
| `son_dead` | The meet's Son is dead or gone. This includes a Son burned before any tank took it (the forced burn kills it on arrival). |
| `receiver_dead` | The receiver died or left the world. |
| `receiver_charmed` | The receiver is charmed (Cause Insanity 24327 or any charm). The puller walks to the circle edge again. |
| `receiver_victim` | The receiver became Hakkar's victim. The puller walks to the circle edge again. |
| `receiver_busy` | The receiver already holds another Son (and the meet Son is not on a tank yet). The puller walks to the circle edge again. |
| `peeled` | The receiver was waiting and the Son's victim became someone other than the puller, the returner or nobody (a damage dealer or a healer peeled it). The normal take branches handle the Son. The row names who took it: `victim=<guid low>`, `victim_role=<tank\|healer\|dps\|pet>` and `son_yd=<yd from the receiver to the Son>`. |
| `timeout` | 20 s after the shot with no take. |

With `DungeonRunSeekSonMeet` the meet's Son is also held for the damage dealers while the meet is open and no tank has
it (`SeekMeetHoldsSon`, checked first in the hold rule): the 5 s non-tank release, the 20 s cap and the late-engaged
forced burn do not apply to it, and its puller and returner count on neither non-tank clock (the 3 s healer take and the
5 s release). Once a tank has it the meet closes (`taken`) and today's hold rule applies.

The wait row's `receiver_wait` is the receiver's refusal to take another Son, pull or drag while the Son's victim is the
puller, the returner or nobody (`MeetReceiverShouldWait`); the receiver itself runs its normal tank AI, and the puller
follows its live position.

### Cause Insanity (event `BossTacticsCharm`)

One row per charm, from `ReadSeekFight`: a group-wide aura scan at most every 500 ms looks for Cause Insanity 24327,
and the row is built 1500 ms after the charmed player is first seen (Hakkar has turned to his next victim by then,
median 1.05 s after the cast). `charmed=<role>:<class>,victim=<role>:<class>,ot1=<threat>,ot2=<threat>,top=<role>:<class>:<threat>|...`
(up to five threat entries, the charmed player left out; `ot1`/`ot2` are the second and third tanks' threat, or -1
when absent), and info2 is `Cause Insanity on <role>: Hakkar turned to <role> (<class>)`.
**Not a refusal:** it is a measurement of the threat successor's margin for the Cause Insanity work, and it carries no
`reason=`.

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
| 50a | `BotUnstick` | info1 the master's name; info2 `reason=nopath_island secs=<n> master_yd=<n> result=teleported\|refused` | `MovementAction::TryPlaceUnstick` (`DungeonRunPlaceUnstick`) | yes: `reason=` | document |

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
- `tools/test_seek_meet_wiring.py` pins the meet's rows (`BossTacticsSonMeet`'s five events and the no-meet counts), the
  meet's flag, receiver branch, wait/`peeled` close and per-update revalidation, the take row's `reason=`/`meet=`, and
  the Cause Insanity row's 500 ms scan (same world, map and instance only) and 1500 ms wait;
  `tools/test_seek_walkout_policy.cpp` pins `MeetRefusalWhy`, `PickMeetReceiver`, `MeetRefusalCounts`,
  `MeetReceiverShouldWait`, `MeetReceiverLostWhy` and `WalkoutMeetCloseIf` (two readers on one snapshot: one close wins, and
  only the winner logs the `taken`/`lost`/`peeled` row); `tools/test_seek_fight_state_policy.cpp` pins the charm edge
  (the same player charmed twice with a gap logs two rows); `tools/test_seek_aura_policy.cpp` pins all eight
  `SeekTakeReasonWord` inputs and `SeekMeetHoldsSon` (the meet Son on its puller is held even with a 6 s non-tank clock and
  a forced burn; a Son on a tank is released as before; a closed meet or another Son changes nothing);
  `tools/test_seek_walkout_policy.cpp` also pins `WalkoutMeetArrived` (within 8 yd of the receiver, or at the old
  circle-edge point) and `SeekTakeEdgeVerdict` / `SeekTakeEdgeCheck` (a Son at 34 yd and 4 yd from its inside victim
  is taken; a victim at 40 yd, a Son at z 45, a Son at 50 yd, or a Son at 36 yd but 15 yd from its victim is refused, the
  last two as `edge_far`; the boundaries at circle + 8 yd and 10 yd from the victim); `tools/test_seek_meet_wiring.py` pins the `peeled` row's
  `victim=`, `victim_role=` and `son_yd=` fields.
