// Standalone check for the run leader's Uldaman run 4 fixes (2026-10-17, runtime/DungeonRunPolicy.h):
//   g++ -std=c++17 -Wall -Wextra tools/test_run_leader_sentinel_policy.cpp -o /tmp/test_run_leader_sentinel_policy && /tmp/test_run_leader_sentinel_policy
//  - the tank's reach for a pull stops once a mob has aggro (CheckReach),
//  - a walk back replays the plan's points only from the last point before the first direct step,
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
    CHECK(r.walkBackIdx == 3);
    CHECK(Tick(r, f) == "move 100.0 0.0 0.0 direct");

    // A lead bot already east of the hole walks straight to the last point.
    Run e = Start(&plan, 7);
    Facts g = Party(140);
    g.seen = false;
    StartWalkBack(e, g);
    CHECK(e.walkBack && e.walkBackDirect && e.walkBackIdx == 6);

    // A lead bot on the direct step itself starts at it.
    Run d = Start(&plan, 7);
    Facts h = Party(104);
    h.seen = false;
    StartWalkBack(d, h);
    CHECK(d.walkBack && d.walkBackIdx >= 3);

    // Without a direct step on the way: straight to the last point, as before.
    DungeonPlan flat;
    flat.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 90), PullStep(230, {1})};
    Run p = Start(&flat, 2);
    Facts k = Party(0);
    k.seen = false;
    StartWalkBack(p, k);
    CHECK(p.walkBack && p.walkBackDirect && p.walkBackIdx == 1);

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
    TestWalkBackStartsBeforeTheFirstDirectStep();
    TestDeadBotIsRevivedBeforeAWalkBackOrADirectStep();
    std::cout << "ok\n";
    return 0;
}
