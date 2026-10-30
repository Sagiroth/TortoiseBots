#pragma once

// The run leader (`.bot run`, docs/play/dungeon-run-leader.md): an opt-in
// driver that walks a bound party through a dungeon plan
// (ai/playerbot/DungeonPlanPolicy.h) by itself, behind
// AiPlayerbot.DungeonSelfClear (default 1).
//
// It gives the party ONLY the remote commander's own orders (`cmd move`,
// `move ... direct`, `pullback`, `rest`, `loot`, `open`, `use`, `ritual`, `gossip`), so every
// commander fix also fixes the run leader and the two cannot drift apart. The
// fights themselves are the bots' own combat AI (with the boss tactics table).
//
// This header is the whole decision: one Run per commander, advanced once a
// second by Tick() with a Facts snapshot that the world glue in
// commands/BotCommands.cpp reads from the party and the plan step's
// surroundings. Tick() answers with at most one order (the text after `.bot
// cmd `); the glue sends it as the commander and reports a refusal back with
// OrderRefused(). No game types here: tools/test_dungeon_run_policy.cpp runs it
// on its own.
//
// Safety rules, carried over from turtle-agent's remote driver (its plan
// stepper is the owner's MIT code; provenance in its PR):
//   - gather before a pull: every living bot within kGatherYd of the lead bot;
//   - the plan's cc units get a `cc <mark>` before a pull (never the pull
//     target), one unit per bot by creature type and spell (kRunCc), its kill_first units are pulled first and focused in a
//     fight (a summoned healing ward too);
//   - walk in the plan's hops; before each hop, pull what stands near it, but
//     never what a later pull or boss step owns (OwnedByLaterStep);
//   - a pack or boss counts as done only when the lead bot stands within
//     kSeeYd of it and no living, hostile unit of it is left;
//   - kWipeDead dead (or all dead) regroups by itself once the fight is over:
//     revive, gather, then walk back over the plan's own move points (at most
//     kAutoRegroupsPerStep times a step, then it pauses);
//   - the owner's rule (2026-10-17): a fallen member (the lead bot too) is
//     waited for until it is alive and back with the party before the next
//     pull, boss, task or move (WaitForFallen), fight first, never mid-event;
//     a lead bot back at the entrance alone walks back while the living
//     party holds where it is (LeadRevivedAway);
//   - a refused gather is sent once more, then the run goes on without it;
//     at a gather's ceiling (and an approach hop's) a far straggler that
//     stands still or stalled (kSummonYd) is summoned once;
//   - a fallen bot alive again far from the lead bot, out of combat, is
//     summoned to it before its corpse run walks the dungeon, once per
//     revive; a failed summon walks back (SummonRevived);
//   - every wait has a ceiling (gather, walk, pull, fight, rest, task, object
//     wait, and kStepIdleCapMs per step); a ceiling pauses the run with the
//     reason, it never loops;
//   - any order from a player to the party pauses the run until `resume`;
//   - a party member coming back with attackers on it (more than kGatherYd,
//     at most kRunBackMaxYd away) keeps running to the party past the mobs
//     while the run waits for it (RunBack; the glue makes it a passive
//     follower of the lead bot); the party stays where it is and fights them
//     once it has arrived. Never during an event.

#include "../ai/playerbot/DungeonPlanPolicy.h"
#include "../ai/playerbot/policy/combat/RangedApproachPolicy.h"
#include "GossipOrderPolicy.h"
#include "RemoteCommanderTasks.h"
#include "RezHoldPolicy.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots {
namespace DungeonRun {

using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

constexpr std::uint32_t kTickMs = 1000;           // the glue ticks every run this often
// The test speed (`.bot run start ... speed=<x>`, owner request 2026-10-25): bot-only test runs only; a clear is 1.0.
constexpr float kTestSpeedMax = 3.0f;
constexpr std::uint32_t kSpeedSettleMs = 2000; // the walking rule holds this long without a break before the rate goes up
constexpr std::uint32_t kWipeDead = 3;            // this many dead bots stop the step for a regroup
constexpr std::uint32_t kWipeWindowMs = 90000; // every bot dead within this long is a wipe, a self-revived lead bot or not (RFC 2026-10-20 run 2b)
// Real players in the bound party (2026-10-23, `.bot commander bind ... inparty`; Facts::players):
constexpr float kPlayerPullYd = 60.0f;              // no pull or boss while a player is farther than this from the lead bot
constexpr std::uint32_t kPlayerGatherWaitMs = 60000; // a regroup waits this long for a living player to come within kGatherYd
constexpr std::uint32_t kPlayerHoldCapMs = 300000;   // a step held (or a regroup waiting) for a player this long pauses
constexpr std::uint32_t kAutoRegroupsPerStep = 2; // regroups the run does by itself per step, then it pauses
constexpr std::uint32_t kGatherTries = 2;         // a gather refused this often: the run goes on without it
constexpr std::uint32_t kWalkBackRetries = 3;     // a walk-back point refused for a passing reason is sent again this often
constexpr float kArriveYd = 3.0f;                 // a walk is done this close to its point
constexpr float kStalledArriveYd = 6.0f;          // ... or this close once it stopped gaining ground (a point
                                                  // just off the mesh or on a ledge the bot cannot reach)
constexpr float kStandZTolYd = 4.5f;              // a stand this far above or below the lead bot or less is judged on the ground plane
constexpr float kSeeYd = 60.0f;                   // a pack, boss or spot is judged only from this close
constexpr float kWaitClearMaxDz = 10.0f;          // wait_clear: past the step's own wait, a remaining hostile this far above or below the spot counts as another floor (WaitClearOffFloorOnly)
// A lead bot revived in another part of the instance (LeadInOtherPart, Scarlet
// Monastery r20-19z39 slot 7): raised at a door's destination ...
constexpr float kDoorSpotYd = 10.0f;              // ... this close to it
constexpr float kOtherPartYd = 300.0f;            // ... and farther than this from every point of the plan (the
                                                  // Uldaman and Gnomeregan back doors lie 50-60 yd from their
                                                  // plans' points, the Scarlet Monastery wings' doors 1300+ yd
                                                  // from the Library's)
constexpr std::uint32_t kOtherPartRescuesPerStep = 3; // ... rescue teleports for it a step (a resume gives fresh ones)
constexpr float kApproachYd = 30.0f;              // an unseen pack is approached to this far from its centre
constexpr float kApproachHopYd = 40.0f;           // ... in navmesh path hops of at most this length
constexpr std::uint32_t kMaxApproachHops = 8;     // ... and at most this many without getting nearer, then pause
constexpr float kHopClearYd = 20.0f;              // an approach hop in sight of the pack is finished unless a unit is this close
constexpr float kApproachProgressYd = 5.0f;       // a hop that ends this much nearer than the nearest so far is progress
constexpr std::uint32_t kMaxWalkBackHops = 40;    // a walk back straight to its last point: hops before the replay
constexpr float kRouteSpotYd = 10.0f;             // a lead bot this close to where it died (or stood at a pause) is
                                                  // still on its route checkpoint's stretch (Run::routeKnown)
constexpr float kRouteTrackStepYd = 30.0f; // a paused fight's lead bot moved at most this far since the last tick (a Charge is 25 yd)
constexpr float kRouteTrackDzYd = 6.0f;    // ... and at most this far up or down (a drop into a navmesh hole is more)
constexpr float kDetourMaxYd = 15.0f;             // an unreachable move point may be stood in for by a
                                                  // reachable one at most this far from it ...
constexpr float kMoveFallbackYd = 25.0f;          // ... a plain move's point, this far (r22-19z40 Razorfen Kraul step 85)
constexpr float kApproachStandYd = 25.0f;         // a pack the approach hops did not reach: a stand this near it (ApproachStand) ...
constexpr float kApproachStandMinYd = 15.0f;      // ... and at least this far from its point (a ring, never the pack's centre) ...
constexpr float kApproachStandPullMarginYd = 5.0f; // ... and the pull radius plus this from every unit of the target
constexpr float kApproachStandHostileYd = 20.0f;  // ... with no other hostile this close to it
constexpr std::size_t kUnreachableSkipsInARow = 3; // moves skipped for want of a path in a row, then pause
constexpr float kDetourCloseMaxDz = 4.0f;         // a stand-in within kArriveYd of the point: at most this far above or below it
constexpr float kDetourMaxPathYd = 40.0f;         // ... and the lead bot's navmesh path to it at most this much longer than the straight line
constexpr float kDetourEndYd = 5.0f;              // ... whose navmesh path ends this close to it (DetourPointReachable)
constexpr float kGatherYd = 20.0f;                // every living bot this close to the lead before a pull
constexpr float kHopPullYd = 15.0f;               // units this close to the next hop are pulled first
constexpr std::uint32_t kGatherCapMs = 30000;
constexpr std::uint32_t kGatherComingMaxMs = 90000; // a straggler that keeps coming is waited for this long in all
constexpr float kSummonYd = 40.0f;                // at a gather's ceiling a bot this far that ...
constexpr float kStragglerDzYd = 8.0f;            // ... or this far up or down (another floor) ...
constexpr float kStragglerGainYd = 5.0f;          // ... came less than this closer since the last sample is summoned
constexpr float kStillYd = 2.0f;                   // a bot that moved less than this ...
constexpr std::uint32_t kStillMs = 6000;          // ... in this long stands still (stuck: summoned at a ceiling)
// The test speed's leash (lead decision 2026-10-25, SCHOLO-REFAST-AUDIT 1.5): a fast walk holds once a living bot is
// farther than this from the lead bot, and walks on at kGatherYd or after kGatherCapMs (SpeedLeashTick).
constexpr float kSpeedLeashYd = kGatherYd + kStragglerGainYd; // 25 yd
constexpr std::uint32_t kStuckSummonMs = 15000;   // in a regroup's gather: a far bot still this long is summoned at once
// The straggler ladder (StragglerLadderTick; Shadowfang Keep r25 slot 5 step 39 and r26 slot 7 step 46): a living
// bot out of combat that stays out of the gather (kGatherYd, or kStragglerDzYd up or down) while the run waits for
// the party gets the run leader's own pathed walk to the lead bot after kStragglerWalkMs (`walk <bot> x y z`: it
// holds there), and the summon to the lead bot kStragglerSummonMs after that walk; a gather's ceiling pauses only
// once both went out and the summon had kStragglerSettleMs to bring it in.
constexpr std::uint32_t kStragglerWalkMs = 45000;
constexpr std::uint32_t kStragglerSummonMs = 45000;
constexpr std::uint32_t kStragglerSettleMs = 10000;
constexpr std::uint32_t kSummonWaitMs = 6000;     // a direct step's summoned follower is waited for this long (the summon takes 3 s)
// A direct step's straggler (refused too-far, or not with the lead bot at its gather's ceiling) is summoned again
// once more than kDirectResummonMs passed since its last summon of the step, or once it died and came back since
// then (DirectSummonAllowed), at most kDirectSummonCap summons a bot and step. The refusal pauses only after
// kDirectPauseSummons summons of it and kDirectPauseMs since the step's first refusal (DirectPauseDue); until then
// the run waits for it (the guard keeps going) and sends the step again no sooner than kDirectRetryMs.
constexpr std::uint32_t kDirectResummonMs = 30000;
constexpr std::uint32_t kDirectSummonCap = 3;
constexpr std::uint32_t kDirectPauseSummons = 2;
constexpr std::uint32_t kDirectPauseMs = 60000;
constexpr std::uint32_t kDirectRetryMs = 10000;
// A glue that reads the direct-step test (Facts::directJoinSupported) did not read it on the tick a direct order
// would go out (the step or the walk back's point changed within the tick before): the order waits a tick for the
// read, at most this many ticks in a row (a guard; then the ground rule alone, as for a glue without the test).
constexpr std::uint32_t kDirectJoinHolds = 10;
constexpr std::uint32_t kWalkRetryMs = 12000;     // no progress for this long: send the move again
constexpr std::uint32_t kShortStallMs = 3000;     // a plain walk within kStalledArriveYd that gains nothing this long arrived
constexpr std::uint32_t kWalkTries = 3;           // ... at most this often, then pause
constexpr float kTooFarLegYd = 120.0f;            // a move refused too-far (ReactDistance) goes out in legs this long ...
constexpr std::uint32_t kMaxTooFarLegs = 16;      // ... at most this many per walk (Windhorn Canyon step 1179: the way
                                                  // round the terrace took more than 6), then the refusal pauses as before
constexpr float kLegProgressYd = 5.0f;            // a leg stalled after this much way toward it is done (else: a stall)
constexpr float kLegRepeatYd = 3.0f;              // a next leg ending this close to any earlier leg's end is not sent (the pause)
constexpr float kWalkProgressYd = 2.0f;
// A walk whose lead bot moves (kWalkProgressYd from its spot at the last progress) without getting nearer to the point
// still makes progress, for at most this long in all of one walk (Gnomeregan r27 slot 4 steps 583 and 610: three
// pauses "the lead bot is stuck 82.8 / 87.5 / 104.9 yd from -760 552 -290.7 (... path type 0x1; its own move not
// failing)" while the lead bot walked up the long spiral ramp, its straight distance growing; each resume went on).
constexpr std::uint32_t kWalkMovingCapMs = 120000;
constexpr std::uint32_t kRestWalkResendMs = 3000; // the first walk after a rest not started this long: sent again, once (Run::restWalkDue)
// An exact waypoint (ai::kPlanFlagExact on a move row: the point a direct step
// after it is launched from; ExactWalkTo). The Wailing Caverns hop to Lord
// Serpentis works only from its foot (tests/wc-hop-probe-2, 2026-10-18): a
// direct step sent from a few yards short is refused no-los or lands short of
// the gap, over a 47 yd drop. Same numbers as turtle-agent's remote driver
// (internal/dungeon/remote.go tightenTo: directArrive, tightenStillYd,
// tightenProofHold, tightenBack).
constexpr float kExactArriveYd = 1.5f;            // the lead bot stands this close to the point ...
constexpr float kExactStillYd = 0.5f;             // ... and moved less than this from one spot ...
constexpr std::uint32_t kExactProofMs = 2000;     // ... for this long (2 s of looks, the lead not moving): proved
constexpr float kExactBackYd = 6.5f;              // a `cmd move` shorter than ~6 yd on the ground plane is not
                                                  // carried out (turtle-agent MinMove, ZF-1 run 4): a lead bot
                                                  // nearer than this is walked back along the plan's own points first
constexpr std::uint32_t kExactCapMs = kWalkRetryMs * kWalkTries; // the tighten's ceiling out of combat (36 s): then pause
constexpr std::uint32_t kPullNoFightMs = 25000;   // a pull that started no fight in this time failed
constexpr std::uint32_t kPullTriesPerUnit = 2;    // a unit two pulls could not start a fight with is passed
constexpr std::uint32_t kMaxPullsPerTarget = 3;  // a unit three fought pulls did not kill is out of reach
constexpr std::uint32_t kFruitlessPulls = 3;      // three pulls in a row that kill nothing: the rest of the pack is out of reach
// The pull stand (AiPlayerbot.DungeonRunPullStand, HoldForShot): `pullback`
// goes out only when the tank can shoot from where it stands, else it walks to
// a firing stand first. The margin is the module's own ReachPullAction margin
// (the walk of a ranged pull stops this much inside the shot's range).
constexpr float kShotMarginYd = 5.0f;
constexpr std::uint32_t kShotStandTries = 2;      // firing stands walked to per unit and step, then the order as before
constexpr std::uint32_t kShotStillWaitMs = 3000;  // a moving tank is waited for this long before the stand rule judges it
// A stand walk that gains nothing for kWalkRetryMs, or takes longer than this
// in all, is given up: the order goes out from where the tank is (never a pause).
constexpr std::uint32_t kShotWalkCapMs = kWalkRetryMs * kWalkTries;
constexpr std::uint32_t kMaxPullsPerStep = 20;
constexpr std::uint32_t kMaxWalkBackPulls = 8;    // pulls on the way of a walk back (their own count), then pause
// A lead bot on a navmesh island (IslandEscape). Zul'Farrak r3-19z20: the tank
// chased a Shadowhunter onto a ~3 x 3 yd patch of mesh at 1589 1046 that no
// path leaves; every walk-back point was refused or a zero-length hop, and the
// lead bot stood there 13 minutes, through a resume.
constexpr float kIslandSizeYd = 3.0f;             // that island's size (offline navmesh check, PLAN-ZF-ZUMRAH-WALKBACK B5)
constexpr float kGoodPathMinYd = 10.0f;           // a partial path that ends this close to the lead bot (short of its
                                                  // point) may only cross the island: from 1589.6 1046.9 the path to
                                                  // step 65 ended 3.05 yd away, on it. A good path is complete or ends farther.
static_assert(kGoodPathMinYd > kIslandSizeYd + kArriveYd, "a path that only crosses the island is no good path");
// A stalled walk whose lead bot's own move failed (StallFallback) sends `cmd move ... direct`
// only within the direct step's own limits (RemoteCommander::kDirectMaxDistance and
// kDirectMaxVertical; BotCommands.cpp static_asserts both).
constexpr float kStallDirectMaxYd = 25.0f;
constexpr float kStallDirectMaxVertical = 8.0f;
// ... and only for the diagnosed case: the lead bot in water and the straight line a
// swim line (every 2 yd sample in water, in sight: WorldPosition::IsSwimLine) with no
// living hostile creature within this far of it (Codex review 2026-10-23: the server's
// direct step checks the end's floor and sight only, not the line).
constexpr float kStallDirectHostileYd = 15.0f;
constexpr float kEscapeMaxYd = 10.0f;             // the straight hop off it goes to the last good spot this close ...
constexpr float kEscapeMaxVertical = 8.0f;        // ... and this far up or down (RemoteCommander::kDirectMaxVertical)
constexpr std::uint32_t kIslandEscapes = 2;       // hops off one island, then pause
constexpr std::uint32_t kStrandSettleMs = 2000;   // the stranded lead bot's teleport (StrandedTeleportTick) settles this long
constexpr std::uint32_t kStrandTries = 3;         // ... failed teleports (refused, or the lead bot not there after it) a step
constexpr std::uint32_t kStrandWaits = 10;        // ... "wait" answers a step, then a pause
constexpr std::uint32_t kStrandSummonWaitMs = 120000; // ... a bot left behind in a fight is waited for this long, then a pause
constexpr std::uint32_t kEscapeSettleMs = 2000;   // the hop is over once the lead bot stands still this long ...
constexpr std::uint32_t kEscapeCapMs = 6000;      // ... or after this long
constexpr float kEscapeMovedYd = 2.0f;            // the glue refuses the hop when the lead bot moved this far since the read
// The straight line of the hop, checked before the lead bot is moved (CheckEscapeSegment):
constexpr float kSegmentStepYd = 2.0f;            // sampled every this far ...
constexpr std::size_t kSegmentSamples = 8;        // ... at most this many samples (the end point is the last)
constexpr float kSegmentGroundYd = 4.0f;          // a floor within this far up or down of the line at each sample
constexpr float kSegmentRiseYd = 2.0f;            // at most this height step from one sample to the next (up; down
                                                  // too, but for a ring spot whose path leads on: ...)
constexpr float kSegmentDropYd = kSegmentGroundYd; // ... at most this step down (Uldaman r16-19z34: a ramp's tip 2.1-2.5 yd
                                                  // above the floor it is not linked to)
constexpr float kEscapeHostileYd = 10.0f;         // no living hostile this close to the hop's end
// One partial path toward one point does not prove an island (a ledge, a closed
// door, a point off the mesh): IslandConfirmed wants a second sign as well.
constexpr float kIslandCellYd = 4.0f;             // the evidence is kept for the lead bot's cell of this size ...
constexpr float kEvidenceSpotYd = 2.0f;           // ... a second reading counts from this close to the first one's spot ...
constexpr float kEvidenceTargetYd = 10.0f;        // ... toward a point at least this far from the first one's point
constexpr std::uint64_t kGoodSpotFreshMs = 120000; // ... and, for the forward move check, read this recently (GoodSpotFresh)
constexpr float kGoodSpotProbeYd = 40.0f;         // the last good spot this close is probed (the glue: Facts::goodSpotCut)
constexpr std::uint32_t kWalkBackResends = 6;     // a walk-back point sent this often from one spot (kArriveYd) is passed
constexpr std::uint32_t kStepEscapes = 4;         // hops off an island one step may send in all (a good read in
                                                  // between gives kIslandEscapes again; the step ceiling is 300 s)
constexpr std::uint32_t kRegroupReviveMs = 240000; // after a resume: the dead must be up within this
constexpr std::uint32_t kRegroupGatherMs = 60000;  // ... and the party gathered within this after that
constexpr std::uint32_t kFightCapMs = 180000;
constexpr std::uint32_t kBossFightCapMs = 420000;
// The two ceilings above were progress-aware from Dragonmaw Retreat r25 slot 7 (2026-10-23) on: step 260 paused
// four times on "a fight lasted more than 180 s" while the boss Searistrasz (62072) lost hp steadily from 100 % to
// 7 % over about 20 min with the party fine. Past a ceiling the run pauses only when no engaged unit lost this many
// tenths of a percent of its hp in the last window (FightProgressTick), or a party member died in it; a fight that
// keeps losing hp pauses at the hard cap anyway. Event fights keep their own ceiling unchanged.
constexpr std::uint32_t kFightProgressWindowMs = 60000;
constexpr std::uint32_t kFightProgressPermille = 20; // 2 percentage points
constexpr std::uint32_t kFightHardCapMs = 1800000;
constexpr std::uint32_t kCeilingQuietMs = 5000; // a ceiling pause whose party stays out of combat this long goes on by itself
constexpr std::uint32_t kLeadBackSettleMs = 5000; // a lead bot back on the plan's map this long resumes its off-map pause
constexpr std::uint32_t kPlayerHoldBackSettleMs = 5000; // a player back with the party this long resumes his hold pause
constexpr std::uint32_t kStaleCombatGraceMs = 20000; // a combat flag with nothing hostile engaged is waited out this long in all of one fight, then the fight ceiling runs again
constexpr std::uint32_t kStaleConfirmMs = 3000;      // ... and it must stand this long without a break before a wait_object is finished over it
constexpr std::uint32_t kRestCapMs = 60000;
constexpr std::uint32_t kRestRepeatMs = 20000;
constexpr std::uint32_t kPartyRezWaitMs = 15000;  // a regroup with a living rezzer (priest, paladin, shaman) waits this
                                                  // long for its resurrection before `revive party` releases the dead
constexpr std::uint32_t kLootFfaWaitMs = 20000;   // a loot step's free-for-all detour waits this long for a busy carrier
constexpr std::uint32_t kLootRestoreTries = 3;    // ... and orders the loot method back this many times (one a tick)
constexpr std::uint32_t kTaskResends = 2;         // loot, open, use: an interrupted or in-combat task is sent again this often
constexpr std::uint32_t kTaskCapMs = 90000;       // loot, open, use: the commander task's own budget is shorter
constexpr std::uint32_t kUseOnResends = 1;        // use_on: an answer that says the cast did not happen is sent again once
constexpr std::uint32_t kUseOnProofMs = 30000;    // use_on: after the ACK, the object's change or its boss must show this soon
constexpr std::uint32_t kOpenProofWaitMs = 5000; // open_proof: after the task answered, its proof must show this soon
constexpr float kUseOnSummonYd = 150.0f;          // use_on: how far around the lead bot the glue looks for the summoned boss
constexpr std::uint32_t kStepIdleCapMs = 300000;  // out-of-combat time one step may take
constexpr std::uint32_t kBossSearchMs = 60000;    // a boss not at his spot (a patrol) is waited for this long, then pause
// ... unless his row says longer: a boss step's `timeout_s` (the plan's `wait_s`) is the wait at his point before
// the instance search and the pause (1-kPlanMaxTimeoutS, 600 s; 0: kBossSearchMs). The rows carry no patrol path,
// so the party can only stand where the patrol passes and wait for it: Wailing Caverns' Kresh walks a 525.9 yd
// round in the river (about 210 s at the walk speed), and the 60 s default missed him 9 runs in 10.
inline std::uint32_t BossAwayWaitMs(const PlanStep& s)
{
    std::uint32_t const secs = s.timeoutS ? std::min(s.timeoutS, ai::kPlanMaxTimeoutS) : kBossSearchMs / 1000u;
    return secs * 1000u;
}
constexpr std::uint32_t kStandPullWaitMs = 600000;
constexpr float kStandPullMarginYd = 1.0f;
// A unit a party member has as its victim that stays at 1 hp (or at 1 % or less, unchanged) this long cannot die
// now (HpHeldTick; Scarlet Cathedral r23 slot 5: Whitemane sat at 1 hp of 5284 under her script's invincibility,
// and the fight ceiling paused four times on the same unkillable fight).
constexpr std::uint32_t kHpHeldMs = 60000;
// A pull step's patroller (WaitForPatroller; Uldaman r10-19z29 step 66): the
// Stone Steward 4860 walks a lane whose north end only is in the step's 10 yd
// radius, so the step said "pack cleared" in 0 s in r8, r9 and r10, and where
// the patrol was decided whether it came alone or into the Keeper's fight.
constexpr std::uint32_t kPatrolWaitMs = 60000;    // ... waited for this long at most (a shorter timeout_s of the step caps it)
constexpr float kPatrolSeeYd = 30.0f;             // the glue reads patrollers of the step's entries this far outside its radius
// A big pack's firing stand walk (BigPackWalkGate; Uldaman r10-19z29 step 79):
// the lead walked to the stand of a 9-joiner scorpid pack with the healer and
// the mage 27-30 yd behind on the ramp, and the pack woke on a dps first.
constexpr std::uint32_t kBigPackJoiners = 8;      // a pull with this many joiners expected or more is a big pack
// Scripted events (ritual, hold; docs/play/dungeon-run-leader.md "Events").
constexpr float kHoldReturnYd = 8.0f;             // out of a fight, a lead bot farther than this from the hold spot walks back
// ZF r12-19z29: the engage order sent the tank alone down the stairs while the four ranged stood on their
// places 36-60 yd off, and the freed crew died to the trolls with only the tank beside them. The Uldaman
// Keepers and Archaedas holds send the tank 15-20 yd after a keeper while the ranged keep their places
// (that is what the places are for), so the tank's distance from the spot alone never releases them: a
// living ranged bot (the healer too) must be this far from the living tank, out of heal and cast range,
// on the ground (the 3D distance less the height), for kHoldFollowTicks fight ticks in a row
// (ReleaseHoldStands).
constexpr float kHoldFollowRangedYd = 30.0f;
constexpr std::uint32_t kHoldFollowTicks = 2;
constexpr std::uint32_t kHoldIdlePullMs = 10000;  // an awake event unit that has not come for this long is pulled
constexpr std::uint32_t kEventStartMs = 30000;    // an encounter hold whose data does not leave "not started" in this time failed
// A hold done by its units (no done flag) whose units were all dead before it began (no unit seen alive in the radius, no
// awake one, corpses in sight): it ends this long after the first look at the spot, on a tick with the spot in sight (HoldStep; ZF r15-19z33: Bly's crew
// killed by the troll waves, "4 of 4 down" held the whole 300 s).
constexpr std::uint32_t kHoldAllDeadMs = 10000;
constexpr float kGossipWalkYd = 30.0f;            // a gossip step walks to its point first when the lead bot is farther than this
constexpr std::uint32_t kRitualNoReplyMs = 5000;  // a ritual whose bots are free again with no answer this long: pause
constexpr std::uint32_t kEventResetWaitMs = 60000; // before a ritual: an encounter still in progress or failed (a boss
                                                   // walking home) is waited for this long, then the ritual goes out
// Placing the party on a hold spot (HoldStands). Uldaman run 3 (19z12): the
// priest and the mage stood 12-20 yd from the tank on the Keepers' spot and a
// keeper killed them there while the tank held "stay". The ranged bots (healer
// too) stand behind the tank, on the side away from the event's mobs, in a
// row across that line; the others follow the lead bot as before.
constexpr float kHoldBehindYd = 6.0f;             // the ranged row this far behind the hold spot ...
constexpr float kHoldSpreadYd = 3.0f;             // ... its bots this far apart across the line ...
constexpr float kHoldGatherYd = 10.0f;            // ... and none farther than this from the spot (nor out of the radius)
constexpr float kHoldMobSideYd = 5.0f;            // the event's mobs' middle this far off the spot gives "away"
                                                  // (the four Stone Keepers ring the spot: 3.4 yd, no side)
// Guarding the party from a stand point (GuardTarget): the run leader holds the
// tank on "stay" at its pullback or walk-back point, where it does not go after
// a mob on someone else. Uldaman rerun 2026-10-17, keepers door pull 100 and the
// walk back at -10 337: adds killed the healer and the DPS 13-37 yd away while
// the tank stood 50 s at 53 % with nothing on it.
constexpr float kGuardYd = 40.0f;                 // party bots this close to the tank, hit by a mob, are guarded
constexpr std::uint32_t kGuardWaitMs = 4000;      // ... once mobs were hitting them (in melee reach, or standing and
                                                  // casting: not while a pullback's mobs still run in) for this long
constexpr std::uint32_t kGuardHealerWaitMs = 0;   // ... a mob hitting a healer at once: at pull 100 two Maulers
                                                  // took the healer from 73 % to dead in 4 s
constexpr std::uint32_t kGuardRepeatMs = 2000;    // a further pick-up no sooner than this after the last one
constexpr std::uint32_t kGuardOrdersPerFight = 3; // pick-up orders one fight may send (per budget: see below)
// A mob hitting the party only by a spell from where it stands (UnitFact::castingAtRange), with a living hostile out of
// combat within kGuardCampYd of it (UnitFact::campIdle: its own camp, a pack not pulled yet), is not picked up or engaged
// when the tank would walk more than kGuardNoWalkYd to it (GuardLeavesCampAlone). Scholomance r30 slot C step 16
// (t 398.8-402.8): the tank walked 14 yd after a Neophyte casting at the priest from its spawn, the priest died behind it
// and four mobs of the next pack were on it. 15 yd is inside the 17-20 yd a level-57-60 elite aggroes a level-60 bot at;
// 8 yd is a step past melee reach.
constexpr float kGuardCampYd = 15.0f;
constexpr float kGuardNoWalkYd = 8.0f;
// The budget above starts over when a unit that was not in the fight joins it
// once the budget is spent (a summon lands, a mob crosses into the party).
// Deadmines r12-19z29 steps 90, 118 and 125: the three pick-ups went in the
// first seconds, then Remote-Controlled Golem summons, the Overseer, the
// Harvester and Gilnid joined and beat the healer 25-60 s while the tank sent
// nothing (13 of the run's 14 deaths). Bounded so that it cannot spam: once a
// refill happened, one unit gets at most kGuardOrdersPerUnit orders a fight
// (pick-ups and engages together, those before the refill counted; a unit at
// the cap is passed over for the next one; before any refill one unit may take
// the whole budget), and a fight at most kGuardOrdersFightCap in all.
// kGuardRepeatMs spaces every one of them.
constexpr std::uint32_t kGuardOrdersPerUnit = 2;
constexpr std::uint32_t kGuardOrdersFightCap = 12;
// The kill_first focus (PickKillFirst) sends every bot, the tank too, at its
// unit. Zul'Farrak r5-19z23 step 27: the camp's witch doctors aggroed on the
// tank from 20 yd (wipe 1: the focus took it into the camp, next to the north
// group, 13 mobs at once) and from 43 yd (wipe 2: a pet had pulled them). 15 yd
// is the stand's melee and close-caster reach, well inside the 20 yd at which
// the camp woke; a unit farther off is left to come to the tank (r3 lived that way).
constexpr float kKillFirstMaxYd = 15.0f;
// A kill_first focus every bot refused (OrderRefused: "No bot of the group
// accepted the focus order"; AttackAction refuses an evading target) leaves
// its unit out of the pick for this long, and the focus goes to the next one
// or to none. Deadmines r12-19z29 step 118, t 2210.9: the focused 622 3569 had
// turned to flee, all three living bots refused, and the focus stayed on it
// with no second order while it lived. After the wait it may be focused again.
constexpr std::uint32_t kFocusRefusedIgnoreMs = 8000;
// The healer's stand for a pull (the run leader's commander context): this far
// behind the lead bot's spot, on the line from the pull target through that spot
// (Uldaman run 4: the priest stood level with or ahead of the tank and died of
// the camp's first hits). Within heal range of the tank (40 yd), clear of the
// pack's first swings. The glue checks the point on the ground and the navmesh
// and falls back to half the distance, then to the spot itself.
constexpr float kHealerBackYd = 9.0f;
inline bool HealerBackPoint(float leadX, float leadY, float targetX, float targetY, float back, float& x, float& y)
{
    float const dx = leadX - targetX, dy = leadY - targetY;
    float const len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f)
        return false; // standing on the target: no side is behind
    x = leadX + dx / len * back;
    y = leadY + dy / len * back;
    return true;
}

// After the fight the tank walks back to the spot it stood on at the first
// pick-up, not to the run's anchor (the last walk's end, the leg's start
// point when the leg's pulls have no move step): Uldaman run 3 (19z12,
// tests/uldaman-events-3) walked the tank 90 yd back to the baelog entrance
// after every guarded pull. Farther than this from that spot once the fight
// is over, it stays where the fight left it.
constexpr float kGuardReturnMaxYd = kGuardYd;
// A party member coming back with attackers runs on to the party (RunBack; the
// owner's rule of 2026-10-17). Uldaman 2026-10-17 (tests/uldaman-events-2):
// the revived priest and mage stopped to fight a scorpid at -174 311, 73-76 yd
// from the tank, and the regroup gave up with a bot 107.9 yd away; the rogues
// at the tank joined in by themselves once the mage came within 50 yd (the
// bots' own group help reaches SightDistance, 60 yd).
constexpr float kRunBackMaxYd = 120.0f;           // a member with attackers this far from the lead bot runs back
constexpr float kRunBackArriveYd = 12.0f;         // ... until it is this close: then it fights with the party
constexpr std::uint32_t kRunBackCapMs = 90000;    // one run back ends after this long
// A fallen bot revived far from the lead bot is summoned to it (SummonRevived)
// instead of walking the dungeon back. Uldaman run 3 (19z12, 10:45-10:54): the
// priest and the mage revived at the entrance (-229 46), ran about 270 yd back,
// died twice to Shrike Bats at -52..-80 359..375 (4 extra deaths), and the
// regroup paused at its 60 s gather ceiling with a bot 273.7 yd out; a `cmd
// summon` sent by hand right after each was alive, before the bats, had both at
// the altar within 20 s, while one sent in the bats' fight did nothing.
constexpr std::uint32_t kReviveSummonWaitMs = 10000; // a revived bot's summon (3 s) not at the lead bot after this
                                                     // long failed: it walks back
constexpr float kRegroupWalkYdPerS = 3.0f;        // the regroup's gather ceiling allows this pace (fights on the way)
constexpr std::uint32_t kRegroupGatherMaxMs = 180000; // ... from the farthest walker, up to this long

// Rest gate: it starts when someone is below a "start" line after a fight or
// before a step, and ends when everybody is at the "end" line (or at kRestCapMs).
struct RestLines
{
    std::uint32_t startHealth = 80, startHealerMana = 50, startMana = 35;
    std::uint32_t endHealth = 90, endHealerMana = 80, endMana = 60;
};

enum class State : std::uint8_t { Running, Paused, Done };
enum class Phase : std::uint8_t { Begin, Gather, Walk, AwaitPull, AwaitTask, Rest, Wait, Hold };

// A loot step's free-for-all detour (AiPlayerbot.DungeonRunLootFfa, TaskStep): Idle; Ordered: `lootmethod ffa`
// went out; Looting: the loot order is out under free-for-all; Restoring: the saved method was ordered back.
enum class LootFfa : std::uint8_t { Idle, Ordered, Looting, Restoring };

inline const char* StateName(State s)
{
    switch (s)
    {
        case State::Running: return "running";
        case State::Paused: return "paused";
        default: return "done";
    }
}

inline const char* PhaseName(Phase p)
{
    switch (p)
    {
        case Phase::Begin: return "begin";
        case Phase::Gather: return "gather";
        case Phase::Walk: return "walk";
        case Phase::AwaitPull: return "pull";
        case Phase::AwaitTask: return "task";
        case Phase::Rest: return "rest";
        case Phase::Hold: return "hold";
        default: return "wait";
    }
}

// A creature near the step, as the glue saw it this tick.
struct UnitFact
{
    std::uint64_t guid = 0;
    std::uint32_t entry = 0;
    float distLead = 0.0f;
    float x = 0.0f, y = 0.0f; // hold units and patrollers: where it stands (HoldStands: the side the event comes from)
    float z = 0.0f;           // ... patrollers (OwnedByLaterStep); step units: the unit's real height (the passed-boss pause)
    bool hostile = true;     // not friendly to the party (hostile or neutral): it may be pulled
    bool stealthed = false;
    bool evading = false;    // running home: not pulled this tick
    // A run crowd-control aura is on it: Polymorph, Shackle Undead, Banish or
    // Sap (kRunCc's spells; the name is from when the run had only Polymorph).
    bool polymorphed = false;
    // The core's creature type (kCreatureHumanoid ...): which run cc fits it
    // (CcFits). 0: not read, Polymorph only (the facts before #1159).
    std::uint32_t creatureType = 0;
    // CcBit of each run cc family it is immune to (its creature_template
    // mechanic_immune_mask or a live mechanic immunity against that spell's
    // mechanic): never picked for that spell.
    std::uint32_t ccImmune = 0;
    // Kill-first candidates (Facts::killFirst): in combat with a party member,
    // or summoned / owned (a healing ward) by a unit that is.
    bool engaged = false;
    std::uint64_t owner = 0;
    bool ownerEngaged = false;
    // A kill_first unit keeping an aura (a channelled heal) on a unit engaged
    // with the party (Hailar's Ritualists, r19-19z37): this fight's, though it
    // is in no combat itself.
    bool auraOnEngaged = false;
    bool victimHealer = false; // its victim is a healer (Facts::partyAttackers, and a hold's units)
    bool victimLead = false;   // a hold's units: its victim is the lead bot (the tank holds it)
    // Facts::partyAttackers only:
    bool hitting = false;      // it hits its victim now: in melee reach of it, or standing and casting (not running in)
    bool victimPet = false;    // its victim is a party bot's pet (the lead bot's own too), not a bot (victimHealer false)
    // ... `hitting` only by a spell cast from where it stands, not in melee reach of its victim (Facts::partyAttackers),
    bool castingAtRange = false;
    // ... and for such a caster farther than kGuardNoWalkYd from the lead bot: how many living hostile creatures out of
    // combat stand within kGuardCampYd of it (a pack not pulled yet). 0: none, or not read.
    std::uint32_t campIdle = 0;
    float distSpot = -1.0f;    // ... during a hold: its distance from the hold spot (-1: not read)
    // Pull step units only, when Facts::joinerCeiling is set: how many other
    // creatures an estimate (ai::EstimateAggro) says come if this one is pulled.
    std::uint32_t joiners = 0;
    bool joinersPatrol = false; // ... and a lone patrol alone puts that over the ceiling
    bool joinersRead = false;   // the count above is this step's (ReadPullJoiners judged the unit; a held
                                // or off-index unit keeps its earlier read)
    // Pull step and hop units, when Facts::pullStand is set: the lead bot sees
    // it now (Unit::IsWithinLOSInMap).
    bool losLead = false;
    // The group can attack it: the same test `cmd attack` refuses invalid-target on ("The group cannot attack
    // that target.": the judge bot's Unit::IsValidAttackTarget, which covers UNIT_FLAG_NON_ATTACKABLE, the not
    // selectable flag and a friendly unit). Gnomeregan r21-19z40e step 180: the Alarm-a-bomb 2600 (7897) kept
    // the party in combat, was refused three times, and the fight ran past the ceiling four times. A unit the
    // group cannot attack is never pulled, picked up, engaged, focused or crowd-controlled by the run, and a
    // fight with only such units left is no fight (UnattackableOnlyFight). Unread: true.
    bool attackable = true;
    // Facts::engagedUnits only: its hit points now and its most (0: not read), and a party bot or pet has it as
    // its victim. HpHeldTick watches such a unit that stays at 1 hp (or 1 % or less, unchanged).
    std::uint32_t health = 0;
    std::uint32_t maxHealth = 0;
    bool partyHits = false;
};

// The mage's Polymorph (all ranks and forms): the aura the run waits for after
// a `cc moon`, and the spell a mage must know to be the run's crowd control.
constexpr std::uint32_t kPolymorphSpells[] = {118, 12824, 12825, 12826, 28270, 28271, 28272};
// Issue #1159: the other classes' run crowd control (all ranks), known by the
// bot (MemberFact::cc) and read as an aura on the unit (UnitFact::polymorphed).
constexpr std::uint32_t kShackleUndeadSpells[] = {9484, 9485, 10955};
constexpr std::uint32_t kBanishSpells[] = {710, 18647};
constexpr std::uint32_t kSapSpells[] = {6770, 2070, 11297};

// The core's creature types (CreatureType, SharedDefines.h).
constexpr std::uint32_t kCreatureBeast = 1;
constexpr std::uint32_t kCreatureDemon = 3;
constexpr std::uint32_t kCreatureElemental = 4;
constexpr std::uint32_t kCreatureUndead = 6;
constexpr std::uint32_t kCreatureHumanoid = 7;
constexpr std::uint32_t kCreatureCritter = 8;
constexpr std::uint32_t kCreatureMechanical = 9;
constexpr std::uint32_t CreatureBit(std::uint32_t type) { return type ? 1u << type : 0u; }

// The run's crowd-control spells (issue #1159), in the order of their
// reliability: the bot for a unit is the free one whose spell comes first
// here. Fear is not one (a feared mob runs into the next pack), nor Freezing
// Trap (the hunter's "freezing trap on cc" walks to the marked mob and lays
// the trap at its feet in the fight, TrapOnCcTargetAction; there is no trap
// laid at the stand before the pull to model).
enum class CcFamily : std::uint8_t { None = 0, Polymorph, Shackle, Banish, Sap };
constexpr std::uint32_t CcBit(CcFamily family) { return 1u << static_cast<unsigned>(family); }

struct RunCcSpell
{
    CcFamily family;
    const char* spell;   // for the note
    const char* held;    // "<entry> is <held>" once its aura shows
    std::uint32_t types; // CreatureBit of each creature type it works on
    bool prePull;        // only on a unit out of combat, by a bot out of combat, before the pull (Sap)
};
constexpr RunCcSpell kRunCc[] = {
    {CcFamily::Polymorph, "Polymorph", "polymorphed",
        CreatureBit(kCreatureHumanoid) | CreatureBit(kCreatureBeast) | CreatureBit(kCreatureCritter), false},
    {CcFamily::Shackle, "Shackle Undead", "shackled", CreatureBit(kCreatureUndead), false},
    {CcFamily::Banish, "Banish", "banished", CreatureBit(kCreatureDemon) | CreatureBit(kCreatureElemental), false},
    {CcFamily::Sap, "Sap", "sapped", CreatureBit(kCreatureHumanoid), true},
};

inline const RunCcSpell* RunCcOf(CcFamily family)
{
    for (const RunCcSpell& c : kRunCc)
        if (c.family == family)
            return &c;
    return nullptr;
}

// Whether `family` works on a unit of `creatureType`. An unread type (0) is
// Polymorph's only: the run's crowd control before the table.
inline bool CcFits(CcFamily family, std::uint32_t creatureType)
{
    if (!creatureType)
        return family == CcFamily::Polymorph;
    const RunCcSpell* c = RunCcOf(family);
    return c && (c->types & CreatureBit(creatureType));
}

// The raid marks the run's cc orders use, one per unit held at a time (the
// order moves the mark onto its unit and sets the bot's "rti cc" to it, so two
// held units need two marks). Moon first, as before.
constexpr const char* kCcMarks[] = {"moon", "square", "diamond", "triangle"};
// ... their raid icon slots (RtiTargetValue::GetRtiIndex): moon 4, square 5,
// diamond 2, triangle 3. -1 for any other name.
inline int CcMarkIcon(const std::string& mark)
{
    return mark == "moon" ? 4 : mark == "square" ? 5 : mark == "diamond" ? 2 : mark == "triangle" ? 3 : -1;
}
// The raid icon slot of the run's kill_first focus (CommanderFocus puts the skull on its unit; RtiTargetValue::GetRtiIndex).
constexpr int kFocusIcon = 7;
// A warlock is the crowd control of a unit only within Banish's range of the
// lead bot (the party is gathered on it): beyond it the warlock's "fear on
// cc" row (20 yd) is the one that can fire on the mark.
constexpr float kBanishYd = 30.0f;
// A Sap goes out before the pull, which waits this long for its aura (the
// rogue stealths and walks to the mob), then goes without it.
constexpr std::uint32_t kSapWaitMs = 15000;
// In the fight a `cc moon` must show its aura within this much of the mage's
// own fight time (19z5: the mage waits for the tank's threat, walks into range
// and casts 1.5 s; Polymorphs landed 7-8 s into its fight), else once more,
// then noted.
constexpr std::uint32_t kCcLandMs = 8000;
// The aggro estimate's rest waits (Facts::joinerCeiling) a step: after that a pull goes out without one.
constexpr std::uint32_t kMaxJoinerRests = 3;

// A living bot of the party other than the lead bot.
struct MemberFact
{
    std::string name;
    float yd = 0.0f;        // from the lead bot (1000 on another map)
    bool polymorph = false; // a mage that knows Polymorph (kPolymorphSpells): the run's crowd control
    bool inCombat = false;  // the bot or its living pet is in combat (the glue's MemberInCombat)
    float dz = 0.0f;        // height difference to the lead bot (another floor)
    std::uint32_t attackers = 0; // units attacking it or its living pet (their attacker sets, no scan)
    float x = 0.0f, y = 0.0f, z = 0.0f; // where it stands (the stand-still watch: UpdateStill)
    bool ranged = false;    // a ranged bot or a healer: it stands behind the tank in a hold (HoldStands)
    // With Facts::directJoinRead: the glue's own direct-step test for this bot (the one `cmd move ... direct`
    // refuses too-far on: a complete navmesh path of at most kDirectMaxGatherPath yd onto the lead bot's spot,
    // or within kDirectNearLead yd of it on its floor in its sight, or its own line to the point). Unread: true.
    bool withLead = true;
    // The run cc it knows besides `polymorph` (CcBit: a priest's Shackle
    // Undead, a warlock's Banish, a rogue's Sap; issue #1159).
    std::uint32_t cc = 0;
    // A rogue whose pre-pull Sap trigger ("sap cc mark") can fire at all: its
    // master is a live client (PlayerbotAI::HasRealPlayerMaster) and Sap is
    // not switched off ("no sap"). Without it no Sap is ordered.
    bool canSap = false;
    // In combat: the units engaged with this bot or its pet (their attackers, their victims and the units holding
    // either on their threat lists), each once, with
    // UnitFact::attackable (engagedRead). Gnomeregan r23
    // step 181: the Alarm-a-bomb 2600 (7897) took the priest as its victim; the priest and the hunter stood in
    // combat 35 yd from the lead bot for 8 min, follow orders and the run back did not move them, and the gather
    // paused the run four times (CombatDropTick).
    std::vector<UnitFact> engaged{};
    bool engagedRead = false;
    // Dire Maul West r38 slot B (2026-10-24): the bot is not on the plan's map (the glue: its map id is not the
    // plan's; outside the instance). It is no part of the party's fight (PlanMapFightView). Unread: false.
    bool offPlanMap = false;
    // Round 1 (2026-10-24): the bot is in the open world, so a return teleport can reach it (the glue: not in a
    // battleground, its map not an instance/dungeon/battleground, not on a transport, taxi-flying, teleporting,
    // summoned or loading). A bot outside the instance that is not returnable gets a note, never a return or a pause.
    // Unread: true (returnable).
    bool offMapReturnable = true;
    // Round 2 (2026-10-24): why it cannot be returned: the busy recheck (a pending summon or teleport move) keeps its
    // own wording; the rest (battleground, instance map, transport) is the open-world check. Unread: OpenWorld.
    enum class OffMapBlock : std::uint8_t { Busy, OpenWorld };
    OffMapBlock offMapBlock = OffMapBlock::OpenWorld;
};

// An instance door: where an area trigger into the plan's map puts a player
// (the trigger's destination, tw_world_areatrigger_teleport).
struct DoorFact
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Every run cc family `m` knows.
inline std::uint32_t MemberCc(const MemberFact& m)
{
    return m.cc | (m.polymorph ? CcBit(CcFamily::Polymorph) : 0u);
}

// A real player in the bound group (2026-10-23: 1 to 4 players, the rest bots, one of them the bound commander).
// Never a MemberFact: no order of the run (walk, summon, revive, crowd control, loot, gather) names a player. The run
// counts him for the party's deaths and health, waits for him in a regroup (RegroupPlayerWait) and holds its steps for
// him (PlayerHoldNote).
struct PlayerFact
{
    std::string name;
    bool commander = false;      // the bound commander
    bool alive = true;           // not dead (a body or a ghost)
    bool ghost = false;          // ... released: a ghost on its way back
    bool onMap = true;           // in the world on the plan's map (in the instance)
    bool online = true;          // ... logged in (a member who logged out stays in the group's slots: onMap false)
    bool inCombat = false;       // a living player in a fight: the party is in it too (ApplyPlayerFacts)
    bool gm = false;             // a GameMaster account (IsBotAdministrator): the core keeps his hp at 1, he does not die
    float yd = 0.0f;             // from the lead bot (1000 off its map)
    std::uint32_t healthPct = 100;
};

// A hostile's place, as the glue saw it this tick.
struct HostileSpot
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    // In combat with, or targeting, a party member (BotCommands.cpp
    // EngagedWithParty: a party bot is its victim or attacks it): at the
    // timeout such a hostile still pauses the step, whatever its dz.
    bool engaged = false;
};

// What the glue reads once a tick for the run's current step.
struct Facts
{
    // The real players of the bound group (empty: a bot-only party, every rule below as before).
    std::vector<PlayerFact> players;
    // loot: a player carries the step's item (his name), and the remembered corpse is a player's round-robin turn.
    std::string itemPlayer;
    std::string lootTurnPlayer;
    // The run's item needs (spec item-leg-skip C2/C2b): the glue reads the
    // party's bags once a tick for the current leg's needs. legNeedRead false:
    // the read is due (the leg gate notes it and waits a tick).
    bool legNeedRead = false;
    // ... item -> the first party member (bots first: lead, then the rest,
    // then players, dead ones too) whose bags hold it; "" none. Read for the
    // current leg's Optional needs (the skip gate) and its Door needs (the
    // door hold: only a holder that is a real player opens the door).
    std::vector<std::pair<std::uint32_t, std::string>> legNeedHolders;
    // ... the current leg's first door need: its object gameobject found near
    // the lead bot this tick (doorNeedRead; not found is NOT closed: nothing is
    // decided and nothing held on it), its state (doorNeedState: 0 closed/other,
    // 1 open) and its position (the hold fires only near it, DoorNear).
    bool doorNeedRead = false;
    std::uint32_t doorNeedState = 0;
    float doorNeedX = 0.0f, doorNeedY = 0.0f, doorNeedZ = 0.0f;
    // ... the first bot (the lead bot first) carrying that door's item: with
    // DungeonRunBotKeysForPlayers = 1 it opens the door for the party (DoorKeyOpen).
    std::string doorNeedCarrier;
    // ... the best real-player SKILL_LOCKPICKING of the party (0 none): a
    // rogue's Lockpicking that opens the door instead of the key.
    std::uint32_t doorNeedLockpick = 0;
    // AiPlayerbot.DungeonRunBotKeysForPlayers (rule 5): with a real player the
    // run behaves as bot-only (a bot carries the keys and opens the doors).
    bool botKeysForPlayers = false;
    // The party (the commander's bound bots, the lead bot included).
    std::uint32_t total = 0, alive = 0, dead = 0;
    bool leadAlive = true;
    // The instance's doors (every area trigger into the plan's map), read by the glue from the core's trigger table
    // once per map. A living lead bot on one of them far from every point of the plan revived in another part of the
    // instance (LeadInOtherPart). Empty (not read, a policy test that does not set it): no such rule.
    std::vector<DoorFact> doors;
    bool anyInCombat = false;
    bool leadInCombat = false;          // the lead bot itself is in a fight
    // anyInCombat, yet no living hostile is engaged with a bot (or its pet): a
    // combat flag left standing after the last kill, or a non-attackable add.
    // The fight timer does not run on it (Uldaman run 4, archaedas wait_object).
    bool staleCombat = false;
    // In a fight (anyInCombat): the living, not friendly units engaged with the party (on a bot's or pet's
    // attacker set, or a bot's or pet's victim), each once, with entry, guid and UnitFact::attackable. Read
    // (engagedRead) by the glue on every tick in combat; a policy test that does not set it keeps the old rules.
    std::vector<UnitFact> engagedUnits;
    bool engagedRead = false;
    // ... the party's own bots under an enemy's charm (Dominate Mind, Mind Control) that fight the party: never
    // engaged units, but named in the note when nothing else is engaged (EngagedUnitsNote).
    std::vector<std::string> charmedFighting;
    float leadX = 0.0f, leadY = 0.0f, leadZ = 0.0f;
    bool leadMoving = false;           // the lead bot is moving now (Unit::IsMoving): an exact point's proof waits
    // The walk after a rest (Run::restWalkDue): the lead bot is in a rest state (a meal or drink aura up, or not
    // standing), or casting a spell (a heal, a buff: never cut, the walk waits for the cast to end).
    bool leadResting = false;
    bool leadCasting = false;
    // The lead bot's water and own-move state, for the stuck note (StuckNote) and the
    // stall's fallback (StallFallback): in water or under water (Player::IsInWater /
    // IsUnderwater); its "move to position" failed within the last 5 s
    // (ai::PositionMoveFailing), and the fail reason of its last move (MoveFailReasonName).
    bool leadInWater = false;
    bool leadMoveFailed = false;
    std::string leadMoveFailReason;
    // Its stay walk rests after failed walks (StayArrivalPolicy.h StayWalkBackingOff), and
    // how many failed in a row: the stuck note names that rather than an older fail reason.
    bool leadStayWalkResting = false;
    std::uint32_t leadStayWalkFails = 0;
    // The stall's direct move (StallFallback): read by the glue only near a stall
    // (StallDirectProbeWanted), for the walk's point: the line from the lead bot is
    // clear for a straight swim (StallDirectLineClear). Not read: no direct move.
    bool stallDirectRead = false;
    bool stallDirectClear = false;
    float maxMemberYd = 0.0f;           // the living bot farthest from the lead bot
    // The group's raid icons now (Group::GetTargetIcon, star 0 ... skull 7):
    // a cc order only takes a mark that is free or one the run set itself.
    std::uint64_t raidIcons[8] = {};
    bool raidIconsRead = false; // ... read this tick (the lead bot's group): only then they confirm a clear (SyncCcIcons)
    std::vector<MemberFact> members;    // the living bots, the lead bot too (at 0 yd)
    // The glue read MemberFact::withLead for every member this tick (only while DirectJoinWanted: a direct step
    // is the current one or the walk back's). The gate before a direct step then asks that test, not only the
    // ground distance (kGatherYd), so a bot on another floor within it never passes the gate and fails the step.
    bool directJoinRead = false;
    // The glue reads that test at all (the module's glue: always; the policy tests that do not set it, and any
    // legacy caller, keep the ground rule alone when it is not read).
    bool directJoinSupported = false;
    std::vector<std::string> deadNames; // the dead bots (ghosts too), the lead bot too
    // The lead bot has a navmesh path back to the run's anchor (Run::anchor*).
    // Read lazily: only on a tick after the policy asked for it
    // (Run::anchorPathWanted, set when a gather needs the answer).
    bool anchorPathRead = false;
    bool anchorReachable = true;
    // A walk back straight to its last point: the navmesh path the glue read
    // for the next hop passes an uncleared pull or boss group
    // (PathCrossesUnclearedPack); the shortcut is dropped for the replay.
    bool directPathCrossesPack = false;
    // ... and the navmesh path the glue read for that hop ends short of the point (PathEndsShort: no path, or partial
    // and ending more than kPathShortYd from it or kPathShortDzYd above or below it; a bare PATHFIND_INCOMPLETE is
    // also set at the path finder's polygon cap and for a point just off the mesh, and does not count). A straight
    // walk back then replays the plan's own points (Razorfen Kraul r27 slot 8: the hops toward the den at z 63
    // climbed to a ledge at z 104 with no way down, three times).
    bool approachIncomplete = false;
    // AiPlayerbot.DungeonRunWalkBackRescue (default 1): a stuck walk back, and a placed run's lead bot revived behind
    // its start step, are teleported to a safe point (WalkBackStuckRescue, PlacedRunRescue). Off: the old pause.
    bool walkBackRescue = true;
    // AiPlayerbot.DungeonRunFightForward (default 1): FightForwardTick may leave the moves to a close pack out.
    bool fightForward = true;
    // AiPlayerbot.DungeonRunLightRest (default 1): the light rest lines before a small pull (LightRestAhead). Off:
    // the full lines everywhere, as before.
    bool lightRest = true;
    // Fight forward's way, read by the glue on the tick after the policy asked (Run::fightForwardWanted) for that
    // pull step (forwardFor): the lead bot's navmesh path to the pack's point does not end short (PathEndsShort) and
    // is at most kPathDetourFactor times the straight line (forwardPathOk); the lead bot sees the pack's point
    // (forwardLos); a living hostile creature that is not one of the pack's own stands within its aggro radius plus
    // kForwardAggroMarginYd of that path (forwardHostileNear); a living hostile stands within kHopPullYd of a move
    // point left out (forwardHopAlive).
    bool forwardRead = false;
    std::size_t forwardFor = static_cast<std::size_t>(-1);
    bool forwardPathOk = false;
    bool forwardLos = false;
    bool forwardHostileNear = false;
    bool forwardHopAlive = false;
    // The replay's shortcut (RouteShortcut), read the same way (Run::shortcutWanted) for that point (shortcutFor): the
    // lead bot's navmesh path to it does not end short and is at most kPathDetourFactor times the straight line.
    bool shortcutRead = false;
    std::size_t shortcutFor = static_cast<std::size_t>(-1);
    bool shortcutOk = false;
    // In a fight, when the plan has kill_first: its living, not friendly units
    // from the bots' own attacker lists (a summoned healing ward included).
    std::vector<UnitFact> killFirst;
    std::uint32_t minHealthPct = 100;   // living bots
    std::uint32_t minHealerManaPct = 100;
    std::uint32_t minManaPct = 100;     // mana users that are not healers
    std::uint32_t eating = 0;           // living bots with a food or drink aura up (seated: standing ends it)
    bool rezzer = false;                // a living bot out of combat that can resurrect (priest, paladin, shaman)
    // AiPlayerbot.DungeonRunRezHold: the glue read the roster for the rez hold
    // (RezHoldPolicy.h) and the regroup asks it instead of the fixed kPartyRezWaitMs
    // wait on `rezzer`. Off (the default): rezHold false, rezMembers empty, the
    // old rule runs byte for byte.
    bool rezHold = false;
    std::vector<TortoiseBots::RezHold::Member> rezMembers; // every bot of the party, dead ones too
    bool rezInProgress = false;         // ... a party rezzer is casting a resurrection, or one waits to be accepted
    // The step.
    std::uint32_t team = 0;             // the party's team: ai::kPlanFlagAlliance or kPlanFlagHorde (0 unknown)
    bool seen = false;                  // the lead bot stands within kSeeYd of the step's point
    // pull/boss not seen yet: the next approach hop, a point of the lead bot's
    // navmesh path toward the step's point (kApproachHopYd along it at most,
    // kApproachYd short of the point). approachKnown false: no path.
    bool approachKnown = false;
    // The glue asked the navmesh for that hop this tick. It does not while a
    // walk or a regroup is under way, so on the tick such a walk ends no hop
    // is known yet without that meaning "no path" (Uldaman run 3, 19z12: baelog
    // pull 7 and the descent move paused with "no walkable path" on the tick
    // the walk back to the stand point ended, and again on the tick a resume's
    // regroup ended; both navmesh paths are complete).
    bool approachRead = true;
    float approachX = 0.0f, approachY = 0.0f, approachZ = 0.0f;
    // Read with that hop (and for a walk back's move point in sight with a path
    // of its own): the lead bot's navmesh path toward the point is partial and
    // ends within kGoodPathMinYd of it, short of the point, or is no path (LeadOnIsland:
    // IslandEscape), or it gets somewhere (LeadPathGood: the lead bot's spot is the last good one).
    bool leadIsland = false;
    bool leadPathGood = false;
    // ... and with leadIsland only: the path to the run's last good lead spot
    // (within kGoodSpotProbeYd and kEscapeMaxVertical) is incomplete or no path:
    // a known spot of the main mesh the lead bot cannot reach (GoodSpotProbeWanted).
    bool goodSpotCut = false;
    std::vector<UnitFact> units;        // pull/boss: living units of the step's entries within its radius
    // pull (not boss): living units of the step's entries that walk a
    // waypoint patrol (the core's default movement type), outside its radius
    // and within kPatrolSeeYd beyond it, none a later step owns
    // (OwnedByLaterStep). distSpot: from the step's point; x, y, z: where it is.
    std::vector<UnitFact> patrollers;
    std::vector<UnitFact> hopUnits;     // move: living hostile units within kHopPullYd of the point, not owned by a later step
    // boss: a dead unit (corpse) of the boss's entry within the step's radius
    // plus kSeeYd of its point (a pullback drags him toward the party), or
    // within kSeeYd of the lead bot; bossAlive: he stands in the step's units.
    bool bossDeadNearSpot = false;
    bool bossDeadNearLead = false;
    // ... and that corpse's guid and place (the nearest to the step's point when
    // there are several; 0: none seen), so a later `loot` step finds it where it
    // lies, not where the plan says he spawns (Revelosh dies 50-60 yd from his spot).
    std::uint64_t bossCorpseGuid = 0;
    float bossCorpseX = 0.0f, bossCorpseY = 0.0f, bossCorpseZ = 0.0f;
    // boss: the instance-wide search for the boss's entry (Run::bossSearchWanted, once per BossAwayWaitMs window):
    // read (bossSearchRead) by the glue on the tick after the policy asked, from the entry's spawns on the lead
    // bot's map, wherever they stand. Alive: one of them lives (where); dead: one lies dead (that corpse).
    // Scarlet Cathedral r23 slot 5: Mograine died at the door, 290 yd from his spot, and the boss step paused
    // "not found" three times until the corpse decay moved him home 600 s later.
    bool bossSearchRead = false;
    bool bossSearchAlive = false;
    float bossAliveX = 0.0f, bossAliveY = 0.0f, bossAliveZ = 0.0f;
    bool bossSearchDead = false;
    // ... a spawn of the entry stands in a grid that is not loaded (Map::IsLoaded false): that area was not searched.
    bool bossSearchUnloaded = false;
    // ... the step's own spawn (the spawn row nearest to the step's point) lies dead: boss down whatever another
    // spawn row of the entry does (a second living one elsewhere never turns his corpse into a pause). The corpse
    // fields are then his.
    bool bossSearchOwnDead = false;
    // ... the entry has no spawn row on the map (a script summons him): nothing could be searched.
    bool bossSearchNoSpawn = false;
    // ... seen neither alive nor dead, and every spawn row of the entry on the map belongs to a game event that is
    // not active (game_event_creature or its pool's event, read in sGameEventMgr: such a row spawns only while its
    // event runs). The event of the step's own spawn and its description. Scarlet Graveyard r23 slot 7: Scorn (14693,
    // spawn 2350) spawns only with event 81 "Scourge Invasion", whose start and end were 0 on that server, and his
    // boss step paused "not found" until the run was stopped by hand.
    bool bossSearchEventOff = false;
    std::uint32_t bossSearchEventId = 0;
    std::string bossSearchEventName;
    std::uint64_t bossSearchCorpseGuid = 0;
    float bossSearchCorpseX = 0.0f, bossSearchCorpseY = 0.0f, bossSearchCorpseZ = 0.0f;
    int objectState = -1;               // wait_object, hold done by object: 0 ready, 1 active, 2 active_alt; -1 not found
    // wait_object: the -1 above is an authoritative "not there": the lead bot is alive and in the world on the plan's map,
    // the step's point lies within AiPlayerbot.ReactDistance of it (the by-entry search covers only that) and the point's
    // grid is loaded. A -1 read while this is false (a dead, released or teleporting lead bot, another map, too far, an
    // unloaded grid) never counts as gone.
    bool objectReadable = false;
    // hold (units: the living units of its `entries` within its radius, the
    // friendly, still dormant ones too):
    std::uint32_t eventDown = 0;        // dead units of its `entries` within its radius
    long long eventData = -1;           // done by instance data: the value now; -1 the map has no instance data
    std::vector<HostileSpot> clearUnits; // wait_clear: the living hostiles within the 2D radius of the step's
                                         // point, with z and whether engaged with the party; the wait counts every
                                         // one, and only the timeout boundary drops all-off-floor bystanders.
    std::string carrier;                // the bot that does object and item steps (the lead bot)
    // open / use_item: the step's inputs (its item, and `param` when set) are
    // split between bots, none carries them all: who holds what (PickItemCarrier).
    std::string itemsSplit;
    // use_on: the act of the carrier's task had started when its answer came (the cast began: an in-combat answer after it
    // is no sign the item did not take); the object's state differed from the one it had when the use began (ObjectEffectOf
    // at the answer); a unit of the boss row right after the use_on is in sight around the lead bot, alive or dead.
    bool taskActStarted = false;
    bool objectReacted = false;
    bool summonSeen = false;
    // open_proof (the current step only): the glue looked for the proof this tick (proofRead), and a spawned game object
    // (a living creature with kPlanFlagProofCreature) of the step's one `entries` entry stands within its radius of x,y,z
    // (proofUp). Not read is never proof.
    bool proofRead = false;
    bool proofUp = false;
    bool carrierHasItem = false;        // the step's `item` is in the carrier's bags
    bool carrierHasEntryItem = false;   // use_item: the item to use is in the bags
    bool carrierBusy = false;           // a commander task (loot/open/use) runs for the carrier
    // loot: read for the step's item, on the tick of an order and after a refusal.
    bool partyHasItem = false;          // some party bot (dead ones too) carries the step's `item`
    std::string itemHolder;             // ... the first such bot
    // ... and only after a refusal, from the remembered corpse:
    std::string lootTurn;               // the party bot whose round-robin turn the corpse is ("": none, not a bot)
    bool corpseChecked = false;         // the remembered corpse (by guid) was looked at
    bool corpseHasItem = false;         // ... and is in the map, still holding the step's item (not looted)
    int taskReply = 0;                  // the carrier's task answer since the run's order: 0 none, 1 ACK, 2 ERR
    std::string taskText;
    // AiPlayerbot.DungeonRunLootFfa (default 0): read every tick, only a loot step looks at them.
    bool lootFfaOn = false;             // the key is on
    int lootMethod = -1;                // the group's loot method (RemoteCommander::kLoot*), -1: no group
    bool leadLeadsGroup = false;        // the lead bot leads the group (`cmd lootmethod` needs it)
    // Guarding the party (GuardTarget), read in fights only:
    bool leadIsTank = false;            // the lead bot is a tank
    bool leadStaying = false;           // the lead bot holds "stay" (the run's move to a stand or walk-back point)
    bool leadStayKnown = false;         // ... and its stay point (set by the pullback or the run's move) on this map
    float leadStayX = 0.0f, leadStayY = 0.0f, leadStayZ = 0.0f;
    bool leadHit = false;               // a living mob with the lead bot as its victim is in melee reach of it
                                        // (a caster that turns on the tank from 30 yd is not the tank holding it)
    // ... and the nearest such mob (EngageTarget: the tank on "stay" fights nothing
    // until an attack order takes it off the stay); guid, entry, distLead, polymorphed,
    // evading and hitting set. leadHasAttacker is false when no mob is on the lead bot.
    UnitFact leadAttacker;
    bool leadHasAttacker = false;
    // Living, not friendly mobs whose victim is a party bot other than the lead
    // bot within kGuardYd of it; distLead from the lead bot, `hitting` and
    // `victimHealer` set.
    std::vector<UnitFact> partyAttackers;
    // The pull in flight (CheckReach), read in fights only: the lead bot's own
    // pull command is still reaching its target (the target has not come for
    // it, it has not turned back), and how many living, not friendly mobs have
    // a party bot as their victim (any of them, the lead bot too).
    bool leadReaching = false;
    std::uint32_t reachAggro = 0;
    // AiPlayerbot.DungeonRunAggroEstimate: the ceiling of expected joiners
    // (AiPlayerbot.DungeonRunAggroCeiling) while the estimate is on, 0 = off.
    // Off, the units carry no estimate and no pull ever waits for it.
    std::uint32_t joinerCeiling = 0;
    // AiPlayerbot.DungeonRunPullStand (HoldForShot): the key is on. Off (the
    // default) nothing below is read and the pull goes out as before, byte for byte.
    bool pullStand = false;
    // AiPlayerbot.DungeonRunWalkBackAfterWonFight (GuardWalkBackWanted): on (1),
    // the old walk back to the guard's stand point after every guarded fight. Off
    // (0, the default) the run goes on from where the tank stands once the step's
    // pack is cleared (Frostmane Hollow r20-19z39: the party ran back from each
    // boss and pack, 2-3 s a pull, and walked forward again).
    bool walkBackAfterWonFight = false;
    // AiPlayerbot.DungeonRunRetreatAt2Dead (RetreatTick): on (1), two dead in a fight the party is losing pull the
    // living back to the last stand once. Off (0, the default): nothing changes.
    bool retreatAt2Dead = false;
    // The lead bot's ranged weapon shot range (its PullStrategy::GetRange(),
    // pull action "shoot" only); 0 for a spell pull, a body pull ("reach pull":
    // no ranged weapon, skill, spell or ammo) or when it could not be read. At or
    // under kShotMarginYd it keeps the old order too (PullStandOn).
    float leadShotYd = 0.0f;
    // The firing stand the policy asked for (Run::shotStandFor), read this
    // tick: the lead bot's navmesh path to that unit walked back from the unit
    // end to the first point within leadShotYd - kShotMarginYd that sees it
    // (ShotStandOnPath). shotStandGuid is the unit it was read for (0: none
    // read this tick); shotStandKnown false: no such point, or the unit is gone.
    std::uint64_t shotStandGuid = 0;
    bool shotStandKnown = false;
    float shotStandX = 0.0f, shotStandY = 0.0f, shotStandZ = 0.0f;
    float shotStandYd = 0.0f;           // ... the stand's distance from the unit (centre to centre); it sees the unit
};

// A bot's own stand point in a hold (HoldStands): the glue walks it there and
// keeps it on "stay" until the list changes.
struct HoldStand
{
    std::string name;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Where a boss's corpse lay when the boss step saw him down.
struct CorpseMark
{
    std::uint64_t guid = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// A wait_object target a wait step has read found (Run::objectsSeen).
struct WaitSight
{
    std::uint32_t entry = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

constexpr float kWaitSightYd = 10.0f;               // two waits on one entry within this of each other are one target
constexpr std::uint32_t kGoneConfirmMs = 3000;      // a gone object must read not found, readably, this long without a break

struct Run
{
    const DungeonPlan* plan = nullptr;
    std::size_t step = 0;
    // Area triggers the plan's move steps name (ArmReachedTriggers): armed from the step on until one
    // fired for the run (NoteTriggerFired), then never again. triggerScan: the next step index to look at.
    std::size_t triggerScan = 0;
    std::vector<std::uint32_t> armedTriggers;
    std::vector<std::uint32_t> firedTriggers;
    State state = State::Running;
    Phase phase = Phase::Begin;
    std::string note;        // what it does now, or why it paused

    std::uint32_t phaseMs = 0;
    std::uint32_t stepIdleMs = 0;
    std::uint32_t playerGatherMs = 0; // a regroup's wait for a living player (RegroupPlayerWait)
    std::uint32_t playerHoldMs = 0;   // a hold for a player (PlayerHoldNote) or a regroup's wait for a dead one, kPlayerHoldCapMs
    bool restDue = true;     // check the rest lines before the step
    int regroupDead = -1;    // the dead when the regroup was first read (-1 not yet): more than that is a new wipe
    std::uint32_t eatWaitMs = 0; // meal wait used since the last fight (kRestCapMs per fight: it cannot loop)
    bool restForEating = false; // the rest phase is the meal wait: it also ends when nobody eats any more
    // A rest phase ended (the rest gate): the next walk (WalkTo) the lead bot has not started within
    // kRestWalkResendMs is sent again, once, with leadWalkReset (RestWalkResendDue says when). Kept over the step
    // change (a pull step's rest is followed by the next step's walk), dropped when the lead bot gains ground or
    // arrives, a fight starts or the run pauses. Blackfathom Deeps r22 step 85 and r23 slot 6b step 8: after a rest
    // the lead bot stood on "stay" (act "food", then "stay") and never took the walk; its retries every 12 s did not
    // move it, and a resume did.
    bool restWalkDue = false;
    // ... the order of this tick is that second walk: the glue first stands the lead bot up (a seated bot is not
    // moved; standing ends its meal or drink). The glue clears it.
    bool leadWalkReset = false;
    bool stepRested = false; // a `rest` step has had its gate
    bool orderSent = false;  // task steps: the order went out
    std::uint32_t taskResends = 0; // task steps: orders sent again after an interrupted cast or an in-combat answer
    std::uint32_t proofWaitMs = 0; // open_proof: waited for the proof after the task answered (kOpenProofWaitMs)
    std::set<std::size_t> useOnRang; // use_on steps whose order was ACKed (or went unanswered) this run: never sent again; ResetStep keeps it
    long long dataAtOrder = -1; // a ritual: the hold's instance data when the order went out
    bool walkedThrough = false;
    // The target of the approach hop the walk goes to (hopWalk), to tell it from
    // another walk of the step (the stealth walk through a pack's centre).
    std::unordered_set<std::uint64_t> hopWaiveUnits; // the gather is left out for a pull of these units only (the step's units when a hop was abandoned)
    bool hopWalk = false;
    float hopX = 0.0f, hopY = 0.0f, hopZ = 0.0f;
    std::uint32_t approachHops = 0;     // approach hops since the party last got nearer to the step's point
    float approachBestYd = 0.0f;        // the nearest the lead bot has been to it (0: not set yet)
    // A pull or boss step whose approach hops got no nearer (ApproachStand): a stand the
    // glue's NearestReachablePoint finds within kApproachStandYd of the pack, the party
    // walks there and pulls from it. Kept across a resume (ResetStep leaves it; NextStep
    // clears it), so a resume of its pause never hops the same way again.
    // approachStand: 0 none, 1 the glue searches (approachStandWanted), 2 a stand found,
    // 4 walking to it, 3 spent (none found, or the stand did not bring the pack in sight).
    // The moves of the last 'three moves in a row had no walkable path' pause
    // (MoveRefusedUnreachable): the run went back to the first of them; the same three
    // failing again pause with the route note instead of a skip of three more.
    std::vector<std::size_t> noPathSkips;
    std::size_t approachStandStep = static_cast<std::size_t>(-1);
    std::uint8_t approachStand = 0;
    bool approachStandWanted = false;
    bool approachStandResumed = false; // a resume of this step's approach pause: no hops, the stand first
    bool approachStandResumedSearch = false; // the search under way is that resume's (none found: hops again)
    // The step whose refused pullback (PullbackRefusedUnreachable: the tank could not start moving to a firing
    // position) asked the approach stand: FightStep runs that stand's search and walk with the pack in sight.
    std::size_t refusedPullStandStep = static_cast<std::size_t>(-1);
    std::string approachStandWhy;      // the hops' note the stand's pause starts with (empty after a resume)
    float approachStandX = 0.0f, approachStandY = 0.0f, approachStandZ = 0.0f;
    // A move whose point the navmesh cannot reach walks to the nearest
    // reachable point near it instead (UseDetour).
    bool detour = false;
    float detourX = 0.0f, detourY = 0.0f, detourZ = 0.0f;
    // The step whose own move (or its detour) was last refused for want of a
    // path, and how many such refusals it had (MoveRefusedUnreachable). Kept by
    // ResetStep, so a resume does not start the step's detour over.
    std::size_t moveRefusedStep = static_cast<std::size_t>(-1);
    std::uint32_t moveRefusals = 0;
    // (ForgetMoveRefusals clears both and unreachableSkips when the run goes back
    // to an earlier step; Resume clears them for a step that is never skipped.)
    // The moves skipped for want of a path since a step was last done any other
    // way (MoveRefusedUnreachable; NextStep clears it): kUnreachableSkipsInARow pause.
    std::vector<std::size_t> unreachableSkips;
    bool fighting = false;
    std::uint32_t fightMs = 0;
    std::uint32_t staleMs = 0;          // all the time in this fight the combat flag stood with nothing hostile engaged (Facts::staleCombat)
    std::uint32_t staleRunMs = 0;       // ... and the time it has stood so without a break
    std::uint32_t restRepeatMs = 0;
    // The last point the run itself walked the party to (a move, an approach
    // hop, a stand point): a gather goes back there when a fight left the lead
    // bot off it (another floor, no path back), not where the lead bot stands.
    bool anchorKnown = false;
    float anchorX = 0.0f, anchorY = 0.0f, anchorZ = 0.0f;
    bool anchorPathWanted = false; // a gather needs the lead bot's path back read (next tick)
    // Pull and boss steps left without being cleared (skipped, out of reach):
    // a direct walk back keeps clear of them too.
    std::unordered_set<std::size_t> notCleared;
    // Legs whose item need was decided once this run (an optional leg's skip,
    // a key door opened, carried or held): ResetStep keeps them.
    std::set<std::uint8_t> legNeedDecided;
    // Optional legs skipped this run, in skip order (ResetStep keeps them).
    std::vector<std::uint8_t> skippedLegs;
    // The door hold's pause (DoorNeedHold, DoorKeyOpen's last try): only the
    // door read open ends it (DoorOpenWhilePaused, then the run goes on by
    // itself); `.bot run resume` keeps it while the door reads closed
    // (ResumeChecked); `.bot run skip` walks the party past it by hand (the leg
    // is decided). Pause and Resume clear it.
    bool doorPaused = false;
    std::uint8_t doorPausedLeg = 0;
    std::string doorPausedAsk; // the hold's ask (what opens the door), for the resume's refusal
    std::string say;           // what the bots say on /s this tick (TakeSay); "" none
    // DoorKeyOpen: the leg whose door a carrier bot is opening (-1 none), the
    // open orders sent for it and the time since the last one.
    int doorOpenLeg = -1;
    std::uint32_t doorOpenTries = 0;
    std::uint32_t doorOpenMs = 0;
    // Windhorn Canyon r26 slot 5b step 68: pull steps whose pack was judged not reachable
    // from the floor the party stood on (DeferUnreachablePack) were deferred, not paused:
    // each is tried once more at the end of its leg (StepAfter), from where the party is then.
    std::vector<std::size_t> deferredPulls;                        // deferred, their retry still to come
    std::size_t deferRetryStep = static_cast<std::size_t>(-1);     // the current step is that retry
    std::size_t deferReturnStep = static_cast<std::size_t>(-1);    // the step the run goes on with after the retries
    std::uint8_t deferLeg = 0;                                     // the leg those retries belong to
    std::vector<std::size_t> deferGivenUp; // still not reachable on the retry: skipped, not cleared (DoneNote lists them)
    std::string deferLog; // a deferral or a give-up, for the glue's log once (the glue clears it)
    // The boss tactic table the server reads now (the glue sets it before each tick: ai::GetBossTactics()). A
    // `.bot run reloadplans` swaps it while this run keeps its old plan, whose boss steps joined the old rows
    // (PlanStep::tacticEntries): PullMayBeDeferred reads both (the Codex review of 7cfcbff6). Null: the plan's only.
    const ai::BossTacticTable* liveTactics = nullptr;
    // Where the lead bot stood when an approach paused (PauseApproach): a resume that finds it
    // still there searches the stand from that spot, with no walk back (ApproachResumeInPlace).
    bool approachPauseKnown = false;
    float approachPauseX = 0.0f, approachPauseY = 0.0f, approachPauseZ = 0.0f;
    // Walk.
    float walkX = 0.0f, walkY = 0.0f, walkZ = 0.0f;
    std::uint32_t walkTries = 0;
    std::uint32_t walkStillMs = 0;
    // The core PathFinder's type for the last plain `cmd move` the run sent
    // (CommanderMove's check; NoteMovePathType), for the stuck note. Known:
    // a plain move was acked since the run started.
    bool movePathKnown = false;
    std::uint32_t movePathType = 0;
    // A stalled walk whose lead bot's own move failed, out of combat (StallFallback):
    // 0 not tried, 1 the `direct` move sent (or not possible), 2 a dry stand-in asked
    // of the glue (stallStandInWanted, StallStandIn), 3 spent. A fresh walk clears it.
    std::uint8_t stallFallback = 0;
    bool stallStandInWanted = false;
    float walkBestYd = 0.0f;
    // A walk whose move was refused too-far (beyond AiPlayerbot.ReactDistance) goes in
    // legs (TooFarLegTaken): the leg under way and how many this walk sent. A fresh walk
    // clears them.
    bool legOn = false;
    float legX = 0.0f, legY = 0.0f, legZ = 0.0f;
    std::vector<float> legEnds; // x, y, z of every leg this walk sent (TooFarLegRepeats), three floats a leg
    float legBestYd = 0.0f;
    float legStartYd = 0.0f; // the lead bot's distance to the leg when it went out
    std::uint32_t walkLegs = 0;
    // An exact waypoint's tighten (ExactWalkTo): on once the walk to the point
    // ended (3 yd, or a stall within 6 yd); it brings the lead bot onto the
    // point and wants the stand-still proof before the step is done.
    bool exactOn = false;
    std::uint32_t exactMs = 0;          // the tighten's time (out of combat: the fight code runs instead)
    std::uint32_t exactRounds = 0;      // moves onto the point it sent
    std::uint32_t exactWaitMs = 0;      // the lead bot standing (not on the point) since the last order
    std::uint8_t exactStage = 0;        // 0 judging, 1 walking back to exactBack*, 2 walking onto the point
    float exactBackX = 0.0f, exactBackY = 0.0f, exactBackZ = 0.0f;
    bool exactLastSet = false;          // the lead bot's spot on the tighten's last tick (moving between looks)
    float exactLastX = 0.0f, exactLastY = 0.0f, exactLastZ = 0.0f;
    bool exactAnchorSet = false;        // the proof: one spot within kExactArriveYd of the point ...
    float exactAnchorX = 0.0f, exactAnchorY = 0.0f, exactAnchorZ = 0.0f;
    std::uint32_t exactStillMs = 0;     // ... stood on this long
    // The exact step whose proof stands (kept across NextStep: the direct step
    // after it reads it; cleared by a pause). npos: none.
    std::size_t exactProved = static_cast<std::size_t>(-1);
    // Pulls.
    std::uint64_t pullGuid = 0;
    std::uint32_t pulls = 0;
    std::unordered_map<std::uint64_t, std::uint32_t> pullTries;
    std::unordered_set<std::uint64_t> passed;
    bool pullHop = false;           // the pull out is a hop pull (a move's or the walk back's)
    std::size_t openAtLastPull = 0;
    std::uint32_t fruitlessPulls = 0;
    std::uint32_t bossWaitMs = 0;   // a boss step: time at the spot with the boss neither alive nor dead in sight
    bool bossSeenAlive = false;     // a boss step: the boss was among the step's living units
    // A boss step past BossAwayWaitMs with the boss neither alive nor dead in sight asked the glue for the
    // instance-wide search (Facts::bossSearchRead, next tick); one per window (ResetStep clears it).
    bool bossSearchWanted = false;
    // A stand-pull boss step: time waited out of combat with the boss in the radius but out of the tank's shot
    // (kStandPullWaitMs ceiling); cumulative across pauses, resumes and wipe recoveries of the SAME step: NextStep
    // and Skip move it on (standPullStep), Resume and the wipe recoveries keep it. standPullShotGuid authorizes
    // the pull past HoldForShot for the unit the gate judged in shot (0: none); PullUnit skips the stand rule
    // only for that guid and clears it once a pullback goes out; the wait paths before that clear it too.
    std::size_t standPullStep = static_cast<std::size_t>(-1);
    std::uint32_t standPullMs = 0;
    std::uint64_t standPullShotGuid = 0;
    std::uint32_t patrolWaitMs = 0; // a pull step: time spent waiting for a patroller outside the radius (WaitForPatroller)
    std::unordered_set<std::uint64_t> patrolSeen; // ... the patrollers this step saw (a pass of one is remembered run-wide)
    // A big pack's stand walk (BigPackWalkGate): the walk's unit, stand and
    // joiner count; the hold for the party, its time, and whether its ceiling
    // was spent (the walk then goes on without the gate).
    bool bigWalk = false;
    std::uint64_t bigWalkGuid = 0;
    float bigWalkX = 0.0f, bigWalkY = 0.0f, bigWalkZ = 0.0f;
    std::uint32_t bigJoiners = 0;
    bool bigHold = false;
    std::uint32_t bigHoldMs = 0;
    bool bigHoldSpent = false;
    std::string bigHoldNote;
    // The last joiner count the glue read per unit this step: a unit held for
    // its rest is not judged again (joinersRead false), the count stands.
    std::unordered_map<std::uint64_t, std::uint32_t> joinerSeen;
    std::uint32_t walkBackPulls = 0; // pulls on the way of the walk back (not counted in `pulls`)
    // Run-wide (kept across steps).
    std::unordered_set<std::uint64_t> unreachable;   // units given up on: never pulled again this run
    // Units that never came (PLAN-ZF-NOFIGHT; Zul'Farrak r3/r6, the island
    // Shadowhunter 81610): hop units only (a move's or the walk back's) whose
    // one pull drew no fight within kPullNoFightMs, or that the tank could not
    // shoot (PullUnit). Read only by the hop picks (PickPull `hop`), so a pull,
    // boss or hold step's own units are never left out by it. Cleared by every
    // regroup (BeginRegroup: a resume, a wipe recovery); a unit in it that turns
    // up as an attacker of the party is taken out at once (ForgetUnitsThatCame).
    std::unordered_set<std::uint64_t> neverCame;
    // Patrollers given up on (WaitForPatroller): the wait ran out, or a pull
    // step passed it after its pulls drew no fight. Run-wide, so a later step
    // listing the same entry does not wait for it again; cleared like neverCame
    // by every regroup (a resume, a wipe recovery).
    std::unordered_set<std::uint64_t> patrolGaveUp;
    // loot: the bot the order goes to ("": the carrier), the bots a refusal on the
    // loot rules has been tried with, and the refusal's pause text while the
    // next bot is tried.
    std::string taskBot;
    std::vector<std::string> lootTried;
    std::string lootRefusal;
    bool lootRights = false;            // the refusal was the loot rules (another member's turn ...)
    // The free-for-all detour of a loot step (LootFfaStart, LootFfaStep). lootFfaUsed: once per step (ResetStep).
    // lootFfaLive and lootRestore are run-wide: the group's method may be ffa because of this run, and was
    // lootRestore (RemoteCommander::kLoot*) before. A running tick with lootFfaLive and no detour in progress
    // orders it back (LootFfaRestoreTick); the glue does so whenever the run is not running.
    LootFfa lootFfa = LootFfa::Idle;
    bool lootFfaUsed = false;
    bool lootFfaLive = false;
    int lootRestore = -1;
    std::uint32_t lootRestoreTries = 0; // restore orders sent by the running-tick restore (LootFfaRestoreTick)
    std::uint32_t lootFfaMs = 0;        // time since the ffa order went out (while lootFfaLive)
    std::unordered_set<std::uint32_t> missing;       // items the run did not get (a skipped loot or use)
    // wait_object targets (entry and the step's point) a wait step read found at least once, in any state: a wait_object
    // row with ai::kPlanFlagGoneIsDone is done by that object being gone later, never by one never seen. Two waits on
    // one entry at points more than kWaitSightYd apart are two targets (WaitSightSeen).
    std::vector<WaitSight> objectsSeen;
    // The time the current wait_object step read its object not found with Facts::objectReadable (a miss that proves the
    // object is gone), unbroken; goneMissStep is the step it counts for.
    std::uint32_t goneMissMs = 0;
    std::size_t goneMissStep = static_cast<std::size_t>(-1);
    std::unordered_map<std::uint32_t, CorpseMark> corpses; // boss entry -> the corpse seen when that boss step ended "boss down"
    std::uint32_t pendingMissing = 0;                 // the item a paused step failed to get (missing on skip)
    std::size_t skippedOpenStep = static_cast<std::size_t>(-1); // an `open` left out: the wait after it goes too
    // Regroup after a resume (revive the dead, gather), before the step.
    bool regroup = false;
    bool reviveSent = false;
    bool regroupGatherSent = false;
    std::uint32_t regroupMs = 0;
    // The rez hold (AiPlayerbot.DungeonRunRezHold, RezHoldPolicy.h): the time a resurrector could act
    // (restarted while one drinks), and the regroup clock's reading when the hold gave up and the revive
    // went out (the 240 s revive budget runs from there). Both stay 0 with the key off.
    std::uint32_t rezAbleMs = 0;
    std::uint32_t reviveBaseMs = 0;
    // The rez hold's nudge: the time the hold went on with a rezzer able and no cast under way, and whether the
    // lead bot was sent onto the body already (once per regroup). Both stay 0/false with the key off.
    std::uint32_t rezHoldMs = 0;
    bool rezNudged = false;
    // The order of that nudge, kept to tell its refusal (OrderRefused) from any other, and whether the module
    // answered it with a path refusal (the body cannot be walked to): the hold is then over and the revive goes out.
    std::string rezNudgeOrder;
    bool rezBodyUnreachable = false;
    void RestartRegroupClock()
    {
        regroupMs = 0;
        reviveBaseMs = 0;
    }
    // Gathers (a move to the lead bot's own spot): the one sent this tick, so a
    // refusal of it is known; refused kGatherTries times, it is left out.
    std::string gatherOrder;
    std::uint32_t gatherRefusals = 0;
    bool gatherWaived = false;
    std::unordered_set<std::string> summoned; // stragglers this step summoned already (once each)
    std::uint32_t gatherComingMs = 0; // the gather ceilings this step let a straggler that keeps coming pass (GatherCeiling)
    // The straggler ladder (StragglerLadderTick), per bot out of the gather while the run waits for the party:
    // how long it has been out (out of combat), the rung reached (0 none, 1 walked to the lead bot by path,
    // 2 summoned) and how long since that rung went out.
    struct Straggle
    {
        std::uint32_t outMs = 0;
        std::uint8_t rung = 0;
        std::uint32_t rungMs = 0;
    };
    std::unordered_map<std::string, Straggle> straggle;
    std::string sapWalkDue; // a rogue whose Sap failed: walked back to the lead bot's spot next (SapWait)
    std::string summonDue; // a follower a direct step left behind (refused too-far): summoned on the next tick
    std::string summonWait; // ... the summon sent: the step waits for that bot at the lead bot (kSummonWaitMs at most)
    std::uint32_t summonWaitMs = 0;
    // Every summon of a bot this step (MarkSummoned): how many, and the run clock (runMs) of the last one. A
    // direct step's straggler may be summoned again by this record (DirectSummonAllowed), not once by `summoned`.
    struct StepSummon
    {
        std::uint32_t count = 0;
        std::uint64_t lastMs = 0;
    };
    std::unordered_map<std::string, StepSummon> stepSummons;
    std::unordered_map<std::string, std::uint64_t> deadSeenMs; // the run clock a bot was last seen dead (NoteDeadSeen), the whole run
    bool directRefusedOn = false;     // this step's direct step was refused too-far (or its gather hit the ceiling) ...
    std::uint64_t directRefusedMs = 0; // ... first at this run clock (DirectPauseDue)
    std::size_t directRefusedIdx = 0;  // ... at this plan point (DirectTroublePoint)
    std::uint32_t directJoinHolds = 0; // ticks a direct order was held for facts without the glue's test (kDirectJoinHolds)
    std::string directWaitFor;        // a refused direct step's straggler the run waits for (no summon allowed yet)
    std::uint32_t directWaitMs = 0;   // ... this long so far (kDirectRetryMs, then the step goes out again)
    std::unordered_map<std::string, float> memberYd; // each bot's distance to the lead at the gather's last sample
    // Revived bots summoned to the lead bot (SummonRevived), not yet with it:
    // the time since the summon went out. Not counted against the regroup's
    // gather ceiling; one that fails (refused, or not there in
    // kReviveSummonWaitMs) leaves the list and walks back.
    std::unordered_map<std::string, std::uint32_t> reviveSummons;
    std::uint32_t regroupGatherCapMs = kRegroupGatherMs; // the regroup's gather ceiling (RegroupGatherCap)
    // Each living bot's stand-still watch (UpdateStill, every tick): where it
    // stood when it last moved kStillYd, and how long it has stayed there.
    struct StillState
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::uint32_t ms = 0;
    };
    std::unordered_map<std::string, StillState> still;
    // The lead bot dying alone in a fight and coming back to life far from
    // where it fell (its own release and corpse run: the instance entrance)
    // while the others won the fight deep inside (LeadRevivedAway).
    bool leadSeen = false;
    float leadLastX = 0.0f, leadLastY = 0.0f, leadLastZ = 0.0f; // the living lead bot's last spot
    bool leadDied = false;
    // The owner's rule of 2026-10-17, "Party should wait if a party member
    // dies until it is back": every bot seen dead since the party was last
    // together. The run starts nothing new until each is alive and within
    // kGatherYd of the lead bot again (WaitForFallen).
    std::unordered_set<std::string> waitFor;
    // The living party held where it won the fight while the lead bot (and
    // the bots revived with it at the entrance) walk the plan's way back to
    // it (LeadRevivedAway): the glue keeps each named bot on stay at its spot,
    // and the run leaves them out of its gathers and summons (HoldView). A bot
    // drops out once the lead bot is within kGatherYd of it; the list ends with
    // the walk back, on a pause and with the step.
    std::vector<std::string> hold;
    // A regroup for a dead lead bot (WaitForFallen): once the lead bot is up
    // again more than kSeeYd from where it fell, the hold is filled before the
    // regroup gathers (19z11 03:24: the fight ended with the lead bot a ghost).
    bool holdDue = false;
    float leadDeathX = 0.0f, leadDeathY = 0.0f, leadDeathZ = 0.0f;
    // The lead bot revived in another part of the instance (LeadInOtherPart):
    // seen on a door's destination far from every point of the plan, kept
    // while it stays that far (it may walk off the door).
    bool leadOtherPart = false;
    // The pause for it (PauseOtherPart) keeps the hold: the living party stays
    // where it is instead of following the lead bot out through the instance's
    // exits (HoldStandsNow; the glue keeps those bots on stay while paused).
    bool holdWhilePaused = false;
    // The run is paused for it (PauseOtherPart; Pause clears it): `.bot run
    // resume` with the lead bot still that far is refused (ResumeChecked).
    bool otherPartPaused = false;
    // ... unless the pause's reason lets a resume run the rescue teleport (OtherPartResumeRescues; Pause clears it).
    bool otherPartResumeRescue = false;
    // The rescue teleports for a lead bot in another part at step otherPartRescueStep (OtherPartRescue counts them,
    // kOtherPartRescuesPerStep; Resume starts them over).
    std::uint32_t otherPartRescues = 0;
    std::size_t otherPartRescueStep = static_cast<std::size_t>(-1);
    // Crowd control and kill-first (the plan's cc_priority / cc_always / kill_first).
    std::unordered_set<std::uint64_t> ccSent; // units this step put a `cc <mark>` on (once each)
    // Who holds which unit (issue #1159): one unit per bot and per mark while
    // that unit lives among the step's units; a unit is never given a second bot.
    struct CcHold
    {
        std::string bot;
        std::string mark;
        CcFamily family = CcFamily::None;
    };
    std::unordered_map<std::uint64_t, CcHold> ccHolds;
    // The cc orders whose aura is not seen yet (CheckCc, and SapWait before
    // the pull), in the order sent.
    struct CcWatch
    {
        std::uint64_t guid = 0;
        bool resent = false;
        std::uint32_t waitMs = 0;
    };
    std::vector<CcWatch> ccWatch;
    std::vector<std::uint64_t> ccLandedDrop; // the glue drops every bot's order on these (CC landed this tick)
    bool ccSapFailed = false; // a Sap did not land this step: no other Sap in it
    bool ccRegather = false;  // a Sap wait ended: gather (the rogue is by its mob) before the pull
    // The raid icons the run's cc orders set, icon slot -> the unit (for the
    // whole run, not the step): the run reuses only these and clears them at
    // its end (CcIconsToClear), and never moves an icon it did not set.
    std::unordered_map<int, std::uint64_t> ccIcons;
    std::uint64_t focusGuid = 0;              // the kill-first unit focused in this fight
    // Kill-first units whose focus was refused (kFocusRefusedIgnoreMs): unit ->
    // runMs until which PickKillFirst passes it over. focusUnstayed: such a
    // refusal this fight (CommanderFocus takes every bot off "stay" before it
    // asks them), so the guard keeps watching though focusGuid is cleared.
    std::unordered_map<std::uint64_t, std::uint64_t> focusRefused;
    bool focusUnstayed = false;
    // The unit the run's own focus order put the skull on (0: none), the unit whose skull the glue takes off now
    // (DropFocus; 0: none), and the previous focus unit's skull (0: none). Every bot's "rti" is "skull" (RtiValue's
    // default) and RtiTargetValue takes any living skull-marked unit in sight, in combat or not: a skull left on a
    // unit that went home after a wipe held every DPS bot's target for minutes (Scholomance r30 slot C, t 595-973:
    // four bots on an Acolyte 48 yd away at its spawn).
    // A new focus moves the skull with its order; until that order lands (it may be refused before the glue moves
    // the skull: not visible/found/dead) the old skull stays where it was, remembered here. The glue clears both
    // where they still are (EndRunIcon: a moved skull is a Drop, a no-op).
    std::uint64_t focusIconGuid = 0;
    std::uint64_t focusIconClear = 0;
    std::uint64_t prevFocusIconGuid = 0;
    // Aggro estimate (Facts::joinerCeiling): a pull expected to bring more
    // joiners than the ceiling waits once for the party to be rested.
    std::unordered_set<std::uint64_t> joinerHeld; // units already held once this step
    bool joinerRest = false;                  // the rest check is due before the pull
    bool restForJoiners = false;              // the rest phase is the joiner rest: its time is not step idle time
    std::uint32_t joinerRests = 0;            // joiner rests used this step (kMaxJoinerRests)
    std::string joinerNote;                   // "N joiners expected ..."
    // The pull stand (Facts::pullStand, HoldForShot): the unit the glue reads a
    // firing stand for on the next tick (0: none), the stands walked to per
    // unit this step, the stand walk under way (its unit and point), the time
    // a moving tank has been waited for, and how the last order's shot stood
    // (the order's note). shotStandPrep: a tick of this pull held for the
    // stand rule; the pull's fruitless count is not taken again until it goes out.
    std::uint64_t shotStandFor = 0;
    std::unordered_map<std::uint64_t, std::uint32_t> shotStandTries;
    bool shotStandWalk = false;
    std::uint64_t shotStandGuid = 0;
    float shotStandX = 0.0f, shotStandY = 0.0f, shotStandZ = 0.0f;
    std::uint32_t shotStillMs = 0;
    std::uint32_t shotWalkMs = 0;             // the stand walk's time (kShotWalkCapMs)
    bool shotStandPrep = false;
    std::uint64_t shotHoldGuid = 0;           // ... the unit the prep is for (another pick checks the way back)
    std::string shotHow;
    // HoldForShot ran on this tick. A tick it did not run on (a fight, a
    // gather, a dead member, a joiner rest, a regroup, another step kind) ends
    // the hold: the stand request, the walk and the wait are dropped (Tick).
    bool shotGateRan = false;
    // The first of ccWatch (SyncCcWatch), for the readers of one order:
    std::uint64_t ccPending = 0;              // the `cc <mark>` sent, its aura not seen yet
    bool ccResent = false;                    // ... sent a second time
    std::uint32_t ccWaitMs = 0;               // fight time since the mark (or its second sending)
    // Regroups the run started by itself at this step (a wipe or kWipeDead
    // dead), and the walk back over the plan's move points after one.
    std::uint32_t autoRegroups = 0;
    bool walkBackDue = false;    // walk back once the regroup is over
    bool walkBack = false;
    bool walkBackDirect = false; // straight to the last point in hops (before a replay of every point)
    std::size_t walkBackStart = 0; // the plan step the walk back began at (the straight target, or the replay's start)
    bool walkBackRestarted = false; // a replay that could not begin went back to the first point the route allows, once
    std::size_t walkBackIdx = 0; // the plan step whose point is walked now
    // The step the run started at (`.bot run start ... from`, a placed run; 0 from the beginning; the glue sets it
    // next to Start, whose `fromStep` the policy tests also use for a run that walked the steps before it). A walk back never
    // goes to a point before it: the party never walked those steps in this run (Gnomeregan r27 slot 4, placed at
    // step 543: after the wipe at step 615 the walk back replayed the plan's points from step 53, 23 minutes).
    std::size_t startStep = 0;
    // The test speed (`.bot run start ... speed=<1.0-3.0>`, owner request 2026-10-25; set by the glue next to Start,
    // never changed by the run). Above 1.0 the glue gives the party's bots and pets this rate while the run walks
    // (SpeedRateNow); 1.0, every clear, touches nothing. The status line and the done note say it (SpeedLabel).
    float testSpeed = 1.0f;
    bool speedPlayerBlocked = false; // a real player was in the party on a tick: 1.0 for the rest of the run
    std::uint32_t speedOnMs = 0;     // the walking rule (SpeedWalking) has held this long without a break
    float speedNow = 1.0f;           // the rate of the last tick (the glue's SyncRunSpeed reads it)
    // The test speed's leash (SpeedLeashTick): the walk holds for the party, since when, its note, and whether this
    // step already spent its kGatherCapMs (no second hold until the next step; cleared only in ResetStep).
    bool speedLeash = false;
    std::uint32_t speedLeashMs = 0;
    bool speedLeashSpent = false;
    std::string speedLeashOrder; // the hold's own move on the lead bot's spot (OrderRefused tells it from the walk's)
    // Times a straight walk back moved its target to an earlier point after its path passed an uncleared pack
    // (ClearPointBefore, kStraightBackoffs), this walk back.
    std::uint32_t straightBackoffs = 0;
    // The retreat at two dead (RetreatTick, AiPlayerbot.DungeonRunRetreatAt2Dead): under way, its clock, the orders
    // it sent, and whether this fight had its one retreat.
    bool retreatOn = false;
    std::uint32_t retreatMs = 0;
    std::uint32_t retreatOrders = 0;
    bool retreatDone = false;
    // The rest under way ends at the light lines (RestLinesFor: the next fight is a small pull, or there is none ahead).
    bool restLight = false;
    // The step before was a pull or boss whose fight the party won (FightStep: "pack cleared", "boss down" after a
    // pull went out): the next tick may pull the next pack from where the tank stands (FightForwardTick).
    bool fightForwardDue = false;
    std::size_t fightForwardWanted = static_cast<std::size_t>(-1); // the pull step whose way the glue reads next tick
    std::size_t shortcutWanted = static_cast<std::size_t>(-1);     // the replay point whose path the glue reads next tick
    std::size_t shortcutFrom = static_cast<std::size_t>(-1);       // ... and the point reached before it
    bool bigPackDue = false;  // a big pack seen with the estimate off: rested to the full lines first (BigPackRest)
    std::size_t bigPackStep = static_cast<std::size_t>(-1); // ... once a step
    // The walk's own-movement watch (WalkTo, kWalkMovingCapMs): the lead bot's spot at its last progress, and how long
    // the walk has been kept alive by movement alone (no new best distance to the point).
    float walkMoveX = 0.0f, walkMoveY = 0.0f, walkMoveZ = 0.0f;
    std::uint32_t walkMovingMs = 0;
    // The route checkpoint (run-wide: kept across steps, wipes and regroups):
    // the plan index of the move or direct step point the lead bot reached
    // last, its step done or a walk back's point (npos: none yet). The lead
    // bot moves only on the navmesh between such points, and the navmesh
    // never crosses a direct step's hole, so while `routeKnown` holds it has
    // crossed every direct step up to the checkpoint and none after it
    // (StartWalkBack). The run cannot vouch for that once the lead bot died
    // (a release comes back at the entrance) or the run paused (a player may
    // move it): `routeCheck` then holds the spot it stood on, and on its next
    // living tick the checkpoint is known again only if it stands within
    // kRouteSpotYd of that spot (raised where it fell, not moved).
    std::size_t checkpoint = static_cast<std::size_t>(-1);
    bool routeKnown = false;
    bool routeCheck = false;
    float routeSpotX = 0.0f, routeSpotY = 0.0f, routeSpotZ = 0.0f;
    std::uint32_t walkBackRefusals = 0; // passing refusals of that point
    // The island escape (IslandEscape): the living lead bot's last spot a
    // navmesh path left (Facts::leadPathGood; run-wide) and whether this tick's
    // read was such a path, the straight hops off the island sent this step, the
    // hop under way, and where the last walk-back point was given up (two in a
    // row given up from one spot, with no good path read: islandSuspect).
    bool goodLeadKnown = false;
    float goodLeadX = 0.0f, goodLeadY = 0.0f, goodLeadZ = 0.0f;
    std::uint64_t goodLeadMs = 0; // runMs when it was read (GoodSpotFresh)
    std::uint64_t runMs = 0;      // the run's own clock: every running tick's dt
    // The hop off an island under way was sent by MoveStep's check of the
    // forward run's own move point (its no-spot falls back like a walk back's).
    bool escapeFromMove = false;
    bool leadPathGoodNow = false;
    std::uint32_t islandEscapes = 0;
    bool escapeOn = false;
    std::uint32_t escapeMs = 0;
    bool passSpotKnown = false;
    float passX = 0.0f, passY = 0.0f, passZ = 0.0f;
    bool islandSuspect = false;
    // The island's evidence (IslandConfirmed): a single partial reading, the
    // lead bot's cell and spot and the point it was read toward; cleared when
    // the lead bot leaves the cell or a good path is read.
    bool islandEvidence = false;
    int evidenceCellX = 0, evidenceCellY = 0;
    float evidenceX = 0.0f, evidenceY = 0.0f, evidenceZ = 0.0f;
    float evidenceTX = 0.0f, evidenceTY = 0.0f, evidenceTZ = 0.0f;
    // A walk back's hop that found no spot (no-spot), and where: from there the
    // walk back goes on as before (points passed, its own ceilings).
    bool noSpotKnown = false; // ... or the forward run's move check's (escapeFromMove)
    float noSpotX = 0.0f, noSpotY = 0.0f, noSpotZ = 0.0f;
    std::uint32_t stepEscapes = 0; // hops off an island this step (kStepEscapes)
    std::uint32_t islandRefusals = 0; // path refusals taken as the island's second sign this step (RefusedFromIsland)
    // The stranded lead bot's teleport (StrandedLead, owner decision 2026-10-23): due (the next walk-back tick out
    // of combat sends it), the plan step whose point it goes to, where the lead bot stood at the last send (read
    // afresh from the facts on each send), the plan step it succeeded on (at most once a step: ResetStep leaves
    // it, so a resume on the same step gets none), the teleport under way with its settle time, the summons of
    // the party left behind after it, and this step's failed tries and "wait" answers (strandCountStep: the step
    // they count for; ResetStep leaves them too).
    bool strandDue = false;
    std::size_t strandIdx = 0;
    float strandX = 0.0f, strandY = 0.0f, strandZ = 0.0f;
    std::size_t strandStep = static_cast<std::size_t>(-1);
    bool strandOn = false;
    std::uint32_t strandMs = 0;
    bool strandSummon = false;
    std::set<std::string> strandSummoned;
    std::uint32_t strandSummonMs = 0; // how long the summons waited for a bot left behind in a fight
    std::size_t strandCountStep = static_cast<std::size_t>(-1);
    std::uint32_t strandTries = 0;
    std::uint32_t strandWaits = 0;
    // The teleport due now (or just failed) is a ReplayRescue's (set by ReplayRescue beside RescueDue): when
    // StrandFailed gives up on it, the walk back replays the plan's points (ReplayWalkBack, spec 2B) instead of
    // hopping straight toward the target the run had just refused. Cleared when the lead bot arrives (its step's
    // teleport is used), by any other rescue, when the replay starts, and by ResetStep; a send keeps it (the
    // outcome is not known yet).
    bool strandReplay = false;
    // A pending teleport is a bot-only kind's (set by ReplayRescue, IslandWalkBackRescue and OtherPartRescue beside
    // RescueDue): a real player who joined the party since it was armed walks back with the party, so
    // StrandedTeleportTick drops it before the send (no try spent) and falls back as a refused rescue of that kind
    // does (an other-part rescue pauses for the other part instead). Cleared with
    // the due flag (ResetStep, ReplayWalkBack, the cancel), by any other rescue (RescueDue drops it; the three
    // bot-only kinds set their flag again after it), when the lead bot arrives (its step's teleport is used) and
    // when StrandFailed gives it up; a send keeps it (a refused send is tried again, and the recheck runs before
    // every send).
    bool strandBotOnly = false;
    // The pending teleport was armed by OtherPartRescue (set beside RescueDue there, with strandBotOnly): a real
    // player who joined the party since it was armed does not walk back with the party alone (the lead bot is in
    // another part), so StrandedTeleportTick drops it before the send (no try spent) and pauses for the other part
    // (PauseOtherPart with OtherPartBlock::Player: the old player-present pause and hold, no walk-back advance)
    // instead of passing the walk-back point. The island rescue keeps its behaviour (it passes the point).
    // Cleared with the due flag (ResetStep, ReplayWalkBack, the cancel), by any other rescue (RescueDue drops it;
    // OtherPartRescue sets it again after it), when the lead bot arrives (its step's teleport is used) and when
    // StrandFailed gives it up; a send keeps it (a refused send is tried again, and the recheck runs before
    // every send).
    bool strandFromOtherPart = false;
    // The return of a bot outside the instance (OffMapReturnTick, Dire Maul West r38 slot B): the run clock (runMs)
    // of each bot's last return, and each bot's returns at the step offMapReturnStep (kOffMapReturnTries a step and
    // bot; the pause names the bot that needed a fourth return; Resume starts every bot's count over).
    std::unordered_map<std::string, std::uint64_t> offMapReturnAt;
    std::size_t offMapReturnStep = static_cast<std::size_t>(-1);
    std::unordered_map<std::string, std::uint32_t> offMapReturnTries;
    // Round 2 (2026-10-24): bots outside the instance reported not returnable at this step (their note went out once
    // per bot per step, and an earlier copy of the same note is replaced, not appended to).
    std::size_t offMapNotedStep = static_cast<std::size_t>(-1);
    std::unordered_set<std::string> offMapNoted;
    // The walk-back point last sent, from where, and how often (kWalkBackResends).
    std::size_t resendIdx = static_cast<std::size_t>(-1);
    float resendX = 0.0f, resendY = 0.0f, resendZ = 0.0f;
    std::uint32_t resends = 0;
    // A scripted event's hold (Hold steps): its clock runs from the first look,
    // in fights too; a regroup or resume starts it over.
    bool eventOn = false;
    std::uint32_t eventResetWaitMs = 0; // a ritual waiting for its encounter to reset (kEventResetWaitMs)
    std::uint32_t eventMs = 0;
    bool eventStarted = false;   // the done data left "not started", or an event unit was seen awake
    bool holdUnitSeen = false;   // a units hold (no done flag): a unit of it stood alive in the radius at some tick of this hold
    bool holdArrived = false;    // the party stood on the hold spot once
    // The party's places on the hold spot (HoldStands), set once the lead bot
    // stands there out of a fight and again after each fight; the glue places
    // the named bots whenever holdStandsGen changes and lets them follow the
    // lead bot again once the list is empty (the step's end, a pause).
    std::vector<HoldStand> holdStands;
    std::uint32_t holdStandsGen = 0;
    bool holdPlaced = false;
    std::uint32_t holdReleases = 0; // times a hold's fight let the places go (ReleaseHoldStands)
    std::uint32_t holdFarTicks = 0; // fight ticks in a row with a ranged bot out of the tank's reach and the tank off the spot
    // The pause came from a player (an order to the party, `.bot run pause`),
    // not from the run: it never resumes by itself (HoldDoneWhilePaused).
    bool pausedByPlayer = false;
    // The pause is the fight ceiling's (kFightCapMs): once the party is out of
    // combat for kCeilingQuietMs the run goes on by itself, once per step
    // (CeilingFightOverWhilePaused). Any other pause, Resume and Skip clear it.
    bool ceilingPaused = false;
    std::uint32_t ceilingQuietMs = 0;
    // The pause is the lead bot's off the plan's map (PauseLeadOffMap): once the
    // lead bot is back on it for kLeadBackSettleMs the run goes on by itself,
    // once per step (LeadBackWhilePaused). Any other pause, Resume and Skip clear it.
    bool leadOffMapPaused = false;
    std::uint32_t leadBackMs = 0;
    // The pause is the 5 min player hold's (PausePlayerHold): once the player is
    // back with the party for kPlayerHoldBackSettleMs the run goes on by itself,
    // once per step (PlayerHoldBackWhilePaused). Any other pause, Resume and Skip clear it.
    bool playerHoldPaused = false;
    std::uint32_t playerHoldBackMs = 0;
    // The last tick's combat was with units the group cannot attack only (UnattackableOnlyFight): the note
    // about it is made on the first tick of each such stretch.
    bool unattackableFight = false;
    // UnattackableGraceTick: the engaged units (guids, sorted) of the current only-unattackable stretch, and
    // how long that same set has stood so without a break.
    std::vector<std::uint64_t> unattackableSet;
    std::uint32_t unattackableMs = 0;
    // CombatDropTick: per bot, the engaged units (guids, sorted) of its current only-unattackable or only-far-held
    // stretch, which kind it is, and how long that same set has stood so as that kind; the run time of its last
    // combat drop; and this tick's drops, which the glue carries out (CombatStop on the bot and its pet) after Tick.
    // One unit first UNATTACKABLE then attackable but far away (a phase ending far off) changes kind: the timer
    // starts over, so a carried-over stretch never drops early.
    enum class CombatDropKind : std::uint8_t
    {
        Unattackable,
        FarHeld
    };
    struct CombatDropState
    {
        std::vector<std::uint64_t> set;
        CombatDropKind kind = CombatDropKind::Unattackable;
        std::uint32_t ms = 0;
    };
    std::unordered_map<std::string, CombatDropState> combatDrop;
    std::unordered_map<std::string, std::uint64_t> combatDroppedAt;
    std::vector<std::string> combatDrops;
    // HpHeldTick: per engaged unit (guid) at 1 hp (or 1 % or less) its entry, the hit points it stands at, and how
    // long a party member has had it as its victim since without a change. Cleared out of combat and by ResetStep.
    struct HpWatch
    {
        std::uint32_t entry = 0;
        std::uint32_t hp = 0;
        std::uint32_t ms = 0;
    };
    std::unordered_map<std::uint64_t, HpWatch> hpWatch;
    // FightProgressTick: per unit engaged in this fight (guid) the low mark of its hp (tenths of a percent of its
    // most): the hp it last set a new low at, kFightProgressPermille or more under the mark before, and when, by the
    // watch's own clock (fight ticks watched); the dead count of the last tick and when it last rose. Cleared out of
    // combat and by ResetStep (ClearFightProgress).
    struct HpLow
    {
        std::uint32_t low = 0;
        std::uint32_t at = 0;
        bool fell = false;     // it has set a new low in this fight while hittable (at is that time)
        bool hittable = false; // ... on its last read: attackable or a party member's victim
        bool inRead = false;   // ... and it was in the last tick's read
    };
    std::unordered_map<std::uint64_t, HpLow> fightHp;
    std::uint32_t fightHpMs = 0;
    std::uint32_t fightHpDead = 0;
    bool fightHpDeath = false;
    std::uint32_t fightHpDeathAt = 0;
    std::size_t ceilingResumedStep = static_cast<std::size_t>(-1); // the step that has used its one self-resume (ResetStep keeps it)
    std::size_t leadBackResumedStep = static_cast<std::size_t>(-1); // the step that has used its one lead-back self-resume (ResetStep keeps it)
    std::size_t playerHoldResumedStep = static_cast<std::size_t>(-1); // the step that has used its one player-hold self-resume (ResetStep keeps it)
    std::uint32_t eventIdleMs = 0; // out of a fight with an awake event unit standing
    std::uint32_t eventUp = 0, eventDown = 0, eventTotal = 0; // the event's units now: awake, dead, all seen
    // Run-wide counts for the report.
    std::uint32_t wipes = 0;     // the whole party dead, counted once each time
    bool wiped = false;
    std::uint32_t nearWipes = 0; // kWipeDead or more dead with someone alive, counted once each time
    bool nearWiped = false;
    std::uint32_t leadDeaths = 0;   // the lead bot's deaths (a living to dead edge of Facts::leadAlive), run-wide
    bool leadDown = false;          // ... the lead bot was dead on the last running tick
    bool windowWiped = false;       // a window wipe was counted; re-armed once fewer than kWipeDead are dead
    std::uint64_t lastWipeMs = 0;   // runMs of the last counted wipe (either kind)
    std::unordered_map<std::string, std::uint64_t> diedAtMs; // each bot's last living to dead edge (runMs)
    std::unordered_set<std::string> deadNow;                  // Facts::deadNames on the last running tick
    bool eventRetry = false;     // an event failed this run: a ritual sent now is its retry
    std::unordered_set<std::size_t> eventRearmed; // start steps re-armed by a wipe regroup (once each)
    // Guarding the party from a stand point (GuardTarget): the time the party
    // has been hit with the tank idle on stay, the pick-up orders this fight
    // sent, and the walk back to the stand point after the fight.
    std::uint32_t guardMs = 0;
    std::uint32_t guardOrders = 0;
    std::uint32_t guardSinceOrderMs = 0; // fight time since the last pick-up
    bool guardReturn = false;
    // The tank's reach for a pull is cut off once a mob has aggro (CheckReach):
    // `stopReachDue` asks the glue to end the lead bot's pull, once per pull.
    bool stopReachDue = false;
    bool reachStopped = false;
    float guardX = 0.0f, guardY = 0.0f, guardZ = 0.0f; // the tank's spot at the fight's first pick-up
    // In a hold (HoldGuardTarget, HoldEngageTarget): the event's dead count at
    // the last reset of guardOrders (each unit of the event gets its own
    // orders), and whether an order took the tank off its stay on the hold
    // spot (out of the fight the hold puts it back on it).
    std::uint32_t guardEventDown = 0;
    bool holdUnstayed = false;
    std::uint64_t holdEngageGuid = 0; // the unit the last engage order sent the tank at
    std::uint64_t engageGuid = 0;     // the mob the last engage-on-arrival order (EngageTarget) sent the tank at
    std::uint32_t engageOrders = 0;   // engage orders this fight (kGuardOrdersPerFight at most; guardOrders is not touched)
    // The budget's refill outside a hold (CheckGuard): the units seen attacking
    // the party this fight (partyAttackers, the lead bot's attacker), the orders
    // of either kind sent at each, and all orders this fight, refills included
    // (kGuardOrdersPerUnit, kGuardOrdersFightCap).
    std::unordered_set<std::uint64_t> guardSeen;
    std::unordered_map<std::uint64_t, std::uint32_t> guardUnitOrders;
    std::uint32_t fightOrders = 0;
    bool guardRefilled = false; // a refill since the last order: that order's note says so
    std::uint32_t guardRefills = 0; // refills this fight: the per-unit cap applies only once there was one
    // Members running back to the party with attackers on them (RunBack): the
    // glue keeps each one a passive follower of the lead bot (it does not stop
    // to fight) while its name is here. Per bot: how long it has run, and its
    // stall sample.
    struct RunBackState
    {
        std::uint32_t ms = 0;
        std::uint32_t stillMs = 0;
        float bestYd = 0.0f;
    };
    std::vector<std::string> runBack;
    std::unordered_map<std::string, RunBackState> runBackState;
    std::unordered_set<std::string> runBackDone; // stalled or out of time in this wait: not again until it ends
    std::uint32_t runBacks = 0;  // run backs this run (the report)
};

inline const PlanStep* Current(const Run& r)
{
    return r.plan && r.step < r.plan->steps.size() ? &r.plan->steps[r.step] : nullptr;
}

// The hold a start step leads into: the current step is a ritual, open,
// use_item, gossip or move, and the next steps up to a hold are moves only. Its event
// may already be fighting (the Stone Keepers wake with the ritual's cast), so
// such a fight is bounded as the hold's. Null for none.
inline const PlanStep* EventHoldAhead(const Run& r)
{
    const PlanStep* s = Current(r);
    if (!s || (s->kind != PlanStepKind::Ritual && s->kind != PlanStepKind::Open && s->kind != PlanStepKind::UseItem &&
            s->kind != PlanStepKind::Gossip && s->kind != PlanStepKind::UseOn && s->kind != PlanStepKind::Move &&
            s->kind != PlanStepKind::MoveDirect))
        return nullptr;
    for (std::size_t i = r.step + 1; i < r.plan->steps.size(); ++i)
    {
        const PlanStep& next = r.plan->steps[i];
        if (next.kind == PlanStepKind::Hold)
            return &next;
        if (next.kind != PlanStepKind::Move && next.kind != PlanStepKind::MoveDirect)
            return nullptr;
    }
    return nullptr;
}

// The run is inside a scripted event or leading into one: a hold, its
// ritual or start step, the moves up to the hold, or an event already on.
// The event's quiet gaps between waves are not the end of a fight.
inline bool InEvent(const Run& r)
{
    const PlanStep* s = Current(r);
    return r.eventOn || (s && (s->kind == PlanStepKind::Hold || s->kind == PlanStepKind::Ritual)) ||
        EventHoldAhead(r) != nullptr;
}

// A ritual refused because its object is still active ("already in
// progress") after the event failed: the core leaves a summoning ritual active
// after its last use, so the retry cannot be started in this instance.
inline bool SpentRitual(const Run& r, const std::string& order, const std::string& answer)
{
    return r.eventRetry && order.compare(0, 7, "ritual ") == 0 && answer.compare(0, 4, "busy") == 0 &&
        answer.find("already in progress") != std::string::npos;
}

inline std::string SpentRitualNote(const std::string& what)
{
    return what + " refused: the altar is still active from the failed try, so it is spent; an instance reset is "
        "needed to retry the event (.bot run skip goes on without it)";
}

inline std::string Num(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
}

inline std::string MoveOrder(float x, float y, float z, bool direct)
{
    return "move " + Num(x) + " " + Num(y) + " " + Num(z) + (direct ? " direct" : "");
}

// The run leader's own walk of one bot (not a `cmd`: the glue's RunBotWalk): the bot walks to the point on its own
// navmesh path and holds there until the party's next move or its next fight (StragglerLadderTick, SapWait).
inline std::string WalkOrder(const std::string& bot, float x, float y, float z)
{
    return "walk " + bot + " " + Num(x) + " " + Num(y) + " " + Num(z);
}

// A raw object guid as an order token: "0x" + 16 hex digits (RemoteCommander::ParseGuid).
inline std::string GuidArg(std::uint64_t guid)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(guid));
    return buf;
}

inline std::string EntryArg(std::uint32_t entry, float x, float y, float z)
{
    return "entry:" + std::to_string(entry) + "@" + Num(x) + "," + Num(y) + "," + Num(z);
}

inline void EnterPhase(Run& r, Phase p)
{
    r.phase = p;
    r.phaseMs = 0;
}

// The pull stand's state of one step (HoldForShot).
inline void ResetShotStand(Run& r)
{
    r.shotStandFor = 0;
    r.shotStandTries.clear();
    r.shotStandWalk = false;
    r.shotStandGuid = 0;
    r.shotStandX = r.shotStandY = r.shotStandZ = 0.0f;
    r.shotStillMs = 0;
    r.shotWalkMs = 0;
    r.shotStandPrep = false;
    r.shotHoldGuid = 0;
    r.shotHow.clear();
}

// The big pack's stand walk gate is over (the pull went out, a fight, the step).
inline void EndBigWalk(Run& r)
{
    r.bigWalk = false;
    r.bigWalkGuid = 0;
    r.bigJoiners = 0;
    r.bigHold = false;
    r.bigHoldMs = 0;
    r.bigHoldSpent = false;
    r.bigHoldNote.clear();
}

// A tick the pull stand's gate did not run on ends its hold (the stand tries
// already spent stay counted): no stand request is left for the glue to
// answer, and the next pull checks the way back and counts as usual.
inline void EndShotHold(Run& r)
{
    r.shotStandFor = 0;
    r.shotStandWalk = false;
    r.shotStillMs = 0;
    r.shotWalkMs = 0;
    r.shotStandPrep = false;
}

inline void ClearRunBack(Run& r)
{
    r.runBack.clear();
    r.runBackState.clear();
    r.runBackDone.clear();
}

// The lead bot reached the point of plan step `idx` (a move or direct step
// done, or a walk back's point): the route checkpoint (Run::checkpoint).
inline void NoteCheckpoint(Run& r, std::size_t idx)
{
    r.checkpoint = idx;
    r.routeKnown = true;
    r.routeCheck = false;
}

// A walk to the point of plan step `idx` is done (WalkTo): its checkpoint,
// except a direct step's walk that stopped short of its point (WalkTo also
// ends a stalled walk within kStalledArriveYd, and a few yards short of a
// direct point can still be the near side of its hole).
inline void NoteReached(Run& r, const Facts& f, std::size_t idx)
{
    const PlanStep& p = r.plan->steps[idx];
    if (p.kind == PlanStepKind::MoveDirect && ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) > kArriveYd)
        return;
    NoteCheckpoint(r, idx);
}

// The run can no longer vouch that the lead bot stays on its checkpoint's
// stretch (it died, or the run paused): its last living spot is kept, and the
// next living tick compares (CheckRoute).
inline void SuspendRoute(Run& r)
{
    if (!r.routeKnown || r.routeCheck)
        return;
    if (!r.leadSeen)
    {
        r.routeKnown = false;
        return;
    }
    r.routeCheck = true;
    r.routeSpotX = r.leadLastX;
    r.routeSpotY = r.leadLastY;
    r.routeSpotZ = r.leadLastZ;
}

// Whether the route checkpoint tells where the lead bot is (Run::checkpoint):
// a point reached, nothing since that could move it off that stretch, and
// before the current step (an event that failed sends the run back to an
// earlier step; a checkpoint past the step says nothing about the way to it).
inline bool CheckpointKnown(const Run& r)
{
    return r.routeKnown && r.checkpoint != static_cast<std::size_t>(-1) && r.checkpoint < r.step;
}

// Whether plan step `idx` is a direct step the lead bot has not crossed (it is
// after the checkpoint, or the checkpoint is not known): a walk back never
// passes one on a failure (its hole would be skipped), it pauses instead.
inline bool DirectStillToCross(const Run& r, std::size_t idx)
{
    if (!r.plan || idx >= r.plan->steps.size() || r.plan->steps[idx].kind != PlanStepKind::MoveDirect)
        return false;
    return !(CheckpointKnown(r) && idx <= r.checkpoint);
}

inline void DropFocus(Run& r);

inline void Pause(Run& r, const std::string& why)
{
    DropFocus(r); // a pause leaves nobody's target skull-marked: a resume picks a focus again as usual
    SuspendRoute(r); // a player may move the lead bot while the run is paused
    r.exactProved = static_cast<std::size_t>(-1); // ... so an exact point is proved again after a resume
    r.state = State::Paused;
    r.note = why;
    r.restWalkDue = false; // a resume regroups and walks afresh
    r.pausedByPlayer = false; // PausePlayer sets it after
    r.ceilingPaused = false; // the fight ceiling sets it after
    r.ceilingQuietMs = 0;
    r.leadOffMapPaused = false; // PauseLeadOffMap sets it after
    r.leadBackMs = 0;
    r.playerHoldPaused = false; // PausePlayerHold sets it after
    r.playerHoldBackMs = 0;
    r.doorPaused = false; // PauseDoor sets it after
    ClearRunBack(r); // a paused run leaves every bot to its own AI
    r.hold.clear();
    r.holdDue = false;
    r.holdWhilePaused = false;
    r.otherPartPaused = false; // PauseOtherPart sets it after
    r.otherPartResumeRescue = false; // ... and this one
    r.holdStands.clear();
    r.holdPlaced = false;
    r.holdFarTicks = 0;
}

// Whether the run's hold (Run::hold) keeps its bots on stay now: while the run
// runs, and in the pause for a lead bot in another part (PauseOtherPart).
inline bool HoldStandsNow(const Run& r)
{
    return r.state == State::Running || (r.state == State::Paused && r.holdWhilePaused);
}

// Clears an exact waypoint's tighten (not its standing proof, exactProved).
inline void ResetExact(Run& r)
{
    r.exactOn = false;
    r.exactMs = 0;
    r.exactRounds = 0;
    r.exactWaitMs = 0;
    r.exactStage = 0;
    r.exactLastSet = false;
    r.exactAnchorSet = false;
    r.exactStillMs = 0;
}

// Plan step `idx` is an exact waypoint (a move with ai::kPlanFlagExact).
inline bool IsExactPoint(const DungeonPlan* plan, std::size_t idx)
{
    return plan && idx < plan->steps.size() && plan->steps[idx].kind == PlanStepKind::Move &&
        (plan->steps[idx].flags & ai::kPlanFlagExact) != 0;
}

// The exact waypoint a direct step at `idx` is launched from: the step right
// before it when that is one, else npos.
inline std::size_t ExactLaunch(const DungeonPlan* plan, std::size_t idx)
{
    if (!plan || idx == 0 || idx >= plan->steps.size() || plan->steps[idx].kind != PlanStepKind::MoveDirect)
        return static_cast<std::size_t>(-1);
    return IsExactPoint(plan, idx - 1) ? idx - 1 : static_cast<std::size_t>(-1);
}

// A move_hop_exact (PlanStep::exactLaunch) with no exact move right before
// it in its leg: rows the exporter would never write. Its hop is not sent (the
// run pauses), as it would not be on a module that does not know the kind.
inline bool ExactLaunchBroken(const DungeonPlan* plan, std::size_t idx)
{
    if (!plan || idx >= plan->steps.size() || !plan->steps[idx].exactLaunch)
        return false;
    if (idx == 0)
        return true;
    return !IsExactPoint(plan, idx - 1) || plan->steps[idx - 1].leg != plan->steps[idx].leg;
}

inline std::string ExactLaunchBrokenNote(std::size_t idx)
{
    return "step " + std::to_string(idx + 1) + " is a move_hop_exact with no exact move (flags 64) right before it in " +
        "its leg: the hop is not sent; export the plan again (.bot run skip passes it)";
}

// Forgets the fight ceiling's hp watch (FightProgressTick).
inline void ClearFightProgress(Run& r)
{
    r.fightHp.clear();
    r.fightHpMs = 0;
    r.fightHpDead = 0;
    r.fightHpDeath = false;
    r.fightHpDeathAt = 0;
}

// The run's kill_first focus is over: the unit its skull is on (Run::focusIconGuid) is handed to the glue to take
// the skull off (Run::focusIconClear; only where it still is: EndRunIcon), and the focus is forgotten. A pending
// new focus (Run::prevFocusIconGuid: the old unit, whose skull the refused new order never replaced) is kept next
// to it: the glue reads all three recorded GUIDs (both skulls must clear) and only zeroes them once EndRunIcon
// reports success or drop for each.
inline void DropFocus(Run& r)
{
    r.focusIconClear = r.focusIconGuid;
    r.focusIconGuid = 0;
    r.focusGuid = 0;
}

// Clears what belongs to one step (also on resume, so a resumed step gets
// fresh tries and ceilings).
inline void ResetStep(Run& r)
{
    EnterPhase(r, Phase::Begin);
    r.stepIdleMs = 0;
    r.restDue = true;
    r.eatWaitMs = 0;
    r.restForEating = false;
    r.stepRested = false;
    r.orderSent = false;
    r.taskResends = 0;
    r.proofWaitMs = 0;
    r.taskBot.clear();
    r.lootTried.clear();
    r.lootRefusal.clear();
    r.lootRights = false;
    r.lootFfa = LootFfa::Idle; // lootFfaLive stays: the next running tick orders the method back (LootFfaRestoreTick)
    r.lootFfaUsed = false;
    r.dataAtOrder = -1;
    r.walkedThrough = false;
    r.approachHops = 0;
    r.approachBestYd = 0.0f;
    r.hopWalk = false;
    r.hopWaiveUnits.clear();
    r.detour = false;
    r.walkTries = 0;
    r.walkStillMs = 0;
    r.walkBestYd = 0.0f;
    r.walkX = r.walkY = r.walkZ = 0.0f;
    ResetExact(r);
    r.pullGuid = 0;
    r.pulls = 0;
    r.pullTries.clear();
    r.passed.clear();
    r.pullHop = false;
    r.openAtLastPull = 0;
    r.fruitlessPulls = 0;
    r.bossWaitMs = 0;
    r.bossSeenAlive = false;
    r.bossSearchWanted = false;
    // The 600 s is cumulative per step across pauses/resumes and wipe recoveries: Resume (and every wipe path above)
    // calls ResetStep on the SAME step, so the timer stays; NextStep moves on, so a new step starts it over.
    if (r.step != r.standPullStep)
    {
        r.standPullStep = r.step;
        r.standPullMs = 0;
    }
    r.standPullShotGuid = 0;
    r.hpWatch.clear();
    ClearFightProgress(r);
    r.patrolWaitMs = 0;
    r.patrolSeen.clear();
    EndBigWalk(r);
    r.joinerSeen.clear();
    r.walkBackPulls = 0;
    r.gatherOrder.clear();
    r.gatherRefusals = 0;
    r.gatherWaived = false;
    r.summoned.clear();
    r.gatherComingMs = 0;
    r.summonDue.clear();
    r.summonWait.clear();
    r.summonWaitMs = 0;
    r.stepSummons.clear();
    r.directRefusedOn = false;
    r.directRefusedMs = 0;
    r.directRefusedIdx = 0;
    r.directJoinHolds = 0;
    r.directWaitFor.clear();
    r.directWaitMs = 0;
    r.memberYd.clear();
    r.ccSent.clear();
    r.joinerHeld.clear();
    r.joinerRest = false;
    r.restForJoiners = false;
    r.joinerRests = 0;
    r.joinerNote.clear();
    ResetShotStand(r);
    DropFocus(r);
    r.focusRefused.clear();
    r.focusUnstayed = false;
    r.ccPending = 0;
    r.ccResent = false;
    r.ccWaitMs = 0;
    r.ccHolds.clear();
    r.ccWatch.clear();
    r.ccLandedDrop.clear();
    r.ccSapFailed = false;
    r.ccRegather = false;
    r.sapWalkDue.clear();
    r.straggle.clear();
    r.walkBackDue = false;
    r.walkBack = false;
    r.walkBackDirect = false;
    r.walkBackIdx = 0;
    r.walkBackStart = 0;
    r.walkBackRestarted = false;
    r.walkBackRefusals = 0;
    r.islandEscapes = 0; // a resume hops the lead bot off an island again
    r.escapeOn = false;
    r.escapeMs = 0;
    r.passSpotKnown = false;
    r.islandSuspect = false;
    r.islandEvidence = false;
    r.noSpotKnown = false;
    r.stepEscapes = 0;
    r.islandRefusals = 0;
    r.strandDue = false;
    r.strandOn = false;
    r.strandReplay = false;
    r.strandBotOnly = false;
    r.strandFromOtherPart = false;
    r.strandMs = 0;
    r.strandSummon = false;
    r.strandSummoned.clear();
    r.strandSummonMs = 0;
    r.escapeFromMove = false;
    r.resendIdx = static_cast<std::size_t>(-1);
    r.resends = 0;
    r.eventOn = false;
    r.eventResetWaitMs = 0;
    r.eventMs = 0;
    r.eventStarted = false;
    r.holdUnitSeen = false;
    r.holdArrived = false;
    r.eventIdleMs = 0;
    r.eventUp = r.eventDown = r.eventTotal = 0;
    r.holdStands.clear();
    r.holdPlaced = false;
    r.holdFarTicks = 0;
    r.guardMs = 0;
    r.guardOrders = 0;
    r.engageGuid = 0;
    r.engageOrders = 0;
    r.guardSeen.clear();
    r.guardUnitOrders.clear();
    r.guardRefilled = false;
    r.guardRefills = 0;
    r.fightOrders = 0;
    r.guardReturn = false;
    r.stopReachDue = false;
    r.reachStopped = false;
    r.guardEventDown = 0;
    r.holdUnstayed = false;
    r.holdEngageGuid = 0;
    r.hold.clear();
    r.holdDue = false;
    r.speedLeash = false;
    r.speedLeashMs = 0;
    r.speedLeashSpent = false;
    r.speedLeashOrder.clear();
}

// "N wipe(s), M near wipe(s)", or "" for none.
inline std::string WipeCounts(const Run& r)
{
    std::string out;
    if (r.wipes)
        out = std::to_string(r.wipes) + (r.wipes == 1 ? " wipe" : " wipes");
    if (r.nearWipes)
        out += (out.empty() ? "" : ", ") + std::to_string(r.nearWipes) + (r.nearWipes == 1 ? " near wipe" : " near wipes");
    return out;
}

// "N lead death(s)", or "" for none.
inline std::string LeadDeathCount(const Run& r)
{
    if (!r.leadDeaths)
        return std::string();
    return std::to_string(r.leadDeaths) + (r.leadDeaths == 1 ? " lead death" : " lead deaths");
}

// The deferred pulls the run left alive (Run::deferGivenUp: not reachable from the floor
// the party took, skipped at the end of their leg, on a failed retry or at a stop), for the
// run's end: "" when there are none.
inline std::string DeferredReport(const Run& r)
{
    if (r.deferGivenUp.empty() || !r.plan)
        return std::string();
    std::string out = std::to_string(r.deferGivenUp.size()) + (r.deferGivenUp.size() == 1 ? " pack" : " packs") +
        " left alive (deferred, not reachable from this floor):";
    for (std::size_t i = 0; i < r.deferGivenUp.size(); ++i)
    {
        std::size_t const idx = r.deferGivenUp[i];
        out += std::string(i ? "," : "") + " step " + std::to_string(idx + 1);
        if (idx < r.plan->steps.size())
        {
            const PlanStep& s = r.plan->steps[idx];
            out += " at " + Num(s.x) + " " + Num(s.y) + " " + Num(s.z);
        }
    }
    return out;
}

// The test speed for the lines a reader counts clears by (StatusLine's last field, DoneNote): "" at 1.0.
inline std::string SpeedLabel(const Run& r)
{
    if (r.testSpeed <= 1.0f)
        return std::string();
    return "speed " + Num(r.testSpeed) + (r.speedPlayerBlocked ? " (test; 1.0 since a real player joined)" : " (test)");
}
// Forward: the item-need gates (spec item-leg-skip C2/C2b, defined beside
// PausePlayer, past NextStep) for the reports above.
inline std::string SkippedLegsReport(const Run& r);

inline bool InSkippedLeg(const Run& r, std::uint8_t leg)
{
    return std::find(r.skippedLegs.begin(), r.skippedLegs.end(), leg) != r.skippedLegs.end();
}
inline bool InSkippedLegAt(const Run& r, std::size_t i)
{
    return r.plan && i < r.plan->steps.size() && InSkippedLeg(r, r.plan->steps[i].leg);
}

// The boss steps the run ended without clearing: still in notCleared (their
// pull or boss end or a `.bot run skip` put them there), never deferred (a
// deferral is a pull, listed by DeferredReport instead). A hidden live boss
// is never fine, so the plan's end line names every step left here: a boss
// killed later outside his own step is not reconciled (the trace outcome
// counts kills). "" when there are none. A boss the run comes back to on his
// own step and clears is out of notCleared again, so he is not named.
inline std::string UnclearedBosses(const Run& r)
{
    if (!r.plan)
        return std::string();
    std::string out;
    for (std::size_t i = 0; i < r.plan->steps.size(); ++i)
    {
        if (!r.notCleared.count(i))
            continue;
        if (InSkippedLegAt(r, i))
            continue; // an optional leg's skip says so in its own note and report
        const PlanStep& s = r.plan->steps[i];
        if (s.kind != PlanStepKind::Boss)
            continue; // pulls: a deferral names them in DeferredReport, a skip says so in its own note
        out += std::string(out.empty() ? "" : ", ") + "boss " + std::to_string(s.entry) + " (step " + std::to_string(i + 1) + ")";
    }
    return out;
}

inline std::string DoneNote(const Run& r)
{
    std::string counts = WipeCounts(r);
    std::string const lead = LeadDeathCount(r);
    if (!lead.empty())
        counts += (counts.empty() ? "" : ", ") + lead;
    std::string const skipped = SkippedLegsReport(r);
    std::string all = counts.empty() ? skipped : skipped.empty() ? counts : counts + "; " + skipped;
    std::string const deferred = DeferredReport(r);
    all = all.empty() ? deferred : deferred.empty() ? all : all + "; " + deferred;
    std::string const speed = SpeedLabel(r);
    if (!speed.empty())
        all = all.empty() ? speed : speed + "; " + all;
    std::string const bosses = UnclearedBosses(r);
    std::string const named = bosses.empty() ? std::string() : "boss skipped, not seen killed: " + bosses;
    std::string const done = all.empty() ? named : named.empty() ? all : all + "; " + named;
    return done.empty() ? std::string("plan done") : "plan done (" + done + ")";
}
// Each bot's distance to the lead bot now: the sample a gather's ceiling
// compares with (taken when the gather goes out and at each ceiling).
inline void SampleMembers(Run& r, const Facts& f)
{
    for (const MemberFact& m : f.members)
        r.memberYd[m.name] = m.yd;
}

// Each living bot's stand-still watch, fed every tick: a bot that moved
// kStillYd from its watch spot starts a fresh clock there; one that did not
// adds the tick. Bots not in the facts (dead, gone) are dropped, so a revived
// bot starts afresh.
inline void UpdateStill(Run& r, const Facts& f, std::uint32_t dt)
{
    for (auto it = r.still.begin(); it != r.still.end();)
    {
        bool present = false;
        for (const MemberFact& m : f.members)
            if (m.name == it->first)
            {
                present = true;
                break;
            }
        it = present ? std::next(it) : r.still.erase(it);
    }
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty())
            continue;
        auto it = r.still.find(m.name);
        if (it == r.still.end() || ai::PlanDistance(m.x, m.y, m.z, it->second.x, it->second.y, it->second.z) >= kStillYd)
        {
            Run::StillState& st = r.still[m.name];
            st.x = m.x;
            st.y = m.y;
            st.z = m.z;
            st.ms = 0;
            continue;
        }
        it->second.ms += dt;
    }
}

// The bot has moved less than kStillYd for kStillMs.
inline bool IsStill(const Run& r, const std::string& name)
{
    auto it = r.still.find(name);
    return it != r.still.end() && it->second.ms >= kStillMs;
}

// A summon of `name` goes out this tick: once per step and regroup for the summons that keep that rule
// (Run::summoned), and counted with its time for a direct step's straggler (Run::stepSummons).
inline void MarkSummoned(Run& r, const std::string& name)
{
    r.summoned.insert(name);
    Run::StepSummon& s = r.stepSummons[name];
    ++s.count;
    s.lastMs = r.runMs;
}

// The run clock a bot was last seen dead (every tick): a bot that died and came back after its last summon is
// a new case for a direct step's summon (DirectSummonAllowed).
inline void NoteDeadSeen(Run& r, const Facts& f)
{
    for (const std::string& name : f.deadNames)
        r.deadSeenMs[name] = r.runMs;
}

// r12-19z29 Uldaman leg 5 (2026-10-22): a direct step's straggler summoned already this step (the mid-step
// revive's summon) was never summoned again, so its too-far refusal paused the run, and the paused run stopped
// guarding: two more deaths. It may be summoned again once more than kDirectResummonMs passed since its last
// summon of the step, or once it died and came back since then; never more than kDirectSummonCap times a step.
inline bool DirectSummonAllowed(const Run& r, const std::string& name)
{
    auto it = r.stepSummons.find(name);
    if (it == r.stepSummons.end() || it->second.count == 0)
        return true;
    if (it->second.count >= kDirectSummonCap)
        return false;
    auto dead = r.deadSeenMs.find(name);
    if (dead != r.deadSeenMs.end() && dead->second >= it->second.lastMs)
        return true;
    return r.runMs - it->second.lastMs > kDirectResummonMs;
}

// The step's walk points: what a walk back after a regroup replays.
inline bool IsMovePoint(const PlanStep& s)
{
    return s.kind == PlanStepKind::Move || s.kind == PlanStepKind::MoveDirect;
}

// Whether a plan step of leg `leg` is in a skipped leg (spec item-leg-skip C2:
// never walked by a walk back or replayed).
// A move point a walk back replays: a walk point that is not in a skipped leg.
inline bool IsRunMovePoint(const Run& r, std::size_t i)
{
    return r.plan && i < r.plan->steps.size() && IsMovePoint(r.plan->steps[i]) && !InSkippedLegAt(r, i);
}

inline bool InSkippedLegAt(const Run& r, std::size_t i);

// The walk back's next point as WalkBack reaches it on its next tick: it passes the steps that are not walk
// points (a pull, a rest) in the same tick, so the facts must be read for the point after them. -1: none
// (the walk back is over, or none is on).
inline std::size_t NextWalkBackIdx(const Run& r)
{
    if (!r.walkBack || !r.plan)
        return static_cast<std::size_t>(-1);
    std::size_t i = r.walkBackIdx;
    while (i < r.step && i < r.plan->steps.size() && !IsRunMovePoint(r, i))
        ++i;
    return i < r.step && i < r.plan->steps.size() ? i : static_cast<std::size_t>(-1);
}

// The point a direct step's trouble belongs to: the walk back's point, else the step's own.
inline std::size_t DirectTroublePoint(const Run& r)
{
    std::size_t const back = NextWalkBackIdx(r);
    return back != static_cast<std::size_t>(-1) ? back : r.step;
}

// The first too-far refusal (or direct gather ceiling) at a point starts the clock DirectPauseDue reads; a walk
// back that went on to another point starts it over there.
inline void NoteDirectTrouble(Run& r)
{
    std::size_t const point = DirectTroublePoint(r);
    if (r.directRefusedOn && r.directRefusedIdx == point)
        return;
    r.directRefusedOn = true;
    r.directRefusedMs = r.runMs;
    r.directRefusedIdx = point;
}

// A direct step's straggler pauses the run only after kDirectPauseSummons summons of it this step and
// kDirectPauseMs since the first refusal at this point, or after 2 x kDirectPauseMs whatever the summons (a
// straggler held in a fight is never summoned: the time bound); until then the run waits and keeps guarding.
inline bool DirectPauseDue(const Run& r, const std::string& name)
{
    if (!r.directRefusedOn)
        return false;
    auto it = r.stepSummons.find(name);
    std::uint32_t const summons = it == r.stepSummons.end() ? 0 : it->second.count;
    std::uint64_t const since = r.runMs - r.directRefusedMs;
    return (summons >= kDirectPauseSummons && since >= kDirectPauseMs) || since >= 2ull * kDirectPauseMs;
}

// The point of the direct step the gate before it guards, when there is one: the walk back's next point (as
// WalkBack reaches it: NextWalkBackIdx) when it is a direct one, else the current step when it is a direct step.
// Not while a walk is under way (the gate is not asked then). While this holds the glue reads
// MemberFact::withLead (one navmesh path per follower within kGatherYd) with that point for the bot's own line.
inline bool DirectJoinWanted(const Run& r, float& x, float& y, float& z)
{
    if (r.state != State::Running || !r.plan || r.phase == Phase::Walk)
        return false;
    const PlanStep* s = nullptr;
    std::size_t const back = NextWalkBackIdx(r);
    if (back != static_cast<std::size_t>(-1))
        s = &r.plan->steps[back];
    else
        s = Current(r);
    if (!s || s->kind != PlanStepKind::MoveDirect)
        return false;
    x = s->x;
    y = s->y;
    z = s->z;
    return true;
}

inline bool DirectJoinWanted(const Run& r)
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    return DirectJoinWanted(r, x, y, z);
}

// The farthest living bot the glue's direct-step test finds not with the lead bot (only with
// Facts::directJoinRead), or none.
inline const MemberFact* NotWithLead(const Facts& f)
{
    if (!f.directJoinRead)
        return nullptr;
    const MemberFact* pick = nullptr;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && !m.withLead && (!pick || m.yd > pick->yd))
            pick = &m;
    return pick;
}

// The gate before a direct step: every living bot within kGatherYd of the lead bot and, when the glue read it,
// with the lead bot by the very test `cmd move ... direct` refuses on.
inline bool DirectGathered(const Facts& f)
{
    return f.maxMemberYd <= kGatherYd && !NotWithLead(f);
}

// The gate passed, but on facts without the glue's test although the glue reads it: the direct order waits for
// the next tick's read (no order, the note as it is), kDirectJoinHolds ticks at most. True: hold now.
inline bool HoldForDirectJoin(Run& r, const Facts& f)
{
    if (f.directJoinRead || !f.directJoinSupported)
    {
        r.directJoinHolds = 0;
        return false;
    }
    if (r.directJoinHolds >= kDirectJoinHolds)
        return false;
    ++r.directJoinHolds;
    return true;
}

// The distance from (x,y,z) to the plan's nearest point (a step without a
// point, 0 0 0, does not count); -1 for a plan without points.
inline float PlanNearestYd(const Run& r, float x, float y, float z)
{
    float best = -1.0f;
    if (!r.plan)
        return best;
    for (const PlanStep& s : r.plan->steps)
    {
        if (s.x == 0.0f && s.y == 0.0f && s.z == 0.0f)
            continue;
        float const d = ai::PlanDistance(x, y, z, s.x, s.y, s.z);
        if (best < 0.0f || d < best)
            best = d;
    }
    return best;
}

// The lead bot revived in another part of the instance (Scarlet Monastery
// r20-19z39 slot 7, SM-GRAVEYARD-2026-10-22): alive within kDoorSpotYd of a
// door's destination (Facts::doors) that lies farther than kOtherPartYd from
// every point of the plan. The core raises a ghost at the destination of the
// door its corpse run took, and the 300 s corpse-run timeout puts it at one
// too, so every revive away from the body lands on a door: in the run's own
// part that door lies near the plan (the entry; a back door the plan passes),
// in another wing it does not, and nothing the run can order (a gather, a
// summon to the lead bot, the walk back, an island hop) joins the two. Kept
// (Run::leadOtherPart) while the lead bot stays that far from the plan; off
// once it is near the plan again or dead.
inline bool LeadInOtherPart(Run& r, const Facts& f)
{
    float const planYd = f.leadAlive ? PlanNearestYd(r, f.leadX, f.leadY, f.leadZ) : -1.0f;
    if (planYd < 0.0f || planYd <= kOtherPartYd)
    {
        r.leadOtherPart = false;
        return false;
    }
    if (!r.leadOtherPart)
        for (const DoorFact& d : f.doors)
            if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, d.x, d.y, d.z) <= kDoorSpotYd)
            {
                r.leadOtherPart = true;
                break;
            }
    return r.leadOtherPart;
}

// Why the rescue teleport for a lead bot in another part does not go now (OtherPartRescueWhy; None: it goes).
// Player: a real player in the party (he walks back with it). Off: AiPlayerbot.DungeonRunWalkBackRescue off. Cap:
// kOtherPartRescuesPerStep rescues at this step. Used: the step's rescue latch used (Run::strandStep). Tries: the
// step's kStrandTries failed teleports spent. Busy: a fight, a rescue already due or on, the run not running, the
// lead bot dead. NoPoint: no safe point on the way it came. Dead: a party bot still dead, everything else fits.
enum class OtherPartBlock : std::uint8_t { None, Player, Off, Cap, Used, Tries, Busy, NoPoint, Dead };

// The pauses whose `.bot run resume` runs the rescue instead of being refused (ResumeChecked): the reason passes
// (the dead come back, a resume gives fresh rescues, arms the latch again, a fight ends). Not with a real player, the
// rescue off, the teleport's tries spent or no safe point: nothing the run can do then (the refusal as before).
inline bool OtherPartResumeRescues(OtherPartBlock why)
{
    return why == OtherPartBlock::Dead || why == OtherPartBlock::Cap || why == OtherPartBlock::Used ||
        why == OtherPartBlock::Busy;
}

// The dead party bots' names for a note (Facts::deadNames, else their count).
inline std::string OtherPartDeadNames(const Facts& f)
{
    std::string out;
    for (const std::string& name : f.deadNames)
        out += (out.empty() ? "" : ", ") + name;
    return out.empty() ? std::to_string(f.dead) + " dead" : out;
}

// The pause for a lead bot in another part, with that reason (not "navmesh
// island"): the living bots away from it (more than kGatherYd: at the fight
// spot) hold where they are while the run is paused (Run::holdWhilePaused),
// the ones already held too. Asked only where the rescue teleport does not
// go (OtherPartRescueWhy, `why`). Where a resume can run it (OtherPartResumeRescues) the note says why it did not
// go and that `.bot run resume` runs it; otherwise (a real player, the rescue off, its tries spent, no safe point)
// moving the lead bot to the party is left to the player, with the note as before.
inline void PauseOtherPart(Run& r, const Facts& f, OtherPartBlock why)
{
    std::vector<std::string> keep = r.hold; // held already (HoldView leaves them out of f)
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && m.name != f.carrier && m.yd > kGatherYd &&
            std::find(keep.begin(), keep.end(), m.name) == keep.end())
            keep.push_back(m.name);
    bool const resumeRescues = OtherPartResumeRescues(why);
    std::string reason;
    if (why == OtherPartBlock::Dead)
        reason = "the rescue teleport waits for the dead (" + OtherPartDeadNames(f) + ")";
    else if (why == OtherPartBlock::Cap)
        reason = std::to_string(kOtherPartRescuesPerStep) + " rescue teleports at this step did not hold";
    else if (why == OtherPartBlock::Used)
        reason = "the rescue teleport of this step is used";
    else
        reason = "the rescue teleport cannot go now";
    std::string const where = "the lead bot revived in another part of the instance (" + Num(f.leadX) + " " +
        Num(f.leadY) + " " + Num(f.leadZ) + "), " + Num(PlanNearestYd(r, f.leadX, f.leadY, f.leadZ)) +
        " yd from the run: ";
    Pause(r, where +
        (resumeRescues ? reason + "; .bot run resume teleports it to the run's way"
                       : std::string("it cannot walk back; bring it to the party, then .bot run resume")) +
        (keep.empty() ? std::string() : std::string(" (the party holds where it is)")));
    r.hold = keep;
    r.holdWhilePaused = !keep.empty();
    r.otherPartPaused = true;
    r.otherPartResumeRescue = resumeRescues;
}

// A gather's ceiling: the farthest straggler, a bot more than kSummonYd from
// the lead bot (or kStragglerDzYd up or down), or one that stands still more
// than kGatherYd away, that stands still now
// (IsStill) or came less than kStragglerGainYd closer since the last sample
// (stuck behind, not on its way), is summoned to the lead bot (`cmd summon`),
// once per bot and step (and per regroup), and the gather waits again. The
// stand-still test is what catches a bot that walked part of the way and then
// stuck: Uldaman 19z11, the revived priest gained more than 5 yd, then stood
// at the scorpid pit's navmesh hole and the regroup paused. stillOnly: only
// bots that stand still (an approach hop's ceiling: the walkers are on their
// way). False when there is none to summon: the caller pauses as before.
inline bool SummonStraggler(Run& r, const Facts& f, std::string& order, bool stillOnly = false)
{
    const MemberFact* pick = nullptr;
    bool pickStill = false;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || r.summoned.count(m.name))
            continue;
        // A bot in a fight, or with something on it, is never summoned, far or
        // not: `cmd summon` refuses a bot in combat, and one with attackers
        // would drag the encounter to the lead bot or be abandoned in it.
        if (m.inCombat || m.attackers > 0)
            continue;
        // Far: past kSummonYd, or on another floor. Not far, a bot past the
        // gather radius that stands still is stuck too: the gather (kGatherYd
        // 20 yd) waits for it while the summon skips it (kSummonYd 40), so a
        // bot that sticks 20-40 yd out is never summoned and the run pauses
        // (events-5 00:55:59, grimlok pull 93: Stoutster stood 34.9 yd away,
        // still, "the party did not gather within 30 s", twice).
        bool const tooFar = m.yd > kSummonYd || m.dz > kStragglerDzYd;
        bool const still = IsStill(r, m.name);
        // A bot past the gather radius that stands still is stuck (a busy one
        // never gets here, see above), exactly as in a regroup
        // (SummonStuckInRegroup). The far rule below keeps its own tests.
        if (!tooFar && !(still && m.yd > kGatherYd))
            continue;
        if (!still)
        {
            if (!tooFar || stillOnly)
                continue;
            auto it = r.memberYd.find(m.name);
            if (it == r.memberYd.end() || it->second - m.yd >= kStragglerGainYd)
                continue;
        }
        if (!pick || m.yd > pick->yd)
        {
            pick = &m;
            pickStill = still;
        }
    }
    SampleMembers(r, f);
    if (!pick)
        return false;
    MarkSummoned(r, pick->name);
    order = "summon " + pick->name;
    r.note = "gather: " + pick->name + " stands " + Num(pick->yd) + " yd from the lead bot and " +
        (pickStill ? "has not moved for " + std::to_string(kStillMs / 1000) + " s" : std::string("does not come closer")) +
        ": summoned";
    return true;
}

// Every bot the gather still waits for (beyond kGatherYd) is held in combat by units the group cannot attack only
// (Run::combatDrop: its combat is being dropped, CombatDropTick): the gather's ceiling does not pause on it but
// waits again, within the same budget as a straggler that is coming (kGatherComingMaxMs in all, Run::
// gatherComingMs). Gnomeregan r23 step 181: the bomb 7897 held the priest 35 yd out, and the gather paused the
// run four times. True when it waits (the note is set).
inline bool HeldStragglerWait(Run& r, const Facts& f)
{
    std::string held;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || m.yd <= kGatherYd)
            continue;
        if (!r.combatDrop.count(m.name))
            return false;
        held += (held.empty() ? "" : ", ") + m.name + " (" + Num(m.yd) + " yd)";
    }
    if (held.empty() || kGatherCapMs + r.gatherComingMs >= kGatherComingMaxMs)
        return false;
    r.gatherComingMs += kGatherCapMs;
    r.note = "gathering: " + held + " held in combat by units the group cannot attack only: its combat is dropped, "
        "waiting for it";
    return true;
}

// The straggler ladder. Shadowfang Keep r25 slot 5 step 39 and r26 slot 7 step 46: after a wipe and after a Sap
// that did not land, the rogue went back and forth 10-12 min on the stair between (-263 2304 z 78) and (-290 2307
// z 91) on its own follow and formation station while the lead bot waited at the top (-250.7 2332.5 z 96.8); a
// summon to the lead bot put it there, and its formation station walked it down again; the gather paused the run
// four times ("a bot is 35-38 yd from the lead bot"). Out of the gather: past kGatherYd or kStragglerDzYd up or
// down from the lead bot.
inline bool OutOfGather(const MemberFact& m)
{
    return m.yd > kGatherYd || m.dz > kStragglerDzYd;
}

// The run waits for the party to gather: a pull's or a direct step's gather, or a regroup's once it went out.
inline bool GatherWaiting(const Run& r)
{
    return r.phase == Phase::Gather || (r.regroup && r.regroupGatherSent);
}

// A bot the run waits on for its crowd control (a Sap walks it off to its mob): no straggler.
inline bool OnCcWait(const Run& r, const std::string& name)
{
    for (const Run::CcWatch& w : r.ccWatch)
    {
        auto const hold = r.ccHolds.find(w.guid);
        if (hold != r.ccHolds.end() && hold->second.bot == name)
            return true;
    }
    return false;
}

// One tick of the ladder's clocks (TickBody, every tick): a bot still on rung 0 starts over once it is back in the
// gather or the run does not wait for a gather; a bot past rung 0 keeps its rung until the step or regroup ends;
// a bot in a fight or with attackers keeps its rung but its clock stands.
inline void UpdateStraggle(Run& r, const Facts& f, std::uint32_t dt)
{
    // A bot that reached a rung keeps it for the rest of the step or regroup (only those start the ladder over:
    // ResetStep, BeginRegroup), back in the gather or not, so a bot summoned in and walked out again by its own
    // formation station does not climb it again and hold the ceiling off for ever (Opus review of 95f66dd8).
    std::unordered_map<std::string, Run::Straggle> next;
    for (const auto& entry : r.straggle)
        if (entry.second.rung > 0)
            next.insert(entry);
    if (!GatherWaiting(r) || f.anyInCombat)
    {
        if (!GatherWaiting(r))
            r.straggle.swap(next);
        return;
    }
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || m.name == f.carrier || !OutOfGather(m) || OnCcWait(r, m.name))
            continue;
        Run::Straggle s;
        auto const it = r.straggle.find(m.name);
        if (it != r.straggle.end())
            s = it->second;
        if (!m.inCombat && !m.attackers)
        {
            if (s.rung == 0)
                s.outMs += dt;
            else
                s.rungMs += dt;
        }
        next[m.name] = s;
    }
    r.straggle.swap(next);
}

// The ladder's order, if one is due (the farthest bot first, one a tick, never toward a dead lead bot or one in
// another part, never a bot in a fight or with attackers): out of the gather kStragglerWalkMs, the bot walks to the
// lead bot by the server's path (WalkOrder: it holds there, its follow no longer walks it off); kStragglerSummonMs
// after that walk and still out, it is summoned to the lead bot (`cmd summon`, the gather's own summon). True when
// an order goes out (the note says which).
inline bool StragglerLadderTick(Run& r, const Facts& f, std::string& order)
{
    if (!order.empty() || !GatherWaiting(r) || !f.leadAlive || f.anyInCombat || LeadInOtherPart(r, f))
        return false;
    const MemberFact* pick = nullptr;
    for (const MemberFact& m : f.members)
    {
        auto const it = r.straggle.find(m.name);
        if (it == r.straggle.end() || m.inCombat || m.attackers || !OutOfGather(m))
            continue;
        bool const due = (it->second.rung == 0 && it->second.outMs >= kStragglerWalkMs) ||
            (it->second.rung == 1 && it->second.rungMs >= kStragglerSummonMs);
        if (due && (!pick || m.yd > pick->yd))
            pick = &m;
    }
    if (!pick)
        return false;
    Run::Straggle& s = r.straggle[pick->name];
    if (s.rung == 0)
    {
        order = WalkOrder(pick->name, f.leadX, f.leadY, f.leadZ);
        r.note = "gather: " + pick->name + " out of the gather " + std::to_string(kStragglerWalkMs / 1000) + " s (" +
            Num(pick->yd) + " yd from the lead bot): " + pick->name + " walked to the lead by path";
    }
    else
    {
        MarkSummoned(r, pick->name);
        order = "summon " + pick->name;
        r.note = "gather: " + pick->name + " still " + Num(pick->yd) + " yd from the lead bot " +
            std::to_string(kStragglerSummonMs / 1000) + " s after its walk: " + pick->name + " summoned to the lead";
    }
    ++s.rung;
    s.rungMs = 0;
    return true;
}

// A gather's ceiling does not pause while the ladder has a rung left for a bot it waits for (out of the gather, out
// of combat, no attackers, a living lead bot in the run's part): before its walk and its summon, and for
// kStragglerSettleMs after the summon. The ladder bounds it (kStragglerWalkMs + kStragglerSummonMs +
// kStragglerSettleMs of gather wait out of combat). True: the ceiling waits again (the note says for whom).
inline bool StragglerLadderPending(Run& r, const Facts& f)
{
    if (!f.leadAlive || LeadInOtherPart(r, f))
        return false;
    for (const MemberFact& m : f.members)
    {
        auto const it = r.straggle.find(m.name);
        if (it == r.straggle.end() || m.inCombat || m.attackers || !OutOfGather(m))
            continue;
        if (it->second.rung < 2 || it->second.rungMs < kStragglerSettleMs)
        {
            r.note = "gathering: " + m.name + " is " + Num(m.yd) + " yd from the lead bot: " +
                (it->second.rung == 0 ? "it walks to the lead by path first"
                    : it->second.rung == 1 ? "it walked to the lead by path; the summon comes next"
                                           : "summoned to the lead, waiting for it");
            return true;
        }
    }
    return false;
}

// The ladder's walk refused (no path for the bot, a bot gone or busy): its summon goes out on the next tick
// instead of kStragglerSummonMs later.
inline bool StragglerWalkRefused(Run& r, const std::string& order, const std::string& answer)
{
    if (order.compare(0, 5, "walk ") != 0)
        return false;
    std::string const rest = order.substr(5);
    std::string const name = rest.substr(0, rest.find(' '));
    auto const it = r.straggle.find(name);
    if (it != r.straggle.end() && it->second.rung == 1)
        it->second.rungMs = kStragglerSummonMs;
    r.note = "the walk of " + name + " to the lead was refused (" + answer + ")" +
        (it != r.straggle.end() && it->second.rung == 1 ? ": summoned next" : "");
    return true;
}

// The ceiling of a pull's or a direct step's gather (kGatherCapMs). In order:
// SummonStraggler (a stuck or far bot, once each). Else the farthest bot, if it
// is still on its way (it is not still and came kStragglerGainYd closer since
// the last sample, the gather's start or the last ceiling), is waited for: the
// gather starts over for another kGatherCapMs, up to kGatherComingMaxMs in all
// (Run::gatherComingMs). Uldaman r4-19z21 slot 2 step 102: a bot 33.7 yd out,
// walking in at about 1 yd/s, paused the run at the 30 s ceiling. Else, that
// bot is summoned once (past the limit, or when it is walking but not coming)
// unless it is in a fight; and if none of that applies: the pause, with the
// text the caller gave.
// A direct step's gather (direct): the farthest bot may also be one within
// kGatherYd that the glue's test finds not with the lead bot (NotWithLead), and
// one summoned already this step is summoned again by DirectSummonAllowed; the
// pause waits for DirectPauseDue, the gather starts over meanwhile.
inline void GatherCeiling(Run& r, const Facts& f, std::string& order, const std::string& pauseText,
    bool direct = false)
{
    std::unordered_map<std::string, float> const sampled = r.memberYd; // SummonStraggler takes a new sample
    if (SummonStraggler(r, f, order))
    {
        EnterPhase(r, Phase::Gather);
        return;
    }
    const MemberFact* farthest = nullptr;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && m.yd > kGatherYd && (!farthest || m.yd > farthest->yd))
            farthest = &m;
    if (direct && !farthest)
        farthest = NotWithLead(f);
    if (direct && farthest)
    {
        NoteDirectTrouble(r);
        if (r.summoned.count(farthest->name))
        {
            if (!farthest->inCombat && !farthest->attackers && DirectSummonAllowed(r, farthest->name))
            {
                MarkSummoned(r, farthest->name);
                order = "summon " + farthest->name;
                r.note = "gather: " + farthest->name + " is not with the lead bot before a direct step (" +
                    Num(farthest->yd) + " yd): summoned again";
                EnterPhase(r, Phase::Gather);
                return;
            }
            if (!DirectPauseDue(r, farthest->name))
            {
                EnterPhase(r, Phase::Gather);
                r.note = "gathering: waiting for " + farthest->name + " to come before a direct step (" +
                    Num(farthest->yd) + " yd)";
                return;
            }
        }
    }
    if (farthest && !r.summoned.count(farthest->name))
    {
        auto it = sampled.find(farthest->name);
        bool const coming = !IsStill(r, farthest->name) && it != sampled.end() && it->second - farthest->yd >= kStragglerGainYd;
        if (coming && kGatherCapMs + r.gatherComingMs < kGatherComingMaxMs)
        {
            r.gatherComingMs += kGatherCapMs;
            EnterPhase(r, Phase::Gather);
            r.note = "gathering: " + farthest->name + " is coming (" + Num(farthest->yd) + " yd)";
            return;
        }
        if (!farthest->inCombat && !farthest->attackers)
        {
            MarkSummoned(r, farthest->name);
            order = "summon " + farthest->name;
            r.note = "gather: " + farthest->name + " stands " + Num(farthest->yd) + " yd from the lead bot and " +
                (coming ? std::string("did not come in time") : std::string("does not come closer")) + ": summoned";
            EnterPhase(r, Phase::Gather);
            return;
        }
    }
    if (HeldStragglerWait(r, f))
    {
        EnterPhase(r, Phase::Gather);
        return;
    }
    // A straggler the ladder has not walked and summoned yet: no pause before both (StragglerLadderTick).
    if (StragglerLadderPending(r, f))
    {
        EnterPhase(r, Phase::Gather);
        return;
    }
    Pause(r, pauseText);
}

// A regroup's gather: a living bot far from the lead bot (kSummonYd, or
// kStragglerDzYd up or down) that has not moved for kStuckSummonMs stood still
// where it is stuck (a navmesh pocket), so it is summoned at once instead of
// the gather's whole ceiling being waited out for it (events-5 01:48:43 ->
// 01:50:42: Stoutster stood 363 yd away, still, until the ceiling; the pit at
// 43,377 / -164,299 is a repeat spot). Once per bot per regroup (Run::summoned,
// the same set SummonStraggler uses), the farthest first, one a tick. The
// gather's clock starts over; the caller sends the order. False: none.
inline bool SummonStuckInRegroup(Run& r, const Facts& f, std::string& order)
{
    if (!f.leadAlive || LeadInOtherPart(r, f)) // never toward a lead bot in another part (r20: Nightcrest)
        return false;
    // The stand-still watch is older than the gather: it runs while the dead
    // are revived. The 15 s have to be the gather's own clock (regroupMs starts
    // over when the gather goes out and on every summon), or a bot that stood
    // still 120 yd away during the revives is summoned one tick after the
    // gather, before anyone could have seen it walk.
    if (r.regroupMs < kStuckSummonMs)
        return false;
    const MemberFact* pick = nullptr;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || m.name == f.carrier || r.summoned.count(m.name) || m.inCombat || m.attackers)
            continue;
        if (m.yd <= kSummonYd && m.dz <= kStragglerDzYd)
            continue;
        auto it = r.still.find(m.name);
        if (it == r.still.end() || it->second.ms < kStuckSummonMs)
            continue;
        if (!pick || m.yd > pick->yd)
            pick = &m;
    }
    if (!pick)
        return false;
    MarkSummoned(r, pick->name);
    order = "summon " + pick->name;
    r.RestartRegroupClock();
    r.note = "regroup: " + pick->name + " has not moved for " + std::to_string(kStuckSummonMs / 1000) + " s " +
        Num(pick->yd) + " yd away: summoned";
    return true;
}

// "waiting for Priest (dead), Mage (running back)": the bots the regroup waits
// for, the dead by name and the living ones still out of reach.
inline std::string WaitNote(const Run& r, const Facts& f)
{
    std::string out;
    auto add = [&](const std::string& name, const char* why)
    {
        out += (out.empty() ? "" : ", ") + name + " (" + why + ")";
    };
    for (const std::string& name : f.deadNames)
        add(name, "dead");
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && (m.yd > kGatherYd || m.dz > kStragglerDzYd) &&
            (r.waitFor.count(m.name) || r.regroup))
            add(m.name, m.offPlanMap ? "outside the instance" : "running back");
    if (out.empty() && f.dead)
        out = std::to_string(f.dead) + " dead";
    return out.empty() ? std::string("waiting for the party") : "waiting for " + out;
}

// The regroup's gather ceiling, set when its gather goes out:
// kRegroupGatherMs, or the time the farthest bot needs to walk in at
// kRegroupWalkYdPerS, at most kRegroupGatherMaxMs (a revived bot whose summon
// fails walks the whole way).
inline std::uint32_t RegroupGatherCap(const Facts& f)
{
    float const ms = f.maxMemberYd / kRegroupWalkYdPerS * 1000.0f;
    if (ms <= static_cast<float>(kRegroupGatherMs))
        return kRegroupGatherMs;
    return ms >= static_cast<float>(kRegroupGatherMaxMs) ? kRegroupGatherMaxMs : static_cast<std::uint32_t>(ms);
}

// A revived bot's summon has failed: it walks back with the party's gather
// (the regroup's gather goes out once no summon is pending), and a gather
// already out starts its ceiling over for it.
inline void ReviveSummonFailed(Run& r, const std::string& name, const std::string& why)
{
    r.reviveSummons.erase(name);
    if (r.regroupGatherSent)
        r.RestartRegroupClock();
    r.note = "regroup: the summon of " + name + " " + why + ": it walks back";
}

// One second of the revived bots' summons (regroup only): a fallen bot
// (Run::waitFor) seen dead again may be summoned again after its next revive;
// one with the lead bot is done; one not there in kReviveSummonWaitMs failed.
inline void UpdateReviveSummons(Run& r, const Facts& f, std::uint32_t dt)
{
    for (const std::string& name : f.deadNames)
    {
        r.summoned.erase(name); // one summon per bot per revive
        r.reviveSummons.erase(name);
    }
    std::vector<std::string> failed;
    for (auto it = r.reviveSummons.begin(); it != r.reviveSummons.end();)
    {
        const MemberFact* m = nullptr;
        for (const MemberFact& x : f.members)
            if (x.name == it->first)
                m = &x;
        if (!m || (m->yd <= kGatherYd && m->dz <= kStragglerDzYd))
        {
            it = r.reviveSummons.erase(it);
            continue;
        }
        it->second += dt;
        if (it->second >= kReviveSummonWaitMs)
            failed.push_back(it->first);
        ++it;
    }
    for (const std::string& name : failed)
        ReviveSummonFailed(r, name, "did not bring it in " + std::to_string(kReviveSummonWaitMs / 1000) + " s");
}

// The owner's fix of 2026-10-17 (Uldaman run 3, kReviveSummonWaitMs): a
// fallen bot (Run::waitFor) alive again more than kSummonYd from the lead bot
// (or kStragglerDzYd up or down), out of combat and with no attackers, is
// summoned to the lead bot (`cmd summon`) on the first such tick, before its
// corpse run takes it through the dungeon; once per bot per revive (Run::
// summoned). Never while it fights (`cmd summon` refuses a bot in combat: it
// runs back, RunBack), never with the lead bot dead (it is the summon point),
// never a held bot (HoldView). The farthest first, one a tick. False: none.
inline bool SummonRevived(Run& r, const Facts& f, std::string& order)
{
    if (!f.leadAlive || LeadInOtherPart(r, f)) // never toward a lead bot in another part
        return false;
    const MemberFact* pick = nullptr;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || m.name == f.carrier || !r.waitFor.count(m.name) || r.summoned.count(m.name) ||
            m.inCombat || m.attackers || (m.yd <= kSummonYd && m.dz <= kStragglerDzYd))
            continue;
        if (!pick || m.yd > pick->yd)
            pick = &m;
    }
    if (!pick)
        return false;
    MarkSummoned(r, pick->name);
    r.reviveSummons[pick->name] = 0;
    order = "summon " + pick->name;
    r.note = "regroup: " + pick->name + " is alive again " + Num(pick->yd) +
        " yd from the lead bot: summoned to it instead of walking back";
    return true;
}


// Where the lead bot stands against the run's anchor: on it (or no anchor),
// off it (more than kStragglerDzYd above or below: a fight chased it up a
// ridge; or no navmesh path back), or not known yet (away on the anchor's
// floor and the path back not read: asked for, the glue reads it next tick).
enum class AnchorState : std::uint8_t { On, Off, Unknown };

inline AnchorState CheckAnchor(Run& r, const Facts& f)
{
    if (!r.anchorKnown)
        return AnchorState::On;
    float const dz = f.leadZ > r.anchorZ ? f.leadZ - r.anchorZ : r.anchorZ - f.leadZ;
    if (dz > kStragglerDzYd)
        return AnchorState::Off;
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.anchorX, r.anchorY, r.anchorZ) <= kArriveYd)
        return AnchorState::On;
    if (!f.anchorPathRead)
    {
        r.anchorPathWanted = true;
        return AnchorState::Unknown;
    }
    return f.anchorReachable ? AnchorState::On : AnchorState::Off;
}

inline bool LeadOffAnchor(Run& r, const Facts& f)
{
    return CheckAnchor(r, f) == AnchorState::Off;
}

// Sends a gather: the lead bot holds where it stands and the others form up on
// it; a lead bot off the anchor walks back to the anchor first and the party
// gathers there (19z5 step 83: a gather on the lead's own spot pinned the party
// on a hilltop with no path down).
inline void SendGather(Run& r, const Facts& f, std::string& order)
{
    order = LeadOffAnchor(r, f) ? MoveOrder(r.anchorX, r.anchorY, r.anchorZ, false)
                                : MoveOrder(f.leadX, f.leadY, f.leadZ, false);
    r.gatherOrder = order;
    SampleMembers(r, f);
}

// Whether `order` is the gather this tick sent (the glue tells `cmd move` it
// is the run leader's gather on the lead bot's own spot).
inline bool IsGatherOrder(const Run& r, const std::string& order)
{
    return !r.gatherOrder.empty() && order == r.gatherOrder;
}

// The step the glue reads the step facts for (seen, approach, hopUnits): the
// move point a walk back replays, else the current step.
inline const PlanStep* FactStep(const Run& r)
{
    if (r.walkBack && r.plan && r.walkBackIdx < r.step && r.step < r.plan->steps.size())
        return &r.plan->steps[r.walkBackIdx];
    return r.plan && r.step < r.plan->steps.size() ? &r.plan->steps[r.step] : nullptr;
}

// The proof's own step (DungeonRun::OpenProofStep): the actual current step,
// and only when it is an open_proof. Never FactStep's walk-back replayed
// point: a stale proof left up in the world must not finish the current step.
inline const PlanStep* ProofStep(const Run& r)
{
    const PlanStep* s = Current(r);
    return s && s->kind == PlanStepKind::Open && s->openProof ? s : nullptr;
}

// OwnedByLaterStep's `from` for the hop pulls: the current step; during a walk
// back the step before it, so a pack of a step already done (back after a
// respawn) is pulled on the way, and the current step's own pack and later
// ones are left to their steps.
inline std::size_t OwnerFrom(const Run& r)
{
    return r.walkBack && r.step > 0 ? r.step - 1 : r.step;
}

// The glue's words on an `unreachable` answer when the refused path was the
// lead bot's own no-progress path (RemoteCommander::NoProgressFromLead): an
// incomplete path that ends where the lead bot stands. The source-side evidence
// RefusedFromIsland needs; a bare `unreachable` (no polygon at the point, a
// closed door, a point on a patch the path got well toward) does not carry it.
constexpr const char* kLeadNoProgressMark = "Every path from the lead bot ends where it stands";

inline bool LeadNoProgressRefusal(const std::string& answer)
{
    return answer.compare(0, 11, "unreachable") == 0 && answer.find(kLeadNoProgressMark) != std::string::npos;
}

// A refused move whose point cannot be walked to (the move's own verdicts:
// no path, off the mesh; a direct step's no ground or no sight line), as
// opposed to a refusal for the lead bot's state of the moment.
inline bool IsPathRefusal(const std::string& answer)
{
    for (const char* code : {"unreachable", "off-mesh", "no-ground", "no-los"})
        if (answer.compare(0, std::char_traits<char>::length(code), code) == 0)
            return true;
    return false;
}

// The waiver of a hop abandoned for a close unit ends with anything that
// changes the pull's situation (combat, a regroup, another target).
inline void DropHopWaiver(Run& r)
{
    if (r.hopWaiveUnits.empty())
        return;
    r.hopWaiveUnits.clear();
    r.gatherWaived = false;
}

inline void BeginRegroup(Run& r)
{
    DropHopWaiver(r);
    r.neverCame.clear(); // a resume or a wipe recovery judges the hop units afresh
    r.patrolGaveUp.clear(); // ... and the patrollers
    r.anchorKnown = false; // the party comes back to life elsewhere
    r.regroup = true;
    r.playerGatherMs = 0;
    r.playerHoldMs = 0;
    r.reviveSent = false;
    r.regroupGatherSent = false;
    r.RestartRegroupClock();
    r.rezAbleMs = 0;
    r.rezHoldMs = 0;
    r.rezNudged = false;
    r.rezNudgeOrder.clear();
    r.rezBodyUnreachable = false;
    r.regroupDead = -1;
    r.summoned.clear(); // one summon per bot per regroup (SummonStraggler)
    r.gatherComingMs = 0;
    r.straggle.clear(); // the ladder starts over with the regroup's gather
    r.sapWalkDue.clear();
    r.reviveSummons.clear();
    r.regroupGatherCapMs = kRegroupGatherMs;
    r.leadDied = false; // the regroup revives and gathers whoever died
}

// The approach stand of a step (ApproachStand) is forgotten once the step is done.
inline void ClearApproachStand(Run& r)
{
    r.approachStandStep = static_cast<std::size_t>(-1);
    r.approachStand = 0;
    r.approachStandWanted = false;
    r.approachStandResumed = false;
    r.approachStandResumedSearch = false;
    r.approachStandWhy.clear();
    r.approachPauseKnown = false;
    r.refusedPullStandStep = static_cast<std::size_t>(-1);
}

// A rewind of the run to an earlier step (EventFailed, RearmEventOnWipe): the approach
// stand and the moves of the last no-path pause (Run::noPathSkips) belong to the way the
// run came before; the party comes forward again from elsewhere (the Opus review of
// f1fdeb67). Not on a resume (it keeps both on purpose: ApproachStand, MoveRefusedUnreachable).
inline void ForgetRewoundRoute(Run& r)
{
    ClearApproachStand(r);
    r.noPathSkips.clear();
}

// The point of a step as a note shows it ("1000.0 0.0 0.0").
inline std::string StepPoint(const PlanStep& s)
{
    return Num(s.x) + " " + Num(s.y) + " " + Num(s.z);
}

// How near (3D) and how level the lead bot must stand to a deferred pack for the run to
// pull it on the way (StepAfter), and the reach of a later gating step around a pack
// (PullMayBeDeferred) when that step has no radius of its own.
constexpr float kDeferRetryYd = 40.0f;
constexpr float kDeferRetryDzYd = 6.0f;
constexpr float kDeferGateYd = 40.0f;

// Whether a spot is near enough a deferred pack to pull it from there: within kDeferRetryYd
// (3D) and kDeferRetryDzYd of height (StepAfter's retry; a resume that keeps a retry).
inline bool NearDeferredPack(const PlanStep& d, float x, float y, float z)
{
    return ai::PlanDistance(x, y, z, d.x, d.y, d.z) <= kDeferRetryYd && std::fabs(z - d.z) <= kDeferRetryDzYd;
}

// The current step is the retry of a deferred pull (StepAfter).
inline bool DeferRetryOn(const Run& r)
{
    return r.deferRetryStep != static_cast<std::size_t>(-1) && r.deferRetryStep == r.step;
}

// The first plan step a pull group counts as not cleared yet from (PathCrossesUnclearedPack's
// `from`), past the current step (`withCurrent` false) or with it: during a retry the step
// the run goes on with afterwards (Run::deferReturnStep), so a pull the run cleared after the
// deferred one never counts as live (the Opus review of 6566f84d); earlier steps count only
// when notCleared names them.
inline std::size_t LiveFrom(const Run& r, bool withCurrent)
{
    if (DeferRetryOn(r))
        return r.deferReturnStep;
    return withCurrent ? r.step : r.step + 1;
}

inline std::string LegText(const Run& r, std::uint8_t leg)
{
    std::string name = r.plan ? r.plan->LegName(leg) : std::string();
    return name.empty() ? std::to_string(leg) : name;
}

inline void NoteGivenUp(Run& r, std::size_t idx)
{
    r.notCleared.insert(idx);
    if (std::find(r.deferGivenUp.begin(), r.deferGivenUp.end(), idx) == r.deferGivenUp.end())
        r.deferGivenUp.push_back(idx);
}

// The step after the current one (NextStep), with the deferred pulls (Run::deferredPulls,
// DeferUnreachablePack) of the current leg:
//   - A retry that ends with its pack still alive (back in notCleared: out of reach, pulls
//     that killed nothing, still not reachable, a skip) is given up (Run::deferGivenUp).
//   - A step that ends its leg (the plan's last too) gives up every pull still deferred in
//     that leg: skipped, not cleared, listed at the run's end. No walk back for them.
//   - Otherwise, after a fight (a pull or boss step) or a plain move, a deferred pack of the
//     leg the lead bot now stands within kDeferRetryYd (3D) and kDeferRetryDzYd (height) of is
//     pulled from there: its step is current again (once), and when it ends the run goes on
//     with the step it would have taken (Run::deferReturnStep).
//   - A deferred or given-up pull the run comes to again in order (a rewind) is taken up
//     fresh: out of those lists and notCleared until its own end says otherwise.
// `note` gets what was done.
inline std::size_t StepAfter(Run& r, std::string& note)
{
    std::size_t const npos = static_cast<std::size_t>(-1);
    std::size_t const total = r.plan ? r.plan->steps.size() : 0;
    if (r.step >= total)
        return r.step + 1;
    bool retried = false;
    if (r.deferRetryStep != npos)
    {
        retried = r.deferRetryStep == r.step;
        r.deferRetryStep = npos;
    }
    std::size_t const left = r.step;
    if (retried && r.notCleared.count(left))
        NoteGivenUp(r, left);
    std::size_t const next = retried ? r.deferReturnStep : left + 1;
    std::uint8_t const leg = retried ? r.deferLeg : r.plan->steps[left].leg;
    bool const legEnds = next >= total || r.plan->steps[next].leg != leg;
    if (legEnds)
    {
        std::string steps;
        for (auto it = r.deferredPulls.begin(); it != r.deferredPulls.end();)
        {
            std::size_t const idx = *it;
            if (idx < total && r.plan->steps[idx].leg == leg)
            {
                NoteGivenUp(r, idx);
                steps += (steps.empty() ? "" : ", ") + std::to_string(idx + 1) + " at " + StepPoint(r.plan->steps[idx]);
                it = r.deferredPulls.erase(it);
            }
            else
                ++it;
        }
        if (!steps.empty())
            note += "; the end of leg " + LegText(r, leg) + ": the deferred pack of step " + steps +
                " was never within reach: skipped, not cleared";
    }
    else if (r.leadSeen)
    {
        const PlanStep& ls = r.plan->steps[left];
        bool const opportunity = retried || ls.kind == PlanStepKind::Pull || ls.kind == PlanStepKind::Boss ||
            (ls.kind == PlanStepKind::Move && !IsExactPoint(r.plan, left));
        for (auto it = r.deferredPulls.begin(); opportunity && it != r.deferredPulls.end(); ++it)
        {
            std::size_t const idx = *it;
            if (idx >= total || idx == left || r.plan->steps[idx].leg != leg)
                continue;
            const PlanStep& d = r.plan->steps[idx];
            float const yd = ai::PlanDistance(r.leadLastX, r.leadLastY, r.leadLastZ, d.x, d.y, d.z);
            if (!NearDeferredPack(d, r.leadLastX, r.leadLastY, r.leadLastZ))
                continue;
            r.deferredPulls.erase(it);
            r.deferRetryStep = idx;
            r.deferReturnStep = next;
            r.deferLeg = leg;
            r.notCleared.erase(idx); // the retry's own end says whether it was cleared
            note += "; the deferred pack of step " + std::to_string(idx + 1) + " at " + StepPoint(d) + " is " + Num(yd) +
                " yd from the lead bot now: pulled from here";
            return idx;
        }
    }
    if (next < total)
    {
        // A rewind brought the run back to a deferred or given-up pull: taken up fresh.
        auto const d = std::find(r.deferredPulls.begin(), r.deferredPulls.end(), next);
        auto const g = std::find(r.deferGivenUp.begin(), r.deferGivenUp.end(), next);
        if (d != r.deferredPulls.end() || g != r.deferGivenUp.end())
        {
            if (d != r.deferredPulls.end())
                r.deferredPulls.erase(d);
            if (g != r.deferGivenUp.end())
                r.deferGivenUp.erase(g);
            r.notCleared.erase(next);
        }
    }
    return next;
}

// The retry of a deferred pull ends without it (a walk back: a resume's or a regroup's with
// the lead bot no longer beside the pack; none walks the party back for a retry): given up,
// and the run is back at the step it left.
inline void AbandonDeferRetry(Run& r)
{
    if (!DeferRetryOn(r))
        return;
    NoteGivenUp(r, r.step);
    r.deferRetryStep = static_cast<std::size_t>(-1);
    r.step = r.deferReturnStep;
    ClearApproachStand(r);
}

// The run ends before its plan does (`.bot run stop`, the time limit): every pull still
// deferred, and a retry under way, is listed as left alive in the note.
inline void EndRunEarly(Run& r, const std::string& note)
{
    if (DeferRetryOn(r))
        NoteGivenUp(r, r.step);
    r.deferRetryStep = static_cast<std::size_t>(-1);
    for (std::size_t idx : r.deferredPulls)
        NoteGivenUp(r, idx);
    r.deferredPulls.clear();
    r.state = State::Done;
    std::string const left = DeferredReport(r);
    std::string const skipped = SkippedLegsReport(r);
    std::string with = left.empty() ? skipped : skipped.empty() ? left : left + "; " + skipped;
    r.note = with.empty() ? note : note + " (" + with + ")";
}

inline void NextStep(Run& r, const std::string& note)
{
    r.pendingMissing = 0;
    r.unreachableSkips.clear(); // a step done (MoveRefusedUnreachable puts its own skips back)
    ClearApproachStand(r);
    std::string full = note;
    r.step = StepAfter(r, full);
    r.autoRegroups = 0;
    ResetStep(r);
    r.fightMs = 0; // a fight that goes on into the next step starts its ceiling over (the hold's 400 s were the wait step's)
    r.note = full;
    if (!Current(r))
    {
        r.state = State::Done;
        r.note = DoneNote(r);
    }
}

inline Run Start(const DungeonPlan* plan, std::size_t fromStep = 0)
{
    Run r;
    r.plan = plan;
    r.step = fromStep;
    r.triggerScan = fromStep; // a run started past a move never arms its trigger
    ResetStep(r);
    r.note = "started";
    if (!Current(r))
    {
        r.state = State::Done;
        r.note = "plan has no steps";
    }
    return r;
}
// The test speed's walking rule (owner request 2026-10-25): the party walks and nothing of a fight is near. Off in any
// fight (a bot, a pet, the run's own), a meal, a pull out or being set up (cc, a firing stand), an event and the moves
// leading into it, every phase but Begin, Gather and Walk, and once the pack of a pull or boss step is in sight.
inline bool SpeedWalking(const Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || r.state != State::Running || r.speedPlayerBlocked)
        return false;
    if (f.anyInCombat || f.leadInCombat || r.fighting || f.eating > 0)
        return false;
    if (r.pullGuid != 0 || !r.ccWatch.empty() || r.shotStandPrep || InEvent(r))
        return false;
    if (r.phase != Phase::Begin && r.phase != Phase::Gather && r.phase != Phase::Walk)
        return false;
    if ((s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Boss) && f.seen)
        return false;
    return true;
}
// A tick that advanced onto a pull or boss step never starts fast: f.seen was read on the step before, for a pack
// the party may already stand in sight of. The glue calls this after Tick with the step Tick saw (round 1, Opus).
inline bool SpeedCapAfterTick(const Run& r, std::size_t stepBefore)
{
    if (r.step == stepBefore)
        return false;
    const PlanStep* s = Current(r);
    return s && (s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Boss);
}

// The rate the glue gives the party this tick (after Tick, before its order goes out): testSpeed once SpeedWalking has
// held kSpeedSettleMs without a break, else 1.0 at once. A real player in the party blocks it for the rest of the run.
inline float SpeedRateNow(Run& r, const Facts& f, std::uint32_t dt)
{
    if (r.testSpeed <= 1.0f)
    {
        r.speedOnMs = 0;
        r.speedNow = 1.0f;
        return 1.0f;
    }
    if (!f.players.empty())
        r.speedPlayerBlocked = true;
    if (!SpeedWalking(r, f))
    {
        r.speedOnMs = 0;
        r.speedNow = 1.0f;
        return 1.0f;
    }
    r.speedOnMs = std::min<std::uint32_t>(r.speedOnMs + dt, kSpeedSettleMs);
    r.speedNow = r.speedOnMs >= kSpeedSettleMs ? r.testSpeed : 1.0f;
    return r.speedNow;
}

// Karazhan Crypt shaft dive (2026-10-25): the test speed waits while a bot
// swims a spline. Changing the run/walk rate re-launches DispatchMovement's
// stale PointMovementGenerator, whose Initialize drops the module's dive
// spline; a bot in water always swims, so waiting costs nothing.
inline bool SpeedChangeWaits(bool inWater, bool splineMoving) { return inWater && splineMoving; }
constexpr std::uint32_t kSpeedRestoreWaitMs = 15000; // a refused restore while a bot swims a spline waits at most this long

// The speed-restore wait is bounded (lead decision 2026-10-25): a run a real
// player joined restores at once, never waits; otherwise a refused restore
// waits at most kSpeedRestoreWaitMs from the first refusal, then restores anyway.
inline bool SpeedRestoreWaits(bool playerBlocked, std::uint32_t waitedMs)
{
    if (playerBlocked)
        return false;
    return waitedMs < kSpeedRestoreWaitMs;
}
// The bound-group scan of SyncRunSpeed (commands/Commander.cpp): which member slots block the test speed for the
// rest of the run. The GUIDs ride along so the call site and the test name the commander case; only the bot flag
// decides: the commander is never exempt, so a GM commander joining a bot-only test-speed run blocks it on that
// same call like any other real player.
inline bool SpeedMemberBlocksRun(std::uint64_t /*memberRaw*/, std::uint64_t /*commanderRaw*/, bool memberIsBot)
{
    return !memberIsBot;
}

// The glue saw a listed bot (or its pet) of the run enter a fight between ticks and put the party back to 1.0.
inline void SpeedFightSeen(Run& r)
{
    r.speedOnMs = 0;
    r.speedNow = 1.0f;
}

inline std::string LowerCopy(std::string v)
{
    for (char& c : v)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}

// ---------------------------------------------------------------------------
// The run's own time limit (AiPlayerbot.DungeonRunMaxMinutes) and the notes of
// a stop. A run that hit its limit used to read "stopped by .bot run stop"
// (Uldaman run 2, 2026-10-17: the agent's 3 h cap sent a plain stop).
// ---------------------------------------------------------------------------

constexpr std::uint32_t kRunMaxMinutesDefault = 180; // the shipped template and the code fallback agree (3 h)

// A run holds its dead (SM-REVIVE-PATH-2026-10-23 fix 3): while it runs or is
// paused, a hired bot of its bound party that dies stays at its body (no auto
// release, runtime/RunReleaseHold.h); the regroup releases it (a resurrection
// in the party, then `revive party`), or the player does. A done run (the
// plan's end, `.bot run stop`, the time limit, the switch off) holds nobody,
// and the glue erases a stopped, timed-out or unbound run, so the bot's own
// release works again then.
inline bool HoldsItsDead(const Run& r)
{
    return r.plan && r.state != State::Done;
}

// The gossip step a run-bound bot may talk at (GossipOrder::MayTalkTo): a running
// or paused run (HoldsItsDead) binds its bots; its current step, read from the
// loaded plan (no database read), names the NPC when it is a gossip step. Null
// (the bot is in no such run), a done run, or a step that is not a gossip step
// widens nothing.
inline GossipOrder::RunGossipStep RunGossipStepOf(const Run* r)
{
    GossipOrder::RunGossipStep out;
    if (!r || !HoldsItsDead(*r))
        return out;
    out.bound = true;
    const PlanStep* s = Current(*r);
    if (!s || s->kind != PlanStepKind::Gossip)
        return out;
    out.atGossip = true;
    out.entry = s->entry;
    return out;
}

// The area triggers a run-bound bot may fire beyond AiPlayerbot.BotAreaTriggers
// (ai::IsBotAreaTriggerArmed): a move or move_direct step's `entry` is armed once the
// run has reached that step (Tick calls ArmReachedTriggers before and after each
// step), and it stays armed past the step (the run may go on to the boss before any
// bot's twice-a-second check ran inside the sphere, or stand in for the point on a
// detour) until it has fired once for this run (NoteTriggerFired). Read from the
// loaded plan (no database read). Null for no run, a done run, or nothing armed.
inline void ArmReachedTriggers(Run& r)
{
    if (!r.plan)
        return;
    for (; r.triggerScan <= r.step && r.triggerScan < r.plan->steps.size(); ++r.triggerScan)
    {
        const PlanStep& s = r.plan->steps[r.triggerScan];
        if (InSkippedLeg(r, s.leg))
            continue;
        if ((s.kind != PlanStepKind::Move && s.kind != PlanStepKind::MoveDirect) || s.entry == 0)
            continue;
        if (std::find(r.firedTriggers.begin(), r.firedTriggers.end(), s.entry) != r.firedTriggers.end() ||
            std::find(r.armedTriggers.begin(), r.armedTriggers.end(), s.entry) != r.armedTriggers.end())
            continue;
        r.armedTriggers.push_back(s.entry);
    }
}

inline void NoteTriggerFired(Run& r, std::uint32_t id)
{
    auto const it = std::find(r.armedTriggers.begin(), r.armedTriggers.end(), id);
    if (it == r.armedTriggers.end())
        return;
    r.armedTriggers.erase(it);
    r.firedTriggers.push_back(id);
}

inline const std::vector<std::uint32_t>* RunArmedTriggersOf(const Run* r)
{
    if (!r || !HoldsItsDead(*r) || r->armedTriggers.empty())
        return nullptr;
    return &r->armedTriggers;
}

// maxMinutes 0 = no limit.
inline bool RunTimeLimitReached(std::uint64_t elapsedMs, std::uint32_t maxMinutes)
{
    return maxMinutes > 0 && elapsedMs >= static_cast<std::uint64_t>(maxMinutes) * 60000ull;
}

// "time limit reached (3h)", "time limit reached (90 min)".
inline std::string RunTimeLimitNote(std::uint32_t maxMinutes)
{
    std::string span = maxMinutes % 60 == 0 ? std::to_string(maxMinutes / 60) + "h"
                                            : std::to_string(maxMinutes) + " min";
    return "time limit reached (" + span + ")";
}

// The note of `.bot run stop [reason]`: no reason keeps the old text; a reason
// (the agent's own cap, "time limit reached (3h)") is shown as it came, cut to
// kRunStopReasonMax characters and reduced to printable ASCII without the
// protocol's separator.
constexpr std::size_t kRunStopReasonMax = 80;

inline std::string RunStopNote(const std::string& reason)
{
    std::string clean;
    for (char c : reason)
    {
        if (c == '|' || c < 0x20 || c > 0x7e)
            c = ' ';
        if (c == ' ' && (clean.empty() || clean.back() == ' '))
            continue;
        clean.push_back(c);
        if (clean.size() >= kRunStopReasonMax)
            break;
    }
    while (!clean.empty() && clean.back() == ' ')
        clean.pop_back();
    return clean.empty() ? std::string("stopped by .bot run stop") : "stopped: " + clean;
}

// ---------------------------------------------------------------------------
// `.bot run start [plan] [from [leg|step] <leg name|leg number|step number>]`
// ---------------------------------------------------------------------------

enum class StartFrom : std::uint8_t
{
    Beginning, // no `from`: the first step
    Leg,       // a leg's name or number (`from 4`, `from leg trogg_west`)
    Step,      // a step number as the status line shows it, from 1 (`from step 57`)
};

struct StartArgs
{
    bool ok = true;       // false: the words do not parse
    std::string plan;     // "" = the one plan of the lead bot's map
    StartFrom from = StartFrom::Beginning;
    std::string value;    // the leg name / number or the step number
};

// `arg` is the text after `start`, already lowercased. Words before `from` are
// the plan (at most one); after it an optional `leg` or `step`, then the value.
inline StartArgs ParseStartArgs(const std::string& arg)
{
    StartArgs out;
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < arg.size())
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t'))
            ++i;
        std::size_t begin = i;
        while (i < arg.size() && arg[i] != ' ' && arg[i] != '\t')
            ++i;
        if (i > begin)
            words.push_back(arg.substr(begin, i - begin));
    }
    std::size_t at = 0;
    if (!words.empty() && words[0] != "from")
        out.plan = words[at++];
    if (at == words.size())
        return out;
    if (words[at] != "from")
    {
        out.ok = false; // a second word that is not `from`
        return out;
    }
    ++at;
    StartFrom kind = StartFrom::Leg;
    if (at < words.size() && (words[at] == "leg" || words[at] == "step"))
        kind = words[at++] == "leg" ? StartFrom::Leg : StartFrom::Step;
    if (at + 1 != words.size())
    {
        out.ok = false; // no value, or words after it
        return out;
    }
    out.from = kind;
    out.value = words[at];
    return out;
}
// `.bot run start ... speed=<x>` (the test speed, owner request 2026-10-25): takes the one `speed=` word out of the
// start's words (any place, any case). given: the word was there; ok false: its value is not 1.0 to kTestSpeedMax with
// at most one decimal ("2", "2.0", "1.5"), or a second `speed=` came. rest: the other words, single-spaced, for
// ParseStartArgs.
struct SpeedArgs
{
    bool given = false;
    bool ok = true;
    float rate = 1.0f;
    std::string rest;
};

inline SpeedArgs TakeSpeed(const std::string& arg)
{
    SpeedArgs out;
    std::size_t i = 0;
    while (i < arg.size())
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t'))
            ++i;
        std::size_t const begin = i;
        while (i < arg.size() && arg[i] != ' ' && arg[i] != '\t')
            ++i;
        if (i == begin)
            break;
        std::string const word = arg.substr(begin, i - begin);
        if (LowerCopy(word).compare(0, 6, "speed=") != 0)
        {
            out.rest += (out.rest.empty() ? "" : " ") + word;
            continue;
        }
        if (out.given)
        {
            out.ok = false;
            continue;
        }
        out.given = true;
        std::string const v = word.substr(6);
        // One or two digits, then optionally '.' and one digit: tenths, exact in a float after the division.
        std::size_t const dot = v.find('.');
        std::string const whole = v.substr(0, dot);
        std::string const frac = dot == std::string::npos ? std::string() : v.substr(dot + 1);
        bool const digits = !whole.empty() && whole.size() <= 2 &&
            std::all_of(whole.begin(), whole.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
            (dot == std::string::npos || (frac.size() == 1 && frac[0] >= '0' && frac[0] <= '9'));
        int const tenths = digits ? std::atoi(whole.c_str()) * 10 + (frac.empty() ? 0 : frac[0] - '0') : 0;
        if (!digits || tenths < 10 || tenths > static_cast<int>(kTestSpeedMax * 10.0f + 0.5f))
        {
            out.ok = false;
            continue;
        }
        out.rate = static_cast<float>(tenths) / 10.0f;
    }
    return out;
}

// Who may walk a run at test speed (`speed=` above 1.0): a player-led run never, a party with a real player never,
// then a GameMaster only, in that order, so the refusal names the reason that holds for anyone first (pip S3
// 2026-10-26: a rank-0 player's player-led start heard `forbidden`). realPlayer: the first real player of the lead
// bot's group by name ("" for none). Empty code: allowed.
struct SpeedRefusal
{
    std::string code;
    std::string text;
};

inline SpeedRefusal CheckTestSpeed(float rate, bool playerLed, const std::string& realPlayer, bool gameMaster)
{
    if (rate <= 1.0f)
        return {};
    if (playerLed)
        return {"real-player", "speed= is for bot-only test runs: a player-led run walks at normal speed."};
    if (!realPlayer.empty())
        return {"real-player", "speed= is for bot-only test runs: " + realPlayer +
            " is a real player in the party; a run with a player in it walks at normal speed."};
    if (!gameMaster)
        return {"forbidden", "Only a GameMaster may start a test-speed run (speed=); without speed= the run walks at "
            "normal speed."};
    return {};
}

enum class FromResult : std::uint8_t
{
    Ok,
    NoSuchLeg,
    NoSuchStep,
};

inline bool AllDigits(const std::string& v)
{
    return !v.empty() && v.size() <= 5 && std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// A scripted event starts at its ritual, open, use_item or gossip step and runs on in the
// hold behind it (EventHoldAhead). A start that lands on the hold, or on a move
// leading into one, would never send the ritual: it goes back to the step that
// starts the event (the Uldaman Keepers' ritual ends leg 10, its hold opens
// leg 11). Anything else, or a hold with no such step before it, stays put.
inline std::size_t EventStartStep(const DungeonPlan& plan, std::size_t idx)
{
    auto const moveLike = [](PlanStepKind k) { return k == PlanStepKind::Move || k == PlanStepKind::MoveDirect; };
    std::size_t const n = plan.steps.size();
    if (idx >= n)
        return idx;
    PlanStepKind const kind = plan.steps[idx].kind;
    if (kind != PlanStepKind::Hold && !moveLike(kind))
        return idx;
    std::size_t j = idx;
    while (j < n && moveLike(plan.steps[j].kind))
        ++j;
    if (kind != PlanStepKind::Hold && !(j < n && plan.steps[j].kind == PlanStepKind::Hold))
        return idx;
    std::size_t k = idx;
    while (k > 0)
    {
        --k;
        PlanStepKind const back = plan.steps[k].kind;
        if (back == PlanStepKind::Ritual || back == PlanStepKind::Open || back == PlanStepKind::UseItem ||
            back == PlanStepKind::Gossip || back == PlanStepKind::UseOn)
            return k;
        if (!moveLike(back))
            return idx;
    }
    return idx;
}

// The index of the step the run starts at. `movedBack` is set when a start
// inside an event was moved back to the step that starts it.
inline FromResult ResolveStartStep(const DungeonPlan& plan, StartFrom from, const std::string& value,
    std::size_t& step, bool* movedBack = nullptr)
{
    step = 0;
    if (movedBack)
        *movedBack = false;
    if (from == StartFrom::Beginning)
        return FromResult::Ok;
    if (from == StartFrom::Step)
    {
        if (!AllDigits(value))
            return FromResult::NoSuchStep;
        std::size_t const n = static_cast<std::size_t>(std::atoi(value.c_str()));
        if (n < 1 || n > plan.steps.size())
            return FromResult::NoSuchStep;
        step = EventStartStep(plan, n - 1);
        if (movedBack)
            *movedBack = step != n - 1;
        return FromResult::Ok;
    }
    bool const numeric = AllDigits(value);
    int const number = numeric ? std::atoi(value.c_str()) : -1;
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
    {
        std::uint8_t const leg = plan.steps[i].leg;
        if (numeric ? static_cast<int>(leg) == number : LowerCopy(plan.LegName(leg)) == value)
        {
            step = EventStartStep(plan, i);
            if (movedBack)
                *movedBack = step != i;
            return FromResult::Ok;
        }
    }
    return FromResult::NoSuchLeg;
}

// A start partway needs the whole party inside the instance: the steps before
// are not done and not checked, so a party that is outside (or scattered over
// two maps) would walk a dungeon plan from the middle in the wrong place.
enum class PartialStartVerdict : std::uint8_t
{
    Ok,
    NotInstance,    // the lead bot is not on an instance map
    PartyScattered, // a commanded bot is on another map or instance than the lead bot
};

inline PartialStartVerdict CheckPartialStart(bool leadOnInstanceMap, std::size_t members, std::size_t membersWithLead)
{
    if (!leadOnInstanceMap)
        return PartialStartVerdict::NotInstance;
    return membersWithLead < members ? PartialStartVerdict::PartyScattered : PartialStartVerdict::Ok;
}

// `.bot run resume`: the party regroups first (the dead are revived, the
// living gather on the lead bot), then the paused step starts over with fresh
// tries; a lead bot far from it walks back on the navmesh (approach hops).
// The step's move refusals and the run of skips are forgotten: the run went back
// to an earlier step (EventFailed, RearmEventOnWipe), so the party comes forward
// again from elsewhere, or a step that is never skipped was resumed.
inline void ForgetMoveRefusals(Run& r)
{
    r.moveRefusedStep = static_cast<std::size_t>(-1);
    r.moveRefusals = 0;
    r.unreachableSkips.clear();
}

// A step that is not a plain move the run may skip when its point is
// unreachable: a direct step, an exact point, any step not a move, and the
// move before a direct step (the hole's crossing starts there), a pull, a boss
// or a hold (the fight's approach stand). Steps that neither move nor fight
// (rest, waits, use_item) are looked past: a move, a rest, then a pull counts
// as before the pull.
inline bool NeverSkipMove(const Run& r)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind != PlanStepKind::Move || IsExactPoint(r.plan, r.step))
        return true;
    for (std::size_t i = r.step + 1; i < r.plan->steps.size(); ++i)
    {
        PlanStepKind const k = r.plan->steps[i].kind;
        if (k == PlanStepKind::Rest || k == PlanStepKind::WaitObject || k == PlanStepKind::WaitClear ||
            k == PlanStepKind::UseItem)
            continue;
        return k == PlanStepKind::MoveDirect || k == PlanStepKind::Pull || k == PlanStepKind::Boss ||
            k == PlanStepKind::Hold;
    }
    return false;
}

// Whether the current step is a pull the run may defer when its pack is not reachable
// from this floor (DeferUnreachablePack). All of: a pull (never a boss); no no_defer flag
// on the step (kPlanFlagNoDefer); none of its entries killed first plan-wide or named by a
// later step of the plan that is not a plain pull or move (a hold's or a wait's units, a
// boss's adds or its ai_boss_tactic subjects (PlanStep::tacticEntries: kill_first targets,
// interrupt casters), a gossip, open, loot or use target); and no later step of its leg that a
// pack left alive would stand in the way of (a hold, a wait, an open, a gossip, a loot, a
// ritual, a use_on, a direct or exact move, a boss) within that step's radius (kDeferGateYd
// when it has none) plus the pack's radius of the pack (3D). Otherwise the old pause.
inline bool PullMayBeDeferred(const Run& r)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind != PlanStepKind::Pull || (s->flags & ai::kPlanFlagNoDefer))
        return false;
    std::vector<std::uint32_t> mine = s->entries;
    if (s->entry)
        mine.push_back(s->entry);
    auto named = [&](std::uint32_t e) { return e && std::find(mine.begin(), mine.end(), e) != mine.end(); };
    for (std::uint32_t e : mine)
        if (r.plan->KillFirstRank(e) >= 0)
            return false;
    for (std::size_t i = r.step + 1; i < r.plan->steps.size(); ++i)
    {
        const PlanStep& n = r.plan->steps[i];
        PlanStepKind const k = n.kind;
        bool const plain = k == PlanStepKind::Pull || k == PlanStepKind::Move || k == PlanStepKind::MoveDirect ||
            k == PlanStepKind::Rest || k == PlanStepKind::UseItem;
        if (!plain)
        {
            if (named(n.entry))
                return false;
            for (std::uint32_t e : n.entries)
                if (named(e))
                    return false;
            for (std::uint32_t e : n.tacticEntries) // a later boss's ai_boss_tactic subjects (JoinBossTactics)
                if (named(e))
                    return false;
            if (k == PlanStepKind::Boss && r.liveTactics) // (Rows answers nothing before the seal)
            {
                // ... and the rows the server reads now, by JoinBossTactics' rule (the map, the boss and its adds).
                std::vector<std::uint32_t> keys = n.entries;
                keys.push_back(n.entry);
                for (std::uint32_t boss : keys)
                    if (const std::vector<ai::BossTacticRow>* rows = boss ? r.liveTactics->Rows(r.plan->map, boss) : nullptr)
                        for (const ai::BossTacticRow& row : *rows)
                            if (named(ai::TacticSubjectEntry(row)))
                                return false;
            }
        }
        if (n.leg != s->leg)
            continue;
        bool const gate = k == PlanStepKind::Hold || k == PlanStepKind::WaitClear || k == PlanStepKind::WaitObject ||
            k == PlanStepKind::Open || k == PlanStepKind::Gossip || k == PlanStepKind::Loot ||
            k == PlanStepKind::Ritual || k == PlanStepKind::UseOn || k == PlanStepKind::MoveDirect ||
            k == PlanStepKind::Boss || IsExactPoint(r.plan, i);
        float const reach = (n.radius > 0.0f ? n.radius : kDeferGateYd) + s->radius;
        if (gate && ai::PlanDistance(n.x, n.y, n.z, s->x, s->y, s->z) <= reach)
            return false;
    }
    return true;
}

inline bool Resume(Run& r)
{
    if (r.state != State::Paused || !Current(r))
        return false;
    r.state = State::Running;
    r.ceilingPaused = false;
    r.ceilingQuietMs = 0;
    r.leadOffMapPaused = false;
    r.playerHoldPaused = false;
    r.otherPartPaused = false;
    r.doorPaused = false;
    r.fighting = false;
    r.autoRegroups = 0;
    r.otherPartRescues = 0; // a resume gives the lead bot in another part fresh rescue teleports
    r.offMapReturnTries.clear();
    r.offMapNoted.clear();
    ResetStep(r);
    r.standPullShotGuid = 0; // a resume never keeps a stale in-shot authorization
    // A step that is never skipped starts its refusals over: the next one may
    // search a stand-in from the lead bot's new spot and hop it off an island.
    // A plain move keeps them, so its second refusal skips it (r22-19z40).
    if (NeverSkipMove(r))
        ForgetMoveRefusals(r);
    // A step whose approach hops got nowhere and whose stand was tried (ApproachStand):
    // the resume does not hop the same way again, it searches the stand first.
    if (r.approachStandStep == r.step)
    {
        r.approachStandResumed = true;
        r.approachStand = 0;
        r.approachStandWanted = false;
    }
    BeginRegroup(r);
    r.walkBackDue = true; // a party revived at the entrance walks back the plan's way
    r.note = "resumed: regrouping";
    return true;
}

// The lead bot as `.bot run resume` finds it (ResumeChecked). read: the glue
// found it in the world; onPlanMap: on the plan's map.
struct LeadSpot
{
    bool read = false;
    bool alive = false;
    bool onPlanMap = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// The door as `.bot run resume` read it near the lead bot (unread when the
// lead bot is not in the world or the door is out of its sight).
enum class DoorRead
{
    Unread,
    Closed,
    Open,
};

inline bool DoorOpenWhilePaused(Run& r, const Facts& f);

inline void RearmRescueAfterDeath(Run& r);

// `.bot run resume` from the glue (SM-REVIVE-PATH-2026-10-23 fix 5): a pause for
// a lead bot in another part of the instance (PauseOtherPart) whose lead bot is
// still alive on the plan's map and more than kOtherPartYd from every point of
// the plan is not resumed: Resume would regroup and the next tick would pause
// again (r21-19z40e slot 7: three times, the same note, no step progress). Refused
// with a plain answer (the pause, its note and its hold stay) unless the pause's reason lets the rescue teleport run
// (Run::otherPartResumeRescue, OtherPartResumeRescues; Dire Maul West r39/r40 slot 6, 2026-10-25: the pause waited
// for a bot still dead and every resume was refused until a GM moved the party by hand): then the run resumes, the
// rescue's latch armed again (the pause came after a death) and its rescues fresh (Resume), and the regroup's next
// tick runs the rescue (OtherPartRescue) with the facts it reads then (a joined player still pauses). Every other case
// resumes as Resume does. True when the run resumed; refusal is set only when it was refused for this reason.
inline bool ResumeChecked(Run& r, const LeadSpot& lead, std::string& refusal, DoorRead door = DoorRead::Closed)
{
    refusal.clear();
    // The door hold: only the door read open ends it, through the same read
    // the tick runs (DoorOpenWhilePaused, with its note), so a resume after
    // the player opened the door goes on instead of being refused; closed
    // keeps the hold, unread names the true cause instead of 'closed'.
    if (r.state == State::Paused && r.doorPaused)
    {
        if (door == DoorRead::Open)
        {
            Facts open;
            open.doorNeedRead = true;
            open.doorNeedState = 1;
            return DoorOpenWhilePaused(r, open);
        }
        if (door == DoorRead::Unread)
        {
            refusal = "the door is out of sight of the lead bot: .bot run skip to go on, or bring the party to the door";
            return false;
        }
        refusal = "the bots still hold at the closed door: " + r.doorPausedAsk +
            ". The run goes on by itself once the door reads open; .bot run resume does not pass a closed door"
            " (.bot run skip walks the party past it by hand)";
        return false;
    }
    if (r.state == State::Paused && r.otherPartPaused && lead.read && lead.alive && lead.onPlanMap)
    {
        float const planYd = PlanNearestYd(r, lead.x, lead.y, lead.z);
        if (planYd > kOtherPartYd)
        {
            if (r.otherPartResumeRescue)
            {
                RearmRescueAfterDeath(r);
                if (!Resume(r))
                    return false;
                r.note = "resumed: the lead bot is in another part of the instance (" + Num(planYd) +
                    " yd from the run): regrouping, then the rescue teleport";
                return true;
            }
            refusal = "the lead bot is still in another part of the instance (" + Num(lead.x) + " " + Num(lead.y) +
                " " + Num(lead.z) + "), " + Num(planYd) +
                " yd away from the run: bring it to the party first, then .bot run resume";
            return false;
        }
    }
    return Resume(r);
}

// `.bot run skip`: leaves the current step out and goes on running (from a
// pause too, with the regroup of a resume). An item the step failed to get is
// noted missing: later steps that need it are left out with a note instead
// of failing one after the other.
inline bool Skip(Run& r)
{
    const PlanStep* s = Current(r);
    if (r.state == State::Done || !s)
        return false;
    bool const wasPaused = r.state == State::Paused;
    // A skip from the door hold walks the party past the still-closed door by
    // hand: the leg's door is decided (no hold on it again this run).
    if (wasPaused && r.doorPaused)
        r.legNeedDecided.insert(r.doorPausedLeg);
    r.doorPaused = false;
    if (r.pendingMissing)
        r.missing.insert(r.pendingMissing);
    if (s->kind == PlanStepKind::Open)
        r.skippedOpenStep = r.step;
    if (s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Boss)
        r.notCleared.insert(r.step);
    // The step's pull state goes with the step (NextStep -> ResetStep); a
    // target refused as invalid-target is already out of reach for the run.
    std::size_t const skipped = r.step + 1;
    r.state = State::Running;
    r.ceilingPaused = false;
    r.ceilingQuietMs = 0;
    r.leadOffMapPaused = false;
    r.playerHoldPaused = false;
    r.otherPartPaused = false;
    NextStep(r, "skipped step " + std::to_string(skipped));
    if (wasPaused && r.state == State::Running)
        BeginRegroup(r);
    return true;
}

// The run was paused by a player (PausePlayer: an order to the party, `.bot run pause`).
inline bool PausedByPlayer(const Run& r)
{
    return r.state == State::Paused && r.pausedByPlayer;
}

// A pause from the fight ceiling (kFightCapMs) goes on by itself once the
// party has been out of combat for kCeilingQuietMs (Uldaman run 4, step 38: the
// fight ended 15 s after the pause and the run waited 470 s for a manual
// resume). Once per step: a second ceiling on the same step stays paused. A
// player's pause and every other pause never come back by themselves. The glue
// ticks it while the run is paused; true when the run resumed.
inline bool CeilingFightOverWhilePaused(Run& r, bool partyInCombat, std::uint32_t dt)
{
    if (r.state != State::Paused || !r.ceilingPaused || PausedByPlayer(r) || r.ceilingResumedStep == r.step)
        return false;
    if (partyInCombat)
    {
        r.ceilingQuietMs = 0;
        return false;
    }
    r.ceilingQuietMs += dt;
    if (r.ceilingQuietMs < kCeilingQuietMs)
        return false;
    r.ceilingResumedStep = r.step;
    if (!Resume(r))
        return false;
    r.note = "the fight past the ceiling is over: going on by itself (regrouping)";
    return true;
}

// A fight-ceiling pause (Run::ceilingPaused, not a player's): the fight goes on and the lead bot moves with it, as in a
// running fight, so the route spot follows each tick's living lead bot while it moves at most kRouteTrackStepYd and
// kRouteTrackDzYd from the last spot (r18-19z36 Uldaman step 68: 14 yd from the pause's spot after the fight, and the walk
// back replayed steps 40-67). A dead lead bot leaves the spot where it was; a jump (a teleport, a GM move, a fall) is not
// followed, so the first running tick after the resume finds the route unknown as before. The glue calls it on every
// paused tick before CeilingFightOverWhilePaused.
inline void TrackRouteWhilePaused(Run& r, bool leadAlive, float x, float y, float z)
{
    if (r.state != State::Paused || !r.ceilingPaused || PausedByPlayer(r) || !r.routeCheck || !leadAlive)
        return;
    if (ai::PlanDistance(x, y, z, r.routeSpotX, r.routeSpotY, r.routeSpotZ) > kRouteTrackStepYd ||
        std::fabs(z - r.routeSpotZ) > kRouteTrackDzYd)
        return;
    r.routeSpotX = x;
    r.routeSpotY = y;
    r.routeSpotZ = z;
}

// The lead bot off the plan's map (pip S2 Deadmines 2026-10-26: the pause waited > 2 min with the lead bot back
// beside the player).
inline void PauseLeadOffMap(Run& r, unsigned planMap)
{
    Pause(r, "the lead bot is not on the plan's map (" + std::to_string(planMap) + ")");
    r.leadOffMapPaused = true;
}
// leadBack: the lead bot alive, in world, not teleporting, on the plan's map. Once per step; never a player's pause.
inline bool LeadBackWhilePaused(Run& r, bool leadBack, std::uint32_t dt)
{
    if (r.state != State::Paused || !r.leadOffMapPaused || PausedByPlayer(r) || r.leadBackResumedStep == r.step)
        return false;
    if (!leadBack)
    {
        r.leadBackMs = 0;
        return false;
    }
    r.leadBackMs += dt;
    if (r.leadBackMs <= kLeadBackSettleMs)
        return false;
    r.leadBackResumedStep = r.step;
    if (!Resume(r))
        return false;
    r.note = "the lead bot is back on the plan's map: going on by itself (regrouping)";
    return true;
}

// The 5 min player hold's pause (O2, pip S2 Deadmines run 1 2026-10-25: the ghost was away 7.5 min, past the cap).
inline void PausePlayerHold(Run& r, const std::string& why)
{
    Pause(r, why);
    r.playerHoldPaused = true;
}
// The cause gone: every player of the party alive and back on the plan's map
// within kGatherYd (the regroup's own gather line). Once per step; never a player's pause.
inline bool PlayerHoldBackWhilePaused(Run& r, const Facts& f, std::uint32_t dt)
{
    if (r.state != State::Paused || !r.playerHoldPaused || PausedByPlayer(r) || r.playerHoldResumedStep == r.step)
        return false;
    for (const PlayerFact& p : f.players)
        if (!p.alive || !p.onMap || p.yd > kGatherYd)
        {
            r.playerHoldBackMs = 0;
            return false;
        }
    r.playerHoldBackMs += dt;
    if (r.playerHoldBackMs <= kPlayerHoldBackSettleMs)
        return false;
    r.playerHoldResumedStep = r.step;
    if (!Resume(r))
        return false;
    r.note = "the player is back with the party: going on by itself (regrouping)";
    return true;
}

// A player's pause: only `.bot run resume` (or skip) goes on from it.
inline void PausePlayer(Run& r, const std::string& why)
{
    Pause(r, why);
    r.pausedByPlayer = true;
}

// A player's order the run goes on through (turtle-agent #270): `cmd summon <bot>` brings that bot to the lead bot, the
// run's own straggler summon (the summon point is the lead bot: CommanderSummon; a bot in combat is refused), so the run
// is not paused for it (RFC 2026-10-20 run 2b: the observer's regroup summons were the run's only manual resume). Every
// other order still pauses. `cmd revive <bot>` and `cmd revive party` (pip S5 2026-10-26: the run's own pause notes ask
// the commander for them, and all three pauses of the run were his revive) are the run's own regroup revive too.
inline bool PlayerOrderKeepsRun(const std::string& order)
{
    return order == "cmd summon" || order == "cmd revive";
}


inline std::string NeedLegLabel(const DungeonPlan& plan, std::uint8_t leg)
{
    const std::string& name = plan.LegName(leg);
    return name.empty() ? std::to_string(leg) : name;
}

// Spec item-leg-skip C2: an optional leg's item: decided once a run, on the
// first tick out of combat at one of its steps. Every optional item of the leg
// carried by someone -> it runs; else the whole leg is skipped (its Pull/Boss
// steps at index >= r.step go into notCleared, so the walk-back path check
// treats the skipped packs as alive; r.step jumps to the leg's LAST step and
// NextStep carries on with the next leg's note). True: the run left the leg
// this tick. The facts of this tick were read for the left step.
inline bool LegNeedGate(Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || !r.plan || f.anyInCombat)
        return false;
    if (r.legNeedDecided.count(s->leg))
        return false;
    bool optional = false;
    for (const ai::PlanLegNeed& need : r.plan->needs)
        if (need.leg == s->leg && need.mode == ai::LegNeedMode::Optional)
        {
            optional = true;
            break;
        }
    if (!optional)
        return false;
    if (!f.legNeedRead)
    {
        r.note = "leg " + NeedLegLabel(*r.plan, s->leg) + ": reading the party's bags";
        return false;
    }
    r.legNeedDecided.insert(s->leg);
    bool carried = true;
    for (const ai::PlanLegNeed& need : r.plan->needs)
    {
        if (need.leg != s->leg || need.mode != ai::LegNeedMode::Optional)
            continue;
        bool held = false;
        for (const auto& have : f.legNeedHolders)
            if (have.first == need.item && !have.second.empty())
            {
                held = true;
                break;
            }
        if (!held)
        {
            carried = false;
            break;
        }
    }
    if (carried)
        return false;
    // Skip: every Pull/Boss step of the leg at index >= r.step is not cleared.
    std::uint32_t skipItem = 0;
    std::string skipName;
    for (const ai::PlanLegNeed& need : r.plan->needs)
        if (need.leg == s->leg && need.mode == ai::LegNeedMode::Optional)
        {
            skipItem = need.item;
            skipName = need.name;
            break;
        }
    for (std::size_t i = r.step; i < r.plan->steps.size(); ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (p.leg != s->leg)
            continue;
        if (p.kind == PlanStepKind::Pull || p.kind == PlanStepKind::Boss)
            r.notCleared.insert(i);
    }
    r.skippedLegs.push_back(s->leg);
    std::string const legText = NeedLegLabel(*r.plan, s->leg);
    std::size_t last = r.step;
    for (std::size_t i = r.step; i < r.plan->steps.size(); ++i)
        if (r.plan->steps[i].leg == s->leg)
            last = i;
    r.step = last;
    r.say = "Does anyone have " + (skipName.empty() ? "item " + std::to_string(skipItem) : skipName) +
        "? Without it we skip " + legText + ".";
    NextStep(r, "leg " + legText + " skipped: it needs item " + std::to_string(skipItem) +
        (skipName.empty() ? std::string() : " (" + skipName + ")") +
        " and nobody in the party carries it (an optional leg; the run goes on with the next leg)");
    return true;
}

// The run's skipped optional legs for the report: "" none, else "optional leg
// skipped: gahzrilla (item 9240)" (plural "optional legs skipped: a (item x), b (item y)").
inline std::string SkippedLegsReport(const Run& r)
{
    if (r.skippedLegs.empty() || !r.plan)
        return std::string();
    std::string out;
    for (std::size_t k = 0; k < r.skippedLegs.size(); ++k)
    {
        std::uint8_t const leg = r.skippedLegs[k];
        std::string items;
        for (const ai::PlanLegNeed& need : r.plan->needs)
            if (need.leg == leg && need.mode == ai::LegNeedMode::Optional)
                items += std::string(items.empty() ? "" : ", ") + "item " + std::to_string(need.item);
        out += (k ? ", " : "") + NeedLegLabel(*r.plan, leg) +
            (items.empty() ? std::string() : " (" + items + ")");
    }
    return std::string("optional leg") + (r.skippedLegs.size() == 1 ? " skipped: " : "s skipped: ") + out;
}

// Spec item-leg-skip C2b/C2c (review round 2: Codex 2, Opus 3/6): the key doors of a leg with a real player in the
// party. The leg's first Door need row names the door object; the glue looks for it near the lead bot each tick
// (Facts::doorNeedRead/State/X/Y/Z): a door not found is not known closed (nothing held, nothing decided); a door
// found open decides the leg. The bots stop only NEAR the door (DoorNear: the current step's point or the lead bot
// within kDoorNearYd of it), so a long leg walks up to its door first.

// The hold stops the party this near the door (yd): the step about to cross it, or the lead bot beside it.
constexpr float kDoorNearYd = 25.0f;
// DoorKeyOpen: the wait for the door to read open after an open order, and the orders before the hold takes over.
constexpr std::uint32_t kDoorOpenWaitMs = 20000;
constexpr std::uint32_t kDoorOpenTries = 3;

// The current leg's first Door need row (nullptr none).
inline const ai::PlanLegNeed* LegDoorNeed(const Run& r)
{
    const PlanStep* s = Current(r);
    if (!s || !r.plan)
        return nullptr;
    for (const ai::PlanLegNeed& need : r.plan->needs)
        if (need.leg == s->leg && need.mode == ai::LegNeedMode::Door)
            return &need;
    return nullptr;
}

inline bool DoorNear(const Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || !f.doorNeedRead)
        return false;
    return ai::PlanDistance(s->x, s->y, s->z, f.doorNeedX, f.doorNeedY, f.doorNeedZ) <= kDoorNearYd ||
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.doorNeedX, f.doorNeedY, f.doorNeedZ) <= kDoorNearYd;
}

// A qualified opener in the party: a real player carries the key (FillLegNeedFacts names a player holder first), a
// real player's Lockpicking reaches the lock's pick, or the run makes the key itself before the leg.
inline bool DoorQualified(const Run& r, const Facts& f, const ai::PlanLegNeed& door)
{
    for (const auto& have : f.legNeedHolders)
        if (have.first == door.item && !have.second.empty())
        {
            for (const PlayerFact& p : f.players)
                if (p.name == have.second)
                    return true;
            break;
        }
    if (door.pick > 0 && f.doorNeedLockpick >= door.pick)
        return true;
    std::size_t first = r.plan->steps.size();
    for (std::size_t i = 0; i < r.plan->steps.size(); ++i)
        if (r.plan->steps[i].leg == door.leg)
        {
            first = i;
            break;
        }
    for (std::size_t j = 0; j < first; ++j)
    {
        const PlanStep& m = r.plan->steps[j];
        if ((m.kind == PlanStepKind::Loot || m.kind == PlanStepKind::UseItem) && m.item == door.item)
            return true;
    }
    return false;
}

// What opens the door, for the notes: "Shadowforge Key (item 11000) or Lockpicking 250".
inline std::string DoorOpener(const ai::PlanLegNeed& door)
{
    std::string const item = door.name.empty() ? "item " + std::to_string(door.item)
                                               : door.name + " (item " + std::to_string(door.item) + ")";
    return item + (door.pick > 0 ? " or Lockpicking " + std::to_string(door.pick) : std::string());
}

// The hold's pause: only the door read open ends it (DoorOpenWhilePaused).
inline void PauseDoor(Run& r, std::uint8_t leg, const std::string& ask, const std::string& note)
{
    Pause(r, note);
    r.doorPaused = true;
    r.doorPausedLeg = leg;
    r.doorPausedAsk = ask;
}

// The line the bots say on /s this tick, taken once: the glue sends it.
inline std::string TakeSay(Run& r)
{
    std::string say;
    say.swap(r.say);
    return say;
}

// The hold (DungeonRunBotKeysForPlayers = 0, or no bot carries the key): a real player in the party, the leg's door
// found closed and near, nobody qualified: the run pauses with the ask note; the bots stand before the door. True:
// the run held at the door this tick.
inline bool DoorNeedHold(Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || !r.plan || f.anyInCombat || f.players.empty() || !f.legNeedRead)
        return false;
    if (r.legNeedDecided.count(s->leg))
        return false;
    const ai::PlanLegNeed* door = LegDoorNeed(r);
    if (!door)
        return false;
    if (DoorQualified(r, f, *door))
    {
        r.legNeedDecided.insert(s->leg); // the player opens it himself
        return false;
    }
    if (f.botKeysForPlayers && !f.doorNeedCarrier.empty())
        return false; // a bot opens it (DoorKeyOpen)
    if (!f.doorNeedRead)
        return false; // not found near the lead bot: not known closed
    if (f.doorNeedState == 1)
    {
        r.legNeedDecided.insert(s->leg);
        return false;
    }
    if (!DoorNear(r, f))
        return false;
    bool anyoneCarries = false;
    for (const auto& have : f.legNeedHolders)
        if (have.first == door->item && !have.second.empty())
            anyoneCarries = true;
    std::string const ask = "open it with " + DoorOpener(*door);
    std::string const howText = !door->how.empty() && !anyoneCarries ? " Get it: " + door->how + "." : "";
    if (!(r.doorPaused && r.doorPausedAsk == ask)) // once per pause, not every tick of the hold
        r.say = "Does anyone have " + (door->name.empty() ? "the key" : door->name) + " to open the door?" +
            (door->pick > 0 ? " Or Lockpicking " + std::to_string(door->pick) + "?" : "");
    PauseDoor(r, s->leg, ask,
        "door to leg " + NeedLegLabel(*r.plan, s->leg) + ": the way is locked (object " + std::to_string(door->object) +
            "): " + ask + "; the bots wait before it and go on by themselves once it reads open (.bot run resume does "
            "not pass a closed door)." + howText);
    return true;
}

// DungeonRunBotKeysForPlayers = 1 with a real player (rule 5): the bot carrying the key opens the leg's door near it
// with the `open` order (the lock's key in its bags), up to kDoorOpenTries orders kDoorOpenWaitMs apart; the door read
// open decides the leg; the last try failing pauses as the hold does. The party stands meanwhile. True: this tick is
// the door's (order holds the open order, or nothing while it waits).
inline bool DoorKeyOpen(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    const PlanStep* s = Current(r);
    if (!s || !r.plan || f.anyInCombat || f.players.empty() || !f.botKeysForPlayers || f.doorNeedCarrier.empty())
        return false;
    if (r.legNeedDecided.count(s->leg))
        return false;
    const ai::PlanLegNeed* door = LegDoorNeed(r);
    if (!door || !f.doorNeedRead)
        return false;
    if (f.doorNeedState == 1)
    {
        r.legNeedDecided.insert(s->leg);
        r.doorOpenLeg = -1;
        return false;
    }
    if (!DoorNear(r, f))
        return false;
    std::string const legText = NeedLegLabel(*r.plan, s->leg);
    if (r.doorOpenLeg != static_cast<int>(s->leg))
    {
        r.doorOpenLeg = s->leg;
        r.doorOpenTries = 0;
        r.doorOpenMs = 0;
    }
    else
        r.doorOpenMs += dt;
    if (r.doorOpenTries > 0 && r.doorOpenMs < kDoorOpenWaitMs)
    {
        r.note = "door to leg " + legText + ": waiting for " + f.doorNeedCarrier + " to open it";
        return true;
    }
    if (r.doorOpenTries >= kDoorOpenTries)
    {
        std::string const ask = "open it with " + DoorOpener(*door);
        PauseDoor(r, s->leg, ask,
            "door to leg " + legText + ": " + f.doorNeedCarrier + " could not open it (object " +
                std::to_string(door->object) + ") in " + std::to_string(kDoorOpenTries) + " tries: " + ask +
                "; the bots wait before it and go on by themselves once it reads open (.bot run resume does not "
                "pass a closed door).");
        return true;
    }
    ++r.doorOpenTries;
    r.doorOpenMs = 0;
    order = "open " + EntryArg(door->object, f.doorNeedX, f.doorNeedY, f.doorNeedZ) + " " + f.doorNeedCarrier;
    r.note = "door to leg " + legText + ": " + f.doorNeedCarrier + " opens it with " +
        (door->name.empty() ? "item " + std::to_string(door->item) : door->name) + " for the party (" +
        std::to_string(r.doorOpenTries) + " of " + std::to_string(kDoorOpenTries) + ")";
    return true;
}

// The door hold's pause ends by itself the tick the glue reads the door open (it reads the door while paused too);
// the run resumes with a resume's regroup. True when it resumed.
inline bool DoorOpenWhilePaused(Run& r, const Facts& f)
{
    if (r.state != State::Paused || !r.doorPaused || !f.doorNeedRead || f.doorNeedState != 1)
        return false;
    std::uint8_t const leg = r.doorPausedLeg;
    r.legNeedDecided.insert(leg);
    if (!Resume(r))
        return false;
    r.note = "door to leg " + (r.plan ? NeedLegLabel(*r.plan, leg) : std::to_string(leg)) +
        " reads open: going on by itself (regrouping)";
    return true;
}

// Any order from a player to the party.
inline bool PauseForPlayer(Run& r, const std::string& order)
{
    if (r.state != State::Running)
        return false;
    if (PlayerOrderKeepsRun(order))
        return false;
    PausePlayer(r, "player order (" + order + "): .bot run resume to go on");
    return true;
}

// The glue's order was refused (ERR): pause with the module's own words. A
// refused `rest` (nobody had food or drink to use) only notes it: the rest
// gate's own ceiling ends the wait. A refused gather is sent once more, then
// left out (the pull or the step goes on with the party as it stands); a
// refused point of a walk back is passed for the next one.
// The follower a refused direct step names: `cmd move ... direct` answers
// "too-far: <bot> is not with <lead> (...)" when a bot has no way onto the
// lead bot's line. Empty for any other answer.
inline std::string DirectStragglerName(const std::string& answer)
{
    static const std::string head = "too-far: ";
    static const std::string mid = " is not with ";
    if (answer.compare(0, head.size(), head) != 0)
        return std::string();
    std::size_t const at = answer.find(mid, head.size());
    if (at == std::string::npos || at == head.size())
        return std::string();
    return answer.substr(head.size(), at - head.size());
}

inline std::string MissingHint(const Run& r)
{
    return r.pendingMissing ? " (item " + std::to_string(r.pendingMissing) +
        " missing: .bot run skip leaves out the steps that need it)" : std::string();
}

// The module's refusals of a loot order that another bot, or the facts, may
// settle (BeginLootAct): the loot rules (another member's turn, already looted,
// not tapped by the group), and a corpse with nothing left on it.
constexpr const char* kLootRightsText = "nothing this bot may loot";
constexpr const char* kLootEmptyText = "Nothing is left to loot";

inline bool LootRefusalOpen(const std::string& text)
{
    return text.find(kLootRightsText) != std::string::npos || text.find(kLootEmptyText) != std::string::npos;
}

// A loot order was refused or failed: the step does not pause yet. The next
// tick (TaskStep, with the facts) counts it done if a party bot carries the
// item, tries the bot whose turn it is or another one for a refusal on the
// loot rules, and pauses only when none is left.
inline void LootRefused(Run& r, const std::string& text)
{
    r.lootRefusal = text;
    r.lootRights = text.find(kLootRightsText) != std::string::npos;
    r.orderSent = false;
    EnterPhase(r, Phase::Begin);
    r.note = text + ": looking for another bot to take it";
}

// The island escape (Zul'Farrak r3-19z20, kGoodPathMinYd). The glue's reading
// of the lead bot's navmesh path toward the point it is sent to: partial, ending
// within kGoodPathMinYd of the lead bot and more than kArriveYd short of the point,
// or no path at all toward a point off the lead bot. Uldaman r9-19z26 step 127: the
// tank stood on a ~3 x 8 yd patch of mesh at 132-139 / 296-304 (z -23.8, the floor
// round it -26.5); every partial path from 137.3 298.7 ended 0.0-0.2 yd from it,
// inside the server's smooth-path slop (PathFinder.cpp SMOOTH_PATH_SLOP 0.4), so
// BuildPointPath made a one-point path and answered NOPATH with the point itself
// as the end: read as partial-only, no reading said island and the escape never ran.
// A NOPATH is one sign like a partial reading; IslandConfirmed still wants a second.
inline bool LeadOnIsland(bool pathNoPath, bool pathIncomplete, float endFromLeadYd, float toPointYd)
{
    if (pathNoPath)
        return toPointYd > kArriveYd;
    return pathIncomplete && endFromLeadYd >= 0.0f && endFromLeadYd <= kGoodPathMinYd &&
        toPointYd > endFromLeadYd + kArriveYd;
}

// ... or a path that gets somewhere: complete, or ending more than kGoodPathMinYd from it.
inline bool LeadPathGood(bool pathNoPath, bool pathIncomplete, float endFromLeadYd)
{
    return !pathNoPath && (!pathIncomplete || endFromLeadYd > kGoodPathMinYd);
}

// A spot around the lead bot that the hop may go to (the glue's ring search):
// its navmesh path toward the point is complete (a start snapped onto the
// island has none), or partial and ending more than kGoodPathMinYd from the
// spot AND from the lead bot (Detour snaps a start up to 5 yd onto the nearest
// polygon: a spot off the mesh can snap onto the island itself).
inline bool RingSpotLeadsOn(bool pathNoPath, bool pathIncomplete, float endFromSpotYd, float endFromLeadYd)
{
    return !pathNoPath && (!pathIncomplete || (endFromSpotYd > kGoodPathMinYd && endFromLeadYd > kGoodPathMinYd));
}

// A candidate of the move's detour (the glue's NearestReachablePoint): the lead
// bot's navmesh path to it ends within kDetourEndYd of it, and, when partial,
// leads on as a ring spot's must (RingSpotLeadsOn; the start is the lead bot):
// it ends more than kGoodPathMinYd from the lead bot. r6-19z24: from the island
// the partial paths to 1587 1050 and 1594.3 1048 ended 3.6 and 1.0 yd from the
// lead bot, within 5 yd of the point: both moves were refused (CheckMove's
// no-progress rule), each a pause.
// Never a point within kArriveYd of the lead bot (r9-19z26 Uldaman step 124: the
// 10 yd ring point 136 298.7 lay 1.3 yd from the tank on its own rubble island,
// its path complete, and the step counted as 'arrived' 10.3 yd from the plan's
// point without a yard walked).
// Nor one whose path from the lead bot (pathYd) is more than kDetourMaxPathYd
// longer than its straight distance from it (a move goes out up to 60 yd off;
// the r22-19z40 review: a 25 yd ring point could mean a long walk round, through
// packs not cleared yet; r22's own walk around went back 60 yd before its refusal).
inline bool DetourPointReachable(bool pathNoPath, bool pathIncomplete, float endFromPointYd, float endFromLeadYd,
    float pointFromLeadYd, float pathYd = 0.0f)
{
    return pointFromLeadYd > kArriveYd && endFromPointYd <= kDetourEndYd && pathYd <= pointFromLeadYd + kDetourMaxPathYd &&
        RingSpotLeadsOn(pathNoPath, pathIncomplete, endFromLeadYd, endFromLeadYd);
}

// A candidate of the detour must also lead on by its OWN path toward the step's
// point (the glue's probe from the candidate to it; RingSpotLeadsOn, as a ring
// spot of the island escape must). Uldaman r13-19z30 slot 5: the lead stood in
// a closed 6 x 6 yd pit; the pit's own point 42.1 374.3 (3.2 yd away, its path
// from the lead complete) was taken as 'the nearest reachable point, 10.0 yd from
// the plan's' although its probe to 50.8 379.3 ended 1.1 yd from it, in the pit, and
// step 97's pullback was refused. A candidate within kArriveYd of the point (the
// first candidate is the point itself, put on the ground) was not asked until
// the r22-19z40 review (Codex): one across a wall or up a ledge passed. Its probe
// must now be complete and end within kArriveYd of the point, and the candidate
// lie at most kDetourCloseMaxDz above or below it.
inline bool DetourCandidateLeadsOn(bool probeNoPath, bool probeIncomplete, float probeEndFromCandidateYd,
    float probeEndFromLeadYd, float candidateFromPointYd, float probeEndFromPointYd, float candidateDz)
{
    if (candidateFromPointYd <= kArriveYd)
        return !probeNoPath && !probeIncomplete && probeEndFromPointYd <= kArriveYd &&
            std::fabs(candidateDz) <= kDetourCloseMaxDz;
    return RingSpotLeadsOn(probeNoPath, probeIncomplete, probeEndFromCandidateYd, probeEndFromLeadYd);
}

// The glue probes the path to the last good lead spot (Facts::goodSpotCut)
// only when that spot is off the lead bot (kArriveYd), within kGoodSpotProbeYd
// and kEscapeMaxVertical.
inline bool GoodSpotProbeWanted(float yd, float dz)
{
    return yd > kArriveYd && yd <= kGoodSpotProbeYd && std::fabs(dz) <= kEscapeMaxVertical;
}

// The glue's reading of the forward run's own move point in sight (NoteLeadWay,
// for MoveStep's island check) costs a navmesh search: it runs only on a tick
// on which MoveStep can act on the move (running, no walk back or regroup, a
// move step in sight and off its point, phase Begin, nobody in combat), and one
// reading serves kMoveProbeCacheMs for the same step, target and
// kMoveProbeCellYd cell of the lead bot (MoveProbeHit).
constexpr float kMoveProbeCellYd = 2.0f;
constexpr std::uint64_t kMoveProbeCacheMs = 5000;

struct MoveProbeCache
{
    bool known = false;
    std::size_t step = 0;
    int cellX = 0, cellY = 0;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    std::uint64_t atMs = 0; // Run::runMs of the reading
    bool island = false;    // Facts::leadIsland it gave
    bool good = false;      // Facts::leadPathGood it gave
};

inline bool ForwardMoveProbeWanted(const Run& r, bool moveStep, bool seen, bool anyInCombat, float toPointYd)
{
    return r.state == State::Running && !r.walkBack && !r.regroup && moveStep && seen && !anyInCombat &&
        r.phase == Phase::Begin && toPointYd > kArriveYd;
}

inline int MoveProbeCell(float v)
{
    return static_cast<int>(std::floor(v / kMoveProbeCellYd));
}

inline bool MoveProbeHit(const MoveProbeCache& c, std::size_t step, float lx, float ly, float tx, float ty, float tz,
    std::uint64_t nowMs)
{
    return c.known && c.step == step && c.cellX == MoveProbeCell(lx) && c.cellY == MoveProbeCell(ly) && c.tx == tx &&
        c.ty == ty && c.tz == tz && nowMs - c.atMs < kMoveProbeCacheMs; // a clock before it wraps: a miss
}

inline void StoreMoveProbe(MoveProbeCache& c, std::size_t step, float lx, float ly, float tx, float ty, float tz,
    std::uint64_t nowMs, bool island, bool good)
{
    c.known = true;
    c.step = step;
    c.cellX = MoveProbeCell(lx);
    c.cellY = MoveProbeCell(ly);
    c.tx = tx;
    c.ty = ty;
    c.tz = tz;
    c.atMs = nowMs;
    c.island = island;
    c.good = good;
}

inline int IslandCell(float v)
{
    return static_cast<int>(std::floor(v / kIslandCellYd));
}

// The last good spot as the hop's end: farther than kArriveYd from the lead bot
// (nearer is the island itself), within kEscapeMaxYd and kEscapeMaxVertical.
inline bool GoodSpotFits(float lx, float ly, float lz, float gx, float gy, float gz)
{
    float const dx = gx - lx, dy = gy - ly;
    return ai::PlanDistance(lx, ly, lz, gx, gy, gz) > kArriveYd && std::sqrt(dx * dx + dy * dy) <= kEscapeMaxYd &&
        std::fabs(gz - lz) <= kEscapeMaxVertical;
}

// One sample of the hop's straight line: the point on the line, and what the
// glue read there (the floor's height, any liquid: water, slime, magma).
struct SegmentSample
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool ground = false;
    float groundZ = 0.0f;
    bool liquid = false;
};

// The samples of the line from (ax,ay,az) to (bx,by,bz): every kSegmentStepYd,
// at most kSegmentSamples, the end point last.
inline std::vector<SegmentSample> EscapeSegmentSamples(float ax, float ay, float az, float bx, float by, float bz)
{
    float const dx = bx - ax, dy = by - ay;
    float const len = std::sqrt(dx * dx + dy * dy);
    std::size_t n = static_cast<std::size_t>(std::ceil(len / kSegmentStepYd));
    n = n < 1 ? 1 : (n > kSegmentSamples ? kSegmentSamples : n);
    std::vector<SegmentSample> out(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        float const t = static_cast<float>(i + 1) / static_cast<float>(n);
        out[i].x = ax + t * dx;
        out[i].y = ay + t * dy;
        out[i].z = az + t * (bz - az);
    }
    return out;
}

enum class SegmentVerdict : std::uint8_t { Ok, NoGround, Step, Climb, Liquid, Hostile };

inline const char* SegmentVerdictName(SegmentVerdict v)
{
    switch (v)
    {
        case SegmentVerdict::Ok: return "ok";
        case SegmentVerdict::NoGround: return "no ground under the line";
        case SegmentVerdict::Step: return "a height step on the line";
        case SegmentVerdict::Climb: return "too far up or down";
        case SegmentVerdict::Liquid: return "liquid on the line";
        default: return "a hostile near the end";
    }
}

// The straight line may be walked: a floor within kSegmentGroundYd of the line at
// every sample, no step over kSegmentRiseYd between samples (the lead bot's own
// height first), never more than kEscapeMaxVertical from where it starts, no
// liquid, and no hostile within kEscapeHostileYd of the end. A step down may be
// up to kSegmentDropYd when the spot's own path leads on (dropOk: a ring spot that
// passed RingSpotLeadsOn), so the lead bot is never dropped into a pit with no
// way out (Uldaman r13-19z30); a rise stays at kSegmentRiseYd, and so does a drop
// to the last good spot (its path toward this point was not read).
inline SegmentVerdict CheckEscapeSegment(float startZ, const std::vector<SegmentSample>& samples, bool hostileNearEnd,
    bool dropOk = false)
{
    if (hostileNearEnd)
        return SegmentVerdict::Hostile;
    if (samples.empty())
        return SegmentVerdict::NoGround;
    float prev = startZ;
    for (const SegmentSample& s : samples)
    {
        if (!s.ground || std::fabs(s.groundZ - s.z) > kSegmentGroundYd)
            return SegmentVerdict::NoGround;
        if (s.liquid)
            return SegmentVerdict::Liquid;
        if (s.groundZ - prev > kSegmentRiseYd || prev - s.groundZ > (dropOk ? kSegmentDropYd : kSegmentRiseYd))
            return SegmentVerdict::Step;
        if (std::fabs(s.groundZ - startZ) > kEscapeMaxVertical)
            return SegmentVerdict::Climb;
        prev = s.groundZ;
    }
    return SegmentVerdict::Ok;
}

// Every tick: the living lead bot's spot is the last good one when this tick's
// path read got somewhere; that also ends the count of hops off an island.
inline void NoteLeadPath(Run& r, const Facts& f)
{
    r.leadPathGoodNow = f.leadAlive && f.leadPathGood;
    // The island's evidence holds for the lead bot's cell; the no-spot mark for its spot.
    if (r.islandEvidence && (IslandCell(f.leadX) != r.evidenceCellX || IslandCell(f.leadY) != r.evidenceCellY))
        r.islandEvidence = false;
    if (r.noSpotKnown && ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.noSpotX, r.noSpotY, r.noSpotZ) > kArriveYd)
        r.noSpotKnown = false;
    if (!r.leadPathGoodNow)
        return;
    r.islandEvidence = false;
    r.islandSuspect = false; // a path that gets somewhere: no island here (a refusal's sign included)
    r.noSpotKnown = false;
    r.goodLeadKnown = true;
    r.goodLeadX = f.leadX;
    r.goodLeadY = f.leadY;
    r.goodLeadZ = f.leadZ;
    r.goodLeadMs = r.runMs;
    r.islandEscapes = 0;
}

// Sign (a) for the forward run's own move point (MoveStep): the last good lead
// spot counts only when it was read within kGoodSpotFreshMs of run time; an
// older one says nothing about where the lead bot stands now. The false positive
// left: a one-way drop within that time (a fall in a fight); the straight hop up
// to the good spot is then refused by CheckEscapeSegment (a rise of more than
// kSegmentRiseYd per sample), a no-spot, and the move goes out as before.
inline bool GoodSpotFresh(const Run& r)
{
    return r.goodLeadKnown && r.runMs - r.goodLeadMs <= kGoodSpotFreshMs;
}

// Whether the lead bot stands on a navmesh island on its way to (tx,ty,tz): this
// tick's reading toward it says so (Facts::leadIsland) AND a second sign does:
// (a) the last good lead spot nearby cannot be reached (Facts::goodSpotCut), or
// (b) an earlier reading from the same spot (kEvidenceSpotYd, same cell) toward
// a point at least kEvidenceTargetYd from this one said so too (r3-19z20: the
// points of steps 64, 65, 67, 68 and 70 all came back partial from 1589.6 1046.9).
// A single reading is kept as evidence and changes nothing: the move is sent,
// refused and passed, or the hop walked, as before. Or two walk-back points were
// given up in a row from one spot (Run::islandSuspect). Never where a walk back's
// hop found no spot (Run::noSpotKnown): the walk back goes on as before there.
inline bool IslandConfirmed(Run& r, const Facts& f, float tx, float ty, float tz, bool goodSpotCounts = true)
{
    if (r.noSpotKnown && ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.noSpotX, r.noSpotY, r.noSpotZ) <= kArriveYd)
    {
        r.islandSuspect = false;
        return false;
    }
    if (!f.leadIsland)
        return r.islandSuspect;
    int const cx = IslandCell(f.leadX), cy = IslandCell(f.leadY);
    bool const sameCell = r.islandEvidence && cx == r.evidenceCellX && cy == r.evidenceCellY;
    bool const second = sameCell &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.evidenceX, r.evidenceY, r.evidenceZ) <= kEvidenceSpotYd &&
        ai::PlanDistance(tx, ty, tz, r.evidenceTX, r.evidenceTY, r.evidenceTZ) >= kEvidenceTargetYd;
    if (!sameCell)
    {
        r.islandEvidence = true;
        r.evidenceCellX = cx;
        r.evidenceCellY = cy;
        r.evidenceX = f.leadX;
        r.evidenceY = f.leadY;
        r.evidenceZ = f.leadZ;
        r.evidenceTX = tx;
        r.evidenceTY = ty;
        r.evidenceTZ = tz;
    }
    return (f.goodSpotCut && goodSpotCounts) || second || r.islandSuspect;
}

inline OtherPartBlock OtherPartRescueWhy(const Run& r, const Facts& f);
inline bool OtherPartRescue(Run& r, const Facts& f);

// The lead bot stands on a navmesh island (IslandConfirmed)
// on its way to (tx,ty,tz): before any point is passed or a hop counted, it is
// moved alone in a straight line off it. The order "escape from lx ly lz [to x y
// z] toward tx ty tz" is the glue's own (RunIslandEscape), not a `cmd`: from the
// spot the facts were read at (the glue refuses it once the lead bot moved
// kEscapeMovedYd), to the last good spot (x y z) when GoodSpotFits, else to the
// nearest spot around the lead bot whose path leads on (RingSpotLeadsOn); in
// sight, on a line CheckEscapeSegment passes. kIslandEscapes hops a step (a
// resume gives fresh ones), then a pause that names the spot. A refusal that
// starts "wait" (a fight, the lead bot moved) is not counted.
inline void IslandEscape(Run& r, const Facts& f, float tx, float ty, float tz, const std::string& what,
    std::string& order)
{
    r.islandSuspect = false;
    r.escapeFromMove = false;
    // No path because the lead bot stands in another wing, not on an island:
    // no hop: the rescue teleport where it goes (OtherPartRescue), else the pause with the true reason
    // (r20: "navmesh island at 1617.6 1002.7").
    if (LeadInOtherPart(r, f))
    {
        OtherPartBlock const why = OtherPartRescueWhy(r, f);
        if (why == OtherPartBlock::None && OtherPartRescue(r, f))
            return;
        PauseOtherPart(r, f, why);
        return;
    }
    std::string const spot = Num(f.leadX) + " " + Num(f.leadY) + " " + Num(f.leadZ);
    if (r.islandEscapes >= kIslandEscapes || r.stepEscapes >= kStepEscapes)
    {
        Pause(r, "the lead bot stands on a navmesh island at " + spot + ": no path toward " + what + " (" +
            std::to_string(r.stepEscapes >= kStepEscapes ? kStepEscapes : kIslandEscapes) +
            " straight hops off it did not help); move it off, then .bot run resume");
        return;
    }
    ++r.islandEscapes;
    ++r.stepEscapes;
    r.escapeOn = true;
    r.escapeMs = 0;
    EnterPhase(r, Phase::Begin);
    bool const good = r.goodLeadKnown && GoodSpotFits(f.leadX, f.leadY, f.leadZ, r.goodLeadX, r.goodLeadY, r.goodLeadZ);
    order = "escape from " + Num(f.leadX) + " " + Num(f.leadY) + " " + Num(f.leadZ) + " " +
        (good ? "to " + Num(r.goodLeadX) + " " + Num(r.goodLeadY) + " " + Num(r.goodLeadZ) + " " : std::string()) +
        "toward " + Num(tx) + " " + Num(ty) + " " + Num(tz);
    r.note = "the lead bot stands on a navmesh island at " + spot + " (no path toward " + what + "): a straight hop off it (" +
        std::to_string(r.islandEscapes) + " of " + std::to_string(kIslandEscapes) + ")";
}

// The hop off the island under way: no other order until the lead bot stood
// still kEscapeSettleMs (or kEscapeCapMs went by); the tick after it reads the way afresh.
inline bool EscapeWait(Run& r, const Facts& f, std::uint32_t dt)
{
    if (!r.escapeOn)
        return false;
    r.escapeMs += dt;
    if (r.escapeMs < kEscapeCapMs && (f.leadMoving || r.escapeMs < kEscapeSettleMs))
        return true;
    r.escapeOn = false;
    EnterPhase(r, Phase::Begin);
    return true;
}

// A walk-back point given up ("on to the next"), the lead bot at (x,y,z). The
// second one in a row given up from one spot (moved less than kArriveYd), with
// no path read there that got anywhere, is not passed: the lead bot is taken to
// stand on an island and is hopped off it first (IslandEscape, via
// islandSuspect). Not for a direct step's point (its line crosses a navmesh
// hole; no path is read for it). r3-19z20 passed 23 points in 13 minutes from one spot.
inline void PassWalkBackPoint(Run& r, float x, float y, float z, const std::string& why, bool direct = false)
{
    if (direct)
        r.islandSuspect = false; // a direct step's point says nothing about an island
    // Where a walk back's hop found no spot the points are passed as before
    // (2012dfee): no suspicion there, or the mark would clear it and the same
    // point would go out again every tick.
    bool const noSpotHere =
        r.noSpotKnown && ai::PlanDistance(x, y, z, r.noSpotX, r.noSpotY, r.noSpotZ) <= kArriveYd;
    bool const same = !direct && !noSpotHere && r.passSpotKnown &&
        ai::PlanDistance(x, y, z, r.passX, r.passY, r.passZ) <= kArriveYd;
    r.passSpotKnown = true;
    r.passX = x;
    r.passY = y;
    r.passZ = z;
    r.approachHops = 0;
    r.walkBackRefusals = 0;
    EnterPhase(r, Phase::Begin);
    if (same && !r.leadPathGoodNow)
    {
        r.islandSuspect = true;
        r.note = why + ": the second point given up from this spot: the lead bot is hopped off it first";
        return;
    }
    r.note = why + ": on to the next";
    ++r.walkBackIdx;
    r.gatherWaived = false;
    r.gatherRefusals = 0;
}

// ai::kPlanFlagGoneIsDone: a loot or open row with the flag and no item counts the object being gone as done. `answer`
// is the module's text for a refused order ("not-found: ...", the by-entry search found nothing spawned) or a failed
// task ("invalid-target: The object is gone.", it despawned on the bot's way), each led by its code.
inline bool GoneIsDone(const PlanStep* s, const std::string& answer)
{
    if (!s || (s->kind != PlanStepKind::Loot && s->kind != PlanStepKind::Open) || s->item ||
        !(s->flags & ai::kPlanFlagGoneIsDone))
        return false;
    return answer.rfind("not-found:", 0) == 0 || answer.rfind("invalid-target: The object is gone", 0) == 0;
}

// ai::kPlanFlagGoneIsDone on a wait_object row: the object is not found now (Facts::objectState -1) and a wait step
// read it found earlier in the run (Run::objectsSeen). An object never seen is not gone, only not there yet: the step
// waits out its timeout as before. Shadowfang Keep's Courtyard Door 18895 is active for a few waypoints of
// Deathstalker Adamant's escort and then removed.
inline bool WaitSightSeen(const Run& r, const PlanStep& s)
{
    for (const WaitSight& w : r.objectsSeen)
        if (w.entry == s.entry && ai::PlanDistance(w.x, w.y, w.z, s.x, s.y, s.z) <= kWaitSightYd)
            return true;
    return false;
}

// Per tick, for the current step (before any fight handling): a wait_object step that reads its object found records
// the sighting; one that reads it not found, readably, counts the time (an unreadable read or a found one starts it over).
inline void UpdateObjectSighting(Run& r, const PlanStep& s, const Facts& f, std::uint32_t dt)
{
    if (s.kind != PlanStepKind::WaitObject)
    {
        r.goneMissMs = 0;
        return;
    }
    if (r.goneMissStep != r.step)
    {
        r.goneMissStep = r.step;
        r.goneMissMs = 0;
    }
    if (f.objectState >= 0)
    {
        if (!WaitSightSeen(r, s))
            r.objectsSeen.push_back({s.entry, s.x, s.y, s.z});
        r.goneMissMs = 0;
    }
    else if (f.objectReadable && f.leadAlive)
        r.goneMissMs += dt;
    else
        r.goneMissMs = 0;
}

inline bool WaitObjectGoneIsDone(const Run& r, const PlanStep& s, const Facts& f)
{
    return s.kind == PlanStepKind::WaitObject && (s.flags & ai::kPlanFlagGoneIsDone) && f.objectState < 0 &&
        f.objectReadable && r.goneMissStep == r.step && r.goneMissMs >= kGoneConfirmMs && WaitSightSeen(r, s);
}

inline void GoneStepDone(Run& r, const PlanStep& s)
{
    NextStep(r, std::string(ai::PlanStepKindName(s.kind)) + " " + std::to_string(s.entry) + ": object gone: counted as done");
}

// A gossip NPC the module found dead (cmd gossip, ZF r12-19z29 step 220: Weegli Blastfuse 7607 lay dead at
// the door): the module's no-npc text then carries "a dead one lies nearby: <name> (<entry>) lies dead N yd
// away and respawns in <time> (...)". None is alive within the order's 40 yd, and a resume sends the same
// order into the same search, so the first refusal pauses plainly. The words claim no more than the module
// found: an NPC of the entry farther away than 40 yd is not ruled out. "" when the answer does not carry the
// mark. `what` is the step's own words ("gossip 7607 option 1").
constexpr const char* kGossipNpcDeadMark = "lies dead";

// The dead NPC's words in the answer: "Sergeant Bly (7604)", "the NPC" when none. "" without the mark.
inline std::string GossipNpcDeadSubject(const std::string& answer)
{
    std::size_t const mark = answer.find(kGossipNpcDeadMark);
    if (mark == std::string::npos)
        return std::string();
    std::size_t from = answer.rfind(": ", mark);
    from = from == std::string::npos ? 0 : from + 2;
    std::string subject = answer.substr(from, mark - from);
    while (!subject.empty() && subject.back() == ' ')
        subject.pop_back();
    return subject.empty() ? std::string("the NPC") : subject;
}

inline std::string GossipNpcDeadNote(const std::string& what, const std::string& answer)
{
    std::size_t const mark = answer.find(kGossipNpcDeadMark);
    if (mark == std::string::npos)
        return std::string();
    std::string const subject = GossipNpcDeadSubject(answer);
    std::string respawn;
    std::string const key = "respawns in ";
    std::size_t const at = answer.find(key, mark);
    if (at != std::string::npos)
    {
        std::size_t const start = at + key.size();
        std::size_t end = answer.find_first_of("(.", start);
        end = end == std::string::npos ? answer.size() : end;
        std::string time = answer.substr(start, end - start);
        while (!time.empty() && time.back() == ' ')
            time.pop_back();
        if (!time.empty())
            respawn = ", respawns in " + time;
    }
    else if (answer.find("no respawn timer", mark) != std::string::npos)
        respawn = ", it never respawns";
    return what + " cannot be done: none alive within 40 yd, a dead one lies nearby (" + subject + respawn +
        "). .bot run resume will not help while it lies dead; .bot run skip goes on without it (a later step that "
        "needs this NPC may fail too), .bot run stop ends the run";
}

// ai::kPlanFlagDeadNpcIsDone: a gossip row with the flag counts the NPC being dead as done. `answer` is the module's text for the
// refused order or the failed task; the dead-NPC wording (GossipNpcDeadNote's mark) is what counts: any other refusal still pauses.
// True (and the step ended, with the note) when it applied.
inline bool GossipDeadIsDone(Run& r, const PlanStep* s, const std::string& answer)
{
    if (!s || s->kind != PlanStepKind::Gossip || !(s->flags & ai::kPlanFlagDeadNpcIsDone))
        return false;
    std::string name = GossipNpcDeadSubject(answer);
    if (name.empty())
        return false;
    // "Sergeant Bly (7604)" -> "Sergeant Bly": the step's own words already name the entry.
    std::string const entry = " (" + std::to_string(s->entry) + ")";
    if (name.size() > entry.size() && name.compare(name.size() - entry.size(), entry.size(), entry) == 0)
        name.erase(name.size() - entry.size());
    NextStep(r, "gossip " + std::to_string(s->entry) + ": " + name + " lies dead, nothing to talk to: the step is counted done "
        "(the plan marks a dead NPC as done)");
    return true;
}

// Uldaman r16-19z34 step 39: a guard return walked a partial path to its end and
// left the tank on the dead-end tip of a ramp, 2.4 yd above a floor the navmesh
// does not link it to. The walk back to the stand point was refused unreachable
// ("going on from here"), then the step's own move to the same point with no ring
// point either: a pause, and a resume paused again. The one reading said island,
// but neither refusal was a second sign (IslandConfirmed), so the hop off it
// never ran. Such a refusal now is one, but only with source-side evidence: an
// `unreachable` whose refused path ended where the lead bot stands
// (LeadNoProgressRefusal; the glue marks it). Only for a plain move step's own point (or the walk back to its
// stand), off a walk back and a detour, while the last path read got nowhere,
// nowhere a hop found no spot (noSpotKnown), within the hop caps (kIslandEscapes,
// kStepEscapes) and at most kStepEscapes times a step (a hop refused "wait" gives
// its caps back). Past them the refusal pauses or goes on as before.
inline bool PathRefusalIslandSign(const Run& r, const std::string& answer)
{
    const PlanStep* s = Current(r);
    bool const noSpotHere =
        r.noSpotKnown && ai::PlanDistance(r.leadLastX, r.leadLastY, r.leadLastZ, r.noSpotX, r.noSpotY, r.noSpotZ) <= kArriveYd;
    return r.state == State::Running && s && s->kind == PlanStepKind::Move && !r.walkBack && !r.detour &&
        LeadNoProgressRefusal(answer) && !r.leadPathGoodNow && !noSpotHere && !r.islandSuspect &&
        r.islandEscapes < kIslandEscapes && r.stepEscapes < kStepEscapes && r.islandRefusals < kStepEscapes;
}

inline void TakeIslandSign(Run& r)
{
    ++r.islandRefusals;
    r.islandSuspect = true; // the next Begin tick's MoveStep (or ApproachHop) hops it off (IslandConfirmed)
    EnterPhase(r, Phase::Begin);
}

// The glue: the step's own move refused unreachable and no detour point found
// (NearestReachablePoint). True when it was taken as the island's second sign
// (the next tick hops the lead bot off); false: OrderRefused, the pause, as before.
inline bool RefusedFromIsland(Run& r, const std::string& order, const std::string& answer)
{
    const PlanStep* s = Current(r);
    if (!s || order != MoveOrder(s->x, s->y, s->z, false) || !PathRefusalIslandSign(r, answer))
        return false;
    TakeIslandSign(r);
    // The guard's walk back to the step's own point is this same order: it is not sent again.
    if (r.guardReturn && order == MoveOrder(r.guardX, r.guardY, r.guardZ, false))
        r.guardReturn = false;
    r.note = "cmd " + order + " refused (" + answer + "), no reachable point near it either: the lead bot may stand on a " +
        "navmesh island: a straight hop off it first";
    return true;
}

inline std::size_t LastReachedBefore(const Run& r, std::size_t idx);
inline bool ReplayWalkBack(Run& r, const Facts& f); // defined below (StrandFailed's fallback); body pins ReplayStartPoint

// The last resort for a stranded lead bot (owner decision 2026-10-23; Sunken
// Temple r26 slot 6b step 233: the lead bot went 13 yd down a ledge onto an
// island with no navmesh route back, and four resumes paused within 6 s each,
// the hop off it finding no spot and the walk back refused unreachable). When
// the walk back to the last point the lead bot reached on the plan (the route
// checkpoint, LastReachedBefore) is refused unreachable from the spot where
// the hop off the island found no spot (noSpotKnown), the run teleports the
// lead bot to that point: out of combat only, at most once a step (Run::strandStep,
// set when the lead bot stands at the point after it), at most kStrandTries
// failed tries a step, the module's own teleport (the glue's RunStrandedTeleport).
// The party left behind is then summoned to it (StrandedTeleportTick).
inline bool StrandedLead(const Run& r, const std::string& answer)
{
    if (r.state != State::Running || !r.plan || !r.walkBack || r.walkBackIdx >= r.step || r.strandDue ||
        r.strandOn || r.strandStep == r.step || (r.strandCountStep == r.step && r.strandTries >= kStrandTries))
        return false;
    if (answer.compare(0, 11, "unreachable") != 0)
        return false;
    if (r.walkBackIdx != LastReachedBefore(r, r.step))
        return false;
    return r.noSpotKnown &&
        ai::PlanDistance(r.leadLastX, r.leadLastY, r.leadLastZ, r.noSpotX, r.noSpotY, r.noSpotZ) <= kArriveYd;
}

// This step's tries and waits of the stranded lead bot's teleport (Run::strandCountStep).
inline void StrandCountsForStep(Run& r)
{
    if (r.strandCountStep == r.step)
        return;
    r.strandCountStep = r.step;
    r.strandTries = 0;
    r.strandWaits = 0;
}

// A regroup the run started by itself after a death (the wipe regroup, LeadRevivedAway): the lead bot came back at a
// new spot (a corpse run), so the rescue's once-a-step latch (Run::strandStep) is armed again and the walk back after
// it may teleport again on the same step (Blackfathom Deeps r29 slot 6: a placed run's second wipe at step 574 found
// the latch used by the first and walked from the entrance for 23 minutes). Bounded by kAutoRegroupsPerStep; a resume
// (ResetStep) still keeps the latch, and this step's failed tries (strandTries, strandWaits) stay counted. The death
// pauses past kAutoRegroupsPerStep (the wipe's, LeadRevivedAway's, WaitForFallen's: 'regroups by itself at this step
// did not hold') arm it too, so the walk back after their resume may teleport once more (Windhorn Canyon r34 slot B
// wipe 3 and Hateforge Quarry r32 slot E wipe 5: the resume found the latch used by the wipe before and replayed the
// placed route from its start, 5-9 min); a resume after any other pause does not.
inline void RearmRescueAfterDeath(Run& r)
{
    r.strandStep = static_cast<std::size_t>(-1);
}

// A failed teleport of the stranded lead bot (refused, or the lead bot not at the
// point after it): due again on the next tick, kStrandTries times a step; then the
// walk back goes on as before (the point is passed when refused again) — or as the replay when the failed teleport
// was a ReplayRescue's (Run::strandReplay): the straight way the run had just refused is skipped, the replay of the
// plan's points (spec 2B) starts. The note keeps the teleport's "failed 3 times" text; the replay's own note says so.
inline void StrandFailed(Run& r, const std::string& why)
{
    StrandCountsForStep(r);
    ++r.strandTries;
    if (r.strandTries < kStrandTries)
    {
        r.strandDue = true;
        r.note = "the teleport of the stranded lead bot failed (" + why + "): trying again (" +
            std::to_string(r.strandTries + 1) + " of " + std::to_string(kStrandTries) + ")";
        return;
    }
    bool const replay = r.strandReplay;
    r.strandReplay = false;
    r.strandBotOnly = false;
    r.strandFromOtherPart = false;
    if (replay && r.walkBack && r.plan)
    {
        // The lead bot's spot is unknown here (StrandFailed has no Facts), so replay from the rescue's spot
        // (RescueDue kept the send's facts as strandX/Y/Z). The next walk-back tick walks that replay's point.
        Facts re;
        re.leadX = r.strandX;
        re.leadY = r.strandY;
        re.leadZ = r.strandZ;
        if (ReplayWalkBack(r, re))
        {
            r.note = "the teleport of the stranded lead bot failed " + std::to_string(kStrandTries) + " times (" + why +
                "): replaying the plan's points";
            return;
        }
        r.note = "the teleport of the stranded lead bot failed " + std::to_string(kStrandTries) + " times (" + why +
            "): the walk back goes on as before";
        return;
    }
    r.note = "the teleport of the stranded lead bot failed " + std::to_string(kStrandTries) + " times (" + why +
        "): the walk back goes on as before";
}

inline bool PullbackRefusedUnreachable(Run& r, const std::string& order, const std::string& answer);

constexpr std::uint64_t kOffMapReturnRepeatMs = 30000; // one bot's returns at least this far apart
constexpr std::uint32_t kOffMapReturnTries = 3;        // returns a step per bot, then the pause naming it

inline bool OffMapReturnTick(Run& r, const Facts& f, std::string& order);

inline void OrderRefused(Run& r, const std::string& order, const std::string& answer)
{
    // The retreat at two dead (RetreatTick): a refused move ends it, the fight goes on as the bots' own; no pause.
    if (r.retreatOn && order == MoveOrder(r.anchorX, r.anchorY, r.anchorZ, false))
    {
        r.retreatOn = false;
        r.retreatDone = true;
        r.note = "retreat refused (" + answer + "): fighting";
        return;
    }
    // The test speed leash's hold on the lead bot's own spot (SpeedLeashTick) refused: cmd move refuses before it
    // touches the lead bot, so its walk is still under way. The walk goes on, the leash spent for the step; never a
    // pause (Karazhan Crypt kcleg6-t1028 step 293: the lead bot swam under the shaft's ramp lip, the floor snap lifted
    // its spot onto the ramp above and the path from the water to it was refused unreachable).
    if (r.speedLeash && !r.speedLeashOrder.empty() && order == r.speedLeashOrder)
    {
        r.speedLeash = false;
        r.speedLeashMs = 0;
        r.speedLeashSpent = true;
        r.speedLeashOrder.clear();
        r.walkStillMs = 0;
        r.note = "test speed: the hold for the party was refused (" + answer + "): walking on";
        return;
    }
    // The stranded lead bot's teleport (StrandedTeleportTick): nothing changed on
    // the lead bot. "wait" (a fight, the lead bot moved since the facts were read)
    // keeps it due, kStrandWaits times a step, then pauses; any other refusal is a
    // failed try (StrandFailed). The step is marked done only on arrival.
    if (order.compare(0, 7, "rescue ") == 0)
    {
        r.strandOn = false;
        r.strandMs = 0;
        EnterPhase(r, Phase::Begin);
        if (answer.compare(0, 4, "wait") == 0)
        {
            StrandCountsForStep(r);
            ++r.strandWaits;
            if (r.strandWaits >= kStrandWaits)
            {
                Pause(r, "the stranded lead bot at " + Num(r.strandX) + " " + Num(r.strandY) + " " + Num(r.strandZ) +
                    " could not be teleported: the teleport waited " + std::to_string(kStrandWaits) +
                    " times this step (" + answer + "); bring it to the last reached point (step " +
                    std::to_string(r.strandIdx + 1) + "), then .bot run resume");
                return;
            }
            r.strandDue = true;
            r.note = "the teleport of the stranded lead bot waits (" + answer + ") (" + std::to_string(r.strandWaits) +
                " of " + std::to_string(kStrandWaits) + ")";
            return;
        }
        StrandFailed(r, "refused: " + answer);
        return;
    }
    // The hop off a navmesh island (IslandEscape) found no spot: counted; the
    // next tick tries again or pauses.
    if (order.compare(0, 7, "escape ") == 0)
    {
        r.escapeOn = false;
        EnterPhase(r, Phase::Begin);
        bool const wait = answer.compare(0, 4, "wait") == 0;
        if (wait && r.islandEscapes > 0)
            --r.islandEscapes; // a fight or a lead bot on the move: not a try
        if (wait && r.stepEscapes > 0)
            --r.stepEscapes;
        // A walk back's hop that found no spot: from this spot the walk back goes
        // on as before (IslandConfirmed), passing the point, bounded by its own
        // ceilings. The same for the forward run's move check (escapeFromMove):
        // the move (and its detour) goes out as before, not a second hop and a pause.
        bool const fromMove = r.escapeFromMove;
        r.escapeFromMove = false;
        if (!wait && ((r.walkBack && r.walkBackIdx < r.step) || fromMove))
        {
            r.noSpotKnown = true;
            r.noSpotX = r.leadLastX;
            r.noSpotY = r.leadLastY;
            r.noSpotZ = r.leadLastZ;
        }
        r.note = "the straight hop off the navmesh island was not made (" + answer + ")" +
            (wait ? ": not counted" : "");
        return;
    }
    // A revived bot's summon (SummonRevived): refused, it walks back.
    if (order.compare(0, 7, "summon ") == 0 && r.reviveSummons.count(order.substr(7)))
    {
        ReviveSummonFailed(r, order.substr(7), "was refused (" + answer + ")");
        return;
    }
    // The summon of a direct step's follower: `busy` (a summon of that bot is
    // pending already) keeps the wait; any other refusal ends it, so the step
    // goes out again at once (and pauses if that bot is named again).
    if (order.compare(0, 7, "summon ") == 0 && !r.summonWait.empty() && order.substr(7) == r.summonWait)
    {
        if (answer.compare(0, 4, "busy") != 0)
        {
            r.summonWait.clear();
            r.summonWaitMs = 0;
        }
        r.note = order + " refused: " + answer + (r.summonWait.empty() ? "" : ": waiting for it");
        return;
    }
    // The loot method orders of a loot step's free-for-all detour (LootFfaStep). A refused `ffa` ends the
    // detour: the step goes on the old way. A refused restore only notes it: the next tick sees the method
    // is not back, and the glue puts it back when the run pauses or stops.
    if (order == "lootmethod ffa" && r.lootFfa == LootFfa::Ordered)
    {
        r.lootFfa = LootFfa::Idle;
        r.lootFfaLive = false;
        r.note = "loot: free-for-all refused (" + answer + "): looking for another bot to take it";
        return;
    }
    if (order.compare(0, 11, "lootmethod ") == 0)
    {
        r.note = order + " refused: " + answer;
        return;
    }
    // A kill_first focus every bot refused (an evading, fleeing unit): the
    // focus is not left on it. Its unit is passed over kFocusRefusedIgnoreMs,
    // and the next tick's pick goes to the next kill_first unit, or to none
    // (the bots' own picks, the plan's order). It never pauses. DropFocus hands both the new unit's skull and the
    // previous focus unit's (not yet replaced: the refused order moved the skull) to the glue to take off.
    if (order.compare(0, 6, "focus ") == 0)
    {
        std::uint64_t const guid = std::strtoull(order.c_str() + 6, nullptr, 10);
        r.focusRefused[guid] = r.runMs + kFocusRefusedIgnoreMs;
        r.focusUnstayed = true;
        if (r.focusGuid == guid)
            DropFocus(r);
        r.note = order + " refused: " + answer + ": the focus is picked again (that unit left out " +
            std::to_string(kFocusRefusedIgnoreMs / 1000) + " s)";
        return;
    }
    // The return of a bot outside the instance (OffMapReturnTick): never a pause; the next one after its 30 s.
    if (order.compare(0, 7, "return ") == 0)
    {
        r.note = order + " refused: " + answer + ": tried again in " + std::to_string(kOffMapReturnRepeatMs / 1000) +
            " s";
        return;
    }
    // The run leader's walk of one bot (the straggler ladder, a rogue back from a failed Sap): help, never a pause.
    if (StragglerWalkRefused(r, order, answer))
        return;
    if (order == "rest" || order == "revive party" || order.compare(0, 7, "summon ") == 0 ||
        order.compare(0, 3, "cc ") == 0 || order.compare(0, 7, "attack ") == 0)
    {
        // The rest gate, the regroup and the gather have their own ceilings;
        // crowd control and the tank's pick-up (GuardTarget) are help, the
        // fight goes on without them (a refused focus: above).
        r.note = order + " refused: " + answer;
        return;
    }
    // The rez hold's nudge (the lead bot walks onto a body). A path refusal (the ledge is not on the navmesh)
    // ends the hold: the revive and the summon of the revived bot go out instead of a pause. Any other refusal
    // only notes it; the hold's own budget ends it, and the nudge is not sent a second time.
    if (!r.rezNudgeOrder.empty() && order == r.rezNudgeOrder)
    {
        r.rezNudgeOrder.clear();
        if (IsPathRefusal(answer))
        {
            r.rezBodyUnreachable = true;
            r.note = "regroup: the lead bot cannot walk to the body (" + answer + "): reviving instead";
        }
        else
            r.note = "regroup: the walk to the body was refused (" + answer + ")";
        return;
    }
    if (!r.gatherOrder.empty() && order == r.gatherOrder)
    {
        r.gatherOrder.clear();
        bool const again = ++r.gatherRefusals < kGatherTries;
        r.gatherWaived = !again;
        if (r.regroup)
            r.regroupGatherSent = false;
        else
            EnterPhase(r, Phase::Begin);
        r.note = "gather refused (" + answer + "): " + (again ? "sending it once more" : "going on without it");
        return;
    }
    // The walk back to the stand point after a pick-up (GuardTarget): the
    // step goes on from where the lead bot stands; refused for want of a path
    // from it, the lead bot is hopped off first (PathRefusalIslandSign).
    if (r.guardReturn && order == MoveOrder(r.guardX, r.guardY, r.guardZ, false))
    {
        r.guardReturn = false;
        EnterPhase(r, Phase::Begin);
        if (PathRefusalIslandSign(r, answer))
        {
            TakeIslandSign(r);
            r.note = "back to the stand point refused (" + answer + "): the lead bot may stand on a navmesh island: " +
                "a straight hop off it first";
            return;
        }
        r.note = "back to the stand point refused (" + answer + "): going on from here";
        return;
    }
    const PlanStep* s = Current(r);
    // A direct step (the step's own or a walk back's) refused because a
    // follower has no way onto the lead bot's line and no straight line of its
    // own (it stands over the navmesh hole the step before crossed, or was left
    // behind): that follower is summoned to the lead bot, and the step goes out
    // again. Summoned again by DirectSummonAllowed (after kDirectResummonMs, or
    // after it died and came back; at most kDirectSummonCap a step), not once
    // per step: r12-19z29 the revive's summon used that once, and the refusal
    // paused the run. No summon allowed yet: the run waits for that bot
    // (directWaitFor, guarding on) until DirectPauseDue, then the pause below.
    if (order.size() > 7 && order.compare(order.size() - 7, 7, " direct") == 0)
    {
        std::string const name = DirectStragglerName(answer);
        if (!name.empty())
        {
            NoteDirectTrouble(r);
            if (DirectSummonAllowed(r, name))
            {
                MarkSummoned(r, name);
                r.summonDue = name;
                EnterPhase(r, Phase::Begin);
                r.note = "direct step: " + name + " is not with the lead bot: summoning it";
                return;
            }
            if (!DirectPauseDue(r, name))
            {
                r.directWaitFor = name;
                r.directWaitMs = 0;
                EnterPhase(r, Phase::Begin);
                auto const it = r.stepSummons.find(name);
                std::uint32_t const summons = it == r.stepSummons.end() ? 0 : it->second.count;
                r.note = "direct step: " + name + " is not with the lead bot (summoned " + std::to_string(summons) +
                    " times this step): waiting for it to come";
                return;
            }
        }
    }
    if (SpentRitual(r, order, answer))
    {
        Pause(r, SpentRitualNote("ritual " + std::to_string(s ? s->entry : 0)));
        return;
    }
    // A pull target the module refuses as such (invalid-target: not an enemy,
    // or the group cannot attack it; Zul'Farrak's Sul'lithuz Hatchlings 8130)
    // is left out for the run; the step goes on with what is left. An `unreachable` one of the step's own pull goes the
    // approach's way (PullbackRefusedUnreachable); other refusals of a pullback (a busy tank, no tank, the lead bot
    // unavailable) pause as before.
    if (order.compare(0, 9, "pullback ") == 0 && answer.compare(0, 14, "invalid-target") == 0)
    {
        std::uint64_t const guid = std::strtoull(order.c_str() + 9, nullptr, 10);
        r.unreachable.insert(guid);
        if (r.pullGuid == guid)
            r.pullGuid = 0;
        EnterPhase(r, Phase::Begin);
        r.note = "pull refused (" + answer + "): that unit is left out";
        return;
    }

    // The step's own pull refused for the tank's firing position: the approach's way out, not the pause below.
    if (PullbackRefusedUnreachable(r, order, answer))
        return;

    if (r.walkBack && r.plan && r.walkBackIdx < r.step)
    {
        const PlanStep& p = r.plan->steps[r.walkBackIdx];
        if (order == MoveOrder(p.x, p.y, p.z, p.kind == PlanStepKind::MoveDirect))
        {
            std::string const point = "walk back: the point of step " + std::to_string(r.walkBackIdx + 1);
            if (IsPathRefusal(answer) && DirectStillToCross(r, r.walkBackIdx))
            {
                Pause(r, point + " refused (" + answer + "): a direct step over a navmesh hole is not passed; " +
                    "gather the party at the point before it, then .bot run resume");
                return;
            }
            if (IsPathRefusal(answer) && StrandedLead(r, answer))
            {
                // No hop off and no way back: the teleport, the next walk-back tick out of combat.
                StrandCountsForStep(r);
                r.strandDue = true;
                r.strandBotOnly = false; // an older rescue: never a bot-only kind
                r.strandFromOtherPart = false; // ... nor an other-part rescue
                r.strandIdx = r.walkBackIdx;
                r.strandX = r.leadLastX;
                r.strandY = r.leadLastY;
                r.strandZ = r.leadLastZ;
                r.note = point + " refused (" + answer + ") where the hop off the island found no spot: the lead bot " +
                    "is stranded; it is teleported to the last reached point out of combat";
                EnterPhase(r, Phase::Begin);
                return;
            }
            if (IsPathRefusal(answer))
            {
                // The point itself cannot be walked to now: the next one.
                PassWalkBackPoint(r, r.leadLastX, r.leadLastY, r.leadLastZ, point + " refused (" + answer + ")",
                    p.kind == PlanStepKind::MoveDirect);
                return;
            }
            // The lead bot cannot go right now (dead, teleporting, busy ...):
            // the same point again, a few times, then pause.
            if (++r.walkBackRefusals > kWalkBackRetries)
            {
                Pause(r, point + " refused " + std::to_string(r.walkBackRefusals) + " times (" + answer + ")");
                return;
            }
            r.note = point + " refused (" + answer + "): sending it again";
            EnterPhase(r, Phase::Begin);
            return;
        }
    }
    if (s && s->kind == PlanStepKind::UseItem && s->item && !r.missing.empty())
    {
        // A use that cannot work because an earlier item is missing (a
        // reagent): a consequence, not a second failure.
        r.missing.insert(s->item);
        NextStep(r, "use_item " + std::to_string(s->entry) + " refused (" + answer + "): an earlier item is missing; " +
            "item " + std::to_string(s->item) + " is noted missing too");
        return;
    }
    if ((order.compare(0, 5, "loot ") == 0 || order.compare(0, 5, "open ") == 0) && GoneIsDone(s, answer))
    {
        GoneStepDone(r, *s);
        return;
    }
    if (s && (s->kind == PlanStepKind::Loot || s->kind == PlanStepKind::UseItem) && s->item)
        r.pendingMissing = s->item;
    if (s && s->kind == PlanStepKind::Loot && order.compare(0, 5, "loot ") == 0 && LootRefusalOpen(answer))
    {
        LootRefused(r, "cmd " + order + " refused: " + answer);
        return;
    }
    if (s && s->kind == PlanStepKind::Gossip && order.compare(0, 7, "gossip ") == 0)
    {
        if (GossipDeadIsDone(r, s, answer))
            return;
        std::string const dead = GossipNpcDeadNote(
            "gossip " + std::to_string(s->entry) + " option " + std::to_string(s->param), answer);
        if (!dead.empty())
        {
            Pause(r, dead);
            return;
        }
    }
    Pause(r, "cmd " + order + " refused: " + answer + MissingHint(r));
}

inline bool NeedsRest(const Facts& f, const RestLines& l)
{
    return f.minHealthPct < l.startHealth || f.minHealerManaPct < l.startHealerMana || f.minManaPct < l.startMana;
}

inline bool Rested(const Facts& f, const RestLines& l)
{
    return f.minHealthPct >= l.endHealth && f.minHealerManaPct >= l.endHealerMana && f.minManaPct >= l.endMana;
}

// ---------------------------------------------------------------------------
// Rest to what the next step needs (WASTED-TIME 2026-10-23 sink 3: 203 min of rest in 31 runs, 6.5 min a run, 104
// gates of 30 s or more; many were sips between small packs). The rest gate after a fight and the meal wait use the
// light lines when the next fight within kRestLookaheadSteps steps is a plain pull (or no fight is ahead: moves,
// loot, doors), and the full lines (RestLines, unchanged) before a boss, a hold, a ritual, a scripted event or a pull
// whose aggro estimate read kBigPackJoiners or more, and on a plan `rest` step. The light lines start a rest only under
// 60 % health, 35 % healer mana or 25 % other mana (the next small pull is within the party's budget above them) and
// end it at 75 / 60 / 45. The party rests together, all at once (`cmd rest`: every bot eats and drinks what it
// needs at the same time); nobody walks on while another drinks. Under the light lines a bot still eating or drinking
// holds the next order only while the lead bot itself is seated or eating (the pull would find the tank seated:
// Uldaman pulls 17 and 61) or the party is under the light end lines.
constexpr std::size_t kRestLookaheadSteps = 8;
constexpr RestLines kLightRestLines{60, 35, 25, 75, 60, 45};

inline RestLines LightLines(const RestLines& full)
{
    RestLines l;
    l.startHealth = std::min(full.startHealth, kLightRestLines.startHealth);
    l.startHealerMana = std::min(full.startHealerMana, kLightRestLines.startHealerMana);
    l.startMana = std::min(full.startMana, kLightRestLines.startMana);
    l.endHealth = std::min(full.endHealth, kLightRestLines.endHealth);
    l.endHealerMana = std::min(full.endHealerMana, kLightRestLines.endHealerMana);
    l.endMana = std::min(full.endMana, kLightRestLines.endMana);
    return l;
}

inline const PlanStep* EventHoldAhead(const Run& r);
inline bool InEvent(const Run& r);
inline std::size_t OpenUnits(const Run& r, const std::vector<UnitFact>& units);

// With the aggro estimate off (Facts::joinerCeiling 0): a pull step still to be pulled whose pack in sight has
// kBigPackUnits open units or more (the Opus review of 0d9957be: a big pack had only the estimate to show it).
constexpr std::size_t kBigPackUnits = 5;

inline bool BigPackHere(const Run& r, const Facts& f, const PlanStep& s, bool stepOver)
{
    return f.joinerCeiling == 0 && s.kind == PlanStepKind::Pull && !stepOver && f.seen &&
        OpenUnits(r, f.units) >= kBigPackUnits;
}

// Whether the next fight needs only the light lines (see above). From step `from` on (the current step, or the one
// after it when the current pull or boss has nothing left: TickBody), at most kRestLookaheadSteps steps.
inline bool LightRestAhead(const Run& r, std::size_t from)
{
    if (!r.plan || InEvent(r) || EventHoldAhead(r))
        return false;
    for (std::size_t i = from; i < r.plan->steps.size() && i < from + kRestLookaheadSteps; ++i)
    {
        const PlanStep& s = r.plan->steps[i];
        switch (s.kind)
        {
            case PlanStepKind::Boss:
            case PlanStepKind::Hold:
            case PlanStepKind::Ritual:
                return false;
            case PlanStepKind::Pull:
                if (i == r.step)
                    for (const auto& seen : r.joinerSeen)
                        if (seen.second >= kBigPackJoiners)
                            return false;
                return true;
            default:
                break; // a move, a loot, a door: the fight after it decides
        }
    }
    return true; // no fight within reach: walking on needs no full party
}

// How far the lead bot is from a plain walk's point for the stalled arrival
// (kStalledArriveYd): the ground-plane distance while the point is no more than
// kStandZTolYd above or below it (a stand on a ledge a few yards off the lead
// bot's floor, which its horizontal reach covers), else the 3D distance (a real
// other floor). Ragefire Chasm: stands 3-4 yd above or below at 6.0-6.7 yd in
// 3D paused the run as stuck, 5.2-5.4 yd on the ground.
inline float StandDistance(float lx, float ly, float lz, float x, float y, float z)
{
    float const dz = lz > z ? lz - z : z - lz;
    if (dz <= kStandZTolYd)
        return std::sqrt((lx - x) * (lx - x) + (ly - y) * (ly - y));
    return ai::PlanDistance(lx, ly, lz, x, y, z);
}

// The executor's deadband for a `cmd move`: the order puts the lead bot on stay and runs
// "return to stay position", which acts only beyond GetRange("follow") of the point in raw
// 3D (StayPointReached, StayArrivalPolicy.h); inside a dungeon that range is
// AiPlayerbot.RaidFollowDistance, 5.0 yd. A stall that close is a stalled arrival already:
// StandDistance is never more than the 3D distance, and kStalledArriveYd is wider. (A
// separate deadband rule, InFollowDeadband, was removed in round 1 of the 442a2bdf review:
// on the 3D rule it added nothing.)
static_assert(kStalledArriveYd >= 5.0f, "a stall inside the executor's 5.0 yd stay range must count as arrived");

// A plain `cmd move` the run sent was acked: keep the path type its check saw
// (RemoteCommander::CheckMove's facts) for the stuck note.
inline void NoteMovePathType(Run& r, const std::string& order, std::uint32_t pathType)
{
    if (order.compare(0, 5, "move ") != 0 || order.find(" direct") != std::string::npos)
        return;
    r.movePathKnown = true;
    r.movePathType = pathType;
}

// What the stuck pause adds about the lead bot (Blackfathom Deeps 2026-10-23:
// every such pause began with the lead bot in a deep pool, its own move failing
// every 2 s, while `cmd move` had acked the point): whether it is in water, the
// path type `cmd move` saw, and whether its own move is failing, and why.
inline std::string StuckNote(const Run& r, const Facts& f)
{
    char type[16];
    std::snprintf(type, sizeof(type), "0x%x", static_cast<unsigned>(r.movePathType));
    return std::string(" (lead in water: ") + (f.leadInWater ? "yes" : "no") + "; cmd move path type " +
        (r.movePathKnown ? std::string(type) : std::string("not read")) + "; its own move " +
        (f.leadStayWalkResting ? "failing: stay walk resting after " + std::to_string(f.leadStayWalkFails) + " failed walks"
         : f.leadMoveFailed ? "failing: " + (f.leadMoveFailReason.empty() ? std::string("none") : f.leadMoveFailReason)
                            : std::string("not failing")) + ")";
}

// Whether the lead bot's own move failed for want of a path (MoveFailReasonName):
// dispatch-short (fewer than 2 points) or empty-path (no route). The stall's fallback
// runs only for those. shortcut-empty is not among them: MoveTo2 never records it (a
// path that makeShortCut empties returns true, "rebuild next tick"), so such a stall
// gets no fallback (a known limit; Codex review 2026-10-23). Never for clipped-empty (ClipPath cut the
// path at a hostile in reach or a hazard: going straight or around would walk into that
// pack or AoE), magma, cantmove or any other reason (Opus review 2026-10-23).
inline bool StallPathFailure(const std::string& reason)
{
    return reason == "dispatch-short" || reason == "empty-path";
}

// Whether a stall's `direct` move fits the direct step's limits (kStallDirectMaxYd on the
// ground, kStallDirectMaxVertical up or down); the server checks the rest (the floor
// under the point, the lead bot's sight, every follower's way onto the line).
inline bool StallDirectFits(const Facts& f, float x, float y, float z)
{
    float const ground = std::sqrt((f.leadX - x) * (f.leadX - x) + (f.leadY - y) * (f.leadY - y));
    return ground <= kStallDirectMaxYd && std::fabs(z - f.leadZ) <= kStallDirectMaxVertical;
}

// The distance from (px,py,pz) to the segment a -> b.
inline float PointToSegmentYd(float px, float py, float pz, float ax, float ay, float az, float bx, float by, float bz)
{
    float const dx = bx - ax, dy = by - ay, dz = bz - az;
    float const len2 = dx * dx + dy * dy + dz * dz;
    float t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return ai::PlanDistance(px, py, pz, ax + dx * t, ay + dy * t, az + dz * t);
}

// The glue's verdict for the stall's direct move: the lead bot in water, the line to
// the point a swim line (swimLine: WorldPosition::IsSwimLine), and no living hostile
// creature within kStallDirectHostileYd of the line.
inline bool StallDirectLineClear(bool leadInWater, bool swimLine, const std::vector<HostileSpot>& hostiles,
    float ax, float ay, float az, float bx, float by, float bz)
{
    if (!leadInWater || !swimLine)
        return false;
    for (const HostileSpot& h : hostiles)
        if (PointToSegmentYd(h.x, h.y, h.z, ax, ay, az, bx, by, bz) <= kStallDirectHostileYd)
            return false;
    return true;
}

// A wait_clear counts every hostile in the step's 2D radius (the glue lists
// only those): no dz filter during the normal wait, so a required mob idling
// on another level (Shadowfang voidwalkers, Sneed, the Aku'mai waves) still
// holds the step until it targets the party; wait_clear has no entry list.
// Only past the step's own wait does the timeout boundary drop the
// bystanders: see the WaitClear case in TickBody.
inline std::uint32_t WaitClearCount(const PlanStep& s, const Facts& f)
{
    std::uint32_t nearCount = 0;
    for (const HostileSpot& h : f.clearUnits)
    {
        float const dx = h.x - s.x, dy = h.y - s.y;
        if (std::sqrt(dx * dx + dy * dy) <= s.radius)
            ++nearCount;
    }
    return nearCount;
}

// Whether every remaining hostile stands on another floor and none fights the
// party: non-engaged and more than kWaitClearMaxDz above or below the spot.
// At the timeout such bystanders (Shadowfang Keep r38 slotA step 308: z
// ~100-157 against the spot's z 129) finish the step instead of pausing it.
// Any engaged hostile, or one within |dz| <= 10 of the spot, pauses as before.
inline bool WaitClearOffFloorOnly(const PlanStep& s, const Facts& f)
{
    bool any = false;
    for (const HostileSpot& h : f.clearUnits)
    {
        float const dx = h.x - s.x, dy = h.y - s.y;
        if (std::sqrt(dx * dx + dy * dy) > s.radius)
            continue;
        any = true;
        if (h.engaged || std::fabs(h.z - s.z) <= kWaitClearMaxDz)
            return false;
    }
    return any;
}

// Whether the glue reads the direct move's line this tick (Facts::stallDirectRead):
// a walk under way, its fallback not tried yet, within kTickMs * 3 of the stall.
inline bool StallDirectProbeWanted(const Run& r)
{
    return r.state == State::Running && r.phase == Phase::Walk && r.stallFallback == 0 &&
        r.walkStillMs + 3 * kTickMs >= kWalkRetryMs;
}

// Whether the stalled walk is the current plain move step's own walk, with no stand-in
// on yet: the walk a stand-in (UseDetour) may replace. Not a walk back's, a pull's, an
// approach hop's, a stand's or the guard's walk back.
inline bool StallStandInFits(const Run& r, float x, float y, float z)
{
    const PlanStep* s = Current(r);
    return s && s->kind == PlanStepKind::Move && !r.detour && !(r.walkBack && r.walkBackIdx < r.step) &&
        s->x == x && s->y == y && s->z == z && !IsExactPoint(r.plan, r.step) &&
        ExactLaunch(r.plan, r.step) == static_cast<std::size_t>(-1);
}

// Whether the walk under way is sent again for a lead bot left idle by a rest (Run::restWalkDue): the walk has
// gained nothing for kRestWalkResendMs, the lead bot is not moving (a slow walk is left alone), nobody is in combat
// (the lead bot itself included: a fight against units the group cannot attack leaves Run::fighting false), the
// lead bot casts nothing (a heal or buff is never cut: the walk waits for the cast to end), and its stall is
// confirmed: it is in a rest state (a meal or drink aura, or not standing) or its own last move failed
// (Facts::leadMoveFailed). Otherwise the old retry after kWalkRetryMs stands (Codex and Opus review round 1).
inline bool RestWalkResendDue(const Run& r, const Facts& f)
{
    return r.restWalkDue && r.walkStillMs >= kRestWalkResendMs && !f.leadMoving && !f.anyInCombat &&
        !f.leadInCombat && !f.leadCasting && (f.leadResting || f.leadMoveFailed);
}

// Blackfathom Deeps 2026-10-22/23 (BFD-STUCK-POINTS-2026-10-23): the lead bot stood in
// a deep pool, its own move FAILED every 2 s, and the walk re-sent the same `cmd move`
// three times and paused; a resume sent it again. The refusal path's stand-in (UseDetour)
// never ran, because a stall is no refusal. Now a walk stalled for kWalkRetryMs while the
// lead bot's own move is failing for want of a path (Facts::leadMoveFailed,
// StallPathFailure) and the party is out of combat
// first tries what a player would do: the straight `cmd move ... direct` to the point,
// only in the diagnosed case (the lead bot in water, the line a clear swim line:
// Facts::stallDirectClear) and within the direct step's limits (StallDirectFits);
// otherwise, or after a further stall, on the
// current move step's own walk, a stand-in on dry ground (the glue's NearestReachablePoint,
// dry only; StallStandIn). Each at most once per walk; then the old retries and pause.
// True: an order (or the glue's search) went out instead of the plain retry.
inline bool StallFallback(Run& r, const Facts& f, float x, float y, float z, bool direct, std::string& order)
{
    if (direct || f.anyInCombat || r.stallFallback >= 3)
        return false;
    if (r.stallFallback == 0 && (!f.leadMoveFailed || !StallPathFailure(f.leadMoveFailReason)))
        return false;
    std::string const why = "the lead bot's own move to " + Num(x) + " " + Num(y) + " " + Num(z) + " failed (" +
        (f.leadMoveFailReason.empty() ? std::string("none") : f.leadMoveFailReason) + (f.leadInWater ? ", in water" : "") + ")";
    if (r.stallFallback == 0)
    {
        r.stallFallback = 1;
        if (f.leadInWater && f.stallDirectRead && f.stallDirectClear && StallDirectFits(f, x, y, z))
        {
            r.walkStillMs = 0;
            order = MoveOrder(x, y, z, true);
            r.note = why + ": the straight move (direct) instead";
            return true;
        }
    }
    if (r.stallFallback == 1)
    {
        r.stallFallback = 2;
        if (StallStandInFits(r, x, y, z))
        {
            r.walkStillMs = 0;
            r.stallStandInWanted = true;
            r.note = why + ": looking for a stand-in on dry ground";
            return true;
        }
    }
    r.stallFallback = 3;
    return false;
}

inline std::size_t SafePointAtOrBefore(const Run& r, std::uint32_t team, std::size_t last);
inline bool ApproachWalkStuck(Run& r, const Facts& f, float dist, float x, float y, float z);

// The rescue teleport's bounds (StrandedLead's): out of combat, at most once a step, kStrandTries failed tries a step,
// AiPlayerbot.DungeonRunWalkBackRescue on. Then it is due to plan point `target` (the walk back goes on from there).
inline bool RescueAllowed(const Run& r, const Facts& f)
{
    return f.walkBackRescue && r.state == State::Running && r.plan && !f.anyInCombat && !r.strandDue && !r.strandOn &&
        r.strandStep != r.step && !(r.strandCountStep == r.step && r.strandTries >= kStrandTries);
}

inline void RescueDue(Run& r, const Facts& f, std::size_t target)
{
    StrandCountsForStep(r);
    r.strandDue = true;
    r.strandIdx = target;
    r.strandX = f.leadX;
    r.strandY = f.leadY;
    r.strandZ = f.leadZ;
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = target; // the walk back goes on from the point it is teleported to
    r.walkBackDirect = false;
    r.strandReplay = false;
    r.strandBotOnly = false; // the two bot-only kinds set it again below
    r.strandFromOtherPart = false; // OtherPartRescue sets it again below
    r.approachHops = 0;
    EnterPhase(r, Phase::Begin);
}

// The rescue's point for a lead bot revived in another part of the instance: the safe point on the way it came
// (SafePointAtOrBefore of LastReachedBefore, as every other rescue). npos when there is none.
inline std::size_t OtherPartSafePoint(const Run& r, const Facts& f)
{
    std::size_t const last = LastReachedBefore(r, r.step);
    if (!r.plan || last == static_cast<std::size_t>(-1) || last >= r.plan->steps.size())
        return static_cast<std::size_t>(-1);
    return SafePointAtOrBefore(r, f.team, last);
}

// Why the rescue teleport for a lead bot in another part (LeadInOtherPart, which the caller has asked) does not go
// now (OtherPartBlock), checked in that order, so Dead means a bot still dead is the only thing missing: a bot-only
// party (Facts::players), the rescue on, fewer than kOtherPartRescuesPerStep rescues at this step, bounded as every
// rescue (RescueAllowed: the latch, the tries, out of combat), a safe point, and nobody dead (the summons that follow
// cannot bring a dead bot; it comes up at the same door seconds later: Dire Maul West r39/r40 slot 6).
inline OtherPartBlock OtherPartRescueWhy(const Run& r, const Facts& f)
{
    if (!f.players.empty())
        return OtherPartBlock::Player;
    if (!f.walkBackRescue)
        return OtherPartBlock::Off;
    if (r.otherPartRescueStep == r.step && r.otherPartRescues >= kOtherPartRescuesPerStep)
        return OtherPartBlock::Cap;
    if (!RescueAllowed(r, f))
    {
        if (r.strandStep == r.step)
            return OtherPartBlock::Used;
        if (r.strandCountStep == r.step && r.strandTries >= kStrandTries)
            return OtherPartBlock::Tries;
        return OtherPartBlock::Busy;
    }
    if (!f.leadAlive)
        return OtherPartBlock::Busy;
    if (OtherPartSafePoint(r, f) == static_cast<std::size_t>(-1))
        return OtherPartBlock::NoPoint;
    if (f.dead > 0)
        return OtherPartBlock::Dead;
    return OtherPartBlock::None;
}

// The rescue's point when the rescue goes (OtherPartRescueWhy is None), else npos.
inline std::size_t OtherPartRescueTarget(const Run& r, const Facts& f)
{
    if (OtherPartRescueWhy(r, f) != OtherPartBlock::None)
        return static_cast<std::size_t>(-1);
    return OtherPartSafePoint(r, f);
}

// Dire Maul West r36 slot D (a full bot-led run, 2026-10-24): after each of eight wipes the party came back at the
// door at 47.5 -153.7 -2.7, 350 yd from every point of the plan, and the run paused 'the lead bot revived in another
// part of the instance' (PauseOtherPart); each time it was fixed by hand (the commander there, the bots summoned to
// him, then the walk back from the last reached point: 40 s to 4 min). The rescue teleport to the rescue's point
// (OtherPartRescueTarget) instead, armed bot-only (StrandedTeleportTick rechecks for a joined player before the
// send); its summons bring the party left at the door (out of combat). Counted for the step
// (kOtherPartRescuesPerStep). True when the rescue is due.
inline bool OtherPartRescue(Run& r, const Facts& f)
{
    std::size_t const target = OtherPartRescueTarget(r, f);
    if (target == static_cast<std::size_t>(-1))
        return false;
    float const yd = PlanNearestYd(r, f.leadX, f.leadY, f.leadZ);
    RescueDue(r, f, target);
    r.strandBotOnly = true; // armed bot-only: StrandedTeleportTick rechecks for a joined player before the send
    r.strandFromOtherPart = true; // armed by the other-part rescue: a joined player pauses instead of passing
    if (r.otherPartRescueStep != r.step)
    {
        r.otherPartRescueStep = r.step;
        r.otherPartRescues = 0;
    }
    ++r.otherPartRescues;
    r.note = "the lead bot revived in another part of the instance (" + Num(f.leadX) + " " + Num(f.leadY) + " " +
        Num(f.leadZ) + "), " + Num(yd) + " yd from the run: it is teleported to a safe point on the way it came (step " +
        std::to_string(target + 1) + ") out of combat";
    return true;
}

// A walk back whose lead bot is stuck short of its point (the walk's tries spent) is not paused: the lead bot is
// teleported to a safe point by the stranded lead bot's rescue (StrandedTeleportTick): the last point it reached on
// the plan (LastReachedBefore: the route checkpoint, else the last move point before the step), or, when a live pack
// is near that one, the latest point before it out of every live pack's reach (StraightWalkBackTarget; never the stand
// beside the pack that wiped the party: the Opus review of 0d9957be), and the walk back goes on from there. The party
// left behind is summoned to it only out of combat (StrandedTeleportTick waits for a bot that fights). Razorfen Kraul r27 slot 8: after each of three wipes the walk back's hops ended on a ledge at z 104, 37-50 yd
// above the den, and paused "the lead bot is stuck 51.7 yd from 2078.0 1961.0 63.1"; a resume did nothing. Out of
// combat only, at most once a step and kStrandTries failed tries a step (as StrandedLead), and never with the lead
// bot at that point already. True when the rescue is due.
inline bool WalkBackStuckRescue(Run& r, const Facts& f, float dist, float x, float y, float z)
{
    if (!r.walkBack || !RescueAllowed(r, f) || r.walkBackIdx >= r.step)
        return false;
    std::size_t const last = LastReachedBefore(r, r.step);
    if (last == static_cast<std::size_t>(-1) || last >= r.plan->steps.size())
        return false;
    std::size_t const target = SafePointAtOrBefore(r, f.team, last);
    if (target == static_cast<std::size_t>(-1))
    {
        // Never a teleport beside a live pack: the pause says so.
        Pause(r, "walk back: the lead bot is stuck " + Num(dist) + " yd from " + Num(x) + " " + Num(y) + " " + Num(z) +
            StuckNote(r, f) + "; no point on the way it came is out of every live pack's reach for the rescue: bring " +
            "the lead bot to the party's way, then .bot run resume");
        return true;
    }
    const PlanStep& p = r.plan->steps[target];
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) <= kArriveYd)
        return false;
    RescueDue(r, f, target);
    r.note = "walk back: the lead bot is stuck " + Num(dist) + " yd from " + Num(x) + " " + Num(y) + " " + Num(z) +
        StuckNote(r, f) + ": it is teleported to a safe point on the way it came (step " + std::to_string(target + 1) +
        ") out of combat";
    return true;
}

// The test speed's leash (lead decision 2026-10-25, SCHOLO-REFAST-AUDIT 1.5): at 3x the followers fall 80-127 yd
// behind (a core follow runs at the bot's own rate and never closes a gap; every cast or meal opens one 3x as fast),
// and the party reached packs split. Only on a fast test walk: never at 1.0, with a real player in the party this
// tick or blocked by one before, while the walking rule is off (SpeedWalking: a fight, a meal, a pull out or being
// set up, a pack in sight), on a direct step, with the lead bot in water (a stop could cut a swim spline), in a
// fight or an event.
inline bool SpeedLeashApplies(const Run& r, const Facts& f, bool direct)
{
    return r.testSpeed > 1.0f && f.players.empty() && !r.speedPlayerBlocked && r.speedNow > 1.0f && !direct &&
        !f.leadInWater && !f.anyInCombat && !f.leadInCombat && !r.fighting && !InEvent(r) && SpeedWalking(r, f);
}

// The living bot farthest from the lead bot (Facts::members), or none. Only living bots on the plan's map count:
// Members holds living bots only; a bot off the map or whose distance is unknown (1000 yd) never holds the lead.
inline const MemberFact* SpeedLeashStraggler(const Facts& f)
{
    const MemberFact* farthest = nullptr;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && !m.offPlanMap && m.yd < 1000.0f && (!farthest || m.yd > farthest->yd))
            farthest = &m;
    return farthest;
}

// One tick of the leash inside WalkTo, the walk's point x y z and the lead bot `dist` from it. True: this tick is the
// leash's (the hold order, the resume order or nothing went out): WalkTo returns false at once.
inline bool SpeedLeashTick(Run& r, const Facts& f, float x, float y, float z, bool direct, float dist,
    std::uint32_t dt, std::string& order)
{
    const MemberFact* farthest = SpeedLeashStraggler(f);
    std::string const who = farthest ? farthest->name + " " + Num(farthest->yd) + " yd behind" : Num(f.maxMemberYd) + " yd";
    if (r.speedLeash)
    {
        r.speedLeashMs += dt;
        bool const capped = r.speedLeashMs >= kGatherCapMs;
        if (!SpeedLeashApplies(r, f, direct) || !farthest || farthest->yd <= kGatherYd || capped)
        {
            r.speedLeash = false;
            r.speedLeashMs = 0;
            if (capped)
                r.speedLeashSpent = true;
            r.walkStillMs = 0;
            r.walkBestYd = dist;
            r.walkMoveX = f.leadX;
            r.walkMoveY = f.leadY;
            r.walkMoveZ = f.leadZ;
            if (r.legOn)
                r.legBestYd = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.legX, r.legY, r.legZ);
            order = r.legOn ? MoveOrder(r.legX, r.legY, r.legZ, false) : MoveOrder(x, y, z, direct);
            r.note = capped ? "test speed: the party did not close up within " + std::to_string(kGatherCapMs / 1000) +
                    " s (" + who + "): walking on"
                            : "test speed: the party closed up, walking on";
            return true;
        }
        r.walkStillMs = 0; // a hold is not a stalled walk (kWalkRetryMs, the stuck pause)
        r.note = "test speed: the walk waits for the party (" + who + ")";
        return true;
    }
    if (r.speedLeashSpent || !SpeedLeashApplies(r, f, direct) || !farthest || farthest->yd <= kSpeedLeashYd)
        return false;
    r.speedLeash = true;
    r.speedLeashMs = 0;
    r.walkStillMs = 0;
    order = MoveOrder(f.leadX, f.leadY, f.leadZ, false); // the lead bot holds where it stands (as the firing-stand hold)
    r.speedLeashOrder = order;
    r.note = "test speed: the walk waits for the party (" + who + ")";
    return true;
}

// Walks the party to a point. True once the lead bot is there; otherwise
// `order` may hold the move to send. Pauses after kWalkTries moves without
// progress.
inline bool WalkTo(Run& r, const Facts& f, float x, float y, float z, bool direct, std::uint32_t dt,
    std::string& order)
{
    float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, x, y, z);
    // The anchor is where the lead bot actually stands on arrival: the point,
    // or, for a stalled arrival short of it (a point off the mesh), its own spot.
    auto arrived = [&](float ax, float ay, float az)
    {
        r.speedLeash = false;
        r.speedLeashMs = 0;
        r.restWalkDue = false; // the walk after the rest is done: no later stall is taken for it
        r.anchorKnown = true;
        r.anchorX = ax;
        r.anchorY = ay;
        r.anchorZ = az;
        return true;
    };
    if (dist <= kArriveYd)
        return arrived(x, y, z);
    bool const fresh = r.phase != Phase::Walk || r.walkX != x || r.walkY != y || r.walkZ != z;
    if (fresh)
    {
        EnterPhase(r, Phase::Walk);
        r.walkX = x;
        r.walkY = y;
        r.walkZ = z;
        r.walkTries = 1;
        r.walkStillMs = 0;
        r.walkBestYd = dist;
        r.stallFallback = 0;
        r.stallStandInWanted = false;
        r.legOn = false;
        r.walkLegs = 0;
        r.legEnds.clear();
        r.walkMoveX = f.leadX;
        r.walkMoveY = f.leadY;
        r.walkMoveZ = f.leadZ;
        r.walkMovingMs = 0;
        r.speedLeash = false;
        r.speedLeashMs = 0;
        if (SpeedLeashTick(r, f, x, y, z, direct, dist, dt, order))
            return false; // a straggler already 25 yd off: hold before the walk goes out
        order = MoveOrder(x, y, z, direct);
        return false;
    }
    if (SpeedLeashTick(r, f, x, y, z, direct, dist, dt, order))
        return false;
    r.walkStillMs += dt;
    bool const moved = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.walkMoveX, r.walkMoveY, r.walkMoveZ) >= kWalkProgressYd;
    if (dist < r.walkBestYd - kWalkProgressYd)
    {
        r.walkBestYd = dist;
        r.walkStillMs = 0;
        r.restWalkDue = false; // the lead bot took the walk
    }
    else if (moved && r.walkMovingMs < kWalkMovingCapMs)
    {
        // Moving on a way that leads away first (a spiral ramp): progress, within the cap.
        r.walkMovingMs += r.walkStillMs;
        r.walkStillMs = 0;
        r.restWalkDue = false;
    }
    if (moved)
    {
        r.walkMoveX = f.leadX;
        r.walkMoveY = f.leadY;
        r.walkMoveZ = f.leadZ;
    }
    // A leg of a move refused too-far (TooFarLegTaken) walked, or stalled after at least
    // kLegProgressYd of way toward it: the walk's own point goes out again from here (in
    // reach now, or the next leg), not a try. Its progress is the way to the leg (a path
    // can wind away from the point first). A leg stalled with no such way is a stall like
    // any other: the stall fallback, the retries and the pause below (the Codex review of
    // f1fdeb67: a stalled leg counted as done re-sent the far point, skipped StallFallback
    // and spent the leg budget in place).
    float const toLeg = r.legOn ? ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.legX, r.legY, r.legZ) : 0.0f;
    if (r.legOn && toLeg < r.legBestYd - kWalkProgressYd)
    {
        r.legBestYd = toLeg;
        r.walkStillMs = 0;
    }
    if (r.legOn && (toLeg <= kArriveYd || (r.legStartYd - toLeg >= kLegProgressYd && r.walkStillMs >= kShortStallMs)))
    {
        r.legOn = false;
        r.walkStillMs = 0;
        r.note = "the leg " + Num(r.legX) + " " + Num(r.legY) + " " + Num(r.legZ) + " walked: on to " + Num(x) + " " +
            Num(y) + " " + Num(z) + " (" + Num(dist) + " yd)";
        order = MoveOrder(x, y, z, direct);
        return false;
    }
    // A plain walk to a point close by (kStalledArriveYd) that gains nothing at
    // all is done after 3 s: a `cmd move` to a point 3-6 yd away does not move
    // the lead bot, and the 12 s rule made every such plan row idle 13 s (WC
    // run B: 20 rows of 12-15 s, 275 s of a 101 min run; rows 28-31 were 3-6 yd
    // hops). A direct step (over a navmesh hole) keeps the 12 s rule and its
    // checkpoint.
    // A plain walk judges the stalled arrival by StandDistance (a stand on a ledge);
    // a direct step keeps the 3D distance.
    float const stallYd = direct ? dist : StandDistance(f.leadX, f.leadY, f.leadZ, x, y, z);
    if (!direct && r.walkStillMs >= kShortStallMs && stallYd <= kStalledArriveYd)
        return arrived(f.leadX, f.leadY, f.leadZ);
    if (r.walkStillMs >= kWalkRetryMs && stallYd <= kStalledArriveYd)
        return arrived(f.leadX, f.leadY, f.leadZ);
    // The first walk after a rest that the lead bot has not started (Run::restWalkDue, RestWalkResendDue): sent
    // again once, the glue standing the lead bot up first (Run::leadWalkReset). Not a try.
    if (RestWalkResendDue(r, f))
    {
        r.restWalkDue = false;
        r.leadWalkReset = true;
        r.walkStillMs = 0;
        order = MoveOrder(x, y, z, direct);
        r.note = "the lead bot did not take the walk after the rest: the walk again, the lead bot stood up";
        return false;
    }
    if (r.walkStillMs >= kWalkRetryMs)
    {
        if (StallFallback(r, f, x, y, z, direct, order))
            return false;
        if (r.walkTries >= kWalkTries)
        {
            if (WalkBackStuckRescue(r, f, dist, x, y, z))
                return false;
            if (ApproachWalkStuck(r, f, dist, x, y, z))
                return false;
            Pause(r, "the lead bot is stuck " + Num(dist) + " yd from " + Num(x) + " " + Num(y) + " " + Num(z) +
                StuckNote(r, f));
            return false;
        }
        ++r.walkTries;
        r.walkStillMs = 0;
        order = MoveOrder(x, y, z, direct);
    }
    return false;
}

inline float GroundDistance(float ax, float ay, float bx, float by)
{
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

// Where a lead bot standing too near exact waypoint `idx` to be moved onto it
// (kExactBackYd on the ground plane: the module carries out no shorter move)
// walks back to first: the latest plan point before it on the run's own route
// that is a plain move, at least kExactBackYd from the lead bot and from the
// waypoint on the ground plane and within kSeeYd of the waypoint. Never past a
// direct step (back over a hole). False: none.
inline bool ExactBackPoint(const Run& r, const Facts& f, std::size_t idx, float& x, float& y, float& z)
{
    const PlanStep& p = r.plan->steps[idx];
    for (std::size_t i = idx; i-- > 0;)
    {
        const PlanStep& q = r.plan->steps[i];
        if (q.kind == PlanStepKind::MoveDirect)
            return false;
        if (q.kind != PlanStepKind::Move)
            continue;
        if (GroundDistance(q.x, q.y, f.leadX, f.leadY) < kExactBackYd || GroundDistance(q.x, q.y, p.x, p.y) < kExactBackYd)
            continue;
        if (ai::PlanDistance(q.x, q.y, q.z, p.x, p.y, p.z) > kSeeYd)
            continue;
        x = q.x;
        y = q.y;
        z = q.z;
        return true;
    }
    return false;
}

// Walks the party to exact waypoint `idx` (ai::kPlanFlagExact) and proves the
// lead bot stands on it. True once proved: within kExactArriveYd of the point,
// not moving, and on one spot (kExactStillYd) for kExactProofMs; the step is
// then done and Run::exactProved names it. The rules of the plain walk do not
// end it: its 3 yd arrival and its stalled arrival (6 yd) only start the
// tighten. The tighten orders only while the lead bot stands (never over the
// move that carries it), after kShortStallMs of standing: a lead bot farther
// than kExactBackYd on the ground plane is sent onto the point again; a nearer
// one walks back to ExactBackPoint, then onto the point (a `cmd move` that
// short is not carried out). At most kWalkTries moves onto the point, and no
// proof within kExactCapMs out of combat pauses the run: the step after it (a
// direct step) is never sent from short of the point.
inline bool ExactWalkTo(Run& r, const Facts& f, std::size_t idx, std::uint32_t dt, std::string& order)
{
    const PlanStep& p = r.plan->steps[idx];
    std::string const what = "the exact point of step " + std::to_string(idx + 1) + " (" + Num(p.x) + " " +
        Num(p.y) + " " + Num(p.z) + ")";
    if (!r.exactOn)
    {
        if (!WalkTo(r, f, p.x, p.y, p.z, false, dt, order))
            return false;
        ResetExact(r);
        r.exactOn = true;
        EnterPhase(r, Phase::Walk);
        r.walkX = p.x;
        r.walkY = p.y;
        r.walkZ = p.z;
    }
    r.exactMs += dt;
    float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
    bool const moved = r.exactLastSet &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.exactLastX, r.exactLastY, r.exactLastZ) > kExactStillYd;
    bool const moving = f.leadMoving || moved;
    r.exactLastSet = true;
    r.exactLastX = f.leadX;
    r.exactLastY = f.leadY;
    r.exactLastZ = f.leadZ;
    // The proof: looks on one spot within kExactArriveYd, the lead bot not moving.
    if (dist <= kExactArriveYd && !moving)
    {
        if (!r.exactAnchorSet ||
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.exactAnchorX, r.exactAnchorY, r.exactAnchorZ) > kExactStillYd)
        {
            r.exactAnchorSet = true;
            r.exactAnchorX = f.leadX;
            r.exactAnchorY = f.leadY;
            r.exactAnchorZ = f.leadZ;
            r.exactStillMs = 0;
        }
        else
            r.exactStillMs += dt;
        if (r.exactStillMs >= kExactProofMs)
        {
            ResetExact(r);
            r.exactProved = idx;
            r.anchorKnown = true;
            r.anchorX = p.x;
            r.anchorY = p.y;
            r.anchorZ = p.z;
            return true;
        }
        r.note = "on " + what + ": proving the lead bot stands still on it";
        return false;
    }
    r.exactAnchorSet = false;
    r.exactStillMs = 0;
    if (r.exactMs >= kExactCapMs)
    {
        Pause(r, what + ": the lead bot was not proved to stand on it within " + std::to_string(kExactCapMs / 1000) +
            " s (" + Num(dist) + " yd away, " + (moving ? "still moving" : "not on the point") + "); the next step " +
            "is not sent from short of it: put the lead bot on the point (within " + Num(kExactArriveYd) +
            " yd), then .bot run resume");
        return false;
    }
    if (moving)
    {
        r.exactWaitMs = 0;
        r.note = "walking onto " + what;
        return false;
    }
    r.exactWaitMs += dt;
    if (r.exactWaitMs < kShortStallMs)
    {
        r.note = "walking onto " + what + " (" + Num(dist) + " yd away)";
        return false;
    }
    // Walked back to the back point (or stopped on the way): onto the point.
    if (r.exactStage == 1 &&
        GroundDistance(f.leadX, f.leadY, p.x, p.y) >= kExactBackYd - kExactArriveYd && r.exactRounds < kWalkTries)
    {
        ++r.exactRounds;
        r.exactStage = 2;
        r.exactWaitMs = 0;
        order = MoveOrder(p.x, p.y, p.z, false);
        r.note = "walking onto " + what + " from the point behind it";
        return false;
    }
    if (r.exactRounds >= kWalkTries)
    {
        r.note = "waiting on " + what + " (" + Num(dist) + " yd away)";
        return false;
    }
    float bx = 0.0f, by = 0.0f, bz = 0.0f;
    if (GroundDistance(f.leadX, f.leadY, p.x, p.y) < kExactBackYd && ExactBackPoint(r, f, idx, bx, by, bz))
    {
        r.exactStage = 1;
        r.exactBackX = bx;
        r.exactBackY = by;
        r.exactBackZ = bz;
        r.exactWaitMs = 0;
        order = MoveOrder(bx, by, bz, false);
        r.note = what + ": the lead bot stands " + Num(dist) + " yd off it, too near for a move onto it: back to " +
            Num(bx) + " " + Num(by) + " " + Num(bz) + " on the plan's way first";
        return false;
    }
    ++r.exactRounds;
    r.exactStage = 2;
    r.exactWaitMs = 0;
    order = MoveOrder(p.x, p.y, p.z, false);
    r.note = "walking onto " + what + " again (" + Num(dist) + " yd away)";
    return false;
}

// The unit to pull now: hostile, attackable by the group (UnitFact::attackable), not evading, not passed, not
// stealthed; nearest to the lead bot. When `entry` is not 0 only that entry. Null for none.
// `hop`: a hop pick (Facts::hopUnits), which also leaves out Run::neverCame and
// any unit whose entry a gossip step of the plan names (r36 shadowfang_keep slotI:
// Deathstalker Adamant 3849, the friendly gossip NPC that opens the courtyard door,
// was pulled as a hop unit and killed). Only gossip steps: other kinds name objects,
// items or encounter data, not creatures.
inline const UnitFact* PickPull(const Run& r, const std::vector<UnitFact>& units, std::uint32_t entry,
    bool hop = false)
{
    const UnitFact* best = nullptr;
    for (const UnitFact& u : units)
    {
        if (!u.hostile || !u.attackable || u.evading || u.stealthed || r.passed.count(u.guid) ||
            r.unreachable.count(u.guid))
            continue;
        if (hop && r.neverCame.count(u.guid))
            continue;
        if (hop && r.plan && ai::PlanNamesEventUnit(*r.plan, u.entry))
            continue;
        if (entry && u.entry != entry)
            continue;
        if (!best || u.distLead < best->distLead)
            best = &u;
    }
    return best;
}

// Units the step still has to deal with: hostile, alive (the glue lists only
// living ones), not passed. Evading ones count (they come back). On any step
// but a boss step a unit the group cannot attack (UnitFact::attackable) does
// not: it is never pulled, so the step would wait on it to its idle ceiling. A
// boss step keeps waiting on its boss while it cannot be attacked yet.
inline std::size_t OpenUnits(const Run& r, const std::vector<UnitFact>& units)
{
    const PlanStep* const s = Current(r);
    bool const boss = s && s->kind == PlanStepKind::Boss;
    std::size_t n = 0;
    for (const UnitFact& u : units)
        if (u.hostile && (u.attackable || boss) && !r.passed.count(u.guid) && !r.unreachable.count(u.guid))
            ++n;
    return n;
}

// Whether a unit may be pulled or crowd-controlled now (PickPull's rules).
inline bool Pullable(const Run& r, const UnitFact& u)
{
    return u.hostile && u.attackable && !u.evading && !u.stealthed && !r.passed.count(u.guid) &&
        !r.unreachable.count(u.guid);
}

// The unit of `guid` among the step's units, null when it is gone (dead, or out of the read).
inline const UnitFact* StepUnit(const Facts& f, std::uint64_t guid)
{
    for (const UnitFact& u : f.units)
        if (u.guid == guid)
            return &u;
    return nullptr;
}

// ccPending, ccResent and ccWaitMs mirror the first watched order.
inline void SyncCcWatch(Run& r)
{
    r.ccPending = r.ccWatch.empty() ? 0 : r.ccWatch.front().guid;
    r.ccResent = !r.ccWatch.empty() && r.ccWatch.front().resent;
    r.ccWaitMs = r.ccWatch.empty() ? 0 : r.ccWatch.front().waitMs;
}

// The order that takes a bot off its cc mark (`cc clear <bot> <mark> <guid>`):
// its "rti cc" goes to none, so no class row of it fires on the mark (a
// warlock's "fear on cc"), and the raid icon is cleared when it is still on
// that unit. The run's record of that icon stays until the facts show the
// icon off that unit (SyncCcIcons): a refused clear (the bot left the group,
// no AI) leaves it for the run's end to clear.
inline std::string CcClearOrder(const Run::CcHold& hold, std::uint64_t guid)
{
    return "cc clear " + hold.bot + " " + hold.mark + " " + std::to_string(guid);
}

// Each tick the group's icons were read: a recorded icon no longer on the
// unit the run put it on (cleared by a confirmed clear, a refused cc order that
// put it back, or moved by someone else) is no longer the run's.
inline void SyncCcIcons(Run& r, const Facts& f)
{
    if (!f.raidIconsRead)
        return;
    for (auto it = r.ccIcons.begin(); it != r.ccIcons.end();)
    {
        if (it->first < 0 || it->first >= 8 || f.raidIcons[it->first] != it->second)
            it = r.ccIcons.erase(it);
        else
            ++it;
    }
}

// The run's end, for one recorded icon: Clear it (still on the run's unit),
// Drop it (someone moved or cleared it: not the run's to touch), or Retry
// (the group could not be found now).
enum class RunIconEnd : std::uint8_t { Clear, Drop, Retry };
inline RunIconEnd RunIconAtEnd(bool groupFound, std::uint64_t live, std::uint64_t ours)
{
    if (!groupFound)
        return RunIconEnd::Retry;
    return live == ours ? RunIconEnd::Clear : RunIconEnd::Drop;
}

// In the fight after a `cc <mark>`: its aura must show on that unit within
// kCcLandMs of its bot's own fight time. Not there: the mark once more (the
// same mark and bot), then noted as not landed; a warlock is then taken off
// the mark (CcClearOrder), so its Fear row cannot fire on it. A Sap is a
// pre-pull spell (SapWait): one that has not shown when the fight starts is
// only noted. At most one order a tick. Returns true when it sent one.
inline bool CheckCc(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    bool sent = false;
    for (std::size_t i = 0; i < r.ccWatch.size();)
    {
        Run::CcWatch& w = r.ccWatch[i];
        const UnitFact* held = StepUnit(f, w.guid);
        auto const hold = r.ccHolds.find(w.guid);
        const RunCcSpell* spell = hold == r.ccHolds.end() ? nullptr : RunCcOf(hold->second.family);
        if (!held || held->polymorphed || !spell)
        {
            if (held && spell)
                r.note = "fighting: " + std::to_string(held->entry) + " is " + spell->held;
            if (held && held->polymorphed && spell)
                r.ccLandedDrop.push_back(w.guid);
            r.ccWatch.erase(r.ccWatch.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        std::string const bot = hold->second.bot;
        bool botAlive = false;
        // Counted only while the crowd control itself is in the fight.
        for (const MemberFact& m : f.members)
            if (m.name == bot)
            {
                botAlive = true;
                if (m.inCombat)
                    w.waitMs += dt;
            }
        // The crowd control's bot gone from the members (dead: the glue lists
        // living bots only) before the aura landed: no sheep comes, so the
        // mark is no longer the CC bot's to leave alone (CcOrderOut).
        if (!botAlive)
        {
            r.ccWatch.erase(r.ccWatch.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (spell->prePull)
        {
            r.note = "fighting: cc did not land on " + std::to_string(held->entry) + " (" + spell->spell + " before the pull)";
            r.ccWatch.erase(r.ccWatch.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (w.waitMs < kCcLandMs)
        {
            ++i;
            continue;
        }
        if (!w.resent && botAlive)
        {
            if (!sent)
            {
                w.resent = true;
                w.waitMs = 0;
                r.note = "fighting: the crowd control on " + std::to_string(held->entry) + " did not land: once more";
                order = "cc " + hold->second.mark + " " + std::to_string(held->guid) + " " + bot;
                sent = true;
            }
            ++i;
            continue;
        }
        if (hold->second.family == CcFamily::Banish && botAlive)
        {
            if (sent)
            {
                ++i; // the clear goes out next tick
                continue;
            }
            order = CcClearOrder(hold->second, held->guid);
            sent = true;
            r.ccHolds.erase(hold);
        }
        r.note = "fighting: cc did not land on " + std::to_string(held->entry);
        r.ccWatch.erase(r.ccWatch.begin() + static_cast<std::ptrdiff_t>(i));
    }
    SyncCcWatch(r);
    return sent;
}

// Before the pull: a Sap sent is waited for (the rogue stealths and walks to
// its mob) up to kSapWaitMs, with no gather that would call the rogue back.
// Landed: the party gathers (the rogue is by its mob), then the pull.
// Not landed in time, or its unit gone from the facts (out of the read, out of
// sight: the Sap is off): the rogue is taken off the mark first (`cc clear`,
// so it stops), the unit is no longer held and no other Sap goes out this
// step; the gather follows on the next tick. Returns true while it holds the
// pull (an order or a wait).
inline bool SapWait(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    for (std::size_t i = 0; i < r.ccWatch.size();)
    {
        Run::CcWatch& w = r.ccWatch[i];
        auto const hold = r.ccHolds.find(w.guid);
        const RunCcSpell* spell = hold == r.ccHolds.end() ? nullptr : RunCcOf(hold->second.family);
        if (!spell || !spell->prePull)
        {
            ++i;
            continue;
        }
        const UnitFact* unit = StepUnit(f, w.guid);
        if (unit && !unit->polymorphed && w.waitMs + dt < kSapWaitMs)
        {
            w.waitMs += dt;
            r.note = "waiting for " + hold->second.bot + "'s " + spell->spell + " on " + std::to_string(unit->entry) +
                " before the pull";
            SyncCcWatch(r);
            return true;
        }
        std::uint64_t const guid = w.guid;
        r.ccWatch.erase(r.ccWatch.begin() + static_cast<std::ptrdiff_t>(i));
        r.ccRegather = true;
        if (unit && unit->polymorphed)
        {
            r.note = std::to_string(unit->entry) + " is " + spell->held + ": the pull goes";
            r.ccLandedDrop.push_back(guid);
            continue;
        }
        // Off: stop the rogue before anything else goes out.
        r.note = unit ? "sap did not land on " + std::to_string(unit->entry) + " in " +
                std::to_string(kSapWaitMs / 1000) + " s: the rogue is taken off it, the pull goes without it"
                      : "sap off: its unit " + GuidArg(guid) + " left the read; the rogue is taken off it";
        order = CcClearOrder(hold->second, guid);
        r.ccSapFailed = true;
        r.sapWalkDue = hold->second.bot; // and back to the party (below), never left by its mob
        r.ccSent.erase(guid);
        r.ccHolds.erase(hold);
        SyncCcWatch(r);
        return true;
    }
    SyncCcWatch(r);
    if (r.ccRegather)
    {
        r.ccRegather = false;
        if (f.maxMemberYd > kGatherYd)
        {
            // The gather's ceiling counts from here, not from the cc order.
            std::string const why = r.note;
            r.phaseMs = 0;
            SendGather(r, f, order);
            r.note = why + "; gathering before the pull";
            return true;
        }
    }
    // A rogue whose Sap failed is walked back to the lead bot's spot by the run leader (WalkOrder: it holds there
    // until the party's next move or its fight), after the gather above so the gather's follow does not undo it.
    // Shadowfang Keep r26 slot 7 step 46: taken off 3855, the rogue stayed below the stair on its own follow and
    // the gather paused the run four times. Not a dead rogue, nor one in a fight.
    if (!r.sapWalkDue.empty())
    {
        std::string const rogue = r.sapWalkDue;
        r.sapWalkDue.clear();
        for (const MemberFact& m : f.members)
            if (m.name == rogue && f.leadAlive && !m.inCombat && !m.attackers && !f.anyInCombat)
            {
                order = WalkOrder(rogue, f.leadX, f.leadY, f.leadZ);
                r.note = "the Sap did not land: " + rogue + " walked to the lead by path, it stays with the party";
                return true;
            }
    }
    return false;
}

// The pack member the run crowd-controls before a pull of `pullGuid`: the
// best-ranked of the plan's cc entries (cc_priority, then cc_always) among
// the step's units, never the pull target, not one already done this step.
// Never the boss of a boss step or one of its own adds
// (the step's entries, killed first in its kill order): Uldaman's cc lists
// (20261021174717) name the Geomancer 4853 that Grimlok's step lists and the
// 7030 of Galgann's, and a polymorphed add of the kill order holds the fight
// up (turtle-agent remote_cc.go never sheeps the active boss's `first` adds).
inline bool BossStepUnit(const PlanStep* s, std::uint32_t entry)
{
    if (!s || s->kind != PlanStepKind::Boss)
        return false;
    return entry == s->entry || std::find(s->entries.begin(), s->entries.end(), entry) != s->entries.end();
}

// A cc order: the unit, the bot, its spell and the mark. noMarkEntry: a unit
// a bot could have held, left because no mark was free (unit null then).
struct CcPick
{
    const UnitFact* unit = nullptr;
    std::string bot;
    CcFamily family = CcFamily::None;
    std::string mark;
    std::uint32_t noMarkEntry = 0;
};

// Issue #1159: the unit as above, by rank, then the spell's reliability
// (kRunCc's order), then the nearest; only a unit some free bot has a spell
// for: the unit's creature type in the spell's (CcFits) and the unit not
// immune to it (ccImmune); a Banish only within kBanishYd of the lead bot; a
// Sap only with the party and that rogue out of combat, a rogue whose Sap
// trigger can fire (canSap), and none after a Sap that did not land this
// step. Never a kill_first unit (that list wins: it is killed, not held). A
// bot is free while no unit it holds lives (one unit per bot), and a held
// unit is never given a second one. Its mark is the first of kCcMarks whose
// raid icon is empty, or set by the run and on no living held unit; another
// icon (a player's) is never moved, and with none free no cc goes out.
inline CcPick PickCcOrder(const Run& r, const Facts& f, std::uint64_t pullGuid)
{
    CcPick pick;
    if (!r.plan)
        return pick;
    std::unordered_set<std::string> busy;
    std::unordered_set<std::string> marks;
    for (const auto& h : r.ccHolds)
        if (StepUnit(f, h.first))
        {
            busy.insert(h.second.bot);
            marks.insert(h.second.mark);
        }
    const char* mark = nullptr;
    for (const char* m : kCcMarks)
    {
        if (marks.count(m))
            continue;
        int const icon = CcMarkIcon(m);
        std::uint64_t const live = f.raidIcons[icon];
        auto const set = r.ccIcons.find(icon);
        if (!live || (set != r.ccIcons.end() && set->second == live))
        {
            mark = m;
            break;
        }
    }
    const PlanStep* const step = Current(r);
    int bestRank = 0;
    std::size_t bestSpell = 0;
    for (const UnitFact& u : f.units)
    {
        if (u.guid == pullGuid || !Pullable(r, u) || r.ccSent.count(u.guid) || BossStepUnit(step, u.entry) ||
            r.plan->KillFirstRank(u.entry) >= 0)
            continue;
        int const rank = r.plan->CcRank(u.entry);
        if (rank < 0)
            continue;
        // The most reliable spell a free bot has for it.
        std::size_t spellIdx = 0;
        const MemberFact* bot = nullptr;
        for (; spellIdx < sizeof(kRunCc) / sizeof(kRunCc[0]) && !bot; ++spellIdx)
        {
            const RunCcSpell& c = kRunCc[spellIdx];
            if (!CcFits(c.family, u.creatureType) || (u.ccImmune & CcBit(c.family)))
                continue;
            if (c.prePull && (f.anyInCombat || r.ccSapFailed))
                continue;
            if (c.family == CcFamily::Banish && u.distLead > kBanishYd)
                continue;
            for (const MemberFact& m : f.members)
                if (!m.name.empty() && !busy.count(m.name) && (MemberCc(m) & CcBit(c.family)) &&
                    !(c.prePull && (m.inCombat || !m.canSap)))
                {
                    bot = &m;
                    break;
                }
        }
        if (!bot)
            continue;
        --spellIdx; // the loop stepped past the spell it found
        bool const better = !pick.unit || rank < bestRank ||
            (rank == bestRank && (spellIdx < bestSpell || (spellIdx == bestSpell && u.distLead < pick.unit->distLead)));
        if (better)
        {
            pick.unit = &u;
            pick.bot = bot->name;
            pick.family = kRunCc[spellIdx].family;
            bestRank = rank;
            bestSpell = spellIdx;
        }
    }
    if (pick.unit && !mark)
    {
        pick.noMarkEntry = pick.unit->entry;
        pick.unit = nullptr;
        pick.bot.clear();
        pick.family = CcFamily::None;
    }
    else if (pick.unit)
        pick.mark = mark;
    return pick;
}

inline const UnitFact* PickCc(const Run& r, const Facts& f, std::uint64_t pullGuid)
{
    return PickCcOrder(r, f, pullGuid).unit;
}

// The raid icons the run set that the glue clears at the run's end (only
// where the icon is still on that unit): icon slot and unit.
inline std::vector<std::pair<int, std::uint64_t>> CcIconsToClear(const Run& r)
{
    std::vector<std::pair<int, std::uint64_t>> out(r.ccIcons.begin(), r.ccIcons.end());
    std::sort(out.begin(), out.end());
    return out;
}

// The pull stand rule is on for this pull (AiPlayerbot.DungeonRunPullStand):
// the key, a ranged weapon shot the glue could read with more range than the
// margin (the glue reads no stand below that), and not inside a scripted event.
inline bool PullStandOn(const Run& r, const Facts& f)
{
    return f.pullStand && f.leadShotYd > kShotMarginYd && !InEvent(r);
}

// The tank's shot can go out from where it stands: the unit in sight and
// within the shot's range less the module's reach margin.
inline bool ShootableFromLead(const Facts& f, const UnitFact& u)
{
    return u.losLead && u.distLead <= f.leadShotYd - kShotMarginYd;
}

// A pack's pull target (a pull step): a kill_first unit first (best rank,
// then nearest), else the nearest; never the unit the crowd control is for,
// or a crowd-controlled one, while another can be pulled. Null for none.
// With the pull stand on, among units of the same rank one the tank can shoot
// from where it stands goes before a nearer one it cannot.
inline const UnitFact* PickPackPull(const Run& r, const Facts& f)
{
    const UnitFact* cc = PickCc(r, f, 0);
    const UnitFact* best = nullptr;
    const UnitFact* held = nullptr;
    int bestRank = -1;
    bool const stand = PullStandOn(r, f);
    for (const UnitFact& u : f.units)
    {
        if (!Pullable(r, u))
            continue;
        if ((cc && u.guid == cc->guid) || r.ccSent.count(u.guid))
        {
            if (!held || u.distLead < held->distLead)
                held = &u;
            continue;
        }
        int const rank = r.plan ? r.plan->KillFirstRank(u.entry) : -1;
        bool better = !best;
        if (best)
        {
            if (rank >= 0 && (bestRank < 0 || rank < bestRank))
                better = true;
            else if (rank == bestRank && stand && ShootableFromLead(f, u) != ShootableFromLead(f, *best))
                better = ShootableFromLead(f, u);
            else if (rank == bestRank && u.distLead < best->distLead)
                better = true;
        }
        if (better)
        {
            best = &u;
            bestRank = rank;
        }
    }
    return best ? best : held;
}

// Whether the kill_first focus is bounded by distance: on a pull or move
// step outside a scripted event. A boss step and an event are not (Zum'rah's
// Ward 7785 is focused wherever it stands while the tank holds his adds).
inline bool KillFirstBounded(const Run& r)
{
    const PlanStep* s = Current(r);
    return s && !InEvent(r) &&
        (s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Move || s->kind == PlanStepKind::MoveDirect);
}

// The focused unit is gone: no longer among this fight's kill_first units (it
// died: the glue lists living ones only) or crowd-controlled by the run. Then
// there is nothing left to take the focus back from.
inline bool FocusGone(const Run& r, const Facts& f)
{
    if (r.ccSent.count(r.focusGuid))
        return true;
    for (const UnitFact& u : f.killFirst)
        if (u.guid == r.focusGuid)
            return false;
    return true;
}

// In a fight: the kill_first unit to focus (best rank, then nearest; not a
// crowd-controlled one). The unit already focused stays against units of its own rank
// (2b); only a better rank, or its death, cc or refusal, moves the focus. On a pull or move step (KillFirstBounded) only one
// within kKillFirstMaxYd of the lead bot (the tank): the focus sends every bot
// at it, and Zul'Farrak r5-19z23 step 27 lost the party twice to a focus on a
// witch doctor 20 and 43 yd off, in its camp. A farther one is left to come
// in; the bots fight with their own picks in the meantime. The bound is for a
// new focus only: the unit already focused stays eligible at any distance, so
// a better-ranked one walking back and forth across the bound switches the
// focus once, not every tick. There is no order that takes a focus back (the
// skull and every bot's attack stay), so a held focus is never dropped for
// distance: a focused unit that walks back to its camp still draws the party.
// A better rank still takes it.
// Boss steps and events: any distance, as before. Only this fight's units:
// engaged with the party, the ward of one that is, or one keeping an aura on
// one that is (UnitFact::auraOnEngaged: Hailar's Ritualists channelled their
// heal from out of combat, r19-19z37). Null for none.
inline const UnitFact* PickKillFirst(const Run& r, const Facts& f)
{
    if (!r.plan)
        return nullptr;
    bool const bounded = KillFirstBounded(r);
    const UnitFact* best = nullptr;
    int bestRank = 0;
    const UnitFact* held = nullptr;
    int heldRank = 0;
    for (const UnitFact& u : f.killFirst)
    {
        if (!u.hostile || !u.attackable || u.evading || u.stealthed || r.ccSent.count(u.guid))
            continue;
        // Its focus was refused a moment ago (kFocusRefusedIgnoreMs).
        auto const refused = r.focusRefused.find(u.guid);
        if (refused != r.focusRefused.end() && r.runMs < refused->second)
            continue;
        // Only this fight's: engaged with the party, the ward of one that is,
        // or a caster keeping an aura on one that is (a channeller).
        if (!u.engaged && !(u.owner && u.ownerEngaged) && !u.auraOnEngaged)
            continue;
        if (bounded && u.guid != r.focusGuid && u.distLead > kKillFirstMaxYd)
            continue;
        int const rank = r.plan->KillFirstRank(u.entry);
        if (rank < 0)
            continue;
        if (u.guid == r.focusGuid)
        {
            held = &u;
            heldRank = rank;
        }
        if (!best || rank < bestRank || (rank == bestRank && u.distLead < best->distLead))
        {
            best = &u;
            bestRank = rank;
        }
    }
    // The focused unit stays against one of the same rank: the nearest of a rank changes as the tank and the mobs move,
    // and each change moved the skull and every bot's attack before anything died (Scholomance r30 slot C, t 1521.6-
    // 1530.7: three Acolytes at 80-100 % focused in turn every 3 s). A better rank still takes it.
    if (held && heldRank == bestRank)
        return held;
    return best;
}

// The firing stand on the lead bot's navmesh path to a unit (the glue's
// read for Run::shotStandFor): `path` runs from the lead bot (front) toward the
// unit, `range` is Facts::leadShotYd - kShotMarginYd, `hasLos(point)` whether
// the unit is in sight from a point (at the core's sight heights).
//   - Beyond range: the ranged reach's own choice (ai::SelectRangedApproachPoint,
//     ReachTargetAction::MoveIntoRangeAlongPath): the path walked back from the
//     unit end to the first point within range that sees it, or the nearest
//     point toward it in sight when none within range does.
//   - Within range but out of sight (that choice leaves this to the chase): the
//     first point of the path, from the lead bot on, nearer the unit and in sight.
// Every stand sees the unit; none found: known false (the order goes out as before).
// The path is cut before its first point more than kStandLedgeDropYd below the
// lead bot's floor (its front): no stand lies down a ledge or past one (Sunken
// Temple r26 slot 6b step 233: firing stand 1 for 5228 took the lead bot from
// the z -172.9 floor 13 yd down onto an island with no navmesh route back, and
// the run stopped there; the owner's ledge rule of 2026-10-23 for chases, here
// for the run leader's own stand walk).
constexpr float kStandLedgeDropYd = 6.0f;

struct ShotStand
{
    bool known = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// ShotStandOnPath's choice on the path left on the lead bot's floor.
template <typename LosFn>
inline ShotStand ShotStandOnFloor(const std::vector<ai::ApproachPoint>& path, const ai::ApproachPoint& unit, float range,
    LosFn hasLos)
{
    ShotStand stand;
    if (path.size() < 2)
        return stand;
    ai::RangedApproachChoice const choice = ai::SelectRangedApproachPoint(path, unit, range, hasLos);
    if (choice.valid)
    {
        stand.known = true;
        stand.x = choice.point.x;
        stand.y = choice.point.y;
        stand.z = choice.point.z;
        return stand;
    }
    float const startSq = ai::ApproachDistanceSq(path.front(), unit);
    if (startSq > range * range)
        return stand; // beyond range and no point nearer in sight
    for (std::size_t i = 1; i < path.size(); ++i)
        if (ai::ApproachDistanceSq(path[i], unit) < startSq && hasLos(path[i]))
        {
            stand.known = true;
            stand.x = path[i].x;
            stand.y = path[i].y;
            stand.z = path[i].z;
            return stand;
        }
    return stand;
}

template <typename LosFn>
inline ShotStand ShotStandOnPath(const std::vector<ai::ApproachPoint>& path, const ai::ApproachPoint& unit, float range,
    LosFn hasLos)
{
    ShotStand stand;
    if (path.size() < 2 || range <= 0.0f)
        return stand;
    std::vector<ai::ApproachPoint> upper;
    upper.reserve(path.size());
    for (const ai::ApproachPoint& point : path)
    {
        if (path.front().z - point.z > kStandLedgeDropYd)
            break; // a ledge: nothing at or past it
        upper.push_back(point);
    }
    return ShotStandOnFloor(upper, unit, range, hasLos);
}

// The joiners the aggro estimate expects for a pull of `u`: this tick's read,
// else the last one this step (a unit held for its joiner rest is not judged
// again by the glue). 0 with the estimate off.
inline std::uint32_t ExpectedJoiners(const Run& r, const UnitFact& u)
{
    if (u.joinersRead)
        return u.joiners;
    auto const it = r.joinerSeen.find(u.guid);
    return it == r.joinerSeen.end() ? 0 : it->second;
}

// The pull stand (AiPlayerbot.DungeonRunPullStand; PLAN-PULLBACK-RANGE F1):
// `pullback` goes out only when the tank's shot can go out from where it
// stands. In the WC runs of 19z17 the tank, ordered from a spot it could not
// shoot from, walked to a firing position by itself and never fired from
// there (0 shots in 33 such pulls; 16 of 16 from a stand). So, before the
// order:
//   - a moving tank is waited for up to kShotStillWaitMs (again after each
//     stand walk);
//   - a unit out of sight or beyond leadShotYd - kShotMarginYd: the glue
//     reads a firing stand for it (one tick, Run::shotStandFor), and the party
//     walks there (WalkTo); a walk with no headway for kWalkRetryMs, or longer
//     than kShotWalkCapMs, gives the stand up and orders from here (never a
//     pause); at most kShotStandTries stands per unit and step;
//   - no stand found, or the stands used up: the order as before.
// True while the pull waits (`order` may hold the move); false: the order
// goes out now, and Run::shotHow says how its shot stood.
inline bool HoldForShot(Run& r, const Facts& f, const UnitFact& u, std::uint32_t dt, std::string& order)
{
    r.shotGateRan = true;
    // A stand request is answered (or dropped) on the very next tick: the
    // glue reads at most one stand per request.
    bool const standRead = r.shotStandFor == u.guid && f.shotStandGuid == u.guid;
    r.shotStandFor = 0;
    if (!PullStandOn(r, f))
    {
        r.shotHow = f.leadShotYd > kShotMarginYd ? "no stand rule in an event" : "no ranged weapon pull: stand rule off";
        return false;
    }
    std::string const what = std::to_string(u.entry) + " " + GuidArg(u.guid);
    std::uint32_t& tries = r.shotStandTries[u.guid];
    auto hold = [&](const std::string& note)
    {
        r.shotStandPrep = true;
        r.shotHoldGuid = u.guid;
        r.note = note;
        return true;
    };
    // The stand walk for this unit: on to its end. A walk something else
    // broke off (a gather) ends here too; its stand counted already.
    if (r.shotStandWalk && r.shotStandGuid == u.guid)
    {
        r.shotStandWalk = false;
        if (r.phase == Phase::Walk && r.walkX == r.shotStandX && r.walkY == r.shotStandY && r.walkZ == r.shotStandZ)
        {
            // Stuck (no headway for kWalkRetryMs short of the point) or too
            // long: the stand is given up before WalkTo would resend the move
            // or pause the run, and the order goes out from here.
            float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.shotStandX, r.shotStandY, r.shotStandZ);
            bool const gaining = dist < r.walkBestYd - kWalkProgressYd;
            r.shotWalkMs += dt;
            if ((!gaining && r.walkStillMs + dt >= kWalkRetryMs && dist > kStalledArriveYd) ||
                r.shotWalkMs >= kShotWalkCapMs)
            {
                // Held one tick more, without the hold's pass on the way back:
                // the next tick checks the anchor from where the tank got stuck
                // (another floor: the gather back first), then the order goes out.
                tries = kShotStandTries;
                r.shotWalkMs = 0;
                EnterPhase(r, Phase::Begin);
                r.shotHow = "firing stand unreachable: ordering from here";
                r.shotStandPrep = false;
                r.note = "firing stand unreachable for " + what + ": checking the way back before the order";
                return true;
            }
            if (!WalkTo(r, f, r.shotStandX, r.shotStandY, r.shotStandZ, false, dt, order))
            {
                if (r.state != State::Running)
                    return true; // stuck: WalkTo paused the run with its reason
                r.shotStandWalk = true;
                return hold("to firing stand " + std::to_string(tries) + " for " + what);
            }
            EnterPhase(r, Phase::Begin); // arrived (the anchor is the stand now)
        }
        r.shotStillMs = 0;
    }
    if (f.leadMoving && r.shotStillMs < kShotStillWaitMs)
    {
        r.shotStillMs += dt;
        return hold("waiting for the tank to stand still before pulling " + what);
    }
    bool const moving = f.leadMoving;
    if (ShootableFromLead(f, u))
    {
        r.shotHow = std::string(tries ? "shot from firing stand " + std::to_string(tries) : "shot from where it stands") +
            (moving ? ", still moving" : "");
        return false;
    }
    if (tries >= kShotStandTries)
    {
        if (r.shotHow.compare(0, 24, "firing stand unreachable") != 0)
            r.shotHow = "firing stands used up (" + std::to_string(tries) + ")";
        return false;
    }
    if (standRead)
    {
        if (!f.shotStandKnown)
        {
            tries = kShotStandTries;
            r.shotHow = "no firing stand found";
            return false;
        }
        if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.shotStandX, f.shotStandY, f.shotStandZ) <= kArriveYd)
        {
            // The stand is where the tank stands: walking gains nothing.
            tries = kShotStandTries;
            r.shotHow = "the firing stand is where it stands";
            return false;
        }
        ++tries;
        r.shotStandWalk = true;
        r.shotStandGuid = u.guid;
        r.shotStandX = f.shotStandX;
        r.shotStandY = f.shotStandY;
        r.shotStandZ = f.shotStandZ;
        r.shotStillMs = 0;
        r.shotWalkMs = 0;
        // A big pack (kBigPackJoiners): the walk is watched every tick by
        // BigPackWalkGate, so the lead bot never walks into reach first.
        EndBigWalk(r);
        std::uint32_t const joiners = ExpectedJoiners(r, u);
        if (joiners >= kBigPackJoiners)
        {
            r.bigWalk = true;
            r.bigWalkGuid = u.guid;
            r.bigWalkX = f.shotStandX;
            r.bigWalkY = f.shotStandY;
            r.bigWalkZ = f.shotStandZ;
            r.bigJoiners = joiners;
        }
        WalkTo(r, f, f.shotStandX, f.shotStandY, f.shotStandZ, false, dt, order);
        return hold("to firing stand " + std::to_string(tries) + " for " + what + " (" + Num(f.shotStandYd) +
            " yd from it; was " + Num(u.distLead) + " yd, los " + (u.losLead ? "yes" : "no") + ")");
    }
    r.shotStandFor = u.guid; // the glue reads it on the next tick
    return hold("reading a firing stand for " + what);
}

// Gathers the party, crowd-controls the plan's cc targets of the pack (a
// `cc <mark> <guid> <bot>` each, once per unit and step, PickCcOrder), then
// sends `pullback <guid>`.
// With the pull stand on (HoldForShot), the tank is put where it can shoot first.
// `hop`: a move's hop pull (the walk back's are hop pulls too). Such a unit is
// not the step's target: with the pull stand on, one the tank still cannot
// shoot once the stand rule gave up is left out of the hop picks (neverCame),
// with no order: the pullback would walk the tank at it (ReachPullAction), and
// in r6-19z24 step 64 that walk, the engage on arrival and the chase put the
// tank on the island of 81610 (PLAN-ZF-NOFIGHT). The judgement is made from
// where the tank stands now: a unit around a corner is left too, and fights
// when the party walks by. After a regroup cleared the set, the same rule
// leaves a still unshootable unit out again without any pull.
inline void PullUnit(Run& r, const Facts& f, const UnitFact& u, std::string& order, const UnitFact* cc = nullptr,
    bool walkBack = false, std::uint32_t dt = kTickMs, bool hop = false)
{
    hop = hop || walkBack;
    // The owner's wait rule at the pull itself (Uldaman 19z11: 17 of 63 pulls
    // went out with a member dead, 15 with the healer, two of them into
    // wipes): no pull while a bot is dead, outside a scripted event. Tick's
    // wait (WaitForFallen) regroups first; this holds if a death comes in
    // between.
    if (f.dead > 0 && !InEvent(r))
    {
        r.standPullShotGuid = 0; // the pull never went out (a death came in between)
        r.note = WaitNote(r, f) + ": no pull while a party member is dead";
        return;
    }
    // A Sap sent before this pull: wait for it, before any gather (that would
    // call the rogue back from its mob).
    if (SapWait(r, f, dt, order))
    {
        r.standPullShotGuid = 0; // the pull never went out
        return;
    }
    if (!r.hopWaiveUnits.empty() && !r.hopWaiveUnits.count(u.guid))
        DropHopWaiver(r); // another target than the units the hop was abandoned for: gather as usual
    if (r.shotStandPrep && r.shotHoldGuid != u.guid)
        r.shotStandPrep = false; // the pull stand held another unit: this one checks the way back
    // A pull the stand rule holds checked the way back on its first tick; its
    // stand walk is driven every tick, not every other one while the path back
    // is read (shotStandPrep is only set on consecutive gate ticks).
    AnchorState const anchor = r.gatherWaived || r.shotStandPrep ? AnchorState::On : CheckAnchor(r, f);
    if (anchor == AnchorState::Unknown && r.phase != Phase::Gather)
    {
        r.standPullShotGuid = 0; // the pull never went out (the way back is read first)
        r.note = "checking the way back to " + Num(r.anchorX) + " " + Num(r.anchorY) + " " + Num(r.anchorZ);
        return; // the glue reads the path next tick
    }
    if ((f.maxMemberYd > kGatherYd || anchor == AnchorState::Off) && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = anchor == AnchorState::Off ? "gathering back at " + Num(r.anchorX) + " " + Num(r.anchorY) + " " +
                Num(r.anchorZ) + " before a pull (the fight left the lead bot off the way)"
                                         : std::string("gathering before a pull");
            SendGather(r, f, order);
        }
        else if (StragglerLadderTick(r, f, order))
        {
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            GatherCeiling(r, f, order, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                " s (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)");
        }
        r.standPullShotGuid = 0; // the pull never went out (gather first)
        return;
    }
    if (!walkBack && r.pulls >= kMaxPullsPerStep)
    {
        Pause(r, std::to_string(kMaxPullsPerStep) + " pulls did not clear the step");
        r.standPullShotGuid = 0; // the pull never went out (the cap paused the step)
        return;
    }
    if (walkBack && r.walkBackPulls >= kMaxWalkBackPulls)
    {
        Pause(r, "walk back: " + std::to_string(kMaxWalkBackPulls) + " pulls on the way did not clear it");
        r.standPullShotGuid = 0; // the pull never went out (the walk-back cap paused the step)
        return;
    }
    CcPick const ccPick = cc ? PickCcOrder(r, f, u.guid) : CcPick();
    if (ccPick.unit && ccPick.unit->guid == cc->guid && cc->guid != u.guid && !r.ccSent.count(cc->guid))
    {
        // The mark goes out before the pull; a Polymorph, Shackle Undead or
        // Banish comes with the fight (the class's `cc` strategy is a combat
        // one), where the fight gate checks the aura (CheckCc). A Sap is cast
        // out of combat (the rogue's "sap cc mark"): the pull waits for it
        // (SapWait). One unit per bot, each with its own mark (PickCcOrder).
        r.ccSent.insert(cc->guid);
        r.ccHolds[cc->guid] = Run::CcHold{ccPick.bot, ccPick.mark, ccPick.family};
        r.ccIcons[CcMarkIcon(ccPick.mark)] = cc->guid; // the order moves that icon onto it
        Run::CcWatch watch;
        watch.guid = cc->guid;
        r.ccWatch.push_back(watch);
        SyncCcWatch(r);
        // Still getting the pull ready: the next tick's pull is not counted
        // as a second pull attempt (FightStep's fruitless count).
        EnterPhase(r, Phase::Gather);
        r.note = "crowd control on " + std::to_string(cc->entry) + " (" + ccPick.bot + ": " +
            RunCcOf(ccPick.family)->spell + ") before the pull";
        order = "cc " + ccPick.mark + " " + std::to_string(cc->guid) + " " + ccPick.bot;
        r.standPullShotGuid = 0; // the pull never went out (the mark goes out first)
        return;
    }
    // The pull stand (off: false at once, nothing changed): before the gather
    // waiver is spent, so a stand walk from a hop in sight keeps it. The
    // stand-pull gate's authorization covers only the unit it picked in shot.
    if (f.pullStand && r.standPullShotGuid != u.guid)
    {
        auto const done = r.pullTries.find(u.guid);
        if ((done == r.pullTries.end() || done->second < kMaxPullsPerTarget) && HoldForShot(r, f, u, dt, order))
            return;
    }
    // A gather left out holds for this pull only: the next one gathers again.
    r.gatherWaived = false;
    r.hopWaiveUnits.clear();
    r.gatherRefusals = 0;
    if (hop && PullStandOn(r, f) && !ShootableFromLead(f, u))
    {
        r.neverCame.insert(u.guid);
        r.standPullShotGuid = 0; // the pull never went out (a hop unit the tank cannot shoot is left out)
        r.note = "hop unit " + std::to_string(u.entry) + " " + GuidArg(u.guid) +
            " passed: the tank cannot shoot it from here (" + r.shotHow + "), and a hop pull never walks to it";
        r.shotStandPrep = false;
        r.shotStandFor = 0;
        r.shotStandWalk = false;
        r.shotStillMs = 0;
        r.shotHow.clear();
        EnterPhase(r, Phase::Begin);
        return;
    }
    std::uint32_t& tries = r.pullTries[u.guid];
    if (tries >= kMaxPullsPerTarget)
    {
        // Each pull started a fight, yet the unit lives: it evades, leashes
        // home or stands where the party cannot reach. Never loop on it.
        r.unreachable.insert(u.guid);
        r.standPullShotGuid = 0; // the pull never went out (the target is left out of reach)
        r.note = "unit " + std::to_string(u.entry) + " survived " + std::to_string(kMaxPullsPerTarget) +
            " pulls: left as out of reach";
        return;
    }
    ++tries;
    ++(walkBack ? r.walkBackPulls : r.pulls);
    r.pullGuid = u.guid;
    r.pullHop = hop;
    r.reachStopped = false;
    EndBigWalk(r);
    EnterPhase(r, Phase::AwaitPull);
    r.note = "pulling " + std::to_string(u.entry);
    order = "pullback " + std::to_string(u.guid);
    if (f.pullStand)
    {
        // Each order its own status line (the trace keeps only changes): the
        // target's guid, the how-many-th order to it, and how its shot stood.
        r.note += " " + GuidArg(u.guid) + " (order " + std::to_string(tries) + "; " + r.shotHow + ")";
        r.shotStandPrep = false;
        r.shotStandFor = 0;
        r.shotStandWalk = false;
        r.shotStillMs = 0;
        r.shotHow.clear();
    }
    r.standPullShotGuid = 0; // the pull went out: the authorization is spent
    // The aggro estimate (Facts::joinerCeiling): the note says how many joiners
    // the estimate expects with this pull, or that the facts carried no count
    // this step (the glue reads it only for units it judged; a unit held or out
    // of the index keeps its earlier read).
    if (f.joinerCeiling > 0)
        r.note += u.joinersRead ? "; " + std::to_string(u.joiners) + " joiners expected"
                                : std::string("; joiners not read");
}

// The step's pull is out: wait for the fight (the fight gate takes over) or
// give the pull up after kPullNoFightMs. A hop unit that never came is left
// out of the hop picks at once (Run::neverCame: the agent's one pullback); a
// step's own unit only for the step, after kPullTriesPerUnit such pulls.
inline void AwaitPull(Run& r)
{
    if (r.phaseMs < kPullNoFightMs)
        return;
    auto it = r.pullTries.find(r.pullGuid);
    if (it != r.pullTries.end() && it->second >= kPullTriesPerUnit)
    {
        r.passed.insert(r.pullGuid);
        if (r.patrolSeen.count(r.pullGuid))
            r.patrolGaveUp.insert(r.pullGuid); // a patroller that never came: not waited for again this run
    }
    if (r.pullHop)
        r.neverCame.insert(r.pullGuid);
    r.pullGuid = 0;
    r.pullHop = false;
    EnterPhase(r, Phase::Begin);
}

// The innermost ring of the stand search: kApproachStandMinYd, or the step's pull radius
// plus kApproachStandPullMarginYd when that is more (none when that is past kApproachStandYd).
inline float ApproachStandMinRing(const PlanStep& s)
{
    return std::max(kApproachStandMinYd, s.radius + kApproachStandPullMarginYd);
}

inline bool IsStepTarget(const PlanStep& s, std::uint32_t entry)
{
    return entry && (entry == s.entry || std::find(s.entries.begin(), s.entries.end(), entry) != s.entries.end());
}

// A pause of the approach (the hops, the stand): where the lead bot stood is kept, so a
// resume that finds it still there skips the walk back (ApproachResumeInPlace).
inline void PauseApproach(Run& r, const Facts& f, const std::string& why)
{
    Pause(r, why);
    r.approachPauseKnown = true;
    r.approachPauseX = f.leadX;
    r.approachPauseY = f.leadY;
    r.approachPauseZ = f.leadZ;
}

// How far the lead bot may stand from the approach pause's spot for a resume to search
// the stand from where it is, with no walk back first.
constexpr float kApproachResumeInPlaceYd = 20.0f;

// Windhorn Canyon r26 slot 5b step 68: each resume of the approach pause walked back to
// step 59 and hopped the same way into the same pause (4 pauses, ~100 s each). A resume
// whose lead bot still stands where that pause left it (the regroup's end) skips the walk
// back: the stand search goes out from there (ApproachStand), and for a pull that may be
// deferred, none found defers it (ApproachStandFound).
inline bool ApproachResumeInPlace(const Run& r, const Facts& f)
{
    return r.approachStandResumed && r.approachStandStep == r.step && r.approachPauseKnown &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.approachPauseX, r.approachPauseY, r.approachPauseZ) <=
        kApproachResumeInPlaceYd;
}

// Windhorn Canyon r26 slot 5b (2026-10-23): step 68's pack stood on the z 242 terrace,
// the party fought on the z 195-222 floor below; '8 approach hops did not bring the party
// within 60.0 yd of the pack; ... not reachable from this floor: no safe stand 15.0-25.0
// yd from it the lead bot has a path to' paused the run 4 times, and the stop rule ended
// it at step 68 of 1890. A pull whose pack is judged not reachable from this floor (no
// safe stand after hops that got no nearer, or a stand that did not bring it in sight) is
// deferred instead when PullMayBeDeferred: the run notes it, counts it not cleared (a
// walk back keeps clear of it) and goes on. Later in the same leg, once the lead bot
// stands near and level with it after a fight or a move, it is pulled from there (StepAfter);
// at the leg's end a pack never reached is skipped and listed (DoneNote, StatusLine).
// A retry that finds it not reachable again ends it the same way (no second deferral).
// False: not deferred (a boss, a gating pull): the caller pauses as before.
inline bool DeferUnreachablePack(Run& r, const PlanStep& s, const std::string& why)
{
    bool const retry = DeferRetryOn(r);
    if (!retry && !PullMayBeDeferred(r))
        return false;
    std::size_t const idx = r.step;
    std::string const pack = "pack at " + StepPoint(s);
    std::string note;
    r.notCleared.insert(idx); // a retry ending so is given up (StepAfter)
    if (retry)
        note = pack + " skipped, not cleared: still not reachable from this floor on its retry (" + why + ")";
    else
    {
        if (std::find(r.deferredPulls.begin(), r.deferredPulls.end(), idx) == r.deferredPulls.end())
            r.deferredPulls.push_back(idx);
        note = pack + " deferred: not reachable from this floor; the run goes on (" + why +
            "); it is pulled when the party passes near it later in the leg";
    }
    r.deferLog = "step " + std::to_string(idx + 1) + ": " + note;
    NextStep(r, note);
    return true;
}

// Whether a step whose approach hops got no nearer asks the glue for a stand
// (ApproachStand): a pull or boss step, its stand not tried yet this step.
inline bool ApproachStandWanted(const Run& r, const PlanStep& s)
{
    return (s.kind == PlanStepKind::Pull || s.kind == PlanStepKind::Boss) &&
        !(r.approachStandStep == r.step && r.approachStand != 0);
}

// A pull or boss step's approach found no way on: a hop that goes nowhere (ApproachHop), or the walk to a hop or to
// the stand stuck after kWalkTries (ApproachWalkStuck). As the hop cap: the stand search first (ApproachStand), once a
// step; once it was tried, the deferral (DeferUnreachablePack: a pull PullMayBeDeferred lets go); otherwise the approach
// pause. A deferred pull's retry is given up, never paused (StepAfter). Blackfathom Deeps r29 slot 6 step 580: a pack
// in the pool 9 yd below the floor paused four times ('no walkable path toward the pack') and the stop rule ended the
// run.
inline void ApproachNoWay(Run& r, const Facts& f, const PlanStep& s, const std::string& why)
{
    if (DeferRetryOn(r) && DeferUnreachablePack(r, s, why))
        return; // a retry is never a pause: given up (StepAfter)
    if (ApproachStandWanted(r, s))
    {
        r.approachStandStep = r.step;
        r.approachStand = 1;
        r.approachStandWanted = true;
        r.approachStandWhy = why;
        r.note = why + ": looking for a stand within " + Num(kApproachStandYd) + " yd of it";
        return;
    }
    if (r.approachStandStep == r.step)
    {
        r.approachStand = 3; // spent
        if (DeferUnreachablePack(r, s, why))
            return;
        PauseApproach(r, f, why); // a resume searches the stand again from where it stands (ApproachStand)
        return;
    }
    Pause(r, why);
}

// The walk of an approach hop, or of the walk to the approach stand, stuck after kWalkTries (WalkTo): not the plain
// pause, the approach's own way out (ApproachNoWay). Only for the step's own approach walk: not in a walk back, the
// current step a pull or boss, and the walk's point the hop's (Run::hopX/Y/Z: ApproachHop and ApproachStand set it).
// True when it took the stall.
inline bool ApproachWalkStuck(Run& r, const Facts& f, float dist, float x, float y, float z)
{
    if (r.walkBack || !r.hopWalk || x != r.hopX || y != r.hopY || z != r.hopZ)
        return false;
    const PlanStep* s = Current(r);
    if (!s || (s->kind != PlanStepKind::Pull && s->kind != PlanStepKind::Boss))
        return false;
    std::string const what = s->kind == PlanStepKind::Boss ? "boss" : "pack";
    std::string const why = "the lead bot is stuck " + Num(dist) + " yd from " + Num(x) + " " + Num(y) + " " + Num(z) +
        StuckNote(r, f) + " on the way toward the " + what + " at " + Num(s->x) + " " + Num(s->y) + " " + Num(s->z);
    r.hopWalk = false;
    EnterPhase(r, Phase::Begin);
    ApproachNoWay(r, f, *s, why);
    return true;
}
// Stormwrought Ruins r36 slot J step 553, Windhorn Canyon r36 slot G step 1854, Hateforge Quarry r35 slot 4b steps
// 264/265, Blackrock Depths r35 slot 6 step 91 (2026-10-24): the step's pull went out with the pack in sight but out of
// the tank's shot, its firing stands spent (HoldForShot: none found, used up, unreachable, or where it stands); the
// glue's reach pull could not launch the tank's move ('unreachable: The tank could not start moving to a firing
// position for the pull.') and OrderRefused paused the run; each resume ordered the same pull from the same spot into
// the same pause, and the stop rule ended four runs. Now such a refusal of the step's own pull (a pull or boss step in
// AwaitPull for that unit; not a hop pull, a walk back's or an event's) counts no pull and takes the approach's way out
// (ApproachNoWay): a retry given up; the stand search once a step; then the deferral (PullMayBeDeferred's gate); then
// the approach pause. At most two such refusals a step (before and after the stand). True: handled.
inline bool PullbackRefusedUnreachable(Run& r, const std::string& order, const std::string& answer)
{
    if (order.compare(0, 9, "pullback ") != 0 || answer.compare(0, 11, "unreachable") != 0)
        return false;
    std::uint64_t const guid = std::strtoull(order.c_str() + 9, nullptr, 10);
    const PlanStep* s = Current(r);
    if (!s || (s->kind != PlanStepKind::Pull && s->kind != PlanStepKind::Boss) || r.walkBack || r.pullHop ||
        r.phase != Phase::AwaitPull || r.pullGuid != guid || InEvent(r))
        return false;
    // A refused order started no fight: not a pull of the step, nor of the unit (kMaxPullsPerTarget).
    auto const tried = r.pullTries.find(guid);
    if (tried != r.pullTries.end() && tried->second > 0)
        --tried->second;
    if (r.pulls > 0)
        --r.pulls;
    r.pullGuid = 0;
    EnterPhase(r, Phase::Begin);
    std::string text = answer;
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    std::string const why = "cmd " + order + " refused: " + text;
    // Where the lead bot stood on the tick the order went out (the approach pause keeps it: ApproachResumeInPlace).
    Facts here;
    here.leadX = r.leadLastX;
    here.leadY = r.leadLastY;
    here.leadZ = r.leadLastZ;
    ApproachNoWay(r, here, *s, why);
    if (r.state == State::Running && r.approachStandWanted)
        r.refusedPullStandStep = r.step;
    return true;
}

// Gnomeregan r24 slot 4 step 412, Sunken Temple r25 slot 4 step 5, Razorfen Kraul
// r24 slot 8 step 299: '8 approach hops did not bring the party within 60 yd of the
// pack' (nearest 66.8 to 99.6 yd), and every resume walked back and hopped the same
// way into the same pause (Gnomeregan: three times, 10 minutes circling). Now the cap
// first asks the glue once for a stand within kApproachStandYd of the pack
// (NearestReachablePoint, the unreachable move's stand-in search: the path from the
// lead bot ends at the stand, at most kDetourMaxPathYd longer than the straight line,
// and the stand's own path leads on toward the pack); the party walks there and the
// pull goes out from it (in sight: FightStep). Only when none is found, or the stand
// does not bring the pack in sight, the run pauses. A resume of that pause hops no
// more: it searches the stand again from where the party stands now, walks to it,
// or pauses at once saying the pack is not reachable from this floor.
// True: this tick is the stand's (a search asked, a walk to the stand, a pause).
inline bool ApproachStand(Run& r, const PlanStep& s, std::uint32_t dt, const Facts& f, std::string& order,
    const std::string& what)
{
    if (r.approachStandStep != r.step)
        return false;
    std::string const pack = "the " + what + " at " + Num(s.x) + " " + Num(s.y) + " " + Num(s.z);
    if (r.approachStand == 1)
    {
        r.note = "approach: looking for a stand within " + Num(kApproachStandYd) + " yd of " + pack;
        return true; // the glue answers on this tick (ApproachStandFound)
    }
    if (r.approachStand == 2)
    {
        // Walked like a hop (FinishHopInSight): in sight of the pack the walk to the stand
        // is finished first and its end is the anchor, not the last looping hop's end (the
        // Opus review of f1fdeb67: a gather back to that anchor, on the other floor, walked
        // the lead bot out of sight and paused 'the stand did not bring it within 60 yd').
        r.approachStand = 4;
        r.hopWalk = true;
        r.hopX = r.approachStandX;
        r.hopY = r.approachStandY;
        r.hopZ = r.approachStandZ;
        r.note = "approach: walking to the stand " + Num(r.approachStandX) + " " + Num(r.approachStandY) + " " +
            Num(r.approachStandZ) + ", " +
            Num(ai::PlanDistance(r.approachStandX, r.approachStandY, r.approachStandZ, s.x, s.y, s.z)) + " yd from " +
            pack + ", to pull from there";
        WalkTo(r, f, r.approachStandX, r.approachStandY, r.approachStandZ, false, dt, order);
        return true;
    }
    if (r.approachStand == 4)
    {
        // The walk ended (WalkTo, the Walk phase above) and the pack is still out of sight.
        r.approachStand = 3;
        std::string const why = "the stand " + Num(r.approachStandX) + " " + Num(r.approachStandY) + " " +
            Num(r.approachStandZ) + " did not bring it within " + Num(kSeeYd) + " yd (the lead bot " +
            Num(ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z)) + " yd from it)";
        if (DeferUnreachablePack(r, s, why))
            return true;
        PauseApproach(r, f, (r.approachStandWhy.empty() ? std::string() : r.approachStandWhy + "; ") + pack +
            " is not reachable from this floor: " + why);
        return true;
    }
    if (r.approachStandResumed && r.approachStand == 0)
    {
        // A resume of the approach pause: the stand first, from where the party stands now.
        r.approachStandResumed = false;
        r.approachStandResumedSearch = true;
        r.approachStand = 1;
        r.approachStandWanted = true;
        r.approachStandWhy.clear();
        r.note = "approach (resumed): looking for a stand within " + Num(kApproachStandYd) + " yd of " + pack +
            " before any hop";
        return true;
    }
    return false;
}

// The glue's answer to the approach stand's search (ApproachStand): found (x y z), a
// point within kApproachStandYd of the step's point the lead bot has a navmesh path to.
// The party walks there (the next tick, WalkTo); none found: the pause, its note says
// the pack is not reachable from this floor. True: the stand is taken.
inline bool ApproachStandFound(Run& r, const Facts& f, bool found, float x, float y, float z)
{
    r.approachStandWanted = false;
    const PlanStep* s = Current(r);
    if (!s || r.state != State::Running || r.approachStandStep != r.step || r.approachStand != 1)
        return false;
    std::string const what = s->kind == PlanStepKind::Boss ? "boss" : "pack";
    std::string const pack = "the " + what + " at " + Num(s->x) + " " + Num(s->y) + " " + Num(s->z);
    float const ring = found ? ai::PlanDistance(x, y, z, s->x, s->y, s->z) : 0.0f;
    bool const resumedSearch = r.approachStandResumedSearch;
    r.approachStandResumedSearch = false;
    bool const none = !found || ring > kApproachStandYd + 0.5f || ring < ApproachStandMinRing(*s) - 0.5f;
    std::string const noStand = "no safe stand " + Num(ApproachStandMinRing(*s)) + "-" + Num(kApproachStandYd) +
        " yd from it the lead bot has a path to (the lead bot " +
        Num(ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s->x, s->y, s->z)) + " yd from it" +
        (r.approachBestYd > 0.0f ? ", the approach hops got no nearer than " + Num(r.approachBestYd) + " yd" : "") + ")";
    // A pull that may be deferred (DeferUnreachablePack): no hops again after a resume's
    // search found none, no pause: deferred (or, on its retry, skipped).
    if (none && DeferUnreachablePack(r, *s, (resumedSearch ? std::string("resumed: ") : std::string()) +
            (r.approachStandWhy.empty() ? std::string() : r.approachStandWhy + "; ") + noStand))
        return false;
    if (none && resumedSearch)
    {
        // A resume's search from where the party stands now found none (the Opus review of
        // f1fdeb67: a player who moved the party behind a winding ramp had no way out but
        // the pause): the hops again, with a fresh cap; that cap then pauses (no stand again).
        r.approachStand = 3; // the cap is fresh: the resume's ResetStep cleared the hops
        EnterPhase(r, Phase::Begin);
        r.note = "approach (resumed): no safe stand near " + pack + " from here: approach hops again";
        return false;
    }
    if (none)
    {
        r.approachStand = 3;
        PauseApproach(r, f, (r.approachStandWhy.empty() ? std::string() : r.approachStandWhy + "; ") + pack +
            " is not reachable from this floor: " + noStand);
        return false;
    }
    r.approachStand = 2; // the next tick walks there (ApproachStand)
    r.approachStandX = x;
    r.approachStandY = y;
    r.approachStandZ = z;
    EnterPhase(r, Phase::Begin);
    r.note = "approach: a stand " + Num(x) + " " + Num(y) + " " + Num(z) + " found, " +
        Num(ai::PlanDistance(x, y, z, s->x, s->y, s->z)) + " yd from " + pack;
    return true;
}

// Walks toward a step whose point is out of sight, one navmesh hop at a time
// (the glue's approach point). A hop is fixed when its walk starts; once there
// the next tick asks for the next one. Bounded by kMaxApproachHops.
inline void ApproachHop(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order,
    const std::string& what)
{
    r.note = "approaching the " + what;
    if (r.phase == Phase::Walk)
    {
        if (WalkTo(r, f, r.walkX, r.walkY, r.walkZ, false, dt, order))
            EnterPhase(r, Phase::Begin);
        return;
    }
    if (ApproachStand(r, s, dt, f, order, what))
        return;
    if (!f.approachKnown && !f.approachRead)
    {
        r.note += ": reading the way"; // the next tick asks the navmesh
        return;
    }
    if (IslandConfirmed(r, f, s.x, s.y, s.z))
    {
        IslandEscape(r, f, s.x, s.y, s.z, "the " + what, order);
        return;
    }
    // A hop that ends where the lead bot stands goes nowhere: no path, not a hop.
    if (!f.approachKnown ||
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.approachX, f.approachY, f.approachZ) <= kArriveYd)
    {
        std::string const why = "no walkable path toward the " + what + " at " + Num(s.x) + " " + Num(s.y) + " " +
            Num(s.z);
        ApproachNoWay(r, f, s, why);
        return;
    }
    // The cap counts hops that did not bring the lead bot nearer: a long way
    // walked in hops of kApproachHopYd is not stopped by it, a loop is.
    float const toStep = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z);
    if (r.approachBestYd <= 0.0f)
        r.approachBestYd = toStep;
    else if (toStep < r.approachBestYd - kApproachProgressYd)
    {
        r.approachBestYd = toStep;
        r.approachHops = 0;
    }
    if (++r.approachHops > kMaxApproachHops)
    {
        // Before the pause, a bot that stands still far behind is summoned
        // to the lead bot (once each), so a resume starts with the party
        // together. The lead bot's own way stays the reason for the pause.
        // Uldaman 19z11 02:50: the priest stood on a navmesh pocket at 43 371
        // while the lead bot hopped from the entrance.
        if (SummonStraggler(r, f, order, true))
        {
            r.note = "approach: " + r.note.substr(std::string("gather: ").size()) + " before the pause";
            return;
        }
        std::string const why = std::to_string(kMaxApproachHops) + " approach hops did not bring the party within " +
            Num(kSeeYd) + " yd of the " + what + " (nearest it got: " + Num(r.approachBestYd) + " yd)";
        if (ApproachStandWanted(r, s))
        {
            r.approachStandStep = r.step;
            r.approachStand = 1;
            r.approachStandWanted = true;
            r.approachStandWhy = why;
            r.note = why + ": looking for a stand within " + Num(kApproachStandYd) + " yd of it";
            return;
        }
        if (r.approachStandStep == r.step)
            PauseApproach(r, f, why); // the stand was tried: a resume searches it again (ApproachStand)
        else
            Pause(r, why);
        return;
    }
    r.hopWalk = true;
    r.hopX = f.approachX;
    r.hopY = f.approachY;
    r.hopZ = f.approachZ;
    WalkTo(r, f, f.approachX, f.approachY, f.approachZ, false, dt, order);
}

// After a regroup the run started by itself the party stands where it came
// back to life (the instance entrance after a corpse run). It walks the plan's
// own move points again, in order, from the one nearest the lead bot up to the
// current step: the way the run came, with no new path search. Nothing to walk
// when the current step's point is nearer than every earlier move point.
// A point of a navmesh path (the glue's PathFinder points).
struct PathPoint
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// The point `maxYd` along a navmesh path (its first point the lead bot's spot), on the
// path's own segments, never a straight line to its end; the path's end when it is
// shorter. False for a path of fewer than two points or a leg that ends within
// kArriveYd of the start (no progress).
inline bool PointAlongPath(const std::vector<PathPoint>& path, float maxYd, float& x, float& y, float& z)
{
    if (path.size() < 2)
        return false;
    float walked = 0.0f;
    x = path.back().x;
    y = path.back().y;
    z = path.back().z;
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const PathPoint& a = path[i - 1];
        const PathPoint& b = path[i];
        float const len = ai::PlanDistance(a.x, a.y, a.z, b.x, b.y, b.z);
        if (walked + len >= maxYd && len > 0.0f)
        {
            float const t = (maxYd - walked) / len;
            x = a.x + t * (b.x - a.x);
            y = a.y + t * (b.y - a.y);
            z = a.z + t * (b.z - a.z);
            break;
        }
        walked += len;
    }
    return ai::PlanDistance(path[0].x, path[0].y, path[0].z, x, y, z) > kArriveYd;
}

// The answer of a `cmd move` refused for its distance alone: the point is farther than
// AiPlayerbot.ReactDistance from the lead bot (CommanderMove's TooFar verdict). Not a
// direct step's too-far, which names a bot that is not with the lead bot.
inline bool ReactTooFarAnswer(const std::string& answer)
{
    return answer.compare(0, 8, "too-far:") == 0 && answer.find("(AiPlayerbot.ReactDistance)") != std::string::npos;
}

// Sunken Temple r25 slot 4 step 5 (2026-10-23): the walk's `cmd move -449.5 25.9 -148.0`
// from 157 yd away was refused 'too-far: The point is more than 150 yd
// (AiPlayerbot.ReactDistance) from the lead bot. Move in shorter legs.' and the run
// paused. Now such a refusal of the walk's own plain move (any walk: a step's, an
// approach hop's, a walk back's, a stand's) asks the glue for a leg: the point at most
// kTooFarLegYd along the lead bot's navmesh path to it (PointAlongPath on the path the
// server's PathFinder returns); the leg goes out instead of the pause. Bounded by
// kMaxTooFarLegs per walk.
inline bool TooFarLegWanted(const Run& r, const std::string& order, const std::string& answer)
{
    return r.state == State::Running && r.phase == Phase::Walk && ReactTooFarAnswer(answer) &&
        order == MoveOrder(r.walkX, r.walkY, r.walkZ, false) && r.walkLegs < kMaxTooFarLegs;
}

// The 0-based leg index whose end (legEnds[3k..3k+2]) lies within kLegRepeatYd of
// (lx, ly, lz), or npos if none does. An empty legEnds gives npos (as does a fresh
// walk), so the walkLegs > 0 meaning is kept.
inline std::size_t RepeatedLeg(const Run& r, float lx, float ly, float lz)
{
    for (std::size_t k = 0; k + 3 <= r.legEnds.size(); k += 3)
        if (ai::PlanDistance(lx, ly, lz, r.legEnds[k], r.legEnds[k + 1], r.legEnds[k + 2]) <= kLegRepeatYd)
            return k / 3;
    return static_cast<std::size_t>(-1);
}

// Whether the glue's next leg ends within kLegRepeatYd of any leg this walk sent:
// the lead bot made no way toward it, so the same leg is not sent again (the refusal
// pauses, TooFarLegRepeatNote saying why).
inline bool TooFarLegRepeats(const Run& r, float lx, float ly, float lz)
{
    return RepeatedLeg(r, lx, ly, lz) != static_cast<std::size_t>(-1);
}

inline std::string TooFarLegRepeatNote(const Run& r, float lx, float ly, float lz)
{
    std::size_t const k = RepeatedLeg(r, lx, ly, lz);
    if (k != static_cast<std::size_t>(-1) && k + 1 < r.walkLegs)
        return " (the next leg of the way, " + Num(lx) + " " + Num(ly) + " " + Num(lz) + ", ends where leg " +
            std::to_string(k + 1) + " did, " + Num(r.legEnds[3 * k]) + " " + Num(r.legEnds[3 * k + 1]) + " " +
            Num(r.legEnds[3 * k + 2]) + ": the lead bot made no way toward it)";
    return " (the next leg of the way, " + Num(lx) + " " + Num(ly) + " " + Num(lz) + ", ends where the last one did, " +
        Num(r.legX) + " " + Num(r.legY) + " " + Num(r.legZ) + ": the lead bot made no way toward it)";
}

// The glue's leg for a move refused too-far (TooFarLegWanted) went out (`accepted`) as
// `cmd move lx ly lz`: the walk goes on toward its own point, and once the lead bot
// stands at the leg (or stalls on its way) WalkTo sends that point again. True: taken
// (the refusal is handled); false: the refusal goes on as before (OrderRefused).
inline bool TooFarLegTaken(Run& r, const Facts& f, float lx, float ly, float lz, bool accepted,
    const std::string& legAnswer)
{
    if (!accepted)
    {
        r.note = "a leg toward " + Num(r.walkX) + " " + Num(r.walkY) + " " + Num(r.walkZ) + " was refused too (" +
            legAnswer + ")";
        return false;
    }
    r.legOn = true;
    r.legX = lx;
    r.legY = ly;
    r.legZ = lz;
    r.legBestYd = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, lx, ly, lz);
    r.legStartYd = r.legBestYd;
    r.legEnds.insert(r.legEnds.end(), {lx, ly, lz});
    ++r.walkLegs;
    r.walkStillMs = 0;
    r.note = "the move to " + Num(r.walkX) + " " + Num(r.walkY) + " " + Num(r.walkZ) +
        " is beyond the lead bot's reach: walking a leg of the way first, to " + Num(lx) + " " + Num(ly) + " " + Num(lz) +
        " (leg " + std::to_string(r.walkLegs) + ")";
    return true;
}

// The margin past a pull group's radius a direct walk back keeps from it.
constexpr float kPackClearanceYd = 10.0f;

inline float SegmentDistance(const PathPoint& a, const PathPoint& b, float px, float py, float pz)
{
    float const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float const len2 = dx * dx + dy * dy + dz * dz;
    float t = len2 > 0.0f ? ((px - a.x) * dx + (py - a.y) * dy + (pz - a.z) * dz) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return ai::PlanDistance(a.x + t * dx, a.y + t * dy, a.z + t * dz, px, py, pz);
}

// Whether a path passes within a pull or boss group's radius plus
// kPackClearanceYd of its point, for every group not cleared yet: the step
// `from` and later ones, and earlier ones left without being cleared
// (`notCleared`: skipped, out of reach), of the party's team. The whole path
// counts, its last leg too. The shortest navmesh path back
// ignores packs: a straight walk past an uncleared one body-pulls it.
inline bool PathCrossesUnclearedPack(const DungeonPlan& plan, std::size_t from, const std::vector<PathPoint>& path,
    std::uint32_t team, const std::unordered_set<std::size_t>& notCleared = {})
{
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
    {
        if (i < from && !notCleared.count(i))
            continue; // cleared before
        const PlanStep& g = plan.steps[i];
        if ((g.kind != PlanStepKind::Pull && g.kind != PlanStepKind::Boss) || !ai::StepForTeam(g, team))
            continue;
        float const reach = g.radius + kPackClearanceYd;
        if (path.size() == 1 && ai::PlanDistance(path[0].x, path[0].y, path[0].z, g.x, g.y, g.z) <= reach)
            return true;
        for (std::size_t k = 1; k < path.size(); ++k)
            if (SegmentDistance(path[k - 1], path[k], g.x, g.y, g.z) <= reach)
                return true;
    }
    return false;
}

// The approach stand's search (ApproachStand; the glue's ApproachStandSearch), after
// the Codex review of f1fdeb67: the refused move's stand-in search took the plan point
// itself (the pack's centre) first and looked at no hostile near the stand or on its
// path, so the walk to it could body-pull the target and more packs. Its own search:
// candidates on rings of kApproachStandMinYd..kApproachStandYd around the step's point
// (never inside the target's pull radius plus kApproachStandPullMarginYd), each with the
// lead bot's navmesh path to it and a sight line to the pack's point. A hostile seen
// near the pack (the glue's scan): its position and entry.
struct StandHostile
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::uint32_t entry = 0;
};

// The run leader's walk of one bot (RunBotWalk, the glue's `walk <bot> x y z`) on the bot's own navmesh path:
// refused (the reason; empty: it goes) when the path is more than kDetourMaxPathYd longer than the straight line
// (the stand-in detour's cap: a long way round through the dungeon), when it passes a pull or boss group not
// cleared yet (PathCrossesUnclearedPack, from the current step; `plan` null: no plan to read), or when it passes
// within kHopPullYd of a living hostile out of combat (`hostiles`, the glue's scan around the bot). Codex
// adversarial review of 95f66dd8: the path check only asked that it ended at the point. A refused ladder walk
// goes on to the summon (StragglerWalkRefused).
// The longest path such a walk takes: the glue's hostile scan around the bot reaches this far plus kHopPullYd, so
// a longer path could pass a hostile it never saw (Codex review of 6c8297ec).
constexpr float kBotWalkMaxPathYd = 120.0f;
// A hostile or a pack that is already within its threshold of the bot at the path's start (the bot stands there:
// a rogue whose Sap failed stands at its target, inside the current pull's pack; Opus review of 95f66dd8 and
// 6c8297ec, the r26 bug) is exempt from the path's checks only while the path moves away from it: refused once a
// point within its threshold comes nearer to it than an earlier point did (by more than kBotWalkAwayEpsYd). Every
// other hostile and pack is checked on the whole path (Codex review of 5a146681: a blanket 20 yd at the start let
// the walk cross a hostile that began 16-20 yd away, or another pack inside it).
constexpr float kBotWalkAwayEpsYd = 0.05f;
constexpr float kBotWalkSampleYd = 0.5f; // the path is checked at points this far apart (and at every corner)

inline std::string BotWalkRefusal(const std::vector<PathPoint>& path, float straightYd, float pathYd,
    const DungeonPlan* plan, std::size_t from, std::uint32_t team, const std::unordered_set<std::size_t>& notCleared,
    const std::vector<StandHostile>& hostiles)
{
    if (pathYd > kBotWalkMaxPathYd)
        return "too-long: the path is " + Num(pathYd) + " yd, more than " + Num(kBotWalkMaxPathYd) + " yd";
    if (pathYd > straightYd + kDetourMaxPathYd)
        return "detour: the path is " + Num(pathYd) + " yd for " + Num(straightYd) + " yd in a straight line";
    if (path.empty())
        return std::string();
    // The path's points, kBotWalkSampleYd apart (every corner too), from its start.
    std::vector<PathPoint> pts = {path.front()};
    for (std::size_t k = 1; k < path.size(); ++k)
    {
        const PathPoint& a = path[k - 1];
        const PathPoint& b = path[k];
        float const len = ai::PlanDistance(a.x, a.y, a.z, b.x, b.y, b.z);
        int const n = static_cast<int>(len / kBotWalkSampleYd) + 1;
        for (int i = 1; i <= n; ++i)
        {
            float const t = static_cast<float>(i) / static_cast<float>(n);
            pts.push_back(PathPoint{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)});
        }
    }
    // Whether the path comes within `reach` of (cx cy cz): from a start already within it, only by a point within
    // it that is nearer than an earlier one (it turns back toward it).
    auto passes = [&](float cx, float cy, float cz, float reach) -> bool
    {
        bool const startNear = ai::PlanDistance(pts[0].x, pts[0].y, pts[0].z, cx, cy, cz) <= reach;
        float farthest = 0.0f;
        for (const PathPoint& q : pts)
        {
            float const d = ai::PlanDistance(q.x, q.y, q.z, cx, cy, cz);
            if (d <= reach && (!startNear || d < farthest - kBotWalkAwayEpsYd))
                return true;
            farthest = d > farthest ? d : farthest;
        }
        return false;
    };
    if (plan)
        for (std::size_t i = 0; i < plan->steps.size(); ++i)
        {
            if (i < from && !notCleared.count(i))
                continue; // cleared before (as PathCrossesUnclearedPack)
            const PlanStep& g = plan->steps[i];
            if ((g.kind != PlanStepKind::Pull && g.kind != PlanStepKind::Boss) || !ai::StepForTeam(g, team))
                continue;
            if (passes(g.x, g.y, g.z, g.radius + kPackClearanceYd))
                return "pack: the path passes a pack not cleared yet";
        }
    for (const StandHostile& h : hostiles)
        if (passes(h.x, h.y, h.z, kHopPullYd))
            return "hostile: the path passes within " + Num(kHopPullYd) + " yd of " + std::to_string(h.entry);
    return std::string();
}

// One walker of the run leader's (RunBotWalk) at the glue's sync (SyncRunWalkers): kept on its stay; dropped from
// the list untouched (no longer on stay: the party's next move formed it up, or the bot is gone); or released, its
// run-imposed stay cleared before it leaves the list (dead, in a fight, its run not running, or no longer a
// member of the lead bot's group, the lead bot or the binding gone, or on another map than the lead bot: Codex
// adversarial review of 95f66dd8, a bot kept on the run's stay after it left the party).
enum class WalkerFate : std::uint8_t { Keep, Drop, Release };

inline WalkerFate WalkerVerdict(bool botFound, bool onStay, bool botAlive, bool inCombat, bool runRunning,
    bool leadAvailable, bool inLeadGroup, bool sameMap)
{
    if (!botFound || !onStay)
        return WalkerFate::Drop;
    if (!botAlive || inCombat || !runRunning || !leadAvailable || !inLeadGroup || !sameMap)
        return WalkerFate::Release;
    return WalkerFate::Keep;
}

// A leg of a move refused too-far (TooFarLegWanted) never ends beside a hostile: the
// hop point's check (kHopPullYd) for its end too (the Opus review of f1fdeb67). The
// longest of maxYd, 3/4, 1/2 and 1/4 of it along the path whose end has no hostile
// within kHopPullYd; false when every one has (the refusal pauses, saying so).
inline bool PickSafeLeg(const std::vector<PathPoint>& path, float maxYd, const std::vector<StandHostile>& hostiles,
    float& x, float& y, float& z)
{
    for (float part : {1.0f, 0.75f, 0.5f, 0.25f})
    {
        if (!PointAlongPath(path, maxYd * part, x, y, z))
            return false;
        bool clear = true;
        for (const StandHostile& h : hostiles)
            clear = clear && ai::PlanDistance(x, y, z, h.x, h.y, h.z) > kHopPullYd;
        if (clear)
            return true;
    }
    return false;
}

struct StandCandidate
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool reachable = false;    // the lead bot's path ends at it (DetourPointReachable, its length capped)
    bool los = false;          // it sees the pack's point
    float pathYd = 0.0f;       // that path's length
    std::vector<PathPoint> path;
};

// Why a candidate is no approach stand ("" when it is one): off the ring, no path, no
// sight of the pack, within the target's pull radius plus the margin of one of its units,
// another hostile within kApproachStandHostileYd, or its path from the lead bot passing a
// pull group not cleared yet other than the target's own (PathCrossesUnclearedPack).
inline std::string ApproachStandReject(const Run& r, const PlanStep& s, const StandCandidate& c,
    const std::vector<StandHostile>& hostiles, std::uint32_t team)
{
    float const ring = ai::PlanDistance(c.x, c.y, c.z, s.x, s.y, s.z);
    if (ring < ApproachStandMinRing(s) - 0.5f || ring > kApproachStandYd + 0.5f)
        return "off the ring";
    if (!c.reachable)
        return "no path";
    if (!c.los)
        return "no sight of the pack";
    for (const StandHostile& h : hostiles)
    {
        float const d = ai::PlanDistance(c.x, c.y, c.z, h.x, h.y, h.z);
        // A member of the target's pack by place, not by entry alone (the Codex review of
        // 8b81efa3: a same-entry mob of a neighbouring pack got only the pull radius + 5):
        // the step's entry within its pull radius of the step's point. Any other hostile,
        // same entry or not, keeps kApproachStandHostileYd.
        bool const member = IsStepTarget(s, h.entry) && ai::PlanDistance(h.x, h.y, h.z, s.x, s.y, s.z) <= s.radius;
        if (member ? d < s.radius + kApproachStandPullMarginYd : d < kApproachStandHostileYd)
            return member ? "inside the target's pull radius" : "another hostile close by";
    }
    std::unordered_set<std::size_t> notCleared = r.notCleared;
    notCleared.erase(r.step);
    if (r.plan && PathCrossesUnclearedPack(*r.plan, LiveFrom(r, false), c.path, team, notCleared))
        return "its path passes an uncleared pack";
    return std::string();
}

// The approach stand: the candidate that passes ApproachStandReject with the shortest
// path from the lead bot (-1 for none: the pause).
inline int PickApproachStand(const Run& r, const PlanStep& s, const std::vector<StandCandidate>& candidates,
    const std::vector<StandHostile>& hostiles, std::uint32_t team)
{
    int best = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i)
        if (ApproachStandReject(r, s, candidates[i], hostiles, team).empty() &&
            (best < 0 || candidates[i].pathYd < candidates[static_cast<std::size_t>(best)].pathYd))
            best = static_cast<int>(i);
    return best;
}

// The lowest plan index a walk back may start at: the checkpoint when it is
// known (a point before it can lie behind a hole the lead bot crossed since),
// else 0 (as at the entrance: before every hole). Never before the step the
// run started at (Run::startStep: a placed run never walked the steps before it).
inline std::size_t ReplayFloor(const Run& r)
{
    return std::max(CheckpointKnown(r) ? r.checkpoint : static_cast<std::size_t>(0), r.startStep);
}

// The first direct step before the current step that the lead bot has not
// crossed (npos for none): after the checkpoint when it is known, else the
// plan's first direct step; never one before the run's start step.
inline std::size_t FirstUncrossedDirect(const Run& r)
{
    std::size_t const from = std::max(CheckpointKnown(r) ? r.checkpoint + 1 : static_cast<std::size_t>(0), r.startStep);
    for (std::size_t i = from; i < r.step && i < r.plan->steps.size(); ++i)
        if (r.plan->steps[i].kind == PlanStepKind::MoveDirect)
            return i;
    return static_cast<std::size_t>(-1);
}

// The last move point in [ReplayFloor, hole), before the direct step `hole`:
// where a walk back stops its navmesh hops and replays the plan's own points
// over the hole. `hole` itself when no move point is before it.
inline std::size_t LastPointBeforeDirect(const Run& r, std::size_t hole)
{
    std::size_t start = hole;
    for (std::size_t i = ReplayFloor(r); i < hole; ++i)
        if (IsRunMovePoint(r, i))
            start = i;
    return start;
}

// The first move point at or after ReplayFloor, before the current step (npos
// for none): where a replay that cannot begin starts over (WalkBack).
inline std::size_t FirstReplayPoint(const Run& r)
{
    for (std::size_t i = ReplayFloor(r); i < r.step && i < r.plan->steps.size(); ++i)
        if (IsRunMovePoint(r, i))
            return i;
    return static_cast<std::size_t>(-1);
}

// The walk back first goes straight to its start point (in navmesh hops,
// StartWalkBack); when that fails it replays every point from the one nearest
// the lead bot (ReplayWalkBack), but only among the points the route allows:
// not before the checkpoint when it is known, and not past the first direct
// step not crossed yet (that direct step is then always replayed, even from a
// lead bot nearer the step than any point). True when there is one.
// The point a replay of the plan's points begins at (ReplayWalkBack's choice), npos for none.
inline std::size_t ReplayStartPoint(const Run& r, const Facts& f)
{
    const PlanStep* cur = Current(r);
    if (!cur)
        return static_cast<std::size_t>(-1);
    std::size_t const hole = FirstUncrossedDirect(r);
    std::size_t const ceiling = hole != static_cast<std::size_t>(-1) ? hole : r.step;
    float best = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, cur->x, cur->y, cur->z);
    std::size_t idx = static_cast<std::size_t>(-1);
    if (hole != static_cast<std::size_t>(-1))
    {
        idx = LastPointBeforeDirect(r, hole);
        best = -1.0f;
    }
    for (std::size_t i = ReplayFloor(r); i < ceiling; ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsRunMovePoint(r, i))
            continue;
        float const d = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
        if (best < 0.0f || d < best)
        {
            best = d;
            idx = i;
        }
    }
    return idx;
}

inline bool ReplayWalkBack(Run& r, const Facts& f)
{
    r.strandReplay = false;
    r.strandBotOnly = false;
    r.strandFromOtherPart = false;
    r.strandDue = false; // a refused teleport's replay answers the same failure, not a second send
    r.walkBackDirect = false;
    r.shortcutWanted = r.shortcutFrom = static_cast<std::size_t>(-1);
    r.walkBack = false;
    r.approachHops = 0;
    std::size_t const idx = ReplayStartPoint(r, f);
    if (idx == static_cast<std::size_t>(-1))
        return false;
    r.walkBackIdx = idx;
    r.walkBack = true;
    r.walkBackStart = r.walkBackIdx;
    EnterPhase(r, Phase::Begin);
    return true;
}

// Whether the plan's points from `from` up to the current step hold a direct
// step: a navmesh hole (Uldaman's scorpid pit) the navmesh hops of a straight
// walk back cannot cross; only the plan's own points cross it.
inline bool DirectStepOnWay(const Run& r, std::size_t from)
{
    for (std::size_t i = from; i < r.step && i < r.plan->steps.size(); ++i)
        if (r.plan->steps[i].kind == PlanStepKind::MoveDirect)
            return true;
    return false;
}

// The plan's move point nearest the lead bot before the current step (npos
// for none). Proximity only: on a folded route it can lie past a hole the lead
// bot has not crossed, so no walk back starts from it (StartWalkBack).
inline std::size_t NearestMovePoint(const Run& r, const Facts& f)
{
    std::size_t idx = static_cast<std::size_t>(-1);
    float best = 0.0f;
    for (std::size_t i = 0; i < r.step; ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsRunMovePoint(r, i))
            continue;
        float const d = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
        if (idx == static_cast<std::size_t>(-1) || d < best)
        {
            best = d;
            idx = i;
        }
    }
    return idx;
}

// The last plan step before `idx` whose point the lead bot reached: the route checkpoint
// when it is known and before `idx`, else the last move point before it (where a walk
// back goes, StartWalkBack); npos for none.
inline std::size_t LastReachedBefore(const Run& r, std::size_t idx)
{
    if (r.routeKnown && r.checkpoint != static_cast<std::size_t>(-1) && r.checkpoint < idx)
        return r.checkpoint;
    std::size_t last = static_cast<std::size_t>(-1);
    for (std::size_t i = r.startStep; i < idx && i < r.plan->steps.size(); ++i)
        if (IsRunMovePoint(r, i))
            last = i;
    return last;
}

// The last pull or boss step of the party's team after plan step `after` and before the
// current step that the party cleared (not skipped, not left out of reach: notCleared),
// npos for none: the last point the run reached by fighting there (StartWalkBack).
inline std::size_t LastClearedFight(const Run& r, std::uint32_t team, std::size_t after)
{
    std::size_t found = static_cast<std::size_t>(-1);
    for (std::size_t i = after + 1; i < r.step && i < r.plan->steps.size(); ++i)
    {
        const PlanStep& g = r.plan->steps[i];
        if ((g.kind == PlanStepKind::Pull || g.kind == PlanStepKind::Boss) && ai::StepForTeam(g, team) &&
            !r.notCleared.count(i))
            found = i;
    }
    return found;
}

// ---------------------------------------------------------------------------
// The walk back's way (time sinks 2026-10-23, WASTED-TIME sink 5; Gnomeregan r26 slot 4 and r27 slot 4).
// r26: after the wipe at step 627 the straight walk back to step 623's point "passed an uncleared pack" (that point
// is the stand beside the very pack that wiped the party: the path's last leg always comes that close), so the
// replay began at the point nearest the lead bot, at the entrance (step 47), and walked every point up to step 277
// in 14 min until the 3 h cap. r27 (placed at step 543): the same replay from step 53, 23 min to step 118.
//   - A straight walk back aims at the latest point (from the last move point back, never before ReplayFloor) that
//     lies out of every live pack's reach (StraightWalkBackTarget), so its path does not end beside one; the plan's
//     points from there to the step are replayed.
//   - A straight path that still passes an uncleared pack moves its target back to such a point at least
//     kStraightBackoffYd before it, at most kStraightBackoffs times, before the replay of the plan's points.
//   - Steps before the run's start step count as uncleared for the path check (UnclearedForPath): a placed run
//     never fought there.
//   - The replay leaves out a stretch of the route that comes back within kRouteLinkYd (and kRouteLinkDzYd up or
//     down) of the point just reached (RouteShortcut): an out-and-back side room's points are not walked twice.
//     Only plain move points; never past a direct step, an exact point or its launch, or a pack left uncleared.
//   - A failed straight walk back whose replay would walk kReplayRescueYd or more before the safe point on the way it
//     came is rescued instead (ReplayRescue: a bot-only party, the rescue's bounds; Stratholme r29 slot 4, Maraudon
//     r28b slot B), and so is a backoff whose plan points from its new target walk that far (BackoffRescue: Shadowfang
//     Keep r36 slot A, the second backoff landed on step 1).
constexpr std::uint32_t kStraightBackoffs = 2;
constexpr float kStraightBackoffYd = 40.0f;
constexpr float kReplayRescueYd = 300.0f;
constexpr float kRouteLinkYd = 12.0f;
constexpr float kRouteLinkDzYd = 3.0f;
// A navmesh path that "reaches" a point (PathEndsShort false), and one that is no detour (PathFitsStraight). The path
// finder sets PATHFIND_INCOMPLETE for a point just off the mesh (its end then lies a few yards from the point), so only
// an end far from the point (or on another floor) is short. It sets it as well at its polygon cap (256): such a path
// ends wherever its polygons ran out, anywhere along a long way (the Opus re-review of a4a16e14), so a walk takes a
// capped path that still gets on (PathWalkable) and plans again from its end; a skip or a shortcut does not.
constexpr float kPathShortYd = 10.0f;
constexpr float kPathShortDzYd = 6.0f;
constexpr float kPathDetourFactor = 1.3f;

inline bool PathEndsShort(bool noPath, bool incomplete, float endX, float endY, float endZ, float x, float y, float z)
{
    if (noPath)
        return true;
    if (!incomplete)
        return false;
    return ai::PlanDistance(endX, endY, endZ, x, y, z) > kPathShortYd || std::fabs(endZ - z) > kPathShortDzYd;
}

// A partial path whose end still makes way toward the point: at least kCappedGainShare of the straight distance
// nearer than the start, and on the start's or the point's floor (kPathShortDzYd). The capped long path of above.
constexpr float kCappedGainShare = 0.5f;

inline bool CappedPathGetsOn(bool noPath, bool incomplete, float sx, float sy, float sz, float endX, float endY,
    float endZ, float x, float y, float z)
{
    if (noPath || !incomplete)
        return false;
    float const from = ai::PlanDistance(sx, sy, sz, x, y, z);
    float const left = ai::PlanDistance(endX, endY, endZ, x, y, z);
    bool const floor = std::fabs(endZ - sz) <= kPathShortDzYd || std::fabs(endZ - z) <= kPathShortDzYd;
    return floor && left <= (1.0f - kCappedGainShare) * from;
}

// A path a walk (the walk back's approach hops) may take: it reaches the point (PathEndsShort false) or, capped, still
// gets on toward it (CappedPathGetsOn): the next hop plans again from its end.
inline bool PathWalkable(bool noPath, bool incomplete, float sx, float sy, float sz, float endX, float endY, float endZ,
    float x, float y, float z)
{
    return !PathEndsShort(noPath, incomplete, endX, endY, endZ, x, y, z) ||
        CappedPathGetsOn(noPath, incomplete, sx, sy, sz, endX, endY, endZ, x, y, z);
}

// A path finder result that is a fallback, not a navmesh route: PATHFIND_SHORTCUT (a straight line the finder made up)
// or PATHFIND_NOT_USING_PATH (no navmesh used at all). Neither proves the way (the Codex review of a4a16e14; the glue's
// LegOnPath refuses both, and any incomplete path, in its own test).
inline bool NavPathIsFallback(bool shortcut, bool notUsingPath)
{
    return shortcut || notUsingPath;
}

// A genuine navmesh route to the point (fight forward's and the replay shortcut's read, the glue's DirectNavPath): no
// fallback (NavPathIsFallback) and not ending short (PathEndsShort).
inline bool NavPathReaches(bool noPath, bool incomplete, bool shortcut, bool notUsingPath, float endX, float endY,
    float endZ, float x, float y, float z)
{
    return !NavPathIsFallback(shortcut, notUsingPath) && !PathEndsShort(noPath, incomplete, endX, endY, endZ, x, y, z);
}

inline bool PathFitsStraight(float pathYd, float straightYd)
{
    return pathYd <= kPathDetourFactor * std::max(straightYd, kArriveYd);
}

// Whether plan step `i` (a pull or boss of the party's team) is a live pack for the walk back: the current step and
// later ones (LiveFrom), one left uncleared, or one before the run's start step.
inline bool WalkBackPackLive(const Run& r, std::size_t i)
{
    return i >= LiveFrom(r, true) || r.notCleared.count(i) || i < r.startStep;
}

// Whether (x, y, z) lies within a live pull or boss group's radius plus kPackClearanceYd (PathCrossesUnclearedPack's
// reach), for the party's team.
inline bool PointNearLivePack(const Run& r, std::uint32_t team, float x, float y, float z)
{
    if (!r.plan)
        return false;
    for (std::size_t i = 0; i < r.plan->steps.size(); ++i)
    {
        const PlanStep& g = r.plan->steps[i];
        if ((g.kind != PlanStepKind::Pull && g.kind != PlanStepKind::Boss) || !ai::StepForTeam(g, team) ||
            !WalkBackPackLive(r, i))
            continue;
        if (ai::PlanDistance(x, y, z, g.x, g.y, g.z) <= g.radius + kPackClearanceYd)
            return true;
    }
    return false;
}

// The steps the glue's straight-path check counts as not cleared besides those from LiveFrom on: notCleared, and
// every step before the run's start step (PathCrossesUnclearedPack's `notCleared`).
inline std::unordered_set<std::size_t> UnclearedForPath(const Run& r)
{
    std::unordered_set<std::size_t> out = r.notCleared;
    for (std::size_t i = 0; i < r.startStep && r.plan && i < r.plan->steps.size(); ++i)
        out.insert(i);
    return out;
}

// The latest plain move point at or before `from` and at or after the run's start step that is out of every live
// pack's reach and, when `minYd` > 0, at least that far from point `from` (npos for none). Not bounded by the route
// checkpoint (the Opus re-review of a4a16e14: after a wipe the checkpoint is the stand beside the pack that wiped the
// party, so a search from it found nothing and the rescue teleported there); a direct step still ends the search,
// so no point behind a navmesh hole is chosen.
inline std::size_t ClearPointBefore(const Run& r, std::uint32_t team, std::size_t from, float minYd)
{
    if (!r.plan || from >= r.plan->steps.size())
        return static_cast<std::size_t>(-1);
    const PlanStep& t = r.plan->steps[from];
    std::size_t const floor = r.startStep;
    for (std::size_t j = from + 1; j-- > floor;)
    {
        const PlanStep& p = r.plan->steps[j];
        if (p.kind == PlanStepKind::MoveDirect)
            break; // never back over a hole
        if (p.kind != PlanStepKind::Move)
            continue;
        if (minYd > 0.0f && ai::PlanDistance(p.x, p.y, p.z, t.x, t.y, t.z) < minYd)
            continue;
        if (!PointNearLivePack(r, team, p.x, p.y, p.z))
            return j;
    }
    return static_cast<std::size_t>(-1);
}

// A safe point at or before `last`: `last` when no live pack is near it, else the latest plain move point before it
// that is clear (ClearPointBefore); npos when there is none.
inline std::size_t SafePointAtOrBefore(const Run& r, std::uint32_t team, std::size_t last)
{
    if (!r.plan || last >= r.plan->steps.size())
        return static_cast<std::size_t>(-1);
    const PlanStep& p = r.plan->steps[last];
    if (!PointNearLivePack(r, team, p.x, p.y, p.z))
        return last;
    return ClearPointBefore(r, team, last, 0.0f);
}

// The straight walk back's target: the safe point (SafePointAtOrBefore), or `last` itself when there is none (the walk
// goes there as before; only the rescue's teleport refuses an unsafe point).
inline std::size_t StraightWalkBackTarget(const Run& r, std::uint32_t team, std::size_t last)
{
    std::size_t const safe = SafePointAtOrBefore(r, team, last);
    return safe == static_cast<std::size_t>(-1) ? last : safe;
}

// A placed run's lead bot revived behind its start step (the lead's decision on round 1 of 0d9957be; Gnomeregan r27
// slot 4: placed at step 543, revived at the entrance, the run never walked the steps between): the plan point
// nearest it lies before the start step and none of the run's own points (start step to the current one) is within
// kSeeYd of it. It is teleported at once by the stranded lead bot's rescue (RescueDue), when the rescue is allowed
// (RescueAllowed: AiPlayerbot.DungeonRunWalkBackRescue, out of combat, once a step). The target, in a party with no
// real player (Facts::players): the safe point on the way it came, as every other rescue (SafePointAtOrBefore of
// LastReachedBefore: the last point reached, or the latest one before it out of every live pack's reach), never before
// the start step. Hateforge Quarry r32 slot E (placed at step 83, six wipes at step 224): teleported to step 84 every
// time, then the replay 84-223, 270-525 s a wipe; Windhorn Canyon r34 slot B (placed at 727, wipes at 1025): to step
// 734, then about 5 min to step 1021. With a real player in the party, or no such point (a direct step between it and
// the last point reached), the first plain move point from the start step on that no live pack is near, as before:
// he walks back with the party from there. True when it is due.
inline bool PlacedRunRescue(Run& r, const Facts& f)
{
    if (r.startStep == 0 || !RescueAllowed(r, f) || r.step <= r.startStep)
        return false;
    std::size_t nearest = static_cast<std::size_t>(-1);
    float best = 0.0f, ownBest = -1.0f;
    for (std::size_t i = 0; i < r.step && i < r.plan->steps.size(); ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsRunMovePoint(r, i))
            continue;
        float const d = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
        if (nearest == static_cast<std::size_t>(-1) || d < best)
        {
            nearest = i;
            best = d;
        }
        if (i >= r.startStep && (ownBest < 0.0f || d < ownBest))
            ownBest = d;
    }
    if (nearest == static_cast<std::size_t>(-1) || nearest >= r.startStep || (ownBest >= 0.0f && ownBest <= kSeeYd))
        return false;
    std::size_t target = static_cast<std::size_t>(-1);
    if (f.players.empty())
    {
        std::size_t const last = LastReachedBefore(r, r.step);
        if (last != static_cast<std::size_t>(-1) && last >= r.startStep && last < r.plan->steps.size())
            target = SafePointAtOrBefore(r, f.team, last); // never before the start step (ClearPointBefore's floor)
    }
    bool const byTheWipe = target != static_cast<std::size_t>(-1);
    for (std::size_t j = r.startStep; !byTheWipe && j < r.step && j < r.plan->steps.size(); ++j)
    {
        const PlanStep& p = r.plan->steps[j];
        if (p.kind == PlanStepKind::MoveDirect)
            break;
        if (p.kind == PlanStepKind::Move && !PointNearLivePack(r, f.team, p.x, p.y, p.z))
        {
            target = j;
            break;
        }
    }
    if (target == static_cast<std::size_t>(-1))
        return false;
    RescueDue(r, f, target);
    r.note = "walk back: the lead bot is up behind the run's start step (step " + std::to_string(r.startStep + 1) +
        "; the plan point nearest it is step " + std::to_string(nearest + 1) + "): it is teleported to " +
        (byTheWipe ? "the safe point on the way it came (step " + std::to_string(target + 1) + ")"
                   : "step " + std::to_string(target + 1)) +
        " out of combat";
    return true;
}

// The yards a replay of the plan's points (ReplayWalkBack) walks before it reaches plan point `target`: from the lead
// bot to the replay's first point (ReplayStartPoint), then point to point (every move point) up to `target`. 0 when
// the replay begins at or after `target`, or has no first point.
inline float ReplayYardsTo(const Run& r, const Facts& f, std::size_t target)
{
    std::size_t const from = ReplayStartPoint(r, f);
    if (from == static_cast<std::size_t>(-1) || from >= target || target >= r.plan->steps.size())
        return 0.0f;
    const PlanStep& a = r.plan->steps[from];
    float yd = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, a.x, a.y, a.z);
    float x = a.x, y = a.y, z = a.z;
    for (std::size_t i = from + 1; i <= target; ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsRunMovePoint(r, i))
            continue;
        yd += ai::PlanDistance(x, y, z, p.x, p.y, p.z);
        x = p.x;
        y = p.y;
        z = p.z;
    }
    return yd;
}

// A straight walk back that failed (no complete path, no walkable path, its hops spent, or its way past an uncleared
// pack after the backoffs) in a bot-only party, when the replay of the plan's points would walk at least
// kReplayRescueYd before the safe point on the way it came: the rescue teleport to that point instead (the safe-point
// rule of WalkBackStuckRescue: the last point reached, or the latest one before it out of every live pack's reach),
// bounded as every rescue (RescueAllowed: AiPlayerbot.DungeonRunWalkBackRescue, out of combat, once a step, re-armed
// by a self-regroup after a death). Never with a real player in the party (Facts::players): he walks back with the
// party, the replay as before. Stratholme r29 slot 4 (wipes at steps 55-56: the replay from step 1, 685-780 yd,
// re-fighting respawned packs, a second wipe on the way, about 12 min), Maraudon r28b slot B (step 565: the replay
// from step 1, about 9,000 yd; still at step 25 after 20 min). True when the rescue is due.
inline bool ReplayRescue(Run& r, const Facts& f, const std::string& why)
{
    if (!f.players.empty() || !r.walkBack || !RescueAllowed(r, f) || r.walkBackIdx >= r.step)
        return false;
    std::size_t const last = LastReachedBefore(r, r.step);
    if (last == static_cast<std::size_t>(-1) || last >= r.plan->steps.size())
        return false;
    std::size_t const target = SafePointAtOrBefore(r, f.team, last);
    if (target == static_cast<std::size_t>(-1))
        return false; // never a teleport beside a live pack: the replay as before
    const PlanStep& p = r.plan->steps[target];
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) <= kArriveYd)
        return false;
    float const yd = ReplayYardsTo(r, f, target);
    if (yd < kReplayRescueYd)
        return false;
    RescueDue(r, f, target);
    r.strandBotOnly = true; // armed bot-only: StrandedTeleportTick rechecks for a joined player before the send
    r.strandReplay = true; // refused kStrandTries times: the walk back replays the plan's points (StrandFailed)
    r.note = "walk back: " + why + ": the replay of the plan's points would walk " + Num(yd) + " yd: the lead bot " +
        "is teleported to a safe point on the way it came (step " + std::to_string(target + 1) + ") out of combat";
    return true;
}

// The yards of the plan's move points from point `from` to point `target`, point to point (every move point between, as
// ReplayYardsTo counts them, without the lead bot's leg to `from`); 0 when `from` is not before `target`.
inline float PointsYards(const Run& r, std::size_t from, std::size_t target)
{
    if (!r.plan || from >= target || target >= r.plan->steps.size())
        return 0.0f;
    const PlanStep& a = r.plan->steps[from];
    float yd = 0.0f;
    float x = a.x, y = a.y, z = a.z;
    for (std::size_t i = from + 1; i <= target; ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsRunMovePoint(r, i))
            continue;
        yd += ai::PlanDistance(x, y, z, p.x, p.y, p.z);
        x = p.x;
        y = p.y;
        z = p.z;
    }
    return yd;
}

// A straight walk back's backoff (its way passes an uncleared pack: the target moves back to `back`, ClearPointBefore)
// makes the walk back a replay of the plan's points from `back`: when those points walk kReplayRescueYd or more before
// the safe point on the way it came, the rescue teleport to that point instead of the backoff, by ReplayRescue's rule and
// bounds (a bot-only party, RescueAllowed, a safe point, never onto the lead bot's own spot; the same kind: armed
// bot-only, refused kStrandTries times it replays). The straight walk to `back` itself is not counted: the straight walk
// is the first resort at any length. Shadowfang Keep r36 slot A (wipes 2 and 3 at step 43): 'straight to step 16 instead
// (1 of 2)', then 'straight to step 1 instead (2 of 2)', every point 7-15 within reach of a later leg's pack on the floors
// above; the lead bot was revived on step 1, so the straight walk "worked" at once and the walk back walked the points
// 1-42 (307 yd, 98-132 s) without ReplayRescue, which only a failed straight walk asks. True when the rescue is due.
inline bool BackoffRescue(Run& r, const Facts& f, const std::string& why, std::size_t back)
{
    if (!f.players.empty() || !r.walkBack || !RescueAllowed(r, f) || r.walkBackIdx >= r.step)
        return false;
    std::size_t const last = LastReachedBefore(r, r.step);
    if (last == static_cast<std::size_t>(-1) || last >= r.plan->steps.size())
        return false;
    std::size_t const target = SafePointAtOrBefore(r, f.team, last);
    if (target == static_cast<std::size_t>(-1))
        return false; // never a teleport beside a live pack: the backoff as before
    const PlanStep& p = r.plan->steps[target];
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) <= kArriveYd)
        return false;
    float const yd = PointsYards(r, back, target);
    if (yd < kReplayRescueYd)
        return false;
    RescueDue(r, f, target);
    r.strandBotOnly = true; // armed bot-only: StrandedTeleportTick rechecks for a joined player before the send
    r.strandReplay = true; // refused kStrandTries times: the walk back replays the plan's points (StrandFailed)
    r.note = "walk back: " + why + ": the plan's points from step " + std::to_string(back + 1) + " would walk " + Num(yd) +
        " yd: the lead bot is teleported to a safe point on the way it came (step " + std::to_string(target + 1) +
        ") out of combat";
    return true;
}

// A walk-back point with no walkable path from the spot where the hop off the navmesh island found no spot
// (Run::noSpotKnown): the walk back and the island hop both failed (the owner's last resort, 2026-10-23), so the lead
// bot is teleported to a safe point on the way it came (as WalkBackStuckRescue), not passed point after point. Dire
// Maul North r31 slot 6: revived on the entrance island at 115.7 -181.6 -3.1, every point 4-114 passed ('no walkable
// path toward the point of step N: on to the next'), then 'walked back to step 115' and four pauses with the party
// never moved. Out of combat, once a step (RescueAllowed); never with a real player in the party (Facts::players):
// he walks back with the party, the point passed as before (as with no safe point). True when the rescue is due.
inline bool IslandWalkBackRescue(Run& r, const Facts& f, const std::string& why)
{
    if (!r.noSpotKnown || ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.noSpotX, r.noSpotY, r.noSpotZ) > kArriveYd)
        return false;
    if (!f.players.empty() || !r.walkBack || !RescueAllowed(r, f) || r.walkBackIdx >= r.step)
        return false;
    std::size_t const last = LastReachedBefore(r, r.step);
    if (last == static_cast<std::size_t>(-1) || last >= r.plan->steps.size())
        return false;
    std::size_t const target = SafePointAtOrBefore(r, f.team, last);
    if (target == static_cast<std::size_t>(-1))
        return false;
    const PlanStep& p = r.plan->steps[target];
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) <= kArriveYd)
        return false;
    RescueDue(r, f, target);
    r.strandBotOnly = true; // armed bot-only: StrandedTeleportTick rechecks for a joined player before the send
    r.note = "walk back: " + why + " from a navmesh island the hop could not leave: the lead bot is teleported to a " +
        "safe point on the way it came (step " + std::to_string(target + 1) + ") out of combat";
    return true;
}

inline bool HoleReplayRescue(Run& r, const Facts& f, std::size_t hole); // defined below (StartWalkBack's hole branch calls it)

// The point a replayed walk back goes on to after reaching point `i`: the latest plain move point k > i, before the
// current step, within kRouteLinkYd on the ground and kRouteLinkDzYd up or down of point i, with no direct step, exact
// point (or its launch) or uncleared pack between them; i + 1's turn (i itself) when there is none.
inline std::size_t RouteShortcut(const Run& r, std::size_t i)
{
    if (!r.plan || i >= r.plan->steps.size())
        return i;
    const PlanStep& a = r.plan->steps[i];
    std::size_t best = i;
    for (std::size_t k = i + 1; k < r.step && k < r.plan->steps.size(); ++k)
    {
        const PlanStep& p = r.plan->steps[k];
        if (p.kind == PlanStepKind::MoveDirect || IsExactPoint(r.plan, k) || r.notCleared.count(k))
            break;
        if (p.kind != PlanStepKind::Move || ExactLaunch(r.plan, k) != static_cast<std::size_t>(-1))
            continue;
        if (GroundDistance(a.x, a.y, p.x, p.y) <= kRouteLinkYd && std::fabs(a.z - p.z) <= kRouteLinkDzYd)
            best = k;
    }
    return best;
}

// After a regroup: the way back to the current step. The rule (route topology,
// never straight-line proximity: Uldaman's route doubles back, and a point past
// the scorpid pit lies nearer the entrance than points before it):
//   - Every direct step the lead bot has not crossed is replayed, in plan
//     order. It has crossed exactly those up to the route checkpoint while
//     that is known (Run::checkpoint: the last point it reached, kept across
//     wipes; lost on a death or a pause unless it is back on the same spot);
//     otherwise none (a release comes back at the entrance, before every hole).
//   - With such a direct step: straight (navmesh hops) to the last move point
//     before the first of them, not before the checkpoint, then the plan's
//     points from there (the hops over the hole, the rest). Uldaman: step 39
//     and the pit hops, not step 1 (run 4: 528 yd through Baelog's wing).
//   - Without one, no walk back when the lead bot stands at the last pull or
//     boss the party cleared after the last move point (LastClearedFight: within
//     kSeeYd of its point and kStragglerDzYd up or down): the run goes on from
//     the last point it reached, not from the move point before those fights.
//     Sunken Temple r25 slot 4 step 5: after the pulls of steps 2-4 each resume
//     sent the lead bot from 35 yd beside step 4's pack back up the entrance
//     ramp to step 1 ('the straight way to the point of step 1 passes an
//     uncleared pack: replaying the plan's points', 14-46 s each).
//   - Without one (no direct step in the plan, or all of them crossed): as
//     before, no walk back when it stands on the last move point before the
//     step or within kApproachYd of the step's own point and nearer to it than
//     to that move point (Galgann pull 108, 2026-10-16: a straight line from
//     the entrance is shorter to the pack than to its stand yet has no path),
//     else straight to the last move point in navmesh hops.
// With no direct step left to cross, standing on the last move point sets the
// checkpoint there.
inline void StartWalkBack(Run& r, const Facts& f)
{
    AbandonDeferRetry(r); // no walk back for a retry, ever: given up, the walk back goes to the step it left
    r.walkBackDue = false;
    r.walkBack = false;
    r.walkBackDirect = false;
    r.straightBackoffs = 0;
    r.shortcutWanted = r.shortcutFrom = static_cast<std::size_t>(-1);
    const PlanStep* cur = Current(r);
    if (!cur)
        return;
    std::size_t last = r.step;
    for (std::size_t i = r.startStep; i < r.step; ++i) // never a point before the run's start step
        if (IsRunMovePoint(r, i))
            last = i;
    // A placed run's lead bot up behind its start step: teleported to the run's first safe point (PlacedRunRescue).
    if (PlacedRunRescue(r, f))
        return;
    // After a 'three moves in a row had no walkable path' pause (Run::noPathSkips, the run
    // back at the first of them): the last point the lead bot really reached, when that is
    // a plain move's (LastReachedBefore), not a move point it never reached.
    if (!r.noPathSkips.empty() && r.step == r.noPathSkips.front())
    {
        std::size_t const reached = LastReachedBefore(r, r.step);
        if (reached != static_cast<std::size_t>(-1) && r.plan->steps[reached].kind == PlanStepKind::Move)
            last = reached;
    }
    if (last == r.step)
        return;
    const PlanStep& p = r.plan->steps[last];
    float const toLast = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
    // The holes first: on a folded route the spot a lead bot comes back at can
    // lie on a later point (the last one too) before the holes it has to cross.
    std::size_t const hole = FirstUncrossedDirect(r);
    if (hole != static_cast<std::size_t>(-1))
    {
        if (HoleReplayRescue(r, f, hole))
            return;
        r.walkBack = true;
        r.walkBackIdx = LastPointBeforeDirect(r, hole);
        r.walkBackDirect = r.walkBackIdx < hole; // a navmesh point first; a direct step is replayed as such
        r.walkBackStart = r.walkBackIdx;
        r.walkBackRestarted = false;
        r.approachHops = 0;
        r.note = "walk back: a direct step is on the way (step " + std::to_string(hole + 1) + "): " +
            (r.walkBackDirect ? "straight to step " + std::to_string(r.walkBackIdx + 1) + ", then the plan's points"
                              : std::string("the plan's points from it"));
        EnterPhase(r, Phase::Begin);
        return;
    }
    if (toLast <= kArriveYd)
    {
        NoteCheckpoint(r, last);
        return; // on the last point already
    }
    std::size_t const cleared = LastClearedFight(r, f.team, last);
    if (cleared != static_cast<std::size_t>(-1))
    {
        const PlanStep& c = r.plan->steps[cleared];
        float const toCleared = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, c.x, c.y, c.z);
        if (toCleared <= kSeeYd && std::fabs(f.leadZ - c.z) <= kStragglerDzYd)
        {
            r.note = "walk back: none, the lead bot stands at the last cleared step (step " +
                std::to_string(cleared + 1) + ", " + Num(toCleared) + " yd from its point)";
            return;
        }
    }
    float const toStep = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, cur->x, cur->y, cur->z);
    if (toStep <= kApproachYd && toStep <= toLast)
        return; // back near the step already
    // The straight target clear of every live pack (StraightWalkBackTarget): the last point when it is, else the
    // latest point before it that is; the plan's points from there on are replayed.
    std::size_t const target = StraightWalkBackTarget(r, f.team, last);
    r.walkBack = true;
    r.walkBackDirect = true;
    r.walkBackIdx = target;
    r.walkBackStart = target;
    if (target != last)
        r.note = "walk back: the point of step " + std::to_string(last + 1) + " lies by an uncleared pack: straight to " +
            "step " + std::to_string(target + 1) + ", then the plan's points";
    r.walkBackRestarted = false;
    r.approachHops = 0;
    EnterPhase(r, Phase::Begin);
}

// A walk back with a direct step (a navmesh hole) the lead bot has not crossed (StartWalkBack's hole branch) replays the
// plan's points from the last point before the hole; when that walk (the lead bot's leg to that point, then point to
// point) would be kReplayRescueYd or more before the safe point on the way it came, and that safe point lies past the
// hole, the rescue teleport goes to that point instead, by ReplayRescue's rule and bounds (a bot-only party, RescueAllowed,
// a safe point, never onto the lead bot's own spot; armed bot-only, refused kStrandTries times it replays the plan's
// points: StrandFailed). Uldaman 19z16 (wipe at step 84: 'point of step 40' -> 61 -> 84, 4 min) and r18-19z36 (after the
// Stone Keeper fight-ceiling pause: steps 40-67, about 5 min). True when the rescue is due.
inline bool HoleReplayRescue(Run& r, const Facts& f, std::size_t hole)
{
    if (!f.players.empty() || !RescueAllowed(r, f) || !r.plan || hole >= r.plan->steps.size())
        return false;
    std::size_t const last = LastReachedBefore(r, r.step);
    if (last == static_cast<std::size_t>(-1) || last >= r.plan->steps.size())
        return false;
    std::size_t const target = SafePointAtOrBefore(r, f.team, last);
    if (target == static_cast<std::size_t>(-1) || target <= hole)
        return false; // a safe point before the hole: the straight walk there is the short way
    const PlanStep& t = r.plan->steps[target];
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, t.x, t.y, t.z) <= kArriveYd)
        return false;
    std::size_t const from = LastPointBeforeDirect(r, hole);
    const PlanStep& a = r.plan->steps[from];
    float const yd = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, a.x, a.y, a.z) + PointsYards(r, from, target);
    if (yd < kReplayRescueYd)
        return false;
    RescueDue(r, f, target);
    r.strandBotOnly = true; // armed bot-only: StrandedTeleportTick rechecks for a joined player before the send
    r.strandReplay = true;  // refused kStrandTries times: the walk back replays the plan's points (StrandFailed)
    r.note = "walk back: a direct step is on the way (step " + std::to_string(hole + 1) + ") and the plan's points from step " +
        std::to_string(from + 1) + " would walk " + Num(yd) + " yd: the lead bot is teleported to a safe point on the way it " +
        "came (step " + std::to_string(target + 1) + ") out of combat";
    return true;
}

// On to the next walk-back point, with fresh tries, hops and gather waiver.
inline void NextWalkBackPoint(Run& r)
{
    r.islandSuspect = false;
    ++r.walkBackIdx;
    r.walkBackRefusals = 0;
    r.approachHops = 0;
    r.gatherWaived = false;
    r.gatherRefusals = 0;
    EnterPhase(r, Phase::Begin);
}

// A bound on any walk back, whatever its state (the final check on d7e94c3b:
// after a hop that found no spot one point went out every tick, 3000 ticks):
// each try at the current point (its move sent, or its way found missing) from
// one spot (kArriveYd) is counted; past kWalkBackResends the point is passed.
// True when it was.
inline bool ResendBound(Run& r, const Facts& f)
{
    if (r.resendIdx == r.walkBackIdx &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.resendX, r.resendY, r.resendZ) <= kArriveYd)
        ++r.resends;
    else
    {
        r.resendIdx = r.walkBackIdx;
        r.resendX = f.leadX;
        r.resendY = f.leadY;
        r.resendZ = f.leadZ;
        r.resends = 1;
    }
    if (r.resends <= kWalkBackResends)
        return false;
    r.resends = 0;
    r.note = "walk back: the point of step " + std::to_string(r.walkBackIdx + 1) + " was tried " +
        std::to_string(kWalkBackResends) + " times from this spot: on to the next";
    NextWalkBackPoint(r); // clears the island suspicion too
    return true;
}

// One second of the walk back. True while it walks (the tick ends there);
// false once it is over. Each point is replayed as its move step would be:
// the hostiles within kHopPullYd of it (the glue's hopUnits, read for this
// point: FactStep) are pulled first, a direct one gathers first, then a
// WalkTo (bounded by kWalkTries). One point a tick: the facts are read for it.
inline bool WaitForFallen(Run& r, const Facts& f);

// A dead bot first (the owner's wait rule, 2026-10-17): no walk back point and
// no direct step goes out while a party member is dead. A direct step takes
// the lead bot's line, and a dead bot cannot join it. Uldaman run 4, 14:03:
// the rogue died on his corpse run 120 yd away after the regroup was over;
// the walk back pushed on to the pit's direct step, named a bot that stood
// with the lead bot, and paused with the rogue still dead 9 minutes later.
// The ordinary wait (WaitForFallen) only runs between steps; this asks for
// the same regroup (revive party, gather, a stuck straggler summoned) on any
// tick before such an order. True when the regroup starts. Not in a fight,
// a scripted event or a regroup.
inline bool FallenFirst(Run& r, const Facts& f)
{
    if (f.dead == 0 || r.regroup || f.anyInCombat || InEvent(r))
        return false;
    return WaitForFallen(r, f);
}

// The stranded lead bot's teleport (StrandedLead): sent on the first walk-back tick
// with the party out of combat ("rescue from lx ly lz to x y z", the glue's own
// RunStrandedTeleport, not a `cmd`; the spot read afresh from this tick's facts),
// then nothing else for kStrandSettleMs. Then the lead bot standing at the point
// (kArriveYd) is the success: the step's teleport is used (Run::strandStep) and
// the bots left behind (on another floor, kStragglerDzYd, or beyond kGatherYd)
// are summoned to it, one a tick, out of combat; else a failed try (StrandFailed).
// True while it holds the walk back.
inline bool StrandedTeleportTick(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    if (!r.plan || r.strandIdx >= r.plan->steps.size())
        return false;
    const PlanStep& p = r.plan->steps[r.strandIdx];
    std::string const target = "the last reached point (step " + std::to_string(r.strandIdx + 1) + ")";
    std::string const spot = Num(r.strandX) + " " + Num(r.strandY) + " " + Num(r.strandZ);
    if (r.strandOn)
    {
        r.strandMs += dt;
        if (r.strandMs < kStrandSettleMs)
            return true;
        r.strandOn = false;
        r.strandMs = 0;
        EnterPhase(r, Phase::Begin);
        float const away = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
        if (away > kArriveYd)
        {
            StrandFailed(r, "the lead bot stands " + Num(away) + " yd from the point after it");
            return true;
        }
        r.strandStep = r.step;
        NoteCheckpoint(r, r.strandIdx); // the lead bot stands on that point (kArriveYd): past every hole before it
        r.strandReplay = false; // used: no replay after it
        r.strandBotOnly = false; // used: no player recheck after it
        r.strandFromOtherPart = false; // used: no player pause after it
        r.strandSummon = true;
        r.strandSummoned.clear();
        r.strandSummonMs = 0;
        r.note = "the lead bot was stranded at " + spot + " with no way back: teleported to " + target;
        return true;
    }
    if (r.strandSummon)
    {
        // A bot left behind that fights (in combat, or with attackers) is only
        // waited for: it is summoned on the first tick it is free, for at most
        // kStrandSummonWaitMs, then the run pauses naming it.
        const MemberFact* busy = nullptr;
        for (const MemberFact& m : f.members)
        {
            if (m.name.empty() || m.name == f.carrier || r.strandSummoned.count(m.name))
                continue;
            if (std::fabs(m.dz) <= kStragglerDzYd && m.yd <= kGatherYd)
                continue;
            if (m.inCombat || m.attackers > 0)
            {
                if (!busy)
                    busy = &m;
                continue;
            }
            r.strandSummoned.insert(m.name);
            MarkSummoned(r, m.name);
            order = "summon " + m.name;
            r.note = "the lead bot was teleported to " + target + ": " + m.name + " was left behind (" + Num(m.yd) +
                " yd from it): summoned";
            return true;
        }
        if (!busy)
        {
            r.strandSummon = false;
            r.strandSummonMs = 0;
            return false;
        }
        r.strandSummonMs += dt;
        if (r.strandSummonMs >= kStrandSummonWaitMs)
        {
            Pause(r, "the lead bot was teleported to " + target + ", but " + busy->name + " stayed in a fight " +
                Num(busy->yd) + " yd from it at " + Num(busy->x) + " " + Num(busy->y) + " " + Num(busy->z) + " for " +
                std::to_string(kStrandSummonWaitMs / 1000) + " s: bring it to the lead bot, then .bot run resume");
            return true;
        }
        r.note = "the lead bot was teleported to " + target + ": waiting for " + busy->name + " (left behind, " +
            Num(busy->yd) + " yd from it) to leave its fight before summoning it";
        return true;
    }
    if (!r.strandDue)
        return false;
    if (f.anyInCombat || f.leadInCombat)
    {
        r.note = "the lead bot is stranded at " + Num(f.leadX) + " " + Num(f.leadY) + " " + Num(f.leadZ) +
            " with no way back: the teleport to " + target + " waits for the fight to end";
        return true;
    }
    // A bot-only rescue armed, then a real player joined before the send: he walks back with the party, so drop
    // the teleport (no try spent) and fall back as a refused rescue of that kind does (a refused ReplayRescue
    // replays the plan's points, a refused island rescue passes the point as before). Older rescues never set
    // the flag and send as before. An other-part rescue's teleport is different: the lead bot is in another part,
    // so the party alone cannot walk back to it; the run pauses for the other part instead (PauseOtherPart of
    // OtherPartBlock::Player: the old player-present pause and hold, no walk-back advance).
    if (r.strandBotOnly && !f.players.empty())
    {
        bool const fromOtherPart = r.strandFromOtherPart;
        r.strandDue = false;
        r.strandBotOnly = false;
        r.strandFromOtherPart = false;
        if (fromOtherPart)
        {
            PauseOtherPart(r, f, OtherPartBlock::Player);
            return true;
        }
        if (r.strandReplay)
        {
            std::string const why = "a real player joined after the rescue was armed: replaying the plan's points";
            if (ReplayWalkBack(r, f))
            {
                r.note = "walk back: " + why;
                return true;
            }
            r.note = "walk back: a real player joined after the rescue was armed: the walk back goes on as before";
            return true;
        }
        std::string const whyJoined = "walk back: a real player joined after the rescue was armed";
        PassWalkBackPoint(r, f.leadX, f.leadY, f.leadZ, whyJoined);
        return true;
    }

    // The spot as the facts read it now (the lead bot may have moved on the island); a send keeps strandReplay
    // and strandFromOtherPart (they say what the teleport was, cleared when StrandFailed gives it up or the lead
    // bot arrives).
    r.strandX = f.leadX;
    r.strandY = f.leadY;
    r.strandZ = f.leadZ;
    std::string const now = Num(r.strandX) + " " + Num(r.strandY) + " " + Num(r.strandZ);
    r.strandDue = false;
    r.strandOn = true;
    r.strandMs = 0;
    // The lead bot leaves the island: its readings there say nothing any more.
    r.noSpotKnown = false;
    r.islandSuspect = false;
    r.islandEvidence = false;
    r.passSpotKnown = false;
    r.approachHops = 0;
    r.walkBackRefusals = 0;
    order = "rescue from " + now + " to " + Num(p.x) + " " + Num(p.y) + " " + Num(p.z);
    r.note = "the lead bot is stranded at " + now + " with no way back: teleporting it to " + target;
    return true;
}

// The walk back's prefix on a hop pull's note, once (r18-19z36: 'walk back: walk back: ... gathering before a pull').
inline std::string WalkBackNote(const std::string& note)
{
    static const std::string prefix = "walk back: ";
    return note.compare(0, prefix.size(), prefix) == 0 ? note : prefix + note;
}

inline bool WalkBack(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    if (!r.walkBack)
        return false;
    if (r.walkBackIdx < r.step && FallenFirst(r, f))
        return true;
    if (StrandedTeleportTick(r, f, dt, order))
        return true;
    // The replay's shortcut asked for last tick (RouteShortcut): taken only on a direct navmesh path there.
    if (r.shortcutWanted != static_cast<std::size_t>(-1))
    {
        std::size_t const to = r.shortcutWanted, from = r.shortcutFrom;
        r.shortcutWanted = r.shortcutFrom = static_cast<std::size_t>(-1);
        bool const take = f.shortcutRead && f.shortcutFor == to && f.shortcutOk && to > from + 1;
        r.walkBackIdx = take ? to - 1 : from; // the next point is `to` itself, or the one after `from`
        NextWalkBackPoint(r);
        r.note = take ? "walk back: the route comes back by the point of step " + std::to_string(from + 1) + " at step " +
                std::to_string(to + 1) + ": steps " + std::to_string(from + 2) + "-" + std::to_string(to) + " left out"
                      : "walk back: no direct way from step " + std::to_string(from + 1) + " to step " +
                std::to_string(to + 1) + ": the plan's points";
        return true;
    }
    while (r.walkBackIdx < r.step && !IsRunMovePoint(r, r.walkBackIdx))
        ++r.walkBackIdx;
    if (r.walkBackIdx >= r.step)
    {
        r.walkBack = false;
        r.stepIdleMs = 0;
        r.note = "walked back to step " + std::to_string(r.step + 1);
        EnterPhase(r, Phase::Begin);
        return true; // the next tick reads the facts for the step itself
    }
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return true;
    }
    if (const UnitFact* hopUnit = PickPull(r, f.hopUnits, 0, true))
    {
        PullUnit(r, f, *hopUnit, order, nullptr, true, dt);
        if (r.state == State::Running)
            r.note = WalkBackNote(r.note);
        return true;
    }
    const PlanStep& p = r.plan->steps[r.walkBackIdx];
    // A point out of sight (more than kSeeYd: beyond ReactDistance the move
    // itself is refused) is approached in navmesh hops, as a resume does;
    // no path toward it at all: it is passed for the next one.
    if (!f.seen && !(r.phase == Phase::Walk && r.walkX == p.x && r.walkY == p.y && r.walkZ == p.z))
    {
        std::string const what = "the point of step " + std::to_string(r.walkBackIdx + 1);
        if (r.phase == Phase::Walk)
        {
            r.note = "walk back: approaching " + what;
            if (WalkTo(r, f, r.walkX, r.walkY, r.walkZ, false, dt, order))
                EnterPhase(r, Phase::Begin);
            return true;
        }
        // Never at a direct step's point: the lead bot may stand at its launch
        // spot, and DirectStillToCross decides there.
        if (p.kind != PlanStepKind::MoveDirect && IslandConfirmed(r, f, p.x, p.y, p.z))
        {
            IslandEscape(r, f, p.x, p.y, p.z, what, order);
            return true;
        }
        if (r.walkBackDirect && f.approachKnown && f.approachIncomplete)
        {
            // No complete navmesh path reaches the point from here: hops along it end on a ledge or a dead end
            // (Razorfen Kraul r27 slot 8). The plan's own points instead, the way the run came.
            if (ReplayRescue(r, f, "no complete path to " + what + " from here"))
                return true;
            if (ReplayWalkBack(r, f))
                r.note = "walk back: no complete path to " + what + " from here: replaying the plan's points";
            else
                r.note = "walk back: no complete path to " + what + " from here";
            return true;
        }
        if (r.walkBackDirect && f.directPathCrossesPack)
        {
            // The shortest way passes a pack not cleared yet: first a straight target farther back that no live pack
            // is near (kStraightBackoffs times), then the plan's own points instead (the way the run came, its packs
            // cleared).
            if (r.straightBackoffs < kStraightBackoffs && r.walkBackIdx == r.walkBackStart)
            {
                std::size_t const back = ClearPointBefore(r, f.team, r.walkBackIdx, kStraightBackoffYd);
                if (back != static_cast<std::size_t>(-1) &&
                    BackoffRescue(r, f, "the straight way to " + what + " passes an uncleared pack", back))
                    return true;
                if (back != static_cast<std::size_t>(-1))
                {
                    ++r.straightBackoffs;
                    r.walkBackIdx = r.walkBackStart = back;
                    r.approachHops = 0;
                    r.note = "walk back: the straight way to " + what + " passes an uncleared pack: straight to step " +
                        std::to_string(back + 1) + " instead (" + std::to_string(r.straightBackoffs) + " of " +
                        std::to_string(kStraightBackoffs) + ")";
                    return true;
                }
            }
            if (ReplayRescue(r, f, "the straight way to " + what + " passes an uncleared pack"))
                return true;
            if (ReplayWalkBack(r, f))
                r.note = "walk back: the straight way to " + what + " passes an uncleared pack: replaying the plan's points";
            else
                r.note = "walk back: the straight way to " + what + " passes an uncleared pack";
            return true;
        }
        std::uint32_t const cap = r.walkBackDirect ? kMaxWalkBackHops : kMaxApproachHops;
        if (!f.approachKnown && !f.approachRead)
        {
            r.note = "walk back: approaching " + what + ": reading the way"; // the next tick asks the navmesh
            return true;
        }
        // A hop that ends where the lead bot stands goes nowhere: no path, not a hop.
        bool const noWay = !f.approachKnown ||
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.approachX, f.approachY, f.approachZ) <= kArriveYd;
        if ((noWay || r.approachHops >= cap) && ResendBound(r, f))
            return true;
        if (noWay || r.approachHops >= cap)
        {
            std::string const why = noWay ? "no walkable path toward " + what
                                                     : std::to_string(cap) + " hops did not reach " + what;
            if (r.walkBackDirect)
            {
                // Straight there failed: replay the plan's points instead.
                if (ReplayRescue(r, f, why))
                    return true;
                if (ReplayWalkBack(r, f))
                    r.note = "walk back: " + why + ": replaying the plan's points";
                else
                    r.note = "walk back: " + why;
                return true;
            }
            // A replay that cannot even begin (its first point has no walkable
            // path from where the lead bot stands) goes back to the first point
            // the route allows (ReplayFloor: the checkpoint when known, else
            // the plan's first point), once.
            std::size_t const first = FirstReplayPoint(r);
            if (!r.walkBackRestarted && r.walkBackIdx == r.walkBackStart && first != static_cast<std::size_t>(-1) &&
                first < r.walkBackIdx)
            {
                r.walkBackRestarted = true;
                r.walkBackIdx = r.walkBackStart = first;
                r.approachHops = 0;
                r.note = "walk back: " + why + ": replaying from the first point (step " + std::to_string(first + 1) + ")";
                return true;
            }
            if (noWay && IslandWalkBackRescue(r, f, why))
                return true;
            if (DirectStillToCross(r, r.walkBackIdx))
            {
                Pause(r, "walk back: " + why + ": a direct step over a navmesh hole is not passed; " +
                    "gather the party at the point before it, then .bot run resume");
                return true;
            }
            PassWalkBackPoint(r, f.leadX, f.leadY, f.leadZ, "walk back: " + why);
            return true;
        }
        ++r.approachHops;
        r.note = "walk back: approaching " + what;
        WalkTo(r, f, f.approachX, f.approachY, f.approachZ, false, dt, order);
        return true;
    }
    bool const direct = p.kind == PlanStepKind::MoveDirect;
    // An exact waypoint on the way back is replayed as its step is (ExactWalkTo),
    // and a direct step launched from one waits for its proof (MoveStep).
    bool const reachedStraightTarget = r.walkBackDirect && r.walkBackIdx == r.walkBackStart;
    if (IsExactPoint(r.plan, r.walkBackIdx))
    {
        r.note = "walk back: to the exact point of step " + std::to_string(r.walkBackIdx + 1);
        if (ExactWalkTo(r, f, r.walkBackIdx, dt, order))
        {
            NoteCheckpoint(r, r.walkBackIdx);
            NextWalkBackPoint(r);
            if (reachedStraightTarget)
                r.walkBackDirect = false;
        }
        return true;
    }
    if (ExactLaunchBroken(r.plan, r.walkBackIdx))
    {
        Pause(r, "walk back: " + ExactLaunchBrokenNote(r.walkBackIdx));
        return true;
    }
    std::size_t const launch = ExactLaunch(r.plan, r.walkBackIdx);
    if (launch != static_cast<std::size_t>(-1) &&
        !(r.phase == Phase::Walk && r.walkX == p.x && r.walkY == p.y && r.walkZ == p.z) &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) > kArriveYd)
    {
        const PlanStep& l = r.plan->steps[launch];
        if (r.exactProved != launch ||
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, l.x, l.y, l.z) > kExactArriveYd)
        {
            r.exactProved = static_cast<std::size_t>(-1);
            r.note = "walk back: to the exact point of step " + std::to_string(launch + 1) + " before its direct step";
            if (ExactWalkTo(r, f, launch, dt, order))
                EnterPhase(r, Phase::Begin);
            return true;
        }
    }
    if (direct && r.phase != Phase::Walk && !DirectGathered(f) && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = "walk back: gathering before a direct step";
            SendGather(r, f, order);
        }
        else if (StragglerLadderTick(r, f, order))
        {
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            GatherCeiling(r, f, order, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                " s before a direct step (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)", true);
        }
        return true;
    }
    if (direct && r.phase != Phase::Walk && HoldForDirectJoin(r, f))
        return true;
    if (!direct && r.phase != Phase::Walk && ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) > kArriveYd &&
        IslandConfirmed(r, f, p.x, p.y, p.z))
    {
        IslandEscape(r, f, p.x, p.y, p.z, "the point of step " + std::to_string(r.walkBackIdx + 1), order);
        return true;
    }
    // A bound whatever the state: the same point sent more than kWalkBackResends
    // times from one spot is passed (ResendBound).
    if (!(r.phase == Phase::Walk && r.walkX == p.x && r.walkY == p.y && r.walkZ == p.z) && ResendBound(r, f))
        return true;
    r.note = "walk back: to the point of step " + std::to_string(r.walkBackIdx + 1);
    if (WalkTo(r, f, p.x, p.y, p.z, direct, dt, order))
    {
        // The straight target is reached: from here the plan's own points go
        // on (the hole's direct steps, then the rest), each one a hop of its
        // own, not a straight walk with the straight walk's cap.
        NoteReached(r, f, r.walkBackIdx);
        // A stretch of the route that comes back by this point is left out (RouteShortcut): its points are the
        // next ones after it.
        // The glue reads the lead bot's path to that point first (Run::shortcutWanted): no shortcut through a thin
        // wall (the Opus review of 0d9957be).
        std::size_t const reached = r.walkBackIdx;
        std::size_t const on = direct ? reached : RouteShortcut(r, reached);
        if (on > reached + 1)
        {
            r.shortcutWanted = on;
            r.shortcutFrom = reached;
            r.note = "walk back: the route comes back by the point of step " + std::to_string(reached + 1) +
                " at step " + std::to_string(on + 1) + ": reading the way there";
            if (reachedStraightTarget)
                r.walkBackDirect = false;
            return true;
        }
        NextWalkBackPoint(r);
        if (reachedStraightTarget)
            r.walkBackDirect = false;
    }
    return true;
}

// The approach hop the pack came in sight during is walked to its end first
// (Ragefire 19z13, trogg cave pull 13: the pull's gather broke the hop off, the
// anchor was still the stand at the top of the ramp, more than kStragglerDzYd
// above, so the gather "went back" to it and the next hop came forward again:
// eight times, then the hop cap). Arriving at the hop's point makes it the
// anchor (WalkTo). Not when a unit of the step stands within kHopClearYd of the
// lead bot: then the hop is over and the pull goes out from here, with the gather
// (and the gather back to the old anchor) left out for this pull.
inline bool FinishHopInSight(Run& r, const Facts& f, std::uint32_t dt, std::string& order, const char* what)
{
    if (r.phase != Phase::Walk || !r.hopWalk || r.walkX != r.hopX || r.walkY != r.hopY || r.walkZ != r.hopZ)
        return false;
    for (const UnitFact& u : f.units)
        if (u.hostile && u.distLead < kHopClearYd)
        {
            // The hop is over; the pull goes from where the lead bot stands. The
            // party is neither gathered beside the pack nor sent back to the old
            // anchor for this pull: the followers stay behind (PullUnit clears
            // the waiver when the pull goes out).
            EnterPhase(r, Phase::Begin);
            r.hopWalk = false;
            r.gatherWaived = true;
            for (const UnitFact& each : f.units)
                r.hopWaiveUnits.insert(each.guid);
            return false;
        }
    r.note = std::string("approaching the ") + what;
    if (!WalkTo(r, f, r.hopX, r.hopY, r.hopZ, false, dt, order))
        return true;
    EnterPhase(r, Phase::Begin); // arrived (the anchor is set): on with the pull
    return false;
}

// The big pack's firing stand walk (Run::bigWalk, set by HoldForShot when a
// pull with kBigPackJoiners or more joiners expected walks to a stand). Uldaman
// r10-19z29 step 79: the stand walk was driven only on the ticks the pull stand
// ran; on the ticks between (another unit's joiner rest, a way-back read) the
// lead bot walked on unwatched to a stand 10 yd from the scorpids while the
// healer and the mage were 27-30 yd behind on the ramp, and the gather came
// after it stood there. This gate runs on every out-of-combat tick of the walk:
// a living bot more than kGatherYd from both the lead bot and the stand stops
// the lead bot where it stands (a gather on its own spot) until every bot is
// that close, at most kGatherCapMs; then the walk goes on with "proceeding
// anyway". It keeps the pull stand's walk state while it holds. True: this
// tick is the gate's (`order` may hold the gather or the walk's move).
inline bool BigPackWalkGate(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    if (!r.bigWalk)
        return false;
    bool const walking = r.phase == Phase::Walk && r.walkX == r.bigWalkX && r.walkY == r.bigWalkY && r.walkZ == r.bigWalkZ;
    if (!walking)
    {
        EndBigWalk(r); // the walk ended (arrived, given up, a gather took over): the usual gates from here
        return false;
    }
    std::string const what = "big pack (" + std::to_string(r.bigJoiners) + " joiners expected, " +
        std::to_string(kBigPackJoiners) + " or more)";
    // The pull stand's walk is the gate's while it holds and as it resumes: a
    // tick the stand rule did not run on would drop it (EndShotHold).
    auto keepStandWalk = [&]
    {
        r.shotGateRan = true;
        r.shotStandWalk = true;
        r.shotStandGuid = r.bigWalkGuid;
        r.shotStandX = r.bigWalkX;
        r.shotStandY = r.bigWalkY;
        r.shotStandZ = r.bigWalkZ;
        r.shotStandPrep = true;
        r.shotHoldGuid = r.bigWalkGuid;
    };
    auto walkOn = [&](const std::string& note)
    {
        r.bigHold = false;
        r.bigHoldMs = 0;
        keepStandWalk();
        float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.bigWalkX, r.bigWalkY, r.bigWalkZ);
        r.walkStillMs = 0;
        r.walkBestYd = dist;
        order = MoveOrder(r.bigWalkX, r.bigWalkY, r.bigWalkZ, false);
        r.note = note;
        return true;
    };
    const MemberFact* behind = nullptr;
    if (!r.bigHoldSpent)
        for (const MemberFact& m : f.members)
            if (!m.name.empty() && m.yd > kGatherYd &&
                ai::PlanDistance(m.x, m.y, m.z, r.bigWalkX, r.bigWalkY, r.bigWalkZ) > kGatherYd &&
                (!behind || m.yd > behind->yd))
                behind = &m;
    if (!behind)
    {
        if (r.bigHold)
            return walkOn(what + ": the party gathered, on to the firing stand");
        return false; // together: the pull stand drives the walk as usual
    }
    if (!r.bigHold)
    {
        // The lead bot holds where it stands; the others form up on it.
        r.bigHold = true;
        r.bigHoldMs = 0;
        r.bigHoldNote = what + ": the firing stand walk waits for the party (" + behind->name + " " +
            Num(behind->yd) + " yd behind)";
        order = MoveOrder(f.leadX, f.leadY, f.leadZ, false);
        r.gatherOrder = order;
        SampleMembers(r, f);
        keepStandWalk();
        r.note = r.bigHoldNote;
        return true;
    }
    r.bigHoldMs += dt;
    if (r.bigHoldMs >= kGatherCapMs)
    {
        r.bigHoldSpent = true;
        return walkOn(what + ": the party did not gather within " + std::to_string(kGatherCapMs / 1000) + " s (" +
            behind->name + " " + Num(behind->yd) + " yd behind): proceeding anyway");
    }
    keepStandWalk();
    r.note = r.bigHoldNote;
    return true;
}

// A pull step whose units are all gone may still have one of its entries
// walking a patrol outside the radius (Facts::patrollers). Uldaman r10-19z29
// step 66: the Stone Steward's lane reaches the 10 yd radius only at its north
// end; the step said "pack cleared" in 0 s, and the Steward came into the
// Keeper's fight two steps later. A hostile patroller is waited for, like a
// boss away from his spot (BossAwayWaitMs), up to kPatrolWaitMs or the step's
// shorter timeout_s, until it walks into the radius: there it is one of the
// step's units and the usual pick takes it (the way back, the aggro estimate,
// the fruitless count). Never in a fight, never for a friendly unit, one a
// later step owns (OwnedByLaterStep) or one the run gave up on (Run::
// unreachable, Run::patrolGaveUp: the wait ran out, or its pulls drew no
// fight); none seen: the step is cleared as before. True: the step waits;
// false: it is cleared (`timedOut` says the wait ran out).
// Whether a patroller (Facts::patrollers) is one the step waits for: hostile,
// not passed, reachable, not given up on, not a later step's.
inline bool PatrollerToWaitFor(const Run& r, const Facts& f, const UnitFact& u)
{
    if (!u.hostile || r.passed.count(u.guid) || r.unreachable.count(u.guid) || r.patrolGaveUp.count(u.guid))
        return false;
    return !(r.plan && ai::OwnedByLaterStep(*r.plan, r.step, u.entry, u.x, u.y, u.z, f.team));
}

inline bool WaitForPatroller(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& timedOut)
{
    if (s.kind != PlanStepKind::Pull || f.anyInCombat)
        return false;
    const UnitFact* p = nullptr;
    for (const UnitFact& u : f.patrollers)
    {
        if (!PatrollerToWaitFor(r, f, u))
            continue;
        r.patrolSeen.insert(u.guid);
        if (!p || u.distLead < p->distLead)
            p = &u;
    }
    if (!p)
        return false;
    std::uint32_t const capMs = s.timeoutS && s.timeoutS * 1000u < kPatrolWaitMs ? s.timeoutS * 1000u : kPatrolWaitMs;
    std::string const what = std::to_string(p->entry) + " " + GuidArg(p->guid);
    if (r.patrolWaitMs >= capMs)
    {
        r.patrolGaveUp.insert(p->guid);
        timedOut = "; the patroller " + what + " did not come within " + std::to_string(capMs / 1000) + " s";
        return false;
    }
    r.patrolWaitMs += dt;
    r.note = "waiting for the patroller " + what + " (outside the " + Num(s.radius) + " yd radius; up to " +
        std::to_string(capMs / 1000) + " s)";
    return true;
}

// How a pull or boss step stands once nothing of it is left to pull, by the
// step's own rules (FightStep ends on it; GuardWalkBackWanted asks it too, so
// the walk back after a fight is left out only when the step is really over).
// NotYet: the step goes on: its point is not in sight (Facts::seen false: an
// empty unit list there is no clear pack), a unit is still open, an ambush is
// not walked through yet, a patroller is still to come (WaitForPatroller may
// give it up later), or a boss's death was not witnessed (he may be away).
enum class FightEnd : std::uint8_t { NotYet, OutOfReach, Friendly, Cleared, BossDown };

inline std::size_t UnitsOutOfReach(const Run& r, const Facts& f)
{
    std::size_t n = 0;
    for (const UnitFact& u : f.units)
        if (u.hostile && r.unreachable.count(u.guid))
            ++n;
    return n;
}

// The stand of a stand-pull boss step: the move row right before it in the same leg, or null (then the flag acts as
// off: the step pulls as before).
inline const PlanStep* StandPullStand(const Run& r, const PlanStep& s)
{
    if (!(s.flags & ai::kPlanFlagStandPull) || r.step == 0 || !r.plan)
        return nullptr;
    const PlanStep& prev = r.plan->steps[r.step - 1];
    if (prev.kind != PlanStepKind::Move || prev.leg != s.leg)
        return nullptr;
    return &prev;
}

inline void StandPullGiveUp(Run& r, const PlanStep& s, const std::string& why)
{
    if (s.flags & ai::kPlanFlagSkipUnreachable)
    {
        r.notCleared.insert(r.step);
        NextStep(r, why + ": skipped, not cleared (the plan marks him out of reach)");
        return;
    }
    Pause(r, why + ": bring him to the party, then .bot run resume, or .bot run skip");
}

inline FightEnd FightStepEnd(const Run& r, const Facts& f, const PlanStep& s)
{
    if (!f.seen || OpenUnits(r, f.units) > 0)
        return FightEnd::NotYet;
    if ((s.flags & ai::kPlanFlagAmbush) && !r.walkedThrough)
        return FightEnd::NotYet;
    if (UnitsOutOfReach(r, f))
        return FightEnd::OutOfReach;
    for (const UnitFact& u : f.units)
        if (!u.hostile)
            return FightEnd::Friendly;
    if (s.kind != PlanStepKind::Boss)
    {
        for (const UnitFact& u : f.patrollers)
            if (PatrollerToWaitFor(r, f, u))
                return FightEnd::NotYet;
        return FightEnd::Cleared;
    }
    return f.bossDeadNearSpot || (r.bossSeenAlive && f.bossDeadNearLead) ? FightEnd::BossDown : FightEnd::NotYet;
}

// Where a place lies from the lead bot, for a boss step's pause: "N yd from the lead bot (M yd above it)" (or below),
// `dz` the place's height over the lead bot. Shadowfang Keep r36 slot A: Fenrus 0.0 yd from his spot, the party 33 yd
// below him, which the pause did not say.
inline std::string FromLeadBot(float yd, float dz)
{
    return Num(yd) + " yd from the lead bot (" + Num(std::fabs(dz)) + " yd " + (dz < 0.0f ? "below" : "above") + " it)";
}

// The note of a boss step done because every spawn of the boss belongs to a game event that is not active
// (Facts::bossSearchEventOff). An event without a description is named by its number only.
inline std::string BossEventOffNote(std::uint32_t eventId, const std::string& eventName)
{
    return "boss skipped: spawn is in inactive game event " + std::to_string(eventId) +
        (eventName.empty() ? std::string() : " (" + eventName + ")");
}

// Whether a boss's spawn rows are all gated by game events that are not active (Facts::bossSearchEventOff), from
// every event association of each row (`events[i]`: > 0 the row spawns while that event runs, < 0 it is removed
// while it runs; a row may sit in several game_event_creature rows, Codex review round 1). Off only when there is a
// row, every row has at least one association, every association is a spawn event, and none of them is active
// (`active(id)`). `eventId`: the first event of row `own` (the step's own spawn), else of the first row.
struct SpawnEventGate
{
    bool off = false;
    std::uint32_t eventId = 0;
};

template <typename IsActive>
inline SpawnEventGate SpawnEventsOff(const std::vector<std::vector<int>>& events, std::size_t own, IsActive active)
{
    SpawnEventGate gate;
    if (events.empty())
        return gate;
    for (const std::vector<int>& row : events)
    {
        if (row.empty())
            return gate;
        for (int id : row)
            if (id <= 0 || active(static_cast<std::uint32_t>(id)))
                return gate;
    }
    gate.off = true;
    gate.eventId = static_cast<std::uint32_t>(own < events.size() ? events[own].front() : events.front().front());
    return gate;
}

// pull and boss steps: approach until seen, then pull in order until nothing
// hostile of the step is left. `order` lists the entries in kill order (empty:
// any entry, nearest first).
inline void FightStep(Run& r, const Facts& f, const PlanStep& s, const std::vector<std::uint32_t>& killOrder,
    std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    // A step for the other team (flags 2/4):
    //   - a pack is skipped without a look, unless the party already stands
    //     near it and a unit of it is not friendly;
    //   - a boss is always looked at first, and skipped only when every unit
    //     of its encounter there is friendly to the party. A hostile or
    //     neutral one is fought (Uldaman's Lost Dwarves carry team horde in
    //     the plan, yet were hostile to an Alliance party in a live run).
    if (!ai::StepForTeam(s, f.team))
    {
        bool notFriendly = false;
        for (const UnitFact& u : f.units)
            notFriendly = notFriendly || (u.hostile && !r.passed.count(u.guid));
        bool const skip = s.kind == PlanStepKind::Boss ? (f.seen && !notFriendly) : (!f.seen || !notFriendly);
        if (skip)
        {
            NextStep(r, s.kind != PlanStepKind::Boss ? "pack is for the other team: skipped"
                : f.units.empty() ? "boss is for the other team and not here: skipped"
                                  : "boss is for the other team and friendly to this party: skipped");
            return;
        }
    }

    // A pullback refused for the tank's firing position asked the approach stand with the pack in sight
    // (PullbackRefusedUnreachable): its search (answered by the glue after this tick) and its walk run here, not a
    // new pull of the same unit from the same spot.
    if (r.refusedPullStandStep == r.step && r.approachStandStep == r.step &&
        (r.approachStand == 1 || r.approachStand == 2) &&
        ApproachStand(r, s, dt, f, order, s.kind == PlanStepKind::Boss ? "boss" : "pack"))
        return;

    if (!f.seen)
    {
        ApproachHop(r, f, s, dt, order, s.kind == PlanStepKind::Boss ? "boss" : "pack");
        return;
    }
    // The approach stand did its work once the pack is in sight: spent, so a later tick
    // out of sight hops again instead of the stand's 'did not bring it' pause.
    // The hops start over too (the Opus re-review of b5576562: the cap was still past 8
    // and the nearest the old 66-99 yd, so the first tick out of sight paused at once).
    if (r.approachStandStep == r.step && r.approachStand == 4)
    {
        r.approachStand = 3;
        r.approachHops = 0;
        r.approachBestYd = 0.0f;
    }
    if (FinishHopInSight(r, f, dt, order, s.kind == PlanStepKind::Boss ? "boss" : "pack"))
        return;
    for (const UnitFact& u : f.units)
        if (u.joinersRead)
            r.joinerSeen[u.guid] = u.joiners;
    if (BigPackWalkGate(r, f, dt, order))
        return;
    if (s.kind == PlanStepKind::Boss)
        for (const UnitFact& u : f.units)
            r.bossSeenAlive = r.bossSeenAlive || u.entry == s.entry;
    const UnitFact* pick = nullptr;
    if (killOrder.empty())
        pick = PickPackPull(r, f);
    for (std::uint32_t entry : killOrder)
        if (!pick)
            pick = PickPull(r, f.units, entry);
    if (pick)
    {
        r.bossWaitMs = 0;
        if (s.kind == PlanStepKind::Boss && !f.anyInCombat && pick->entry == s.entry)
            if (const PlanStep* st = StandPullStand(r, s))
            {
                float const fromStand = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, st->x, st->y, st->z);
                if (fromStand > kHoldReturnYd)
                {
                    if (!WalkTo(r, f, st->x, st->y, st->z, false, dt, order))
                    {
                        if (r.state == State::Running)
                            r.note = "back to the stand for boss " + std::to_string(s.entry);
                        r.standPullShotGuid = 0; // the pull never went out (walking back to the stand first)
                        return;
                    }
                }
                if (f.leadShotYd <= kShotMarginYd)
                {
                    StandPullGiveUp(r, s, "the tank has no ranged pull for a stand pull");
                    return;
                }
                float const inShot = f.leadShotYd - kStandPullMarginYd;
                if (pick->losLead && pick->distLead <= inShot)
                    r.standPullShotGuid = pick->guid;
                else
                {
                    r.standPullMs += dt;
                    if (r.standPullMs >= kStandPullWaitMs)
                    {
                        StandPullGiveUp(r, s, "boss " + std::to_string(s.entry) + " did not come within " + Num(inShot) +
                            " yd of the stand in 600 s");
                        return;
                    }
                    r.standPullShotGuid = 0; // the pull never went out: a later pull of another unit pulls as before
                    r.note = "boss " + std::to_string(s.entry) + " patrols: waiting at the stand for him to come within " +
                        Num(inShot) + " yd (now " + Num(pick->distLead) + " yd" + (pick->losLead ? std::string() : ", out of sight") + ")";
                    r.stepIdleMs = dt > r.stepIdleMs ? 0 : r.stepIdleMs - dt;
                    return;
                }
            }
        // The way back to the anchor not known yet: read it first (one tick),
        // before this pull counts as one.
        // (A hold for another unit than the pick is caught by PullUnit's own check of the way back.)
        if (r.phase != Phase::Gather && !r.gatherWaived && !r.shotStandPrep && CheckAnchor(r, f) == AnchorState::Unknown)
        {
            r.standPullShotGuid = 0; // the pull never went out (the way back is read first)
            r.note = "checking the way back to " + Num(r.anchorX) + " " + Num(r.anchorY) + " " + Num(r.anchorZ);
            return;
        }
        // The aggro estimate (off at joinerCeiling 0): more joiners than the
        // ceiling and the party not rested yet waits once per unit, on the
        // rest wait that already exists (TickBody), no movement. At most
        // kMaxJoinerRests such waits a step (counted when a rest really starts);
        // after that the pull goes out.
        std::string budgetNote;
        if (f.joinerCeiling > 0 && pick->joiners > f.joinerCeiling && !r.joinerHeld.count(pick->guid))
        {
            if (r.joinerRests >= kMaxJoinerRests)
                budgetNote = std::to_string(pick->joiners) + " joiners expected, rest budget used";
            else
            {
                r.joinerHeld.insert(pick->guid);
                r.joinerRest = true;
                r.standPullShotGuid = 0; // the pull never went out (the joiner rest holds this tick)
                r.joinerNote = std::to_string(pick->joiners) + " joiners expected" +
                    (pick->joinersPatrol ? " (a patrol is the cause)" : "");
                r.note = r.joinerNote;
                return;
            }
        }
        // Pulls that kill nothing, three in a row: the rest of the pack is
        // out of reach (a pit, a ledge). It is left, not counted cleared.
        std::size_t const open = OpenUnits(r, f.units);
        // A tick the pull stand held (HoldForShot) is the same pull, not another one.
        if (r.phase != Phase::Gather && !r.shotStandPrep)
        {
            if (r.pulls > 0 && open >= r.openAtLastPull)
                ++r.fruitlessPulls;
            else if (r.pulls > 0)
                r.fruitlessPulls = 0;
            r.openAtLastPull = open;
        }
        if (r.fruitlessPulls >= kFruitlessPulls)
        {
            for (const UnitFact& u : f.units)
                if (u.hostile && !r.passed.count(u.guid))
                    r.unreachable.insert(u.guid);
            r.notCleared.insert(r.step);
            NextStep(r, std::string(s.kind == PlanStepKind::Boss ? "boss" : "pack") + " skipped, not cleared: " +
                std::to_string(open) + " units out of reach after " + std::to_string(kFruitlessPulls) +
                " pulls that killed nothing");
            return;
        }
        CcPick const ccPick = PickCcOrder(r, f, pick->guid);
        PullUnit(r, f, *pick, order, ccPick.unit, false, dt);
        if (ccPick.noMarkEntry && order.compare(0, 9, "pullback ") == 0)
            r.note += "; no free raid mark for crowd control on " + std::to_string(ccPick.noMarkEntry);
        if (!budgetNote.empty() && r.phase == Phase::AwaitPull)
            r.note = f.pullStand ? budgetNote + "; " + r.note : budgetNote;
        return;
    }
    if (OpenUnits(r, f.units) > 0)
    {
        // Only evading or stealthed units left: let them settle (the step's
        // idle ceiling bounds this), and walk through a stealthed pack.
        r.note = "waiting for the pack to settle";
        bool const stealthOnly = [&] {
            for (const UnitFact& u : f.units)
                if (u.hostile && !r.passed.count(u.guid) && !u.stealthed)
                    return false;
            return true;
        }();
        if (stealthOnly && WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
            NextStep(r, "walked through a stealthed pack");
        return;
    }
    if ((s.flags & ai::kPlanFlagAmbush) && !r.walkedThrough)
    {
        // An ambush pack shows itself only when the party walks into it.
        r.note = "walking through an ambush pack";
        if (WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
            r.walkedThrough = true;
        if (!r.walkedThrough)
            return;
    }
    FightEnd const end = FightStepEnd(r, f, s);
    std::string const what = s.kind == PlanStepKind::Boss ? "boss" : "pack";
    if (end == FightEnd::OutOfReach)
    {
        r.notCleared.insert(r.step);
        NextStep(r, what + " skipped, not cleared: " + std::to_string(UnitsOutOfReach(r, f)) + " units out of reach");
    }
    else if (end == FightEnd::Friendly)
        NextStep(r, what + " is friendly to this party: left alone");
    else if (s.kind != PlanStepKind::Boss)
    {
        // Cleared, or a patroller to wait for (NotYet): WaitForPatroller decides.
        std::string timedOut;
        if (WaitForPatroller(r, f, s, dt, timedOut))
            return;
        bool const fought = r.pulls > 0;
        NextStep(r, "pack cleared" + timedOut);
        r.fightForwardDue = fought; // FightForwardTick
    }
    else if (end == FightEnd::BossDown)
    {
        // Remember where he fell: a later `loot` step for this entry goes to the
        // corpse by its guid (TaskStep), not to the plan's spawn point.
        if (f.bossCorpseGuid)
            r.corpses[s.entry] = CorpseMark{f.bossCorpseGuid, f.bossCorpseX, f.bossCorpseY, f.bossCorpseZ};
        bool const fought = r.pulls > 0;
        NextStep(r, "boss down");
        r.fightForwardDue = fought; // FightForwardTick
    }
    else
    {
        // Alive in sight by his spot, but passed: the pulls drew no fight with him (AwaitPull, kPullTriesPerUnit).
        // He is not away, so neither the wait for him nor the instance search: a pause at once that says where his
        // spot lies from the lead bot. Shadowfang Keep r36 slot A step 304: Fenrus behind the Sorcerer's Gate on
        // z 128.8, the party on z 95; 'not at his spot: waiting for him' and 60 s to the same pause, twice.
        for (const UnitFact& u : f.units)
            if (u.entry == s.entry && u.hostile && r.passed.count(u.guid))
            {
                auto const tries = r.pullTries.find(u.guid);
                std::string const noFight = "boss " + std::to_string(s.entry) + " is alive by his spot, " +
                    FromLeadBot(u.distLead, u.z - f.leadZ) + ", and " +
                    std::to_string(tries == r.pullTries.end() ? 0u : tries->second) + " pulls drew no fight with him";
                if (s.flags & ai::kPlanFlagSkipUnreachable)
                {
                    r.notCleared.insert(r.step);
                    NextStep(r, noFight + ": skipped, not cleared (the plan marks him out of reach)");
                    return;
                }
                Pause(r, noFight + ": bring the party where it can reach him, then .bot run resume, or .bot run skip");
                return;
            }
        // Not seen alive and not seen dead: he is away (a patrol, Theka in
        // Zul'Farrak), not killed. The plan rows carry no patrol path: the
        // party waits at his spot for him to come by (the row's timeout_s, or
        // kBossSearchMs when it is 0: a round longer than that needs the
        // plan's wait_s), then pauses.
        std::uint32_t const awayMs = BossAwayWaitMs(s);
        r.bossWaitMs += dt;
        r.note = "boss " + std::to_string(s.entry) + " not at his spot: waiting for him";
        if (r.bossWaitMs < awayMs)
            return;
        // The window is over: the glue searches the whole instance for him once (next tick, Facts::bossSearch*).
        if (!r.bossSearchWanted)
        {
            r.bossSearchWanted = true;
            r.note = "boss " + std::to_string(s.entry) + " not at his spot for " + std::to_string(awayMs / 1000) +
                " s: searching the instance for him";
            return;
        }
        r.bossSearchWanted = false;
        if (f.bossSearchRead && f.bossSearchOwnDead)
        {
            r.corpses[s.entry] = CorpseMark{f.bossSearchCorpseGuid, f.bossSearchCorpseX, f.bossSearchCorpseY,
                f.bossSearchCorpseZ};
            NextStep(r, "boss down: his corpse lies at " + Num(f.bossSearchCorpseX) + " " + Num(f.bossSearchCorpseY) + " " +
                Num(f.bossSearchCorpseZ) + ", " +
                Num(ai::PlanDistance(f.bossSearchCorpseX, f.bossSearchCorpseY, f.bossSearchCorpseZ, s.x, s.y, s.z)) +
                " yd from his spot (an instance search after " + std::to_string(awayMs / 1000) + " s)");
            return;
        }
        if (f.bossSearchRead && f.bossSearchAlive)
        {
            // Alive elsewhere: the party is not sent after him (he may be on a patrol, or in another part of the
            // instance); the pause says where he stands.
            std::string const aliveAt = "boss " + std::to_string(s.entry) + " is alive at " + Num(f.bossAliveX) + " " + Num(f.bossAliveY) +
                " " + Num(f.bossAliveZ) + ", " + Num(ai::PlanDistance(f.bossAliveX, f.bossAliveY, f.bossAliveZ, s.x, s.y, s.z)) +
                " yd from his spot (an instance search after " + std::to_string(awayMs / 1000) + " s), " +
                FromLeadBot(ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.bossAliveX, f.bossAliveY, f.bossAliveZ),
                    f.bossAliveZ - f.leadZ);
            if (s.flags & ai::kPlanFlagSkipUnreachable)
            {
                r.notCleared.insert(r.step);
                NextStep(r, aliveAt + ": skipped, not cleared (the plan marks him out of reach)");
                return;
            }
            Pause(r, aliveAt + ": bring the party to him or wait for him, then .bot run resume, or .bot run skip");
            return;
        }
        if (f.bossSearchRead && f.bossSearchDead)
        {
            r.corpses[s.entry] = CorpseMark{f.bossSearchCorpseGuid, f.bossSearchCorpseX, f.bossSearchCorpseY,
                f.bossSearchCorpseZ};
            NextStep(r, "boss down: his corpse lies at " + Num(f.bossSearchCorpseX) + " " + Num(f.bossSearchCorpseY) + " " +
                Num(f.bossSearchCorpseZ) + ", " +
                Num(ai::PlanDistance(f.bossSearchCorpseX, f.bossSearchCorpseY, f.bossSearchCorpseZ, s.x, s.y, s.z)) +
                " yd from his spot (an instance search after " + std::to_string(awayMs / 1000) + " s)");
            return;
        }
        // Seen neither alive nor dead, and every spawn of his belongs to a game event that is not active: he cannot be
        // in the world, so the step is done with a note, no pause. A boss with any spawn outside such an event keeps
        // the pause below.
        if (f.bossSearchRead && f.bossSearchEventOff)
        {
            NextStep(r, BossEventOffNote(f.bossSearchEventId, f.bossSearchEventName));
            return;
        }
        // Searched with a spawn in an unloaded grid: the note says only the loaded parts were looked at.
        std::string const searched = !f.bossSearchRead ? std::string()
            : f.bossSearchNoSpawn ? std::string("; the instance was not searched: he has no spawn row on this map (a script summons him)")
            : f.bossSearchUnloaded ? std::string(" nor in the loaded parts of the instance (an unloaded area was not searched)")
                                   : std::string(" nor anywhere in the instance");
        Pause(r, "boss " + std::to_string(s.entry) + " not found: not alive or dead within " + Num(s.radius) +
            " yd of " + Num(s.x) + " " + Num(s.y) + " " + Num(s.z) + " for " +
            std::to_string(awayMs / 1000) + " s" + searched + " (.bot run skip if he is already dead)");
    }
}

inline void MoveStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    if (const UnitFact* hopUnit = PickPull(r, f.hopUnits, 0, true))
    {
        PullUnit(r, f, *hopUnit, order, nullptr, false, dt, true);
        return;
    }
    // Far away (after a wipe the lead bot comes back from the entrance; a
    // long plan hop): walk back on the navmesh in hops first.
    if (!r.detour && !f.seen)
    {
        ApproachHop(r, f, s, dt, order, "next point");
        return;
    }
    // A direct step launched from an exact waypoint goes out only while the
    // proof of that waypoint stands: the lead bot proved on it (that step, or
    // this one after a resume or a fight) and still within kExactArriveYd.
    // Otherwise the tighten runs again here first (ExactWalkTo); the direct
    // walk under way (its own point in walkX) is not interrupted.
    if (ExactLaunchBroken(r.plan, r.step))
    {
        Pause(r, ExactLaunchBrokenNote(r.step));
        return;
    }
    std::size_t const launch = ExactLaunch(r.plan, r.step);
    bool const directUnderWay = r.phase == Phase::Walk && r.walkX == s.x && r.walkY == s.y && r.walkZ == s.z;
    bool const crossed = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z) <= kArriveYd;
    if (launch != static_cast<std::size_t>(-1) && !directUnderWay && !crossed)
    {
        const PlanStep& l = r.plan->steps[launch];
        if (r.exactProved != launch ||
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, l.x, l.y, l.z) > kExactArriveYd)
        {
            r.exactProved = static_cast<std::size_t>(-1);
            if (ExactWalkTo(r, f, launch, dt, order))
                EnterPhase(r, Phase::Begin); // proved: the next tick gathers and sends the direct step
            return;
        }
    }
    // The forward run's own move point in sight (r6-19z24 Zul'Farrak steps 64/65:
    // the hop pull's fight left the tank on the navmesh island at 1588-1591
    // 1044-1047, 6-9 yd from the point, and both `cmd move`s were refused): the
    // same rule as the walk back's (IslandConfirmed, two signs) before the move
    // goes out (out of sight ApproachHop asks it, unless a detour is on). Never
    // for a direct step (its line crosses a navmesh hole), nor while a walk is under way.
    if (s.kind == PlanStepKind::Move && r.phase != Phase::Walk &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z) > kArriveYd &&
        IslandConfirmed(r, f, s.x, s.y, s.z, GoodSpotFresh(r)))
    {
        IslandEscape(r, f, s.x, s.y, s.z, "the point of step " + std::to_string(r.step + 1), order);
        r.escapeFromMove = r.escapeOn; // its no-spot falls back to the move (OrderRefused)
        return;
    }
    if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && FallenFirst(r, f))
        return;
    // A direct step takes the lead bot's line: the party gathers on it first
    // (`cmd move ... direct` refuses while a bot has no way to the lead's spot).
    if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && !DirectGathered(f) && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = "gathering before a direct step";
            SendGather(r, f, order);
        }
        else if (StragglerLadderTick(r, f, order))
        {
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            GatherCeiling(r, f, order, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                " s before a direct step (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)", true);
        }
        return;
    }
    if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && HoldForDirectJoin(r, f))
        return;
    if (r.detour)
    {
        r.note = "walking around: the plan's point is off the walkable way";
        if (WalkTo(r, f, r.detourX, r.detourY, r.detourZ, false, dt, order))
        {
            // A navmesh walk to a stand-in point: for a direct step it proves
            // no crossing of its hole, so the checkpoint stays where it was.
            if (s.kind != PlanStepKind::MoveDirect)
                NoteCheckpoint(r, r.step);
            NextStep(r, "arrived at the nearest reachable point, " +
                Num(ai::PlanDistance(r.detourX, r.detourY, r.detourZ, s.x, s.y, s.z)) + " yd from the plan's");
        }
        return;
    }
    if (IsExactPoint(r.plan, r.step))
    {
        r.note = "walking to an exact point";
        if (ExactWalkTo(r, f, r.step, dt, order))
        {
            NoteCheckpoint(r, r.step);
            NextStep(r, "arrived: the lead bot stands on the exact point");
        }
        return;
    }
    r.note = "walking";
    if (WalkTo(r, f, s.x, s.y, s.z, s.kind == PlanStepKind::MoveDirect, dt, order))
    {
        NoteReached(r, f, r.step);
        NextStep(r, "arrived");
    }
}

// How far from a move step's point a stand-in may be (UseDetour, the glue's
// NearestReachablePoint): kMoveFallbackYd for a plain move, kDetourMaxYd for a
// direct step (its line is over a navmesh hole).
inline float MoveFallbackYd(const PlanStep& s)
{
    return s.kind == PlanStepKind::Move ? kMoveFallbackYd : kDetourMaxYd;
}

// The glue found that the current move step's point cannot be reached (its
// `cmd move` was refused unreachable or off-mesh) and that (x,y,z), within
// MoveFallbackYd of it, can: the step walks there instead, by the navmesh path
// around whatever is in the way. False (nothing changes) when the step is not
// a move, a detour is already on, or the point is too far from the plan's.
inline bool UseDetour(Run& r, float x, float y, float z)
{
    const PlanStep* s = Current(r);
    if (!s || r.state != State::Running || r.detour ||
        (s->kind != PlanStepKind::Move && s->kind != PlanStepKind::MoveDirect))
        return false;
    // An exact waypoint has no stand-in: a direct step launched from beside it
    // is the very thing the flag forbids. The refusal pauses the run.
    if (IsExactPoint(r.plan, r.step) || ExactLaunch(r.plan, r.step) != static_cast<std::size_t>(-1))
        return false;
    if (ai::PlanDistance(x, y, z, s->x, s->y, s->z) > MoveFallbackYd(*s))
        return false;
    r.detour = true;
    r.detourX = x;
    r.detourY = y;
    r.detourZ = z;
    EnterPhase(r, Phase::Begin);
    return true;
}

// The glue's answer to a stall's stand-in search (StallFallback): found (dx dy dz), a
// point on dry ground the lead bot has a navmesh path to, within MoveFallbackYd of the
// step's point. The step walks there (UseDetour); otherwise the walk's retries and its
// pause go on at the next tick. True: the stand-in is taken.
inline bool StallStandIn(Run& r, bool found, float dx, float dy, float dz)
{
    r.stallStandInWanted = false;
    const PlanStep* s = Current(r);
    if (!s || r.state != State::Running)
        return false;
    std::string const point = Num(s->x) + " " + Num(s->y) + " " + Num(s->z);
    if (found && UseDetour(r, dx, dy, dz))
    {
        r.note = "step " + std::to_string(r.step + 1) + ": the lead bot's own move to " + point +
            " kept failing: walking to the stand-in on dry ground " + Num(dx) + " " + Num(dy) + " " + Num(dz) + ", " +
            Num(ai::PlanDistance(dx, dy, dz, s->x, s->y, s->z)) + " yd from it, and going on from there";
        return true;
    }
    r.stallFallback = 3;
    r.walkStillMs = kWalkRetryMs;
    r.note = "the lead bot's own move to " + point + " kept failing: no stand-in on dry ground within " +
        std::to_string(static_cast<int>(MoveFallbackYd(*s))) + " yd of it";
    return false;
}

// The stall's `direct` move (StallFallback) was refused (too-far naming a bot, no-los,
// no floor under the point ...): no pause for it; the next tick goes on to the stand-in
// or the walk's own retries. True: it was that order.
inline bool StallDirectRefused(Run& r, const std::string& order, const std::string& answer)
{
    if (r.state != State::Running || r.phase != Phase::Walk || r.stallFallback != 1 ||
        order != MoveOrder(r.walkX, r.walkY, r.walkZ, true))
        return false;
    r.walkStillMs = kWalkRetryMs;
    r.note = "the straight move (direct) to " + Num(r.walkX) + " " + Num(r.walkY) + " " + Num(r.walkZ) +
        " was refused (" + answer + "): going on without it";
    return true;
}

// The refused order is the current move step's own `cmd move`, or its detour's
// (UseDetour), and not a walk back's.
inline bool OwnMoveOrder(const Run& r, const std::string& order)
{
    const PlanStep* s = Current(r);
    if (!s || (s->kind != PlanStepKind::Move && s->kind != PlanStepKind::MoveDirect) ||
        (r.walkBack && r.walkBackIdx < r.step))
        return false;
    return order == MoveOrder(s->x, s->y, s->z, s->kind == PlanStepKind::MoveDirect) ||
        (r.detour && order == MoveOrder(r.detourX, r.detourY, r.detourZ, false));
}

// The answer of a `cmd move` the server refused for want of a path to the point.
inline bool UnreachableAnswer(const std::string& answer)
{
    return answer.compare(0, 11, "unreachable") == 0 || answer.compare(0, 8, "off-mesh") == 0;
}

// Whether the glue searches a stand-in (NearestReachablePoint) for the step's
// refused move: no detour on yet, and none used for this step before.
inline bool MoveFallbackWanted(const Run& r)
{
    return Current(r) && !r.detour && !(r.moveRefusedStep == r.step && r.moveRefusals > 0);
}

// The glue's words after a refused move that MoveRefusedUnreachable left to
// OrderRefused: `searched` (a stand-in search ran and found none), else the
// step's stand-in was used already (no search ran).
inline std::string NoStandInNote(const PlanStep& s, bool searched)
{
    if (!searched)
        return " (refused again in this step, after its walk to the nearest reachable point)";
    return " (no reachable point within " + std::to_string(static_cast<int>(MoveFallbackYd(s))) + " yd of it either)";
}

// Razorfen Kraul r22-19z40 step 85: the plan's move to 2180 1604 81.2 lay in a
// navmesh pocket of its own. The run leader walked around to a stand-in point,
// the move was refused unreachable again, and the run paused; each resume
// walked back to steps 66-81 and paused on the same refusal (3 times). Now the
// glue hands a refused own move (OwnMoveOrder, UnreachableAnswer) here, with the
// stand-in it found (found, dx dy dz; MoveFallbackWanted): its first refusal
// walks to the stand-in (UseDetour, within kMoveFallbackYd), or else hops the
// lead bot off a navmesh island (RefusedFromIsland, its own caps); a plain move
// with neither, or refused a second time this step, is skipped with a note and
// the party goes on to the next step. True: handled. False (OrderRefused
// pauses or goes on as before): a direct step, an exact point or the step
// before a direct one (the hole's crossing needs them), the guard's walk back
// to its stand point, or a refusal from the lead bot's own spot
// (LeadNoProgressRefusal: the next point would be refused the same way).
inline bool MoveRefusedUnreachable(Run& r, const std::string& order, const std::string& answer, bool found,
    float dx, float dy, float dz)
{
    const PlanStep* s = Current(r);
    if (!s || r.state != State::Running || !OwnMoveOrder(r, order) || !UnreachableAnswer(answer))
        return false;
    if (r.moveRefusedStep != r.step)
    {
        r.moveRefusedStep = r.step;
        r.moveRefusals = 0;
    }
    std::string const stepNo = std::to_string(r.step + 1);
    std::string const point = Num(s->x) + " " + Num(s->y) + " " + Num(s->z);
    bool const again = r.moveRefusals > 0;
    if (!again && found && UseDetour(r, dx, dy, dz))
    {
        ++r.moveRefusals;
        r.note = "step " + stepNo + ": cmd move to the plan's point " + point + " refused (" + answer +
            "): walking to the nearest reachable point " + Num(dx) + " " + Num(dy) + " " + Num(dz) + ", " +
            Num(ai::PlanDistance(dx, dy, dz, s->x, s->y, s->z)) + " yd from it, and going on from there";
        return true;
    }
    if (!again && RefusedFromIsland(r, order, answer))
        return true;
    std::size_t const next = r.step + 1;
    if (NeverSkipMove(r) ||
        (r.guardReturn && order == MoveOrder(r.guardX, r.guardY, r.guardZ, false)) || LeadNoProgressRefusal(answer))
        return false;
    ++r.moveRefusals;
    std::vector<std::size_t> skips = r.unreachableSkips;
    skips.push_back(r.step);
    NextStep(r, "step " + stepNo + " move to " + point + " skipped: no walkable path; the party goes on to step " +
        std::to_string(next + 1) + (again ? " (refused again after the walk to the nearest reachable point: " :
        " (no reachable point within " + std::to_string(static_cast<int>(kMoveFallbackYd)) + " yd of it: ") +
        answer + ")");
    r.unreachableSkips = skips;
    // Three in a row (no step done in between): the lead bot is likely in the
    // wrong part of the instance, and every next move would be skipped too.
    // Razorfen Kraul r24 slot 8 steps 331-343: the pause came at 334, and each resume
    // skipped three more from the same spot (pauses at 337, 340, 343; the lead bot never
    // moved). The run now goes back to the first of them, so a resume walks the lead bot
    // back to the last point it reached (the walk back's last move point before them)
    // and tries them again; the same three failing again pause with the route note, the
    // run still at the first of them: never a skip of three more.
    if (r.state == State::Running && skips.size() >= kUnreachableSkipsInARow)
    {
        std::string steps;
        for (std::size_t i = 0; i < skips.size(); ++i)
            steps += (i ? (i + 1 == skips.size() ? " and " : ", ") : "") + std::to_string(skips[i] + 1);
        std::size_t const first = skips.front();
        std::size_t const reached = LastReachedBefore(r, first);
        bool const same = r.noPathSkips == skips;
        r.noPathSkips = skips;
        std::string const lead = Num(r.leadLastX) + " " + Num(r.leadLastY) + " " + Num(r.leadLastZ);
        std::string const back = reached == static_cast<std::size_t>(-1) ? std::string("the plan's start")
                                                                         : "step " + std::to_string(reached + 1);
        r.step = first;
        r.autoRegroups = 0;
        ResetStep(r);
        ForgetMoveRefusals(r);
        if (same)
            Pause(r, "the same three moves had no walkable path again (steps " + steps + "; the lead bot at " + lead +
                "): the way on from " + back + ", the last point the lead bot reached, needs a fix in the plan (a ramp "
                "or a waypoint); the run stays at step " + std::to_string(first + 1) + ", a resume tries them again from " +
                back);
        else
            Pause(r, "three moves in a row had no walkable path: the lead bot may be in the wrong part of the instance "
                "(steps " + steps + " skipped; the lead bot at " + lead + "): the run went back to step " +
                std::to_string(first + 1) + "; a resume walks the lead bot back to " + back +
                ", the last point it reached, and tries them again");
    }
    return true;
}

// open / use_item: the bags of one bot, for PickItemCarrier.
struct BagFact
{
    std::string name;
    std::uint64_t guid = 0;          // the bot's guid (OutstandingCarrierVerdict)
    std::vector<std::uint32_t> held; // the step's input items this bot carries
    std::string away;                // why it cannot take the task ("dead", "not in the instance"); "": it can
};

// The bot an open / use_item goes to: the lead bot when it carries every input
// of the step (its item, the item to use, and the second item in `param`),
// else the first available bot that does. None does: the lead bot, and `split`
// says who holds what (an unavailable bot: item N is with X, who is dead) when
// someone holds the step's first input. Only the step's own inputs count,
// never loot a bot kept for other reasons; a dead bot or one off the lead
// bot's map is never chosen.
inline std::string PickItemCarrier(const std::vector<BagFact>& bags, const std::vector<std::uint32_t>& inputs,
    const std::string& lead, std::string* split = nullptr)
{
    auto holdsAll = [&](const BagFact& b)
    {
        for (std::uint32_t item : inputs)
            if (item && std::find(b.held.begin(), b.held.end(), item) == b.held.end())
                return false;
        return true;
    };
    std::string best;
    for (const BagFact& b : bags)
        if (b.away.empty() && holdsAll(b) && (best.empty() || b.name == lead))
            best = b.name;
    if (!best.empty())
        return best;
    if (split && !inputs.empty())
    {
        bool first = false;
        for (const BagFact& b : bags)
            first = first || std::find(b.held.begin(), b.held.end(), inputs[0]) != b.held.end();
        if (first)
            for (const BagFact& b : bags)
            {
                std::string has;
                for (std::uint32_t item : b.held)
                    has += (has.empty() ? "" : ", ") + std::to_string(item);
                if (has.empty())
                    continue;
                *split += std::string(split->empty() ? "" : "; ") +
                    (b.away.empty() ? b.name + " holds " + has
                                    : std::string("item ") + has + " is with " + b.name + ", who is " + b.away);
            }
    }
    return lead;
}

// open / use_item while its order is out (r.orderSent): the bot the order went
// to (`carrierRaw`, the glue's slot) keeps the step, so its busy state, its
// reply and the made item are read from that bot even after the use consumed
// the inputs (Uldaman: the Medallion 7740 and the Shaft 7741 used by a bot
// that is not the lead bot become the Staff 7733 in its bags; no bot carries
// the inputs any more). The carrier is never picked again while the order is
// out: when that bot left the party, is dead or off the lead bot's map, the
// order is retired (RetireOrder) before the facts are read.
enum class CarrierVerdict : std::uint8_t
{
    NoOrder, // no order out (or a new one, carrierRaw 0): PickItemCarrier picks
    Pinned,  // the order's bot is there: it is the carrier (`name`)
    Retire,  // the order's bot left, died or is off the map: RetireOrder
};

inline CarrierVerdict OutstandingCarrierVerdict(bool orderSent, std::uint64_t carrierRaw,
    const std::vector<BagFact>& bags, std::string* name = nullptr)
{
    if (!orderSent || !carrierRaw)
        return CarrierVerdict::NoOrder;
    for (const BagFact& b : bags)
        if (b.guid == carrierRaw && b.away.empty())
        {
            if (name)
                *name = b.name;
            return CarrierVerdict::Pinned;
        }
    return CarrierVerdict::Retire;
}

// An outstanding open / use_item whose bot is gone (CarrierVerdict::Retire):
// the order is dropped like a refused loot (LootRefused), with no answer read
// from it, and the step goes out again from Begin to a fresh pick, where the
// usual checks apply (a dead bot holding the inputs: the split note; the
// fallen-member rules of a step not awaiting a task).
inline void RetireOrder(Run& r)
{
    r.orderSent = false;
    EnterPhase(r, Phase::Begin);
    r.note = "the bot of the order left the party, died or is off the map: the order goes out again";
}

// A later step of the plan needs a bot to carry this item (open and use_on take `item`, use_item its `entry`, any
// of them `param`): a player's copy of it is no use to the carrier.
inline bool LaterStepNeedsItem(const Run& r, std::uint32_t item)
{
    if (!r.plan || !item)
        return false;
    for (std::size_t i = r.step + 1; i < r.plan->steps.size(); ++i)
    {
        const PlanStep& s = r.plan->steps[i];
        bool const usesItem = s.kind == PlanStepKind::Open || s.kind == PlanStepKind::UseOn;
        if ((usesItem && s.item == item) || (s.kind == PlanStepKind::UseItem && s.entry == item) ||
            ((usesItem || s.kind == PlanStepKind::UseItem) && s.param == item))
            return true;
    }
    return false;
}

// loot: the next bot to try after a refusal on the loot rules: the one whose
// round-robin turn the corpse is (f.lootTurn), then each living party bot not
// tried yet. Never a bot off the lead bot's map (MemberFact::yd 1000). "" for none.
inline std::string NextLootBot(const Run& r, const Facts& f)
{
    auto tried = [&](const std::string& name)
    {
        return std::find(r.lootTried.begin(), r.lootTried.end(), name) != r.lootTried.end();
    };
    if (!f.lootTurn.empty() && !tried(f.lootTurn))
        for (const MemberFact& m : f.members)
            if (m.name == f.lootTurn && m.yd < 1000.0f)
                return f.lootTurn;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && !tried(m.name) && m.yd < 1000.0f)
            return m.name;
    return std::string();
}

// loot: no bot is left to try (or the refusal was not about the loot rules).
inline void LootGivenUp(Run& r, const Facts& f, const PlanStep& s)
{
    std::string text = r.lootRefusal;
    if (!f.lootTurnPlayer.empty())
        text = "loot: it is " + f.lootTurnPlayer + "'s turn to loot the corpse (round robin): take " +
            (s.item ? "item " + std::to_string(s.item) : std::string("the loot")) + ", then .bot run resume (" + text + ")";
    else if (s.item && f.corpseChecked && !f.corpseHasItem)
        text = "loot: item " + std::to_string(s.item) + " not on corpse and not in any bag (" + text + ")";
    else if (r.lootRights && r.lootTried.size() > 1)
    {
        text += " (tried";
        for (const std::string& name : r.lootTried)
            text += " " + name;
        text += ")";
    }
    // A player took the item a later step needs on a bot: he hands it over.
    if (s.item && !f.itemPlayer.empty())
        text += "; " + f.itemPlayer + " carries item " + std::to_string(s.item) + ": trade it to " +
            (f.carrier.empty() ? std::string("the lead bot") : f.carrier) + ", then .bot run resume";
    Pause(r, text + MissingHint(r));
}

// loot: the order for `bot` (and the status note for it). The boss corpse remembered from his step is
// named by guid (the loot task walks to it); otherwise the plan's entry@point search.
inline std::string LootOrderText(Run& r, const PlanStep& s, const std::string& bot)
{
    std::string const kind = ai::PlanStepKindName(s.kind);
    auto const corpse = r.corpses.find(s.entry);
    std::string const target = corpse != r.corpses.end() && corpse->second.guid
        ? GuidArg(corpse->second.guid) : EntryArg(s.entry, s.x, s.y, s.z);
    if (corpse != r.corpses.end() && corpse->second.guid)
        r.note = kind + " by " + bot + ": the corpse of " + std::to_string(s.entry) + " at " +
            Num(corpse->second.x) + " " + Num(corpse->second.y) + " " + Num(corpse->second.z);
    return "loot " + target + " " + bot + (s.item ? " " + std::to_string(s.item) : "");
}

// loot, AiPlayerbot.DungeonRunLootFfa: the corpse's item is not the carrier's to take under the group's
// loot rules (another member's turn; Revelosh's Shaft of Tsol went to the warlock, 2026-10-21). Before the
// next bot is tried, the lead bot (the group's leader) sets free-for-all, the carrier loots, and the method
// goes back. Once per step; the lead bot must lead the group and the method must be one to restore.
inline bool LootFfaWanted(const Run& r, const Facts& f, const PlanStep& s)
{
    // With a real player in the party the group's loot method is the players': never switched (2026-10-23).
    return f.players.empty() && f.lootFfaOn && f.leadLeadsGroup && s.item && !r.lootFfaUsed && r.lootFfa == LootFfa::Idle &&
        f.lootMethod > RemoteCommander::kLootFfa && f.lootMethod <= RemoteCommander::kLootNeedBeforeGreed;
}

inline std::string LootRestoreOrder(const Run& r)
{
    return std::string("lootmethod ") + RemoteCommander::LootMethodName(r.lootRestore);
}

// Starts the detour: the order is `lootmethod ffa`; the method to restore is read now.
inline void LootFfaStart(Run& r, const Facts& f, std::string& order)
{
    r.lootFfaUsed = true;
    r.lootFfaLive = true;
    r.lootRestoreTries = 0;
    r.lootFfaMs = 0;
    r.lootRestore = f.lootMethod;
    r.lootFfa = LootFfa::Ordered;
    r.taskBot.clear();
    r.note = std::string("loot: another member's turn: free-for-all loot for the carrier (the method goes back to ") +
        RemoteCommander::LootMethodName(f.lootMethod) + ")";
    order = "lootmethod ffa";
}

// The detour's later ticks; true when the tick is spent (an order sent, or waiting). False: go on with the
// usual loot step (the detour ended, or never took).
inline bool LootFfaStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    switch (r.lootFfa)
    {
        case LootFfa::Ordered:
        {
            // A refusal of the order ended it already (OrderRefused). Free-for-all must have taken, or the
            // carrier is no better off: the old way (the next bot).
            if (f.lootMethod != RemoteCommander::kLootFfa)
            {
                // Not free-for-all (the order did not take, or a player set another method): nothing to put back.
                r.lootFfa = LootFfa::Idle;
                r.lootFfaLive = false;
                r.note = "loot: free-for-all did not take: looking for another bot to take it";
                return false;
            }
            if (f.carrierHasItem || f.partyHasItem)
            {
                r.lootFfa = LootFfa::Restoring;
                order = LootRestoreOrder(r);
                r.note = "loot: the item is in a bag already: the loot method goes back";
                return true;
            }
            if (f.carrierBusy)
            {
                // Bounded: the group must not stay on free-for-all for a carrier that never frees up. On the cap
                // the next running tick orders the method back (LootFfaRestoreTick) and the step goes the old way.
                if (r.lootFfaMs >= kLootFfaWaitMs)
                {
                    r.lootFfa = LootFfa::Idle;
                    r.note = "loot: " + f.carrier + " did not finish an earlier order in " +
                        std::to_string(kLootFfaWaitMs / 1000) + " s: the loot method goes back";
                    return false;
                }
                r.note = "loot: waiting for " + f.carrier + " to finish an earlier order";
                return true;
            }
            r.orderSent = true;
            EnterPhase(r, Phase::AwaitTask);
            r.lootTried.push_back(f.carrier);
            r.lootFfa = LootFfa::Looting;
            r.note = "loot by " + f.carrier + " (free-for-all)";
            order = LootOrderText(r, s, f.carrier);
            return true;
        }
        case LootFfa::Looting:
            // The task's answer (ACK or ERR) or its end, whichever comes first: the method goes back before
            // the answer is read. The task cap pauses the step in the usual code (the glue restores then).
            if (f.taskReply == 0 && f.carrierBusy)
                return false;
            r.lootFfa = LootFfa::Restoring;
            order = LootRestoreOrder(r);
            r.note = "loot: the carrier's loot is answered: the loot method goes back";
            return true;
        case LootFfa::Restoring:
            // The restore order went out last tick: the next running tick reads the method and, if it is not
            // back, orders it again (LootFfaRestoreTick).
            r.lootFfa = LootFfa::Idle;
            return false;
        case LootFfa::Idle:
            break;
    }
    return false;
}

// Every running tick, whatever the step: the group's loot method is ffa because of this run's detour and no
// detour tick is in charge (a regroup after a wipe, a lead death or a fight cap reset the step with the method
// still ffa; a restore that was refused or did not take). Orders the saved method back, one order a tick, at most
// kLootRestoreTries times; then the run pauses and a human sets it (the glue tries once more as the run pauses).
// The flag ends when the facts read a method that is not ffa. True: the tick is spent.
inline bool LootFfaRestoreTick(Run& r, const Facts& f, std::string& order)
{
    if (!r.lootFfaLive || r.lootFfa != LootFfa::Idle)
        return false;
    if (f.lootMethod != RemoteCommander::kLootFfa)
    {
        // Not ffa any more (restored, or a player's choice), or no group to restore.
        r.lootFfaLive = false;
        r.lootRestoreTries = 0;
        return false;
    }
    if (r.lootRestoreTries >= kLootRestoreTries)
    {
        Pause(r, "the loot method of the lead bot's group is still free-for-all after " +
            std::to_string(kLootRestoreTries) + " orders to put it back to " + RemoteCommander::LootMethodName(r.lootRestore) +
            " (AiPlayerbot.DungeonRunLootFfa): set it with .bot cmd lootmethod " +
            RemoteCommander::LootMethodName(r.lootRestore) + ", then .bot run resume");
        return true;
    }
    ++r.lootRestoreTries;
    order = LootRestoreOrder(r);
    r.note = "the loot method goes back to " + std::string(RemoteCommander::LootMethodName(r.lootRestore)) +
        " (try " + std::to_string(r.lootRestoreTries) + ")";
    return true;
}

// A task answer that is worth one more try: the cast was cut (an interrupted opening, the party's fight), or the
// bot or the object was busy. The fight is handled first by the run (it holds the tick), then the same order goes out.
inline bool TaskResendable(const std::string& text)
{
    auto starts = [&](const std::string& prefix) { return text.compare(0, prefix.size(), prefix) == 0; };
    return starts("in-combat:") || starts("busy:") || starts("failed: The opening cast was interrupted");
}

// The first sentence of a module answer, for a note ("2.5 s" does not end it: a full stop ends it when a space or
// the end follows).
inline std::string FirstSentence(const std::string& text)
{
    std::size_t const at = text.find(". ");
    std::string out = at == std::string::npos ? text : text.substr(0, at);
    while (!out.empty() && out.back() == '.')
        out.pop_back();
    return out;
}

inline std::string OpenProofName(const PlanStep& s)  // "object 148883 within 10 yd"
{
    std::uint32_t const proof = s.entries.empty() ? 0 : s.entries.front();
    return std::string((s.flags & ai::kPlanFlagProofCreature) ? "creature " : "object ") + std::to_string(proof) +
        " within " + std::to_string(std::lround(s.radius)) + " yd";
}
inline std::string OpenProofAnswer(const Facts& f)
{
    if (f.taskReply == 1) return "the task said done";
    if (f.taskReply == 2) return "the task said: " + FirstSentence(f.taskText);
    return "the bot is free again with no answer";
}
// open_proof after its order: the step waits for the use's task to answer (or the bot to be free again: the task
// holds the bot still while it acts, so the next step never starts under it), then the PROOF alone decides:
// up -> done; not up -> wait kOpenProofWaitMs, then pause. An in-combat / busy / interrupted answer without the
// proof goes the plain open's way (sent again, kTaskResends). False: not handled, TaskStep goes on as for an open.
inline bool OpenProofStep(Run& r, const Facts& f, const PlanStep& s)
{
    bool const up = f.proofRead && f.proofUp;
    bool const answered = f.taskReply != 0 || !f.carrierBusy;
    if (!answered)
        return false;
    if (f.taskReply == 2 && !up && TaskResendable(f.taskText) && r.taskResends < kTaskResends)
        return false;
    std::string const what = "open " + std::to_string(s.entry);
    if (up)
    {
        NextStep(r, what + " done: " + OpenProofName(s) + " is up");
        return true;
    }
    if (r.proofWaitMs < kOpenProofWaitMs)
    {
        r.proofWaitMs += kTickMs;
        r.note = what + ": the use answered (" + OpenProofAnswer(f) + "); waiting for " + OpenProofName(s);
        return true;
    }
    Pause(r, what + ": " + OpenProofName(s) + " did not show in " + std::to_string(kOpenProofWaitMs / 1000) +
        " s after the use (" + OpenProofAnswer(f) + "): the use did not take; .bot run resume uses it again, " +
        ".bot run skip goes on");
    return true;
}
// loot, open, use_item: one commander task for the carrier, then its result.
inline void TaskStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const kind = ai::PlanStepKindName(s.kind);
    if (s.kind == PlanStepKind::Loot && r.lootFfa != LootFfa::Idle && LootFfaStep(r, f, s, order))
        return;
    if (!r.orderSent)
    {
        if ((s.kind == PlanStepKind::Loot || s.kind == PlanStepKind::UseItem) && s.item && f.carrierHasItem)
        {
            NextStep(r, kind + ": " + f.carrier + " already carries item " + std::to_string(s.item));
            return;
        }
        // A loot step is done when any party bot has the item (the quest item of
        // one member is looted by that member, or already was: Uldaman 19z13,
        // Revelosh 7741), before the order and after a refusal.
        if (s.kind == PlanStepKind::Loot && s.item && f.partyHasItem)
        {
            NextStep(r, kind + ": " + (f.itemHolder.empty() ? f.carrier : f.itemHolder) + " already carries item " +
                std::to_string(s.item));
            return;
        }
        // ... or a player of the party took it (his round-robin turn, his quest item).
        if (s.kind == PlanStepKind::Loot && s.item && !f.itemPlayer.empty() && !LaterStepNeedsItem(r, s.item))
        {
            NextStep(r, kind + ": " + f.itemPlayer + " (a player) carries item " + std::to_string(s.item));
            return;
        }
        if (s.kind == PlanStepKind::Loot && !r.lootRefusal.empty())
        {
            std::string next;
            // The item gone from the corpse (and in no bag): no bot can take it.
            if (r.lootRights && !(s.item && f.corpseChecked && !f.corpseHasItem))
            {
                if (LootFfaWanted(r, f, s))
                {
                    LootFfaStart(r, f, order);
                    return;
                }
                next = NextLootBot(r, f);
            }
            if (next.empty())
            {
                LootGivenUp(r, f, s);
                return;
            }
            r.taskBot = next;
        }
        if (s.kind == PlanStepKind::Open && s.openProof && f.proofRead && f.proofUp)
        {
            // A resumed run, or a use whose answer was lost: the script's own success sign is up. Not used again
            // (a second use of a used statue is refused "already used").
            NextStep(r, "open " + std::to_string(s.entry) + ": " + OpenProofName(s) + " is up already: done before the use (not used again)");
            return;
        }
        if ((s.kind == PlanStepKind::Open || s.kind == PlanStepKind::UseItem) && !f.itemsSplit.empty())
        {
            // The items the step needs sit in different bags (a loot step passed
            // on another bot's bags): one bot must carry them all.
            Pause(r, kind + ": no bot carries every item it needs: " + f.itemsSplit);
            return;
        }
        if (s.kind == PlanStepKind::Open && s.item && !f.carrierHasItem)
        {
            if (r.missing.count(s.item))
            {
                std::size_t const here = r.step;
                NextStep(r, "open " + std::to_string(s.entry) + " left out: it needs item " + std::to_string(s.item) +
                    ", which the run did not get");
                r.skippedOpenStep = here;
                return;
            }
            std::string openAsk = "open: " + f.carrier + " does not carry item " + std::to_string(s.item) + " (the lock needs it)";
            openAsk += " Get it (or a GM: .additem " + std::to_string(s.item) + "), then .bot run resume";
            if (!f.itemPlayer.empty())
                openAsk += "; " + f.itemPlayer + " carries it: trade it to " +
                    (f.carrier.empty() ? std::string("the lead bot") : f.carrier) + ", then .bot run resume";
            Pause(r, openAsk);
            return;
        }
        if (s.kind == PlanStepKind::UseItem && !f.carrierHasEntryItem)
        {
            if (r.missing.count(s.entry))
            {
                if (s.item)
                    r.missing.insert(s.item);
                NextStep(r, "use_item " + std::to_string(s.entry) + " left out: the run did not get that item");
                return;
            }
            r.pendingMissing = s.item;
            std::string useAsk =
                "use_item: " + f.carrier + " does not carry item " + std::to_string(s.entry);
            useAsk += " Get it (or a GM: .additem " + std::to_string(s.entry) + "), then .bot run resume";
            if (!f.itemPlayer.empty())
                useAsk += "; " + f.itemPlayer + " carries it: trade it to " +
                    (f.carrier.empty() ? std::string("the lead bot") : f.carrier) + ", then .bot run resume";
            Pause(r, useAsk);
            return;
        }
        if (f.carrierBusy)
        {
            r.note = kind + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        std::string const bot = s.kind == PlanStepKind::Loot && !r.taskBot.empty() ? r.taskBot : f.carrier;
        if (s.kind == PlanStepKind::Loot)
            r.lootTried.push_back(bot);
        r.note = kind + " by " + bot;
        if (s.kind == PlanStepKind::Loot)
            order = LootOrderText(r, s, bot);
        else if (s.kind == PlanStepKind::Open)
            order = "open " + EntryArg(s.entry, s.x, s.y, s.z) + " " + f.carrier;
        else
            order = "use " + std::to_string(s.entry) + " " + f.carrier;
        return;
    }
    bool const consequence = s.kind == PlanStepKind::UseItem && s.item && !r.missing.empty();
    if (s.kind == PlanStepKind::Open && s.openProof && OpenProofStep(r, f, s)) return;
    if (f.taskReply == 2)
    {
        if (consequence)
        {
            r.missing.insert(s.item);
            NextStep(r, "use_item failed (" + f.taskText + "): an earlier item is missing; item " +
                std::to_string(s.item) + " is noted missing too");
            return;
        }
        if (GoneIsDone(&s, f.taskText))
        {
            GoneStepDone(r, s);
            return;
        }
        if (s.kind != PlanStepKind::Open && s.item)
            r.pendingMissing = s.item;
        if (s.kind == PlanStepKind::Loot && LootRefusalOpen(f.taskText))
        {
            LootRefused(r, kind + " failed: " + f.taskText);
            return;
        }
        if (TaskResendable(f.taskText) && r.taskResends < kTaskResends)
        {
            // An interrupted cast or an in-combat answer: the run leader fights first (it already does), then the
            // same order goes out again, to the same bot (taskBot and lootTried stay).
            ++r.taskResends;
            r.orderSent = false;
            EnterPhase(r, Phase::Begin);
            r.note = kind + ": " + FirstSentence(f.taskText) + ": sending it again (" + std::to_string(r.taskResends) +
                " of " + std::to_string(kTaskResends) + ")";
            return;
        }
        Pause(r, kind + " failed: " + f.taskText + MissingHint(r));
        return;
    }
    if (f.carrierBusy)
    {
        if (r.phaseMs >= kTaskCapMs)
            Pause(r, kind + ": " + f.carrier + " did not finish in " + std::to_string(kTaskCapMs / 1000) + " s");
        return;
    }
    if (s.kind != PlanStepKind::Open && s.item && !f.carrierHasItem && !(s.kind == PlanStepKind::Loot && f.partyHasItem))
    {
        if (consequence)
        {
            r.missing.insert(s.item);
            NextStep(r, "use_item did not make item " + std::to_string(s.item) +
                " (an earlier item is missing): it is noted missing too");
            return;
        }
        r.pendingMissing = s.item;
        Pause(r, kind + " done, but item " + std::to_string(s.item) + " is not in " + f.carrier +
            "'s bags (.bot run skip leaves out the steps that need it)");
        return;
    }
    NextStep(r, kind + " done");
}

// ---------------------------------------------------------------------------
// Scripted events (docs/play/dungeon-run-leader.md "Events"): a start step
// (`ritual` here; `open` and `use_item` start events too), then a `hold` that
// stands on a spot and fights what the event sends until its done condition.
// ---------------------------------------------------------------------------

// The bots of a ritual: the carrier (the lead bot) first, then the nearest
// living bots of the party, `want` in all (fewer when the party has fewer).
inline std::vector<std::string> RitualBots(const Facts& f, std::uint32_t want)
{
    std::vector<const MemberFact*> others;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && m.name != f.carrier)
            others.push_back(&m);
    std::sort(others.begin(), others.end(), [](const MemberFact* a, const MemberFact* b) { return a->yd < b->yd; });
    std::vector<std::string> names;
    if (!f.carrier.empty() && want)
        names.push_back(f.carrier);
    for (const MemberFact* m : others)
        if (names.size() < want)
            names.push_back(m->name);
    return names;
}

// ritual: `cmd ritual` for `param` bots (the commander's own order: they walk
// to the object, use it in turn and hold still until it has cast), then its
// one answer. An ERR pauses with the module's words; it never waits blind.
inline void RitualStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const what = "ritual " + std::to_string(s.entry);
    std::uint32_t const want = s.param ? s.param : ai::kPlanDefaultRitualBots;
    if (!r.orderSent)
    {
        // The event this ritual starts, when its hold reads an encounter state
        // (the glue reads that data on the ritual step too).
        const PlanStep* hold = EventHoldAhead(r);
        if (hold && (hold->flags & ai::kPlanFlagDoneData) && (hold->flags & ai::kPlanFlagEncounter) && f.eventData >= 0)
        {
            if (f.eventData == static_cast<long long>(hold->param))
            {
                NextStep(r, what + " left out: the event is done already (data " + std::to_string(hold->entry) +
                    " reads " + std::to_string(hold->param) + ")");
                return;
            }
            // Still in progress or failed (Archaedas walking home after a
            // reset): a ritual now would start a dead event. Wait for the
            // reset, bounded (the Stone Keepers stay at fail for good).
            bool const busy = f.eventData == 1 || f.eventData == static_cast<long long>(ai::kPlanEncounterFail);
            if (busy && r.eventResetWaitMs < kEventResetWaitMs)
            {
                r.eventResetWaitMs += kTickMs;
                r.note = what + ": waiting for the encounter to reset (data " + std::to_string(hold->entry) + " reads " +
                    std::to_string(f.eventData) + ")";
                return;
            }
        }
        if (f.carrierBusy)
        {
            r.note = what + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        std::vector<std::string> const bots = RitualBots(f, want);
        if (bots.size() < want)
        {
            Pause(r, what + " needs " + std::to_string(want) + " living bots; " + std::to_string(bots.size()) +
                " are up (revive the party, then .bot run resume)");
            return;
        }
        r.orderSent = true;
        r.dataAtOrder = f.eventData;
        EnterPhase(r, Phase::AwaitTask);
        order = "ritual " + EntryArg(s.entry, s.x, s.y, s.z);
        std::string list;
        for (const std::string& name : bots)
        {
            order += " " + name;
            list += (list.empty() ? "" : ", ") + name;
        }
        r.note = what + ": starting the event (" + list + ")";
        return;
    }
    if (f.taskReply == 2)
    {
        Pause(r, SpentRitual(r, "ritual ", f.taskText) ? SpentRitualNote(what) : what + " failed: " + f.taskText);
        return;
    }
    if (f.taskReply == 1)
    {
        NextStep(r, what + " done: the event started");
        return;
    }
    if (f.carrierBusy)
    {
        if (r.phaseMs >= kTaskCapMs)
            Pause(r, what + ": the bots did not finish in " + std::to_string(kTaskCapMs / 1000) + " s");
        return;
    }
    if (r.phaseMs >= kRitualNoReplyMs)
        Pause(r, what + ": the bots are free again but the ritual gave no answer (.bot run resume tries it again)");
}

// gossip: the lead bot talks to creature `entry` and picks option `param` (the
// commander's `cmd gossip <entry> <bot> <option>`: the bot walks to the nearest
// living NPC of the entry within 40 yd of itself, talks to it and picks the
// option, one answer). The lead bot walks to the step's point first when it is
// farther than kGossipWalkYd from it (as a move would: approach hops out of
// sight, a plain walk in sight), so the NPC is within the order's search range.
// ACK ends the step; an ERR pauses with the module's own words (an NPC that
// AiPlayerbot.BotGossipNpcs does not list is the order's `not-allowed`, which
// names that key; since fix/plan-gossip-allow the NPC of this step was let
// through for the run's bots, RunGossipStepOf); no answer within the step's timeout pauses too. A step
// right before a hold counts as that event's start step (EventHoldAhead).
inline void GossipStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    std::string const what = "gossip " + std::to_string(s.entry) + " option " + std::to_string(s.param);
    if (!r.orderSent)
    {
        float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z);
        bool const direct = r.phase == Phase::Walk && r.walkX == s.x && r.walkY == s.y && r.walkZ == s.z;
        bool const hop = r.phase == Phase::Walk && !direct;
        if (hop || (!direct && dist > kGossipWalkYd && !f.seen))
        {
            ApproachHop(r, f, s, dt, order, "NPC");
            return;
        }
        if (direct || dist > kGossipWalkYd)
        {
            r.note = what + ": walking to the NPC";
            if (!WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
                return;
        }
        if (f.carrierBusy)
        {
            r.note = what + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        order = "gossip " + std::to_string(s.entry) + " " + f.carrier + " " + std::to_string(s.param);
        r.note = what + " by " + f.carrier;
        return;
    }
    if (f.taskReply == 2)
    {
        if (GossipDeadIsDone(r, &s, f.taskText))
            return;
        std::string const dead = GossipNpcDeadNote(what, f.taskText);
        Pause(r, dead.empty() ? what + " failed: " + f.taskText : dead);
        return;
    }
    if (f.taskReply == 1)
    {
        NextStep(r, what + " done");
        return;
    }
    if (r.phaseMs >= s.timeoutS * 1000)
    {
        Pause(r, what + ": no answer in " + std::to_string(s.timeoutS) + " s (.bot run resume tries it again)");
        return;
    }
    r.note = what + ": waiting for the answer";
}

// use_on: the carrier (the bot of the party that carries item `item`, the lead
// bot first) uses that item ON game object `entry` at x,y,z: the commander's
// `use <item> <bot> entry:<obj>@x,y,z`, which walks the bot to the object and
// casts the item's spell at it (Zul'Farrak's gong 141832: the Mallet of
// Zul'Farrak 9240's spell 11434 opens its lock and the gong's event summons
// Gahz'rilla; a plain `open` of the gong does nothing).
//
// The use is made ONCE a run. A second use summons a second Gahz'rilla, which
// the instance kills at once (instance_zulfarrak.cpp OnCreatureCreate:
// DisappearAndDie once the encounter is in progress), leaving an invisible
// corpse at the pit that a boss row may take for his death. So:
//   - an answer that says the cast did not happen (in combat, busy, an
//     interrupted cast) is sent again once (kUseOnResends), and only when the
//     act had not started (Facts::taskActStarted);
//   - a boss row's boss already in sight (alive or dead, Facts::summonSeen)
//     ends the step without a use, before the order and after an ERR;
//   - once an ACK came, the step is latched for the run (Run::useOnRang, kept
//     through ResetStep: a wipe or the lead bot's death replays the step, never
//     the use);
//   - the ACK alone is not taken as done: the object must have reacted
//     (Facts::objectReacted) or the boss must show within kUseOnProofMs, else
//     the run pauses saying so.
// An ERR pauses with the module's text; the item in a dead or absent bot's
// bags (Facts::itemsSplit) pauses naming that bot; no carrier with the item
// pauses before any order (the item is the operator's to give: the run leader
// has no GM step).
inline void UseOnStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const what = "use_on " + std::to_string(s.entry) + " with item " + std::to_string(s.item);
    const PlanStep* next = r.plan && r.step + 1 < r.plan->steps.size() ? &r.plan->steps[r.step + 1] : nullptr;
    std::string const boss = next && next->kind == PlanStepKind::Boss ? "boss " + std::to_string(next->entry) : std::string();
    bool const seen = f.summonSeen && !boss.empty();
    if (!r.orderSent)
    {
        if (seen)
        {
            r.useOnRang.insert(r.step);
            NextStep(r, what + ": " + boss + " is here already (alive or dead): not used again");
            return;
        }
        if (r.useOnRang.count(r.step))
        {
            Pause(r, what + ": used once already this run (its order was ACKed) and " +
                (boss.empty() ? std::string("nothing shows it took") : boss + " is not in sight") +
                ": not used again (a second use summons a second boss that the instance kills at once, leaving an " +
                "invisible corpse); .bot run skip goes on");
            return;
        }
        if (!f.itemsSplit.empty())
        {
            Pause(r, what + ": " + f.itemsSplit + " (revive or bring that bot, then .bot run resume)");
            return;
        }
        if (!f.carrierHasItem)
        {
            std::string useOnAsk = what + ": no bot of the party carries item " + std::to_string(s.item) + " (" +
                f.carrier + " was checked first): give it to the lead bot (GM .additem " + std::to_string(s.item) +
                " with the bot selected), then .bot run resume";
            if (!f.itemPlayer.empty())
                useOnAsk += "; " + f.itemPlayer + " carries it: trade it to " +
                    (f.carrier.empty() ? std::string("the lead bot") : f.carrier) + ", then .bot run resume";
            Pause(r, useOnAsk);
            return;
        }
        if (f.carrierBusy)
        {
            r.note = what + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        order = "use " + std::to_string(s.item) + " " + f.carrier + " " + EntryArg(s.entry, s.x, s.y, s.z);
        r.note = what + " by " + f.carrier;
        return;
    }
    if (f.taskReply == 2)
    {
        if (seen)
        {
            r.useOnRang.insert(r.step);
            NextStep(r, what + ": " + boss + " is here (the use took before the answer " + FirstSentence(f.taskText) +
                "): not used again");
            return;
        }
        if (TaskResendable(f.taskText) && !f.taskActStarted && r.taskResends < kUseOnResends)
        {
            ++r.taskResends;
            r.orderSent = false;
            EnterPhase(r, Phase::Begin);
            r.note = what + ": " + FirstSentence(f.taskText) + ": sending it again (" + std::to_string(r.taskResends) +
                " of " + std::to_string(kUseOnResends) + ")";
            return;
        }
        Pause(r, what + " failed: " + f.taskText +
            (f.taskActStarted ? " (the cast had started: not sent again; look whether it took before .bot run resume)"
                              : std::string()));
        return;
    }
    if (f.taskReply == 1)
    {
        r.useOnRang.insert(r.step);
        if (f.objectReacted || seen)
        {
            NextStep(r, what + " done" + (f.objectReacted ? (f.taskText.empty() ? std::string() : ": " + f.taskText)
                                                          : ": " + boss + " is here"));
            return;
        }
        if (r.phase != Phase::Wait)
            EnterPhase(r, Phase::Wait);
        if (r.phaseMs >= kUseOnProofMs)
        {
            Pause(r, what + ": the order was ACKed but object " + std::to_string(s.entry) + " showed no change" +
                (boss.empty() ? std::string() : " and " + boss + " did not show") + " within " +
                std::to_string(kUseOnProofMs / 1000) + " s: the item did not take (it is not used again this run; " +
                ".bot run skip goes on)");
            return;
        }
        r.note = what + ": ACKed; waiting for the object or " + (boss.empty() ? std::string("its event") : boss) +
            " to show it took";
        return;
    }
    if (r.phaseMs >= kTaskCapMs)
    {
        // The use may have taken without an answer: latched like an ACK, never sent again this run.
        r.useOnRang.insert(r.step);
        Pause(r, what + ": no answer in " + std::to_string(kTaskCapMs / 1000) + " s: not used again this run (it may " +
            "have taken); .bot run skip goes on");
        return;
    }
    r.note = what + (f.carrierBusy ? ": " + f.carrier + " is on it" : std::string(": waiting for the answer"));
}

// The event's units now (the glue lists the living ones within the hold's
// radius, the friendly, still dormant ones too, and counts the dead).
// Whether a unit of a hold's radius counts as one of the event's own: awake
// (hostile), or of the hold's first entry (the event's named mobs: the four
// Stone Keepers, Archaedas), dormant ones too. The dormant statues of the other
// entries in the hall (Archaedas's room holds some fifty Earthen Guardians and
// Custodians, 2026-10-16 live run: "0/53 down") count only once they wake.
inline bool EventUnitCounts(const PlanStep& s, const UnitFact& u)
{
    return u.hostile || s.entries.empty() || u.entry == s.entries.front();
}

inline void CountEvent(Run& r, const Facts& f, const PlanStep& s)
{
    std::uint32_t up = 0, counted = 0;
    for (const UnitFact& u : f.units)
    {
        up += u.hostile ? 1 : 0;
        counted += EventUnitCounts(s, u) ? 1 : 0;
    }
    r.eventUp = up;
    r.eventDown = f.eventDown;
    r.eventTotal = counted + f.eventDown;
    // A hold done by its units (no done flag) starts once one of them is seen
    // awake, in a fight too: r14-19z32 Bly turned hostile and was in the fight
    // on the same tick, HoldStep (the only place that marked it) never saw him
    // awake, and the hold waited 300 s twice with the crew dead ("3 of 3
    // down"). A data or object hold keeps its own start rule (HoldStep).
    if (up && !(s.flags & (ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject)))
        r.eventStarted = true;
    // A unit alive in the radius at any tick (friendly or not) rules out the "all dead before the hold" end (HoldStep).
    if (!f.units.empty())
        r.holdUnitSeen = true;
}

// "2 of 4 down, 1 awake", or "" before any unit of the event was seen.
inline std::string EventProgress(const Run& r)
{
    if (!r.eventTotal)
        return std::string();
    return std::to_string(r.eventDown) + " of " + std::to_string(r.eventTotal) + " down" +
        (r.eventUp ? ", " + std::to_string(r.eventUp) + " awake" : std::string());
}

// The step that started the event a hold waits on: the nearest ritual, open,
// use_item or gossip before it, with only moves, rests and waits between. npos for none.
inline std::size_t EventStartStep(const Run& r)
{
    if (!r.plan)
        return static_cast<std::size_t>(-1);
    for (std::size_t i = r.step; i-- > 0;)
    {
        PlanStepKind const k = r.plan->steps[i].kind;
        if (k == PlanStepKind::Ritual || k == PlanStepKind::Open || k == PlanStepKind::UseItem ||
            k == PlanStepKind::Gossip || k == PlanStepKind::UseOn)
            return i;
        if (k != PlanStepKind::Move && k != PlanStepKind::MoveDirect && k != PlanStepKind::Rest &&
            k != PlanStepKind::WaitObject && k != PlanStepKind::WaitClear)
            break;
    }
    return static_cast<std::size_t>(-1);
}

// The event reset (or never started): the run goes back to the step that
// starts it and pauses, so `.bot run resume` regroups and starts it again.
inline void EventFailed(Run& r, const std::string& why)
{
    std::size_t const start = EventStartStep(r);
    std::string again = ": .bot run resume waits for it again, .bot run skip goes on without it";
    if (start != static_cast<std::size_t>(-1))
    {
        r.step = start;
        r.autoRegroups = 0;
        ResetStep(r);
        ForgetMoveRefusals(r);
        ForgetRewoundRoute(r);
        again = ": .bot run resume regroups and starts it again at step " + std::to_string(start + 1) + " (" +
            ai::PlanStepKindName(r.plan->steps[start].kind) + ")";
    }
    r.fighting = false;
    r.eventRetry = true;
    Pause(r, "event failed (" + why + ")" + again);
}

// A wipe regroup inside a hold whose encounter has reset: the encounter is not
// done, so waiting on the spot would wait for an event nobody restarted
// (events-5: the wipe at 01:38:23 was followed by a regroup and a walk back to
// the hold step, the hold then held 30 s for data 2 to leave 0 and paused
// "event failed (data 2 did not leave 0)" at 01:45:12 -- 6.8 min, and
// unattended that pause hits the 10 min cap and the run fails). The run goes
// back to the step that starts the event instead, once per event per run
// (Run::eventRearmed): a second wipe in the same event regroups and pauses as
// it does today. r.eventRetry is set as EventFailed does, so a `busy` altar is
// reported as spent. False: nothing re-armed.
inline bool RearmEventOnWipe(Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind != PlanStepKind::Hold)
        return false;
    if (!(s->flags & ai::kPlanFlagDoneData) || !(s->flags & ai::kPlanFlagEncounter))
        return false;
    // Only a RESET encounter: NOT_STARTED (0) or FAIL (2). IN_PROGRESS (1) is a
    // live event (a wipe with the fight still on, a boss walking home between
    // waves): dropping it and sending the ritual into a running event breaks
    // the event, so the run holds on the spot as it does today. DONE (the
    // hold's param) ends the step before this; -1 is no instance data at all.
    if (f.eventData != static_cast<long long>(ai::kPlanEncounterNotStarted) &&
        f.eventData != static_cast<long long>(ai::kPlanEncounterFail))
        return false;
    std::size_t const start = EventStartStep(r);
    if (start == static_cast<std::size_t>(-1) || r.eventRearmed.count(start))
        return false;
    r.eventRearmed.insert(start);
    r.step = start;
    ForgetMoveRefusals(r);
    ForgetRewoundRoute(r);
    r.eventRetry = true;
    return true;
}

// What a hold done by its object read this tick (the run leader reads the
// object's GO state near the hold spot: RemoteCommanderPolicy.h
// kEntryPointRadius, 30 yd). Only for a DoneObject hold; the done test itself
// is unchanged.
inline std::string HoldObjectRead(const Facts& f, const PlanStep& s)
{
    // 30: RemoteCommanderPolicy.h kEntryPointRadius (not visible here), the
    // by-entry object search radius around the hold spot.
    if (f.objectState < 0)
        return "object " + std::to_string(s.entry) + " not found within 30 yd of the spot";
    auto word = [](int state)
    {
        return state == 0 ? "ready" : state == 1 ? "active" : "active_alt";
    };
    return "object " + std::to_string(s.entry) + " reads " + std::to_string(f.objectState) + " " + word(f.objectState) +
        ", waits for " + std::to_string(s.param) + " " + word(static_cast<int>(s.param));
}
// The hold's done condition from what the glue reads on every tick, with the
// spot in sight or not, in a fight, a regroup or a pause too (instance data is
// map-wide; the object counts when it is within the lead bot's reach). Uldaman
// run 3 (19z12): Archaedas died 12 s after the lead bot; the hold then rested,
// hopped toward its spot from where the lead bot came back to life and paused
// after 8 hops, and the agent's pause cap ended the run FAILED with data 2 at 3
// (DONE) and the vault door 124369 open. "" when not done, else the note.
inline std::string HoldDoneNote(const PlanStep& s, const Facts& f)
{
    if (s.kind != PlanStepKind::Hold)
        return std::string();
    if (s.flags & ai::kPlanFlagDoneData)
    {
        if (f.eventData >= 0 && f.eventData == static_cast<long long>(s.param))
            return "event done: data " + std::to_string(s.entry) + " reads " + std::to_string(s.param);
    }
    else if ((s.flags & ai::kPlanFlagDoneObject) && f.objectState == static_cast<int>(s.param))
        return "event done: object " + std::to_string(s.entry) + " is in state " + std::to_string(s.param);
    return std::string();
}

// A paused run whose current step is a hold whose event is done (the glue
// reads HoldDoneNote's facts while paused too): the step ends, and the run
// resumes by itself with a resume's regroup (revive, gather or summon, walk
// back), so a pause never outlives the event it waited on (Uldaman run 3: the
// agent's 10-minute pause cap ended the run FAILED with Archaedas dead). A
// pause for a player's order stays a pause: the step ends, the note says the
// event is done, and `.bot run resume` goes on. True when the step ended.
inline bool HoldDoneWhilePaused(Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (r.state != State::Paused || !s)
        return false;
    if (r.doorPaused)
        return false; // the door hold ends only on the door read open (DoorOpenWhilePaused) or the skip
    std::string const done = HoldDoneNote(*s, f);
    if (done.empty())
        return false;
    bool const byPlayer = PausedByPlayer(r);
    std::string const why = r.note;
    r.state = State::Running;
    r.fighting = false;
    NextStep(r, done);
    if (r.state == State::Done)
        return true;
    if (byPlayer)
    {
        r.state = State::Paused;
        // The note keeps its "player order" head: the glue lets the held bots
        // go without forming them up over the player's order (ReleaseRunHold).
        r.note = why + "; " + done + " meanwhile: .bot run resume goes on with step " + std::to_string(r.step + 1);
        return true;
    }
    BeginRegroup(r);
    r.walkBackDue = true;
    r.note = done + " while paused (" + why + "): resumed by itself, regrouping";
    return true;
}

// The places of the party on a hold spot (kHoldBehindYd): every living ranged
// bot (a healer too) but the lead bot, in a row kHoldBehindYd behind the spot
// on the side away from the event's mobs (the middle of its named mobs, the
// hold's first entry, else of its awake ones), kHoldSpreadYd apart across that
// line, each at most kHoldGatherYd from the spot and inside the hold's radius.
// Units that ring the spot (no side from their middle): the side whose row is
// nowhere nearer to a unit than the spot is, else the tank's own spot. No
// units seen: the side the party came from (the nearest earlier move point).
// The others follow the lead bot (the tank) on the spot. Empty when no side
// can be told or no ranged bot lives.
inline std::vector<HoldStand> HoldStands(const Run& r, const Facts& f, const PlanStep& s)
{
    std::vector<HoldStand> out;
    std::vector<std::string> names;
    for (const MemberFact& m : f.members)
        if (m.ranged && !m.name.empty() && m.name != f.carrier)
            names.push_back(m.name);
    if (names.empty())
        return out;
    std::sort(names.begin(), names.end());
    // The side the event comes from.
    float bx = 0.0f, by = 0.0f;
    bool side = false;
    for (int pass = 0; pass < 2 && !side; ++pass)
    {
        float cx = 0.0f, cy = 0.0f;
        std::uint32_t n = 0;
        for (const UnitFact& u : f.units)
            if (pass == 0 ? (!s.entries.empty() && u.entry == s.entries.front()) : u.hostile)
            {
                cx += u.x;
                cy += u.y;
                ++n;
            }
        if (!n)
            continue;
        float const dx = cx / n - s.x, dy = cy / n - s.y;
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len >= kHoldMobSideYd)
        {
            bx = -dx / len;
            by = -dy / len;
            side = true;
        }
    }
    float const reach = s.radius > 0.0f ? std::min(kHoldGatherYd, s.radius) : kHoldGatherYd;
    // The row for a side: kHoldBehindYd behind the spot along (ux, uy).
    auto row = [&](float ux, float uy)
    {
        std::vector<HoldStand> stands;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            float const across = (static_cast<float>(i) - 0.5f * static_cast<float>(names.size() - 1)) * kHoldSpreadYd;
            float dx = ux * kHoldBehindYd - uy * across, dy = uy * kHoldBehindYd + ux * across;
            float const len = std::sqrt(dx * dx + dy * dy);
            if (len > reach)
            {
                dx *= reach / len;
                dy *= reach / len;
            }
            stands.push_back(HoldStand{names[i], s.x + dx, s.y + dy, s.z});
        }
        return stands;
    };
    if (side)
        return row(bx, by);
    if (!f.units.empty())
    {
        // The event's units ring the spot (the four Stone Keepers: their middle
        // 3.4 yd off it), and the script wakes each onto the NEAREST player: a
        // row toward any side the party came from may stand nearer to a statue
        // than the tank does. Of 24 sides, the one whose row keeps every stand
        // at least as far from every unit as the spot is, with the widest
        // margin; none such: the ranged stand on the tank's own spot.
        float bestMargin = -1.0f;
        for (int k = 0; k < 24; ++k)
        {
            float const a = static_cast<float>(k) * 3.14159265f / 12.0f;
            std::vector<HoldStand> stands = row(std::cos(a), std::sin(a));
            float margin = 1e9f;
            for (const HoldStand& st : stands)
                for (const UnitFact& u : f.units)
                {
                    float const fromStand = std::sqrt((st.x - u.x) * (st.x - u.x) + (st.y - u.y) * (st.y - u.y));
                    float const fromSpot = std::sqrt((s.x - u.x) * (s.x - u.x) + (s.y - u.y) * (s.y - u.y));
                    margin = std::min(margin, fromStand - fromSpot);
                }
            if (margin >= 0.0f && margin > bestMargin)
            {
                bestMargin = margin;
                out = std::move(stands);
            }
        }
        if (out.empty())
            for (const std::string& name : names)
                out.push_back(HoldStand{name, s.x, s.y, s.z});
        return out;
    }
    for (std::size_t i = r.step; r.plan && i-- > 0;)
    {
        const PlanStep& p = r.plan->steps[i];
        if (p.kind != PlanStepKind::Move && p.kind != PlanStepKind::MoveDirect)
            continue;
        float const dx = p.x - s.x, dy = p.y - s.y;
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len >= kHoldMobSideYd)
            return row(dx / len, dy / len);
    }
    return out;
}

// Whether two stand lists name the same bots (positions aside: the event's
// mobs wake and move, the places stay until the party changes).
inline bool SameStandNames(const std::vector<HoldStand>& a, const std::vector<HoldStand>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name)
            return false;
    return true;
}

// hold: walks to the spot, then stands there and lets the bots fight what the
// event sends (no pulls of its own, but for an awake unit of the event that
// does not come within kHoldIdlePullMs), walks back to the spot after a fight
// that took it more than kHoldReturnYd away, and ends on the done condition.
// A failed event goes back to its start step (EventFailed); `timeout_s` is the
// event's ceiling, fights included.
inline void HoldStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    if (!f.seen)
    {
        ApproachHop(r, f, s, dt, order, "event spot");
        return;
    }
    r.eventOn = true;
    CountEvent(r, f, s);
    std::string const progress = EventProgress(r);
    std::string const tail = progress.empty() ? std::string() : " (" + progress + ")";
    std::string const data = "data " + std::to_string(s.entry);
    if (s.flags & ai::kPlanFlagDoneData)
    {
        if (f.eventData < 0)
        {
            Pause(r, "event: the lead bot's map has no instance data to read (" + data + ")");
            return;
        }
        if (f.eventData == static_cast<long long>(s.param))
        {
            NextStep(r, "event done: " + data + " reads " + std::to_string(s.param) + tail);
            return;
        }
        if (s.flags & ai::kPlanFlagEncounter)
        {
            bool const notStarted = f.eventData == static_cast<long long>(ai::kPlanEncounterNotStarted);
            bool const failed = f.eventData == static_cast<long long>(ai::kPlanEncounterFail);
            if (!notStarted && !failed)
                r.eventStarted = true;
            if (failed)
            {
                EventFailed(r, data + " reads 2, fail: the encounter reset" + tail);
                return;
            }
            if (notStarted && r.eventStarted)
            {
                EventFailed(r, data + " is back to 0, not started: the encounter reset" + tail);
                return;
            }
            if (notStarted && r.eventMs >= kEventStartMs)
            {
                EventFailed(r, data + " did not leave 0, not started, in " + std::to_string(kEventStartMs / 1000) +
                    " s: the start did not take");
                return;
            }
        }
    }
    else if (s.flags & ai::kPlanFlagDoneObject)
    {
        if (f.objectState == static_cast<int>(s.param))
        {
            NextStep(r, "event done: object " + std::to_string(s.entry) + " is in state " + std::to_string(s.param) + tail);
            return;
        }
    }
    else
    {
        if (r.eventUp)
            r.eventStarted = true;
        if (r.eventStarted && f.units.empty())
        {
            NextStep(r, "event done: every unit of it is dead" + tail);
            return;
        }
        // Every unit lay dead before the hold began (r15-19z33: Bly's crew killed by the troll waves, "4 of 4 down" for 300 s): the
        // event can never start. Only kHoldAllDeadMs after the FIRST look at the spot (eventMs runs from that look and keeps running
        // while the spot is out of sight; this code runs only on a tick with the spot in sight), with corpses in sight (eventDown),
        // no unit ever alive in the radius this hold (holdUnitSeen) and none awake (eventStarted); units that are merely not spawned
        // or not visible yet show no corpses and keep the 300 s ceiling.
        if (!r.eventStarted && !r.holdUnitSeen && f.units.empty() && r.eventDown > 0 && r.eventMs >= kHoldAllDeadMs)
        {
            NextStep(r, "event done: none of its units is alive (they died before the hold)" + tail);
            return;
        }
    }
    if (r.eventMs >= s.timeoutS * 1000u)
    {
        std::string const read = (s.flags & ai::kPlanFlagDoneObject) ? "; " + HoldObjectRead(f, s) : std::string();
        Pause(r, "event not done after " + std::to_string(s.timeoutS) + " s" + tail + read +
            ": .bot run resume waits again, .bot run skip goes on");
        return;
    }
    // Hold the spot: walk there first, and back after a fight that took the
    // lead bot away (the bots chase in a fight; the run brings them back).
    float const fromSpot = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z);
    // A guard order took the tank off its stay in the fight (CheckGuard): it
    // goes back on the spot (the move puts it on "stay" there again), even
    // when the fight left it on the spot (a walk sends no move there), or its
    // own movement takes over between the event's units.
    if (r.holdUnstayed && fromSpot <= kArriveYd)
    {
        r.holdUnstayed = false;
        r.holdArrived = true;
        r.note = "event: the tank back on the hold spot" + tail;
        order = MoveOrder(s.x, s.y, s.z, false);
        return;
    }
    if (!r.holdArrived || fromSpot > kHoldReturnYd || r.holdUnstayed)
    {
        r.holdUnstayed = false;
        if (!WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
        {
            if (r.state == State::Running)
                r.note = (r.holdArrived ? "event: back to the hold spot" : "event: walking to the hold spot") + tail;
            return;
        }
        r.holdArrived = true;
    }
    if (r.phase != Phase::Hold)
        EnterPhase(r, Phase::Hold);
    // The party's places on the spot (HoldStands): set on arrival, again when
    // the living party changes (a revived bot).
    {
        std::vector<HoldStand> stands = HoldStands(r, f, s);
        if (!r.holdPlaced || !SameStandNames(stands, r.holdStands))
        {
            r.holdPlaced = true;
            r.holdStands = std::move(stands);
            ++r.holdStandsGen;
            r.holdFarTicks = 0;
        }
    }
    // An awake unit of the event that does not come is pulled after a while.
    const UnitFact* idle = PickPull(r, f.units, 0);
    r.eventIdleMs = idle ? r.eventIdleMs + dt : 0;
    if (idle && r.eventIdleMs >= kHoldIdlePullMs)
    {
        r.eventIdleMs = 0;
        PullUnit(r, f, *idle, order);
        return;
    }
    std::string const read = (s.flags & ai::kPlanFlagDoneObject) ? "; " + HoldObjectRead(f, s) : std::string();
    r.note = "event: holding the spot" + tail + read;
}

// Guarding the party from a stand point. The run leader's moves put the tank
// (the lead bot) on "stay" at a pullback stand or a walk-back point, and a
// staying bot does not go after a mob that hits someone else. In a fight, when
// mobs are hitting party bots within kGuardYd of the tank (in melee reach of
// them, or standing and casting: a pullback's mobs still running in do not
// count), the one nearest the tank is the one it goes to pick up
// (`attack <guid> <tank>`, which ends its stay); after the fight the run walks
// it back to the stand point. A tank that holds a mob of its own in melee
// reach picks up only a mob hitting a healer (Uldaman pull 100: a Brawler on
// the tank while a Geomancer and two Maulers killed the mage and the healer).
// A mob on a party pet (victimPet) is picked up too, but only when no mob on a
// bot qualifies (Uldaman Pause 2: a Scorpid on a pet 15 yd off ran the fight
// past the ceiling), and within the same kGuardYd bound.
// Once the guard took the tank off its stay this fight, it keeps watching:
// the next pick-up needs no stay. Never a polymorphed or evading mob. A
// player's own order to the party pauses the run, so this never overrides the
// commander. An engage on arrival (EngageTarget) also keeps it watching, and
// so does a kill_first focus while it is on (Run::focusGuid, cleared once its
// unit is dead or crowd-controlled and no other is left to focus): the focus order takes every bot,
// the tank too, off "stay" (Zul'Farrak r5-19z23 wipe 2: no pick-up in a whole
// fight while mobs killed the priest). Its pick-ups move only the tank and go
// under the same cap and spacing; the focus is re-sent only when its unit
// changes, so the two do not alternate. Null for none.
//
// Inside a scripted event only in its hold (InHoldArea), and only mobs within
// the hold's radius of its spot (HoldGuardTarget): the moves and the start
// step leading into a hold, and anything outside its radius, stay untouched.
// A unit that had kGuardOrdersPerUnit orders this fight (outside a hold), once
// the fight's budget was refilled: passed over. Before any refill the budget
// of kGuardOrdersPerFight governs alone, and one unit may take all of it (pull
// 100: one Mauler on the healer got 3 pick-ups; the cold review of 2026-10-22).
// The orders a unit had before the refill count.
inline bool GuardUnitCapped(const Run& r, std::uint64_t guid)
{
    if (r.guardRefills == 0)
        return false;
    auto const it = r.guardUnitOrders.find(guid);
    return it != r.guardUnitOrders.end() && it->second >= kGuardOrdersPerUnit;
}
// The run's crowd control on this unit is ordered and not yet landed or given
// up on (Run::ccWatch).
inline bool CcOrderOut(const Run& r, std::uint64_t guid)
{
    for (const Run::CcWatch& w : r.ccWatch)
        if (w.guid == guid)
            return true;
    return false;
}


// A caster hitting the party from its own camp (UnitFact::castingAtRange, campIdle) that the tank would have to walk to
// (kGuardNoWalkYd): the guard leaves it to the DPS (kGuardCampYd).
inline bool GuardLeavesCampAlone(const UnitFact& u)
{
    return u.castingAtRange && u.campIdle > 0 && u.distLead > kGuardNoWalkYd;
}

inline const UnitFact* GuardPick(const Facts& f, float holdRadius, const Run* capped = nullptr, const Run* ccRun = nullptr)
{
    const UnitFact* best = nullptr;
    for (const UnitFact& u : f.partyAttackers)
    {
        if (!u.hostile || !u.attackable || u.evading || u.polymorphed || !u.hitting || u.distLead > kGuardYd)
            continue;
        if (capped && GuardUnitCapped(*capped, u.guid))
            continue;
        if (ccRun && CcOrderOut(*ccRun, u.guid))
            continue;
        if (holdRadius <= 0.0f && GuardLeavesCampAlone(u))
            continue;
        if (holdRadius > 0.0f && (u.distSpot < 0.0f || u.distSpot > holdRadius))
            continue;
        if (f.leadHit && !u.victimHealer)
            continue;
        // A mob on a party pet comes after every mob on a bot, whatever the
        // distances (Uldaman Pause 2: a Scorpid on a pet 15 yd from the tank).
        if (best && best->victimPet != u.victimPet)
        {
            if (u.victimPet)
                continue;
            best = nullptr;
        }
        if (!best || u.distLead < best->distLead)
            best = &u;
    }
    return best;
}

// The current step is a hold whose event is on and the lead bot, a living
// tank, stands within its radius of the spot: the hold step itself (not the
// ritual or the moves before it). Null otherwise.
inline const PlanStep* InHoldArea(const Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind != PlanStepKind::Hold || !r.eventOn || !f.leadAlive || !f.leadIsTank || f.carrier.empty())
        return nullptr;
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s->x, s->y, s->z) > s->radius)
        return nullptr;
    return s;
}

inline const UnitFact* GuardTarget(const Run& r, const Facts& f)
{
    // Watching: fightOrders too, so a refill (guardOrders and engageOrders back
    // to 0) does not end the watch the earlier orders started.
    if (!f.leadAlive || !f.leadIsTank ||
        !(f.leadStaying || r.guardOrders > 0 || r.fightOrders > 0 || r.engageGuid != 0 || r.focusGuid != 0 ||
            r.focusUnstayed) ||
        f.carrier.empty() || InEvent(r))
        return nullptr;
    return GuardPick(f, 0.0f, &r, &r);
}

// The guard inside a hold (Uldaman Keepers, 19z12 attempt 1: the tank held
// "stay" on the hold spot at 102 270 while a Stone Keeper killed the priest
// at 119 266, 17 yd away, and the mage fled at 16 %). The same rules as
// GuardTarget (melee reach or casting, a healer first, the tank's own mob in
// melee reach), bounded to the hold: only mobs within the hold's radius of
// its spot, so the tank never leaves the hold area for a pick-up and never
// goes after a dormant statue (it hits nobody).
inline const UnitFact* HoldGuardTarget(const Run& r, const Facts& f)
{
    // Watching: on stay, or taken off it by a hold order (holdUnstayed) this
    // fight; guardOrders starts over with each death, holdUnstayed does not.
    const PlanStep* s = InHoldArea(r, f);
    if (!s || !(f.leadStaying || r.guardOrders > 0 || r.holdUnstayed))
        return nullptr;
    return GuardPick(f, s->radius, nullptr, &r);
}

// The tank on "stay" on the hold spot does not fight at all: an attack
// action is a movement action, and a staying bot's movement actions are not
// useful (19z12 attempt 1: Trian's "tank assist" was useless and "select new
// target" failed 1871 times in 100 s; it never swung at the keepers, only
// taunted). So once an awake unit of the event is in the fight (hostile, in
// combat, within the radius: the hold's units), the tank on stay is sent at
// one (`attack`, which ends its stay) and fights it where it stands; out of
// the fight the hold puts it back on the spot. The healer's attacker first,
// then one on someone else, then one on the tank itself, each the nearest.
//
// Once a hold order took it off its stay this fight (holdUnstayed), its own
// AI fights and the run only sends it at a unit nobody holds: never one
// already on the tank, nor the one it was sent at last, and while the tank
// holds a unit (one on it, or a mob hitting it in melee reach) only one that
// hits the healer. Switching costs the threat it built: with two units in the
// fight (Archaedas and a waking statue every 10 s) a "the one it is not on"
// rule sent it A, B, A every 2 s.
inline const UnitFact* HoldEngageTarget(const Run& r, const Facts& f)
{
    if (!(f.leadStaying || r.holdUnstayed) || !InHoldArea(r, f))
        return nullptr;
    auto const awake = [](const UnitFact& u)
    {
        return u.hostile && u.attackable && u.engaged && !u.evading && !u.polymorphed && u.distLead <= kGuardYd;
    };
    bool holding = f.leadHit;
    for (const UnitFact& u : f.units)
        if (awake(u) && u.victimLead)
            holding = true;
    auto const rank = [](const UnitFact& u) { return u.victimHealer ? 0 : u.victimLead ? 2 : 1; };
    const UnitFact* best = nullptr;
    for (const UnitFact& u : f.units)
    {
        if (!awake(u))
            continue;
        if (CcOrderOut(r, u.guid))
            continue;
        if (!f.leadStaying && (u.victimLead || u.guid == r.holdEngageGuid || (holding && !u.victimHealer)))
            continue;
        if (!best || rank(u) < rank(*best) || (rank(u) == rank(*best) && u.distLead < best->distLead))
            best = &u;
    }
    return best;
}

// Engage on arrival (PLAN-THREAT-265, M1a). The tank on "stay" at a stand
// fights nothing, so a mob that reaches it, or one hitting the healer, waited
// for the guard's wait rule or for nothing (RFC 19z19 step 25: idle 21 s with a
// Trogg on the mage and the rogue 2-4 yd away; Uldaman 19z19 step 85: three
// mobs on it and no target for 14 s). While the lead bot, a living tank, is on
// stay outside an event, the one to send it at at once: a healer's attacker
// (hitting, not polymorphed or evading, within kGuardYd, the nearest), else the
// nearest mob on the tank itself in melee reach (Facts::leadAttacker). A DPS's
// attacker is not engaged here: it stays with GuardTarget's wait, so a mob
// that will come to the tank does not draw it from its stand. Null for none.
inline const UnitFact* EngageTarget(const Run& r, const Facts& f)
{
    if (!f.leadAlive || !f.leadIsTank || !f.leadStaying || InEvent(r) || f.carrier.empty())
        return nullptr;
    const UnitFact* best = nullptr;
    for (const UnitFact& u : f.partyAttackers)
    {
        if (!u.victimHealer || !u.hostile || !u.attackable || u.evading || u.polymorphed || !u.hitting ||
            u.distLead > kGuardYd)
            continue;
        if (GuardUnitCapped(r, u.guid))
            continue;
        if (GuardLeavesCampAlone(u))
            continue;
        if (CcOrderOut(r, u.guid))
            continue;
        if (!best || u.distLead < best->distLead)
            best = &u;
    }
    if (best)
        return best;
    if (f.leadHasAttacker && f.leadAttacker.hostile && f.leadAttacker.attackable && !f.leadAttacker.evading &&
        !f.leadAttacker.polymorphed && !GuardUnitCapped(r, f.leadAttacker.guid) &&
        !CcOrderOut(r, f.leadAttacker.guid))
        return &f.leadAttacker;
    return nullptr;
}

// A hold's fight in which a living ranged bot (the healer too) is more than kHoldFollowRangedYd from the
// living tank on the ground, and the tank is more than kHoldReturnYd off the spot, for kHoldFollowTicks
// fight ticks in a row (Zul'Farrak r12-19z29: the tank 30-60 yd down the stairs, the four on their places
// 36-40 yd from it, out of heal range): the places are let go and the glue's release (SyncHoldStands ->
// FormUpOnLead) makes the placed bots follow the tank. A tank that goes after a keeper with the ranged
// within reach of it keeps them in place (the Uldaman Keepers and Archaedas holds). The release is one-way
// within the fight: HoldStep does not run while the fight branch does, so the places are set again only when
// the fight is over and HoldStep has the tank back on the spot (holdPlaced false). The dead lead bot is not
// followed (the rez and regroup rules apply). Only the hold's own fight: another step or a hold whose event
// is not on touches nothing. True when the places were let go.
inline bool ReleaseHoldStands(Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind != PlanStepKind::Hold || !r.eventOn || !f.leadAlive || !f.leadIsTank || r.holdStands.empty())
    {
        r.holdFarTicks = 0;
        return false;
    }
    float const fromSpot = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s->x, s->y, s->z);
    float farthest = 0.0f;
    for (const MemberFact& m : f.members)
    {
        if (!m.ranged || m.yd >= 1000.0f)
            continue;
        farthest = std::max(farthest, std::sqrt(std::max(0.0f, m.yd * m.yd - m.dz * m.dz)));
    }
    if (!(fromSpot > kHoldReturnYd) || !(farthest > kHoldFollowRangedYd))
    {
        r.holdFarTicks = 0;
        return false;
    }
    if (++r.holdFarTicks < kHoldFollowTicks)
        return false;
    r.holdFarTicks = 0;
    r.holdStands.clear();
    r.holdPlaced = false;
    ++r.holdStandsGen;
    ++r.holdReleases;
    r.note = "event: the tank is " + Num(fromSpot) + " yd off the spot, the ranged " + Num(farthest) +
        " yd behind the tank: the ranged leave their places and follow it (" + std::to_string(r.holdReleases) + ")";
    return true;
}

// The note's tail for the first order after a refill (CheckGuard), once.
inline std::string RefillNote(Run& r)
{
    if (!r.guardRefilled)
        return std::string();
    r.guardRefilled = false;
    return "; a new unit joined: the orders start over";
}

// One fight tick of the guard: mobs hitting the party for kGuardWaitMs (a
// healer: at once) send the pick-up, at most kGuardOrdersPerFight a fight and
// kGuardRepeatMs apart. In a hold the tank on stay is sent at the event's
// awake unit at once (HoldEngageTarget), and the order count starts over with
// each unit of the event that dies (a hold is one long fight: the four
// Keepers one after the other). True when `order` holds it.
inline bool CheckGuard(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    r.guardSinceOrderMs += dt;
    bool const hold = InHoldArea(r, f) != nullptr;
    if (hold && r.eventDown != r.guardEventDown)
    {
        r.guardEventDown = r.eventDown;
        r.guardOrders = 0;
    }
    if (!hold)
    {
        // A unit not seen in this fight before joins it (a summon, a mob that
        // crossed into the party) once a budget is spent: both budgets start
        // over (kGuardOrdersPerFight each), under kGuardOrdersFightCap. A unit
        // that joins while a budget is left is served by that budget. The
        // spacing (kGuardRepeatMs after the last order) is kept: fightOrders,
        // not the budgets, says whether an order went out this fight.
        bool joined = false;
        auto const see = [&r, &joined](const UnitFact& u)
        {
            if (u.hostile && r.guardSeen.insert(u.guid).second)
                joined = true;
        };
        for (const UnitFact& u : f.partyAttackers)
            see(u);
        if (f.leadHasAttacker)
            see(f.leadAttacker);
        if (joined && (r.guardOrders >= kGuardOrdersPerFight || r.engageOrders >= kGuardOrdersPerFight) &&
            r.fightOrders < kGuardOrdersFightCap)
        {
            r.guardOrders = 0;
            r.engageOrders = 0;
            r.guardRefilled = true;
            ++r.guardRefills;
        }
        // Engage on arrival: the first order of the fight (of either kind)
        // without a wait, every other one (another mob, or after a pick-up) no
        // sooner than kGuardRepeatMs after the last order of either kind, and
        // at most kGuardOrdersPerFight engage orders in all. An `attack` the bot holds
        // (stunned, feared) leaves the stay on, and the nearest mob changes:
        // without the spacing and the cap an order went out every tick. Not
        // one of the pick-ups (guardOrders).
        const UnitFact* arrival = EngageTarget(r, f);
        if (arrival && arrival->guid != r.engageGuid && r.engageOrders < kGuardOrdersPerFight &&
            r.fightOrders < kGuardOrdersFightCap &&
            (r.fightOrders == 0 || r.guardSinceOrderMs >= kGuardRepeatMs))
        {
            r.engageGuid = arrival->guid;
            ++r.engageOrders;
            ++r.fightOrders;
            ++r.guardUnitOrders[arrival->guid];
            r.guardMs = 0;
            r.guardSinceOrderMs = 0; // a pick-up follows it no sooner than kGuardRepeatMs
            if (!r.guardReturn)
            {
                // The stand it leaves, as at the first pick-up.
                r.guardReturn = true;
                r.guardX = f.leadStayKnown ? f.leadStayX : f.leadX;
                r.guardY = f.leadStayKnown ? f.leadStayY : f.leadY;
                r.guardZ = f.leadStayKnown ? f.leadStayZ : f.leadZ;
            }
            r.note = "fighting: the tank engages " + std::to_string(arrival->entry) + " on arrival (" +
                Num(arrival->distLead) + " yd)" + RefillNote(r);
            order = "attack " + std::to_string(arrival->guid) + " " + f.carrier;
            return true;
        }
    }
    const UnitFact* u = hold ? HoldGuardTarget(r, f) : GuardTarget(r, f);
    const UnitFact* engage = hold && !u ? HoldEngageTarget(r, f) : nullptr;
    if (!u && !engage)
    {
        r.guardMs = 0;
        return false;
    }
    if (u)
        r.guardMs += dt;
    std::uint32_t const wait = engage ? 0u : u->victimHealer ? kGuardHealerWaitMs : kGuardWaitMs;
    if ((u && r.guardMs < wait) || r.guardOrders >= kGuardOrdersPerFight ||
        (!hold && r.fightOrders >= kGuardOrdersFightCap) ||
        ((r.guardOrders > 0 || (!hold && r.fightOrders > 0)) && r.guardSinceOrderMs < kGuardRepeatMs))
        return false;
    r.guardMs = 0;
    r.guardSinceOrderMs = 0;
    ++r.guardOrders;
    if (!hold)
    {
        ++r.fightOrders;
        ++r.guardUnitOrders[u->guid];
    }
    if (hold)
    {
        // The hold walks it back to the spot (HoldStep), not the guard's return.
        r.holdUnstayed = true;
        if (engage)
        {
            r.holdEngageGuid = engage->guid;
            r.note = "event: the tank takes " + std::to_string(engage->entry) + " (" + Num(engage->distLead) + " yd)";
            order = "attack " + std::to_string(engage->guid) + " " + f.carrier;
            return true;
        }
    }
    else if (!r.guardReturn)
    {
        // The first pick-up: the tank's stay point (its stand), not where it
        // is now: a tank on stay may still be dragging a pullback in (Uldaman
        // run 3: 18 yd short of the stand, in the corridor). Its own spot only
        // when no stay point is known.
        r.guardReturn = true;
        r.guardX = f.leadStayKnown ? f.leadStayX : f.leadX;
        r.guardY = f.leadStayKnown ? f.leadStayY : f.leadY;
        r.guardZ = f.leadStayKnown ? f.leadStayZ : f.leadZ;
    }
    r.note = std::string(hold ? "event" : "fighting") + ": the tank picks up " + std::to_string(u->entry) + " off " +
        (u->victimHealer ? "the healer" : u->victimPet ? "a pet" : "the party") + " (" + Num(u->distLead) + " yd)" +
        RefillNote(r);
    order = "attack " + std::to_string(u->guid) + " " + f.carrier;
    return true;
}

// Whether the guard's walk back (Run::guardReturn) is still wanted once a fight
// is over (asked only out of combat, outside an event, a task and a wait).
// Frostmane Hollow r20-19z39 (2026-10-22, steps 277/278, 3077-3082 s): after
// every guarded pull the tank and the party walked 2-3 s back to the stand,
// then forward again for the next pack or boss; the owner: "why do they run
// back from the bosses?". With AiPlayerbot.DungeonRunWalkBackAfterWonFight at 0
// (the default):
//   - a pull or boss step its own end rule calls over (FightStepEnd: pack
//     cleared, boss down by a witnessed death, the rest out of reach or
//     friendly): no walk back; the step ends and the next one starts from
//     where the tank stands (its own approach, firing stand or gather places
//     the party; the anchor check still gathers back on a lost floor);
//   - a pull or boss step that goes on (FightStepEnd NotYet: units left, the
//     point out of sight, an ambush, a patroller to come, a boss whose death
//     was not seen): kept, the same stand pulls again;
//   - a move step: no walk back, its walk goes on forward to its point;
//   - any other step (it acts where the party stood: a wait_clear, a rest, an
//     object or item step): kept.
// During a wipe's walk back (Run::walkBack) the step judged is the one whose
// point is replayed (FactStep: a move point, so the replay goes on forward),
// not the step the run returns to; the facts are read for that step too.
// The key at 1 keeps the walk back after every fight, as before.
inline bool GuardWalkBackWanted(const Run& r, const Facts& f, const PlanStep& s)
{
    if (f.walkBackAfterWonFight)
        return true;
    switch (s.kind)
    {
        case PlanStepKind::Pull:
        case PlanStepKind::Boss:
            return FightStepEnd(r, f, s) == FightEnd::NotYet;
        case PlanStepKind::Move:
        case PlanStepKind::MoveDirect:
            return false;
        default:
            return true;
    }
}

// The tank's reach for a pull stops as soon as a mob has aggro. Uldaman run 4
// (2026-10-17, sentinel pull 2): the tank kept walking toward the west Relic
// Hunters to reach its pull target, 15.7 yd from the Sentinel, after the camp
// was awake, and pulled six mobs with the boss. While the lead bot is still
// reaching (Facts::leadReaching) and any mob has a party bot as its victim
// (Facts::reachAggro), the glue ends the lead bot's pull (Run::stopReachDue,
// once per pull): the tank stands where it is and the fight is the party's.
// True when it asks.
inline bool CheckReach(Run& r, const Facts& f)
{
    if (r.reachStopped || !f.leadReaching || f.reachAggro == 0 || !f.leadAlive || InEvent(r))
        return false;
    r.reachStopped = true;
    r.stopReachDue = true;
    r.note = "fighting: the pull stops, " + std::to_string(f.reachAggro) +
        (f.reachAggro == 1 ? " mob has" : " mobs have") + " aggro already";
    return true;
}

// Whether a bot is kept to heals (PlayerbotAI::RunHealOnly): only a healer
// that is in the party of a running run and is not its lead bot. The glue asks
// it of every bot of every running run once a second; whoever fails it gets
// its own AI back (a hire removed mid-run, a role change, a pause, the end).
inline bool HealerKeptToHeals(bool runRunning, bool inRunParty, bool isLead, bool isHealer)
{
    return runRunning && inRunParty && !isLead && isHealer;
}

// Whether the glue puts the lead bot back on "stay" now (RFC r3-19z20, step
// 22): an attack or pull order ends the lead's stay (RelaxTacticalMovement)
// and nothing gave it back after the fight, so while the party rested the
// lead ran the idle "formation station" walk, 16 yd into the lava basin, and
// the party followed it in. While the run rests, the lead holds where it
// stands; the next step's own order (a move puts it on "stay" at its point,
// a pull or attack ends the stay again) takes over when the rest ends. Asked
// once a tick; true only while the lead is off "stay", so it is sent once.
// A lead standing in magma (Player::IsInMagma) is not held there: it must be
// free to walk out (the movement guard lets a bot in magma leave it).
// Not while the run fights (Run::fighting) or the guard's walk back is
// pending (Run::guardReturn): the phase stayed Rest through a fight that began
// in the rest, and a tank on nobody's threat list is not in combat, so each
// guard pick-up (`attack`, which ends the stay) was undone on the next tick
// (THREAT F1: r15 Uldaman step 40, 1159-1192 s, 3 of 3 pick-ups undone, the
// hunter and the warlock dead; 9 of 27 rest-phase guard orders in r12-r17).
// A stale combat flag (Facts::staleCombat: in combat by the flag, nothing
// hostile engaged with any bot or pet) is no fight: it kept Run::fighting
// true and the phase Rest up to the fight ceiling, so the lead is held then,
// a pick-up pending or not (Codex and Opus review of the first F1 commit).
// Run::guardReturn in the Rest phase: set by a pick-up during a rest fight
// (the sentinel replay); after a fight it is not reached in Rest (the fight's
// end moves the phase to Begin and the walk back returns each tick), kept as
// belt and braces.
inline bool LeadStayDueWhileResting(Run const& r, bool leadAlive, bool leadInCombat, bool leadStaying,
    bool leadInMagma, bool combatStale)
{
    bool const actionableFight = (r.fighting || r.guardReturn) && !combatStale;
    return r.state == State::Running && r.phase == Phase::Rest && !actionableFight && leadAlive && !leadInCombat &&
        !leadStaying && !leadInMagma;
}

// A party member coming back with attackers on it. The owner's rule
// (2026-10-17): "the party members running back to party should just run past
// the mobs and try to run to the party, when its with the party and gets help
// it can stop and fight with them."
//
// Only while the run waits for its party: a regroup (revive, gather), a
// step's gather, a walk back, or a rest (THREAT F1: r15 Uldaman step 40, the
// hunter 20-36 yd out with eleven scorpids, and r15 Deadmines step 83, four
// members fighting 55-70 yd from the resting tank for 60 s, got no run back).
// Never during an event (a hold, or the steps leading into one: the Keepers,
// Archaedas); a player's order pauses the run (and ends every run back) anyway.
inline bool RunBackAllowed(const Run& r)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind == PlanStepKind::Hold || r.eventOn || EventHoldAhead(r))
        return false;
    return r.regroup || r.phase == Phase::Gather || r.walkBack || r.phase == Phase::Rest;
}

// One second of the run backs. A living bot other than the lead bot, more
// than kGatherYd and at most kRunBackMaxYd from it, with attackers, runs on to
// the party (Run::runBack: the glue keeps it a passive follower of the lead
// bot, so it does not turn on its attackers) until it is within
// kRunBackArriveYd; there the party's own group help takes its attackers on
// (SightDistance). The party sends no order meanwhile: it does not walk out to
// meet it. A bot that stops gaining ground (kStragglerGainYd in kWalkRetryMs:
// the Uldaman scorpid pit floor has no navmesh path out) or runs longer than
// kRunBackCapMs is let go; the gather's summon of a stalled straggler takes it
// from there. A bot is listed only while the lead bot is out of combat. True
// while someone runs back: the run waits (no regroup or step order), but its
// fight handling (the wipe regroup, crowd control, kill_first, the ceilings)
// still runs.
inline bool RunBack(Run& r, const Facts& f, std::uint32_t dt)
{
    bool const allowed = RunBackAllowed(r) && f.leadAlive && (r.regroup || (f.dead < kWipeDead && f.alive > 0));
    if (!allowed)
    {
        ClearRunBack(r);
        return false;
    }
    if (r.regroup && !r.reviveSent && f.dead > 0)
        return false; // the revive goes out first
    auto member = [&](const std::string& name) -> const MemberFact*
    {
        for (const MemberFact& m : f.members)
            if (m.name == name)
                return &m;
        return nullptr;
    };
    // The running ones: arrived, dead, stalled or out of time end their run.
    for (std::size_t i = 0; i < r.runBack.size();)
    {
        std::string const name = r.runBack[i];
        const MemberFact* m = member(name);
        Run::RunBackState& st = r.runBackState[name];
        st.ms += dt;
        st.stillMs += dt;
        // Held only by units the group cannot attack (Run::combatDrop): no run back, its combat is dropped instead.
        bool done = !m || m->yd <= kRunBackArriveYd || r.combatDrop.count(name);
        if (!done)
        {
            if (m->yd < st.bestYd - kStragglerGainYd)
            {
                st.bestYd = m->yd;
                st.stillMs = 0;
            }
            if (st.stillMs >= kWalkRetryMs || st.ms >= kRunBackCapMs)
            {
                r.runBackDone.insert(name);
                done = true;
            }
        }
        if (done)
        {
            r.runBackState.erase(name);
            r.runBack.erase(r.runBack.begin() + static_cast<std::ptrdiff_t>(i));
        }
        else
            ++i;
    }
    // New ones, only while the lead bot is in no fight of its own: a healer
    // standing back in the party's fight is not called in (and keeps healing).
    for (const MemberFact& m : f.members)
    {
        if (f.leadInCombat)
            break;
        if (m.name.empty() || m.name == f.carrier || !m.attackers || m.yd <= kGatherYd || m.yd > kRunBackMaxYd ||
            r.runBackDone.count(m.name) || r.combatDrop.count(m.name) ||
            std::find(r.runBack.begin(), r.runBack.end(), m.name) != r.runBack.end())
            continue;
        r.runBack.push_back(m.name);
        Run::RunBackState st;
        st.bestYd = m.yd;
        r.runBackState[m.name] = st;
        ++r.runBacks;
    }
    if (r.runBack.empty())
        return false;
    std::string note;
    for (const std::string& name : r.runBack)
    {
        const MemberFact* m = member(name);
        std::uint32_t const n = m ? m->attackers : 0;
        note += (note.empty() ? "regroup: " : "; ") + name + " running back with " + std::to_string(n) +
            (n == 1 ? " attacker" : " attackers");
    }
    r.note = note;
    return true;
}

// The out-of-combat ceiling of a step: kStepIdleCapMs, or a wait's own
// timeout plus a minute to get there when that is longer.
// The lead bot's death, seen on any tick outside a regroup (a regroup
// revives and gathers whoever died): where it fell is its last living spot.
inline void NoteLeadDeath(Run& r, const Facts& f)
{
    for (const std::string& name : f.deadNames)
        r.waitFor.insert(name);
    // The lead bot dead during a regroup comes back at the entrance after a
    // `revive party`: the regroup walks the plan's way back afterwards.
    if (!f.leadAlive && r.regroup)
        r.walkBackDue = true;
    if (!f.leadAlive)
        SuspendRoute(r); // it comes back where it fell, or at the entrance
    if (f.leadAlive && r.routeCheck)
    {
        // Alive again after a death or a pause: still on the checkpoint's
        // stretch only where it was (a resurrection on its corpse); anywhere
        // else (the entrance after a release, a player's move) it is not known.
        r.routeCheck = false;
        r.routeKnown = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.routeSpotX, r.routeSpotY, r.routeSpotZ) <=
            kRouteSpotYd;
    }
    if (f.leadAlive)
    {
        r.leadSeen = true;
        r.leadLastX = f.leadX;
        r.leadLastY = f.leadY;
        r.leadLastZ = f.leadZ;
        return;
    }
    if (r.leadDied || r.regroup || !r.leadSeen)
        return;
    r.leadDied = true;
    r.leadDeathX = r.leadLastX;
    r.leadDeathY = r.leadLastY;
    r.leadDeathZ = r.leadLastZ;
}

// The lead bot died alone in a fight (fewer than kWipeDead dead) and came
// back to life by itself more than kSeeYd from where it fell: its own release
// and corpse run put it at the instance entrance while the others won the
// fight deep inside. Uldaman 19z11 02:48: no regroup followed, the step went
// on approaching the pack from the entrance and paused after 8 hops at the
// scorpid pit's navmesh hole. Handled as the wipe recovery (regroup: gather
// on the lead bot, summon a stuck straggler at the ceiling; then the walk back
// over the plan's own points and direct steps), which exists and is proven;
// summoning the lead bot to the party would need a new summon target (`cmd
// summon` brings a bot to the lead bot, not the lead bot to a bot). Counted
// against kAutoRegroupsPerStep; past that it pauses. A lead bot raised where
// it fell (a party resurrection) needs nothing.
// The living party stays where it won the fight: only the revived few walk
// back (19z11 03:24: the three alive deep in the hall were summoned to the
// entrance by hand and the whole party walked the way again). Held: every
// living bot more than kGatherYd from the lead bot and within kSeeYd of where
// the lead bot fell.
inline void FillHold(Run& r, const Facts& f)
{
    r.hold.clear();
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && m.yd > kGatherYd &&
            ai::PlanDistance(m.x, m.y, m.z, r.leadDeathX, r.leadDeathY, r.leadDeathZ) <= kSeeYd)
            r.hold.push_back(m.name);
}

// The lead bot fell inside an event's hold (and is dead, or came back to life
// since): with the fight over, the hold cannot go on without it, so the run
// regroups (revive, gather or hold the party, walk back) instead of hopping
// toward the spot from wherever the lead bot is (Uldaman run 3, 19z12).
inline bool LeadLostInHold(const Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    return s && s->kind == PlanStepKind::Hold && r.eventOn && (!f.leadAlive || r.leadDied);
}

inline bool LeadRevivedAway(Run& r, const Facts& f)
{
    if (!r.leadDied || !f.leadAlive || f.anyInCombat)
        return false;
    bool const lostInHold = LeadLostInHold(r, f);
    r.leadDied = false;
    if (r.regroup || (InEvent(r) && !lostInHold) ||
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.leadDeathX, r.leadDeathY, r.leadDeathZ) <= kSeeYd)
        return false;
    // Up in another wing: no walk back reaches the party from there. A death: the rescue's latch is armed again (as
    // the regroup's below), and in a bot-only party the rescue teleport (OtherPartRescue), bounded by its own
    // kOtherPartRescuesPerStep (not the step's regroups: Dire Maul West r39/r40 slot 6); a bot still dead: the
    // regroup, which waits for it and then runs the rescue (the regroup's other-part site); otherwise the pause.
    if (LeadInOtherPart(r, f))
    {
        RearmRescueAfterDeath(r); // a death: the rescue may teleport once more
        OtherPartBlock const why = OtherPartRescueWhy(r, f);
        if (why == OtherPartBlock::None || why == OtherPartBlock::Dead)
        {
            ++r.autoRegroups;
            r.fighting = false;
            ResetStep(r);
            if (why == OtherPartBlock::None && OtherPartRescue(r, f))
                return true;
            BeginRegroup(r);
            r.walkBackDue = true;
            r.note = "the lead bot revived in another part of the instance: regrouping by itself, the rescue teleport "
                "after the dead (" + OtherPartDeadNames(f) + ") are up";
            return true;
        }
        PauseOtherPart(r, f, why);
        return true;
    }
    if (r.autoRegroups >= kAutoRegroupsPerStep)
    {
        RearmRescueAfterDeath(r); // a death: the resume's walk back may teleport once more
        Pause(r, "the lead bot died and came back " + Num(ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.leadDeathX,
            r.leadDeathY, r.leadDeathZ)) + " yd from where it fell (" + std::to_string(kAutoRegroupsPerStep) +
            " regroups by itself at this step did not hold): .bot run resume");
        return true;
    }
    ++r.autoRegroups;
    r.fighting = false;
    ResetStep(r);
    RearmRescueAfterDeath(r);
    BeginRegroup(r);
    r.walkBackDue = true;
    FillHold(r, f);
    r.note = "the lead bot died and came back to life away from the party: regrouping by itself (" +
        std::to_string(r.autoRegroups) + " of " + std::to_string(kAutoRegroupsPerStep) + " at this step)" +
        (r.hold.empty() ? std::string() : ", the party holds while it walks back");
    return true;
}

// The owner's wait rule: drops the fallen that are back (alive and within
// kGatherYd of the lead bot), then starts the regroup when one is not. The
// lead bot dead itself: the regroup's revive brings it back (at the entrance
// after a release) and the walk back follows; counted against
// kAutoRegroupsPerStep, past that it pauses. False when nobody is waited for.
inline bool WaitForFallen(Run& r, const Facts& f)
{
    for (auto it = r.waitFor.begin(); it != r.waitFor.end();)
    {
        bool back = false;
        for (const MemberFact& m : f.members)
            if (m.name == *it)
            {
                back = m.yd <= kGatherYd && m.dz <= kStragglerDzYd;
                break;
            }
        it = back ? r.waitFor.erase(it) : std::next(it);
    }
    if (r.waitFor.empty() && f.dead == 0 && f.leadAlive)
        return false;
    bool const lead = !f.leadAlive;
    if (lead)
    {
        if (r.autoRegroups >= kAutoRegroupsPerStep)
        {
            RearmRescueAfterDeath(r); // a death: the resume's walk back may teleport once more
            Pause(r, "the lead bot is dead: revive it, then .bot run resume (" +
                std::to_string(kAutoRegroupsPerStep) + " regroups by itself at this step did not hold)");
            return true;
        }
        ++r.autoRegroups;
    }
    std::string const note = WaitNote(r, f);
    bool const deathSpotKnown = r.leadDied;
    // The regroup's gather overrides a walk under way: the step starts over
    // from its beginning once the party is back (a fresh move, not a wait for
    // the walk's retry clock).
    EnterPhase(r, Phase::Begin);
    BeginRegroup(r);
    // A loot step's free-for-all detour waiting for its carrier (LootFfa::Ordered) ends here: this regroup does not
    // reset the step, and the running-tick restore (LootFfaRestoreTick) only runs with no detour in progress.
    // lootFfaLive stays, so the method is ordered back on the next tick.
    r.lootFfa = LootFfa::Idle;
    if (lead)
    {
        r.walkBackDue = true;
        r.holdDue = deathSpotKnown; // the hold waits for the lead bot to be up (regroup)
    }
    r.note = note + (lead ? ": the lead bot is dead, reviving and gathering before the step goes on"
                          : ": reviving and gathering before the step goes on");
    return true;
}

inline std::uint32_t StepIdleCapMs(const PlanStep& s)
{
    bool const wait = s.kind == PlanStepKind::WaitObject || s.kind == PlanStepKind::WaitClear ||
        s.kind == PlanStepKind::Hold;
    // A boss's wait for him at his point (BossAwayWaitMs) counts as a wait too: the plan's wait_s may exceed the
    // 300 s idle ceiling (Wailing Caverns' Kresh, 300 s), and the idle pause must not end the step before it.
    std::uint32_t const own = wait ? s.timeoutS * 1000 + 60000
        : s.kind == PlanStepKind::Boss ? BossAwayWaitMs(s) + 60000 : 0;
    return own > kStepIdleCapMs ? own : kStepIdleCapMs;
}

// One second of the run. Returns the order to send now (the text after
// `.bot cmd `), or "" for none.
// The held party (Run::hold): a bot drops out once dead, gone or within
// kGatherYd of the lead bot; the list ends once the walk back is over.
inline void UpdateHold(Run& r, const Facts& f)
{
    if (!r.walkBack && !r.walkBackDue && !r.regroup)
    {
        r.hold.clear();
        return;
    }
    r.hold.erase(std::remove_if(r.hold.begin(), r.hold.end(), [&](const std::string& name)
        {
            // The bot a direct step summoned (summonWait) stays held until it
            // arrives (kArriveYd), so a timed-out summon 3-20 yd off keeps it held.
            float const nearYd = name == r.summonWait ? kArriveYd : kGatherYd;
            for (const MemberFact& m : f.members)
                if (m.name == name)
                    return m.yd <= nearYd;
            return true;
        }), r.hold.end());
}

// The party is in combat (Facts::anyInCombat) and every unit engaged with it is one the group cannot attack
// (Facts::engagedUnits, UnitFact::attackable false). Gnomeregan r21-19z40e step 180: the Alarm-a-bomb 2600
// (7897) kept the party in combat, every `attack` on it was refused invalid-target, and "a fight lasted more
// than 180 s" paused the run four times on that step. Such a fight cannot be won, so it is no fight: Tick runs
// the step on a view with the combat flags cleared (no fight timer, no ceiling, the step goes on), outside boss
// steps and events (UnattackableFightLetGo), once the same units stood so for kUnattackableGraceMs. Only when the
// units were read and at least one is engaged: nothing engaged at all is the stale flag's (Facts::staleCombat),
// with its own grace and its ceiling.
inline bool UnattackableOnlyFight(const Facts& f)
{
    if (!f.anyInCombat || !f.engagedRead || f.engagedUnits.empty())
        return false;
    for (const UnitFact& u : f.engagedUnits)
        if (u.attackable)
            return false;
    return true;
}

// An entry the plan fights as a boss (a boss step's own entry) or as an event's unit (a hold step's entries),
// wherever in the plan that step stands.
inline bool PlanBossOrHoldEntry(const Run& r, std::uint32_t entry)
{
    if (!r.plan || !entry)
        return false;
    for (const PlanStep& s : r.plan->steps)
    {
        if (s.kind == PlanStepKind::Boss && s.entry == entry)
            return true;
        if (s.kind == PlanStepKind::Hold && std::find(s.entries.begin(), s.entries.end(), entry) != s.entries.end())
            return true;
    }
    return false;
}

// One of the engaged units is a boss or an event unit of the plan (PlanBossOrHoldEntry), whatever the step.
inline bool EngagedPlanBossOrHold(const Run& r, const Facts& f)
{
    for (const UnitFact& u : f.engagedUnits)
        if (PlanBossOrHoldEntry(r, u.entry))
            return true;
    return false;
}

// UnattackableOnlyFight on a step where it is let go: not a boss step and not inside a scripted event (a hold,
// or the steps leading into one), where a boss or an event unit may stand unattackable for a while in its own
// fight (a phase, a submerge) and the fight must keep its handling, its timer and its ceiling. Nor while any
// engaged unit is a boss or an event unit of the plan, whatever the step (EngagedPlanBossOrHold): Scarlet
// Cathedral r23 slot 5 fought Mograine on pull step 66; his fake death and a charmed judge read the fight as
// unattackable only, the grace let it go, and the regroup released the dead into his and Whitemane's fight.
inline bool UnattackableLetGoStep(const Run& r)
{
    const PlanStep* const s = Current(r);
    return s && s->kind != PlanStepKind::Boss && !InEvent(r);
}

inline bool UnattackableFightLetGo(const Run& r, const Facts& f)
{
    return UnattackableLetGoStep(r) && UnattackableOnlyFight(f) && !EngagedPlanBossOrHold(r, f);
}

// One snapshot proves nothing: a live mob can be unattackable for a moment (a burrow, a phase, a scripted
// transition, spawning in) and come back. The same engaged set (by guid) must stand unattackable-only on a step
// that lets it go (UnattackableFightLetGo) for this long without a break before the fight counts as no fight.
constexpr std::uint32_t kUnattackableGraceMs = 10000;

// One tick of that grace (Codex adversarial review of b981daeb): any engaged unit attackable, a step that does
// not let it go, or a changed set starts it over at once. True once the same set has stood kUnattackableGraceMs.
inline bool UnattackableGraceTick(Run& r, const Facts& f, std::uint32_t dt)
{
    if (!UnattackableFightLetGo(r, f))
    {
        r.unattackableSet.clear();
        r.unattackableMs = 0;
        return false;
    }
    std::vector<std::uint64_t> set;
    for (const UnitFact& u : f.engagedUnits)
        set.push_back(u.guid);
    std::sort(set.begin(), set.end());
    if (set != r.unattackableSet)
    {
        r.unattackableSet = std::move(set);
        r.unattackableMs = dt;
    }
    else
        r.unattackableMs += dt;
    return r.unattackableMs >= kUnattackableGraceMs;
}

// The distance the glue gives a bot (MemberFact::yd) or a unit (UnitFact::distLead) off the lead bot's map.
constexpr float kOtherMapYd = 1000.0f;

// Dire Maul West r38 slot B step 29 (image 19z54e, 2026-10-24): the hunter Pozzle stood outside the instance after a
// rescue (Feralas, map 1) in a fight with a Gordok Mauler 11442 there. The lead bot in the instance judged it "cannot
// be attacked" (WorldObject::IsValidAttackTarget refuses a unit on another map) at 1000 yd; the unattackable grace and
// the combat drop let it go every 10 s and it took him again a tick later, so his fight kept the party "in combat":
// the regroup's revive clock ran on one tick of 11, the regroup's fights ran on one clock past the 180 s ceiling and
// paused the run ("kept on by entry 11442 ... cannot be attacked"), and with the party dead the wipe's regroup waited
// 130 s on "fighting on". A party bot off the plan's map (MemberFact::offPlanMap) is no part of the party's fight: when
// only such bots fight (not the lead bot, no other bot, no living real player), the view has no combat; when the party
// fights in the instance too, the units off the lead bot's map (kOtherMapYd) leave the engaged units (only with the
// lead bot alive: a dead lead bot's ghost may be on another map). True with `out` set when the view differs from `f`.
inline bool PlanMapFightView(const Facts& f, Facts& out)
{
    if (!f.anyInCombat)
        return false;
    bool offMapFight = false;
    bool onMapFight = f.leadInCombat;
    for (const MemberFact& m : f.members)
        if (m.inCombat)
        {
            if (m.offPlanMap)
                offMapFight = true;
            else
                onMapFight = true;
        }
    for (const PlayerFact& p : f.players)
        if (p.alive && p.inCombat)
            onMapFight = true;
    if (!offMapFight)
        return false;
    if (!onMapFight)
    {
        out = f;
        out.anyInCombat = false;
        out.staleCombat = false;
        return true;
    }
    if (!f.leadAlive || !f.engagedRead)
        return false;
    out = f;
    out.engagedUnits.clear();
    for (const UnitFact& u : f.engagedUnits)
        if (u.distLead < kOtherMapYd)
            out.engagedUnits.push_back(u);
    if (out.engagedUnits.size() == f.engagedUnits.size())
        return false;
    out.staleCombat = out.engagedUnits.empty();
    return true;
}

// The paused ceiling's self-resume reads only the engaged units (the glue's FightOn facts carry no members): with the
// lead bot on the plan's map (the glue reads it only then), units all off its map are the fight of a bot outside the
// instance (PlanMapFightView), not the party's.
inline bool OffMapUnitsOnly(const Facts& f)
{
    if (!f.engagedRead || f.engagedUnits.empty())
        return false;
    for (const UnitFact& u : f.engagedUnits)
        if (u.distLead < kOtherMapYd)
            return false;
    return true;
}
// A bot-only run brings a party bot outside the instance back to the lead bot (lead decision 2026-10-24, Dire Maul
// West r38 slot B: the hunter stood outside 20 min and the summon, refused for a bot in combat and cancelled by any
// combat, could not bring him): `return <name> <map>`, the glue's RunOffMapReturn drops its combat and teleports it.
// True when it set the order or paused (the caller sends the order). Only with `order` empty.
inline bool OffMapReturnTick(Run& r, const Facts& f, std::string& order)
{
    // Round 3 (2026-10-25): the clearing runs before every early return: a reported bot that comes back on a
    // guarded tick (an order already set, a player joined, the lead dead, a strand rescue active) still drops
    // its entry, so leaving again at the same step reports it again; the guards only gate new notes and orders.
    if (r.offMapReturnStep != r.step)
    {
        r.offMapReturnStep = r.step;
        r.offMapReturnTries.clear();
    }
    if (r.offMapNotedStep != r.step)
    {
        r.offMapNotedStep = r.step;
        r.offMapNoted.clear();
    }
    for (auto it = r.offMapNoted.begin(); it != r.offMapNoted.end();)
    {
        bool outside = false;
        for (const MemberFact& m : f.members)
            if (m.name == *it && m.offPlanMap)
            {
                outside = true;
                break;
            }
        if (outside)
            ++it;
        else
            it = r.offMapNoted.erase(it);
    }
    if (!order.empty() || r.state != State::Running || !r.plan || !f.walkBackRescue || !f.players.empty() ||
        !f.leadAlive || r.strandDue || r.strandOn)
        return false;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || !m.offPlanMap || m.name == f.carrier || r.reviveSummons.count(m.name))
            continue;
        if (!m.offMapReturnable)
        {
            // Round 2 (2026-10-24): one note per bot per step: the first sight writes it, later ticks leave it
            // alone; an earlier copy of the same note is replaced, never appended to (Codex medium: it grew on
            // every tick). Skipped bots (the carrier, a revive summon) never join the set here: coming back still
            // reports them.
            if (!r.offMapNoted.insert(m.name).second)
                continue;
            std::string const piece = m.name +
                (m.offMapBlock == MemberFact::OffMapBlock::Busy
                    ? " is outside the instance and busy (summon or teleport pending): left alone"
                    : " is outside the instance but cannot be returned (not in the open world): left alone");
            std::string::size_type const at = r.note.find(piece);
            if (at != std::string::npos)
                r.note.replace(at, piece.size(), piece);
            else
                r.note += (r.note.empty() ? "" : "; ") + piece;
            continue;
        }
        auto const last = r.offMapReturnAt.find(m.name);
        if (last != r.offMapReturnAt.end() && r.runMs < last->second + kOffMapReturnRepeatMs)
            continue;
        std::uint32_t tries = 0;
        auto const counted = r.offMapReturnTries.find(m.name);
        if (counted != r.offMapReturnTries.end())
            tries = counted->second;
        if (tries >= kOffMapReturnTries)
        {
            Pause(r, m.name + " is outside the instance (not on map " + std::to_string(r.plan->map) + ") after " +
                std::to_string(kOffMapReturnTries) + " returns at this step: bring it to the lead bot, then .bot run resume");
            return true;
        }
        r.offMapReturnTries[m.name] = tries + 1;
        r.offMapReturnAt[m.name] = r.runMs;
        order = "return " + m.name + " " + std::to_string(r.plan->map);
        r.note += (r.note.empty() ? "" : "; ") + m.name + " is outside the instance: teleported back to the lead bot " +
            "(try " + std::to_string(tries + 1) + " of " + std::to_string(kOffMapReturnTries) + " at this step)";
        return true;
    }
    return false;
}

// The party is in a fight the run counts as one: in combat, and not one the grace above has let go (it ticks
// the grace). The glue's self-resume of a ceiling pause asks this (CeilingFightOverWhilePaused). Units all off the lead bot's map are quiet (OffMapUnitsOnly).
inline bool FightOn(Run& r, const Facts& f, std::uint32_t dt)
{
    if (f.anyInCombat && OffMapUnitsOnly(f))
    {
        r.unattackableSet.clear();
        r.unattackableMs = 0;
        return false;
    }
    return f.anyInCombat && !UnattackableGraceTick(r, f, dt);
}

// A bot of the party (not the commander: the glue lists the bound bots only) held in combat by units the group
// cannot attack only: every unit engaged with it or its pet is one (MemberFact::engaged).
inline bool HeldByUnattackable(const MemberFact& m)
{
    if (!m.engagedRead || m.engaged.empty())
        return false;
    for (const UnitFact& u : m.engaged)
        if (u.attackable)
            return false;
    return true;
}

// Dire Maul West r36 slot D step 421 (wipe 4, 2026-10-24): the party came back at the door at 47.5 -153.7 -2.7, 350
// yd from every point of the plan, and went into combat at once: six Residual Monstrosities (11484, attackable, 100 %)
// chased it from the wipe and stood at 47 151.7, 305 yd away, for over 3 min. Nothing could reach either side, and
// the fight ceiling paused the run twice; `.combatstop` on each bot by hand ended it. With the lead bot that far from
// the plan (LeadFarFromPlan: no fight of the plan is there), a bot near it (kGatherYd) held in combat only by units
// farther than kFarHoldYd from the lead bot (HeldByFarUnitsOnly) for kFarHoldGraceMs drops it as one held by units
// the group cannot attack does (CombatDropTick, the glue's DropUnattackableCombat).
constexpr float kFarHoldYd = 100.0f;
constexpr std::uint32_t kFarHoldGraceMs = 15000;

inline bool LeadFarFromPlan(const Run& r, const Facts& f)
{
    return f.leadAlive && PlanNearestYd(r, f.leadX, f.leadY, f.leadZ) > kOtherPartYd;
}

inline bool HeldByFarUnitsOnly(const MemberFact& m)
{
    if (!m.engagedRead || m.engaged.empty() || m.yd > kGatherYd)
        return false;
    for (const UnitFact& u : m.engaged)
        if (u.distLead <= kFarHoldYd)
            return false;
    return true;
}

// How long apart two combat drops of one bot are at least (a re-aggro starts the grace over anyway).
constexpr std::uint32_t kCombatDropRepeatMs = 10000;

// Gnomeregan r23 step 181 (2026-10-23): the Alarm-a-bomb 2600 (7897), which the group cannot attack, took the
// priest as its victim. He and the hunter stood in combat 35 yd from the lead bot for 8 min: a bot in combat
// with nothing it can target only shields and fades, and neither `follow` nor the run back moved it; the gather
// paused the run four times. Once a bot's engaged set (MemberFact::engaged, its pet's too) has been the same
// units, all of them ones the group cannot attack, for kUnattackableGraceMs of ticks in a row, on a step that
// lets such units go (not a boss step, not in an event), the run drops its combat: the bot's name goes into
// Run::combatDrops for the glue (CombatStop on the bot and its pet, off the units' threat lists), at most once
// per kCombatDropRepeatMs per bot. An attackable unit, a changed set or another step kind starts it over, and so
// does a change of kind: one unit first UNATTACKABLE then attackable but far away (a phase ending while the unit
// stands far off) stands the full kFarHoldGraceMs before the drop, not the far grace minus the unattackable time.
// Returns the note for the drops of this tick ("Varcus dropped combat with entry 7897 (cannot be attacked)"; a bot outside the instance: "(outside the instance)").
// The same for a bot held by far units only (HeldByFarUnitsOnly) while the lead bot is far from the plan
// (LeadFarFromPlan), on any step, after kFarHoldGraceMs ("Unjin dropped combat with entry 11484 (305.2 yd away)").
// Neither kind drops while a held-on unit is a boss or an event unit of the plan (the planUnit guard below, the
// same guard as UnattackableFightLetGo): a boss or scripted hold unit >100 yd away is fought where it stands.
inline std::string CombatDropTick(Run& r, const Facts& f, std::uint32_t dt)
{
    r.combatDrops.clear();
    bool const letGo = r.state == State::Running && UnattackableLetGoStep(r);
    bool const farOff = r.state == State::Running && LeadFarFromPlan(r, f);
    if (!letGo && !farOff)
    {
        r.combatDrop.clear();
        return std::string();
    }
    std::string note;
    std::unordered_map<std::string, Run::CombatDropState> next;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty())
            continue;
        // Never while a held-on unit is a boss or an event unit of the plan, whatever the step (the same guard as
        // UnattackableFightLetGo: Scarlet Cathedral r23 slot 5, Mograine's fake death).
        bool planUnit = false;
        for (const UnitFact& u : m.engaged)
            if (PlanBossOrHoldEntry(r, u.entry))
                planUnit = true;
        bool const unattackable = letGo && !planUnit && HeldByUnattackable(m);
        bool const farHeld = !unattackable && !planUnit && farOff && HeldByFarUnitsOnly(m);
        if (!unattackable && !farHeld)
            continue;
        std::vector<std::uint64_t> set;
        for (const UnitFact& u : m.engaged)
            set.push_back(u.guid);
        std::sort(set.begin(), set.end());
        Run::CombatDropState st;
        Run::CombatDropKind const kind = farHeld ? Run::CombatDropKind::FarHeld : Run::CombatDropKind::Unattackable;
        auto const it = r.combatDrop.find(m.name);
        if (it != r.combatDrop.end() && it->second.set == set && it->second.kind == kind)
            st = it->second;
        else
        {
            st.set = std::move(set);
            st.kind = kind;
        }
        st.ms += dt;
        auto const last = r.combatDroppedAt.find(m.name);
        bool const spaced = last == r.combatDroppedAt.end() || r.runMs >= last->second + kCombatDropRepeatMs;
        if (st.ms >= (farHeld ? kFarHoldGraceMs : kUnattackableGraceMs) && spaced)
        {
            r.combatDrops.push_back(m.name);
            r.combatDroppedAt[m.name] = r.runMs;
            std::string units;
            for (const UnitFact& u : m.engaged)
                units += (units.empty() ? "entry " : ", entry ") + std::to_string(u.entry) +
                    (farHeld ? " (" + Num(u.distLead) + " yd away)"
                         : std::string(m.offPlanMap ? " (outside the instance)" : " (cannot be attacked)"));
            note += (note.empty() ? "" : "; ") + m.name + " dropped combat with " + units;
        }
        next[m.name] = std::move(st);
    }
    r.combatDrop = std::move(next);
    return note;
}

// A creature guid as the traces print it (0xF130001ED90075B6).
inline std::string GuidHex(std::uint64_t guid)
{
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%016llX", static_cast<unsigned long long>(guid));
    return buf;
}

// One fight tick of the watch on units that cannot die now (Scarlet Cathedral r23 slot 5: Whitemane sat at 1 hp
// of 5284 under her script's invincibility, her revive of a dead Mograine never came, and the fight ceiling's
// pause was resumed four times into the same fight). Only a unit at exactly 1 hp, or at 1 % of its most or less,
// counts (HpHeldLow): a target with more is never held, whatever its shields, heals or armour do (a Codex review:
// a victim is no proof of damage landing). Each such engaged unit the group can attack, not evading: while a party
// bot or pet has it as its victim (UnitFact::partyHits) and its hp does not change, its time runs; any change of
// its hp or a tick with no party member on it starts it over, and a unit out of that band or no longer engaged is
// forgotten. At kHpHeldMs the run
// pauses with its own note. A resume is allowed (the step's stop rule bounds the repeats). True when it paused.
inline bool HpHeldLow(const UnitFact& u)
{
    return u.health == 1 || (u.health && u.maxHealth && static_cast<std::uint64_t>(u.health) * 100u <= u.maxHealth);
}

inline bool HpHeldTick(Run& r, const Facts& f, std::uint32_t dt)
{
    if (!f.anyInCombat || !f.engagedRead)
    {
        r.hpWatch.clear();
        return false;
    }
    std::unordered_map<std::uint64_t, Run::HpWatch> next;
    for (const UnitFact& u : f.engagedUnits)
    {
        if (!HpHeldLow(u) || !u.attackable || u.evading)
            continue;
        auto const had = r.hpWatch.find(u.guid);
        Run::HpWatch w;
        w.entry = u.entry;
        w.hp = u.health;
        // The 60 s are continuous targeting at the same hp: a tick with no party member on it starts them over
        // (Codex review round 2).
        if (had != r.hpWatch.end() && had->second.hp == u.health && u.partyHits)
            w.ms = had->second.ms + dt;
        next[u.guid] = w;
    }
    r.hpWatch = std::move(next);
    for (const auto& [guid, w] : r.hpWatch)
    {
        if (w.ms < kHpHeldMs)
            continue;
        std::string const at = w.hp == 1 ? std::string("1 hp") : std::to_string(w.hp) + " hp (1 % or less)";
        Pause(r, "entry " + std::to_string(w.entry) + " stays at " + at +
            ": it cannot die now (an event or script holds it); unit " + GuidHex(guid) +
            ": .bot run skip goes on without this step, .bot run stop ends the run (a resume fights it again)");
        return true;
    }
    return false;
}

// How many engaged units a note names; the rest are counted.
constexpr std::size_t kEngagedNoteMax = 6;

// The units that keep the party in combat (Facts::engagedUnits), for a note: "entry 7897 (0xF130001ED90075B6)
// cannot be attacked, entry 6206 (0x...) attackable". Read and none engaged: "nothing hostile engaged". Not
// read: empty.
inline std::string EngagedUnitsNote(const Facts& f, bool withHp = false)
{
    if (!f.engagedRead)
        return std::string();
    if (f.engagedUnits.empty() && !f.charmedFighting.empty())
    {
        std::string names;
        for (const std::string& name : f.charmedFighting)
            names += (names.empty() ? "" : ", ") + name;
        return "nothing hostile engaged but a charmed party member fighting the party (" + names + ")";
    }
    if (f.engagedUnits.empty())
        return "nothing hostile engaged";
    std::string note;
    std::size_t n = 0;
    for (const UnitFact& u : f.engagedUnits)
    {
        if (n == kEngagedNoteMax)
        {
            note += ", " + std::to_string(f.engagedUnits.size() - n) + " more";
            break;
        }
        note += (n ? ", entry " : "entry ") + std::to_string(u.entry) + " (" + GuidHex(u.guid) + ") " +
            (u.attackable ? "attackable" : "cannot be attacked");
        // withHp (the progress-aware ceiling's pauses): its hp in whole percent of its most, when read.
        if (withHp && u.health && u.maxHealth)
            note += " at " + std::to_string(static_cast<std::uint64_t>(u.health) * 100u / u.maxHealth) + " %";
        ++n;
    }
    return note;
}

// What the fight ceiling's hp watch (FightProgressTick) saw over its last kFightProgressWindowMs.
enum class FightProgress
{
    Unread,  // no engaged unit with its hp read: the ceiling pauses as before
    Young,   // hp read, no loss yet, and the watch is younger than its window: the ceiling waits
    Losing,  // an engaged unit the party can hit set a new low in the window: the ceiling waits
    Stalled, // the watch has a full window and no engaged unit lost that much in it: the ceiling pauses
    Dying,   // a party member died in the window: the ceiling pauses
};

// One fight tick of the hp watch behind the progress-aware fight ceiling (kFightProgressWindowMs; Dragonmaw Retreat
// r25 slot 7: Searistrasz 62072 lost hp from 100 % to 7 % over about 20 min while the 180 s ceiling paused four
// times). Each engaged unit with its hp read keeps a low mark: its hp when first seen, then each new low that stands
// kFightProgressPermille or more under the mark before. A new low counts (HpLow::fell, at) only when the unit is one
// the party can hit (attackable, or a party member's victim) when it is seen: loss taken while it could not be hit
// never counts later (Codex review of b466e9ad), though the mark still moves down. A unit's death counts as a new
// low on the tick it shows: its hp read at 0, or a unit that was hittable on its last read leaving the read (the
// glue lists living units only; Opus re-review of b466e9ad: a flat boss with an add killed every 30 s paused at
// 181 s on the tick an add died). The fight is losing hp while any mark of the fight, read this tick or not, set a
// new low within the window. Net loss only, per unit: hp that swings (a healer healing two adds in turn, 60 -> 50
// -> 60 % out of phase) sets no new low after its first trough (Opus cold review of fa482263), and a fresh add at
// full hp hides no other unit's loss. A unit keeps its mark for the whole fight, also when it comes back to the
// read. A rise of the party's dead count (not the count the watch began with) is a member lost in the fight.
inline FightProgress FightProgressTick(Run& r, const Facts& f, std::uint32_t dt)
{
    bool const fresh = r.fightHpMs == 0;
    r.fightHpMs += dt;
    std::uint32_t const now = r.fightHpMs;
    if (!fresh && f.dead > r.fightHpDead)
    {
        r.fightHpDeath = true;
        r.fightHpDeathAt = now;
    }
    r.fightHpDead = f.dead;
    auto const newLow = [now](Run::HpLow& mark)
    {
        mark.at = now;
        mark.fell = true;
    };
    bool read = false;
    std::unordered_set<std::uint64_t> seen;
    if (f.engagedRead)
        for (const UnitFact& u : f.engagedUnits)
        {
            if (!u.maxHealth)
                continue;
            read = true;
            seen.insert(u.guid);
            bool const hittable = u.attackable || u.partyHits;
            std::uint32_t const pm =
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(u.health) * 1000u / u.maxHealth);
            auto const had = r.fightHp.find(u.guid);
            if (had == r.fightHp.end())
            {
                Run::HpLow mark;
                mark.low = pm;
                mark.hittable = hittable;
                mark.inRead = true;
                if (!u.health && hittable)
                    newLow(mark); // seen first at 0 hp: a death in this fight
                r.fightHp[u.guid] = mark;
                continue;
            }
            Run::HpLow& mark = had->second;
            if (pm + kFightProgressPermille <= mark.low || (!u.health && mark.low))
            {
                mark.low = pm;
                if (hittable)
                    newLow(mark);
            }
            mark.hittable = hittable;
            mark.inRead = true;
        }
    // A unit hittable on its last read that left the read: dead (or gone from the fight) on this tick.
    bool losing = false;
    for (auto& [guid, mark] : r.fightHp)
    {
        if (mark.inRead && !seen.count(guid))
        {
            mark.inRead = false;
            if (mark.hittable && f.engagedRead)
                newLow(mark);
        }
        if (mark.fell && now - mark.at <= kFightProgressWindowMs)
            losing = true;
    }
    if (!read)
        return FightProgress::Unread;
    if (r.fightHpDeath && now - r.fightHpDeathAt < kFightProgressWindowMs)
        return FightProgress::Dying;
    if (losing)
        return FightProgress::Losing;
    return now >= kFightProgressWindowMs ? FightProgress::Stalled : FightProgress::Young;
}

// One of the engaged units (read, and one the group can attack) is a boss step's entry, whatever the step: the fight
// gets the boss ceiling
// (kBossFightCapMs). Dragonmaw Retreat r25 slot 7 fought Searistrasz 62072 on step 260, a step before his own.
inline bool EngagedPlanBoss(const Run& r, const Facts& f)
{
    if (!r.plan || !f.engagedRead)
        return false;
    for (const UnitFact& u : f.engagedUnits)
        for (const PlanStep& s : r.plan->steps)
            if (u.attackable && s.kind == PlanStepKind::Boss && s.entry && s.entry == u.entry)
                return true;
    return false;
}

// The facts without the held bots: gathers, the gather and regroup ceilings
// and their summons look at the bots walking back only. A held bot a direct
// step summoned (Run::summonWait) is shown, so the summon's wait sees it
// arrive (uldaman-events-4, 14:03 and 14:39: hidden, the wait ended at once,
// the step went out before the 3 s summon landed and the walk back paused at
// the scorpid pit); it stays in the hold until it stands at the lead bot.
inline Facts HoldView(const Run& r, const Facts& f)
{
    Facts g = f;
    g.members.clear();
    g.maxMemberYd = 0.0f;
    for (const MemberFact& m : f.members)
    {
        if (m.name != r.summonWait && std::find(r.hold.begin(), r.hold.end(), m.name) != r.hold.end())
            continue;
        g.members.push_back(m);
        g.maxMemberYd = std::max(g.maxMemberYd, m.yd);
    }
    return g;
}

// The party's dead: the bots and the real players (2026-10-23). A wipe is every member dead, the players too.
inline std::uint32_t PartyDead(const Facts& f)
{
    std::uint32_t dead = f.dead;
    for (const PlayerFact& p : f.players)
        dead += p.alive ? 0u : 1u;
    return dead;
}

inline bool PartyAllDead(const Facts& f)
{
    if (f.total == 0 || f.alive > 0)
        return false;
    for (const PlayerFact& p : f.players)
        if (p.alive)
            return false;
    return true;
}

// pip S5 (2026-10-26): every bot dead and a GameMaster commander still fighting at the hp clamp (his hp stays at 1,
// mobs pinned on him): his fight never ended, the wipe's regroup never revived the bots and the run noted
// "3 dead: fighting on, a regroup follows" until its regroups ran out. With every bot dead and only GameMasters
// fighting, their fight is not the party's: the regroup revives the bots (`revive party`). A real player (not a
// GameMaster) in a fight still holds it, as does any bot alive.
inline bool OnlyGmFightOverDeadBots(const Facts& f)
{
    if (f.total == 0 || f.alive > 0)
        return false;
    bool gmFight = false;
    for (const PlayerFact& p : f.players)
        if (p.alive && p.inCombat)
        {
            if (!p.gm)
                return false;
            gmFight = true;
        }
    return gmFight;
}

// The players as party members in the facts the run judges (the glue calls it after reading them): a living player's
// health counts for the rest gate (his mana does not: a player drinks on his own), and a living player in a fight
// puts the party in it (no walk, gather, regroup or revive while he fights; review round 1), except a GameMaster's
// fight once every bot is dead (OnlyGmFightOverDeadBots).
inline void ApplyPlayerFacts(Facts& f)
{
    for (const PlayerFact& p : f.players)
        if (p.alive)
        {
            f.minHealthPct = std::min(f.minHealthPct, p.healthPct);
            if (p.inCombat)
                f.anyInCombat = true;
        }
    if (OnlyGmFightOverDeadBots(f))
    {
        f.anyInCombat = false; // the glue set it from the players' fight too (ReadRunPlayers)
        f.staleCombat = false;
    }
}

// Why the run holds the step for a real player, or "" (2026-10-23). Out of combat, outside an event, before an order
// of the step goes out (Begin, Gather, Walk; never mid-pull, mid-task, in a rest or a wait):
//   - a dead player (a body or a ghost) holds every step: the party stays where he fell, as it does for a fallen bot,
//     so a bot of the party can raise him; nothing is ordered to him (he releases himself);
//   - a player off the plan's map, or more than kPlayerPullYd from the lead bot, holds a pull or a boss only: plain
//     walks go on (the lead bot never waits for a player on a walk).
// No pause: the run goes on by itself once the player is back.
inline std::string PlayerHoldNote(const Run& r, const Facts& f, const PlanStep& s)
{
    if (f.players.empty() || f.anyInCombat || InEvent(r))
        return std::string();
    for (const PlayerFact& p : f.players)
        if (p.alive && p.inCombat)
            return std::string();
    if (r.phase != Phase::Begin && r.phase != Phase::Gather && r.phase != Phase::Walk)
        return std::string();
    for (const PlayerFact& p : f.players)
        if (!p.alive && p.online)
            return "holding: " + p.name + (p.ghost ? " is a ghost" : " is dead") + " (" +
                (p.onMap ? Num(p.yd) + " yd from the lead bot" : std::string("not in the instance")) + "): " +
                (p.ghost ? "back to the body (or a resurrection) first" : "a resurrection in the party or a release first");
    // A walk whose next point has hostiles near it pulls them first (Facts::hopUnits): that is a pull too.
    bool const hopPull = (s.kind == PlanStepKind::Move || s.kind == PlanStepKind::MoveDirect) && !f.hopUnits.empty();
    if (s.kind != PlanStepKind::Pull && s.kind != PlanStepKind::Boss && !hopPull)
        return std::string();
    for (const PlayerFact& p : f.players)
        if (!p.online)
            return "holding the pull: " + p.name + " is offline";
    for (const PlayerFact& p : f.players)
        if (!p.onMap)
            return "holding the pull: " + p.name + " is not in the instance";
    for (const PlayerFact& p : f.players)
        if (p.yd > kPlayerPullYd)
            return "holding the pull: " + p.name + " is " + Num(p.yd) + " yd from the lead bot (the pull goes out within " +
                Num(kPlayerPullYd) + " yd)";
    return std::string();
}

// The end of a regroup (the bots up and gathered): every living player on the plan's map comes within kGatherYd of the
// lead bot first, waited for at most kPlayerGatherWaitMs, then the run pauses naming who and how far. A dead or absent
// player is not waited for here (PlayerHoldNote holds the steps for him). True when the tick is spent.
inline bool RegroupPlayerWait(Run& r, const Facts& f, std::uint32_t dt)
{
    // A dead player is a dead member: the regroup waits for him to be up (a resurrection in the party, or his own
    // release and run back), at most kPlayerHoldCapMs, then pauses. Nothing is ordered to him.
    for (const PlayerFact& p : f.players)
        if (!p.alive && p.online)
        {
            r.playerHoldMs += dt;
            std::string const why = p.name + (p.ghost ? " (a ghost, " : " (dead, ") +
                (p.onMap ? Num(p.yd) + " yd from the lead bot)" : std::string("not in the instance)"));
            if (r.playerHoldMs >= kPlayerHoldCapMs)
            {
                r.playerHoldMs = 0;
                PausePlayerHold(r, "regroup: waited " + std::to_string(kPlayerHoldCapMs / 60000) + " min for " + why +
                    ": a resurrection or a corpse run, then .bot run resume");
                return true;
            }
            r.note = "regroup: waiting for " + why + ": a resurrection in the party or a release first";
            return true;
        }
    r.playerHoldMs = 0;
    const PlayerFact* farthest = nullptr;
    for (const PlayerFact& p : f.players)
        if (p.alive && p.onMap && p.yd > kGatherYd && (!farthest || p.yd > farthest->yd))
            farthest = &p;
    if (!farthest)
    {
        r.playerGatherMs = 0;
        return false;
    }
    r.playerGatherMs += dt;
    if (r.playerGatherMs >= kPlayerGatherWaitMs)
    {
        r.playerGatherMs = 0;
        PausePlayerHold(r, "regroup: waited " + std::to_string(kPlayerGatherWaitMs / 1000) + " s for " + farthest->name + " (" +
            Num(farthest->yd) + " yd from the lead bot): come to the party, then .bot run resume");
        return true;
    }
    r.note = "regroup: waiting for " + farthest->name + " (" + Num(farthest->yd) + " yd from the lead bot)";
    return true;
}

inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines);
inline void EndRetreat(Run& r);

inline std::string Tick(Run& r, const Facts& raw, std::uint32_t dt = kTickMs, const RestLines& lines = RestLines())
{
    // A bot outside the instance is no part of the party's fight (PlanMapFightView: Dire Maul West r38 slot B).
    Facts view;
    bool const offMap = PlanMapFightView(raw, view);
    const Facts& in = offMap ? view : raw;
    // Combat with units the group cannot attack only is no fight (UnattackableOnlyFight) once the same units
    // have stood so for the grace (UnattackableGraceTick): the step runs on a view with the combat flags cleared.
    bool const unattackable = UnattackableGraceTick(r, in, dt);
    Facts quiet;
    if (unattackable)
    {
        quiet = in;
        quiet.anyInCombat = false;
        quiet.leadInCombat = false;
        quiet.staleCombat = false;
    }
    // The bots held by such units only: their combat is dropped after the same grace (CombatDropTick), and a
    // gather's ceiling waits for them meanwhile (HeldStragglerWait).
    std::string const dropNote = CombatDropTick(r, in, dt);
    const Facts& f = unattackable ? quiet : in;
    if (!f.anyInCombat)
    {
        r.hpWatch.clear(); // HpHeldTick watches one fight
        ClearFightProgress(r); // ... and so does FightProgressTick
        EndRetreat(r);         // ... and the retreat at two dead (RetreatTick)
    }
    bool const firstUnattackable = unattackable && !r.unattackableFight;
    r.unattackableFight = unattackable;
    r.shotGateRan = false;
    SyncCcIcons(r, f);
    ArmReachedTriggers(r); // a step reached by a skip, a resume or a jump since the last tick
    std::string order;
    bool held = false;
    if (r.state == State::Running && !r.hold.empty())
    {
        UpdateHold(r, f);
        if (!r.hold.empty())
        {
            order = TickBody(r, HoldView(r, f), dt, lines);
            held = true;
        }
    }
    if (!held)
        order = TickBody(r, f, dt, lines);
    OffMapReturnTick(r, f, order); // a bot outside the instance in a bot-only run (lead decision 2026-10-24)
    if (!r.shotGateRan)
        EndShotHold(r); // the pull stand's hold ends with any tick its gate did not run on
    ArmReachedTriggers(r); // the steps this tick reached
    if (firstUnattackable && r.state == State::Running)
        r.note += (r.note.empty() ? "" : "; ") + std::string("in combat with units the group cannot attack only (") +
            EngagedUnitsNote(in) + "): not counted as a fight";
    if (!dropNote.empty())
        r.note += (r.note.empty() ? "" : "; ") + dropNote;
    return order;
}

// A unit in Run::neverCame that turns up as an attacker of the party (on a
// bot, on the lead bot, or a kill_first unit in the fight) came after all: it
// is taken out of the set at once, so the hop picks see it again.
inline void ForgetUnitsThatCame(Run& r, const Facts& f)
{
    if (r.neverCame.empty())
        return;
    for (const UnitFact& u : f.partyAttackers)
        r.neverCame.erase(u.guid);
    for (const UnitFact& u : f.killFirst)
        r.neverCame.erase(u.guid);
    if (f.leadHasAttacker)
        r.neverCame.erase(f.leadAttacker.guid);
}

// ---------------------------------------------------------------------------
// Fight forward (WASTED-TIME 2026-10-23 sink 2: 1,909 pulls walked 28.2 yd each beyond the farthest new ground, the
// tank going back to the plan's stand and forward again; r24 slot 7 step 3: the pack at -194 2131, back to the stand
// at -231 2144, 33 yd, then step 4 walked forward again). After a won fight (Run::fightForwardDue), when the steps
// ahead are plain moves (no exact point, no launch, no direct step), at most kFightForwardMoves of them, up to a
// pull of the party's team whose point lies within kFightForwardYd of the lead bot (and kStragglerDzYd up or down),
// not an ambush, not left uncleared, and the straight line to it passes no other live pack (its radius plus
// kPackClearanceYd), those moves are left out: the pull goes out from where the tank stands (the run's anchor is
// set there, so the gather and the pullback's return are here). AiPlayerbot.DungeonRunWalkBackAfterWonFight = 1
// keeps the old way. The pull's own rules stay: its gather, the firing stand (AiPlayerbot.DungeonRunPullStand: a
// tank that cannot shoot from here still walks to one), crowd control and the aggro estimate. Never in an event or a
// walk back.
constexpr float kFightForwardYd = 30.0f;
constexpr float kForwardAggroMarginYd = 5.0f; // a live hostile within its aggro radius plus this of the way: no skip
constexpr std::size_t kFightForwardMoves = 4;

// The pull step the moves from the current step lead to, if fight forward may take it (npos for none).
inline std::size_t FightForwardTarget(const Run& r, const Facts& f)
{
    // Never past a leg's end (StepAfter gives up the leg's deferred packs there) nor with a deferred pull pending
    // (StepAfter may turn back to it after a pull: the Codex review of 0d9957be).
    if (!r.plan || r.step == 0 || !r.deferredPulls.empty() || DeferRetryOn(r))
        return static_cast<std::size_t>(-1);
    std::uint8_t const leg = r.plan->steps[r.step - 1].leg;
    std::size_t moves = 0;
    for (std::size_t i = r.step; i < r.plan->steps.size(); ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (p.leg != leg)
            return static_cast<std::size_t>(-1);
        if (p.kind == PlanStepKind::Move)
        {
            if (++moves > kFightForwardMoves || IsExactPoint(r.plan, i) || ExactLaunch(r.plan, i) != static_cast<std::size_t>(-1))
                return static_cast<std::size_t>(-1);
            continue;
        }
        if (p.kind != PlanStepKind::Pull || !moves || !ai::StepForTeam(p, f.team) || (p.flags & ai::kPlanFlagAmbush) ||
            r.notCleared.count(i))
            return static_cast<std::size_t>(-1);
        if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) > kFightForwardYd ||
            std::fabs(f.leadZ - p.z) > kStragglerDzYd)
            return static_cast<std::size_t>(-1);
        PathPoint a, b;
        a.x = f.leadX;
        a.y = f.leadY;
        a.z = f.leadZ;
        b.x = p.x;
        b.y = p.y;
        b.z = p.z;
        for (std::size_t k = 0; k < r.plan->steps.size(); ++k)
        {
            const PlanStep& g = r.plan->steps[k];
            if (k == i || (g.kind != PlanStepKind::Pull && g.kind != PlanStepKind::Boss) || !ai::StepForTeam(g, f.team) ||
                (k < r.step && !r.notCleared.count(k)))
                continue;
            if (SegmentDistance(a, b, g.x, g.y, g.z) <= g.radius + kPackClearanceYd)
                return static_cast<std::size_t>(-1);
        }
        return i;
    }
    return static_cast<std::size_t>(-1);
}

// Why the glue's read of the way forbids the skip ("" when it does not): the read missing or for another step, a
// path that ends short of the pack or is a detour, no sight of the pack, a live hostile by the way, or a live hostile
// at a move point left out.
inline std::string FightForwardReadRefusal(const Facts& f, std::size_t target)
{
    if (!f.forwardRead || f.forwardFor != target)
        return "the way was not read";
    if (!f.forwardPathOk)
        return "no direct navmesh path";
    if (!f.forwardLos)
        return "no sight of the pack";
    if (f.forwardHostileNear)
        return "a hostile by the way";
    if (f.forwardHopAlive)
        return "a hostile at a move point left out";
    return std::string();
}

// On the first tick of the step after a won fight (once a rest is over): the pull it may lead to is asked of the
// glue (Run::fightForwardWanted: its navmesh path, sight line and hostiles by the way, read next tick); on that
// tick, with every check passed and the target unchanged, the moves are left out. True while it holds the tick.
inline bool FightForwardTick(Run& r, const Facts& f)
{
    if (!r.fightForwardDue || r.phase == Phase::Rest)
        return false; // a rest or meal wait first: judged when it is over
    std::size_t const asked = r.fightForwardWanted;
    r.fightForwardWanted = static_cast<std::size_t>(-1);
    if (!f.fightForward || f.walkBackAfterWonFight || r.walkBack || r.regroup || InEvent(r) || r.phase != Phase::Begin ||
        f.anyInCombat || f.dead > 0)
    {
        r.fightForwardDue = false;
        return false;
    }
    std::size_t const target = FightForwardTarget(r, f);
    if (target == static_cast<std::size_t>(-1))
    {
        r.fightForwardDue = false;
        return false;
    }
    if (asked != target)
    {
        r.fightForwardWanted = target;
        r.note = "fight forward: reading the way to the pack of step " + std::to_string(target + 1);
        return true;
    }
    r.fightForwardDue = false;
    std::string const refused = FightForwardReadRefusal(f, target);
    if (!refused.empty())
    {
        r.note = "fight forward: not to the pack of step " + std::to_string(target + 1) + " (" + refused +
            "): the plan's rows";
        return false;
    }
    std::size_t const first = r.step;
    const PlanStep& p = r.plan->steps[target];
    float const yd = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
    while (r.step < target && Current(r) && Current(r)->kind == PlanStepKind::Move)
        NextStep(r, "fight forward");
    if (r.step != target)
        return true; // StepAfter went another way (a step for the other team): its own rules from here
    r.anchorKnown = true;
    r.anchorX = f.leadX;
    r.anchorY = f.leadY;
    r.anchorZ = f.leadZ;
    r.note = "fight forward: the pack of step " + std::to_string(target + 1) + " is " + Num(yd) +
        " yd from the tank: pulled from here (" +
        (target - first == 1 ? "step " + std::to_string(first + 1) : "steps " + std::to_string(first + 1) + "-" +
            std::to_string(target)) + " left out)";
    return true;
}

// ---------------------------------------------------------------------------
// The retreat at two dead (AiPlayerbot.DungeonRunRetreatAt2Dead, default 0; WASTED-TIME 2026-10-23 sink 5: 30 notes
// "3 dead: fighting on, a regroup follows", each a wipe and a corpse walk back of 4-28 min). On: in a fight with
// exactly kWipeDead - 1 members dead (two in a party of five), the lead bot alive and the fight being lost (the
// progress watch behind the fight ceiling, FightProgressTick, read and no engaged unit set a new low in the last
// kRetreatQuietMs), the living walk back to the run's last stand (Run::anchor, at most kRetreatMaxYd away) with one
// `move` (sent again once after kRetreatResendMs), for at most kRetreatCapMs, once per fight; then the fight is the
// bots' own again. Never inside a scripted event, on the steps leading into one, on a hold step, or on a boss step
// that is an event (an encounter, or done by instance data or an object). A refused order ends it (OrderRefused).
constexpr std::uint32_t kRetreatQuietMs = 15000;
constexpr float kRetreatMaxYd = 40.0f;
constexpr std::uint32_t kRetreatResendMs = 5000;
constexpr std::uint32_t kRetreatCapMs = 20000;

// The fight is being lost: the progress watch read engaged units for at least kRetreatQuietMs and none set a new low
// in that time (FightProgressTick's marks).
inline bool FightBeingLost(const Run& r)
{
    if (r.fightHp.empty() || r.fightHpMs < kRetreatQuietMs)
        return false;
    for (const auto& entry : r.fightHp)
        if (entry.second.fell && r.fightHpMs - entry.second.at < kRetreatQuietMs)
            return false;
    return true;
}

// Whether the retreat may run on step `s` at all: no event (InEvent, a hold ahead, a hold step) and no event boss.
inline bool RetreatAllowedOn(const Run& r, const PlanStep& s)
{
    if (InEvent(r) || EventHoldAhead(r) || s.kind == PlanStepKind::Hold)
        return false;
    std::uint32_t const eventFlags = ai::kPlanFlagEncounter | ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject;
    return !(s.kind == PlanStepKind::Boss && (s.flags & eventFlags));
}

// One fight tick of the retreat. True while it holds the tick (the guard, crowd control and focus wait).
inline bool RetreatTick(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    if (!f.retreatAt2Dead)
        return false;
    if (r.retreatOn)
    {
        r.retreatMs += dt;
        float const away = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.anchorX, r.anchorY, r.anchorZ);
        if (away <= kArriveYd || r.retreatMs >= kRetreatCapMs || !f.leadAlive || f.dead >= kWipeDead)
        {
            r.retreatOn = false;
            r.retreatDone = true;
            r.note = "retreat over (" + Num(away) + " yd from the last stand): fighting";
            return false;
        }
        if (r.retreatOrders < 2 && r.retreatMs >= kRetreatResendMs * r.retreatOrders)
        {
            ++r.retreatOrders;
            order = MoveOrder(r.anchorX, r.anchorY, r.anchorZ, false);
        }
        r.note = "retreat: " + std::to_string(f.dead) + " dead, the living pull back to the last stand at " +
            Num(r.anchorX) + " " + Num(r.anchorY) + " " + Num(r.anchorZ) + " (" + Num(away) + " yd)";
        return true;
    }
    if (r.retreatDone || f.dead + 1 != kWipeDead || f.alive == 0 || !f.leadAlive || !r.anchorKnown ||
        !RetreatAllowedOn(r, s) || !FightBeingLost(r))
        return false;
    float const away = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.anchorX, r.anchorY, r.anchorZ);
    if (away <= kArriveYd || away > kRetreatMaxYd)
        return false;
    r.retreatOn = true;
    r.retreatMs = 0;
    r.retreatOrders = 1;
    order = MoveOrder(r.anchorX, r.anchorY, r.anchorZ, false);
    r.note = "retreat: " + std::to_string(f.dead) + " dead and no engaged unit lost hp in " +
        std::to_string(kRetreatQuietMs / 1000) + " s: the living pull back to the last stand at " + Num(r.anchorX) +
        " " + Num(r.anchorY) + " " + Num(r.anchorZ) + " (" + Num(away) + " yd)";
    return true;
}

// The retreat's state ends with the fight.
inline void EndRetreat(Run& r)
{
    r.retreatOn = false;
    r.retreatMs = 0;
    r.retreatOrders = 0;
    r.retreatDone = false;
}

// Whether every bot of the party died within kWipeWindowMs (Run::diedAtMs), each death after the last counted wipe, with
// no living real player: the collapse the one-tick rule misses when the lead bot revives between the deaths. The party's
// bots are Facts::members (the living, the lead bot too) and Facts::deadNames; their names must cover Facts::total (a
// policy test that sets only counts never fires it).
inline bool WindowWipe(const Run& r, const Facts& f)
{
    if (f.total < 2)
        return false;
    for (const PlayerFact& p : f.players)
        if (p.alive)
            return false;
    std::unordered_set<std::string> names(f.deadNames.begin(), f.deadNames.end());
    for (const MemberFact& m : f.members)
        if (!m.name.empty())
            names.insert(m.name);
    if (names.size() < f.total)
        return false;
    for (const std::string& n : names)
    {
        auto it = r.diedAtMs.find(n);
        if (it == r.diedAtMs.end() || it->second <= r.lastWipeMs || r.runMs - it->second > kWipeWindowMs)
            return false;
    }
    return true;
}

// The run's wipe, near-wipe and lead-death counts for the report, once per running tick (TickBody).
inline void NoteWipes(Run& r, const Facts& f)
{
    for (const std::string& n : f.deadNames)
        if (!r.deadNow.count(n))
            r.diedAtMs[n] = r.runMs;
    r.deadNow = std::unordered_set<std::string>(f.deadNames.begin(), f.deadNames.end());
    if (!f.leadAlive && !r.leadDown)
        ++r.leadDeaths;
    r.leadDown = !f.leadAlive;
    // A wipe (the whole party dead: every bot and every real player) counts once, however long it lasts.
    bool const partyWiped = PartyAllDead(f);
    bool const windowWipe = !partyWiped && WindowWipe(r, f);
    if ((partyWiped && !r.wiped && !r.windowWiped) || (windowWipe && !r.windowWiped && !r.wiped))
    {
        ++r.wipes;
        r.lastWipeMs = r.runMs;
    }
    r.wiped = partyWiped;
    if (windowWipe)
        r.windowWiped = true;
    else if (PartyDead(f) < kWipeDead)
        r.windowWiped = false;
    // A near wipe (kWipeDead or more dead, the players counted, someone still alive) counts once
    // too; one that ends in a wipe counts as both.
    std::uint32_t const partyDead = PartyDead(f);
    bool const nearWipe = partyDead >= kWipeDead && !partyWiped;
    // The latch holds until fewer than kWipeDead are dead: a near wipe that
    // turns into a wipe, and the revive one by one after it, is one event.
    if (nearWipe && !r.nearWiped)
        ++r.nearWipes;
    if (nearWipe)
        r.nearWiped = true;
    else if (partyDead < kWipeDead)
        r.nearWiped = false;
}

inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)
{
    std::string order;
    if (r.state != State::Running)
        return order;
    const PlanStep* s = Current(r);
    if (!s)
    {
        r.state = State::Done;
        r.note = DoneNote(r);
        return order;
    }
    r.runMs += dt;
    r.phaseMs += dt;
    ForgetUnitsThatCame(r, f);
    if (r.eventOn)
        r.eventMs += dt; // an event's clock runs in its fights too
    r.gatherOrder.clear(); // only this tick's gather can be refused next
    if (f.anchorPathRead)
        r.anchorPathWanted = false;
    UpdateStill(r, f, dt);
    UpdateStraggle(r, f, dt);
    NoteDeadSeen(r, f);
    NoteLeadDeath(r, f);
    NoteLeadPath(r, f);
    if (FactStep(r) == s)
        UpdateObjectSighting(r, *s, f, dt);
    else
        r.goneMissMs = 0; // facts for another step (a walk back): the unbroken miss run starts over
    if (r.lootFfaLive)
    {
        r.lootFfaMs += dt;
        if (LootFfaRestoreTick(r, f, order))
            return order;
    }

    // The stale flag's clocks: one snapshot of "nothing hostile engaged" proves
    // nothing (a guardian, a scripted fight), so the grace is spent once per
    // combat and a wait step is finished over it only when it stands unbroken.
    // They end with the combat (any early return below included), not with a
    // step finished over the flag.
    if (!f.anyInCombat)
    {
        r.staleMs = 0;
        r.staleRunMs = 0;
    }
    else if (f.staleCombat)
    {
        r.staleMs += dt;
        r.staleRunMs += dt;
    }
    else
        r.staleRunMs = 0;

    // A hold whose event is done ends on any tick (HoldDoneNote): in a fight,
    // a regroup, a rest or an approach too, the spot in sight or not.
    if (s->kind == PlanStepKind::Hold)
    {
        std::string const done = HoldDoneNote(*s, f);
        if (!done.empty())
        {
            if (f.seen)
                CountEvent(r, f, *s);
            std::string const progress = EventProgress(r);
            NextStep(r, done + (progress.empty() ? std::string() : " (" + progress + ")"));
            s = Current(r);
            if (!s)
                return order;
        }
    }

    // Spec item-leg-skip C2/C2b: the leg's item needs (an optional leg's
    // skip, a key door's hold) before the step runs its rules. The hold fires
    // on the leg's FIRST step, before any move of that leg is ordered.
    if (LegNeedGate(r, f))
        return order;
    if (DoorKeyOpen(r, f, dt, order))
        return order;
    if (DoorNeedHold(r, f))
        return order;
    NoteWipes(r, f);
    // A wipe also ends the run's own fight below (the bots' all-dead: a dead player cannot be revived by the run).
    bool const allDead = f.total > 0 && f.alive == 0;
    std::uint32_t const partyDead = PartyDead(f);

    // A party member coming back with attackers runs on to the party; the run
    // waits for it where it stands (RunBack): no regroup or step order, but
    // the fight handling below still runs.
    bool const runBackWait = RunBack(r, f, dt);
    std::string const runBackNote = runBackWait ? r.note : std::string();

    // After a resume or a skip from a pause: the dead get up, the living
    // gather on the lead bot, then the step goes on.
    // A fight during the regroup (a pack met on the corpse run) is fought
    // first: the combat block below bounds it, and the regroup goes on after.
    // Deaths in it that make a wipe hand over to the regroup by itself.
    // The party's dead (bots and players) count (review round 1); a fight a living player is in is a fight.
    if (r.regroup && r.regroupDead < 0)
        r.regroupDead = static_cast<int>(partyDead);
    if (r.regroup && !f.anyInCombat && (partyDead >= kWipeDead || allDead) &&
        static_cast<int>(partyDead) > r.regroupDead)
        r.regroup = false;
    if (r.regroup && !runBackWait && !f.anyInCombat)
    {
        r.regroupMs += dt;
        // A fight met in the regroup is over on this quiet tick (the block returns before the end of a fight below):
        // the next one gets its own ceiling (Dire Maul West r38 slot B: 10-s fights of a regroup added up past 180 s).
        r.fightMs = 0;
        UpdateReviveSummons(r, f, dt);
        // The lead bot up in another part of the instance (another wing's
        // door, far from every point of the plan): no gather, summon or walk
        // back joins it to the party, so the run pauses with that reason and
        // the living party holds where it is (r20-19z39 slot 7). In a bot-only
        // party with everyone up (a wipe: they all came back at that door), the
        // rescue teleport instead (OtherPartRescue): the regroup ends here, its
        // summons bring the party (Dire Maul West r36 slot D).
        // A bot still dead with everything else fitting (OtherPartBlock::Dead; Dire Maul West r39/r40 slot 6: the
        // lead bot up at the door, one bot still a ghost for 1-8 s): the regroup waits for it, `revive party` again
        // each minute, bounded as the revive (kRegroupReviveMs), then the pause that a resume rescues from.
        if (LeadInOtherPart(r, f))
        {
            OtherPartBlock const why = OtherPartRescueWhy(r, f);
            if (why == OtherPartBlock::None && OtherPartRescue(r, f))
            {
                r.regroup = false;
                r.reviveSummons.clear();
                r.gatherWaived = false;
                r.gatherRefusals = 0;
                r.waitFor.clear();
                r.leadDied = false;
                r.holdDue = false;
                r.hold.clear();
                r.walkBackDue = false; // the rescue's walk back is set (RescueDue)
                return order;
            }
            if (why == OtherPartBlock::Dead && r.regroupMs <= kRegroupReviveMs + r.reviveBaseMs)
            {
                r.note = "regroup: the lead bot revived in another part of the instance: waiting for " +
                    OtherPartDeadNames(f) + " (dead) before the rescue teleport";
                if (!r.reviveSent || r.regroupMs % 60000 < dt)
                {
                    r.reviveSent = true;
                    order = "revive party";
                }
                return order;
            }
            PauseOtherPart(r, f, why);
            return order;
        }
        // A regroup for a dead lead bot: once it is up away from where it fell
        // (a release: the entrance), the living party there holds, before any
        // gather; from the next tick the regroup looks at the others only.
        if (r.holdDue && f.leadAlive)
        {
            r.holdDue = false;
            if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.leadDeathX, r.leadDeathY, r.leadDeathZ) > kSeeYd)
            {
                FillHold(r, f);
                if (!r.hold.empty())
                {
                    r.note = "regroup: the lead bot is up away from the party: the party holds while it walks back";
                    return order;
                }
            }
        }
        if (f.dead > 0 || !f.leadAlive)
        {
            // A living priest, paladin or shaman raises the dead where they
            // lie: `revive party` (a release and a corpse run) waits for it.
            // A nudge the module answered as unwalkable (OrderRefused) ends the wait: the revive goes out now.
            if (f.rezHold && !r.rezBodyUnreachable)
            {
                // The rez hold decides (RezHoldPolicy.h): hold for a resurrection,
                // wait for the resurrector's mana, or fall through to the revive.
                TortoiseBots::RezHold::Inputs hold;
                hold.holdMs = r.regroupMs;
                hold.ableMs = r.rezAbleMs;
                hold.rezInProgress = f.rezInProgress;
                hold.partyEngaged = f.anyInCombat;
                hold.reviveSent = r.reviveSent;
                TortoiseBots::RezHold::Result const verdict = TortoiseBots::RezHold::Decide(hold, f.rezMembers);
                using TortoiseBots::RezHold::Verdict;
                using TortoiseBots::RezHold::Reason;
                // The budget of a rezzer that could act runs while it could; a drink restarts it.
                if (verdict.verdict == Verdict::WaitMana)
                    r.rezAbleMs = 0;
                else if (verdict.reason == Reason::Recovering || verdict.reason == Reason::Casting)
                    r.rezAbleMs += dt;
                if (verdict.verdict == Verdict::Hold || verdict.verdict == Verdict::WaitMana)
                {
                    auto nameOf = [&](int idx) -> std::string
                    {
                        return idx >= 0 && static_cast<std::size_t>(idx) < f.rezMembers.size()
                            ? f.rezMembers[static_cast<std::size_t>(idx)].name : std::string();
                    };
                    std::string who = nameOf(verdict.rezzerIdx);
                    if (!who.empty() && verdict.targetIdx >= 0)
                        who += " for " + nameOf(verdict.targetIdx);
                    // How far the rezzer stands from the body and whether it sees it: the 2026-10-20 Uldaman
                    // hold ran 63 s on a priest that never got into sight of a body 20 yd away.
                    float rezzerYd = 0.0f;
                    bool rezzerSees = false;
                    bool const viewed = verdict.verdict == Verdict::Hold &&
                        TortoiseBots::RezHold::RezzerView(verdict, f.rezMembers, rezzerYd, rezzerSees);
                    r.note = "regroup: " + WaitNote(r, f) + ", " +
                        (verdict.verdict == Verdict::Hold ? std::string("a resurrection in the party first")
                                                          : std::string("the resurrector drinks first")) +
                        " (" + TortoiseBots::RezHold::ReasonName(verdict.reason) +
                        (who.empty() ? std::string() : ": " + who) +
                        (viewed ? ", " + nameOf(verdict.rezzerIdx) + " " + Num(rezzerYd) + " yd from the body, sight " +
                                      (rezzerSees ? "yes" : "no")
                                : std::string()) + ")";
                    SummonRevived(r, f, order);
                    // A hold that went kRezNudgeMs with a rezzer able and no cast: the lead bot walks onto the body
                    // (once), and the party follows it into the rezzer's reach and sight.
                    if (verdict.verdict == Verdict::WaitMana || f.rezInProgress)
                        r.rezHoldMs = 0;
                    else if (verdict.reason == Reason::Recovering)
                    {
                        r.rezHoldMs += dt;
                        int const t = verdict.targetIdx;
                        if (order.empty() && !r.rezNudged && r.rezHoldMs >= TortoiseBots::RezHold::kRezNudgeMs &&
                            f.leadAlive && t >= 0 && static_cast<std::size_t>(t) < f.rezMembers.size() &&
                            f.rezMembers[static_cast<std::size_t>(t)].placed)
                        {
                            TortoiseBots::RezHold::Member const& body = f.rezMembers[static_cast<std::size_t>(t)];
                            if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, body.bodyX, body.bodyY, body.bodyZ) >
                                TortoiseBots::RezHold::kRezNearYd)
                            {
                                r.rezNudged = true;
                                order = MoveOrder(body.bodyX, body.bodyY, body.bodyZ, false);
                                r.rezNudgeOrder = order;
                                r.note += "; the lead bot walks to the body";
                            }
                        }
                    }
                    return order;
                }
            }
            else if (f.rezzer && !r.reviveSent && !r.rezBodyUnreachable && r.regroupMs < kPartyRezWaitMs)
            {
                r.note = "regroup: " + WaitNote(r, f) + ", a resurrection in the party first";
                SummonRevived(r, f, order);
                return order;
            }
            r.note = "regroup: " + WaitNote(r, f) + ", reviving";
            if (!r.reviveSent || r.regroupMs % 60000 < dt)
            {
                if (!r.reviveSent && f.rezHold)
                    r.reviveBaseMs = r.regroupMs; // the hold's time is not taken from the corpse run
                r.reviveSent = true;
                r.rezHoldMs = 0;
                r.rezNudged = false;
                r.rezNudgeOrder.clear();
                r.rezBodyUnreachable = false;
                order = "revive party";
            }
            else
                SummonRevived(r, f, order); // one already up far away: before its corpse run
            if (r.regroupMs > kRegroupReviveMs + r.reviveBaseMs)
                Pause(r, "regroup: " + std::to_string(f.dead) + " still dead after " +
                    std::to_string(kRegroupReviveMs / 1000) + " s (cmd revive <bot>, then .bot run resume)");
            return order;
        }
        if (f.maxMemberYd > kGatherYd && !f.anyInCombat && !r.gatherWaived)
        {
            // A revived bot far away is summoned before the gather walks it.
            if (SummonRevived(r, f, order))
                return order;
            r.note = "regroup: " + WaitNote(r, f) + ", gathering on the lead bot" +
                (r.reviveSummons.empty() ? "" : " (a summon on its way)");
            if (!r.regroupGatherSent && !r.reviveSummons.empty())
                return order; // the gather goes out once the summons are in (or failed)
            if (!r.regroupGatherSent)
            {
                r.regroupGatherSent = true;
                r.RestartRegroupClock();
                r.regroupGatherCapMs = RegroupGatherCap(f);
                SendGather(r, f, order);
            }
            // A bot being summoned does not count against the ceiling.
            else if (r.reviveSummons.empty())
            {
                // A far bot that has not moved at all is stuck (a navmesh
                // pocket), not walking: it is summoned at once, before the
                // ceiling is waited out for it (SummonStuckInRegroup).
                if (SummonStuckInRegroup(r, f, order))
                    return order;
                if (StragglerLadderTick(r, f, order))
                {
                    r.note = "regroup: " + r.note;
                    return order;
                }
                if (r.regroupMs > r.regroupGatherCapMs)
                {
                    if (SummonStraggler(r, f, order) || HeldStragglerWait(r, f) || StragglerLadderPending(r, f))
                        r.RestartRegroupClock();
                    else
                        Pause(r, "regroup: the party did not gather within " +
                            std::to_string(r.regroupGatherCapMs / 1000) + " s (a bot is " + Num(f.maxMemberYd) +
                            " yd from the lead bot)");
                }
            }
            return order;
        }
        // ... and every living real player of the party with it (bounded, then a pause naming him).
        if (RegroupPlayerWait(r, f, dt))
            return order;
        r.regroup = false;
        r.reviveSummons.clear();
        r.gatherWaived = false;
        r.gatherRefusals = 0;
        r.waitFor.clear(); // everyone alive and gathered: the fallen are back
        r.leadDied = false;
        r.holdDue = false;
        r.note = "regrouped";
        // A retry of a deferred pull (a resume, a regroup): it goes on only with the lead bot still beside the
        // pack (NearDeferredPack), with no walk back; otherwise the walk back gives it up (StartWalkBack).
        if (r.walkBackDue && DeferRetryOn(r) && NearDeferredPack(*Current(r), f.leadX, f.leadY, f.leadZ))
        {
            r.walkBackDue = false;
            r.note = "regrouped beside the deferred pack: its retry goes on";
        }
        if (r.walkBackDue && ApproachResumeInPlace(r, f))
        {
            r.walkBackDue = false;
            r.note = "regrouped where the approach paused: no walk back, the stand is searched from here";
        }
        if (r.walkBackDue)
        {
            StartWalkBack(r, f);
            if (r.walkBack)
                return order; // the next tick reads the facts for the first point
        }
    }

    // A wipe or kWipeDead dead: once the fight is over the run regroups by
    // itself (revive, gather, walk back), kAutoRegroupsPerStep times a step;
    // then it pauses for a regroup by hand.
    // The party's dead, the players counted: three of them (or every bot) are a wipe the run regroups after. The
    // regroup waits for a dead player too (RegroupPlayerWait), so it is not started over while he is down.
    bool const wipe = partyDead >= kWipeDead || allDead;
    bool const autoRegroup = wipe && r.autoRegroups < kAutoRegroupsPerStep;
    if (wipe && !autoRegroup)
    {
        RearmRescueAfterDeath(r); // a death: the resume's walk back may teleport once more
        Pause(r, std::to_string(partyDead) + " dead: regroup (cmd revive party), then .bot run resume (" +
            std::to_string(kAutoRegroupsPerStep) + " regroups by itself at this step did not hold)");
        return order;
    }
    if (autoRegroup && !f.anyInCombat)
    {
        ++r.autoRegroups;
        r.fighting = false;
        // A wipe inside a hold whose event is not done: the event reset, so the
        // regroup goes back to its start step (RearmEventOnWipe).
        std::size_t const rearmed = RearmEventOnWipe(r, f) ? r.step : static_cast<std::size_t>(-1);
        bool const deathSpotKnown = r.leadDied; // BeginRegroup clears it
        ResetStep(r);
        RearmRescueAfterDeath(r);
        BeginRegroup(r);
        r.walkBackDue = true;
        // The lead bot among the dead: as WaitForFallen, the living party holds
        // at the fight spot once it is up away from there (r20-19z39 slot 7:
        // the hunter left the Library after the lead bot's corpse run).
        r.holdDue = deathSpotKnown;
        r.note = std::to_string(partyDead) + " dead (" + WipeCounts(r) + " this run): regrouping by itself (" +
            std::to_string(r.autoRegroups) + " of " + std::to_string(kAutoRegroupsPerStep) + " at this step)" +
            (rearmed == static_cast<std::size_t>(-1) ? std::string()
                                                     : "; the event starts again at step " +
                    std::to_string(rearmed + 1) + " (" + ai::PlanStepKindName(r.plan->steps[rearmed].kind) + ")");
        return order;
    }
    // The owner's rule (2026-10-17): "Party should wait if a party member
    // dies until it is back." Once the fight is over, any death (the lead bot
    // too, one in a walk, one that released and runs back) holds the next
    // pull, boss, task or move until the bot is alive and back with the party:
    // the regroup (a resurrection in the party, `revive party`, the gather, a
    // stuck straggler summoned at its ceiling, then a pause with the reason).
    // Not inside a scripted event (it cannot be left mid-way: the wait comes
    // after it), not during a commander task, and not while a member runs
    // back with attackers (RunBack). The lead bot dead alone came back far
    // from where it fell: the wipe recovery (LeadRevivedAway). In a fight the
    // others fight on; more dead make it a wipe and the run regroups by itself.
    if (!autoRegroup && !f.anyInCombat && !r.regroup && !runBackWait && (!InEvent(r) || LeadLostInHold(r, f)) &&
        r.phase != Phase::AwaitTask && r.phase != Phase::Wait)
    {
        if (LeadRevivedAway(r, f))
            return order;
        if (WaitForFallen(r, f))
            return order;
    }

    // A wait_object whose object already is in the wanted state is done, in a
    // fight or not (a leftover combat flag must not hold it): Uldaman run 4,
    // Archaedas down, the vault door 124369 active, "a fight lasted more than
    // 180 s" at the wait step.
    // Only on a stale flag: a real fight keeps its handling and its timer.
    // A flagged wait (ai::kPlanFlagGoneIsDone) over the same stale flag is finished by its object being gone, too.
    bool const objectDone = s->kind == PlanStepKind::WaitObject && r.staleRunMs >= kStaleConfirmMs &&
        ((f.objectState >= 0 && f.objectState == static_cast<int>(s->param)) || WaitObjectGoneIsDone(r, *s, f));

    // Fights are the bots' own; the run only bounds them.
    if (f.anyInCombat && !objectDone)
    {
        // The ritual answered and its event is fighting already (the first
        // Stone Keeper wakes with the cast): the fight is the hold's.
        // An interrupted answer counts too while the event reads in progress:
        // the altar going back to ready as the keepers woke is the event
        // starting (Uldaman run 4, 19z13: the tank stood on stay all fight).
        bool eventRunning = false;
        if (s->kind == PlanStepKind::Ritual && r.orderSent && f.taskReply == 2)
        {
            const PlanStep* ahead = EventHoldAhead(r);
            // Only the module's typed `interrupted` answer, and only when the data was not in progress already
            // when the order went out (a ritual sent after the reset wait ran out): then this order started it.
            eventRunning = ahead && (ahead->flags & ai::kPlanFlagDoneData) && (ahead->flags & ai::kPlanFlagEncounter) &&
                f.eventData == 1 && r.dataAtOrder != 1 && f.taskText.compare(0, 12, "interrupted:") == 0;
        }
        if (s->kind == PlanStepKind::Ritual && r.orderSent && (f.taskReply == 1 || eventRunning))
        {
            NextStep(r, "ritual " + std::to_string(s->entry) + " done: the event started");
            s = Current(r);
            if (!s)
                return order;
            if (s->kind == PlanStepKind::Hold)
                r.eventOn = true;
        }
        if (!r.fighting)
        {
            r.fighting = true;
            r.restWalkDue = false; // a fight is on: the walk after the rest is over
            r.joinerRest = false; // a fight is on: the aggro estimate's rest wait is moot
            EndBigWalk(r);        // ... and so is the big pack's stand walk gate
            r.fightMs = 0;
            DropHopWaiver(r);
        }
        // A stale flag (nothing hostile engaged) is waited out for a grace
        // period; past it the fight ceiling runs again, so a flag that never
        // clears still ends in a pause.
        bool const staleGrace = f.staleCombat && r.staleMs <= kStaleCombatGraceMs;
        if (!staleGrace)
            r.fightMs += dt;
        // A hold's fights are the event (one wave after the other, no pause
        // between): its own timeout bounds them when that is longer.
        // A start step's fight (or a move's) before its hold is the event's too.
        const PlanStep* const event = s->kind == PlanStepKind::Hold ? s : EventHoldAhead(r);
        std::uint32_t const holdCap = event && event->timeoutS * 1000u > kBossFightCapMs ? event->timeoutS * 1000u
                                                                                       : kBossFightCapMs;
        // A boss step's entry engaged on any step gets the boss ceiling too (EngagedPlanBoss).
        std::uint32_t const cap = event ? holdCap
                                : s->kind == PlanStepKind::Boss || EngagedPlanBoss(r, f) ? kBossFightCapMs : kFightCapMs;
        FightProgress const progress = FightProgressTick(r, f, dt);
        // A unit the party hits that cannot die now (HpHeldTick) pauses before any ceiling: the ceiling's pause
        // would be resumed into the same fight. Not in a wipe (kWipeDead or more dead): the regroup that follows
        // wins, and the watch starts over after it (Opus cold review of bfd5aa1e).
        if (wipe)
            r.hpWatch.clear();
        else if (HpHeldTick(r, f, dt))
            return order;
        if (r.fightMs > cap && autoRegroup)
        {
            // kWipeDead or more dead and one left fighting past the ceiling:
            // a wipe, not a long fight. The regroup starts.
            ++r.autoRegroups;
            r.fighting = false;
            ResetStep(r);
            RearmRescueAfterDeath(r);
            BeginRegroup(r);
            r.walkBackDue = true;
            r.note = std::to_string(f.dead) + " dead and the fight went past " + std::to_string(cap / 1000) +
                " s (" + WipeCounts(r) + " this run): regrouping by itself (" + std::to_string(r.autoRegroups) +
                " of " + std::to_string(kAutoRegroupsPerStep) + " at this step)";
            return order;
        }
        // Past the ceiling, outside an event: a fight whose engaged units still lose hp goes on (FightProgressTick)
        // up to the hard cap (kFightHardCapMs); one with no loss in the window, or a member lost in it, pauses.
        bool const progressHolds = !event && (progress == FightProgress::Losing || progress == FightProgress::Young);
        bool const pastCeiling = r.fightMs > cap && !staleGrace;
        bool const hardCapped = pastCeiling && progressHolds && r.fightMs > std::max(cap, kFightHardCapMs);
        if (pastCeiling && (!progressHolds || hardCapped))
        {
            // The units that kept it going, so the pause shows at once what to look at; with their hp when the
            // watch read it (the old note when it did not, and in an event).
            bool const withHp = !event && progress != FightProgress::Unread;
            std::string const kept = EngagedUnitsNote(f, withHp);
            std::uint32_t const over = hardCapped ? std::max(cap, kFightHardCapMs) : cap;
            std::string why;
            if (hardCapped)
                why = ", the hard cap, though an engaged unit still lost hp";
            else if (withHp && progress == FightProgress::Dying)
                why = " and a party member died in the last " + std::to_string(kFightProgressWindowMs / 1000) + " s";
            else if (withHp)
                why = " and no engaged unit lost hp in " + std::to_string(kFightProgressWindowMs / 1000) + " s";
            Pause(r, "a fight lasted more than " + std::to_string(over / 1000) + " s" + why +
                (kept.empty() ? std::string() : " (kept on by " + kept + ")"));
            r.ceilingPaused = true;
            r.ceilingQuietMs = 0;
            return order;
        }
        // Two dead in a fight being lost: the living pull back to the last stand once (RetreatTick, key off by default).
        if (RetreatTick(r, f, *s, dt, order))
            return order;
        r.note = autoRegroup ? std::to_string(f.dead) + " dead: fighting on, a regroup follows"
               : !f.leadAlive ? std::string("the lead bot is dead: the others fight on") : std::string("fighting");
        if (runBackWait && !autoRegroup)
            r.note = runBackNote;
        if (pastCeiling)
            r.note += "; past the " + std::to_string(cap / 1000) + " s ceiling: " +
                (progress == FightProgress::Losing
                        ? "an engaged unit lost hp in the last " + std::to_string(kFightProgressWindowMs / 1000) + " s"
                        : std::string("watching the engaged units' hp"));
        // A hold done by its units whose fight began before any HoldStep tick
        // (its crew hostile at once): the event is on, so it is counted and an
        // awake unit marks it started (CountEvent); else it waited to timeout_s
        // with every unit dead (Codex review of 81b9fc71). Data and object
        // holds keep HoldStep's start.
        if (s->kind == PlanStepKind::Hold && f.seen &&
            !(s->flags & (ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject)))
            r.eventOn = true;
        if (s->kind == PlanStepKind::Hold && r.eventOn && f.seen)
        {
            CountEvent(r, f, *s);
            std::string const progress = EventProgress(r);
            if (!autoRegroup && f.leadAlive)
                r.note = "event: fighting" + (progress.empty() ? std::string() : " (" + progress + ")");
        }
        ReleaseHoldStands(r, f);
        CheckReach(r, f);
        if (CheckCc(r, f, dt, order))
            return order;
        if (CheckGuard(r, f, dt, order))
            return order;
        // The plan's kill_first unit of the fight (a healer, its healing ward)
        // is focused: skull on it, every bot attacks it. Again only when the
        // best one changes (it died, or a ward came up).
        // None left and the focused unit is gone (FocusGone: dead or
        // crowd-controlled): the focus is over, and with it the guard's reason
        // to keep watching (GuardTarget). One still alive (evading, say) keeps it.
        if (const UnitFact* k = PickKillFirst(r, f))
        {
            if (k->guid != r.focusGuid)
            {
                // The order moves the skull onto k; the old skull stays until it lands (the order may be
                // refused first: not visible/found/dead), so the old unit is remembered (cleared: a refusal of
                // the new order).
                r.prevFocusIconGuid = r.focusIconGuid;
                r.focusGuid = k->guid;
                r.focusIconGuid = k->guid;
                r.focusIconClear = 0;
                r.note = "fighting: kill " + std::to_string(k->entry) + " first";
                order = "focus " + std::to_string(k->guid);
            }
        }
        else if (r.focusGuid != 0 && FocusGone(r, f))
            DropFocus(r);
        return order;
    }
    if (runBackWait)
        return order;
    if (r.fighting)
    {
        // Back to the step's start: it looks at what is left, and rests first.
        r.fighting = false;
        r.restDue = true;
        r.eatWaitMs = 0;
        r.pullGuid = 0;
        DropFocus(r);
        r.focusRefused.clear();
        r.focusUnstayed = false;
        r.guardMs = 0;
        r.guardOrders = 0;
        r.engageGuid = 0;
        r.engageOrders = 0;
        r.guardSeen.clear();
        r.guardUnitOrders.clear();
        r.guardRefilled = false;
        r.guardRefills = 0;
        r.fightOrders = 0;
        r.reachStopped = false;
        // A fight may have moved the lead bot: an exact point's tighten starts
        // over and its proof is taken again before a direct step goes out.
        ResetExact(r);
        r.exactProved = static_cast<std::size_t>(-1);
        if (r.phase != Phase::AwaitTask && r.phase != Phase::Wait)
            EnterPhase(r, Phase::Begin);
    }

    // After a pick-up (CheckGuard): the tank walks back to the spot it stood
    // on at the first pick-up, with the party, before the step goes on; one
    // the fight took it far from is left (it stays near the fight). The
    // arrival ends the tick: this tick's facts were read during the walk,
    // without the navmesh query for an unseen step's approach (the glue skips
    // it while the run walks), so going on at once paused on "no walkable
    // path toward the pack" (Uldaman run 3, baelog pull 7).
    if (r.guardReturn)
    {
        if (InEvent(r) || r.phase == Phase::AwaitTask || r.phase == Phase::Wait)
            r.guardReturn = false;
        else if (!GuardWalkBackWanted(r, f, FactStep(r) ? *FactStep(r) : *s))
            r.guardReturn = false; // the fight is won: the step goes on from where the tank stands
        else if (r.phase != Phase::Walk &&
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.guardX, r.guardY, r.guardZ) > kGuardReturnMaxYd)
        {
            // It stays near the fight: that is the party's spot now, so a
            // later gather does not walk back to an old anchor far behind.
            r.guardReturn = false;
            r.anchorKnown = true;
            r.anchorX = f.leadX;
            r.anchorY = f.leadY;
            r.anchorZ = f.leadZ;
        }
        else if (WalkTo(r, f, r.guardX, r.guardY, r.guardZ, false, dt, order))
        {
            r.guardReturn = false;
            EnterPhase(r, Phase::Begin);
            return order;
        }
        else
        {
            if (r.state == State::Running)
                r.note = "back to the stand point after picking up the party's attackers";
            return order;
        }
    }

    // A walk back is bounded point by point (WalkTo), not by the step's ceiling.
    // The joiner rest (aggro estimate) is bounded by kRestCapMs and kMaxJoinerRests a step, and does not count
    // toward the step's idle ceiling: several loud packs in one pull step must not pause the run.
    // A hold for a real player (PlayerHoldNote) is not idle time either: it ends by itself when he is back.
    if (!r.walkBack && !(r.phase == Phase::Rest && r.restForJoiners) && PlayerHoldNote(r, f, *s).empty())
        r.stepIdleMs += dt;
    if (r.stepIdleMs > StepIdleCapMs(*s))
    {
        Pause(r, "step " + std::to_string(r.step + 1) + " (" + ai::PlanStepKindName(s->kind) + ") took more than " +
            std::to_string(StepIdleCapMs(*s) / 1000) + " s out of combat");
        return order;
    }

    // The hop off a navmesh island under way (IslandEscape): nothing else goes out.
    if (EscapeWait(r, f, dt))
    {
        r.note = "the lead bot hops off the navmesh island";
        return order;
    }

    // A rest holds fight forward's ask (FightForwardTick): no navmesh read on a rest tick; it asks again after it.
    if (r.phase == Phase::Rest)
        r.fightForwardWanted = static_cast<std::size_t>(-1);
    // Rest gate. The lines of the next fight (LightRestAhead): the full ones before a boss, an event or a big pull.
    bool const stepOver = (s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Boss) &&
        FightStepEnd(r, f, *s) != FightEnd::NotYet;
    // With the aggro estimate off, a pack of kBigPackUnits or more in sight on this pull step is a big pull: the full
    // lines (BigPackHere), and a rest to them before its pull once a step.
    bool const bigHere = BigPackHere(r, f, *s, stepOver);
    bool const lightAhead = f.lightRest && !bigHere && LightRestAhead(r, stepOver ? r.step + 1 : r.step);
    RestLines const light = LightLines(lines);
    if (r.phase == Phase::Rest)
    {
        if (r.restForEating)
            r.eatWaitMs += dt;
        // A light rest ends at the light lines; a meal wait under them as soon as the lead bot is not seated.
        // A meal wait whose lead bot is still seated goes on (no Rest/Begin flip-flop: the Opus review of 0d9957be).
        bool const lightDone = r.restLight && Rested(f, light) && (!r.restForEating || !f.leadResting);
        if (Rested(f, lines) || lightDone || r.phaseMs >= kRestCapMs ||
            (r.restForEating && (f.eating == 0 || r.eatWaitMs >= kRestCapMs)))
        {
            r.restForEating = false;
            r.restLight = false;
            r.restWalkDue = true; // the next walk is watched for a lead bot left on its meal or stay
            EnterPhase(r, Phase::Begin);
        }
        else
        {
            r.restRepeatMs += dt;
            if (r.restRepeatMs >= kRestRepeatMs)
            {
                r.restRepeatMs = 0;
                order = "rest";
            }
            return order;
        }
    }
    // A pull that brings many joiners (FightStep): the party is rested to the
    // end lines first, never inside an event.
    if (r.joinerRest)
    {
        r.joinerRest = false;
        if (!Rested(f, lines) && !InEvent(r))
        {
            EnterPhase(r, Phase::Rest);
            r.restForEating = false;
            r.restLight = false; // a big pull: the full lines
            r.restForJoiners = true;
            ++r.joinerRests; // a slot is spent by a rest that really starts, not by a pick the party was rested for
            r.restRepeatMs = 0;
            r.note = "resting: " + r.joinerNote;
            order = "rest";
            return order;
        }
    }
    if (r.phase == Phase::Begin && (r.restDue || (s->kind == PlanStepKind::Rest && !r.stepRested)))
    {
        bool const forced = s->kind == PlanStepKind::Rest && !r.stepRested;
        r.restDue = false;
        r.stepRested = r.stepRested || s->kind == PlanStepKind::Rest;
        bool const lightRest = !forced && lightAhead;
        if (forced || NeedsRest(f, lightRest ? light : lines))
        {
            EnterPhase(r, Phase::Rest);
            r.restForEating = false;
            r.restLight = lightRest;
            r.restForJoiners = false;
            r.restRepeatMs = 0;
            r.note = "resting";
            order = "rest";
            return order;
        }
    }
    // A bot that sat down to eat or drink after the fight is resting even
    // above the start lines (the tank at 82 % ate before Uldaman pull 17 and
    // the pull order found it seated): the run waits until nobody eats or
    // everyone is at the end lines. Looked at on every tick before the pull
    // goes out (Begin, Gather: bots sit down a tick after the fight ends),
    // kRestCapMs of waiting per fight.
    if ((r.phase == Phase::Begin || r.phase == Phase::Gather) && r.eatWaitMs < kRestCapMs)
    {
        // Under the light lines only while the lead bot itself is seated or the party is under them.
        bool const mealHolds = lightAhead ? (f.leadResting && !Rested(f, lines)) || !Rested(f, light) : !Rested(f, lines);
        if (f.eating > 0 && mealHolds && !InEvent(r))
        {
            EnterPhase(r, Phase::Rest);
            r.restForJoiners = false;
            r.restLight = lightAhead;
            r.restForEating = true;
            r.restRepeatMs = 0;
            r.note = "resting: " + std::to_string(f.eating) + " eating or drinking";
            return order;
        }
    }

    // A big pack in sight with the light rest lines on and the estimate off: rested to the full lines first, once a
    // step (not idle time, as the aggro estimate's rest).
    if (f.lightRest && bigHere && r.bigPackStep != r.step && (r.phase == Phase::Begin || r.phase == Phase::Gather) &&
        NeedsRest(f, lines) && !InEvent(r))
    {
        r.bigPackStep = r.step;
        EnterPhase(r, Phase::Rest);
        r.restForEating = false;
        r.restLight = false;
        r.restForJoiners = true;
        r.restRepeatMs = 0;
        r.note = "resting: a pack of " + std::to_string(OpenUnits(r, f.units)) + " units: the full rest lines first";
        order = "rest";
        return order;
    }
    // A follower a direct step left behind (OrderRefused): summoned before the
    // step goes out again. The module's direct check counts every bot, a held
    // one too (Run::hold); the wait below then sees it through HoldView, which
    // shows the bot being waited for. It stays held until it arrives, so a
    // refused or timed-out summon leaves it held where it was.
    if (!r.summonDue.empty())
    {
        order = "summon " + r.summonDue;
        r.summonWait = r.summonDue;
        r.summonWaitMs = 0;
        r.summonDue.clear();
        return order;
    }
    // ... then waits for that bot to stand at the lead bot (the summon is the
    // 3 s `.bot summon` flow: a step sent at once still finds it away), at most
    // kSummonWaitMs; then the step goes out again.
    if (!r.summonWait.empty())
    {
        const MemberFact* member = nullptr;
        for (const MemberFact& m : f.members)
            if (m.name == r.summonWait)
                member = &m;
        r.summonWaitMs += dt;
        if (member && member->yd > kArriveYd && r.summonWaitMs < kSummonWaitMs)
        {
            r.note = "direct step: waiting for " + r.summonWait + " (summoned)";
            return order;
        }
        bool const stillHeld = std::find(r.hold.begin(), r.hold.end(), r.summonWait) != r.hold.end();
        r.summonWait.clear();
        r.summonWaitMs = 0;
        EnterPhase(r, Phase::Begin);
        // A held bot that did not come (timed out, gone): it stays held; these
        // facts still show it, so the step goes out on the next tick, without it.
        if (stillHeld)
            return order;
    }
    // A refused direct step's straggler no summon is allowed for yet
    // (OrderRefused): the step goes out again after kDirectRetryMs, or as soon
    // as the glue's test finds it with the lead bot. No pause meanwhile: a
    // fight on the way runs the guard (CheckGuard) as on any other tick.
    if (!r.directWaitFor.empty())
    {
        const MemberFact* member = nullptr;
        for (const MemberFact& m : f.members)
            if (m.name == r.directWaitFor)
                member = &m;
        r.directWaitMs += dt;
        bool const arrived = member && f.directJoinRead && member->withLead && member->yd <= kGatherYd;
        if (!arrived && r.directWaitMs < kDirectRetryMs) // a held bot is not in these facts: the full wait
        {
            r.note = "direct step: waiting for " + r.directWaitFor + " to come to the lead bot";
            return order;
        }
        r.directWaitFor.clear();
        r.directWaitMs = 0;
        EnterPhase(r, Phase::Begin);
    }

    // A real player dead, a ghost, out of the instance or too far for a pull: the step waits for him (no order, no
    // pause). A gather or a walk under way starts again from the beginning once he is back.
    if (!r.walkBack)
    {
        std::string const playerHold = PlayerHoldNote(r, f, *s);
        if (!playerHold.empty())
        {
            if (r.phase != Phase::Begin)
                EnterPhase(r, Phase::Begin);
            // Bounded (review round 1): kPlayerHoldCapMs of one hold, then a pause with its reason.
            r.playerHoldMs += dt;
            if (r.playerHoldMs >= kPlayerHoldCapMs)
            {
                r.playerHoldMs = 0;
                PausePlayerHold(r, "waited " + std::to_string(kPlayerHoldCapMs / 60000) + " min: " + playerHold +
                    "; .bot run resume once the party is together");
                return order;
            }
            r.note = playerHold;
            return order;
        }
        r.playerHoldMs = 0;
    }

    if (WalkBack(r, f, dt, order))
        return order;

    // After a won fight: the next pack pulled from where the tank stands (FightForwardTick), else the step's rules.
    if (s->kind != PlanStepKind::Move)
        r.fightForwardDue = false;
    else if (FightForwardTick(r, f))
        return order;
    switch (s->kind)
    {
        case PlanStepKind::Move:
        case PlanStepKind::MoveDirect:
            MoveStep(r, f, *s, dt, order);
            break;
        case PlanStepKind::Pull:
            FightStep(r, f, *s, {}, dt, order);
            break;
        case PlanStepKind::Boss:
        {
            std::vector<std::uint32_t> killOrder = s->entries;
            killOrder.push_back(s->entry);
            FightStep(r, f, *s, killOrder, dt, order);
            break;
        }
        case PlanStepKind::Loot:
        case PlanStepKind::Open:
        case PlanStepKind::UseItem:
            TaskStep(r, f, *s, order);
            break;
        case PlanStepKind::WaitObject:
            if (r.skippedOpenStep != static_cast<std::size_t>(-1) && r.step == r.skippedOpenStep + 1)
            {
                NextStep(r, "wait for object " + std::to_string(s->entry) + " left out: the open before it was left out");
                break;
            }
            if (r.phase != Phase::Wait)
                EnterPhase(r, Phase::Wait);
            if (f.objectState == static_cast<int>(s->param))
                NextStep(r, "object " + std::to_string(s->entry) + " is in the state the plan waits for");
            else if (WaitObjectGoneIsDone(r, *s, f))
                NextStep(r, "object " + std::to_string(s->entry) + " is gone after it was seen: counted as done");
            else if (r.phaseMs >= s->timeoutS * 1000)
                Pause(r, "waited " + std::to_string(s->timeoutS) + " s for object " + std::to_string(s->entry) +
                    (f.objectState < 0 ? " (not found near its point)" : " (state " + std::to_string(f.objectState) + ")"));
            else
                r.note = "waiting for object " + std::to_string(s->entry);
            break;
        case PlanStepKind::WaitClear:
        {
            if (!f.seen)
            {
                ApproachHop(r, f, *s, dt, order, "spot to watch");
                break;
            }
            if (r.phase != Phase::Wait)
                EnterPhase(r, Phase::Wait);
            std::uint32_t const nearCount = WaitClearCount(*s, f);
            if (nearCount == 0)
                NextStep(r, "no hostile left near the spot");
            else if (r.phaseMs >= s->timeoutS * 1000)
            {
                // The wait ran out: bystanders left on other floors (every
                // remaining hostile non-engaged and |dz| > 10 from the spot)
                // finish the step instead of pausing it. Anything engaged or
                // on the spot's floor pauses exactly as before.
                if (WaitClearOffFloorOnly(*s, f))
                    NextStep(r, "the spot is clear on its floor: " + std::to_string(nearCount) +
                        " hostiles left on other floors (|dz| > 10)");
                else
                    Pause(r, "waited " + std::to_string(s->timeoutS) + " s; " + std::to_string(nearCount) +
                        " hostiles still near the spot");
            }
            else
                r.note = "waiting for the spot to clear";
            break;
        }
        case PlanStepKind::Rest:
            NextStep(r, "rested");
            break;
        case PlanStepKind::Ritual:
            RitualStep(r, f, *s, order);
            break;
        case PlanStepKind::Gossip:
            GossipStep(r, f, *s, dt, order);
            break;
        case PlanStepKind::UseOn:
            UseOnStep(r, f, *s, order);
            break;
        case PlanStepKind::Hold:
            HoldStep(r, f, *s, dt, order);
            break;
        default:
            Pause(r, std::string("step kind ") + ai::PlanStepKindName(s->kind) +
                " is not supported yet: do it by hand, then .bot run skip and .bot run resume");
            break;
    }
    return order;
}

// The run's phase as the status line and the dashboard show it: during an
// event's hold, how far the event is ("hold 1/4 down", "fight 2/4 down": units
// of the event dead of all seen), else the phase's name.
inline std::string PhaseText(const Run& r)
{
    const PlanStep* s = Current(r);
    if (s && s->kind == PlanStepKind::Hold && r.eventOn && r.eventTotal && r.state == State::Running)
        return std::string(r.fighting ? "fight " : "hold ") + std::to_string(r.eventDown) + "/" +
            std::to_string(r.eventTotal) + " down";
    return PhaseName(r.phase);
}

// The status line for the commander:
//   TBM:RUN|<plan>|<state>|<step>/<steps>|<leg>|<kind>|<phase>|<note>[|left <K>][|speed <x> (test)]
// <step> counts from 1 (0 before the first, steps+1 is never sent: a done run
// shows the last). '|' and line breaks in the note become spaces.
inline std::string StatusLine(const Run& r)
{
    std::size_t const total = r.plan ? r.plan->steps.size() : 0;
    std::size_t const shown = total == 0 ? 0 : (r.step < total ? r.step + 1 : total);
    const PlanStep* s = r.plan && shown ? &r.plan->steps[shown - 1] : nullptr;
    std::string note = r.note;
    for (char& c : note)
        if (c == '|' || c == '\n' || c == '\r')
            c = ' ';
    std::string leg = s ? r.plan->LegName(s->leg) : std::string();
    if (leg.empty() && s)
        leg = std::to_string(s->leg);
    // Packs the run deferred and left alive (given up, still deferred, a retry under way): an
    // eighth field, only when there are any, after the note, so a reader of the seven fields
    // still reads the step (n/m) and the note (it ends with "|left K" there).
    std::size_t const left = r.deferGivenUp.size() + r.deferredPulls.size() + (DeferRetryOn(r) ? 1 : 0);
    // The test speed (SpeedLabel), only above 1.0: last, so a reader of the seven fields still reads the note.
    return "TBM:RUN|" + (r.plan ? r.plan->name : std::string("-")) + "|" + StateName(r.state) + "|" +
        std::to_string(shown) + "/" + std::to_string(total) + "|" + (leg.empty() ? "-" : leg) + "|" +
        (s ? ai::PlanStepKindName(s->kind) : "-") + "|" + PhaseText(r) + "|" + (note.empty() ? "-" : note) +
        (left ? "|left " + std::to_string(left) : std::string()) +
        (SpeedLabel(r).empty() ? std::string() : "|" + SpeedLabel(r));
}

// Which steps the run leader does today; the rest pause the run.
inline bool Supported(PlanStepKind kind)
{
    return kind != PlanStepKind::None;
}

} // namespace DungeonRun
} // namespace TortoiseBots
