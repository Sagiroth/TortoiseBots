// Standalone check for the run leader's Uldaman run 4 fixes (2026-10-17, runtime/DungeonRunPolicy.h):
//   g++ -std=c++17 -Wall -Wextra tools/test_run_leader_sentinel_policy.cpp -o /tmp/test_run_leader_sentinel_policy && /tmp/test_run_leader_sentinel_policy
//  - the tank's reach for a pull stops once a mob has aggro (CheckReach),
//  - a walk back replays every direct step the lead bot has not crossed (route checkpoint), from the last point
//    before the first of them,
//  - a dead bot is revived first, before any walk back point or direct step (FallenFirst).
// The healer's stay during a reach and its heal-only stance are glue and AI rules (tools/test_run_leader_wiring.py,
// tools/test_run_healer_policy.cpp).
#include "../runtime/DungeonRunPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::DungeonRun;
using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

static PlanStep Step(PlanStepKind kind, float x = 0, float y = 0, float z = 0)
{
    PlanStep s;
    s.kind = kind;
    s.x = x;
    s.y = y;
    s.z = z;
    ai::NormalizePlanStep(s);
    return s;
}

static PlanStep PullStep(float x, std::vector<std::uint32_t> entries)
{
    PlanStep s;
    s.kind = PlanStepKind::Pull;
    s.x = x;
    s.entries = entries;
    ai::NormalizePlanStep(s);
    return s;
}

static Facts Party(float x = 0)
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.leadX = x;
    f.carrier = "Tanky";
    f.seen = true;
    return f;
}

static MemberFact Mem(const std::string& name, float yd)
{
    MemberFact m;
    m.name = name;
    m.yd = yd;
    m.x = yd;
    return m;
}

static UnitFact Unit(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u;
    u.guid = guid;
    u.entry = entry;
    u.distLead = dist;
    return u;
}

// Run 4, sentinel pull 2: the tank walked on toward its pull target after other mobs of the camp had aggro.
static void TestReachStopsOnAggro()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {4847})};
    Run r = Start(&plan);
    Facts f = Party(60);
    f.units = {Unit(7, 4847, 40)};
    CHECK(Tick(r, f) == "pullback 7");
    CHECK(r.phase == Phase::AwaitPull && !r.reachStopped);
    // The tank is walking in and nobody has aggro: it goes on.
    f.anyInCombat = true;
    f.leadReaching = true;
    f.reachAggro = 0;
    Tick(r, f);
    CHECK(!r.stopReachDue && !r.reachStopped);
    // A mob of the camp has aggro on someone: the reach stops, once.
    f.reachAggro = 2;
    Tick(r, f);
    CHECK(r.stopReachDue && r.reachStopped && r.note.find("2 mobs have aggro") != std::string::npos);
    r.stopReachDue = false; // the glue consumed it
    Tick(r, f);
    CHECK(!r.stopReachDue);
    // Not reaching any more (the target landed on the tank, it turned back): nothing to stop.
    Run q = Start(&plan);
    Facts g = Party(60);
    g.units = {Unit(7, 4847, 40)};
    Tick(q, g);
    g.anyInCombat = true;
    g.reachAggro = 3;
    g.leadReaching = false;
    Tick(q, g);
    CHECK(!q.stopReachDue && !q.reachStopped);
    // A dead lead bot stops nothing.
    g.leadReaching = true;
    g.leadAlive = false;
    Tick(q, g);
    CHECK(!q.stopReachDue);
    // The next pull arms it again.
    r.reachStopped = true;
    r.fighting = false;
    EnterPhase(r, Phase::Begin);
    f.anyInCombat = false;
    f.reachAggro = 0;
    f.leadReaching = false;
    f.units = {Unit(8, 4847, 40)};
    std::string next;
    for (int i = 0; i < 3 && next.empty(); ++i)
        next = Tick(r, f);
    CHECK(next == "pullback 8" && !r.reachStopped);
}

// ta265 r4: a mob within kReachPackYd of the pull target is the target's own pack (the core's assistance radius);
// its aggro does not stop the tank's reach, so the pullback still brings the pack to the party. One farther away
// (another camp, the sentinel at 15.7 yd) still stops it.
static void TestReachIgnoresThePullTargetsPack()
{
    CHECK(!ReachAggroCounts(0));
    CHECK(!ReachAggroCounts(9.9f));
    CHECK(!ReachAggroCounts(kReachPackYd));
    CHECK(ReachAggroCounts(10.1f));
    CHECK(ReachAggroCounts(15.7f));
}

// Run 4, 13:5x: after the tank's revive the walk back replayed the plan from step 1 (528 yd) through Baelog's wing;
// only the navmesh hole (the direct steps) needs the plan's points.
static void TestWalkBackStartsBeforeTheFirstDirectStep()
{
    DungeonPlan plan;
    // 0..2 west of the hole, 3-4 the direct hops over it, 5..6 east of it, 7 the pull.
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 50), Step(PlanStepKind::Move, 90),
        Step(PlanStepKind::MoveDirect, 100), Step(PlanStepKind::MoveDirect, 110), Step(PlanStepKind::Move, 150),
        Step(PlanStepKind::Move, 190), PullStep(230, {1})};
    Run r = Start(&plan, 7);
    Facts f = Party(0); // the entrance
    f.seen = false;
    StartWalkBack(r, f);
    // Straight to the point before the first direct step (idx 2), not from step 1.
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 2);
    CHECK(r.note.find("straight to step 3") != std::string::npos);
    // At that point the plan's own points go on: the direct hops.
    f.seen = true;
    f.leadX = 90;
    CHECK(Tick(r, f).empty()); // arrived at idx 2
    CHECK(r.walkBackIdx == 3 && r.checkpoint == 2 && r.routeKnown);
    CHECK(Tick(r, f) == "move 100.0 0.0 0.0 direct");

    // A lead bot known east of the hole (its checkpoint is step 6) walks straight to the last point.
    Run e = Start(&plan, 7);
    NoteCheckpoint(e, 5);
    Facts g = Party(140);
    g.seen = false;
    StartWalkBack(e, g);
    CHECK(e.walkBack && e.walkBackDirect && e.walkBackIdx == 6);

    // A lead bot known between the hops (it reached the first one): the second hop is replayed, from the first.
    Run d = Start(&plan, 7);
    NoteCheckpoint(d, 3);
    Facts h = Party(104);
    h.seen = false;
    StartWalkBack(d, h);
    CHECK(d.walkBack && d.walkBackIdx == 3);

    // Two holes: the first one decides where the replay starts.
    DungeonPlan two;
    two.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 40), Step(PlanStepKind::MoveDirect, 50),
        Step(PlanStepKind::Move, 80), Step(PlanStepKind::MoveDirect, 100), Step(PlanStepKind::Move, 140),
        PullStep(230, {1})};
    Run t = Start(&two, 6);
    Facts m = Party(0);
    m.seen = false;
    StartWalkBack(t, m);
    CHECK(t.walkBack && t.walkBackDirect && t.walkBackIdx == 1);
}

// The folded route (Uldaman doubles back): 0-1 before the hole, 2-3 the direct hops over it, 4-5 past it, 6 a later
// point that folds back to (160, 0), 7 the pull. The party's checkpoint is step 6 (index 5) when the lead bot dies.
static DungeonPlan FoldedPlan()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0, 0), Step(PlanStepKind::Move, 40, 0), Step(PlanStepKind::MoveDirect, 50, 10),
        Step(PlanStepKind::MoveDirect, 60, 20), Step(PlanStepKind::Move, 100, 100), Step(PlanStepKind::Move, 150, 150),
        Step(PlanStepKind::Move, 160, 0), PullStep(200, {1})};
    plan.steps[7].y = 200;
    return plan;
}

static Facts At(float x, float y, bool alive = true)
{
    Facts f = Party(x);
    f.leadY = y;
    f.leadAlive = alive;
    f.seen = false;
    return f;
}

// The lead bot dies at its checkpoint (150, 150) and comes back to life at (x, y).
static Run DiesAndRevivesAt(const DungeonPlan& plan, float x, float y)
{
    Run r = Start(&plan, 7);
    NoteCheckpoint(r, 5);
    NoteLeadDeath(r, At(150, 150));        // alive at the checkpoint
    NoteLeadDeath(r, At(150, 150, false)); // dead
    CHECK(r.routeCheck);
    NoteLeadDeath(r, At(x, y));            // alive again
    r.leadDied = false;                    // ... and regrouped (the regroup ends the death)
    return r;
}

// Codex round 2 on #1079: the lead bot revived more than 60 yd from the plan's first point, a later folded point is
// the nearest and reachable, and the recovery must still replay the earlier direct steps.
static void TestFoldedRouteReplaysTheEarlierHole()
{
    DungeonPlan plan = FoldedPlan();
    Facts f = At(165, 5);
    f.walkBackRescue = false;
    Run r = DiesAndRevivesAt(plan, 165, 5);
    CHECK(!r.routeKnown); // came back away from where it fell: where it is on the route is not known
    CHECK(ai::PlanDistance(f.leadX, f.leadY, f.leadZ, 0, 0, 0) > kSeeYd);
    CHECK(NearestMovePoint(r, f) == 6); // by distance the fold point is the nearest
    f.approachKnown = true;             // ... and reachable
    StartWalkBack(r, f);
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 1 && r.walkBackStart == 1);
    // The straight way to step 2 fails: the replay from the nearest point stays before the hole, never step 7.
    f.approachKnown = false;
    Tick(r, f);
    CHECK(r.walkBack && !r.walkBackDirect && r.walkBackIdx < 2);
    // The replay crosses the hole in plan order.
    Facts at1 = At(40, 0);
    at1.seen = true;
    r.walkBackIdx = 1;
    EnterPhase(r, Phase::Begin);
    CHECK(Tick(r, at1).empty());
    CHECK(r.walkBackIdx == 2 && r.checkpoint == 1);
    CHECK(Tick(r, at1) == "move 50.0 10.0 0.0 direct");

    // The full wipe recovery: the lead bot dies in a fight at its checkpoint, `revive party` brings it back at a
    // spot where the fold point is nearest; once the party is gathered the walk back starts before the hole.
    Run w = Start(&plan, 7);
    NoteCheckpoint(w, 5);
    Facts alive = At(150, 150);
    NoteLeadDeath(w, alive); // the living lead bot seen at its checkpoint
    Facts dead = alive;
    dead.leadAlive = false;
    dead.dead = 1;
    dead.alive = 4;
    dead.deadNames = {"Tanky"};
    std::string sent;
    for (int i = 0; i < 3 && sent != "revive party"; ++i)
        sent = Tick(w, dead);
    CHECK(sent == "revive party" && w.regroup && w.walkBackDue);
    Facts back = At(165, 5);
    back.walkBackRescue = false; // the replay path pinned: the rescue below covers the teleport
    back.members = {Mem("Tanky", 0), Mem("Bearcaller", 3)};
    back.approachKnown = true;
    for (int i = 0; i < 6 && w.regroup; ++i)
        Tick(w, back);
    CHECK(!w.regroup && w.walkBack && w.walkBackIdx == 1 && !w.routeKnown);
    // The same wipe with the rescue on: the hole replay is a rescue, armed bot-only, replaying on refusal.
    Run v = Start(&plan, 7);
    NoteCheckpoint(v, 5);
    Facts alive2 = At(150, 150);
    NoteLeadDeath(v, alive2);
    Facts dead2 = alive2;
    dead2.leadAlive = false;
    dead2.dead = 1;
    dead2.alive = 4;
    dead2.deadNames = {"Tanky"};
    std::string sent2;
    for (int i = 0; i < 3 && sent2 != "revive party"; ++i)
        sent2 = Tick(v, dead2);
    CHECK(sent2 == "revive party" && v.regroup && v.walkBackDue);
    Facts on2 = At(165, 5);
    on2.members = {Mem("Tanky", 0), Mem("Bearcaller", 3)};
    on2.approachKnown = true;
    for (int i = 0; i < 6 && v.regroup; ++i)
        Tick(v, on2);
    CHECK(!v.regroup && v.strandDue && v.strandIdx == 6 && v.strandBotOnly && v.strandReplay && !v.routeKnown);
    CHECK(v.note.find("a direct step is on the way (step 3)") != std::string::npos);
}

// Codex round 3 on #1079: no failure passes a direct step the lead bot has not crossed (its hole would be skipped), and
// a detour that stands in for a direct step's point is no crossing.
static void TestUncrossedDirectStepIsNeverPassed()
{
    DungeonPlan plan = FoldedPlan();
    // No navmesh approach to the first hop (out of sight): the run pauses, it does not go on to the second hop.
    Run r = Start(&plan, 7);
    NoteCheckpoint(r, 1);
    r.walkBack = true;
    r.walkBackIdx = 2;
    r.walkBackStart = 1;
    EnterPhase(r, Phase::Begin);
    Facts f = At(-100, 0);
    f.approachKnown = false;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.walkBackIdx == 2 && r.note.find("not passed") != std::string::npos);
    // A path refusal of the hop's own move: the same.
    Run q = Start(&plan, 7);
    NoteCheckpoint(q, 1);
    q.walkBack = true;
    q.walkBackIdx = 2;
    EnterPhase(q, Phase::Begin);
    OrderRefused(q, "move 50.0 10.0 0.0 direct", "no-los: no sight line");
    CHECK(q.state == State::Paused && q.walkBackIdx == 2);
    // A direct step the lead bot has crossed (at or before its checkpoint) is passed as any point.
    Run c = Start(&plan, 7);
    NoteCheckpoint(c, 3);
    c.walkBack = true;
    c.walkBackIdx = c.walkBackStart = 2;
    c.walkBackRestarted = true;
    EnterPhase(c, Phase::Begin);
    Tick(c, f);
    CHECK(c.state == State::Running && c.walkBackIdx == 3);
    // A plain move point that cannot be approached is still passed.
    Run m = Start(&plan, 7);
    m.walkBack = true;
    m.walkBackIdx = 4;
    m.walkBackStart = 1;
    EnterPhase(m, Phase::Begin);
    Tick(m, f);
    CHECK(m.state == State::Running && m.walkBackIdx == 5);

    // A direct hop that stalls 5 yd short of its point ends its walk, but is no crossing: the checkpoint stays.
    Run st = Start(&plan, 7);
    NoteCheckpoint(st, 1);
    st.walkBack = true;
    st.walkBackIdx = st.walkBackStart = 2;
    EnterPhase(st, Phase::Begin);
    Facts shortOf = At(45, 8); // 5.4 yd from (50, 10)
    shortOf.seen = true;
    for (int i = 0; i < 20 && st.walkBackIdx == 2 && st.state == State::Running; ++i)
        Tick(st, shortOf);
    CHECK(st.walkBackIdx == 3 && st.checkpoint == 1);

    // A detour for the first hop (its point off the mesh): reaching the stand-in leaves the checkpoint before the
    // hole, so a wipe after it still replays both hops.
    Run d = Start(&plan, 2);
    NoteCheckpoint(d, 1);
    CHECK(UseDetour(d, 45, 8, 0));
    Facts there = At(45, 8);
    there.seen = true;
    for (int i = 0; i < 3 && d.step == 2; ++i)
        Tick(d, there);
    CHECK(d.step == 3 && d.checkpoint == 1);
}

// Codex round 4 on #1079: the revive spot on the folded last point itself (index 6, (160, 0)) before the holes.
static void TestReviveOnTheFoldedLastPointStillReplays()
{
    DungeonPlan plan = FoldedPlan();
    Run r = DiesAndRevivesAt(plan, 161, 0);
    CHECK(!r.routeKnown);
    StartWalkBack(r, At(161, 0));
    CHECK(r.walkBack && r.walkBackIdx == 1 && !r.routeKnown);
}

// A revive at the entrance (14:03 and 14:39: (-229, 46)): before every hole, the replay starts before the first.
static void TestEntranceReviveReplaysEveryHole()
{
    DungeonPlan plan = FoldedPlan();
    Run r = DiesAndRevivesAt(plan, 2, 1);
    CHECK(!r.routeKnown);
    Facts off = At(2, 1);
    off.walkBackRescue = false; // the replay path pinned: the rescue below covers the teleport
    StartWalkBack(r, off);
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 1);
    // The same revive with the rescue on: past the hole the safe point is a rescue, not a replay.
    Run s = DiesAndRevivesAt(plan, 2, 1);
    Facts on = At(2, 1);
    StartWalkBack(s, on);
    CHECK(s.strandDue && s.strandBotOnly && s.strandReplay);
    CHECK(s.walkBack && s.note.find("a direct step is on the way") != std::string::npos);
}

// Raised where it fell, past every hole: no replay of the holes, straight to the last point; a fallback replay stays
// on the checkpoint's stretch.
static void TestRevivePastAllHolesNeedsNoReplay()
{
    DungeonPlan plan = FoldedPlan();
    Run r = DiesAndRevivesAt(plan, 152, 150);
    CHECK(r.routeKnown && r.checkpoint == 5);
    Facts f = At(152, 150);
    StartWalkBack(r, f);
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 6);
    f.approachKnown = false;
    Tick(r, f); // the straight way fails: the replay starts at the checkpoint, not behind the hole
    CHECK(r.walkBack && r.walkBackIdx >= 5);

    // A pause with the lead bot left where it stood keeps the checkpoint; a lead bot moved away loses it.
    Run p = Start(&plan, 7);
    NoteCheckpoint(p, 5);
    NoteLeadDeath(p, At(150, 150));
    Pause(p, "test");
    NoteLeadDeath(p, At(151, 150));
    CHECK(p.routeKnown && !p.routeCheck);
    Run q = Start(&plan, 7);
    NoteCheckpoint(q, 5);
    NoteLeadDeath(q, At(150, 150));
    Pause(q, "test");
    NoteLeadDeath(q, At(165, 5));
    CHECK(!q.routeKnown);
    Facts qoff = At(165, 5);
    qoff.walkBackRescue = false; // the replay path pinned: the rescue below covers the teleport
    StartWalkBack(q, qoff);
    CHECK(q.walkBack && q.walkBackIdx == 1);
    // The same spot with the rescue on: past the hole the safe point is a rescue, not a replay. A resume ends the
    // pause first (a rescue only fires while running), then the regrouped party walks back the same way.
    Run t = Start(&plan, 7);
    NoteCheckpoint(t, 5);
    NoteLeadDeath(t, At(150, 150));
    Pause(t, "test");
    NoteLeadDeath(t, At(165, 5));
    CHECK(Resume(t));
    StartWalkBack(t, At(165, 5));
    CHECK(t.strandDue && t.strandBotOnly && t.strandReplay);
    CHECK(t.walkBack && t.note.find("a direct step is on the way") != std::string::npos);
}

// No direct step in the plan: the old rule, whatever the checkpoint.
static void TestNoDirectStepKeepsTheOldWalkBack()
{
    DungeonPlan flat;
    flat.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 90), Step(PlanStepKind::Move, 150),
        PullStep(230, {1})};
    Run p = Start(&flat, 3);
    StartWalkBack(p, At(0, 0));
    CHECK(p.walkBack && p.walkBackDirect && p.walkBackIdx == 2);
    Run k = Start(&flat, 3);
    NoteCheckpoint(k, 1);
    StartWalkBack(k, At(0, 0));
    CHECK(k.walkBack && k.walkBackDirect && k.walkBackIdx == 2);
    Run n = Start(&flat, 3);
    StartWalkBack(n, At(220, 0)); // near the step
    CHECK(!n.walkBack);
    Run o = Start(&flat, 3);
    StartWalkBack(o, At(151, 0)); // on the last point
    CHECK(!o.walkBack && o.checkpoint == 2 && o.routeKnown);
}

// 14:03: a bot that died after the regroup was over (the rogue on his corpse run) held the walk back at the pit's
// direct step until a player revived and summoned him. No walk back point and no direct step goes out with a dead bot.
static void TestDeadBotIsRevivedBeforeAWalkBackOrADirectStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::MoveDirect, 20), Step(PlanStepKind::MoveDirect, 30),
        Step(PlanStepKind::Move, 70), PullStep(100, {1})};
    Run r = Start(&plan, 4);
    r.walkBack = true;
    r.walkBackDirect = true;
    r.walkBackIdx = 1;
    EnterPhase(r, Phase::Begin);
    Facts f = Party(15);
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Quickseeker"};
    f.members = {Mem("Tanky", 0), Mem("Bearcaller", 3)};
    std::string order = Tick(r, f);
    CHECK(order != "move 20.0 0.0 0.0 direct");
    CHECK(r.regroup && r.note.find("Quickseeker") != std::string::npos);
    CHECK(r.walkBack && r.walkBackIdx == 1); // the walk back goes on after the regroup
    CHECK(Tick(r, f) == "revive party");
    // He is back with the party: the regroup ends and the direct step goes out.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Bearcaller", 3), Mem("Quickseeker", 4)};
    f.maxMemberYd = 4;
    std::string sent;
    for (int i = 0; i < 6 && sent.empty(); ++i)
        sent = Tick(r, f);
    CHECK(!r.regroup && r.walkBack && sent == "move 20.0 0.0 0.0 direct");

    // The same for a direct step of the plan itself.
    Run s = Start(&plan, 1);
    Facts g = Party(18);
    g.dead = 1;
    g.alive = 4;
    g.deadNames = {"Quickseeker"};
    g.members = {Mem("Tanky", 0)};
    order = Tick(s, g);
    CHECK(order.find("direct") == std::string::npos && s.regroup);
    CHECK(Tick(s, g) == "revive party");

    // Not in a fight (the fight is the bots' own), and not twice.
    Run c = Start(&plan, 1);
    Facts h = Party(18);
    h.dead = 1;
    h.alive = 4;
    h.anyInCombat = true;
    h.deadNames = {"Quickseeker"};
    Tick(c, h);
    CHECK(!c.regroup);
}

int main()
{
    TestReachStopsOnAggro();
    TestReachIgnoresThePullTargetsPack();
    TestWalkBackStartsBeforeTheFirstDirectStep();
    TestFoldedRouteReplaysTheEarlierHole();
    TestEntranceReviveReplaysEveryHole();
    TestReviveOnTheFoldedLastPointStillReplays();
    TestUncrossedDirectStepIsNeverPassed();
    TestRevivePastAllHolesNeedsNoReplay();
    TestNoDirectStepKeepsTheOldWalkBack();
    TestDeadBotIsRevivedBeforeAWalkBackOrADirectStep();
    std::cout << "ok\n";
    return 0;
}
