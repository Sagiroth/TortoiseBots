// Standalone check for runtime/DungeonRunPolicy.h (the run leader's step machine):
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_run_policy.cpp -o /tmp/test_dungeon_run_policy && /tmp/test_dungeon_run_policy
#include "../runtime/DungeonRunPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"

#include <cstdlib>
#include <memory>
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
namespace RemoteCommander = TortoiseBots::RemoteCommander;

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

static PlanStep PullStep(float x, std::vector<std::uint32_t> entries, std::uint32_t flags = 0)
{
    PlanStep s;
    s.kind = PlanStepKind::Pull;
    s.x = x;
    s.entries = entries;
    s.flags = flags;
    ai::NormalizePlanStep(s);
    return s;
}

// One tick as the glue runs it (TickDungeonRun) for the approach stand: Tick, then,
// when it asked for one (Run::approachStandWanted), NearestReachablePoint's answer
// (`stand`: found at sx sy sz; ApproachStandFound).
static std::string StandTick(Run& r, const Facts& f, bool stand = false, float sx = 0, float sy = 0, float sz = 0)
{
    std::string const order = Tick(r, f);
    if (r.approachStandWanted)
        ApproachStandFound(r, f, stand, sx, sy, sz);
    return order;
}

static Facts Party(float x = 0)
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.leadX = x;
    f.carrier = "Tanky";
    f.seen = true;  // the step's point within kSeeYd; tests of far steps clear it
    return f;
}

// A living member `yd` from the lead bot, standing on the x axis at that
// distance (the stand-still watch reads its position).
static MemberFact Mem(const std::string& name, float yd, float dz = 0.0f)
{
    MemberFact m;
    m.name = name;
    m.yd = yd;
    m.dz = dz;
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

static void TestMove()
{
    DungeonPlan plan;
    plan.name = "t";
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::MoveDirect, 52, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    CHECK(r.phase == Phase::Walk);
    // Progress: no new order.
    f.leadX = 20;
    CHECK(Tick(r, f).empty());
    // Arrived: next step, the direct move goes out at once.
    f.leadX = 48;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1);
    CHECK(Tick(r, f) == "move 52.0 0.0 0.0 direct");
    f.leadX = 52;
    Tick(r, f);
    CHECK(r.state == State::Done);
    CHECK(StatusLine(r) == "TBM:RUN|t|done|2/2|0|move_direct|begin|plan done");
}

static void TestWalkStuckIsBounded()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    int orders = 0;
    for (int i = 0; i < 200 && r.state == State::Running; ++i)
        if (!Tick(r, f).empty())
            ++orders;
    CHECK(r.state == State::Paused);
    CHECK(orders == static_cast<int>(kWalkTries));
    CHECK(r.note.find("stuck") != std::string::npos);
}

// Live (WC run B, rows 28/30/31 and 20 more): a plan row 3-6 yd from the tank
// costs ~13 s -- `cmd move` to a point that close does not move the lead bot at
// all, and the walk waited 12 s before it counted it as arrived. 275 s of a
// 101 min run. A plain walk that gains nothing is done after 3 s now.
static void TestShortHopArrivesWithoutTheRetryWait()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 5, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0");
    CHECK(r.step == 0);
    for (int i = 0; i < 4 && r.step == 0 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1 && r.note == "arrived");
    // The anchor is where the lead bot stands, not the point it could not reach.
    CHECK(r.anchorKnown && r.anchorX == 0.0f);
}

// A direct step is the plan's own line over a navmesh hole: it keeps the 12 s
// rule (and its checkpoint), so a hop it cannot finish is not cut short.
static void TestShortDirectHopKeepsTheLongWait()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
    for (int i = 0; i < 11; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(r.step == 0);
    }
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note == "arrived");
}

// ... and a hop the lead bot does walk is not cut short: the walk goes on while
// it gains ground (more than 2 yd a look), and it arrives at its point.
static void TestShortHopThatMovesIsNotCut()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 20, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(26); // 6 yd out: inside kStalledArriveYd, not at the point
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0");
    f.leadX = 23.5f;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && !r.anchorKnown);
    f.leadX = 21.0f;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note == "arrived");
    CHECK(r.anchorKnown && r.anchorX == 20.0f); // arrived at the plan's point, not on its own spot
}

// Live bug: a move point just off the mesh; the lead bot stopped 4.6 yd short.
static void TestStalledNearPointCountsAsArrived()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(!Tick(r, f).empty());
    f.leadX = 45.4f;
    for (int i = 0; i < 30 && r.step == 0 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1 && r.note == "arrived");
    // Still farther than kStalledArriveYd: retries, then the stuck pause.
    Run r2 = Start(&plan);
    f.leadX = 0;
    Tick(r2, f);
    f.leadX = 40;
    for (int i = 0; i < 200 && r2.state == State::Running; ++i)
        Tick(r2, f);
    CHECK(r2.state == State::Paused && r2.note.find("stuck") != std::string::npos);
}

// A plain or direct walk to (0, 0, 0) whose lead bot stands still at (leadX, 0, leadZ) from the
// first tick: `arrived` when the step ended "arrived", `stuck` when the run paused stuck.
static void RunStalledWalk(float leadX, float leadZ, PlanStepKind kind, bool& arrived, bool& stuck)
{
    DungeonPlan plan;
    plan.steps = {Step(kind, 0, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(leadX);
    f.leadZ = leadZ;
    Tick(r, f);
    for (int i = 0; i < 300 && r.step == 0 && r.state == State::Running; ++i)
        Tick(r, f);
    arrived = r.state == State::Running && r.step == 1 && r.note == "arrived";
    stuck = r.state == State::Paused && r.note.find("stuck") != std::string::npos;
}

// Live (Ragefire Chasm r6-19z24 slot 1 step 38, r6 slot 1b step 70, r2-19z19 slot 1 step 53): the stand
// stood 3-4 yd above or below the lead bot on a ledge, the whole party within 7 yd; the 3D distance was
// 6.0-6.7 yd, over kStalledArriveYd, so the walk retried three times and paused ("the lead bot is stuck
// 6.7 yd from ...") for 70 s to 4 min, and one resume fixed it. A stand within kStandZTolYd above or below
// counts by its ground distance: the stalled arrival ends the walk.
static void TestStandOnALedgeCountsByGroundDistance()
{
    bool arrived = false, stuck = false;
    // r6-19z24: 6.0 yd, the stand 3 yd lower (ground 5.2 yd).
    RunStalledWalk(5.2f, 3.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // r6 slot 1b: 6.7 yd, the lead 4 yd above the target (ground 5.4 yd).
    RunStalledWalk(5.4f, 4.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // r2-19z19: 6.5 yd, the lead 3.6 yd above, and a stand below the lead bot.
    RunStalledWalk(5.4f, 3.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    RunStalledWalk(-5.4f, -3.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // The edge of the tolerance counts (6.0 yd on the ground, 4.5 above).
    RunStalledWalk(5.9f, kStandZTolYd, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // A different floor (more than kStandZTolYd above or below) keeps the 3D rule
    // (TestStalledLeadInFollowRangeCountsAsArrived).
    RunStalledWalk(5.5f, kStandZTolYd + 0.5f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    RunStalledWalk(4.0f, 5.5f, PlanStepKind::Move, arrived, stuck); // 6.8 yd in 3D, steeper than the slope bound
    CHECK(!arrived && stuck);
    // Farther than kStalledArriveYd on the ground plane is still stuck.
    RunStalledWalk(6.5f, 3.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    // A direct step keeps the 3D rule.
    RunStalledWalk(5.4f, 4.0f, PlanStepKind::MoveDirect, arrived, stuck);
    CHECK(!arrived && stuck);
}

// Blackfathom Deeps 2026-10-23 (BFD-STUCK-POINTS-2026-10-23): every "lead bot is stuck"
// pause began with the lead bot in a deep pool and its own move failing every 2 s, and
// the note could not say so. It names the lead bot's water state, the path type the
// acked `cmd move` saw, and whether its own move is failing and why.
static void TestStuckPauseNamesTheLeadsWaterState()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(16.0f);
    f.leadInWater = true;
    std::string const order = Tick(r, f);
    CHECK(order == "move 0.0 0.0 0.0");
    NoteMovePathType(r, order, 0x81);
    for (int i = 0; i < 300 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused);
    CHECK(r.note == "the lead bot is stuck 16.0 yd from 0.0 0.0 0.0 (lead in water: yes; cmd move path type 0x81; "
        "its own move not failing)");

    // The note's other words.
    Run q = Start(&plan);
    Facts g = Party(16.0f);
    CHECK(StuckNote(q, g) == " (lead in water: no; cmd move path type not read; its own move not failing)");
    g.leadMoveFailed = true;
    g.leadMoveFailReason = "dispatch-short";
    NoteMovePathType(q, "move 1.0 2.0 3.0", 0x11);
    CHECK(StuckNote(q, g) == " (lead in water: no; cmd move path type 0x11; its own move failing: dispatch-short)");
    // A direct move and other orders do not change it.
    NoteMovePathType(q, "move 1.0 2.0 3.0 direct", 0x8);
    NoteMovePathType(q, "attack 0x0000000000000001", 0x8);
    CHECK(q.movePathType == 0x11);
    g.leadMoveFailReason.clear();
    CHECK(StuckNote(q, g) == " (lead in water: no; cmd move path type 0x11; its own move failing: none)");
    // A stay walk resting after failed walks is named, not an older fail reason
    // (Opus review round 2 of 442a2bdf).
    g.leadMoveFailReason = "empty-path";
    g.leadStayWalkResting = true;
    g.leadStayWalkFails = 4;
    CHECK(StuckNote(q, g) == " (lead in water: no; cmd move path type 0x11; its own move failing: stay walk resting "
        "after 4 failed walks)");
    // The stuck pause carries it.
    Run rest = Start(&plan);
    Facts h = Party(16.0f);
    h.leadMoveFailed = true;
    h.leadStayWalkResting = true;
    h.leadStayWalkFails = 3;
    for (int i = 0; i < 400 && rest.state == State::Running; ++i)
        Tick(rest, h);
    CHECK(rest.state == State::Paused && rest.note.find("stuck") != std::string::npos &&
        rest.note.find("stay walk resting after 3 failed walks") != std::string::npos);
}

// Ticks until the run sends an order (or stops running); at most `max` ticks.
static std::string NextOrder(Run& r, const Facts& f, int max = 400)
{
    for (int i = 0; i < max && r.state == State::Running; ++i)
    {
        std::string const order = Tick(r, f);
        if (!order.empty() || r.stallStandInWanted)
            return order;
    }
    return std::string();
}

// Blackfathom Deeps 2026-10-22/23: the lead bot in a deep pool, its own move failing
// every 2 s; the walk re-sent the same `cmd move` and paused, and the refusal path's
// stand-in never ran. A stall with the lead bot's move failing, out of combat, first
// sends the straight `direct` move, then asks the glue for a dry stand-in, then the
// old retries and pause.
static void TestStalledWalkWithAFailingMoveTriesDirectThenADryStandIn()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(16.0f);
    f.leadInWater = true;
    f.leadMoveFailed = true;
    f.leadMoveFailReason = "dispatch-short";
    // The glue read the line: a clear swim line (the BFD pool case).
    f.stallDirectRead = true;
    f.stallDirectClear = true;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    // The first stall: the direct move, not the same plain move again.
    CHECK(NextOrder(r, f) == "move 0.0 0.0 0.0 direct");
    CHECK(r.state == State::Running && r.walkTries == 1 && r.stallFallback == 1);
    CHECK(r.note == "the lead bot's own move to 0.0 0.0 0.0 failed (dispatch-short, in water): the straight move (direct) instead");
    // It did not help either: the stand-in search goes to the glue (no order this tick).
    CHECK(NextOrder(r, f).empty());
    CHECK(r.stallStandInWanted && r.stallFallback == 2 && r.state == State::Running);
    // The glue found a dry point 8 yd off: the step walks there.
    CHECK(StallStandIn(r, true, 8.0f, 0.0f, 0.0f));
    CHECK(!r.stallStandInWanted && r.detour);
    CHECK(r.note == "step 1: the lead bot's own move to 0.0 0.0 0.0 kept failing: walking to the stand-in on dry ground "
        "8.0 0.0 0.0, 8.0 yd from it, and going on from there");
    CHECK(NextOrder(r, f) == "move 8.0 0.0 0.0");
    // The stand-in's own walk stalls too: its direct move, then no second stand-in
    // (the old retries go on).
    CHECK(NextOrder(r, f) == "move 8.0 0.0 0.0 direct");
    CHECK(NextOrder(r, f) == "move 8.0 0.0 0.0");
    CHECK(!r.stallStandInWanted && r.stallFallback == 3 && r.walkTries == 2);
    f.leadX = 8.0f;
    for (int i = 0; i < 10 && r.step == 0; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);

    // No stand-in found: the walk's own retries go on, then the stuck pause with the note.
    Run q = Start(&plan);
    Facts g = f;
    g.leadX = 16.0f;
    CHECK(Tick(q, g) == "move 0.0 0.0 0.0");
    CHECK(NextOrder(q, g) == "move 0.0 0.0 0.0 direct");
    CHECK(NextOrder(q, g).empty() && q.stallStandInWanted);
    CHECK(!StallStandIn(q, false, 0, 0, 0));
    CHECK(q.note == "the lead bot's own move to 0.0 0.0 0.0 kept failing: no stand-in on dry ground within 25 yd of it");
    CHECK(Tick(q, g) == "move 0.0 0.0 0.0");
    CHECK(q.walkTries == 2);
    CHECK(NextOrder(q, g) == "move 0.0 0.0 0.0");
    CHECK(q.walkTries == 3);
    CHECK(NextOrder(q, g).empty());
    CHECK(q.state == State::Paused);
    CHECK(q.note == "the lead bot is stuck 16.0 yd from 0.0 0.0 0.0 (lead in water: yes; cmd move path type not read; "
        "its own move failing: dispatch-short)");

    // The direct move refused: no pause, the stand-in next.
    Run d = Start(&plan);
    CHECK(Tick(d, g) == "move 0.0 0.0 0.0");
    std::string const direct = NextOrder(d, g);
    CHECK(direct == "move 0.0 0.0 0.0 direct");
    CHECK(!StallDirectRefused(d, "move 0.0 0.0 0.0", "too-far"));
    CHECK(StallDirectRefused(d, direct, "too-far: Healy is not with Tanky"));
    CHECK(d.state == State::Running);
    CHECK(d.note == "the straight move (direct) to 0.0 0.0 0.0 was refused (too-far: Healy is not with Tanky): going on without it");
    CHECK(Tick(d, g).empty() && d.stallStandInWanted);
    // Past the direct stage a refusal of that order is no longer the stall's.
    CHECK(!StallDirectRefused(d, direct, "too-far"));

    // Beyond the direct step's limits (30 yd, or 9 yd up): straight to the stand-in.
    Run far = Start(&plan);
    Facts h = g;
    h.leadX = 30.0f;
    CHECK(Tick(far, h) == "move 0.0 0.0 0.0");
    CHECK(NextOrder(far, h).empty() && far.stallStandInWanted && far.stallFallback == 2);
    CHECK(StallDirectFits(g, 0.0f, 0.0f, 0.0f));
    h.leadX = 10.0f;
    h.leadZ = kStallDirectMaxVertical + 1.0f;
    CHECK(!StallDirectFits(h, 0.0f, 0.0f, 0.0f));
    h.leadZ = 0.0f;
    h.leadX = kStallDirectMaxYd + 0.1f;
    CHECK(!StallDirectFits(h, 0.0f, 0.0f, 0.0f));

    // Its own move not failing, or the party in a fight: the old retries, no fallback.
    Run ok = Start(&plan);
    Facts n = g;
    n.leadMoveFailed = false;
    CHECK(Tick(ok, n) == "move 0.0 0.0 0.0");
    CHECK(NextOrder(ok, n) == "move 0.0 0.0 0.0");
    CHECK(ok.walkTries == 2 && ok.stallFallback == 0);
    Run fight = Start(&plan);
    std::string unused;
    fight.phase = Phase::Walk;
    Facts c = g;
    c.anyInCombat = true;
    CHECK(!StallFallback(fight, c, 0.0f, 0.0f, 0.0f, false, unused));
    CHECK(fight.stallFallback == 0 && unused.empty());
    // A direct walk keeps its own rule.
    CHECK(!StallFallback(fight, g, 0.0f, 0.0f, 0.0f, true, unused));

    // The stand-in only replaces the current move step's own walk (not a pull's, a hop's,
    // a walk back's), and never twice.
    Run own = Start(&plan);
    CHECK(StallStandInFits(own, 0.0f, 0.0f, 0.0f));
    CHECK(!StallStandInFits(own, 1.0f, 0.0f, 0.0f));
    own.detour = true;
    CHECK(!StallStandInFits(own, 0.0f, 0.0f, 0.0f));
    DungeonPlan pull;
    pull.steps = {PullStep(0, {4850}), Step(PlanStepKind::Rest)};
    Run p = Start(&pull);
    CHECK(!StallStandInFits(p, 0.0f, 0.0f, 0.0f));

    // Opus review 2026-10-23: the fallback only for a path failure. clipped-empty (the path
    // cut at a hostile or a hazard), magma, cantmove: the old retries and pause, no direct
    // move and no stand-in; dispatch-short and empty-path: the fallback. shortcut-empty
    // is never recorded by MoveTo2 (Codex review 2026-10-23), so it is not counted either.
    for (const char* reason : {"clipped-empty", "magma", "cantmove", "hazard-short", "shortcut-empty", "none", ""})
    {
        Run cut = Start(&plan);
        Facts k = g;
        k.leadMoveFailReason = reason;
        CHECK(Tick(cut, k) == "move 0.0 0.0 0.0");
        CHECK(NextOrder(cut, k) == "move 0.0 0.0 0.0");
        CHECK(!cut.stallStandInWanted && cut.stallFallback == 0 && cut.walkTries == 2);
        CHECK(NextOrder(cut, k) == "move 0.0 0.0 0.0");
        CHECK(NextOrder(cut, k).empty() && cut.state == State::Paused && !cut.stallStandInWanted);
    }
    for (const char* reason : {"dispatch-short", "empty-path"})
    {
        Run path = Start(&plan);
        Facts k = g;
        k.leadMoveFailReason = reason;
        CHECK(StallPathFailure(reason));
        CHECK(Tick(path, k) == "move 0.0 0.0 0.0");
        CHECK(NextOrder(path, k) == "move 0.0 0.0 0.0 direct");
    }
    CHECK(!StallPathFailure("clipped-empty") && !StallPathFailure("magma"));

    // Codex review 2026-10-23: the direct move only in the diagnosed case. On land beside a
    // ledge (the line not a clear swim line), with the line not read, or with the lead bot
    // out of the water: no direct move, the dry stand-in at once.
    for (int c = 0; c < 3; ++c)
    {
        Run land = Start(&plan);
        Facts l = g;
        if (c == 0)
            l.stallDirectClear = false; // read: not a swim line, or a hostile near it
        else if (c == 1)
            l.stallDirectRead = false;  // not read
        else
            l.leadInWater = false;      // on land
        CHECK(Tick(land, l) == "move 0.0 0.0 0.0");
        CHECK(NextOrder(land, l).empty());
        CHECK(land.stallStandInWanted && land.stallFallback == 2);
        CHECK(!StallStandIn(land, false, 0, 0, 0));
        CHECK(Tick(land, l) == "move 0.0 0.0 0.0"); // the old retries, never a direct move
        CHECK(NextOrder(land, l) == "move 0.0 0.0 0.0");
        CHECK(NextOrder(land, l).empty() && land.state == State::Paused);
    }
    // The glue's verdict: in water, a swim line, no hostile within 15 yd of the line.
    std::vector<HostileSpot> none;
    CHECK(StallDirectLineClear(true, true, none, 16.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    CHECK(!StallDirectLineClear(false, true, none, 16.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    CHECK(!StallDirectLineClear(true, false, none, 16.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    std::vector<HostileSpot> nearLine = {HostileSpot{8.0f, 14.0f, 0.0f}};   // 14 yd off the middle of the line
    CHECK(!StallDirectLineClear(true, true, nearLine, 16.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    std::vector<HostileSpot> farOff = {HostileSpot{8.0f, 16.0f, 0.0f}, HostileSpot{-16.0f, 0.0f, 0.0f}};
    CHECK(StallDirectLineClear(true, true, farOff, 16.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    std::vector<HostileSpot> pastEnd = {HostileSpot{-14.0f, 0.0f, 0.0f}};    // 14 yd beyond the point
    CHECK(!StallDirectLineClear(true, true, pastEnd, 16.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    CHECK(std::fabs(PointToSegmentYd(8.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 16.0f, 0.0f, 0.0f) - 3.0f) < 0.01f);
    CHECK(std::fabs(PointToSegmentYd(20.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 16.0f, 0.0f, 0.0f) - 5.0f) < 0.01f);
    CHECK(std::fabs(PointToSegmentYd(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f) - std::sqrt(3.0f)) < 0.01f);
    // The glue reads the line only near a stall of a walk whose fallback is untried.
    Run probe = Start(&plan);
    CHECK(Tick(probe, n) == "move 0.0 0.0 0.0");
    CHECK(!StallDirectProbeWanted(probe));
    probe.walkStillMs = kWalkRetryMs - 3 * kTickMs;
    CHECK(StallDirectProbeWanted(probe));
    probe.walkStillMs = kWalkRetryMs - 3 * kTickMs - 1;
    CHECK(!StallDirectProbeWanted(probe));
    probe.walkStillMs = kWalkRetryMs;
    probe.stallFallback = 1;
    CHECK(!StallDirectProbeWanted(probe));

    // A fresh walk starts the fallback over.
    Run fresh = Start(&plan);
    CHECK(Tick(fresh, g) == "move 0.0 0.0 0.0");
    CHECK(NextOrder(fresh, g) == "move 0.0 0.0 0.0 direct");
    fresh.stallFallback = 3;
    fresh.walkX = 99.0f; // another point: a fresh walk
    CHECK(Tick(fresh, g) == "move 0.0 0.0 0.0");
    CHECK(fresh.stallFallback == 0);
}

// The executor walks a stay point more than its follow range (5.0 yd in a dungeon) away in raw
// 3D (StayPointReached, runtime/StayArrivalPolicy.h). Zul'Farrak r16-19z34 slot 2, step 37
// (ZF-STEP37-2026-10-22): the lead bot stood on the ramp 4.84 yd from the point on the ground and
// 4.6 yd above it (6.6 yd in 3D); the executor's old ground rule refused the walk and a run-side
// ground deadband counted the stall as arrived. Crescent Grove r26 step 272
// (MOVE-USELESS-2026-10-23): a tauren lead bot 5.67 yd off on the ground and 6.6 yd below never
// walked (the old rule took its bounding radius off) and the ground deadband did not count it
// either: four pauses. With the 3D rule both spots are walks, so a stall there is stuck, and a
// stall within 5.0 yd in 3D is a stalled arrival (the deadband rule was removed).
static void TestStalledLeadInFollowRangeCountsAsArrived()
{
    bool arrived = false, stuck = false;
    // Crescent Grove r26: the lead bot 6.6 yd below its point is not there; a stall there is stuck.
    RunStalledWalk(5.67f, -6.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    RunStalledWalk(5.67f, 6.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    // Zul'Farrak r16: 6.6 yd in 3D is beyond the executor's rule now (it walks there), so a stall is stuck.
    RunStalledWalk(4.84f, 4.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    RunStalledWalk(-4.84f, -4.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    RunStalledWalk(5.1f, 4.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    // A real other floor below or above: stuck.
    RunStalledWalk(2.0f, 8.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    // Flat ground within the follow range: arrived after the short stall (no walk, no jitter).
    RunStalledWalk(4.5f, 0.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    RunStalledWalk(3.0f, 3.8f, PlanStepKind::Move, arrived, stuck); // 4.84 yd in 3D
    CHECK(arrived && !stuck);
    RunStalledWalk(1.0f, 4.8f, PlanStepKind::Move, arrived, stuck); // 4.9 yd in 3D, above kStandZTolYd
    CHECK(arrived && !stuck);
    // A direct step keeps the 3D rule.
    RunStalledWalk(4.84f, 4.6f, PlanStepKind::MoveDirect, arrived, stuck);
    CHECK(!arrived && stuck);

    // The r16 replay: the guard's walk back to the stay anchor (the server-snapped z 17.6) is not
    // counted as arrived from the ramp; the move goes out again, and it arrives once the lead bot
    // has walked there.
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 1787.8f, 790.6f, 17.7f), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    r.guardReturn = true;
    r.guardX = 1787.8f;
    r.guardY = 790.6f;
    r.guardZ = 17.6f;
    Facts f = Party(0);
    f.leadX = 1783.2f;
    f.leadY = 792.1f;
    f.leadZ = 22.2f;
    f.walkBackAfterWonFight = true; // the walk back itself (a move step goes on forward at the key's default)
    CHECK(Tick(r, f) == "move 1787.8 790.6 17.6");
    bool resent = false;
    for (int ticks = 1; ticks < static_cast<int>(kWalkRetryMs / kTickMs) + 2 && r.state == State::Running; ++ticks)
        resent = Tick(r, f) == "move 1787.8 790.6 17.6" || resent;
    CHECK(r.state == State::Running && r.guardReturn && resent);
    f.leadX = 1787.6f;
    f.leadY = 790.7f;
    f.leadZ = 17.6f;
    Tick(r, f);
    CHECK(r.state == State::Running && !r.guardReturn);
    for (int i = 0; i < 60 && r.step == 0 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.note == "arrived");

    // A pull step's walk (an ambush walked through) keeps the stand rule: stuck at the same spot.
    DungeonPlan pull;
    pull.steps = {PullStep(0, {4850}, ai::kPlanFlagAmbush), Step(PlanStepKind::Rest)};
    Run p = Start(&pull);
    Facts g = Party(4.84f);
    g.leadZ = 4.6f;
    for (int i = 0; i < 300 && p.step == 0 && p.state == State::Running; ++i)
        Tick(p, g);
    CHECK(p.state == State::Paused && p.note.find("stuck") != std::string::npos);
}

// Live bug: a scorpid in the Sentinel pit was pulled, fought and leashed home
// over and over for minutes.
static void TestPullLoopOnOneTargetPauses()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {7078}), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(42, 7078, 10)};
    int pulls = 0;
    for (int i = 0; i < 2000 && r.state == State::Running && r.step == 0; ++i)
    {
        f.anyInCombat = false;
        if (Tick(r, f) == "pullback 42")
        {
            ++pulls;
            // Every pull starts a short fight; the unit survives it.
            f.anyInCombat = true;
            for (int k = 0; k < 5; ++k)
                Tick(r, f);
        }
    }
    CHECK(pulls == static_cast<int>(kMaxPullsPerTarget));
    // Left as out of reach: the step ends "skipped, not cleared", the run goes on.
    CHECK(r.state == State::Running && r.step == 1 && r.note.find("skipped, not cleared") != std::string::npos);
    CHECK(r.unreachable.count(42) == 1);
}

// Live: ten Cleft Scorpids in a pit, each pull fought one that leashed home.
static void TestOutOfReachPackIsSkippedNotCleared()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {7078}), Step(PlanStepKind::Move, 30)};
    Run r = Start(&plan);
    Facts f = Party(0);
    for (std::uint64_t g = 1; g <= 10; ++g)
        f.units.push_back(Unit(g, 7078, 10 + g));
    int pulls = 0;
    for (int i = 0; i < 3000 && r.state == State::Running && r.step == 0; ++i)
    {
        f.anyInCombat = false;
        if (Tick(r, f).compare(0, 9, "pullback ") == 0)
        {
            ++pulls;
            f.anyInCombat = true;
            for (int k = 0; k < 5; ++k)
                Tick(r, f);
        }
    }
    CHECK(pulls == static_cast<int>(kFruitlessPulls));
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note.find("skipped, not cleared: 10 units") != std::string::npos);
    // Never pulled again on the way (hop pulls skip them too).
    f.units.clear();
    f.hopUnits = {Unit(3, 7078, 13)};
    CHECK(Tick(r, f) == "move 30.0 0.0 0.0");
}

// Live cascade: no Shaft -> no Staff -> Keystone -> Seal wait, each failing.
static void TestMissingItemPausesOnce()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    PlanStep wait = Step(PlanStepKind::WaitObject, 7, 8, 9);
    wait.entry = 124372;
    wait.param = 1;
    plan.steps = {loot, use, open, wait, Step(PlanStepKind::Move, 1)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0);
    // The corpse is gone: the order is refused. One pause, naming the item.
    OrderRefused(r, order, "not-found: No spawned game object or corpse of that entry is near the point");
    CHECK(r.state == State::Paused && r.note.find("item 7741 missing") != std::string::npos);
    CHECK(Skip(r));
    CHECK(r.missing.count(7741) == 1 && r.state == State::Running && r.step == 1);
    // The medallion is there, but its use needs the Shaft: refused, no pause.
    f.carrierHasEntryItem = true;
    order = Tick(r, f);
    CHECK(order == "use 7740 Tanky");
    OrderRefused(r, order, "busy: The cast failed (spell check 40).");
    CHECK(r.state == State::Running && r.step == 2 && r.missing.count(7733) == 1);
    // No staff: the Keystone and the Seal wait are left out with notes.
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 3 && r.note.find("left out") != std::string::npos);
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 4 && r.note.find("open before it") != std::string::npos);
}

// ai::kPlanFlagGoneIsDone (Zul'Farrak's graves): a loot or open row with the flag counts the object being gone as
// done; the same row without the flag pauses as before.
static PlanStep GraveStep(PlanStepKind kind, std::uint32_t flags)
{
    PlanStep s = Step(kind, 1858.7f, 1053.8f, 8.9f);
    s.entry = 128403;
    s.flags = flags;
    ai::NormalizePlanStep(s);
    return s;
}

static void TestGoneChestWithFlagIsDone()
{
    for (PlanStepKind kind : {PlanStepKind::Loot, PlanStepKind::Open})
    {
        // The search answers `not-found` (nothing spawned of that entry near the point): done, one note, no pause.
        DungeonPlan plan;
        plan.steps = {GraveStep(kind, ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        std::string order = Tick(r, f);
        CHECK(order.compare(0, 5, kind == PlanStepKind::Loot ? "loot " : "open ") == 0);
        OrderRefused(r, order, "not-found: No spawned game object of that entry is near the point (or the bot).");
        CHECK(r.state == State::Running && r.step == 1);
        CHECK(r.note.find("object gone: counted as done") != std::string::npos);

        // The object despawned while the bot walked to it: the task fails `invalid-target`: done too.
        Run r2 = Start(&plan);
        order = Tick(r2, f);
        f.carrierBusy = true;
        f.taskReply = 2;
        f.taskText = "invalid-target: The object is gone.";
        Tick(r2, f);
        CHECK(r2.state == State::Running && r2.step == 1);
        CHECK(r2.note.find("object gone: counted as done") != std::string::npos);
    }
}

static void TestGoneChestWithoutFlagPauses()
{
    for (PlanStepKind kind : {PlanStepKind::Loot, PlanStepKind::Open})
    {
        DungeonPlan plan;
        plan.steps = {GraveStep(kind, 0), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        std::string order = Tick(r, f);
        OrderRefused(r, order, "not-found: No spawned game object of that entry is near the point (or the bot).");
        CHECK(r.state == State::Paused && r.step == 0);
        Run r2 = Start(&plan);
        order = Tick(r2, f);
        f.carrierBusy = true;
        f.taskReply = 2;
        f.taskText = "invalid-target: The object is gone.";
        Tick(r2, f);
        CHECK(r2.state == State::Paused && r2.step == 0);
    }
}

// The flag settles a gone object only: any other refusal (in combat, too far, busy) still pauses, and a row that
// names an item stays a missing-item case.
static void TestGoneIsDoneOnlyForAGoneObject()
{
    DungeonPlan plan;
    plan.steps = {GraveStep(PlanStepKind::Loot, ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    OrderRefused(r, order, "in-combat: The bot is in combat.");
    CHECK(r.state == State::Paused && r.step == 0);

    DungeonPlan withItem;
    PlanStep s = GraveStep(PlanStepKind::Loot, ai::kPlanFlagGoneIsDone);
    s.item = 7741;
    withItem.steps = {s, Step(PlanStepKind::Move, 9)};
    Run r2 = Start(&withItem);
    order = Tick(r2, f);
    OrderRefused(r2, order, "not-found: No spawned game object or corpse of that entry is near the point");
    CHECK(r2.state == State::Paused && r2.step == 0);
}

// ai::kPlanFlagGoneIsDone on a wait_object row (Shadowfang Keep's Courtyard Door 18895, spawn 20835: Deathstalker
// Adamant's escort sets it active and removes it a few waypoints later): done when the object is in the wanted
// state, or when it reads not found AUTHORITATIVELY (Facts::objectReadable: the lead bot alive on the plan's map within
// reach of the point, its grid loaded) for kGoneConfirmMs without a break, after a wait step read it found. Never
// seen, it waits out its timeout and pauses as before.
static PlanStep GoneWaitStep(std::uint32_t flags, float x = -242.58f, float y = 2159.05f, float z = 90.62f)
{
    PlanStep s = Step(PlanStepKind::WaitObject, x, y, z);
    s.entry = 18895;
    s.param = 1;
    s.timeoutS = 25;
    s.flags = flags;
    ai::NormalizePlanStep(s);
    return s;
}

static Facts ReadFacts(int state, bool readable)
{
    Facts f = Party(0);
    f.objectState = state;
    f.objectReadable = readable;
    return f;
}

static void TickN(Run& r, const Facts& f, int n)
{
    for (int i = 0; i < n && r.step == 0 && r.state == State::Running; ++i)
        Tick(r, f);
}

static void TestWaitObjectGoneAfterSeenIsDone()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
    // Seen ready (closed), then three readable misses: done, with the note, long before the timeout.
    Run r = Start(&plan);
    Tick(r, ReadFacts(0, true));
    CHECK(r.step == 0 && r.state == State::Running && r.objectsSeen.size() == 1);
    Facts miss = ReadFacts(-1, true);
    Tick(r, miss);
    Tick(r, miss);
    CHECK(r.step == 0 && r.state == State::Running); // two misses are not enough
    Tick(r, miss);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note.find("is gone after it was seen: counted as done") != std::string::npos);
    // Seen active: the wanted state wins as before, at once.
    Run a = Start(&plan);
    Tick(a, ReadFacts(1, true));
    CHECK(a.state == State::Running && a.step == 1 && a.note.find("in the state the plan waits for") != std::string::npos);
    // Seen in the wrong state (ready), still found: keeps waiting.
    Run w = Start(&plan);
    TickN(w, ReadFacts(0, true), 5);
    CHECK(w.state == State::Running && w.step == 0);
}

// A -1 that the search could not have answered never counts, however long it lasts.
static void TestWaitObjectUnreadableMissIsNotGone()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
    // Lead dead or released, on another map, out of range, grid unloaded: the glue reports objectReadable false.
    Run r = Start(&plan);
    Tick(r, ReadFacts(0, true));
    TickN(r, ReadFacts(-1, false), 10);
    CHECK(r.state == State::Running && r.step == 0 && r.goneMissMs == 0);
    // A dead lead bot (a ghost off the map, released) whatever the glue says: no count.
    Run d = Start(&plan);
    Tick(d, ReadFacts(0, true));
    Facts dead = ReadFacts(-1, true);
    dead.leadAlive = false;
    for (int i = 0; i < 10; ++i)
        Tick(d, dead);
    CHECK(d.step == 0 && d.goneMissMs == 0);
    // ... and it starts the readable count over: two readable misses, an unreadable tick, two more: still waiting.
    Facts miss = ReadFacts(-1, true);
    Tick(r, miss);
    Tick(r, miss);
    Tick(r, ReadFacts(-1, false));
    Tick(r, miss);
    Tick(r, miss);
    CHECK(r.state == State::Running && r.step == 0);
    Tick(r, miss);
    CHECK(r.step == 1);
}

static void TestWaitObjectOneMissThenFoundIsNotGone()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Tick(r, ReadFacts(0, true));
    Facts miss = ReadFacts(-1, true);
    Tick(r, miss);
    Tick(r, miss);
    Tick(r, ReadFacts(0, true)); // a blip: found again, the count starts over
    Tick(r, miss);
    Tick(r, miss);
    CHECK(r.state == State::Running && r.step == 0);
}

static void TestWaitObjectNeverSeenWaitsOutItsTimeout()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Facts f = ReadFacts(-1, true); // readable and never found: not gone, only not there yet
    std::uint32_t ticks = 0;
    while (r.state == State::Running && r.step == 0 && ticks < 200)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Paused && r.step == 0 && ticks >= 25 && ticks <= 27);
    CHECK(r.note.find("not found near its point") != std::string::npos);
}

static void TestWaitObjectGoneWithoutFlagPauses()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(0), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Tick(r, ReadFacts(0, true));
    Facts f = ReadFacts(-1, true);
    std::uint32_t ticks = 0;
    while (r.state == State::Running && r.step == 0 && ticks < 200)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Paused && r.step == 0 && ticks >= 24);
}

// The sighting is the run's, not the step's: a resume of the paused step keeps it.
static void TestWaitObjectSightSurvivesAPause()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Tick(r, ReadFacts(0, true));
    Pause(r, "paused by hand");
    CHECK(Resume(r));
    TickN(r, ReadFacts(-1, true), 5);
    CHECK(r.state == State::Running && r.step == 1);
}

// A sighting belongs to its target (entry and the step's point within kWaitSightYd), not to the entry: the door seen at
// one point says nothing of the same entry's door at another, and a plain wait next to it does not count the sighting.
static void TestWaitObjectSightingsAreKeyedByTarget()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone, 100, 100, 10), GoneWaitStep(ai::kPlanFlagGoneIsDone, 160, 100, 10),
        Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Tick(r, ReadFacts(1, true)); // the first door seen active: step done
    CHECK(r.step == 1 && r.objectsSeen.size() == 1);
    // The second door (the same entry 18895, 60 yd away) was never read found: not gone, however many readable misses.
    Facts miss = ReadFacts(-1, true);
    for (int i = 0; i < 10 && r.step == 1 && r.state == State::Running; ++i)
        Tick(r, miss);
    CHECK(r.step == 1 && r.state == State::Running);
    // Seen at its own point, then gone: done.
    Tick(r, ReadFacts(0, true));
    CHECK(r.objectsSeen.size() == 2);
    for (int i = 0; i < 3; ++i)
        Tick(r, miss);
    CHECK(r.step == 2);
    // A door within kWaitSightYd of a seen one is the same target.
    DungeonPlan near;
    near.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone, 100, 100, 10), GoneWaitStep(ai::kPlanFlagGoneIsDone, 105, 100, 10),
        Step(PlanStepKind::Move, 9)};
    Run n = Start(&near);
    Tick(n, ReadFacts(1, true));
    for (int i = 0; i < 3; ++i)
        Tick(n, miss);
    CHECK(n.step == 2);
}

// A walk back replays an earlier point while the run waits at its step: the
// glue reads the step facts for the replayed point (FactStep), so an object
// read found there belongs to that earlier wait_object step, not to the
// current one. The current flagged wait is not marked seen by it, and
// readable misses at its own point afterwards do not finish it (never seen):
// it waits out its timeout as before.
static void TestWaitObjectSightingDuringWalkBackIsIgnored()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone, 100, 100, 10), GoneWaitStep(ai::kPlanFlagGoneIsDone, 160, 100, 10),
        Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan, 1);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    Tick(r, ReadFacts(0, true)); // the earlier door read found: not the current step's sighting
    CHECK(r.step == 1 && r.state == State::Running);
    CHECK(r.objectsSeen.empty());
    CHECK(!WaitSightSeen(r, plan.steps[1]));
    Facts miss = ReadFacts(-1, true);
    for (int i = 0; i < 3; ++i)
        Tick(r, miss); // kGoneConfirmMs of readable misses: not done, the object was never seen
    CHECK(r.step == 1 && r.state == State::Running);
    std::uint32_t ticks = 3;
    while (r.state == State::Running && r.step == 1 && ticks < 200)
    {
        Tick(r, miss);
        ++ticks;
    }
    CHECK(r.state == State::Paused && r.step == 1 && ticks >= 25 && ticks <= 27);
    CHECK(r.note.find("not found near its point") != std::string::npos);
}

// A walk-back tick for another step (FactStep != s) breaks the current flagged wait's unbroken miss run: 2 s of
// readable misses, one walk-back tick, then 1 s of misses is not gone; 3 s more of misses confirm it.
static void TestWalkBackTickResetsGoneMisses()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone, 100, 100, 10), GoneWaitStep(ai::kPlanFlagGoneIsDone, 160, 100, 10),
        Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan, 1);
    Tick(r, ReadFacts(0, true)); // seen at its own point
    CHECK(r.step == 1 && r.state == State::Running && WaitSightSeen(r, plan.steps[1]));
    Facts miss = ReadFacts(-1, true);
    Tick(r, miss);
    Tick(r, miss); // 2 s of readable misses
    CHECK(r.step == 1 && r.state == State::Running && r.goneMissMs == 2000);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    Tick(r, miss); // facts for another step: ignored, and the miss run starts over
    CHECK(r.step == 1 && r.state == State::Running);
    CHECK(r.goneMissMs == 0);
    Tick(r, miss); // 1 s of misses after the walk back: not gone
    CHECK(r.step == 1 && r.state == State::Running);
    Tick(r, miss);
    Tick(r, miss); // 3 s more: gone after seen
    CHECK(r.state == State::Running && r.step == 2);
    CHECK(r.note.find("is gone after it was seen: counted as done") != std::string::npos);
    Tick(r, miss);
    CHECK(r.state == State::Running && r.step == 2);
}

// A gone object finishes a flagged wait over a stale combat flag, too (the objectDone shortcut), once confirmed.
static void TestWaitObjectGoneWinsOverAStaleFight()
{
    DungeonPlan plan;
    plan.steps = {GoneWaitStep(ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Facts f = ReadFacts(0, true);
    Tick(r, f);
    f = ReadFacts(-1, true);
    f.anyInCombat = true;
    f.staleCombat = true;
    for (std::uint32_t i = 0; i < kStaleConfirmMs / kTickMs + 1 && r.step == 0; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    // Unreadable under the same flag: never.
    Run q = Start(&plan);
    Tick(q, ReadFacts(0, true));
    f = ReadFacts(-1, false);
    f.anyInCombat = true;
    f.staleCombat = true;
    for (int i = 0; i < 10 && q.step == 0; ++i)
        Tick(q, f);
    CHECK(q.step == 0);
}

// After a wipe: resume revives the party and gathers it before the step.
static void TestResumeRegroups()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    r.autoRegroups = kAutoRegroupsPerStep; // the regroups by itself are used up: it pauses
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.state == State::Paused);
    CHECK(Resume(r));
    CHECK(r.autoRegroups == 0);
    CHECK(Tick(r, f) == "revive party");
    CHECK(r.state == State::Running && r.note.find("regroup") == 0);
    CHECK(Tick(r, f).empty());
    // Up again but spread out: gather on the lead bot, then the step.
    f.dead = 0;
    f.alive = 5;
    f.maxMemberYd = 80;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // Revive that never works is bounded.
    Run r2 = Start(&plan);
    r2.autoRegroups = kAutoRegroupsPerStep;
    Facts g = Party(0);
    g.dead = 3;
    g.alive = 2;
    Tick(r2, g);
    Resume(r2);
    for (int i = 0; i < 400 && r2.state == State::Running; ++i)
        Tick(r2, g);
    CHECK(r2.state == State::Paused && r2.note.find("still dead") != std::string::npos);
}

// A far move point (the lead came back from the entrance): navmesh hops.
static void TestFarMoveWalksBackInHops()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.leadX = 40;
    CHECK(Tick(r, f).empty());
    f.seen = true;
    CHECK(Tick(r, f) == "move 200.0 0.0 0.0");
}

// The Sentinel ramp: a direct step gathers the party first.
static void TestDirectStepGathersFirst()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 30;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(Tick(r, f).empty());
    f.maxMemberYd = 4;
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
}

// r12-19z29 Uldaman leg 5 step 41 (2026-10-22): the re-gather before the direct step passed on the ground distance
// (Jinkla within kGatherYd) while she stood on the ledge above the ramp's floor (z -45.4 vs -52.3); the glue's own
// test (no navmesh path of at most 40 yd onto the lead bot's spot, not within 5 yd of it on its floor in its sight)
// refused the step too-far. The gate asks the glue's test (MemberFact::withLead) instead.
static Facts LedgeScene(float jinklaYd, bool withLead)
{
    Facts f = Party(0);
    f.directJoinRead = true;
    MemberFact jinkla = Mem("Jinkla", jinklaYd, 6.9f);
    jinkla.withLead = withLead;
    f.members = {Mem("Tanky", 0), Mem("Nathil", 3), jinkla};
    f.maxMemberYd = jinklaYd;
    return f;
}

static void TestDirectGateAsksTheGluesWithLeadTest()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    CHECK(DirectJoinWanted(r));
    Facts f = LedgeScene(19.5f, false); // inside kGatherYd on the ground, not with the lead bot
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(r.phase == Phase::Gather && r.note == "gathering before a direct step");
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty()); // never the direct step while she is not with the lead bot
    // The summon brought her: with the lead bot, the step goes out.
    f = LedgeScene(1.0f, true);
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
    // Facts the glue did not read (no direct step under it): the ground rule as before.
    Run g = Start(&plan);
    Facts old = LedgeScene(19.5f, false);
    old.directJoinRead = false;
    CHECK(Tick(g, old) == "move 5.0 0.0 0.0 direct");
    // Not for other steps: the glue reads no paths for them.
    DungeonPlan move;
    move.steps = {Step(PlanStepKind::Move, 5)};
    Run m = Start(&move);
    CHECK(!DirectJoinWanted(m));
}

// The gather's ceiling before a direct step: the straggler not with the lead bot was summoned already this step
// (the revive's summon) more than kDirectResummonMs ago: it is summoned again instead of the pause.
static void TestDirectGatherCeilingSummonsAgain()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    MarkSummoned(r, "Jinkla"); // the mid-step revive's summon (SummonRevived), at runMs 0
    r.runMs = 90000;           // 980.7 -> 1070.7 in the trace
    Facts f = LedgeScene(19.5f, false);
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    for (std::uint32_t i = 0; i <= kGatherCapMs / kTickMs && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Jinkla");
    CHECK(r.state == State::Running && r.stepSummons["Jinkla"].count == 2);
}

static std::string const kLedgeStep = "move 5.0 0.0 0.0 direct";
static std::string const kLedgeRefusal = "too-far: Jinkla is not with Tankkinz (no navmesh path of at most 40 yd to "
    "its spot, and not within 5 yd of it on its floor in its sight). A direct step takes the lead bot's line: gather "
    "the party on it first.";

// Ticks until an order comes (at most `ms`); the ms waited in `waited`.
static std::string TickUntilOrder(Run& r, const Facts& f, std::uint32_t ms, std::uint32_t& waited)
{
    waited = 0;
    std::string order;
    while (order.empty() && waited <= ms && r.state == State::Running)
    {
        order = Tick(r, f);
        waited += kTickMs;
    }
    return order;
}

// The refused direct step (r12 1070.7): the straggler was summoned this step already (the revive's summon), so the
// once-per-step summon was skipped and the refusal paused the run, which stopped the guard: two more deaths. Now:
// summoned again after kDirectResummonMs (or after it died and came back), at most kDirectSummonCap times a step;
// no pause before kDirectPauseSummons summons and kDirectPauseMs since the first refusal; the pause text as before.
static void TestRefusedDirectStepSummonsAgainAndPausesLate()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    MarkSummoned(r, "Jinkla");
    r.summoned.insert("Jinkla");
    r.runMs = 90000;
    Facts with = Party(0);
    with.members = {Mem("Tanky", 0), Mem("Jinkla", 1.0f)};
    with.maxMemberYd = 1.0f;
    CHECK(Tick(r, with) == kLedgeStep);
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running); // the summon-once rule paused here
    CHECK(Tick(r, with) == "summon Jinkla");
    CHECK(r.stepSummons["Jinkla"].count == 2);
    // She arrives, the step goes out and is refused again at once: no third summon yet (kDirectResummonMs), no
    // pause (60 s have not passed since the first refusal): the run waits for her, and does not resend every tick.
    std::uint32_t waited = 0;
    CHECK(TickUntilOrder(r, with, kSummonWaitMs, waited) == kLedgeStep);
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running);
    CHECK(r.note.find("waiting for it to come") != std::string::npos);
    std::string order = TickUntilOrder(r, with, kDirectRetryMs + kTickMs, waited);
    CHECK(order == kLedgeStep && waited >= kDirectRetryMs);
    // Refused again until kDirectResummonMs since her last summon: then the third summon.
    for (int i = 0; i < 10 && order != "summon Jinkla"; ++i)
    {
        if (order == kLedgeStep)
            OrderRefused(r, kLedgeStep, kLedgeRefusal);
        CHECK(r.state == State::Running);
        order = TickUntilOrder(r, with, kDirectRetryMs + kSummonWaitMs + kTickMs, waited);
    }
    CHECK(order == "summon Jinkla" && r.stepSummons["Jinkla"].count == kDirectSummonCap);
    // The cap: never a fourth summon; once 60 s passed since the first refusal, the pause, with the old text.
    for (int i = 0; i < 20 && r.state == State::Running; ++i)
    {
        order = TickUntilOrder(r, with, kDirectResummonMs + kDirectRetryMs + kSummonWaitMs, waited);
        CHECK(order != "summon Jinkla");
        if (order == kLedgeStep)
            OrderRefused(r, kLedgeStep, kLedgeRefusal);
    }
    CHECK(r.state == State::Paused);
    CHECK(r.stepSummons["Jinkla"].count == kDirectSummonCap);
    CHECK(r.runMs - r.directRefusedMs >= kDirectPauseMs);
    CHECK(r.note.find("cmd " + kLedgeStep + " refused: " + kLedgeRefusal) == 0);
}

// No pause for a refused direct step before kDirectPauseSummons summons, however long: a bot summoned once is
// summoned a second time once kDirectResummonMs passed, before any pause.
static void TestRefusedDirectStepNeedsTwoSummonsBeforeThePause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    Facts with = Party(0);
    with.members = {Mem("Tanky", 0), Mem("Jinkla", 1.0f)};
    with.maxMemberYd = 1.0f;
    CHECK(Tick(r, with) == kLedgeStep);
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(Tick(r, with) == "summon Jinkla"); // the first: never summoned this step
    std::uint32_t waited = 0;
    CHECK(TickUntilOrder(r, with, kSummonWaitMs, waited) == kLedgeStep);
    r.runMs += kDirectPauseMs; // long past the 60 s: still only one summon, so no pause
    r.stepSummons["Jinkla"].lastMs = r.runMs - 1000; // ... and the last one 1 s ago: no summon now either
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running && r.stepSummons["Jinkla"].count == 1);
    CHECK(r.note.find("waiting for it to come") != std::string::npos);
    CHECK(Tick(r, with).empty()); // the facts do not say she is with the lead bot: the wait goes on
    // The glue's test finds her with the lead bot: the step goes out at once, not after kDirectRetryMs.
    with.directJoinRead = true;
    CHECK(Tick(r, with) == kLedgeStep);
    // The time bound: 2 x kDirectPauseMs since the first refusal pauses whatever the summons, with the old text.
    r.runMs = r.directRefusedMs + 2 * kDirectPauseMs - kTickMs;
    r.stepSummons["Jinkla"].lastMs = r.runMs - 1000;
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running && r.stepSummons["Jinkla"].count == 1);
    r.directWaitFor.clear();
    r.runMs = r.directRefusedMs + 2 * kDirectPauseMs;
    r.stepSummons["Jinkla"].lastMs = r.runMs - 1000;
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Paused && r.note.find("cmd " + kLedgeStep + " refused: " + kLedgeRefusal) == 0);
}

// The cold review of d158546e: a straggler held in a fight is never summoned (`cmd summon` refuses it), so the
// count rule alone never paused the direct gather: past 2 x kDirectPauseMs it pauses, and not before.
static void TestCombatLockedDirectStragglerPausesOnTime()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    MarkSummoned(r, "Jinkla");
    Facts f = LedgeScene(19.5f, false);
    f.members[2].inCombat = true;
    f.members[2].attackers = 1;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::uint64_t pausedAt = 0;
    for (int i = 0; i < 400 && r.state == State::Running; ++i)
    {
        CHECK(Tick(r, f).find("summon") == std::string::npos);
        if (r.state == State::Paused)
            pausedAt = r.runMs;
    }
    CHECK(r.state == State::Paused && r.stepSummons["Jinkla"].count == 1);
    CHECK(r.directRefusedOn && pausedAt - r.directRefusedMs >= 2ull * kDirectPauseMs);
    CHECK(pausedAt - r.directRefusedMs < 2ull * kDirectPauseMs + kGatherCapMs + kTickMs);
    CHECK(r.note.find("the party did not gather within") == 0);
}

// The module's glue as the policy sees it: the test is read on the ticks DirectJoinWanted asked for (with the run
// as the last tick left it); then Jinkla on the ledge is not with the lead bot.
static std::string GlueTick(Run& r, Facts f)
{
    f.directJoinSupported = true;
    f.directJoinRead = DirectJoinWanted(r);
    for (MemberFact& m : f.members)
        if (m.name == "Jinkla")
            m.withLead = !f.directJoinRead;
    return Tick(r, f);
}

// The cold review of d158546e (MED): a walk back passes a pull in the tick it reaches it, so the facts read for
// the pull's index were not the direct step's, and the gate fell back to the ground rule on that tick. The read
// follows WalkBack's own skip (NextWalkBackIdx); a ledge bot never lets the walk back's direct order out.
static void TestWalkBackReadsTheDirectStepPastAPull()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(50, {1}), Step(PlanStepKind::MoveDirect, 5),
        Step(PlanStepKind::Move, 100)};
    Run r = Start(&plan, 3);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    Facts f = LedgeScene(19.5f, false);
    bool sawPull = false;
    for (int i = 0; i < 60; ++i)
    {
        if (r.walkBack && r.walkBackIdx == 1)
        {
            sawPull = true;
            CHECK(DirectJoinWanted(r)); // the point the next tick reaches is the direct step
        }
        std::string const order = GlueTick(r, f);
        CHECK(order.find(" direct") == std::string::npos);
        CHECK(r.state == State::Running);
    }
    CHECK(sawPull && r.walkBack && r.walkBackIdx == 2 && r.phase == Phase::Gather);
    // Down from the ledge (with the lead bot): the direct order goes out.
    f = LedgeScene(1.0f, true);
    f.directJoinSupported = true;
    f.directJoinRead = true;
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
}

// The belt: a glue that reads the test did not read it on the tick the gate passed: no order that tick (the note
// as it was), then the read tick sends it; a bounded hold (kDirectJoinHolds), never forever.
static void TestDirectOrderWaitsForTheRead()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    Facts f = LedgeScene(1.0f, true);
    f.directJoinSupported = true;
    f.directJoinRead = false;
    CHECK(Tick(r, f).empty() && r.directJoinHolds == 1);
    f.directJoinRead = true;
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct" && r.directJoinHolds == 0);
    Run g = Start(&plan);
    f.directJoinRead = false;
    std::uint32_t held = 0;
    std::string order;
    while (order.empty() && held <= kDirectJoinHolds)
    {
        order = Tick(g, f);
        held += order.empty() ? 1 : 0;
    }
    CHECK(order == "move 5.0 0.0 0.0 direct" && held == kDirectJoinHolds);
    // No read while a walk is under way (the gate is not asked then): the glue saves the paths.
    g.phase = Phase::Walk;
    CHECK(!DirectJoinWanted(g));
    g.phase = Phase::Begin;
    CHECK(DirectJoinWanted(g));
}

// The cold review of d158546e (LOW): the refusal clock belongs to the point; a walk back gone on to another direct
// point starts it over, so a pause due at the first point does not fire at the next one.
static void TestDirectRefusalClockStartsOverAtTheNextPoint()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5), Step(PlanStepKind::MoveDirect, 10), Step(PlanStepKind::Move, 100)};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    r.walkBackDirect = true;
    r.stepSummons["Jinkla"].count = kDirectSummonCap;
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running && r.directRefusedIdx == 0);
    NextWalkBackPoint(r);
    r.directWaitFor.clear();
    r.runMs += kDirectPauseMs + 10000;
    OrderRefused(r, "move 10.0 0.0 0.0 direct", kLedgeRefusal);
    CHECK(r.state == State::Running && r.directRefusedIdx == 1 && r.directRefusedMs == r.runMs);
}

// A bot that died and came back since its last summon is a new case: summoned again at once (the cap still holds).
static void TestDirectSummonAfterADeathIsANewCase()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    r.runMs = 10000;
    MarkSummoned(r, "Jinkla");
    CHECK(!DirectSummonAllowed(r, "Jinkla"));
    Facts dead = Party(0);
    dead.deadNames = {"Jinkla"};
    Tick(r, dead); // every running tick notes the dead (NoteDeadSeen)
    CHECK(r.deadSeenMs["Jinkla"] == 10000 + kTickMs);
    r.runMs = 15000;
    CHECK(DirectSummonAllowed(r, "Jinkla"));
    r.stepSummons["Jinkla"].count = kDirectSummonCap;
    CHECK(!DirectSummonAllowed(r, "Jinkla"));
    // The step's record starts over with the next step.
    ResetStep(r);
    CHECK(r.stepSummons.empty() && !r.directRefusedOn && r.directWaitFor.empty());
}

// `.bot run skip` while running goes on running.
static void TestSkipWhileRunning()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(Skip(r));
    CHECK(r.state == State::Running && r.step == 1 && !r.regroup);
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
}

// Live: `cmd move -159.5 281.6 -49.1` refused unreachable (a point up a
// ledge). The glue finds a reachable point near it; the step walks there.
static void TestUnreachableMoveDetours()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    CHECK(order == "move 50.0 0.0 0.0");
    // Too far from the plan's point (26 yd, past kMoveFallbackYd): not a stand-in.
    CHECK(!UseDetour(r, 24, 0, 0));
    CHECK(UseDetour(r, 44, 2, 0));
    CHECK(r.state == State::Running && r.detour);
    // Only once per step.
    CHECK(!UseDetour(r, 45, 0, 0));
    CHECK(Tick(r, f) == "move 44.0 2.0 0.0");
    f.leadX = 44;
    f.leadY = 2;
    Tick(r, f);
    CHECK(r.step == 1 && r.note.find("nearest reachable point") != std::string::npos);
    // The detour is per step.
    CHECK(!r.detour);
    // Not for a step that is not a move.
    CHECK(!UseDetour(r, 1, 1, 1));
}

static void TestPullGatherAndFight()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851, 4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(11, 4851, 45), Unit(12, 4852, 38)};
    // Scattered party: gather first (the lead holds where it stands).
    f.maxMemberYd = 35;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(r.phase == Phase::Gather);
    CHECK(Tick(r, f).empty());
    f.maxMemberYd = 5;
    // Nearest first.
    CHECK(Tick(r, f) == "pullback 12");
    CHECK(r.phase == Phase::AwaitPull);
    // The fight: no orders while it lasts.
    f.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.note == "fighting");
    // Both died; low mana afterwards: rest, then the step is done.
    f.anyInCombat = false;
    f.units.clear();
    f.minHealerManaPct = 20;
    CHECK(Tick(r, f) == "rest");
    CHECK(r.phase == Phase::Rest);
    f.minHealerManaPct = 85;
    Tick(r, f);
    CHECK(r.state == State::Done || r.step == 1);
}

// RFC r3-19z20 step 22: the lead bot holds "stay" while the run rests (an
// attack order had ended its stay, and its idle formation walk took it into
// the lava basin while the healer drank). Only in the rest phase of a running
// run, only for a living lead out of combat that is off "stay".
static void TestLeadHoldsStayWhileResting()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(11, 4851, 38)};
    f.maxMemberYd = 5;
    // Before the pull and during the fight: no hold from here.
    CHECK(Tick(r, f) == "pullback 11");
    CHECK(!LeadStayDueWhileResting(r, true, false, false, false, false));
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(!LeadStayDueWhileResting(r, true, true, false, false, false));
    // The fight is over and the healer is dry: the run rests, the lead holds.
    f.anyInCombat = false;
    f.units.clear();
    f.minHealerManaPct = 20;
    CHECK(Tick(r, f) == "rest");
    CHECK(r.phase == Phase::Rest);
    CHECK(LeadStayDueWhileResting(r, true, false, false, false, false));
    // Sent once: a lead already on "stay" is left alone.
    CHECK(!LeadStayDueWhileResting(r, true, false, true, false, false));
    // A lead standing in magma is left free to walk out of it.
    CHECK(!LeadStayDueWhileResting(r, true, false, false, true, false));
    // A dead lead or one pulled into a fight again is not put on "stay".
    CHECK(!LeadStayDueWhileResting(r, false, false, false, false, false));
    CHECK(!LeadStayDueWhileResting(r, true, true, false, false, false));
    // Still resting a tick later (the formation walk would start any time).
    Tick(r, f);
    CHECK(r.phase == Phase::Rest);
    CHECK(LeadStayDueWhileResting(r, true, false, false, false, false));
    // A paused run gives no orders.
    Run paused = r;
    paused.state = State::Paused;
    CHECK(!LeadStayDueWhileResting(paused, true, false, false, false, false));
    // Rested: the step goes on and the hold is the next order's business.
    f.minHealerManaPct = 85;
    Tick(r, f);
    CHECK(r.phase != Phase::Rest);
    CHECK(!LeadStayDueWhileResting(r, true, false, false, false, false));
}

static UnitFact Hitter(std::uint64_t guid, std::uint32_t entry, float dist, bool healer);

// A run brought to its rest phase after a first fight (the healer dry), the
// tank back on "stay" at its stand: the state of r15 Uldaman step 40 at
// 1151 s ("resting").
static Run RestingRun(DungeonPlan& plan, Facts& f)
{
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    f = Party(0);
    f.leadIsTank = true;
    f.units = {Unit(11, 4851, 38)};
    f.maxMemberYd = 5;
    CHECK(Tick(r, f) == "pullback 11");
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units.clear();
    f.minHealerManaPct = 20;
    CHECK(Tick(r, f) == "rest");
    CHECK(r.phase == Phase::Rest && !r.fighting && !r.guardReturn);
    f.leadStaying = true;
    return r;
}

// THREAT F1 (r15 Uldaman step 40, 1159-1192 s; r15 Deadmines step 83,
// 1607-1687 s): a fight that started while the run rested was a fight. The
// run's phase stayed Rest through it, and the rest's "the lead holds stay"
// (RFC r3-19z20) put the tank back on stay on the tick after each guard
// pick-up. T1: fighting in the rest: no stay due. T2: resting, no fight: stay
// due, as before. T3: the fight over, the guard's walk back pending: no stay.
static void TestRestFightLeavesTheLeadOffStay()
{
    DungeonPlan plan;
    Facts f;
    Run r = RestingRun(plan, f);
    // T2: a lead genuinely idle in the rest (off stay, no fight) is held.
    CHECK(LeadStayDueWhileResting(r, true, false, false, false, false));
    // T1: a fight in the rest; the lead is on nobody's threat list (not in
    // combat itself) and off stay after a pick-up: not put back on stay.
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(r.phase == Phase::Rest && r.fighting);
    CHECK(!LeadStayDueWhileResting(r, true, false, false, false, false));
    // T3: the fight flag gone but the guard's return still pending (belt and
    // braces: the fight's end leaves the rest): no stay due either.
    Run back = r;
    back.fighting = false;
    back.guardReturn = true;
    CHECK(back.phase == Phase::Rest);
    CHECK(!LeadStayDueWhileResting(back, true, false, false, false, false));
    back.guardReturn = false;
    CHECK(LeadStayDueWhileResting(back, true, false, false, false, false)); // only those two flags differ
    // The other conditions are unchanged.
    CHECK(!LeadStayDueWhileResting(back, true, false, true, false, false));
    CHECK(!LeadStayDueWhileResting(back, true, false, false, true, false));
    CHECK(!LeadStayDueWhileResting(back, true, true, false, false, false));
    CHECK(!LeadStayDueWhileResting(back, false, false, false, false, false));
}

// T1 through the tick: a rest fight whose tank leaves stay without a guard
// pick-up, so no guard return is pending: a kill_first focus (CommanderFocus
// takes every bot off "stay", RelaxTacticalMovement). Only Run::fighting
// keeps the rest's stay off the tank then.
static UnitFact Engaged(UnitFact u);
static void TestRestFightFocusLeavesTheLeadOffStay()
{
    DungeonPlan plan;
    Facts f;
    Run r = RestingRun(plan, f);
    plan.killFirst = {7247};
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(77, 7247, 10))};
    CHECK(Tick(r, f) == "focus 77");
    CHECK(r.phase == Phase::Rest && r.fighting && !r.guardReturn);
    f.leadStaying = false; // the focus ended the tank's stay
    for (int i = 0; i < 5; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(!LeadStayDueWhileResting(r, true, false, f.leadStaying, false, f.staleCombat));
    }
}

// Codex adversarial and the Opus cold review of 5da4d254: a combat flag left
// standing in the rest (Facts::staleCombat: anyInCombat, nothing hostile
// engaged with any bot or pet) keeps Run::fighting true and the phase Rest
// for up to the fight ceiling. A stale flag is no fight: the lead off stay is
// held again (the RFC r3 lava wander), with or without a guard pick-up
// pending; once the flag turns real (an attacker), the stay is off again.
static void TestRestStaleCombatStillHoldsTheLead()
{
    DungeonPlan plan;
    Facts f;
    Run r = RestingRun(plan, f);
    // Phase Rest, the flag up, no attackers, no targets, no guard return.
    f.anyInCombat = true;
    f.staleCombat = true;
    f.leadStaying = false;
    for (int i = 0; i < 3; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(r.phase == Phase::Rest && r.fighting && !r.guardReturn);
        CHECK(LeadStayDueWhileResting(r, true, false, f.leadStaying, false, f.staleCombat));
    }
    // The flag turns real: a mob hits the hunter.
    f.staleCombat = false;
    f.members = {Mem("Tanky", 0), Mem("Aeladra", 21.3f)};
    f.members[1].attackers = 1;
    f.members[1].inCombat = true;
    f.partyAttackers = {Hitter(7022, 7022, 22.7f, false)};
    Tick(r, f);
    CHECK(r.fighting);
    CHECK(!LeadStayDueWhileResting(r, true, false, f.leadStaying, false, f.staleCombat));
    // The reviewers' sequence: a guard pick-up (guard return pending), the
    // mobs die, one bot keeps a stale flag: the lead is held where it stands
    // (the walk back to the stand still follows the fight's end).
    Run picked = r;
    picked.guardReturn = true;
    CHECK(!LeadStayDueWhileResting(picked, true, false, false, false, false));
    CHECK(LeadStayDueWhileResting(picked, true, false, false, false, true));
}

// T4: a run back is allowed in the rest phase too (no event ahead), and not
// in a rest during an event's lead-in.
static void TestRunBackAllowedInRest()
{
    DungeonPlan plan;
    Facts f;
    Run r = RestingRun(plan, f);
    CHECK(r.phase == Phase::Rest && !r.regroup && !r.walkBack);
    CHECK(RunBackAllowed(r));
    // Out of the rest, no regroup/gather/walk back: not allowed, as before.
    Run begin = r;
    EnterPhase(begin, Phase::Begin);
    CHECK(!RunBackAllowed(begin));
    // A rest on the moves that lead into a hold (the Keepers, Archaedas): never.
    DungeonPlan holdPlan;
    holdPlan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Hold, 40)};
    Run h = Start(&holdPlan);
    EnterPhase(h, Phase::Rest);
    CHECK(EventHoldAhead(h) != nullptr);
    CHECK(!RunBackAllowed(h));
}

static bool Listed(const Run& r, const std::string& name)
{
    return std::find(r.runBack.begin(), r.runBack.end(), name) != r.runBack.end();
}

// T5: the run back's listing in a rest: the sentinel hunter (21.3 yd from the
// tank, 10 scorpids on it, r15 Uldaman 1165 s) and the Deadmines rogue (59 yd,
// one Craftsman 1731, r15 Deadmines 1625 s, beyond the guard's 40 yd) run on
// to the party; a member inside kGatherYd is not listed. (Why the rogue was
// 59 yd out at all, the "formation station" drift during the rest, is not
// this fix: its own ticket.)
static void TestRunBackListsRestFightMembers()
{
    DungeonPlan plan;
    Facts f;
    Run r = RestingRun(plan, f);
    f.anyInCombat = true;
    f.members = {Mem("Tanky", 0), Mem("Aeladra", 21.3f), Mem("Nightsong", 59.0f), Mem("Elandra", 15.0f)};
    f.members[1].attackers = 10;
    f.members[1].inCombat = true;
    f.members[2].attackers = 1;
    f.members[2].inCombat = true;
    f.members[3].attackers = 2;
    f.members[3].inCombat = true;
    f.maxMemberYd = 59.0f;
    CHECK(Tick(r, f).empty()); // no pick-up: nothing hits a member within kGuardYd
    CHECK(r.phase == Phase::Rest);
    CHECK(Listed(r, "Aeladra") && Listed(r, "Nightsong") && !Listed(r, "Elandra"));
    CHECK(r.note.find("regroup: Aeladra running back with 10 attackers") != std::string::npos);
    CHECK(r.note.find("Nightsong running back with 1 attacker") != std::string::npos);
    // The lead in a fight of its own: nobody new is called in (unchanged).
    Run busy = RestingRun(plan, f);
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.members = {Mem("Tanky", 0), Mem("Aeladra", 21.3f)};
    f.members[1].attackers = 10;
    Tick(busy, f);
    CHECK(busy.runBack.empty());
}

// T6: a replay of r15 Uldaman step 40, the sentinel (1151-1175 s). The run
// rests (1151), the tank Warrunner on stay at its stand, on no threat list.
// 1159: Venomlash 7022 engages the hunter Aeladra 21.3 yd out; 1165: the
// scorpids 7078 are on her too. The guard's pick-up goes out after
// kGuardWaitMs (live: 1163.5, "picks up 7078 off the party (22.7 yd)"); the
// order ends the tank's stay. Live, the next tick's rest hold put it back on
// stay (1163.5, 1167.5, 1171.6: 3 of 3) and the tank never walked; the hunter
// died at 1188.7, the warlock Potpourri at 1191.8. Here the glue's stay check
// (after Tick, before the order, BotCommands.cpp) is asked each tick as the
// glue asks it: the stay stays off for the whole fight, and the hunter and
// the warlock run back. This replay does NOT prove the Run::fighting
// condition: the first pick-up sets Run::guardReturn on the same tick, and
// that masks it from then on (the tank is on stay before it). Only T1 and
// TestRestFightFocusLeavesTheLeadOffStay prove Run::fighting.
static void TestSentinelRestFightReplay()
{
    DungeonPlan plan;
    Facts f;
    Run r = RestingRun(plan, f);
    f.carrier = "Warrunner";
    f.members = {Mem("Warrunner", 0), Mem("Aeladra", 21.3f), Mem("Potpourri", 30.0f)};
    f.maxMemberYd = 30.0f;
    // 1151-1158: resting, the lead holds (the RFC r3 rule is untouched).
    for (int i = 0; i < 3; ++i)
        CHECK(Tick(r, f).empty() && r.phase == Phase::Rest);
    CHECK(!LeadStayDueWhileResting(r, true, false, f.leadStaying, false, false)); // on stay already
    CHECK(LeadStayDueWhileResting(r, true, false, false, false, false));          // off stay: held
    // 1159: Venomlash on the hunter; the tank is on no threat list.
    f.anyInCombat = true;
    f.leadInCombat = false;
    f.members[1].attackers = 1;
    f.members[1].inCombat = true;
    f.partyAttackers = {Hitter(7022, 7022, 22.7f, false)};
    int undone = 0;
    int pickups = 0;
    bool hunterRan = false;
    bool warlockRan = false;
    for (std::uint32_t ms = 0; ms <= 16000; ms += kTickMs)
    {
        if (ms >= 6000)
        {
            // 1165: the scorpids are on her too.
            f.members[1].attackers = 10;
            f.partyAttackers = {Hitter(7078, 7078, 22.7f, false), Hitter(7022, 7022, 14.4f, false)};
        }
        if (ms >= 11000)
        {
            // 1170: the warlock, eating 30 yd out, is hit.
            f.members[2].attackers = 1;
            f.members[2].inCombat = true;
        }
        std::string const order = Tick(r, f);
        CHECK(r.phase == Phase::Rest); // the phase stays Rest through the fight
        hunterRan = hunterRan || Listed(r, "Aeladra");
        warlockRan = warlockRan || Listed(r, "Potpourri");
        // The glue: the stay check runs after Tick, before the order goes out.
        if (LeadStayDueWhileResting(r, true, f.leadInCombat, f.leadStaying, false, f.staleCombat))
        {
            f.leadStaying = true; // FormUpOnLead(..., stay) + StopMoving
            ++undone;
        }
        if (order.compare(0, 7, "attack ") == 0)
        {
            CHECK(order.find(" Warrunner") != std::string::npos);
            CHECK(r.fighting); // set on the same tick as the guard's first order
            ++pickups;
            f.leadStaying = false; // RelaxTacticalMovement: the attack ends the stay
        }
    }
    CHECK(pickups >= 2);
    CHECK(undone == 0);
    CHECK(!f.leadStaying);
    CHECK(r.guardReturn);
    // The hunter and the warlock ran on to the party (standing still here,
    // each is let go after kWalkRetryMs without gaining ground, as before).
    CHECK(hunterRan && warlockRan);
}

static void TestUnseenPackIsApproachedNotCleared()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = false;
    // The glue gives the next hop on the navmesh path; one move per hop.
    f.approachKnown = true;
    f.approachX = 35;
    CHECK(Tick(r, f) == "move 35.0 0.0 0.0");
    f.leadX = 10;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && r.state == State::Running);
    // At the hop and still not in sight: the next hop.
    f.leadX = 34;
    CHECK(Tick(r, f).empty());
    f.approachX = 70;
    CHECK(Tick(r, f) == "move 70.0 0.0 0.0");
    f.leadX = 70;
    f.seen = true;
    Tick(r, f);
    CHECK(r.state == State::Done);
    // No path toward it at all: the stand search first, never a straight-line guess.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    g.seen = false;
    Tick(r2, g);
    CHECK(r2.state == State::Running && r2.approachStandWanted && r2.note.find("no walkable path") == 0);
    // A hop within kArriveYd of the lead bot goes nowhere: no path at once
    // (it used to "arrive" 8 times first; r3-19z20 B-FIX-2). The hop cap's
    // own test is TestApproachHopCapCountsHopsWithoutProgress.
    Run r3 = Start(&plan);
    g.approachKnown = true;
    g.approachX = 1;
    Tick(r3, g);
    CHECK(r3.state == State::Running && r3.approachStandWanted && r3.note.find("no walkable path") == 0);
}

// Ragefire 19z13, trogg cave pull 13: the pack is 70 yd down a ramp. The hop
// toward it was broken off by the pull's gather as soon as the pack came in
// sight, while the anchor was still the previous stand 11 yd higher (more than
// kStragglerDzYd): the gather "went back" to it, the next hop came forward
// again, eight times, then the hop cap. The hop is walked to its end first, and
// its point becomes the anchor.
static void TestFarPackHopIsNotUndoneByTheGatherBack()
{
    DungeonPlan plan;
    plan.steps = {PullStep(-238.0f, {11318})};
    plan.steps[0].y = -36.0f;
    plan.steps[0].z = -56.4f;
    Run r = Start(&plan);
    r.restDue = false;
    r.anchorKnown = true; // the previous step's stand, at the top of the ramp
    r.anchorX = -169.1f; r.anchorY = -34.9f; r.anchorZ = -40.9f;
    Facts f = Party(-169.1f);
    f.leadY = -34.9f; f.leadZ = -40.9f;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -205; f.approachY = -34; f.approachZ = -52;
    CHECK(Tick(r, f) == "move -205.0 -34.0 -52.0");
    // Half way down the pack comes in sight (60 yd): the hop goes on.
    f.leadX = -200.7f; f.leadY = -35.6f; f.leadZ = -48.5f;
    f.seen = true;
    f.units = {Unit(1, 11318, 38)};
    CHECK(Tick(r, f).empty());
    CHECK(r.phase == Phase::Walk && r.note == "approaching the pack");
    // At the hop's point: it is the anchor and the pull goes out, no gather back.
    f.leadX = -205; f.leadY = -34; f.leadZ = -52;
    f.units = {Unit(1, 11318, 34)};
    CHECK(Tick(r, f) == "pullback 1");
    CHECK(r.anchorX == -205.0f && r.anchorZ == -52.0f);
    CHECK(r.note.find("gathering back") == std::string::npos);
    // A unit of the pack within kHopClearYd, the followers far behind: the hop
    // is over and the pull goes out from here. Neither a gather beside the pack
    // nor a walk back to the old anchor, however many ticks it takes.
    Run q = Start(&plan);
    q.restDue = false;
    q.anchorKnown = true;
    q.anchorX = -169.1f; q.anchorY = -34.9f; q.anchorZ = -40.9f;
    Facts g = Party(-169.1f);
    g.leadY = -34.9f; g.leadZ = -40.9f;
    g.seen = false;
    g.approachKnown = true;
    g.approachX = -205; g.approachY = -34; g.approachZ = -52;
    Tick(q, g);
    g.leadX = -222; g.leadY = -34; g.leadZ = -54;
    g.seen = true;
    g.maxMemberYd = 40;
    g.units = {Unit(1, 11318, 17)};
    bool pulled = false;
    for (int i = 0; i < 40 && q.state == State::Running; ++i)
    {
        std::string const order = Tick(q, g);
        CHECK(order.compare(0, 5, "move ") != 0);
        CHECK(q.note.find("gathering") == std::string::npos);
        if (order == "pullback 1")
        {
            pulled = true;
            break;
        }
    }
    CHECK(pulled && q.state == State::Running && q.pulls == 1 && q.anchorX == -169.1f);
    // The fight starts and ends: the next pull is an ordinary one again.
    g.anyInCombat = true;
    Tick(q, g);
    CHECK(q.state == State::Running && q.fighting);
    // A player's move next to the visible pack, the party behind: the anchor stays too.
    Run m = Start(&plan);
    m.restDue = false;
    m.anchorKnown = true;
    m.anchorX = -169.1f; m.anchorY = -34.9f; m.anchorZ = -40.9f;
    Facts h = g;
    h.units = {Unit(1, 11318, 15)};
    Tick(m, h);
    CHECK(m.anchorX == -169.1f && m.anchorZ == -40.9f);
}

// A hop loop that really gets nowhere still pauses after kMaxApproachHops, with
// how near it got; a long way walked in hops that do get nearer is not capped.
static void TestApproachHopCapCountsHopsWithoutProgress()
{
    DungeonPlan plan;
    // no_defer: the pause (PullMayBeDeferred; deferral: TestUnreachablePackIsDeferredThenPulledOnTheWay).
    plan.steps = {PullStep(1000, {1}, ai::kPlanFlagNoDefer)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    for (int hop = 1; hop <= 3 * static_cast<int>(kMaxApproachHops); ++hop)
    {
        f.approachX = f.leadX + 40;
        CHECK(Tick(r, f) == "move " + Num(f.approachX) + " 0.0 0.0");
        f.leadX = f.approachX; // the hop is walked, 40 yd nearer
        CHECK(Tick(r, f).empty());
        CHECK(r.state == State::Running);
    }
    // The lead bot is pushed back each time: no progress, 8 hops, the pause.
    Run s = Start(&plan);
    s.restDue = false;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = true;
    for (int i = 0; i < 100 && s.state == State::Running; ++i)
    {
        g.approachX = 40;
        StandTick(s, g);
        g.leadX = 40;
        StandTick(s, g);
        g.leadX = 0; // dragged back
    }
    CHECK(s.state == State::Paused);
    CHECK(s.note.find("8 approach hops did not bring the party within 60.0 yd of the pack") == 0);
    CHECK(s.note.find("nearest it got: 1000.0 yd") != std::string::npos);
    // Before that pause the glue was asked for a stand near the pack, and found none.
    CHECK(s.approachStand == 3 && s.note.find("; the pack at 1000.0 0.0 0.0 is not reachable from this floor: "
        "no safe stand 20.0-25.0 yd from it the lead bot has a path to") != std::string::npos);
}

// Gnomeregan r24 slot 4 step 412, Sunken Temple r25 slot 4 step 5, Razorfen Kraul r24
// slot 8 step 299: the hop cap paused ('8 approach hops did not bring the party within
// 60 yd of the pack', nearest 66.8 to 99.6 yd) and each resume hopped the same way into
// the same pause. The cap asks the glue once for a stand within 25 yd of the pack
// (ApproachStand); the party walks there and pulls; the pause only when there is none
// or it does not bring the pack in sight; a resume searches the stand, never hops first.
static void TestApproachCapTriesAStandNearThePackBeforeThePause()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1000, {1}, ai::kPlanFlagNoDefer)}; // never deferred: it pauses (PullMayBeDeferred)
    auto capped = [&](Run& r, Facts& g)
    {
        for (int i = 0; i < 100 && r.state == State::Running && !r.approachStandWanted; ++i)
        {
            g.approachX = 40;
            Tick(r, g);
            if (r.approachStandWanted)
                break;
            g.leadX = 40;
            Tick(r, g);
            g.leadX = 0; // dragged back
        }
    };
    auto far = [] { Facts g = Party(0); g.seen = false; g.approachKnown = true; return g; };
    // A stand found: walked to, and the pull goes out from there.
    Run r = Start(&plan);
    r.restDue = false;
    Facts g = far();
    capped(r, g);
    CHECK(r.state == State::Running && r.approachStandWanted && r.approachStand == 1);
    CHECK(r.note.find("8 approach hops did not bring the party within 60.0 yd of the pack (nearest it got: 1000.0 yd): "
        "looking for a stand within 25.0 yd of it") == 0);
    CHECK(Tick(r, g).empty() && r.state == State::Running); // no hop while the glue searches
    CHECK(ApproachStandFound(r, g, true, 980, 0, 0));
    CHECK(r.approachStand == 2 && !r.approachStandWanted && r.state == State::Running);
    CHECK(Tick(r, g) == "move 980.0 0.0 0.0");
    CHECK(r.note.find("approach: walking to the stand 980.0 0.0 0.0, 20.0 yd from the pack at 1000.0 0.0 0.0") == 0);
    Facts there = g;
    there.leadX = 980;
    there.seen = true;
    there.units = {Unit(7, 1, 20)};
    Tick(r, there);
    CHECK(r.state == State::Running && r.note.find("approach") == std::string::npos);
    // A stand that does not bring the pack in sight: the pause, saying so.
    Run s = Start(&plan);
    s.restDue = false;
    Facts h = far();
    capped(s, h);
    CHECK(ApproachStandFound(s, h, true, 980, 0, 0));
    CHECK(Tick(s, h) == "move 980.0 0.0 0.0");
    h.leadX = 980;
    Tick(s, h); // arrived, still out of sight (a wall)
    Tick(s, h);
    CHECK(s.state == State::Paused && s.approachStand == 3);
    CHECK(s.note.find("8 approach hops did not bring the party within 60.0 yd of the pack") == 0);
    CHECK(s.note.find("; the pack at 1000.0 0.0 0.0 is not reachable from this floor: the stand 980.0 0.0 0.0 "
        "did not bring it within 60.0 yd") != std::string::npos);
    // A stand farther than 25 yd from the pack is not taken.
    Run w = Start(&plan);
    w.restDue = false;
    Facts k = far();
    capped(w, k);
    CHECK(!ApproachStandFound(w, k, true, 970, 0, 0) && w.state == State::Paused);
    // Resumed with no stand reachable from the new spot: the search first (no hop before
    // it), then the hops again with a fresh cap (the Opus review of f1fdeb67: a player who
    // moved the party behind a winding ramp had no way out), and that cap pauses.
    Run q = Start(&plan);
    q.restDue = false;
    Facts m = far();
    capped(q, m);
    CHECK(!ApproachStandFound(q, m, false, 0, 0, 0) && q.state == State::Paused);
    CHECK(Resume(q) && q.approachStandResumed);
    m.leadX = 0;
    m.approachX = 40;
    bool searched = false, hopBeforeSearch = false, hopped = false;
    for (int i = 0; i < 200 && q.state == State::Running; ++i)
    {
        std::string const o = StandTick(q, m);
        searched = searched || q.note.find("approach (resumed): no safe stand near the pack at 1000.0 0.0 0.0 from here: "
            "approach hops again") == 0;
        if (o == "move 40.0 0.0 0.0")
        {
            hopBeforeSearch = hopBeforeSearch || !searched;
            hopped = true;
            m.leadX = 40;
            StandTick(q, m);
            m.leadX = 0; // dragged back again
        }
    }
    CHECK(searched && hopped && !hopBeforeSearch && q.state == State::Paused && q.approachStand == 3);
    CHECK(q.note.find("8 approach hops did not bring the party within 60.0 yd of the pack") == 0);
    // Resumed from a spot that has a stand now (a player moved the party): straight to it, no hop.
    CHECK(Resume(q));
    std::string first;
    for (int i = 0; i < 60 && q.state == State::Running && first.empty(); ++i)
        first = StandTick(q, m, true, 978, 0, 0);
    CHECK(first == "move 978.0 0.0 0.0" && q.approachStand == 4);
    // Another step kind (an NPC, the next point) keeps the old pause: no stand asked.
    DungeonPlan moves;
    moves.steps = {Step(PlanStepKind::Move, 1000)};
    Run n = Start(&moves);
    n.restDue = false;
    Facts o = far();
    capped(n, o);
    CHECK(!n.approachStandWanted && n.state == State::Paused &&
        n.note.find("approach hops did not bring the party") != std::string::npos);
    // A step done forgets the stand.
    Run d = Start(&plan);
    d.approachStandStep = 0;
    d.approachStand = 3;
    NextStep(d, "done");
    CHECK(d.approachStandStep == static_cast<std::size_t>(-1) && d.approachStand == 0);
}

// Codex review of f1fdeb67: the approach stand came from the refused move's stand-in
// search, which takes the plan point itself (the pack's centre) first and looks at no
// hostile near the stand or on its path. The approach's own search: a ring 15-25 yd out
// (never inside the target's pull radius + 5), in sight of the pack, no other hostile
// within 20 yd, its path passing no other uncleared pack; the shortest path wins.
static void TestApproachStandIsASafeRingSpot()
{
    DungeonPlan plan;
    PlanStep other = PullStep(1000, {9});
    other.y = 60; // another pack, 60 yd north of the target
    ai::NormalizePlanStep(other);
    plan.steps = {PullStep(1000, {1}), other};
    Run r = Start(&plan);
    const PlanStep& s = plan.steps[0];
    CHECK(s.radius == 15.0f && ApproachStandMinRing(s) == 20.0f);
    auto cand = [](float x, float y, float pathYd, std::vector<PathPoint> path)
    {
        StandCandidate c;
        c.x = x;
        c.y = y;
        c.reachable = true;
        c.los = true;
        c.pathYd = pathYd;
        c.path = path;
        return c;
    };
    std::vector<StandHostile> hostiles = {{1000, 0, 0, 1}, {1003, 2, 0, 1}};
    // The pack's centre (reachable, the shortest path) is never the stand; a ring spot is.
    std::vector<StandCandidate> c = {cand(1000, 0, 60, {{940, 0, 0}, {1000, 0, 0}}),
        cand(978, 0, 40, {{940, 0, 0}, {978, 0, 0}})};
    CHECK(ApproachStandReject(r, s, c[0], hostiles, 0) == "off the ring");
    CHECK(PickApproachStand(r, s, c, hostiles, 0) == 1);
    // The shortest path passes the other pack: that candidate is left out for a longer one.
    std::vector<StandCandidate> d = {cand(1000, 22, 50, {{940, 40, 0}, {1000, 60, 0}, {1000, 22, 0}}),
        cand(978, -5, 70, {{940, 0, 0}, {978, -5, 0}})};
    CHECK(ApproachStandReject(r, s, d[0], hostiles, 0) == "its path passes an uncleared pack");
    CHECK(PickApproachStand(r, s, d, hostiles, 0) == 1);
    // Two safe ones: the shorter path wins, whatever the order.
    std::vector<StandCandidate> two = {cand(978, -5, 70, {{940, 0, 0}, {978, -5, 0}}),
        cand(978, 0, 40, {{940, 0, 0}, {978, 0, 0}})};
    CHECK(PickApproachStand(r, s, two, hostiles, 0) == 1);
    // The target's own pack does not count as crossed (the stand is beside it).
    CHECK(ApproachStandReject(r, s, d[1], hostiles, 0).empty());
    // A target unit within its pull radius + 5, another hostile within 20 yd, no sight, no path: left out.
    std::vector<StandHostile> near = {{1000, 0, 0, 1}, {990, 0, 0, 1}};
    CHECK(ApproachStandReject(r, s, c[1], near, 0) == "inside the target's pull radius");
    std::vector<StandHostile> stray = {{1000, 0, 0, 1}, {965, 5, 0, 77}};
    CHECK(ApproachStandReject(r, s, c[1], stray, 0) == "another hostile close by");
    // Codex review of 8b81efa3: a mob of the target's entry from a neighbouring pack (40 yd
    // from the step's point, past its 15 yd radius) is no member: it needs the 20 yd too.
    // A tight pack (radius 10: members keep 15 yd, every other hostile 20 yd).
    PlanStep tight = s;
    tight.radius = 10;
    StandCandidate near17 = cand(983, 0, 40, {{940, 0, 0}, {983, 0, 0}});
    std::vector<StandHostile> neighbour = {{1000, 0, 0, 1}, {967, 0, 0, 1}}; // same entry, 33 yd from the point, 16 from the stand
    CHECK(ApproachStandReject(r, tight, near17, neighbour, 0) == "another hostile close by");
    std::vector<StandHostile> member = {{999, 0, 0, 1}}; // in the pack, 16 yd from the stand
    CHECK(ApproachStandReject(r, tight, near17, member, 0).empty());
    StandCandidate blind = c[1];
    blind.los = false;
    CHECK(ApproachStandReject(r, s, blind, hostiles, 0) == "no sight of the pack");
    StandCandidate nopath = c[1];
    nopath.reachable = false;
    CHECK(ApproachStandReject(r, s, nopath, hostiles, 0) == "no path");
    // None safe: -1, and the glue's 'none found' is the pause (TestApproachCapTriesAStandNearThePackBeforeThePause).
    std::vector<StandCandidate> unsafe = {c[0], blind, nopath};
    CHECK(PickApproachStand(r, s, unsafe, hostiles, 0) == -1);
    // A wide pack (radius 22): the ring starts past 25 yd, no stand at all.
    PlanStep wide = s;
    wide.radius = 22;
    CHECK(!ApproachStandReject(r, wide, c[1], hostiles, 0).empty());
    // The policy refuses a point inside the ring from the glue too (a gating pull: the pause).
    DungeonPlan gated;
    gated.steps = {PullStep(1000, {1}, ai::kPlanFlagNoDefer)};
    Run g = Start(&gated);
    g.approachStandStep = 0;
    g.approachStand = 1;
    Facts f = Party(900);
    CHECK(!ApproachStandFound(g, f, true, 1000, 0, 0) && g.state == State::Paused);
}

// The Opus review of f1fdeb67: the walk to the approach stand was no hop, so crossing
// the 60 yd line handed it to the pull with the anchor still the last looping hop's end;
// that anchor read Off (another floor) and the gather walked the lead bot back out of
// sight, and the next tick paused 'the stand did not bring it within 60 yd'. The stand is
// walked like a hop (its end the anchor), the stand is spent once the pack is seen, a
// later tick out of sight hops again; rewinds forget it, and legs never end by a hostile.
static void TestApproachStandWalkIsAHopAndRewindsForgetIt()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1000, {1})};
    Run r = Start(&plan);
    r.restDue = false;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = true;
    for (int i = 0; i < 100 && r.state == State::Running && !r.approachStandWanted; ++i)
    {
        g.approachX = 40;
        Tick(r, g);
        if (r.approachStandWanted)
            break;
        g.leadX = 40;
        Tick(r, g);
        g.leadX = 0;
    }
    CHECK(r.anchorKnown && r.anchorX == 40.0f); // the last hop's end
    CHECK(ApproachStandFound(r, g, true, 978, 0, 0));
    r.approachBestYd = 66.8f; // as near as the hops got (Sunken Temple r25)
    CHECK(Tick(r, g) == "move 978.0 0.0 0.0");
    // In sight at 945, the anchor's way back read Off: the walk to the stand goes on, no gather back.
    Facts in = g;
    in.leadX = 945;
    in.seen = true;
    in.units = {Unit(7, 1, 33)};
    in.anchorPathRead = true;
    in.anchorReachable = false;
    std::string const o = Tick(r, in);
    CHECK(o != "move 40.0 0.0 0.0" && r.state == State::Running && r.approachStand == 3);
    in.leadX = 978;
    Tick(r, in);
    CHECK(r.anchorX == 978.0f && r.state == State::Running);
    CHECK(r.approachHops == 0 && r.approachBestYd == 0.0f);
    // A later tick out of sight, the fight having dragged the lead bot 75 yd off: a hop goes
    // out, never the stand's pause nor the old cap's.
    Facts out = in;
    out.seen = false;
    out.units.clear();
    out.leadX = 925;
    out.approachX = 965;
    EnterPhase(r, Phase::Begin); // the pull's fight is over (that pull's own flow is not under test here)
    r.pullGuid = 0;
    std::string hop;
    for (int i = 0; i < 3 && hop.empty() && r.state == State::Running; ++i)
        hop = Tick(r, out);
    CHECK(r.state == State::Running && hop == "move 965.0 0.0 0.0");
    CHECK(r.note.find("did not bring it") == std::string::npos);
    // An event rewind forgets the stand and the no-path pause's moves.
    Run w = Start(&plan);
    w.approachStandStep = 0;
    w.approachStand = 3;
    w.approachStandResumed = true;
    w.noPathSkips = {1, 2, 3};
    ForgetRewoundRoute(w);
    CHECK(w.approachStandStep == static_cast<std::size_t>(-1) && w.approachStand == 0 && !w.approachStandResumed &&
        w.noPathSkips.empty());
    // A leg never ends within 15 yd of a hostile: a shorter one; none clear: no leg.
    std::vector<PathPoint> path = {{0, 0, 0}, {200, 0, 0}};
    float x = 0, y = 0, z = 0;
    CHECK(PickSafeLeg(path, 120, {}, x, y, z) && std::fabs(x - 120) < 0.01f);
    CHECK(PickSafeLeg(path, 120, {{125, 0, 0, 5}}, x, y, z) && std::fabs(x - 90) < 0.01f);
    CHECK(PickSafeLeg(path, 120, {{125, 0, 0, 5}, {80, 5, 0, 6}}, x, y, z) && std::fabs(x - 60) < 0.01f);
    CHECK(!PickSafeLeg(path, 120, {{120, 0, 0, 5}, {90, 0, 0, 5}, {60, 0, 0, 5}, {30, 0, 0, 5}}, x, y, z));
}

// Sunken Temple r25 slot 4 step 5: `cmd move -449.5 25.9 -148.0` from 157 yd away was
// refused 'too-far: The point is more than 150 yd (AiPlayerbot.ReactDistance) ...' and
// the run paused. The refused walk's move now goes out in legs of at most 120 yd along
// the lead bot's navmesh path (the glue's LegOnPath, PointAlongPath on its segments).
static void TestMoveRefusedTooFarGoesInLegsAlongThePath()
{
    std::string const tooFar = "too-far: The point is more than 150 yd (AiPlayerbot.ReactDistance) from the lead bot. "
        "Move in shorter legs.";
    // The leg is on the path's segments, not on the straight line to its end.
    std::vector<PathPoint> path = {{0, 0, 0}, {100, 0, 0}, {100, 100, 0}};
    float x = 0, y = 0, z = 0;
    CHECK(PointAlongPath(path, 120, x, y, z) && x == 100 && y == 20 && z == 0);
    CHECK(PointAlongPath(path, 500, x, y, z) && x == 100 && y == 100); // shorter: its end
    std::vector<PathPoint> line = {{0, 0, 0}, {200, 0, 10}};
    CHECK(PointAlongPath(line, 120, x, y, z) && std::fabs(x - 119.8f) < 0.1f && std::fabs(z - 6.0f) < 0.1f);
    std::vector<PathPoint> stub = {{0, 0, 0}, {2, 0, 0}};
    CHECK(!PointAlongPath(stub, 120, x, y, z));   // no progress
    CHECK(!PointAlongPath({{0, 0, 0}}, 120, x, y, z));
    CHECK(ReactTooFarAnswer(tooFar));
    CHECK(!ReactTooFarAnswer("too-far: Jinkla is not with Tankkinz (no way onto the line)"));
    CHECK(!ReactTooFarAnswer("unreachable: The lead bot has no walkable path to that point."));
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 210)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.seen = true;
    std::string const move = Tick(r, f);
    CHECK(move == "move 200.0 0.0 0.0");
    CHECK(TooFarLegWanted(r, move, tooFar));
    CHECK(!TooFarLegWanted(r, "move 200.0 0.0 0.0 direct", tooFar));
    CHECK(!TooFarLegWanted(r, "move 10.0 0.0 0.0", tooFar)); // not the walk's own point
    CHECK(!TooFarLegWanted(r, move, "busy: The lead bot is casting."));
    CHECK(TooFarLegTaken(r, f, 120, 0, 0, true, ""));
    CHECK(r.state == State::Running && r.legOn && r.walkLegs == 1);
    CHECK(r.note.find("the move to 200.0 0.0 0.0 is beyond the lead bot's reach: walking a leg of the way first, to "
        "120.0 0.0 0.0 (leg 1)") == 0);
    // On its way: no new order; at the leg's end the walk's own point again, not a try.
    f.leadX = 60;
    CHECK(Tick(r, f).empty());
    f.leadX = 119;
    CHECK(Tick(r, f) == move);
    CHECK(!r.legOn && r.walkTries == 1 && r.state == State::Running);
    CHECK(r.note.find("the leg 120.0 0.0 0.0 walked: on to 200.0 0.0 0.0") == 0);
    // A leg the path winds away on (farther from the point first) is no stall while it gains on the leg.
    Run w = Start(&plan);
    w.restDue = false;
    Facts g = Party(0);
    CHECK(Tick(w, g) == move);
    CHECK(TooFarLegTaken(w, g, 0, 100, 0, true, ""));
    for (int i = 1; i <= 8; ++i)
    {
        g.leadY = 10.0f * i; // away from the point (200 0 0), toward the leg
        CHECK(Tick(w, g).empty());
    }
    CHECK(w.legOn);
    // Stalled on the leg's way: the point again after the short stall.
    std::string sent;
    for (int i = 0; i < 4 && sent.empty(); ++i)
        sent = Tick(w, g);
    CHECK(sent == move && !w.legOn && w.walkTries == 1);
    // Codex review of f1fdeb67: a lead bot that does not move on its leg is a stall, not a
    // leg done: no re-send of the far point after 3 s, no leg spent; after the stall time the
    // 19z43e StallFallback (its own move failing: the dry stand-in of the step's own walk).
    Run st = Start(&plan);
    st.restDue = false;
    Facts still = Party(0);
    still.leadMoveFailed = true;
    still.leadMoveFailReason = "dispatch-short";
    CHECK(Tick(st, still) == move);
    CHECK(TooFarLegTaken(st, still, 120, 0, 0, true, ""));
    int quiet = 0;
    for (int i = 0; i < 200 && !st.stallStandInWanted; ++i)
        if (Tick(st, still).empty())
            ++quiet;
    CHECK(st.stallStandInWanted && st.stallFallback == 2 && st.walkLegs == 1 && st.legOn);
    CHECK(quiet >= static_cast<int>(kWalkRetryMs / kTickMs) - 1);
    // The same leg again (the lead bot made no way): not sent; another one is.
    CHECK(TooFarLegRepeats(st, 121, 0, 0) && !TooFarLegRepeats(st, 125, 0, 0));
    CHECK(TooFarLegRepeatNote(st, 121, 0, 0).find("ends where the last one did, 120.0 0.0 0.0") != std::string::npos);
    Run none = Start(&plan);
    CHECK(!TooFarLegRepeats(none, 0, 0, 0)); // no leg yet this walk
    // A leg stalled after real way toward it (5 yd or more) is done: the far point again.
    Run part = Start(&plan);
    part.restDue = false;
    Facts on = Party(0);
    CHECK(Tick(part, on) == move);
    CHECK(TooFarLegTaken(part, on, 120, 0, 0, true, ""));
    on.leadX = 6;
    std::string again;
    for (int i = 0; i < 5 && again.empty(); ++i)
        again = Tick(part, on);
    CHECK(again == move && !part.legOn);
    // The leg refused too: the refusal goes on as before (the pause).
    Run q = Start(&plan);
    q.restDue = false;
    Facts h = Party(0);
    CHECK(Tick(q, h) == move);
    CHECK(!TooFarLegTaken(q, h, 120, 0, 0, false, "busy: x"));
    OrderRefused(q, move, tooFar);
    CHECK(q.state == State::Paused && q.note.find("too-far") != std::string::npos);
    // Bounded: kMaxTooFarLegs legs a walk, then no more.
    Run b = Start(&plan);
    b.restDue = false;
    Facts k = Party(0);
    CHECK(Tick(b, k) == move);
    for (std::uint32_t i = 0; i < kMaxTooFarLegs; ++i)
    {
        CHECK(TooFarLegWanted(b, move, tooFar));
        CHECK(TooFarLegTaken(b, k, 10.0f * (i + 1), 0, 0, true, ""));
        k.leadX = 10.0f * (i + 1); // the leg walked, the point refused again from here
        CHECK(Tick(b, k) == move);
    }
    CHECK(!TooFarLegWanted(b, move, tooFar));
    // A fresh walk starts the count over.
    k.leadX = 200;
    Tick(b, k);
    CHECK(b.step == 1);
    CHECK(Tick(b, k) == "move 210.0 0.0 0.0" && b.walkLegs == 0 && !b.legOn);
}

// Windhorn Canyon step 1179: the way round the terrace took more than 6 legs of 120 yd.
// Up to kMaxTooFarLegs (16) legs go out before the refusal pauses as before.
static void TestTooFarLegsWalkALongWindingWay()
{
    std::string const tooFar = "too-far: The point is more than 150 yd (AiPlayerbot.ReactDistance) from the lead bot. "
        "Move in shorter legs.";
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 2000)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    std::string const move = Tick(r, f);
    CHECK(move == "move 2000.0 0.0 0.0");
    for (int i = 0; i < 16; ++i)
    {
        CHECK(TooFarLegWanted(r, move, tooFar));
        CHECK(TooFarLegTaken(r, f, 100.0f * (i + 1), 0, 0, true, ""));
        f.leadX = 100.0f * (i + 1); // the leg walked, the point refused again from here
        CHECK(Tick(r, f) == move);
    }
    CHECK(!TooFarLegWanted(r, move, tooFar)); // the 17th leg pauses as before
}

// A leg back at any earlier leg's end is a repeat (Windhorn-style bouncing must
// not walk until the cap); a fresh walk forgets the old legs.
static void TestTooFarLegBackToAnEarlierLegIsARepeat()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 300), Step(PlanStepKind::Move, 400)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    std::string const move = Tick(r, f);
    CHECK(move == "move 300.0 0.0 0.0");
    CHECK(TooFarLegTaken(r, f, 120, 0, 0, true, ""));
    f.leadX = 120;
    CHECK(Tick(r, f) == move);
    CHECK(TooFarLegTaken(r, f, 120, 60, 0, true, ""));
    f.leadY = 60;
    CHECK(Tick(r, f) == move);
    CHECK(TooFarLegRepeats(r, 121, 0, 0)); // near the first leg's end, not the last
    CHECK(!TooFarLegRepeats(r, 125, 0, 0));
    CHECK(TooFarLegRepeatNote(r, 121, 0, 0).find("ends where leg 1 did, 120.0 0.0 0.0") != std::string::npos);
    CHECK(TooFarLegRepeatNote(r, 120, 61, 0).find("ends where the last one did, 120.0 60.0 0.0") != std::string::npos);
    // The step done, the next move sent: a fresh walk forgets the old legs.
    f.leadX = 300;
    f.leadY = 0;
    Tick(r, f);
    CHECK(r.step == 1);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0" && r.walkLegs == 0 && !r.legOn);
    CHECK(!TooFarLegRepeats(r, 121, 0, 0));
}

// Sunken Temple r25 slot 4 step 5: after the pulls of steps 2-4 every resume sent the
// lead bot from beside step 4's pack back up the entrance ramp to step 1 ('the straight
// way to the point of step 1 passes an uncleared pack: replaying the plan's points').
// A lead bot at the last pull or boss the party cleared after the last move point needs
// no walk back; farther, on another floor, or with those fights not cleared, as before.
static void TestResumeWalksBackNoFartherThanTheLastClearedFight()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(100, {1}), PullStep(120, {2}), PullStep(400, {3})};
    auto resumed = [&](Facts f, const std::unordered_set<std::size_t>& notCleared, bool& walkedBack)
    {
        Run r = Start(&plan, 3);
        r.restDue = false;
        r.notCleared = notCleared;
        Pause(r, "8 approach hops did not bring the party within 60.0 yd of the pack");
        CHECK(Resume(r));
        f.seen = false;
        f.approachKnown = true;
        f.approachX = f.leadX + 40;
        f.approachZ = f.leadZ;
        walkedBack = false;
        std::string first;
        for (int i = 0; i < 6 && first.empty() && r.state == State::Running; ++i)
        {
            first = Tick(r, f);
            walkedBack = walkedBack || r.walkBack;
        }
        return std::make_pair(r, first);
    };
    bool walkedBack = true;
    Facts f = Party(130);
    auto got = resumed(f, {}, walkedBack);
    CHECK(!walkedBack && got.first.state == State::Running);
    CHECK(got.second == "move 170.0 0.0 0.0"); // the step's own approach, from here
    // The note of the regroup's tick says why.
    {
        Run r = Start(&plan, 3);
        r.restDue = false;
        Pause(r, "x");
        CHECK(Resume(r));
        Facts g = f;
        g.seen = false;
        StartWalkBack(r, g);
        CHECK(!r.walkBack && r.note == "walk back: none, the lead bot stands at the last cleared step (step 3, 10.0 yd "
            "from its point)");
    }
    // Step 3 left out (out of reach): step 2 counts, 30 yd off.
    got = resumed(f, {2}, walkedBack);
    CHECK(!walkedBack);
    // Neither cleared: the walk back to step 1 as before.
    got = resumed(f, {1, 2}, walkedBack);
    CHECK(walkedBack && got.first.walkBackStart == 0);
    // Farther than 60 yd from the last cleared pack: as before.
    got = resumed(Party(185), {}, walkedBack);
    CHECK(walkedBack);
    // Beside it but on another floor (9 yd below): as before.
    Facts below = Party(125);
    below.leadZ = -9;
    got = resumed(below, {}, walkedBack);
    CHECK(walkedBack);
}

// Uldaman 19z13, Revelosh loot step 20: the lead bot may not loot the corpse
// (quest item 7741, another member's turn). The step is done once any party bot
// carries the item; else the bot whose turn it is, then each living bot, is
// tried before the run pauses.
static void TestLootStepIsDoneWhenAnyPartyBotHasTheItem()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {loot, Step(PlanStepKind::Move, 9)};
    // Before the order.
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5)};
    f.partyHasItem = true;
    f.itemHolder = "Healy";
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note.find("Healy already carries item 7741") != std::string::npos);
    // After a refusal (Healy took the item while the order was out).
    Run q = Start(&plan);
    Facts g = Party(0);
    g.members = f.members;
    std::string const order = Tick(q, g);
    CHECK(order.compare(0, 5, "loot ") == 0);
    OrderRefused(q, order, "failed: This corpse has nothing this bot may loot under the group's loot rules");
    CHECK(q.state == State::Running && q.step == 0);
    g.partyHasItem = true;
    g.itemHolder = "Healy";
    CHECK(Tick(q, g).empty());
    CHECK(q.step == 1 && q.state == State::Running);
}

static void TestLootRefusalTriesTheTurnBotThenEachBot()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {loot, Step(PlanStepKind::Move, 9)};
    char const* rights = "failed: This corpse has nothing this bot may loot under the group's loot rules (another "
        "member's turn, already looted, or not tapped by the group)";
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 6)};
    f.corpseChecked = true;
    f.corpseHasItem = true; // still on the corpse
    std::string order = Tick(r, f);
    CHECK(order.find(" Tanky 7741") != std::string::npos);
    OrderRefused(r, order, rights);
    CHECK(r.state == State::Running);
    // The corpse's round-robin turn is Stabby's: he is tried first.
    f.lootTurn = "Stabby";
    order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Stabby 7741") != std::string::npos);
    CHECK(r.state == State::Running && r.taskBot == "Stabby");
    OrderRefused(r, order, rights);
    // Then each bot not tried yet.
    order = Tick(r, f);
    CHECK(order.find(" Healy 7741") != std::string::npos);
    // His task is accepted and the item lands in his bags: done.
    f.taskReply = 1;
    f.partyHasItem = true;
    f.itemHolder = "Healy";
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);

    // Everybody refused: one pause naming who was tried and the skip hint.
    Run p = Start(&plan);
    Facts g = f;
    g.partyHasItem = false;
    g.taskReply = 0;
    g.lootTurn.clear();
    for (int i = 0; i < 3; ++i)
    {
        order = Tick(p, g);
        CHECK(order.compare(0, 5, "loot ") == 0);
        OrderRefused(p, order, rights);
    }
    Tick(p, g);
    CHECK(p.state == State::Paused);
    CHECK(p.note.find("(tried Tanky Healy Stabby)") != std::string::npos);
    CHECK(p.note.find("item 7741 missing") != std::string::npos);

    // The item is gone from the corpse and in no bag: no point in trying the
    // rest; the pause says so plainly.
    Run e = Start(&plan);
    Facts h = f;
    h.partyHasItem = false;
    h.taskReply = 0;
    h.corpseHasItem = false;
    order = Tick(e, h);
    OrderRefused(e, order, rights);
    Tick(e, h);
    CHECK(e.state == State::Paused);
    CHECK(e.note.find("item 7741 not on corpse and not in any bag") != std::string::npos);
    CHECK(e.note.find("item 7741 missing") != std::string::npos);
    // A refusal that is not about the loot rules still pauses at once.
    Run n = Start(&plan);
    order = Tick(n, f);
    OrderRefused(n, order, "not-found: No corpse of that entry is near the point");
    CHECK(n.state == State::Paused && n.note.find("not-found") != std::string::npos);
    // A bot off the lead bot's map (yd 1000) is never tried, not even on its round-robin turn.
    Run m = Start(&plan);
    Facts k = f;
    k.taskReply = 0;
    k.partyHasItem = false;
    k.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 1000)};
    k.lootTurn = "Stabby";
    order = Tick(m, k);
    OrderRefused(m, order, rights);
    order = Tick(m, k);
    CHECK(order.find(" Healy 7741") != std::string::npos && m.taskBot == "Healy");
}

// ---- loot step: free-for-all detour (AiPlayerbot.DungeonRunLootFfa) ----

static char const* const kFfaRights = "failed: This corpse has nothing this bot may loot under the group's loot rules "
    "(another member's turn, already looted, or not tapped by the group)";

static DungeonPlan FfaPlan()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {loot, Step(PlanStepKind::Move, 9)};
    return plan;
}

// The facts of a party under group loot (3) whose lead bot leads it, the key on.
static Facts FfaParty()
{
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 6)};
    f.lootFfaOn = true;
    f.lootMethod = RemoteCommander::kLootGroup;
    f.leadLeadsGroup = true;
    f.corpseChecked = true;
    f.corpseHasItem = true;
    return f;
}

// Order sequence: loot refused on the rules -> lootmethod ffa -> (its ACK) loot by the carrier -> the task's
// answer -> lootmethod group -> the step ends.
static void TestLootFfaOrderSequence()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    f.lootMethod = RemoteCommander::kLootGroup;
    std::string order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Tanky 7741") != std::string::npos);
    OrderRefused(r, order, kFfaRights);
    // Before NextLootBot: the method is changed first, once.
    CHECK(Tick(r, f) == "lootmethod ffa");
    CHECK(r.lootFfaLive && r.lootRestore == RemoteCommander::kLootGroup && r.lootFfa == LootFfa::Ordered);
    // The order was accepted: the next tick reads ffa and sends the loot to the carrier (not another bot).
    f.lootMethod = RemoteCommander::kLootFfa;
    order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Tanky 7741") != std::string::npos);
    CHECK(r.orderSent && r.lootFfa == LootFfa::Looting && r.step == 0);
    // The task runs: nothing is sent, the method is not touched.
    f.carrierBusy = true;
    for (int i = 0; i < 3; ++i)
        CHECK(Tick(r, f).empty() && r.lootFfa == LootFfa::Looting);
    // The answer: the method goes back first, then the answer is read.
    f.taskReply = 1;
    CHECK(Tick(r, f) == "lootmethod group");
    CHECK(r.lootFfa == LootFfa::Restoring && r.step == 0);
    f.lootMethod = RemoteCommander::kLootGroup;
    f.carrierBusy = false;
    f.carrierHasItem = true;
    f.partyHasItem = true;
    f.itemHolder = "Tanky";
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running && r.lootFfa == LootFfa::Idle);
    CHECK(r.note.find("loot done") != std::string::npos);
    // The next running tick reads the method back and lets the flag go, with no second order.
    CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0);
    CHECK(!r.lootFfaLive && r.lootRestoreTries == 0);
}

// An ERR answer restores too, then the old path goes on (the next bot), without a second ffa.
static void TestLootFfaRestoresOnAnErrAnswerAndNeverTwice()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    f.lootMethod = RemoteCommander::kLootRoundRobin;
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    CHECK(Tick(r, f).compare(0, 5, "loot ") == 0);
    f.carrierBusy = true;
    f.taskReply = 2;
    f.taskText = kFfaRights;
    CHECK(Tick(r, f) == "lootmethod rr"); // the saved method, by the name the order parses
    f.lootMethod = RemoteCommander::kLootRoundRobin;
    f.carrierBusy = false;
    std::string const order = Tick(r, f); // the answer is read: refused on the rules -> the next bot
    CHECK(r.lootFfa == LootFfa::Idle);
    CHECK(order.empty() || order.compare(0, 11, "lootmethod ") != 0);
    for (int i = 0; i < 3; ++i)
    {
        std::string const next = Tick(r, f);
        CHECK(next.compare(0, 11, "lootmethod ") != 0);
        if (next.compare(0, 5, "loot ") == 0)
            OrderRefused(r, next, kFfaRights);
    }
    CHECK(r.lootFfaUsed);
}

// The restore is what the glue sends when the run pauses or stops while the method is ffa: the flag and the
// saved method stay set through a pause (fail-if-removed: lootFfaLive / lootRestore set at the ffa order).
static void TestLootFfaStaysLiveThroughAPause()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    f.lootMethod = RemoteCommander::kLootNeedBeforeGreed;
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    CHECK(Tick(r, f).compare(0, 5, "loot ") == 0);
    // The run pauses (the cap, a player's order) with the loot order out and the method on ffa.
    Pause(r, "something stopped the run");
    CHECK(r.state == State::Paused && r.lootFfaLive && r.lootRestore == RemoteCommander::kLootNeedBeforeGreed);
    CHECK(std::string(RemoteCommander::LootMethodName(r.lootRestore)) == "nbg");
    // A resume starts the step afresh but keeps the flag until the glue has put the method back
    // (a resume can come before its next tick).
    CHECK(Resume(r));
    CHECK(r.lootFfa == LootFfa::Idle && !r.lootFfaUsed && r.lootFfaLive);
}

// The loot task ends with no ACK or ERR line seen (not busy any more, reply 0): the method goes back all the same.
static void TestLootFfaRestoresWhenTheTaskEndsUnanswered()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    CHECK(Tick(r, f).compare(0, 5, "loot ") == 0);
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty());
    f.carrierBusy = false;
    CHECK(Tick(r, f) == "lootmethod group");
}

// A refused ffa order: the old path (NextLootBot) goes on, the method was never changed.
static void TestLootFfaRefusedFallsBackToTheOldPath()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    OrderRefused(r, "lootmethod ffa", "lead-unavailable: The lead bot does not lead the group.");
    CHECK(r.state == State::Running && r.lootFfa == LootFfa::Idle && !r.lootFfaLive);
    f.lootTurn = "Stabby";
    std::string const order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Stabby 7741") != std::string::npos);
    CHECK(r.taskBot == "Stabby");
    // An ACK that did not take (the method still reads group): the same.
    Run q = Start(&plan);
    Facts g = FfaParty();
    OrderRefused(q, Tick(q, g), kFfaRights);
    CHECK(Tick(q, g) == "lootmethod ffa");
    std::string const next = Tick(q, g); // g.lootMethod is still group
    CHECK(next.compare(0, 11, "lootmethod ") != 0 && q.lootFfa == LootFfa::Idle);
    CHECK(q.state == State::Running);
}

// A regroup (a wipe, a lead death, a fight cap) resets the step between the ffa ACK and the restore and keeps the
// run Running: the very next tick orders the method back, whatever the step; the tick after reads it and is done.
static void TestLootFfaRegroupBetweenAckAndRestoreRestores()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    CHECK(Tick(r, f).compare(0, 5, "loot ") == 0);
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty());
    ResetStep(r); // the regroup redoes the step
    CHECK(r.lootFfa == LootFfa::Idle && r.lootFfaLive && r.state == State::Running);
    CHECK(Tick(r, f) == "lootmethod group");
    CHECK(r.lootRestoreTries == 1);
    // It took: the facts read the saved method; no second order, the flag is gone.
    f.lootMethod = RemoteCommander::kLootGroup;
    CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0);
    CHECK(!r.lootFfaLive && r.lootRestoreTries == 0 && r.state == State::Running);
    // The redone step may go through the detour once more: the method is not ffa any more.
    CHECK(r.lootFfaUsed == false);
}

// The restore is refused or does not take: one order a tick, three in all, then the run pauses naming the key.
static void TestLootFfaRestoreRefusedRetriesThenPauses()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    f.lootMethod = RemoteCommander::kLootFfa;
    r.lootFfaLive = true;
    r.lootRestore = RemoteCommander::kLootNeedBeforeGreed;
    for (int i = 1; i <= 3; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order == "lootmethod nbg");
        OrderRefused(r, order, "lead-unavailable: The lead bot does not lead the group.");
        CHECK(r.state == State::Running && r.lootRestoreTries == static_cast<std::uint32_t>(i));
    }
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.lootFfaLive); // the glue tries once more as the run pauses, and clears the flag
    CHECK(r.note.find("still free-for-all") != std::string::npos);
    CHECK(r.note.find("the lead bot's group") != std::string::npos);
    CHECK(r.note.find("AiPlayerbot.DungeonRunLootFfa") != std::string::npos);
    CHECK(r.note.find("lootmethod nbg") != std::string::npos);
}

// Only "no group" or a method that is not ffa ends the flag; the key switched off meanwhile does not.
static void TestLootFfaFlagEndsOnlyWhenTheMethodIsNotFfa()
{
    DungeonPlan plan = FfaPlan();
    // The key was switched off by a reload while the detour was live: the restore still goes out.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        f.lootFfaOn = false;
        f.lootMethod = RemoteCommander::kLootFfa;
        r.lootFfaLive = true;
        r.lootRestore = RemoteCommander::kLootRoundRobin;
        CHECK(Tick(r, f) == "lootmethod rr" && r.lootFfaLive);
    }
    // ... and in the Ordered tick (the key off there, the method ffa): the detour goes on, the flag stays.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        CHECK(Tick(r, f) == "lootmethod ffa");
        f.lootFfaOn = false;
        f.lootMethod = RemoteCommander::kLootFfa;
        CHECK(Tick(r, f).compare(0, 5, "loot ") == 0 && r.lootFfaLive);
    }
    // No group (-1): nothing to put back.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        f.lootMethod = -1;
        r.lootFfaLive = true;
        r.lootRestore = RemoteCommander::kLootGroup;
        CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0 && !r.lootFfaLive);
    }
    // A player set another method meanwhile (Ordered tick reads nbg, not ffa, not the saved one): no order, flag gone.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        CHECK(Tick(r, f) == "lootmethod ffa");
        f.lootMethod = RemoteCommander::kLootNeedBeforeGreed;
        CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0);
        CHECK(!r.lootFfaLive && r.lootFfa == LootFfa::Idle);
    }
}

// A bot dies while the detour waits for a busy carrier; the WaitForFallen regroup does not reset the step: the
// detour ends there and the method is ordered back on the next tick (not after the whole regroup).
static void TestLootFfaOrderedThenFallenRegroupRestores()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty() && r.lootFfa == LootFfa::Ordered);
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Stabby"};
    f.members = {Mem("Tanky", 0), Mem("Healy", 5)};
    Tick(r, f);
    CHECK(r.regroup && r.state == State::Running && r.lootFfa == LootFfa::Idle && r.lootFfaLive);
    // The regroup's ticks: the restore is the first order out.
    CHECK(Tick(r, f) == "lootmethod group");
}

// A carrier that stays busy: the ffa wait is bounded; on the cap the method goes back and the step goes the old way.
static void TestLootFfaOrderedWaitIsBounded()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    f.carrierBusy = true;
    int ticks = 0;
    while (r.lootFfa == LootFfa::Ordered && ticks < 60)
    {
        CHECK(Tick(r, f).empty());
        ++ticks;
    }
    CHECK(ticks >= 19 && ticks <= 22 && r.lootFfa == LootFfa::Idle && r.state == State::Running);
    CHECK(r.lootFfaLive);
    CHECK(Tick(r, f) == "lootmethod group");
}

// No ffa without the item, without the key, without the lead bot leading the group, for a method the order cannot
// restore, or when the item is gone from the corpse; the shortcuts come first.
static void TestLootFfaConditions()
{
    DungeonPlan plan = FfaPlan();
    // No item on the step: the old path (the next bot).
    DungeonPlan bare = FfaPlan();
    bare.steps[0].item = 0;
    {
        Run r = Start(&bare);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        std::string const order = Tick(r, f);
        CHECK(order.compare(0, 11, "lootmethod ") != 0 && r.lootFfa == LootFfa::Idle);
    }
    // Key off (the facts' default), not the leader, already ffa, no group: the old path.
    for (int variant = 0; variant < 4; ++variant)
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        if (variant == 0)
            f.lootFfaOn = false;
        else if (variant == 1)
            f.leadLeadsGroup = false;
        else if (variant == 2)
            f.lootMethod = RemoteCommander::kLootFfa;
        else
            f.lootMethod = -1;
        OrderRefused(r, Tick(r, f), kFfaRights);
        for (int i = 0; i < 2; ++i)
            CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0);
        CHECK(!r.lootFfaLive && r.lootFfa == LootFfa::Idle);
    }
    // The item is not on the corpse and in no bag: no point (the usual pause).
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        f.corpseHasItem = false;
        Tick(r, f);
        CHECK(r.state == State::Paused && r.lootFfa == LootFfa::Idle);
    }
    // 'Already carries' and partyHasItem come first: no order at all.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        f.partyHasItem = true;
        f.itemHolder = "Healy";
        CHECK(Tick(r, f).empty());
        CHECK(r.step == 1 && r.lootFfa == LootFfa::Idle && !r.lootFfaLive);
    }
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        f.carrierHasItem = true;
        CHECK(Tick(r, f).empty() && r.step == 1 && !r.lootFfaLive);
    }
    // A refusal that is not about the loot rules pauses at once, no ffa.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), "not-found: No corpse of that entry is near the point");
        CHECK(r.state == State::Paused && !r.lootFfaLive);
    }
}

// The key off: the facts' defaults (what the glue leaves when the key is 0) give byte-identical orders to the old path.
static void TestLootFfaKeyOffIsTheOldPath()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 6)};
    f.corpseChecked = true;
    f.corpseHasItem = true;
    CHECK(!f.lootFfaOn && f.lootMethod == -1 && !f.leadLeadsGroup);
    std::string order = Tick(r, f);
    CHECK(order == "loot entry:6910@1.0,2.0,3.0 Tanky 7741");
    OrderRefused(r, order, kFfaRights);
    f.lootTurn = "Stabby";
    CHECK(Tick(r, f) == "loot entry:6910@1.0,2.0,3.0 Stabby 7741");
    CHECK(!r.lootFfaUsed && !r.lootFfaLive);
}

// open / use_item go to the bot that carries every input of the step: its item
// (or the item to use) and `param`. Loot another bot keeps for other reasons
// does not count.
static void TestItemCarrierIsPickedFromTheStepsOwnInputs()
{
    std::vector<BagFact> bags(3);
    bags[0].name = "Tanky";
    bags[1].name = "Healy";
    bags[2].name = "Stabby";
    std::string split;
    // The Medallion use: 7740, and the Shaft 7741 in `param`.
    std::vector<std::uint32_t> inputs = {7740, 7741};
    bags[0].held = {7740};
    bags[1].held = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Healy");
    // The lead bot carries all: it keeps the step.
    bags[0].held = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    // The Keystone: only its key matters; two bots carry unrelated loot (the
    // glue lists only the step's own inputs, so those do not show up at all).
    std::vector<std::uint32_t> key = {7733};
    bags[0].held.clear();
    bags[1].held.clear();
    bags[2].held = {7733};
    CHECK(PickItemCarrier(bags, key, "Tanky", &split) == "Stabby");
    CHECK(split.empty());
    // Split: the lead bot stays the carrier and the note says who holds what.
    bags[0].held = {7740};
    bags[1].held = {7741};
    bags[2].held.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    CHECK(split == "Tanky holds 7740; Healy holds 7741");
    // The only carrier is dead or off the map: never chosen; the note says where the item is.
    bags[0].held = {7740};
    bags[1].held = {7740, 7741};
    bags[1].away = "dead";
    split.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    CHECK(split == "Tanky holds 7740; item 7740, 7741 is with Healy, who is dead");
    bags[1].away = "not in the instance";
    split.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    CHECK(split.find("who is not in the instance") != std::string::npos);
    // ... and another available bot that carries everything is preferred over the unavailable one.
    bags[2].held = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Stabby");
    bags[1].away.clear();
    bags[1].held = {7741};
    bags[2].held.clear();
    // Nobody has the first input: no split note (the missing-item rules apply).
    split.clear();
    bags[0].held.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky" && split.empty());
}

// While an open / use_item order is out, its bot stays the carrier. Uldaman:
// Healy (not the lead bot) used the Medallion 7740 with the Shaft 7741; both
// are consumed and the Staff 7733 is in Healy's bags, so no bot carries the
// inputs and PickItemCarrier alone would fall back to the lead bot.
static void TestOutstandingCarrierKeepsTheOrdersBot()
{
    std::vector<BagFact> bags(3);
    bags[0].name = "Tanky";
    bags[0].guid = 11;
    bags[1].name = "Healy";
    bags[1].guid = 12;
    bags[2].name = "Stabby";
    bags[2].guid = 13;
    std::vector<std::uint32_t> inputs = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky") == "Tanky"); // what the bags alone say
    // Still present: pinned, the order's bot keeps it.
    std::string name;
    CHECK(OutstandingCarrierVerdict(true, 12, bags, &name) == CarrierVerdict::Pinned && name == "Healy");
    // No order out yet (or a retried step: ResetStep cleared orderSent), or a new order (carrierRaw 0).
    CHECK(OutstandingCarrierVerdict(false, 12, bags) == CarrierVerdict::NoOrder);
    CHECK(OutstandingCarrierVerdict(true, 0, bags) == CarrierVerdict::NoOrder);
    // That bot left the party, died or went off the map: the order is retired, never re-picked.
    CHECK(OutstandingCarrierVerdict(true, 14, bags) == CarrierVerdict::Retire);
    bags[1].away = "dead";
    CHECK(OutstandingCarrierVerdict(true, 12, bags) == CarrierVerdict::Retire);
    bags[1].away = "not in the instance";
    CHECK(OutstandingCarrierVerdict(true, 12, bags) == CarrierVerdict::Retire);
}

// A retired order (its bot gone) goes out again to the fresh pick; the old
// task is not read as finished from the replacement's bags.
static void TestRetiredUseItemOrderGoesOutAgain()
{
    DungeonPlan plan;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    use.param = 7741;
    plan.steps = {use, Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.carrier = "Healy";
    f.carrierHasEntryItem = true;
    CHECK(Tick(r, f) == "use 7740 Healy");
    CHECK(r.orderSent && r.phase == Phase::AwaitTask);
    // Healy died with the order out; Tanky now carries both inputs.
    RetireOrder(r);
    CHECK(!r.orderSent && r.phase == Phase::Begin && r.state == State::Running);
    Facts g = Party(0);
    g.carrierHasEntryItem = true;
    CHECK(Tick(r, g) == "use 7740 Tanky");
    CHECK(r.state == State::Running && r.step == 0);
    // Without the retire the replacement's bags would end the task: the pause the fix prevents.
    Run q = Start(&plan);
    CHECK(Tick(q, f) == "use 7740 Healy");
    Facts h = Party(0);
    CHECK(Tick(q, h).empty());
    CHECK(q.state == State::Paused && q.note.find("is not in Tanky's bags") != std::string::npos);
}

// The use_item pauses, naming who holds what, when the inputs are split.
static void TestUseItemPausesOnSplitInputs()
{
    DungeonPlan plan;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    use.param = 7741;
    plan.steps = {use};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.carrierHasEntryItem = true;
    f.itemsSplit = "Tanky holds 7740; Healy holds 7741";
    Tick(r, f);
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("no bot carries every item it needs: Tanky holds 7740; Healy holds 7741") != std::string::npos);
    // Healy carries both: the glue names her the carrier and the use goes to her.
    Run q = Start(&plan);
    Facts g = Party(0);
    g.carrier = "Healy";
    g.carrierHasEntryItem = true;
    CHECK(Tick(q, g) == "use 7740 Healy");
}

// Uldaman run 4, archaedas wait_object 135/135: Archaedas was dead and the
// vault door 124369 active, yet "a fight lasted more than 180 s" paused the run.
// The hold's long fight had left fightMs over the plain 180 s ceiling, and the
// combat flag stood a few seconds after the last kill.
static void TestWaitObjectDoneWinsOverAStaleFightTimer()
{
    DungeonPlan plan;
    PlanStep wait = Step(PlanStepKind::WaitObject, 1, 2, 3);
    wait.entry = 124369;
    wait.param = 1;
    plan.steps = {wait, Step(PlanStepKind::Move, 9)};
    // The door is in the wanted state and only the combat flag stands: done.
    Run r = Start(&plan);
    r.fighting = true;
    r.fightMs = kFightCapMs + 5000;
    Facts f = Party(0);
    f.anyInCombat = true;
    f.staleCombat = true;
    f.objectState = 1;
    // One stale snapshot proves nothing: the wait step is finished once the
    // flag has stood unbroken for kStaleConfirmMs.
    Tick(r, f);
    CHECK(r.step == 0 && r.state == State::Running);
    for (std::uint32_t i = 0; i < kStaleConfirmMs / kTickMs && r.step == 0; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    // A break (one engaged tick) starts the confirmation over.
    Run b = Start(&plan);
    b.fighting = true;
    Facts e = f;
    Tick(b, e);
    Tick(b, e);
    e.staleCombat = false;
    Tick(b, e);
    e.staleCombat = true;
    Tick(b, e);
    CHECK(b.step == 0 && b.staleRunMs == kTickMs);
    // A real fight (a hostile engaged) with the door open keeps its handling
    // and its ceiling: the step does not advance.
    Run q = Start(&plan);
    q.fighting = true;
    q.fightMs = kFightCapMs + 5000;
    Facts g = Party(0);
    g.anyInCombat = true;
    g.objectState = 1;
    Tick(q, g);
    CHECK(q.state == State::Paused && q.step == 0 &&
        q.note.find("a fight lasted more than 180 s") != std::string::npos);
    // A stale flag with the door shut is waited out for the grace period (the
    // timer does not run), then the ceiling runs again: it ends in a pause.
    Run p = Start(&plan);
    p.fighting = true;
    p.fightMs = kFightCapMs + 5000;
    Facts h = g;
    h.objectState = 0;
    h.staleCombat = true;
    Tick(p, h);
    CHECK(p.state == State::Running && p.fightMs == kFightCapMs + 5000);
    for (std::uint32_t i = 0; i < kStaleCombatGraceMs / kTickMs + 2 && p.state == State::Running; ++i)
        Tick(p, h);
    CHECK(p.state == State::Paused && p.note.find("a fight lasted more than 180 s") != std::string::npos);
    // The grace is spent once per fight: alternating 19 stale ticks with one
    // engaged tick does not renew it, the ceiling still fires.
    Run a = Start(&plan);
    a.fighting = true;
    a.fightMs = kFightCapMs + 5000;
    Facts k = h;
    for (int i = 0; i < 200 && a.state == State::Running; ++i)
    {
        k.staleCombat = (i % 20) != 19;
        Tick(a, k);
    }
    CHECK(a.state == State::Paused && a.note.find("a fight lasted more than 180 s") != std::string::npos);
    // Two satisfied wait_object steps over one uninterrupted stale flag: the
    // second does not get a fresh grace.
    DungeonPlan two;
    two.steps = {plan.steps[0], plan.steps[0]};
    Run w = Start(&two);
    w.fighting = true;
    Facts z = f;
    for (int i = 0; i < 10 && w.step == 0; ++i)
        Tick(w, z);
    CHECK(w.step == 1 && w.staleMs >= kStaleConfirmMs);
    // The flag has stood this long already; the next step's confirmation is
    // unbroken too, so it finishes at once, the grace is not renewed.
    std::uint32_t const spent = w.staleMs;
    Tick(w, z);
    CHECK(w.staleMs > spent);
    // Stale combat, then a wipe regroup by itself when it clears: the next
    // combat gets a fresh grace and a full confirmation.
    Run g2 = Start(&plan);
    g2.fighting = true;
    g2.staleMs = kStaleCombatGraceMs;
    g2.staleRunMs = kStaleConfirmMs;
    Facts wipe = Party(0);
    wipe.dead = kWipeDead;
    wipe.alive = 2;
    wipe.anyInCombat = false;
    Tick(g2, wipe);
    CHECK(g2.regroup && g2.staleMs == 0 && g2.staleRunMs == 0);
    // The flag clearing resets the grace.
    Run c = Start(&plan);
    c.fighting = true;
    Tick(c, h);
    CHECK(c.staleMs > 0);
    h.anyInCombat = false;
    Tick(c, h);
    CHECK(c.staleMs == 0);
    // A fight that goes on into the next step starts that step's ceiling over.
    Run n = Start(&plan);
    n.fighting = true;
    n.fightMs = kFightCapMs + 5000;
    NextStep(n, "hold done");
    CHECK(n.fightMs == 0 && n.fighting);
}

static void TestPullThatStartsNoFightIsPassed()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(7, 4851, 10)};
    int pulls = 0;
    for (int i = 0; i < 120 && r.state == State::Running && r.step == 0; ++i)
    {
        std::string order = Tick(r, f);
        if (order == "pullback 7")
            ++pulls;
    }
    CHECK(pulls == static_cast<int>(kPullTriesPerUnit));
    // The unit is passed, so the step ends (the unit is alive: it is not "cleared", it is passed).
    CHECK(r.state == State::Done);
}

static void TestAmbushWalksThrough()
{
    DungeonPlan plan;
    plan.steps = {PullStep(20, {4850}, ai::kPlanFlagAmbush)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0");
    f.leadX = 19;
    Tick(r, f);
    CHECK(r.state == State::Done);
}

static void TestBossKillOrderAndTeamGate()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 10);
    boss.entry = 6906;
    boss.entries = {6907, 6908};
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(1, 6906, 5), Unit(2, 6908, 9), Unit(3, 6907, 12)};
    // First listed add (6907) before the nearer boss.
    CHECK(Tick(r, f) == "pullback 3");
    // Not hostile (an Alliance party and the Lost Dwarves): left alone, step done.
    Run r2 = Start(&plan);
    for (UnitFact& u : f.units)
        u.hostile = false;
    Tick(r2, f);
    CHECK(r2.step == 1 && r2.note.find("friendly") != std::string::npos);
}

static void TestHopPullsNearUnitsFirst()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.hopUnits = {Unit(9, 4852, 35)};
    CHECK(Tick(r, f) == "pullback 9");
    f.hopUnits.clear();
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
}

// r36 shadowfang_keep slotI: the walk back replayed step 1 and pulled Deathstalker Adamant 3849, the friendly
// prisoner whose gossip opens the courtyard door; he died and step 130's gossip could not be done (respawn 4 h
// 36 min). A hop pick never takes a unit whose entry a gossip step of the plan
// names, on a move step or on a walk back replay; a pull step's own units are unaffected. Only gossip steps count:
// open/use_on name game objects, use_item an item and ritual encounter data (other id namespaces), so a hostile
// creature sharing such a number is still pulled. The guard is hop-only: an event NPC that turned hostile and is
// engaged with the party is still fought through the engaged/attacker path (PickPull's non-hop picks,
// GuardTarget/EngageTarget), never through a hop pick.
static void TestHopNeverPullsAnEventUnit()
{
    DungeonPlan plan;
    PlanStep gossip;
    gossip.kind = PlanStepKind::Gossip;
    gossip.entry = 3849;
    gossip.param = 1;
    ai::NormalizePlanStep(gossip);
    plan.steps = {Step(PlanStepKind::Move, 40), gossip};
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 3849, 5)};
        CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
        CHECK(r.note.find("3849") == std::string::npos);
    }
    {
        // Beside the event unit, an ordinary hostile is still pulled.
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 3849, 5), Unit(10, 3851, 8)};
        CHECK(Tick(r, f) == "pullback 10");
    }
    {
        // The walk back's replay skips the event unit too and pulls the ordinary one.
        Run r = Start(&plan);
        r.walkBack = true;
        r.walkBackIdx = 0;
        r.step = 1;
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 3849, 5), Unit(10, 3851, 8)};
        const UnitFact* hopUnit = PickPull(r, f.hopUnits, 0, true);
        CHECK(hopUnit && hopUnit->guid == 10);
    }
    {
        // (a) An open step's OBJECT entry equal to a hostile creature's entry does not protect it.
        DungeonPlan openPlan;
        PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
        open.entry = 3851;
        ai::NormalizePlanStep(open);
        openPlan.steps = {Step(PlanStepKind::Move, 40), open};
        Run r = Start(&openPlan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 3851, 5)};
        CHECK(Tick(r, f) == "pullback 9");
    }
    {
        // (b) The same for a use_item ITEM entry.
        DungeonPlan itemPlan;
        PlanStep use;
        use.kind = PlanStepKind::UseItem;
        use.entry = 3851;
        use.item = 7733;
        ai::NormalizePlanStep(use);
        itemPlan.steps = {Step(PlanStepKind::Move, 40), use};
        Run r = Start(&itemPlan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 3851, 5)};
        CHECK(Tick(r, f) == "pullback 9");
    }
    {
        // (d) The Adamant case still skipped: the gossip entry alone protects him as a hop pick.
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 3849, 5)};
        CHECK(!PickPull(r, f.hopUnits, 0, true));
        CHECK(PickPull(r, f.hopUnits, 0, false) && PickPull(r, f.hopUnits, 0, false)->guid == 9);
    }
    {
        // (c) An engaged event NPC is still fought: the guard is hop-only, so a non-hop pick takes him, and the
        // engaged/attacker path (GuardTarget/EngageTarget) is untouched by the guard. The engaged hostile in the
        // step's own units is picked when asked without the hop scope; the hop pick still skips him.
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact adamant = Unit(9, 3849, 5);
        adamant.engaged = true;
        f.units = {adamant};
        f.hopUnits = {adamant};
        CHECK(PickPull(r, f.units, 0, false) && PickPull(r, f.units, 0, false)->guid == 9);
        CHECK(PickPull(r, f.units, 3849, false) && PickPull(r, f.units, 3849, false)->guid == 9);
        CHECK(!PickPull(r, f.hopUnits, 0, true));
        // The fight path sees him: an engaged hostile of the event entry in the party's attackers is a guard target.
        Facts g = Party(0);
        g.anyInCombat = true;
        g.leadAlive = true;
        g.leadIsTank = true;
        g.leadStaying = true;
        g.carrier = "Tanky";
        UnitFact hitting = Unit(9, 3849, 5);
        hitting.hitting = true;
        hitting.engaged = true;
        g.partyAttackers = {hitting};
        CHECK(GuardTarget(r, g) && GuardTarget(r, g)->guid == 9);
    }
    {
        // A pull step's own units are unaffected: entry-gated picks still take the event entry when asked.
        Run r = Start(&plan);
        Facts f = Party(0);
        f.units = {Unit(9, 3849, 5)};
        CHECK(PickPull(r, f.units, 3849) && PickPull(r, f.units, 3849)->guid == 9);
    }
}

// Uldaman rerun pull 58 (build 19z11): the fight ended with the priest and the
// mage dead (fewer than kWipeDead) and the next pullback went out at once. Now
// the dead are revived and the party gathers before the pull.
static void TestFewDeadReviveBeforePull()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 2;
    f.alive = 3;
    // No pull while two are dead: the regroup's revive goes out instead.
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for 2 dead: reviving and gathering") == 0);
    CHECK(Tick(r, f) == "revive party");
    CHECK(Tick(r, f).empty());
    // Up again and gathered: the pull follows.
    f.dead = 0;
    f.alive = 5;
    std::string order;
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 21");
    // In a fight with one dead nothing changes: the fight is the bots' own.
    Run r2 = Start(&plan);
    f.dead = 1;
    f.alive = 4;
    f.anyInCombat = true;
    CHECK(Tick(r2, f).empty());
    CHECK(!r2.regroup && r2.note.find("fighting") == 0);
}

// Review of #1055: the 1-2 dead rule stays out of scripted events (a quiet
// gap between waves is not the end of the fight).
static void TestFewDeadRuleSkipsEvents()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    r.eventOn = true;
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(!r.regroup);
    // A move leading into a hold counts as the event too.
    DungeonPlan holdPlan;
    holdPlan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Hold, 40)};
    Run h = Start(&holdPlan);
    Tick(h, f);
    CHECK(!h.regroup);
}

// Review of #1055: a fight during a regroup (a pack met on the corpse run)
// gets the combat block: fight note, fight ceiling, and a third death reaches
// the regroup by itself.
static void TestRegroupFightsFirst()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    BeginRegroup(r);
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.note == "fighting" && r.fighting);
    // The fight ceiling still holds inside the regroup.
    for (std::uint32_t ms = 0; ms <= kFightCapMs && r.state == State::Running; ms += kTickMs)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("a fight lasted") == 0);
    // Three dead in the regroup's fight: the regroup by itself follows the fight.
    Run w = Start(&plan);
    BeginRegroup(w);
    f.dead = 1;
    f.alive = 4;
    CHECK(Tick(w, f).empty());
    f.dead = 3;
    f.alive = 2;
    CHECK(Tick(w, f).empty());
    CHECK(w.note.find("3 dead: fighting on") == 0);
    f.anyInCombat = false;
    Tick(w, f);
    CHECK(w.regroup && w.autoRegroups == 1);
}

// Review of #1055: a living rezzer gets kPartyRezWaitMs to raise the dead
// before `revive party` releases them; without one the revive goes out at once.
static void TestRegroupWaitsForPartyRez()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 1;
    f.alive = 4;
    f.rezzer = true;
    CHECK(Tick(r, f).empty()); // the regroup starts
    std::uint32_t waited = 0;
    std::string order;
    while (order.empty() && waited < 60000)
    {
        order = Tick(r, f);
        waited += kTickMs;
        if (order.empty())
            CHECK(r.note.find("a resurrection in the party first") != std::string::npos);
    }
    CHECK(order == "revive party" && waited >= kPartyRezWaitMs && waited <= kPartyRezWaitMs + 2 * kTickMs);
    // Raised in time: no revive at all, the pull follows.
    Run q = Start(&plan);
    Tick(q, f);
    Tick(q, f);
    f.dead = 0;
    f.alive = 5;
    order.clear();
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(q, f);
    CHECK(order == "pullback 21");
    // No rezzer: the revive goes out on the regroup's first tick.
    Run n = Start(&plan);
    f.dead = 1;
    f.alive = 4;
    f.rezzer = false;
    Tick(n, f);
    CHECK(Tick(n, f) == "revive party");
}

// The rez hold (AiPlayerbot.DungeonRunRezHold, RezHoldPolicy.h). The roster: a tank, a priest
// healer and three more; the mage (index 2) is dead when `dead` is set.
static std::vector<TortoiseBots::RezHold::Member> RezRoster(bool mageDead, std::uint32_t priestMana, float corpseYd = -1.0f)
{
    using namespace TortoiseBots::RezHold;
    std::vector<Member> m(5);
    m[0].tank = true;
    m[1].rez = RezClass::Priest;
    m[1].healer = true;
    m[1].mana = priestMana;
    m[1].rezCost = 160;
    if (mageDead)
    {
        m[2].dead = true;
        m[2].corpseYd = corpseYd;
    }
    return m;
}

// Ticks a regroup with one dead until a revive goes out; the seconds it took (0: none in 200 s).
static std::uint32_t SecondsToRevive(const Facts& f, std::string* note = nullptr)
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    BeginRegroup(r);
    for (std::uint32_t ms = kTickMs; ms <= 200000; ms += kTickMs)
    {
        std::string const order = Tick(r, f);
        if (note)
            *note = r.note;
        if (order == "revive party")
            return ms / 1000;
    }
    return 0;
}

// Key off (the default): the rez hold's facts are never looked at. A run fed a roster that would
// change the verdict (rezHold false) ticks exactly as one fed none, order and note, tick by tick,
// and the old fixed wait is still the one that decides.
static void TestRezHoldOffIsUnchanged()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Facts plain = Party(0);
    plain.units = {Unit(21, 7011, 30)};
    plain.dead = 1;
    plain.alive = 4;
    plain.rezzer = true;
    Facts withRoster = plain;
    withRoster.rezHold = false;
    withRoster.rezMembers = RezRoster(true, 0, 500.0f); // a priest with no mana and a far body: would send at once
    Run a = Start(&plan);
    Run b = Start(&plan);
    std::uint32_t firstOrder = 0;
    for (std::uint32_t ms = kTickMs; ms <= 300000; ms += kTickMs)
    {
        std::string const oa = Tick(a, plain);
        std::string const ob = Tick(b, withRoster);
        CHECK(oa == ob);
        CHECK(a.note == b.note);
        CHECK(a.regroupMs == b.regroupMs && a.reviveSent == b.reviveSent && a.state == b.state);
        if (!firstOrder && oa == "revive party")
            firstOrder = ms;
    }
    CHECK(firstOrder >= kPartyRezWaitMs && firstOrder <= kPartyRezWaitMs + 3 * kTickMs);
    // No rezzer in the plain facts: the old rule revives at once, with or without the roster.
    plain.rezzer = false;
    withRoster.rezzer = false;
    CHECK(SecondsToRevive(plain) == SecondsToRevive(withRoster));
    CHECK(SecondsToRevive(plain) <= 3);
}

// Key on: the hold waits for the priest well past the old 15 s, waits for its mana, and sends the
// revive when a rezzer is missing, the body is far or the budget is spent.
static void TestRezHoldOn()
{
    using namespace TortoiseBots::RezHold;
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.rezHold = true;
    f.rezzer = true; // the old flag is not what decides any more
    std::string note;
    // A priest with mana: held for kHoldMs (not 15 s), then the revive.
    f.rezMembers = RezRoster(true, 500);
    std::uint32_t s = SecondsToRevive(f, &note);
    CHECK(s >= kHoldMs / 1000 && s <= kHoldMs / 1000 + 3);
    f.rezMembers = RezRoster(true, 500);
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run r = Start(&plan);
        BeginRegroup(r);
        for (int i = 0; i < 30; ++i)
            CHECK(Tick(r, f).empty());
        CHECK(r.note.find("a resurrection in the party first") != std::string::npos);
        CHECK(r.note.find("waiting for a resurrection") != std::string::npos);
    }
    // The priest is short of mana: waited for up to kManaWaitMs.
    f.rezMembers = RezRoster(true, 20);
    s = SecondsToRevive(f, &note);
    CHECK(s >= kManaWaitMs / 1000 && s <= kManaWaitMs / 1000 + 3);
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run r = Start(&plan);
        BeginRegroup(r);
        for (int i = 0; i < 20; ++i)
            CHECK(Tick(r, f).empty());
        CHECK(r.note.find("the resurrector drinks first") != std::string::npos);
    }
    // No resurrector at all: the revive goes out on the first ticks.
    f.rezMembers = RezRoster(true, 500);
    f.rezMembers[1].rez = RezClass::None;
    CHECK(SecondsToRevive(f) <= 3);
    // The body lies too far: no wait either.
    f.rezMembers = RezRoster(true, 500, kCorpseMaxYd + 10.0f);
    CHECK(SecondsToRevive(f) <= 3);
    // Raised in time: no revive, the pull follows.
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run q = Start(&plan);
        f.units = {Unit(21, 7011, 30)};
        f.rezMembers = RezRoster(true, 500);
        BeginRegroup(q);
        for (int i = 0; i < 20; ++i)
            CHECK(Tick(q, f).empty());
        f.dead = 0;
        f.alive = 5;
        f.rezMembers = RezRoster(false, 500);
        std::string order;
        for (int i = 0; i < 6 && order.empty(); ++i)
            order = Tick(q, f);
        CHECK(order == "pullback 21");
    }
    // A revive that went out is not taken back by a resurrector turning up after it (the priest
    // revived at the spirit healer is one): the regroup goes on reviving, it does not hold again.
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run r = Start(&plan);
        BeginRegroup(r);
        f.units.clear();
        f.dead = 2;
        f.alive = 3;
        f.rezMembers = RezRoster(true, 500);
        f.rezMembers[1].rez = RezClass::None;
        f.rezMembers[3].dead = true;
        CHECK(Tick(r, f) == "revive party");
        f.rezMembers[1].rez = RezClass::Priest;
        Tick(r, f);
        CHECK(r.reviveSent && r.note.find("reviving") != std::string::npos);
        CHECK(r.note.find("a resurrection in the party first") == std::string::npos);
        f.dead = 1;
        f.alive = 4;
    }
    // Once the revive went out it is not held back again: the old resend every 60 s still runs.
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run r = Start(&plan);
        BeginRegroup(r);
        f.units.clear();
        f.dead = 1;
        f.alive = 4;
        f.rezMembers = RezRoster(true, 500);
        int revives = 0;
        for (std::uint32_t ms = 0; ms < 200000; ms += kTickMs)
            if (Tick(r, f) == "revive party")
                ++revives;
        CHECK(revives == 3); // at ~60 s, then every 60 s: ~120 s, ~180 s
    }
}

// The rez hold's nudge: a hold that went kRezNudgeMs with a rezzer able and no cast under way sends the lead
// bot onto the body, once; the note gives the rezzer's distance to the body and its sight of it.
// The roster: the priest (1) stands `rezzerYd` from the body of the dead mage (2), which lies `bodyYd` from
// the lead bot (at x 0) on the x axis.
static std::vector<TortoiseBots::RezHold::Member> NudgeRoster(float bodyYd, float rezzerYd, bool sees)
{
    std::vector<TortoiseBots::RezHold::Member> m = RezRoster(true, 500);
    m[1].placed = true;
    m[1].x = bodyYd + rezzerYd;
    m[1].seesBody = sees;
    m[2].placed = true;
    m[2].bodyX = bodyYd;
    m[2].bodyY = 0.0f;
    m[2].bodyZ = 0.0f;
    return m;
}

// Ticks a regroup for `ms`; every non-empty order, in order.
static std::vector<std::string> NudgeOrders(Run& r, const Facts& f, std::uint32_t ms)
{
    std::vector<std::string> orders;
    for (std::uint32_t t = kTickMs; t <= ms; t += kTickMs)
    {
        std::string const o = Tick(r, f);
        if (!o.empty())
            orders.push_back(o);
    }
    return orders;
}

static void TestRezHoldNudge()
{
    using namespace TortoiseBots::RezHold;
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.rezHold = true;
    f.leadAlive = true;
    std::string const move = MoveOrder(20.0f, 0.0f, 0.0f, false);
    // A body 20 yd away: nothing before kRezNudgeMs, then one move onto it, and no second one.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        CHECK(NudgeOrders(r, f, kRezNudgeMs - 2 * kTickMs).empty());
        CHECK(!r.rezNudged);
        std::vector<std::string> const orders = NudgeOrders(r, f, 3 * kTickMs);
        CHECK(orders.size() == 1 && orders[0] == move);
        CHECK(r.rezNudged);
        CHECK(r.note.find("waiting for a resurrection") != std::string::npos);
        CHECK(NudgeOrders(r, f, 30000).empty()); // none a second time
        // The next regroup may nudge again.
        BeginRegroup(r);
        CHECK(!r.rezNudged && r.rezHoldMs == 0);
    }
    // The note names the rezzer's distance to the body and its sight of it.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.rezMembers[1].name = "Lyrieluna";
        f.rezMembers[2].name = "Beardkinz";
        Tick(r, f);
        CHECK(r.note.find("Lyrieluna 18.0 yd from the body, sight no") != std::string::npos);
        f.rezMembers[1].seesBody = true;
        Tick(r, f);
        CHECK(r.note.find("Lyrieluna 18.0 yd from the body, sight yes") != std::string::npos);
    }
    // A body 5 yd from the lead bot: no move at all.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(5.0f, 3.0f, true);
        CHECK(NudgeOrders(r, f, 40000).empty());
        CHECK(!r.rezNudged);
    }
    // A resurrection under way (a cast, or a request not yet accepted): no move.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.rezInProgress = true;
        CHECK(NudgeOrders(r, f, 40000).empty());
        CHECK(!r.rezNudged);
        // The cast ends without a rez: the 12 s run again from there.
        f.rezInProgress = false;
        CHECK(NudgeOrders(r, f, kRezNudgeMs - 2 * kTickMs).empty());
        CHECK(NudgeOrders(r, f, 3 * kTickMs).size() == 1);
    }
    // The resurrector drinks (short of mana): no move, and the clock starts over when it is back.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.rezMembers[1].mana = 20;
        CHECK(NudgeOrders(r, f, 30000).empty());
        f.rezMembers[1].mana = 500;
        CHECK(NudgeOrders(r, f, kRezNudgeMs - 2 * kTickMs).empty());
        CHECK(NudgeOrders(r, f, 3 * kTickMs).size() == 1);
    }
    // Able 8 s, drinking, able again: the 12 s start over from the drink (the same restart as the hold budget's).
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        CHECK(NudgeOrders(r, f, 8000).empty());
        f.rezMembers[1].mana = 20;
        CHECK(NudgeOrders(r, f, 4 * kTickMs).empty());
        f.rezMembers[1].mana = 500;
        CHECK(NudgeOrders(r, f, kRezNudgeMs - 2 * kTickMs).empty());
        CHECK(NudgeOrders(r, f, 3 * kTickMs).size() == 1);
    }
    // A revived straggler whose summon falls due on the very tick the 12 s are reached: the summon goes out
    // (the nudge never replaces an order SummonRevived set), and the nudge follows on a later tick, once.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        r.waitFor.insert("Quelinator"); // died earlier in the run, revived far away
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
        f.members[1].inCombat = true; // a summon is not sent into a fight: not due yet
        CHECK(NudgeOrders(r, f, kRezNudgeMs - kTickMs).empty());
        CHECK(r.rezHoldMs == kRezNudgeMs - kTickMs && !r.rezNudged);
        f.members[1].inCombat = false; // due now: this tick reaches 12 s
        CHECK(Tick(r, f) == "summon Quelinator");
        CHECK(r.rezHoldMs >= kRezNudgeMs && !r.rezNudged);
        CHECK(Tick(r, f) == move); // the next tick: the nudge, once
        CHECK(r.rezNudged);
        CHECK(NudgeOrders(r, f, 5000).empty());
        f.members.clear();
    }
    // A body the glue did not place, or a dead lead bot: no move.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.rezMembers[2].placed = false;
        CHECK(NudgeOrders(r, f, 40000).empty());
        Run q = Start(&plan);
        BeginRegroup(q);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.leadAlive = false;
        CHECK(NudgeOrders(q, f, 40000).empty());
        f.leadAlive = true;
    }
    // The key off: the same facts send no move, ever, and the old wait is unchanged.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezHold = false;
        f.rezzer = true;
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        for (std::string const& o : NudgeOrders(r, f, 60000))
            CHECK(o == "revive party");
        CHECK(!r.rezNudged && r.rezHoldMs == 0);
        f.rezHold = true;
        f.rezzer = false;
    }
    // The revive going out ends the nudge for good.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        std::vector<std::string> const orders = NudgeOrders(r, f, 70000);
        CHECK(orders.size() == 2 && orders[0] == move && orders[1] == "revive party");
        CHECK(r.rezHoldMs == 0 && !r.rezNudged);
    }
}

// T4 (F3): the rez nudge's move refused as unreachable (the Uldaman scorpid pit: the ledge the body lies on is
// not on the navmesh) ends the hold at once and goes to the revive; any other refusal keeps the hold.
static void TestRezNudgeUnreachable()
{
    using namespace TortoiseBots::RezHold;
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.rezHold = true;
    f.leadAlive = true;
    f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
    std::string const move = MoveOrder(20.0f, 0.0f, 0.0f, false);
    std::string const noPath = "unreachable: The lead bot has no walkable path to that point.";
    // Reaches the nudge and checks the move order went out.
    auto nudged = [&](Run& r)
    {
        BeginRegroup(r);
        // Ticks up to the nudge itself (the 15 s party-rezzer wait has not run out then).
        std::string o;
        for (std::uint32_t t = 0; t < 2 * kRezNudgeMs && o.empty(); t += kTickMs)
            o = Tick(r, f);
        CHECK(o == move);
        CHECK(r.rezNudged && r.rezNudgeOrder == move && !r.rezBodyUnreachable);
    };
    // No walkable path: not paused, the very next tick revives.
    {
        Run r = Start(&plan);
        nudged(r);
        OrderRefused(r, move, noPath);
        CHECK(r.state != State::Paused);
        CHECK(r.rezBodyUnreachable && r.rezNudgeOrder.empty());
        CHECK(r.note.find("cannot walk to the body") != std::string::npos);
        CHECK(r.note.find("reviving instead") != std::string::npos);
        CHECK(Tick(r, f) == "revive party");
        CHECK(r.state != State::Paused);
        CHECK(r.note.find("reviving") != std::string::npos);
        CHECK(!r.rezBodyUnreachable); // the revive going out resets it
    }
    // Every path refusal code counts, and a living party rezzer does not bring the 15 s wait back.
    for (char const* code : {"off-mesh: x", "no-ground: x", "no-los: x"})
    {
        Run r = Start(&plan);
        nudged(r);
        f.rezzer = true;
        OrderRefused(r, move, code);
        CHECK(r.state != State::Paused && r.rezBodyUnreachable);
        CHECK(Tick(r, f) == "revive party");
        f.rezzer = false;
    }
    // A refusal for the lead bot's state of the moment: not paused, no revive, the hold kept, no second nudge.
    {
        Run r = Start(&plan);
        nudged(r);
        OrderRefused(r, move, "busy: The lead bot is casting.");
        CHECK(r.state != State::Paused);
        CHECK(!r.rezBodyUnreachable && r.rezNudgeOrder.empty());
        CHECK(r.note.find("refused") != std::string::npos);
        for (std::string const& o : NudgeOrders(r, f, 20000))
            CHECK(o != "revive party" && o != move);
        CHECK(r.state != State::Paused);
    }
    // A refusal of some other order is not taken for the nudge's.
    {
        Run r = Start(&plan);
        nudged(r);
        OrderRefused(r, MoveOrder(99.0f, 0.0f, 0.0f, false), noPath);
        CHECK(!r.rezBodyUnreachable);
    }
    // The next regroup starts clean.
    {
        Run r = Start(&plan);
        nudged(r);
        OrderRefused(r, move, noPath);
        BeginRegroup(r);
        CHECK(!r.rezBodyUnreachable && r.rezNudgeOrder.empty() && !r.rezNudged);
    }
}

// The rez hold's clocks (review of b228d8e8). A priest short of mana for 70 s and then back up keeps
// the whole hold budget from there (before, one shared clock released it the same tick); a cast under
// way finishes past the budget; the 240 s corpse-run budget starts when the hold gave up; the status
// note names who is waited for.
static void TestRezHoldClocks()
{
    using namespace TortoiseBots::RezHold;
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.rezHold = true;
    // Mana back at 70 s: held until 70 s + kHoldMs, then the revive.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        std::uint32_t revivedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 300000 && !revivedAt; ms += kTickMs)
        {
            f.rezMembers = RezRoster(true, ms < 70000 ? 20 : 500);
            if (Tick(r, f) == "revive party")
                revivedAt = ms;
        }
        CHECK(revivedAt >= 70000 + kHoldMs && revivedAt <= 70000 + kHoldMs + 3 * kTickMs);
    }
    // A cast under way when the budget is spent: kCastGraceMs more, once.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        std::uint32_t revivedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 200000 && !revivedAt; ms += kTickMs)
        {
            f.rezInProgress = ms >= 50000;
            if (Tick(r, f) == "revive party")
                revivedAt = ms;
        }
        CHECK(revivedAt >= kHoldMs + kCastGraceMs && revivedAt <= kHoldMs + kCastGraceMs + 3 * kTickMs);
        f.rezInProgress = false;
    }
    // The corpse run keeps its 240 s: the pause comes kRegroupReviveMs after the revive, not after the regroup began.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        std::uint32_t revivedAt = 0, pausedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 400000 && !pausedAt; ms += kTickMs)
        {
            if (Tick(r, f) == "revive party" && !revivedAt)
                revivedAt = ms;
            if (r.state == State::Paused)
                pausedAt = ms;
        }
        CHECK(revivedAt >= kHoldMs && pausedAt >= revivedAt + kRegroupReviveMs &&
              pausedAt <= revivedAt + kRegroupReviveMs + 3 * kTickMs);
        // With the key off the pause stays at kRegroupReviveMs from the regroup's start.
        Run o = Start(&plan);
        BeginRegroup(o);
        Facts off = f;
        off.rezHold = false;
        off.rezMembers.clear();
        off.rezzer = true; // the old 15 s wait first: the budget still runs from the regroup's start
        std::uint32_t offPaused = 0;
        for (std::uint32_t ms = kTickMs; ms <= 400000 && !offPaused; ms += kTickMs)
        {
            Tick(o, off);
            if (o.state == State::Paused)
                offPaused = ms;
        }
        CHECK(offPaused >= kRegroupReviveMs && offPaused <= kRegroupReviveMs + 3 * kTickMs);
    }
    // A rezzer that drinks again after it acted gets the budget again (the clock restarts while it drinks).
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        std::uint32_t revivedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 300000 && !revivedAt; ms += kTickMs)
        {
            // able 0-40 s, drinking 40-60 s, able again from 60 s
            f.rezMembers = RezRoster(true, ms >= 40000 && ms < 60000 ? 20 : 500);
            if (Tick(r, f) == "revive party")
                revivedAt = ms;
        }
        CHECK(revivedAt >= 60000 + kHoldMs && revivedAt <= 60000 + kHoldMs + 3 * kTickMs);
    }
    // A new regroup starts the budget over.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        for (int i = 0; i < 40; ++i)
            CHECK(Tick(r, f).empty());
        BeginRegroup(r);
        std::uint32_t revivedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 200000 && !revivedAt; ms += kTickMs)
            if (Tick(r, f) == "revive party")
                revivedAt = ms;
        CHECK(revivedAt >= kHoldMs && revivedAt <= kHoldMs + 3 * kTickMs);
    }
    // The gather's restart of the regroup clock takes the hold's share of the revive budget with it: the
    // dead gone and one dead again while the gather waits, the pause is kRegroupReviveMs from the gather.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        for (int i = 0; i < 70; ++i)
            Tick(r, f); // the hold gives up at ~60 s, the revive is out
        CHECK(r.reviveSent && r.reviveBaseMs > 0);
        Facts g = f;
        g.dead = 0;
        g.alive = 5;
        g.rezMembers = RezRoster(false, 500);
        g.maxMemberYd = 50.0f;
        g.members = {Mem("Far", 50.0f)};
        Tick(r, g); // the gather goes out and restarts the clock
        CHECK(r.regroupGatherSent && r.reviveBaseMs == 0);
        std::uint32_t pausedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 400000 && !pausedAt; ms += kTickMs)
        {
            Tick(r, f); // dead again
            if (r.state == State::Paused)
                pausedAt = ms;
        }
        CHECK(pausedAt >= kRegroupReviveMs && pausedAt <= kRegroupReviveMs + 3 * kTickMs);
    }
    // The note names who is waited for and whom.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        f.rezMembers[1].name = "Holysis";
        f.rezMembers[2].name = "Magnus";
        Tick(r, f);
        Tick(r, f);
        CHECK(r.note.find("Holysis for Magnus") != std::string::npos);
    }
}

// The regroup's revive is bounded: still dead after kRegroupReviveMs pauses.
static void TestRegroupRevivePauses()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 2;
    f.alive = 3;
    BeginRegroup(r);
    int revives = 0;
    for (std::uint32_t ms = 0; ms <= kRegroupReviveMs + kTickMs && r.state == State::Running; ms += kTickMs)
        if (Tick(r, f) == "revive party")
            ++revives;
    CHECK(r.state == State::Paused && r.note.find("2 still dead after 240 s") != std::string::npos);
    CHECK(revives >= 4); // the first, then again every 60 s
}

// Review of #1055 (owner decision): a bot eating or drinking after the fight
// is resting. Uldaman pulls 17 and 61 (19z11): the tank sat eating at 82 %
// (above the 80 % start line) and the pullback found it seated.
static void TestMealWaitBeforePull()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(31, 4852, 54.7f)};
    f.minHealthPct = 82;
    f.eating = 1;
    f.leadResting = true; // the tank eats (the light rest lines hold only for a seated lead bot)
    CHECK(Tick(r, f).empty());
    CHECK(r.phase == Phase::Rest && r.note.find("1 eating") != std::string::npos);
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).find("pullback") == std::string::npos);
    // The meal ends (the tank stood up full): the pull goes out.
    f.eating = 0;
    f.leadResting = false;
    f.minHealthPct = 100;
    std::string order;
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 31");
    // Eating on and on: kRestCapMs at most, then the pull; once per fight.
    Run c = Start(&plan);
    f.eating = 1;
    f.leadResting = true; // the tank eats (the light rest lines hold only for a seated lead bot)
    f.minHealthPct = 82;
    order.clear();
    std::uint32_t ms = 0;
    for (; ms <= kRestCapMs + 5 * kTickMs && order.find("pullback") == std::string::npos; ms += kTickMs)
        order = Tick(c, f);
    CHECK(order == "pullback 31" && ms >= kRestCapMs);
    // Eating starts a tick after the fight ends (bots sit on their next AI
    // tick), as at pull 17: "pack cleared" on one tick, the next step's pull on
    // the next. The wait still holds that pull.
    DungeonPlan two;
    two.steps = {PullStep(40, {4851}), PullStep(40, {4852})};
    Run late = Start(&two);
    f.eating = 0;
    f.leadResting = false;
    f.minHealthPct = 100;
    f.units = {Unit(30, 4851, 20)};
    CHECK(Tick(late, f) == "pullback 30");
    f.anyInCombat = true;
    Tick(late, f);
    f.anyInCombat = false;
    f.units.clear();
    f.minHealthPct = 82;
    std::string first;
    for (int i = 0; i < 3 && late.step == 0; ++i)
        first = Tick(late, f);
    CHECK(late.step == 1 && first.find("pullback") == std::string::npos);
    f.units = {Unit(31, 4852, 54.7f)};
    f.eating = 1;
    f.leadResting = true; // the tank eats (the light rest lines hold only for a seated lead bot)
    CHECK(Tick(late, f).empty());
    CHECK(late.phase == Phase::Rest && late.restForEating);
    // The same step: the fight ends with the party scattered (a gather goes
    // out, nobody eats yet), and the tank sits down on the next tick.
    Run same = Start(&plan);
    f.eating = 0;
    f.leadResting = false;
    f.minHealthPct = 100;
    f.units = {Unit(30, 4852, 20), Unit(31, 4852, 54.7f)};
    CHECK(Tick(same, f) == "pullback 30");
    f.anyInCombat = true;
    Tick(same, f);
    f.anyInCombat = false;
    f.units = {Unit(31, 4852, 54.7f)};
    f.minHealthPct = 82;
    f.maxMemberYd = 35;
    CHECK(Tick(same, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    f.eating = 1;
    f.leadResting = true; // the tank eats (the light rest lines hold only for a seated lead bot)
    CHECK(Tick(same, f).empty());
    CHECK(same.phase == Phase::Rest && same.restForEating);
    // One budget per fight: after kRestCapMs of meal wait the pull goes out
    // even if the bot sits down again.
    order.clear();
    for (std::uint32_t t = 0; t <= kRestCapMs + 5 * kTickMs && order.find("pullback") == std::string::npos; t += kTickMs)
        order = Tick(late, f);
    CHECK(order == "pullback 31" && late.eatWaitMs >= kRestCapMs);
    // At the end lines already: no wait.
    Run e = Start(&plan);
    f.minHealthPct = 95;
    CHECK(Tick(e, f) == "pullback 31");
    // Not inside an event.
    Run v = Start(&plan);
    v.eventOn = true;
    f.minHealthPct = 82;
    Tick(v, f);
    CHECK(v.phase != Phase::Rest);
}

// Blackfathom Deeps r22 step 85 and r23 slot 6b step 8: after a rest the lead bot stood on "stay" (act "food", then
// "stay") and never took the next walk; the retries every 12 s did not move it and the run paused stuck until a
// resume. The first walk after a rest that the lead bot has not started within kRestWalkResendMs is sent again, once,
// with Run::leadWalkReset (the glue stands it up first), but only in a confirmed stall (RestWalkResendDue: a rest
// state or a failed own move; not moving, not casting, nobody in combat; Codex and Opus review round 1).
static void TestWalkAfterRestIsSentAgain()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    // A meal wait, then the walk.
    auto rested = [&](Run& r, Facts& f)
    {
        f.minHealthPct = 82;
        f.eating = 1;
        f.leadResting = true; // the tank eats (the light rest lines hold only for a seated lead bot)
        CHECK(Tick(r, f).empty());
        CHECK(r.phase == Phase::Rest && r.restForEating && !r.restWalkDue);
        f.eating = 0;
        f.leadResting = false;
        f.minHealthPct = 100;
        std::string order;
        for (int i = 0; i < 3 && order.empty(); ++i)
            order = Tick(r, f);
        CHECK(order == "move 50.0 0.0 0.0");
        CHECK(r.phase == Phase::Walk && r.restWalkDue && !r.leadWalkReset);
    };
    // Ticks until an order or `limit` ms; `ms` is how long it took.
    auto until = [](Run& r, const Facts& f, std::uint32_t limit, std::uint32_t& ms)
    {
        std::string order;
        for (ms = 0; ms < limit && order.empty(); ms += kTickMs)
            order = Tick(r, f);
        return order;
    };
    std::uint32_t ms = 0;
    // A seated (eating) lead bot that does not move: the walk again after kRestWalkResendMs, with the stand-up, once.
    Run r = Start(&plan);
    Facts f = Party(0);
    rested(r, f);
    f.leadResting = true;
    CHECK(until(r, f, kWalkRetryMs, ms) == "move 50.0 0.0 0.0" && ms >= kRestWalkResendMs &&
        ms <= kRestWalkResendMs + kTickMs);
    CHECK(r.leadWalkReset && !r.restWalkDue && r.walkTries == 1);
    CHECK(r.note == "the lead bot did not take the walk after the rest: the walk again, the lead bot stood up");
    r.leadWalkReset = false; // the glue's part
    // Not again: the next order is the old retry after kWalkRetryMs, without the reset.
    CHECK(until(r, f, kWalkRetryMs + 2 * kTickMs, ms) == "move 50.0 0.0 0.0" && ms >= kWalkRetryMs);
    CHECK(!r.leadWalkReset && r.walkTries == 2);
    // A lead bot whose own move failed: the same.
    Run mf = Start(&plan);
    Facts g = Party(0);
    rested(mf, g);
    g.leadMoveFailed = true;
    CHECK(until(mf, g, kWalkRetryMs, ms) == "move 50.0 0.0 0.0" && ms <= kRestWalkResendMs + kTickMs && mf.leadWalkReset);
    // Standing and not failing (no confirmed stall): no early order, the old retry only.
    Run s = Start(&plan);
    Facts h = Party(0);
    rested(s, h);
    CHECK(until(s, h, kWalkRetryMs + 2 * kTickMs, ms) == "move 50.0 0.0 0.0" && ms >= kWalkRetryMs && !s.leadWalkReset);
    // A lead bot casting a heal (seated or not): never cut, the walk waits for the cast; it ends, the walk goes again.
    Run c = Start(&plan);
    Facts k = Party(0);
    rested(c, k);
    k.leadResting = true;
    k.leadCasting = true;
    for (std::uint32_t t = 0; t < 2 * kRestWalkResendMs; t += kTickMs)
        CHECK(Tick(c, k).empty());
    CHECK(c.restWalkDue && !c.leadWalkReset);
    k.leadCasting = false;
    CHECK(Tick(c, k) == "move 50.0 0.0 0.0" && c.leadWalkReset);
    // A slow lead bot still moving (under 2 yd in 3 s): not reset.
    Run m = Start(&plan);
    Facts mv = Party(0);
    rested(m, mv);
    mv.leadResting = true;
    mv.leadMoving = true;
    for (std::uint32_t t = 0; t + kTickMs < kWalkRetryMs; t += kTickMs)
        CHECK(Tick(m, mv).empty());
    CHECK(!m.leadWalkReset);
    // In combat (the lead bot alone, as against units the group cannot attack): never.
    Run cb = Start(&plan);
    Facts cf = Party(0);
    rested(cb, cf);
    cf.leadResting = true;
    cf.leadInCombat = true;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(cb, cf).empty() || !cb.leadWalkReset);
    CHECK(!cb.leadWalkReset);
    // The lead bot takes the walk: the watch ends, no second order even if it sits down later.
    Run p = Start(&plan);
    Facts q = Party(0);
    rested(p, q);
    q.leadX = 10;
    CHECK(Tick(p, q).empty() && !p.restWalkDue);
    q.leadResting = true;
    for (int i = 0; i < 6; ++i)
        CHECK(Tick(p, q).empty());
    // Arrived: the watch ends there (a later stall is not taken for it).
    DungeonPlan two;
    two.steps = {Step(PlanStepKind::Move, 2, 0, 0), Step(PlanStepKind::Move, 50, 0, 0)};
    Run a = Start(&two);
    Facts af = Party(0);
    af.minHealthPct = 82;
    af.eating = 1;
    af.leadResting = true; // the tank eats (the light rest lines hold only for a seated lead bot)
    Tick(a, af);
    CHECK(a.phase == Phase::Rest);
    af.eating = 0;
    af.leadResting = false;
    af.minHealthPct = 100;
    for (int i = 0; i < 3 && a.step == 0; ++i)
        Tick(a, af);
    CHECK(a.step == 1 && !a.restWalkDue);
    // No rest before the walk: a stall waits for the old retry (TestWalkStuckIsBounded).
    Run n = Start(&plan);
    Facts nf = Party(0);
    nf.leadResting = true;
    CHECK(Tick(n, nf) == "move 50.0 0.0 0.0" && !n.restWalkDue);
    for (std::uint32_t t = 0; t + kTickMs < kWalkRetryMs; t += kTickMs)
        CHECK(Tick(n, nf).empty());
    // A fight after the rest ends the watch, and so does a pause.
    Run fi = Start(&plan);
    Facts ff = Party(0);
    rested(fi, ff);
    ff.anyInCombat = true;
    Tick(fi, ff);
    CHECK(!fi.restWalkDue);
    Run pa = Start(&plan);
    Facts pf = Party(0);
    rested(pa, pf);
    Pause(pa, "a player's order");
    CHECK(!pa.restWalkDue);
}

static void TestWipeAndLeadDeathPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    r.autoRegroups = kAutoRegroupsPerStep;
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.note.find("3 dead") == 0);
    // Resume after the regroup: the step starts over.
    f.dead = 0;
    f.alive = 5;
    CHECK(Resume(r));
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // The lead bot dead (the owner's wait rule): the party waits for it, a
    // regroup the run counts; past kAutoRegroupsPerStep it pauses.
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.walkBackDue);
    CHECK(r.note.find("waiting for 1 dead: the lead bot is dead") == 0);
    r.regroup = false; // as if the regroup ended and the lead bot fell again
    r.autoRegroups = kAutoRegroupsPerStep;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("lead bot is dead") != std::string::npos);
}

static void TestFightCeiling()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("fight lasted") != std::string::npos);
}

static void TestTaskSteps()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 123329;
    loot.item = 7740;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    PlanStep wait = Step(PlanStepKind::WaitObject, 7, 8, 9);
    wait.entry = 124372;
    wait.param = 1;
    wait.timeoutS = 60;
    plan.steps = {loot, use, open, wait};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.minHealthPct = 100;
    CHECK(Tick(r, f) == "loot entry:123329@1.0,2.0,3.0 Tanky 7740");
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty());
    // Task over without the item: pause, naming it.
    f.carrierBusy = false;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("7740") != std::string::npos);
    // With the item it goes on.
    CHECK(Resume(r));
    f.carrierHasItem = true;
    Tick(r, f);
    CHECK(r.step == 1);
    // use_item: the staff is not there yet; the medallion is.
    f.carrierHasItem = false;
    f.carrierHasEntryItem = true;
    CHECK(Tick(r, f) == "use 7740 Tanky");
    f.carrierHasItem = true;
    Tick(r, f);
    CHECK(r.step == 2);
    CHECK(Tick(r, f) == "open entry:124371@4.0,5.0,6.0 Tanky");
    Tick(r, f);
    CHECK(r.step == 3);
    // The seal opens after a while; then the plan is done.
    f.objectState = 0;
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    CHECK(r.step == 3 && r.state == State::Running);
    f.objectState = 1;
    Tick(r, f);
    CHECK(r.state == State::Done);
}

static void TestOpenWithoutKeyAndTaskError()
{
    DungeonPlan plan;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    plan.steps = {open};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("lock needs it") != std::string::npos);
    CHECK(Resume(r));
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.carrierBusy = true;
    f.taskReply = 2;
    f.taskText = "locked: needs key";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("locked") != std::string::npos);
}

// ---- task resend (F1): an interrupted cast or an in-combat answer sends the same order again ----

static const char* const kInterruptedText =
    "failed: The opening cast was interrupted after 2.5 s (it did not move and is not in combat).";

// One AwaitTask round: the order is out (carrier busy), then the ERR answer is read.
static std::string AnswerErr(Run& r, Facts& f, const std::string& text)
{
    f.carrierBusy = true;
    f.taskReply = 2;
    f.taskText = text;
    std::string order = Tick(r, f);
    f.carrierBusy = false; // the module's task ended with the answer
    f.taskReply = 0;
    f.taskText.clear();
    return order;
}

static void TestTaskResendAfterInterruptedCast()
{
    for (PlanStepKind kind : {PlanStepKind::Loot, PlanStepKind::Open})
    {
        DungeonPlan plan;
        plan.steps = {GraveStep(kind, 0), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        std::string const first = Tick(r, f);
        CHECK(first.compare(0, 5, kind == PlanStepKind::Loot ? "loot " : "open ") == 0);
        CHECK(r.orderSent && r.phase == Phase::AwaitTask);

        // 1st failure: running, back at Begin, the note says so, the next tick sends the same order.
        AnswerErr(r, f, kInterruptedText);
        CHECK(r.state == State::Running && r.step == 0 && r.phase == Phase::Begin && !r.orderSent);
        CHECK(r.note.find("sending it again (1 of 2)") != std::string::npos);
        CHECK(r.note.find("The opening cast was interrupted after 2.5 s (it did not move and is not in combat): sending") != std::string::npos);
        CHECK(Tick(r, f) == first);
        CHECK(r.orderSent && r.phase == Phase::AwaitTask);

        // 2nd failure: again.
        AnswerErr(r, f, kInterruptedText);
        CHECK(r.state == State::Running && r.step == 0 && !r.orderSent);
        CHECK(r.note.find("sending it again (2 of 2)") != std::string::npos);
        CHECK(Tick(r, f) == first);

        // 3rd failure: the old pause.
        AnswerErr(r, f, kInterruptedText);
        CHECK(r.state == State::Paused && r.step == 0);
        CHECK(r.note.find("failed: " + std::string(kInterruptedText)) != std::string::npos);
    }
}

static void TestTaskResendInCombatAndBusy()
{
    for (const char* text : {"in-combat: The bot is in combat and stopped.", "busy: The chest is in use."})
    {
        DungeonPlan plan;
        plan.steps = {GraveStep(PlanStepKind::Loot, 0), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        std::string const first = Tick(r, f);
        AnswerErr(r, f, text);
        CHECK(r.state == State::Running && r.phase == Phase::Begin && !r.orderSent);
        CHECK(r.note.find("sending it again (1 of 2)") != std::string::npos);
        CHECK(Tick(r, f) == first);
    }
}

// The counter is per step: the second step gets its own two re-sends; a resume resets it too.
static void TestTaskResendCounterResetsPerStep()
{
    DungeonPlan plan;
    plan.steps = {GraveStep(PlanStepKind::Open, 0), GraveStep(PlanStepKind::Open, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    AnswerErr(r, f, kInterruptedText);
    Tick(r, f);
    AnswerErr(r, f, kInterruptedText);
    Tick(r, f);
    Tick(r, f); // the 3rd try ends without an error: step 2
    CHECK(r.step == 1);
    CHECK(r.taskResends == 0);
    for (int i = 0; i < 3 && !r.orderSent; ++i)
        Tick(r, f);
    CHECK(r.orderSent);
    AnswerErr(r, f, kInterruptedText);
    CHECK(r.state == State::Running && r.note.find("sending it again (1 of 2)") != std::string::npos);
}

// A loot step sends to the same bot again: taskBot and the tried list are left as they were.
static void TestTaskResendKeepsTheLootBot()
{
    DungeonPlan plan;
    plan.steps = {GraveStep(PlanStepKind::Loot, 0), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    std::string const bot = r.taskBot;
    std::size_t const tried = r.lootTried.size();
    AnswerErr(r, f, kInterruptedText);
    CHECK(r.taskBot == bot);
    CHECK(r.lootTried.size() == tried);
    CHECK(r.lootRefusal.empty());
}

// ---- open_proof (a use done by a spawned proof object/creature: DungeonRunPolicy.h OpenProofStep) ----
//
// The proof-creature flag is the next free bit (2048) on this line: 1024 is
// kPlanFlagNoDefer (a pull only).
// Other failures still pause at once.

static PlanStep StatueRow(std::uint32_t flags = 0)
{
    PlanStep s = Step(PlanStepKind::Open, -515.553f, 95.2582f, -148.74f);
    s.entry = 148830;
    s.entries = {148883};
    s.radius = 10.0f;
    s.openProof = true;
    s.flags = flags;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}
static const char* const kNoChangeText = "failed: The object showed no change after the bot used it.";
static void TestOpenProofDoneWhenTheProofShows()
{
    DungeonPlan plan;
    plan.steps = {StatueRow(), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    CHECK(order == "open entry:148830@-515.6,95.3,-148.7 Tanky");
    f.carrierBusy = false;
    f.taskReply = 2;
    f.taskText = kNoChangeText;
    f.proofRead = true;
    f.proofUp = true;
    Tick(r, f);
    CHECK(r.step == 1);
    CHECK(r.note == "open 148830 done: object 148883 within 10 yd is up");
}

static void TestOpenProofWaitsForTheTaskToAnswer()
{
    DungeonPlan plan;
    plan.steps = {StatueRow()};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    // The task still holds the bot: the proof alone never ends the step.
    f.carrierBusy = true;
    f.taskReply = 0;
    f.proofRead = true;
    f.proofUp = true;
    Tick(r, f);
    CHECK(r.step == 0 && r.state == State::Running);
}

static void TestOpenProofAckWithoutProofPauses()
{
    DungeonPlan plan;
    plan.steps = {StatueRow()};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    f.carrierBusy = false;
    f.taskReply = 1;
    f.taskText = "done";
    f.proofRead = true;
    f.proofUp = false;
    Tick(r, f);
    CHECK(r.step == 0 && r.state == State::Running);
    CHECK(r.note.find("waiting for object 148883") != std::string::npos);
    for (int i = 0; i < 10 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("object 148883 within 10 yd did not show in 5 s after the use (the task said done)") != std::string::npos);
}

static void TestOpenProofLateProofIsDone()
{
    DungeonPlan plan;
    plan.steps = {StatueRow(), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    f.carrierBusy = false;
    f.taskReply = 1;
    f.taskText = "done";
    f.proofRead = true;
    f.proofUp = false;
    Tick(r, f);
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 0);
    f.proofUp = true;
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running);
}

static void TestOpenProofNotReadIsNoProof()
{
    DungeonPlan plan;
    plan.steps = {StatueRow()};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    f.carrierBusy = false;
    f.taskReply = 2;
    f.taskText = kNoChangeText;
    f.proofRead = false;
    f.proofUp = true;
    for (int i = 0; i < 10 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
}

static void TestOpenProofAlreadyUpSkipsTheUse()
{
    DungeonPlan plan;
    plan.steps = {StatueRow(), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.proofRead = true;
    f.proofUp = true;
    CHECK(Tick(r, f).empty());
    CHECK(!r.orderSent);
    CHECK(r.step == 1);
    CHECK(r.note.size() >= std::string("done before the use (not used again)").size() &&
        r.note.compare(r.note.size() - std::string("done before the use (not used again)").size(),
        std::string("done before the use (not used again)").size(), "done before the use (not used again)") == 0);
}

static void TestOpenProofResendsAnInCombatAnswer()
{
    DungeonPlan plan;
    plan.steps = {StatueRow(), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    f.carrierBusy = false;
    f.taskReply = 2;
    f.taskText = "in-combat: the bot is fighting";
    f.proofRead = true;
    f.proofUp = false;
    Tick(r, f);
    CHECK(!r.orderSent && r.step == 0 && r.state == State::Running);
    CHECK(r.note.find("sending it again (1 of") != std::string::npos);
    // The order goes out again; the same answer with the proof up is done, not sent again.
    f.taskReply = 0;
    f.taskText.clear();
    CHECK(!Tick(r, f).empty() && r.orderSent);
    f.carrierBusy = false;
    f.taskReply = 2;
    f.taskText = "in-combat: the bot is fighting";
    f.proofUp = true;
    Tick(r, f);
    CHECK(r.step == 1);
}

static void TestOpenProofCreatureName()
{
    DungeonPlan plan;
    PlanStep s = StatueRow(ai::kPlanFlagProofCreature);
    plan.steps = {s, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    f.carrierBusy = false;
    f.taskReply = 1;
    f.taskText = "done";
    f.proofRead = true;
    f.proofUp = true;
    Tick(r, f);
    CHECK(r.step == 1);
    CHECK(r.note.find("creature 148883 within 10 yd") != std::string::npos);
}

static void TestProofStepIsTheCurrentOpenProofOnly()
{
    // The helper the glue reads the proof through: the actual current step when it is an
    // open_proof (StatueRow), null for anything else and null on a walk back whose replayed
    // point is that same open_proof (FactStep still replays it: movement facts are unchanged).
    DungeonPlan plan;
    plan.steps = {StatueRow(), Step(PlanStepKind::Pull, 60), PullStep(120, {4851})};
    Run r = Start(&plan);
    CHECK(ProofStep(r) == &plan.steps[0]);
    r.step = 1;
    CHECK(ProofStep(r) == nullptr);
    r.step = 2;
    r.walkBack = true;
    r.walkBackIdx = 0;
    CHECK(FactStep(r) == &plan.steps[0]);
    CHECK(ProofStep(r) == nullptr);
}

static void TestOpenProofWalkBackStepDoesNotFinishIt()
{
    // Glue-side rule (BotCommands.cpp ReadRunFacts): the proof is read only for the actual current
    // step, never for FactStep's walk-back replayed point. So a walk back whose replayed point is an
    // old open_proof with its proof up must not finish the current (different) step: the glue leaves
    // proofRead false and the policy waits instead of advancing.
    DungeonPlan plan;
    plan.steps = {StatueRow(), Step(PlanStepKind::Pull, 60), PullStep(120, {4851})};
    Run r = Start(&plan);
    r.step = 2; // current step: the pull, not either open_proof
    r.walkBack = true;
    r.walkBackIdx = 0; // replayed point: the old open_proof, its proof left up in the world
    CHECK(FactStep(r) == &plan.steps[0]);
    Facts f = Party(0);
    f.proofRead = false; // the glue reads no proof: the current step is no open_proof
    f.proofUp = true;
    Tick(r, f);
    CHECK(r.step == 2 && r.state == State::Running);
}

static void TestPlainOpenStillPausesWithoutChange()
{
    DungeonPlan plan;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    plan.steps = {open};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    f.carrierBusy = false;
    f.taskReply = 2;
    f.taskText = kNoChangeText;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("open failed: ") != std::string::npos);
}

// Other failures still pause at once.
static void TestTaskResendNotForOtherFailures()
{
    for (const char* text : {"locked: needs key", "failed: The bot was interrupted by a spell.", "too-far: x"})
    {
        DungeonPlan plan;
        plan.steps = {GraveStep(PlanStepKind::Open, 0), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        AnswerErr(r, f, text);
        CHECK(r.state == State::Paused && r.step == 0);
        CHECK(r.note.find("sending it again") == std::string::npos);
    }
}


// ---- gossip step (talk to an NPC: `cmd gossip <entry> <lead bot> <option>`) ----

// Weegli Blastfuse (7607): option 1, the top of his menu, at x.
static PlanStep GossipRow(float x, std::uint32_t option = 1, std::uint32_t entry = 7607)
{
    PlanStep s;
    s.kind = PlanStepKind::Gossip;
    s.entry = entry;
    s.x = x;
    s.param = option;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}

// The one order goes out once, the busy task is waited for, the ACK ends the step.
static void TestGossipOrderOnceThenAck()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
    CHECK(r.orderSent && r.phase == Phase::AwaitTask);
    // The lead bot's task is under way: no second order, the step stays.
    f.carrierBusy = true;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty() && r.step == 0 && r.state == State::Running);
    // The ACK.
    f.taskReply = 1;
    f.taskText = "1|Will you blow up that door now?";
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running && r.note == "gossip 7607 option 1 done");
}

// An ERR answer pauses with the module's own words; .bot run resume sends the order again.
static void TestGossipErrPauses()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10, 3)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7607 Tanky 3");
    f.taskReply = 2;
    f.taskText = "no-option: No option 3. The menu offers: 1 \"Will you blow up that door now?\".";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("gossip 7607 option 3 failed") != std::string::npos);
    CHECK(r.note.find("no-option: No option 3.") != std::string::npos);
    CHECK(Resume(r));
    f.taskReply = 0;
    f.taskText.clear();
    for (int i = 0; i < 400 && !r.orderSent; ++i) // the resume's regroup first
    {
        std::string order = Tick(r, f);
        if (order.compare(0, 7, "gossip ") == 0)
            break;
    }
    CHECK(r.orderSent);
}

// The NPC a run-bound bot may talk to beyond AiPlayerbot.BotGossipNpcs (RunGossipStepOf, the
// glue's RunGossipStepOfBot): the entry (and alternatives) of the CURRENT gossip step of its
// running or paused run, read from the loaded plan. SFK r27 slot7: Deathstalker Adamant (3849)
// was refused not-allowed although the plan's own gossip step named him.
static void TestRunGossipStepOfReadsTheCurrentStep()
{
    using TortoiseBots::GossipOrder::MayTalkTo;
    std::set<std::uint32_t> const none;
    DungeonPlan plan;
    PlanStep adamant = GossipRow(10, 1, 3849);
    adamant.entries = {3850};
    plan.steps = {Step(PlanStepKind::Rest), adamant, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    // Outside a run (no run): refused.
    CHECK(!RunGossipStepOf(nullptr).bound && !MayTalkTo(none, RunGossipStepOf(nullptr), 3849));
    // In the run, but at a step that is not the gossip: refused.
    CHECK(r.step == 0 && RunGossipStepOf(&r).bound && !RunGossipStepOf(&r).atGossip);
    CHECK(!MayTalkTo(none, RunGossipStepOf(&r), 3849));
    // At the gossip step: its entry, nothing else (its `entries` are not ordered, so not allowed).
    r.step = 1;
    CHECK(MayTalkTo(none, RunGossipStepOf(&r), 3849) && !MayTalkTo(none, RunGossipStepOf(&r), 3850));
    CHECK(!MayTalkTo(none, RunGossipStepOf(&r), 7607));
    // Paused at it (the owner's manual retry): still that step.
    Pause(r, "test");
    CHECK(r.state == State::Paused && MayTalkTo(none, RunGossipStepOf(&r), 3849));
    // Past it: refused again.
    r.state = State::Running;
    r.step = 2;
    CHECK(!MayTalkTo(none, RunGossipStepOf(&r), 3849));
    // A finished run binds nothing.
    r.step = 1;
    r.state = State::Done;
    CHECK(!RunGossipStepOf(&r).bound && !MayTalkTo(none, RunGossipStepOf(&r), 3849));
    // A run without a plan, or a step past the end: nothing.
    Run empty;
    CHECK(!MayTalkTo(none, RunGossipStepOf(&empty), 3849));
    Run past = Start(&plan);
    past.step = 9;
    CHECK(!MayTalkTo(none, RunGossipStepOf(&past), 3849));
}

// The area triggers a run-bound bot may fire beyond AiPlayerbot.BotAreaTriggers (RunArmedTriggersOf, the
// glue's RunArmedAreaTriggers): a move / move_direct step's `entry` is armed from the tick the run reaches
// that step UNTIL the trigger has fired once for this run (NoteTriggerFired), so a run that steps past the
// move (to the boss) before any bot's 500 ms check ran inside the sphere still fires it; it is never armed
// again after that. Read from the loaded plan.
static void TestRunArmsAMoveTriggerUntilItFired()
{
    DungeonPlan plan;
    PlanStep walkIn = Step(PlanStepKind::Move);
    walkIn.entry = 962;
    PlanStep boss = Step(PlanStepKind::Boss);
    boss.entry = 7271;
    PlanStep again = Step(PlanStepKind::MoveDirect);
    again.entry = 962; // the same trigger named again later: not re-armed once fired
    PlanStep other = Step(PlanStepKind::MoveDirect);
    other.entry = 963;
    plan.steps = {Step(PlanStepKind::Rest), walkIn, boss, again, other, Step(PlanStepKind::Rest)};
    CHECK(RunArmedTriggersOf(nullptr) == nullptr);
    Run r = Start(&plan);
    ArmReachedTriggers(r);
    CHECK(RunArmedTriggersOf(&r) == nullptr); // step 0: nothing reached yet
    // The run steps past the move to the boss step within its ticks, before any trigger check ran;
    // the tick that reaches the move arms its trigger before it returns (no later tick needed).
    Facts f = Party(0);
    bool armedOnArrival = false;
    for (int i = 0; i < 200 && r.step < 1; ++i)
    {
        Tick(r, f);
        if (r.step >= 1)
            armedOnArrival = RunArmedTriggersOf(&r) != nullptr;
    }
    CHECK(r.step >= 1 && armedOnArrival);
    r.step = 2;   // at the boss step 102
    r.state = State::Running;
    ArmReachedTriggers(r);
    const std::vector<std::uint32_t>* armed = RunArmedTriggersOf(&r);
    CHECK(armed && *armed == std::vector<std::uint32_t>{962}); // still armed at the boss
    Pause(r, "test");
    CHECK(RunArmedTriggersOf(&r) != nullptr);                  // a paused run keeps it armed
    r.state = State::Running;
    // It fired once: never armed again for this run, also not when a later step names it again.
    NoteTriggerFired(r, 962);
    CHECK(RunArmedTriggersOf(&r) == nullptr);
    r.step = 3;
    ArmReachedTriggers(r);
    CHECK(RunArmedTriggersOf(&r) == nullptr);
    r.step = 4;
    ArmReachedTriggers(r);
    armed = RunArmedTriggersOf(&r);
    CHECK(armed && *armed == std::vector<std::uint32_t>{963});
    NoteTriggerFired(r, 5000);                                 // a listed trigger firing changes nothing here
    CHECK(RunArmedTriggersOf(&r) && RunArmedTriggersOf(&r)->size() == 1);
    // A done run arms nothing.
    r.state = State::Done;
    CHECK(RunArmedTriggersOf(&r) == nullptr);
    // A run started past the move never arms its trigger; a step that is no move arms nothing.
    Run late = Start(&plan, 2);
    ArmReachedTriggers(late);
    CHECK(RunArmedTriggersOf(&late) == nullptr);
    // The arming runs inside Tick: a run ticked onto the move arms it without a separate call.
    Run ticked = Start(&plan);
    ticked.step = 1;
    Tick(ticked, Party(0));
    CHECK(RunArmedTriggersOf(&ticked) && RunArmedTriggersOf(&ticked)->front() == 962);
    // Past the plan's end nothing new is armed, and nothing reads out of range.
    Run past = Start(&plan);
    past.step = 99;
    ArmReachedTriggers(past);
    CHECK(past.armedTriggers.size() == 2 && past.armedTriggers[0] == 962 && past.armedTriggers[1] == 963);
}

// The order's immediate ERR: an NPC that AiPlayerbot.BotGossipNpcs does not list pauses naming the key.
static void TestGossipNotAllowedNamesTheConfigKey()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string const order = Tick(r, f);
    CHECK(order == "gossip 7607 Tanky 1");
    OrderRefused(r, order, "not-allowed: That NPC is not in AiPlayerbot.BotGossipNpcs, so bots may not talk to it.");
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("cmd gossip 7607 Tanky 1 refused") != std::string::npos);
    CHECK(r.note.find("AiPlayerbot.BotGossipNpcs") != std::string::npos);
}

// Plan ZF-EVENT-HOLD T7: the order's immediate ERR for an NPC that lies dead (r12-19z29 step 220: Weegli
// Blastfuse 7607 dead at the door): the first refusal pauses plainly, says a resume will not help and
// names skip and stop; no retry is asked for. The words say what the module found (none alive within
// 40 yd, a dead one nearby), not that the NPC is dead everywhere.
static void TestGossipDeadNpcPausesPlainly()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string const order = Tick(r, f);
    CHECK(order == "gossip 7607 Tanky 1");
    OrderRefused(r, order,
        "no-npc: No living NPC of that entry is within 40 yd of the bot: a dead one lies nearby: Weegli Blastfuse (7607) "
        "lies dead 3 yd away and respawns in 6 d 23 h (604800 s spawn timer).");
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("gossip 7607 option 1 cannot be done") != std::string::npos);
    CHECK(r.note.find("none alive within 40 yd, a dead one lies nearby (Weegli Blastfuse (7607), respawns in 6 d 23 h)") !=
        std::string::npos);
    CHECK(r.note.find("resume will not help") != std::string::npos);
    CHECK(r.note.find(".bot run skip") != std::string::npos && r.note.find(".bot run stop") != std::string::npos);
    CHECK(r.note.find("refused") == std::string::npos);
    // A scripted NPC with no respawn timer.
    Run q = Start(&plan);
    Tick(q, f);
    OrderRefused(q, order,
        "no-npc: No living NPC of that entry is within 40 yd of the bot: a dead one lies nearby: Weegli Blastfuse (7607) "
        "lies dead 3 yd away (it has no respawn timer).");
    CHECK(q.state == State::Paused && q.note.find("(Weegli Blastfuse (7607), it never respawns)") != std::string::npos);
    CHECK(q.note.find("respawns in") == std::string::npos);
}

// T8: the plain no-npc text (nothing dead there either) keeps its text.
static void TestGossipNoNpcKeepsItsText()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string const order = Tick(r, f);
    OrderRefused(r, order, "no-npc: No living NPC of that entry is within 40 yd of the bot.");
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("cmd gossip 7607 Tanky 1 refused: no-npc: No living NPC of that entry is within 40 yd of the bot.") !=
        std::string::npos);
    CHECK(r.note.find("cannot be done") == std::string::npos);
    // The mark outside a gossip step's own order is not read either.
    DungeonPlan other;
    other.steps = {Step(PlanStepKind::Rest)};
    Run o = Start(&other);
    OrderRefused(o, "attack 5 Tanky", "invalid-target: it lies dead there");
    CHECK(o.note.find("cannot be done") == std::string::npos);
}

// T9: the later failure (the NPC died before the bot reached it, the module's own words) gives the same plain note.
static void TestGossipDeadNpcTaskReply()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
    f.taskReply = 2;
    f.taskText = "no-npc: The NPC died or despawned before the bot reached it: Weegli Blastfuse (7607) lies dead 3 yd away "
                 "and respawns in 6 d 23 h (604800 s spawn timer).";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("gossip 7607 option 1 cannot be done") != std::string::npos);
    CHECK(r.note.find("(Weegli Blastfuse (7607), respawns in 6 d 23 h)") != std::string::npos);
    CHECK(r.note.find("resume will not help") != std::string::npos);
    // A late failure without the mark keeps today's note.
    Run q = Start(&plan);
    Tick(q, f);
    f.taskText = "no-npc: The NPC died or despawned before the bot reached it.";
    Tick(q, f);
    CHECK(q.note.find("gossip 7607 option 1 failed: no-npc: The NPC died or despawned") != std::string::npos);
}

// use_on: Zul'Farrak's gong 141832, rung with the Mallet of Zul'Farrak 9240.
static PlanStep GongRow()
{
    PlanStep s;
    s.kind = PlanStepKind::UseOn;
    s.entry = 141832;
    s.item = 9240;
    s.x = 1650.9f;
    s.y = 1171.9f;
    s.z = 10.9f;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}

// One targeted use order by the carrier that holds the item; the busy task is waited for; only the ACK ends the step
// (a free carrier with no answer yet is no answer).
static void TestUseOnOneTargetedOrderThenAck()
{
    CHECK(Supported(PlanStepKind::UseOn));
    DungeonPlan plan;
    plan.steps = {GongRow(), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    f.carrierBusy = true; // an earlier order of the carrier: wait, send nothing
    CHECK(Tick(r, f).empty() && !r.orderSent && r.note.find("waiting for Tanky") != std::string::npos);
    f.carrierBusy = false;
    CHECK(Tick(r, f) == "use 9240 Tanky entry:141832@1650.9,1171.9,10.9");
    CHECK(r.orderSent && r.phase == Phase::AwaitTask && r.note == "use_on 141832 with item 9240 by Tanky");
    f.carrierBusy = true;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty() && r.step == 0);
    f.carrierBusy = false; // free again, but no ACK read yet
    CHECK(Tick(r, f).empty() && r.step == 0 && r.state == State::Running);
    f.taskReply = 1;
    f.taskText = "1|spell 11434";
    f.objectReacted = true; // the gong's state changed under the use (ObjectEffectOf at the ACK)
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "use_on 141832 with item 9240 done: 1|spell 11434");
    CHECK(r.useOnRang.count(0) == 1);
}

// The ACK alone proves nothing: the step ends only once the object reacted or the boss row's boss is in sight
// (alive or dead); else, after kUseOnProofMs, an honest pause that says the ACK came but nothing happened.
static void TestUseOnAckNeedsProof()
{
    PlanStep boss = Step(PlanStepKind::Boss, 1702.6f, 1224.2f, 8.9f);
    boss.entry = 7273;
    DungeonPlan plan;
    plan.steps = {GongRow(), boss};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.taskReply = 1;
    f.taskText = "1|spell 11434";
    Tick(r, f);
    CHECK(r.step == 0 && r.state == State::Running && r.useOnRang.count(0) == 1);
    CHECK(r.note.find("ACKed") != std::string::npos);
    // The boss appears: done.
    Run b = r;
    Facts g = f;
    g.summonSeen = true;
    Tick(b, g);
    CHECK(b.step == 1 && b.note.find("boss 7273 is here") != std::string::npos);
    // Nothing shows within the proof window: an honest pause, not the boss row's search note.
    for (std::uint32_t ms = 0; ms <= kUseOnProofMs && r.state == State::Running; ms += kTickMs)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("the order was ACKed but object 141832 showed no change and boss 7273 did not show") != std::string::npos);
}

// Never a second use once the act started or the item took: an in-combat answer after the cast began (the walking use
// task fails in a fight after its act started) pauses instead of sending again; a boss already here is no reason to use it.
static void TestUseOnNoResendOnceTheActStarted()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.taskReply = 2;
    f.taskText = "in-combat: The bot is in combat and stopped.";
    f.taskActStarted = true;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.taskResends == 0 && r.note.find("the cast had started") != std::string::npos);
    // In combat before the act, but the boss row's boss is already in sight: no second use either.
    PlanStep boss = Step(PlanStepKind::Boss, 1702.6f, 1224.2f, 8.9f);
    boss.entry = 7273;
    DungeonPlan withBoss;
    withBoss.steps = {GongRow(), boss};
    Run s = Start(&withBoss);
    Facts g = Party(1650.9f);
    g.carrierHasItem = true;
    CHECK(!Tick(s, g).empty());
    g.taskReply = 2;
    g.taskText = "in-combat: The bot is in combat and stopped.";
    g.summonSeen = true;
    Tick(s, g);
    CHECK(s.taskResends == 0 && s.state == State::Running && s.step == 1);
    // A boss already here before any order: the step ends without a use.
    Run t = Start(&withBoss);
    Facts h = Party(1650.9f);
    h.carrierHasItem = true;
    h.summonSeen = true;
    CHECK(Tick(t, h).empty() && t.step == 1);
}

// A wipe after the ACK (ResetStep clears orderSent and the resends) never sends the use again: the per-run latch holds,
// and a run with no boss in sight pauses, naming why.
static void TestUseOnResetAfterAckDoesNotUseAgain()
{
    PlanStep boss = Step(PlanStepKind::Boss, 1702.6f, 1224.2f, 8.9f);
    boss.entry = 7273;
    DungeonPlan plan;
    plan.steps = {GongRow(), boss};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.taskReply = 1;
    Tick(r, f);
    CHECK(r.useOnRang.count(0) == 1);
    ResetStep(r); // a wipe or the lead bot's death
    f.taskReply = 0;
    f.taskText.clear();
    CHECK(Tick(r, f).empty() && !r.orderSent);
    CHECK(r.state == State::Paused && r.note.find("used once already this run") != std::string::npos);
    CHECK(Resume(r));
    for (int i = 0; i < 400 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).compare(0, 4, "use ") != 0);
}

// The item is in a dead bot's bags: the pause names that bot (revive it), not a second item.
static void TestUseOnNamesADeadHolder()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = false;
    f.itemsSplit = "item 9240 is with Tanky, who is dead";
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.note.find("item 9240 is with Tanky, who is dead") != std::string::npos);
    CHECK(r.note.find(".additem") == std::string::npos);
}

// No bot of the party carries the item: a pause that says so (the item is the operator's to give), no order.
// Spec item-leg-skip C2/F7: an optional leg's first quiet tick with nobody
// carrying the item skips the whole leg: r.step jumps to leg 10's first step,
// skippedLegs == {9}, the note names the skip, no order goes out. Run to the
// end: the done note lists the skip and names no skipped boss.
static void TestOptionalLegSkippedWithoutItem()
{
    DungeonPlan plan;
    plan.name = "zf";
    ai::PlanLeg leg9;
    leg9.leg = 9;
    leg9.name = "gahzrilla";
    plan.legs.push_back(leg9);
    ai::PlanLeg leg10;
    leg10.leg = 10;
    leg10.name = "courtyard";
    plan.legs.push_back(leg10);
    PlanStep move = Step(PlanStepKind::Move, 0, 0, 0);
    move.leg = 9;
    PlanStep pull = PullStep(10, {7580});
    pull.leg = 9;
    PlanStep gong = GongRow();
    gong.leg = 9;
    PlanStep boss = Step(PlanStepKind::Boss, 20, 0, 0);
    boss.leg = 9;
    boss.entry = 7273;
    boss.entries = {7273};
    ai::NormalizePlanStep(boss);
    PlanStep next = Step(PlanStepKind::Move, 100, 0, 0);
    next.leg = 10;
    plan.steps = {move, pull, gong, boss, next};
    ai::PlanLegNeed need;
    need.leg = 9;
    need.item = 9240;
    need.mode = ai::LegNeedMode::Optional;
    need.name = "Mallet of Zul'Farrak";
    plan.needs.push_back(need);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.legNeedRead = true;
    f.legNeedHolders = {{9240, ""}};
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 4);
    CHECK(r.skippedLegs.size() == 1 && r.skippedLegs[0] == 9);
    CHECK(r.note.find("leg gahzrilla skipped: it needs item 9240 (Mallet of Zul'Farrak)") != std::string::npos);
    CHECK(r.state == State::Running);
    CHECK(r.notCleared.count(1) == 1 && r.notCleared.count(3) == 1);
    CHECK(r.legNeedDecided.count(9) == 1);
    // Run to the end: the last move point walks, the run finishes.
    f.legNeedRead = false;
    f.leadX = 99;
    for (int i = 0; i < 400 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Done);
    CHECK(r.note.find("optional leg skipped: gahzrilla (item 9240)") != std::string::npos);
    CHECK(r.note.find("boss skipped, not seen killed") == std::string::npos);
}

// Spec C2/F8: a holder exists -> the leg runs; later ticks never re-decide.
static void TestOptionalLegRunsWithCarrier()
{
    DungeonPlan plan;
    plan.name = "zf";
    ai::PlanLeg leg9;
    leg9.leg = 9;
    leg9.name = "gahzrilla";
    plan.legs.push_back(leg9);
    PlanStep move = Step(PlanStepKind::Move, 50, 0, 0);
    move.leg = 9;
    PlanStep rest = Step(PlanStepKind::Rest);
    rest.leg = 9;
    plan.steps = {move, rest};
    ai::PlanLegNeed need;
    need.leg = 9;
    need.item = 9240;
    need.mode = ai::LegNeedMode::Optional;
    need.name = "Mallet of Zul'Farrak";
    plan.needs.push_back(need);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.legNeedRead = true;
    f.legNeedHolders = {{9240, "Tanky"}};
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    CHECK(r.step == 0);
    CHECK(r.legNeedDecided.count(9) == 1);
    f.legNeedHolders = {{9240, ""}};
    f.leadX = 48;
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running);
}

// Spec C2/F9: in combat -> not decided that tick; decided on the first quiet tick.
static void TestOptionalLegWaitsOutCombat()
{
    DungeonPlan plan;
    plan.name = "zf";
    ai::PlanLeg leg9;
    leg9.leg = 9;
    leg9.name = "gahzrilla";
    plan.legs.push_back(leg9);
    PlanStep move = Step(PlanStepKind::Move, 50, 0, 0);
    move.leg = 9;
    PlanStep rest = Step(PlanStepKind::Rest);
    rest.leg = 9;
    plan.steps = {move, rest};
    ai::PlanLegNeed need;
    need.leg = 9;
    need.item = 9240;
    need.mode = ai::LegNeedMode::Optional;
    plan.needs.push_back(need);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.legNeedRead = true;
    f.legNeedHolders = {{9240, ""}};
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(r.legNeedDecided.empty() && r.skippedLegs.empty());
    f.anyInCombat = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.skippedLegs.size() == 1 && r.state == State::Done);
    CHECK(r.note.find("optional leg skipped: gahzrilla (item 9240)") != std::string::npos);
}

// Spec C2/F10: after the skip a walk back from leg 10 never picks a leg-9
// index, and ArmReachedTriggers arms no leg-9 trigger; the leg-9 pull is in notCleared.
static void TestSkippedLegNotReplayed()
{
    DungeonPlan plan;
    plan.name = "zf";
    ai::PlanLeg leg9;
    leg9.leg = 9;
    leg9.name = "gahzrilla";
    plan.legs.push_back(leg9);
    PlanStep before = Step(PlanStepKind::Move, 0, 0, 0);
    before.leg = 9;
    before.entry = 777;
    PlanStep pull = PullStep(10, {7580});
    pull.leg = 9;
    PlanStep gong = GongRow();
    gong.leg = 9;
    PlanStep after = Step(PlanStepKind::Move, 100, 0, 0);
    after.leg = 10;
    PlanStep far = Step(PlanStepKind::Move, 200, 0, 0);
    far.leg = 10;
    plan.steps = {before, pull, gong, after, far};
    ai::PlanLegNeed need;
    need.leg = 9;
    need.item = 9240;
    need.mode = ai::LegNeedMode::Optional;
    plan.needs.push_back(need);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.legNeedRead = true;
    f.legNeedHolders = {{9240, ""}};
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 3 && r.skippedLegs.size() == 1);
    CHECK(r.notCleared.count(1) == 1);
    // The second leg-10 point walks, the run stands past the skip.
    f.leadX = 99;
    Tick(r, f);
    CHECK(r.step == 4 && r.state == State::Running);
    // Replay points from leg 10 never touch leg 9 (the lead stands at a
    // leg-10 point; the skipped leg-9 move is not a replay point).
    CHECK(FirstReplayPoint(r) == 3);
    f.leadX = 100;
    CHECK(ReplayStartPoint(r, f) == 3);
    r.triggerScan = 0;
    r.armedTriggers.clear();
    ArmReachedTriggers(r);
    CHECK(r.armedTriggers.empty()); // leg-9 entry 777 not armed
}

// A door leg: plan with leg 1 = [move, pull], needs = {1, 11000, Door, pick
// 250, object 170560, how ...}; the door stands at x 55, 5 yd past the move's point.
static DungeonPlan DoorPlan()
{
    DungeonPlan plan;
    plan.name = "brd";
    ai::PlanLeg leg;
    leg.leg = 1;
    leg.name = "arena";
    plan.legs.push_back(leg);
    PlanStep move = Step(PlanStepKind::Move, 50, 0, 0);
    move.leg = 1;
    PlanStep pull = PullStep(60, {100});
    pull.leg = 1;
    plan.steps = {move, pull};
    ai::PlanLegNeed door;
    door.leg = 1;
    door.item = 11000;
    door.mode = ai::LegNeedMode::Door;
    door.pick = 250;
    door.object = 170560;
    door.name = "Shadowforge Key";
    door.how = "Dark Iron Legacy quest (Searing Gorge/BRD)";
    plan.needs.push_back(door);
    return plan;
}

// The door found near the lead bot (doorNeedRead) at x 55, closed or open.
static Facts DoorFacts(const std::string& player, bool closed, bool botKeys = false)
{
    Facts f = Party(0);
    f.legNeedRead = true;
    PlayerFact p;
    p.name = player;
    f.players.push_back(p);
    f.doorNeedRead = true;
    f.doorNeedState = closed ? 0 : 1;
    f.doorNeedX = 55.0f;
    f.doorNeedLockpick = 0;
    f.legNeedHolders = {{11000, ""}};
    f.botKeysForPlayers = botKeys;
    return f;
}

// One tick as the glue runs it (TickDungeonRun): a paused door hold reads the door first (DoorOpenWhilePaused), then
// the run's Tick.
static std::string GlueDoorTick(Run& r, const Facts& f, std::uint32_t dt = kTickMs)
{
    if (r.state == State::Paused && r.doorPaused)
        DoorOpenWhilePaused(r, f);
    return Tick(r, f, dt);
}

// Spec C2b/F10b: closed door near, player present -> Paused with the ask note, no
// order; bot-only (no players) -> no hold.
static void TestDoorHoldAsksPlayerToOpen()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts f = DoorFacts("Hypeer", true);
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.doorPaused && r.doorPausedLeg == 1);
    CHECK(r.note.find("door to leg arena: the way is locked (object 170560): open it with Shadowforge Key (item 11000) or Lockpicking 250; the bots wait before it and go on by themselves once it reads open (.bot run resume does not pass a closed door).") != std::string::npos);
    CHECK(r.note.find("Get it: Dark Iron Legacy quest") != std::string::npos);
    Run b = Start(&plan);
    Facts g = Party(0);
    g.legNeedRead = true;
    g.doorNeedRead = true;
    g.doorNeedState = 0;
    g.doorNeedX = 55.0f;
    CHECK(Tick(b, g) == "move 50.0 0.0 0.0");
    CHECK(b.state == State::Running);
}

// Spec C2b/F10c: open door / player carries / lockpick 250 -> no hold, leg
// decided; 249 -> holds.
static void TestDoorHoldOpensAndResumes()
{
    DungeonPlan plan = DoorPlan();
    Run open = Start(&plan);
    CHECK(Tick(open, DoorFacts("Hypeer", false)) == "move 50.0 0.0 0.0");
    CHECK(open.legNeedDecided.count(1) == 1);
    Run carried = Start(&plan);
    Facts cf = DoorFacts("Hypeer", true);
    cf.legNeedHolders = {{11000, "Hypeer"}};
    CHECK(Tick(carried, cf) == "move 50.0 0.0 0.0");
    CHECK(carried.legNeedDecided.count(1) == 1);
    Run rogue = Start(&plan);
    Facts rf = DoorFacts("Roguey", true);
    rf.doorNeedLockpick = 250;
    CHECK(Tick(rogue, rf) == "move 50.0 0.0 0.0");
    Run short1 = Start(&plan);
    Facts sf = DoorFacts("Roguey", true);
    sf.doorNeedLockpick = 249;
    CHECK(Tick(short1, sf).empty());
    CHECK(short1.state == State::Paused && short1.doorPaused);
}

// Review round 2 Codex 2 (lead decision): with a real player and the flag off the hold ends ONLY when the door reads
// open: closed -> held; `.bot run resume` (ResumeChecked, the glue's resume) -> refused, still held; ticks while closed
// -> still held, no order; the door reads open -> the run goes on by itself and walks the leg.
static void TestDoorHoldReleasesOnlyWhenOpen()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts closed = DoorFacts("Hypeer", true);
    CHECK(GlueDoorTick(r, closed).empty());
    CHECK(r.state == State::Paused && r.doorPaused);
    LeadSpot spot;
    spot.read = true;
    spot.alive = true;
    spot.onPlanMap = true;
    std::string refusal;
    CHECK(!ResumeChecked(r, spot, refusal, DoorRead::Closed));
    CHECK(r.state == State::Paused && r.doorPaused);
    CHECK(refusal.find("still hold at the closed door: open it with Shadowforge Key (item 11000) or Lockpicking 250") != std::string::npos);
    CHECK(refusal.find(".bot run resume does not pass a closed door") != std::string::npos);
    for (int i = 0; i < 5; ++i)
    {
        CHECK(GlueDoorTick(r, closed).empty());
        CHECK(r.state == State::Paused && r.doorPaused);
    }
    CHECK(r.legNeedDecided.count(1) == 0);
    Facts open = DoorFacts("Hypeer", false);
    CHECK(DoorOpenWhilePaused(r, open)); // the glue's paused-branch call (tools/test_run_item_need_wiring.py)
    CHECK(r.state == State::Running && !r.doorPaused);
    CHECK(r.legNeedDecided.count(1) == 1);
    CHECK(r.note == "door to leg arena reads open: going on by itself (regrouping)");
    // The run walks the leg (after the resume's regroup), never held at this door again even if it reads closed.
    bool moved = false;
    for (int i = 0; i < 30 && !moved; ++i)
    {
        std::string const order = GlueDoorTick(r, closed);
        CHECK(r.state == State::Running);
        moved = order == "move 50.0 0.0 0.0";
    }
    CHECK(moved);
}

// Review round 2 Opus 3: a door not found near the lead bot is not closed (no hold, nothing decided); the hold fires
// once the closed door is found near the step about to cross it or near the lead bot.
static void TestDoorNotFoundIsNotClosed()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts f = DoorFacts("Hypeer", true);
    f.doorNeedRead = false;
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    CHECK(r.state == State::Running && r.legNeedDecided.count(1) == 0);
    f.doorNeedRead = true;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.doorPaused);
}

// Review round 3 Opus fix 3: a resume while the door is out of the lead bot's
// sight (the door object NOT FOUND) must not say 'closed': the hold stays, but
// the refusal names the true cause.

// il4a: a resume while the door hold waits and the door already reads open goes
// on by itself (the tick's DoorOpenWhilePaused, with its note), not refused.
static void TestDoorHoldResumeWhileOpen()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts closed = DoorFacts("Hypeer", true);
    CHECK(GlueDoorTick(r, closed).empty());
    CHECK(r.state == State::Paused && r.doorPaused);
    LeadSpot spot;
    spot.read = true;
    spot.alive = true;
    spot.onPlanMap = true;
    std::string refusal;
    CHECK(ResumeChecked(r, spot, refusal, DoorRead::Open));
    CHECK(refusal.empty() && r.state == State::Running && !r.doorPaused);
    CHECK(r.note == "door to leg arena reads open: going on by itself (regrouping)");
    CHECK(r.legNeedDecided.count(1) == 1);
}

static void TestDoorHoldResumeOutOfSightNamesIt()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts closed = DoorFacts("Hypeer", true);
    CHECK(GlueDoorTick(r, closed).empty());
    CHECK(r.state == State::Paused && r.doorPaused);
    LeadSpot spot;
    spot.read = true;
    spot.alive = true;
    spot.onPlanMap = true;
    std::string refusal;
    CHECK(!ResumeChecked(r, spot, refusal, DoorRead::Closed));
    CHECK(refusal.find("still hold at the closed door") != std::string::npos);
    CHECK(!ResumeChecked(r, spot, refusal, DoorRead::Unread));
    CHECK(refusal == "the door is out of sight of the lead bot: .bot run skip to go on, or bring the party to the door");
    CHECK(r.state == State::Paused && r.doorPaused);
}

static void TestDoorHoldOnlyNearTheDoor()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts f = DoorFacts("Hypeer", true);
    f.doorNeedX = 500.0f; // found, closed, far from the step's point and the lead bot
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    CHECK(r.state == State::Running && r.legNeedDecided.count(1) == 0);
    f.leadX = 480.0f; // the lead bot beside it
    Tick(r, f);
    CHECK(r.state == State::Paused && r.doorPaused);
}

// Spec C2c/F10d + review round 2 Opus 6: flag on with a player and a bot carrying the key: the carrier bot opens the
// door (the `open` order on the door object), no hold; the door reads open -> the leg walks on.
static void TestBotKeysFlagOpensDoor()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts f = DoorFacts("Hypeer", true, true);
    f.doorNeedCarrier = "Tanky";
    CHECK(Tick(r, f) == "open entry:170560@55.0,0.0,0.0 Tanky");
    CHECK(r.state == State::Running && !r.doorPaused);
    CHECK(r.note.find("Tanky opens it with Shadowforge Key for the party (1 of 3)") != std::string::npos);
    CHECK(Tick(r, f).empty()); // waits for the door
    CHECK(r.note.find("waiting for Tanky to open it") != std::string::npos);
    Facts open = DoorFacts("Hypeer", false, true);
    open.doorNeedCarrier = "Tanky";
    CHECK(Tick(r, open) == "move 50.0 0.0 0.0");
    CHECK(r.legNeedDecided.count(1) == 1);
}

// ... the door never opens: an order every kDoorOpenWaitMs, three in all, then the hold (the player is asked), which
// ends when the door reads open.
static void TestBotKeysFlagDoorThatNeverOpensHolds()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts f = DoorFacts("Hypeer", true, true);
    f.doorNeedCarrier = "Tanky";
    int orders = 0;
    for (int i = 0; i < 100 && r.state == State::Running; ++i)
        if (Tick(r, f, 5000).rfind("open entry:170560", 0) == 0)
            ++orders;
    CHECK(orders == 3);
    CHECK(r.state == State::Paused && r.doorPaused);
    CHECK(r.note.find("Tanky could not open it (object 170560) in 3 tries") != std::string::npos);
    Facts open = DoorFacts("Hypeer", false, true);
    open.doorNeedCarrier = "Tanky";
    GlueDoorTick(r, open);
    CHECK(r.state == State::Running && r.legNeedDecided.count(1) == 1);
}

// Flag on but no bot carries the key (the give found no bag space): the hold asks the player.
static void TestBotKeysFlagWithoutCarrierHolds()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    CHECK(Tick(r, DoorFacts("Hypeer", true, true)).empty());
    CHECK(r.state == State::Paused && r.doorPaused);
}

static void TestDoorHoldFlagOffHolds()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts f = DoorFacts("Hypeer", true, false);
    f.doorNeedCarrier = "Tanky"; // a bot carries it, the flag is off: bots never open doors for players
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("door to leg arena") != std::string::npos);
}

// `.bot run skip` from the door hold walks the party past the closed door by hand: the leg is decided.
static void TestSkipPastClosedDoorDecidesLeg()
{
    DungeonPlan plan = DoorPlan();
    Run r = Start(&plan);
    Facts f = DoorFacts("Hypeer", true);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.doorPaused);
    CHECK(Skip(r));
    CHECK(r.state == State::Running && !r.doorPaused && r.legNeedDecided.count(1) == 1);
}

// Spec C3/F11: f.itemPlayer set -> the use_on pause ends with the trade line.
static void TestUseOnPausesNamingPlayerCarrier()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = false;
    f.itemPlayer = "Hypeer";
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused);
    std::string const tail = "; Hypeer carries it: trade it to Tanky, then .bot run resume";
    CHECK(r.note.size() >= tail.size() && r.note.compare(r.note.size() - tail.size(), tail.size(), tail) == 0);
    // The existing no-carrier text stands without a player carrier (F12).
    Run q = Start(&plan);
    Facts g = Party(1650.9f);
    g.carrierHasItem = false;
    CHECK(Tick(q, g).empty());
    CHECK(q.state == State::Paused && q.note.find("no bot of the party carries item 9240") != std::string::npos);
}

static void TestUseOnWithoutTheItemPauses()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = false;
    CHECK(Tick(r, f).empty() && !r.orderSent);
    CHECK(r.state == State::Paused && r.note.find("no bot of the party carries item 9240") != std::string::npos);
    CHECK(r.note.find(".additem 9240") != std::string::npos);
}

// An answer that says the cast did not happen (in combat, busy, an interrupted cast) is sent again ONCE: a second cast
// that took would run the object's event twice (a second Gahz'rilla dies at once and lies invisible at the pit). Any other ERR pauses at once,
// naming the module's text.
static void TestUseOnResendsOnceThenPauses()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    std::string const order = "use 9240 Tanky entry:141832@1650.9,1171.9,10.9";
    CHECK(Tick(r, f) == order);
    f.taskReply = 2;
    f.taskText = "in-combat: Tanky is in combat.";
    Tick(r, f);
    CHECK(r.state == State::Running && !r.orderSent && r.taskResends == 1);
    f.taskReply = 0;
    f.taskText.clear();
    CHECK(Tick(r, f) == order);
    f.taskReply = 2;
    f.taskText = "in-combat: Tanky is in combat.";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("use_on 141832 with item 9240 failed: in-combat: Tanky is in combat.") !=
        std::string::npos);

    Run h = Start(&plan);
    Facts g = Party(1650.9f);
    g.carrierHasItem = true;
    CHECK(Tick(h, g) == order);
    g.taskReply = 2;
    g.taskText = "invalid-target: spell check 10";
    Tick(h, g);
    CHECK(h.state == State::Paused && h.taskResends == 0 &&
        h.note.find("failed: invalid-target: spell check 10") != std::string::npos);
}

// No answer within the task cap pauses (the resume note warns that a resume rings again).
static void TestUseOnNoAnswerPauses()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    for (std::uint32_t ms = 0; ms <= kTaskCapMs && r.state == State::Running; ms += kTickMs)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("no answer") != std::string::npos);
    CHECK(r.useOnRang.count(0) == 1); // it may have taken: never sent again this run
}

// A use_on right before a hold starts that event, as open and use_item do.
static void TestUseOnStartsAnEvent()
{
    DungeonPlan plan;
    PlanStep hold = Step(PlanStepKind::Hold, 1, 2, 3);
    hold.entries = {7273};
    plan.steps = {GongRow(), hold};
    Run r = Start(&plan);
    CHECK(EventHoldAhead(r) != nullptr && EventStartStep(plan, 1) == 0);
    // A failed event goes back to the use_on.
    Run h = Start(&plan, 1);
    EventFailed(h, "the event did not start");
    CHECK(h.state == State::Paused && h.step == 0 && h.note.find("(use_on)") != std::string::npos);
}

// No ACK and no ERR within the step's timeout (default 60 s): the run pauses.
static void TestGossipNoAnswerPauses()
{
    DungeonPlan plan;
    PlanStep row = GossipRow(10);
    CHECK(row.timeoutS == ai::kPlanDefaultTimeoutS);
    plan.steps = {row, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
    f.carrierBusy = true;
    std::uint32_t ticks = 0;
    while (r.state == State::Running && ticks < 200)
    {
        CHECK(Tick(r, f).empty());
        ++ticks;
    }
    CHECK(r.state == State::Paused && r.step == 0 && ticks >= 59 && ticks <= 61);
    CHECK(r.note.find("no answer in 60 s") != std::string::npos);
    // The step's own timeout counts: 20 s.
    DungeonPlan quick;
    PlanStep fast = GossipRow(10);
    fast.timeoutS = 20;
    quick.steps = {fast};
    Run q = Start(&quick);
    Facts g = Party(0);
    Tick(q, g);
    g.carrierBusy = true;
    ticks = 0;
    while (q.state == State::Running && ticks < 200)
    {
        Tick(q, g);
        ++ticks;
    }
    CHECK(q.state == State::Paused && ticks >= 19 && ticks <= 21 && q.note.find("no answer in 20 s") != std::string::npos);
}

// A lead bot busy with an earlier task holds the order back.
static void TestGossipWaitsForABusyLeadBot()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty() && !r.orderSent);
    CHECK(r.note.find("waiting for Tanky to finish an earlier order") != std::string::npos);
    f.carrierBusy = false;
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
}

// Farther than 30 yd from the point the lead bot walks there first (a plain walk in sight, navmesh hops out of
// sight); the order goes out once it is within reach, and a walk once begun is finished.
static void TestGossipWalksToFarPointFirst()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(45)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 45.0 0.0 0.0");
    // 20 yd out is within the NPC search, but the walk was begun: it goes on to the point.
    f.leadX = 25;
    CHECK(Tick(r, f).empty() && !r.orderSent);
    f.leadX = 44;
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");

    // Out of sight of the point: a navmesh hop first.
    DungeonPlan far;
    far.steps = {GossipRow(200)};
    Run q = Start(&far);
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = true;
    g.approachX = 40;
    CHECK(Tick(q, g) == "move 40.0 0.0 0.0");
    g.leadX = 40;
    CHECK(Tick(q, g).empty());
    g.seen = true;
    CHECK(Tick(q, g) == "move 200.0 0.0 0.0");
    g.leadX = 200;
    CHECK(Tick(q, g) == "gossip 7607 Tanky 1");

    // Within 30 yd there is no walk at all.
    DungeonPlan near;
    near.steps = {GossipRow(29)};
    Run n = Start(&near);
    Facts h = Party(0);
    CHECK(Tick(n, h) == "gossip 7607 Tanky 1");
}

// A gossip right before a hold starts that event (like open / use_item): the run leader treats the walk and the
// step as inside the event, and a failed event goes back to it.
static void TestGossipStartsAnEvent()
{
    DungeonPlan plan;
    plan.map = 209;
    plan.steps = {Step(PlanStepKind::Move, 1, 2, 3), GossipRow(10), Step(PlanStepKind::Move, 1, 2, 3),
                  Step(PlanStepKind::Hold, 1, 2, 3)};
    plan.steps[3].entry = 1;
    plan.steps[3].flags = ai::kPlanFlagDoneData;
    plan.steps[3].entries = {5};
    Run r = Start(&plan, 1);
    CHECK(Current(r)->kind == PlanStepKind::Gossip && EventHoldAhead(r) != nullptr && InEvent(r));
    // A start on the hold or the move leading into it goes back to the gossip.
    std::size_t step = 99;
    bool back = false;
    CHECK(ResolveStartStep(plan, StartFrom::Step, "4", step, &back) == FromResult::Ok && step == 1 && back);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "3", step, &back) == FromResult::Ok && step == 1 && back);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "2", step, &back) == FromResult::Ok && step == 1 && !back);
    CHECK(EventStartStep(plan, 3) == 1);
    // The failed event returns to the gossip step.
    Run h = Start(&plan, 3);
    EventFailed(h, "data 1 did not leave 0");
    CHECK(h.state == State::Paused && h.step == 1);
    CHECK(h.note.find("starts it again at step 2 (gossip)") != std::string::npos);

    // Before a wait_object it is no event start: nothing to return to, no hold ahead.
    DungeonPlan door;
    door.steps = {GossipRow(10), Step(PlanStepKind::WaitObject, 1, 2, 3)};
    door.steps[1].entry = 146084;
    Run d = Start(&door);
    CHECK(EventHoldAhead(d) == nullptr && !InEvent(d));
}

// The ZF shape: hold until the pyramid data reads 8, the gossip, then the door wait.
static void TestGossipAfterAHoldThenTheDoor()
{
    DungeonPlan plan;
    PlanStep hold = Step(PlanStepKind::Hold, 1877.5f, 1199.6f, 8.9f);
    hold.entry = 1;
    hold.param = 8;
    hold.flags = ai::kPlanFlagDoneData;
    hold.entries = {7787};
    hold.timeoutS = 600;
    ai::NormalizePlanStep(hold);
    PlanStep door = Step(PlanStepKind::WaitObject, 1854.5f, 1142.9f, 16.1f);
    door.entry = 146084;
    door.param = 1;
    ai::NormalizePlanStep(door);
    PlanStep talk = GossipRow(1877.5f, 1);
    talk.y = 1199.6f;
    talk.z = 8.9f;
    plan.steps = {hold, talk, door};
    Run r = Start(&plan);
    Facts f = Party(1877.5f);
    f.leadY = 1199.6f;
    f.leadZ = 8.9f;
    f.eventData = 8; // the hold's data already reads 8: it ends at once
    // The hold ends on the first look and the gossip step goes out in the same tick (it is in sight, the lead bot on its spot).
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
    CHECK(r.step == 1 && r.orderSent);
    f.carrierBusy = true;
    f.taskReply = 1;
    Tick(r, f);
    CHECK(r.step == 2 && Current(r)->kind == PlanStepKind::WaitObject);
}

// Zul'Farrak's Sergeant Bly (turtle-agent feat/zf-gahz-bly): his gossip, then a hold done by its units (no done flag) with Bly
// first. Bly and his crew stay friendly for 10 s after the option; a boss row would leave them alone at once, the hold waits:
// the friendly first unit counts (EventUnitCounts), no friendly unit is pulled, the event starts when one turns hostile, and
// the hold ends only when none of the four is left alive within its radius.
static void TestUnitsHoldWaitsForAFriendlyBossToTurn()
{
    PlanStep hold = Step(PlanStepKind::Hold);
    hold.radius = 25.0f;
    hold.timeoutS = 300;
    hold.entries = {7604, 7608, 7606, 7605};
    CHECK(ai::NormalizePlanStep(hold) == ai::PlanStepRefusal::None);
    DungeonPlan plan;
    plan.map = 209;
    plan.steps = {GossipRow(0, 1, 7604), hold, Step(PlanStepKind::Move, 500)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7604 Tanky 1");
    f.carrierBusy = true;
    f.taskReply = 1;
    Tick(r, f);
    CHECK(r.step == 1 && Current(r)->kind == PlanStepKind::Hold);
    f.carrierBusy = false;
    f.taskReply = 0;
    std::vector<UnitFact> crew;
    for (std::uint32_t e : {7604u, 7608u, 7606u, 7605u})
    {
        UnitFact u = Unit(e, e, 11.0f);
        u.hostile = false;
        crew.push_back(u);
    }
    f.units = crew;
    // The 10 s before the script turns them: held, nothing pulled, nothing done.
    for (int i = 0; i < 12; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order.find("attack") == std::string::npos && order.find("pull") == std::string::npos);
    }
    CHECK(r.state == State::Running && r.step == 1 && !r.eventStarted && r.eventTotal == 1);
    // Hostile: the event is on; it is not done while one of them lives.
    for (UnitFact& u : f.units)
        u.hostile = true;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.eventStarted && r.eventTotal == 4);
    f.units = {f.units[0]};
    f.eventDown = 3;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    f.units.clear();
    f.eventDown = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2 && r.note.find("every unit of it is dead") != std::string::npos);

    // The contrast: a boss row on the friendly Bly ends at once and leaves him alone.
    DungeonPlan bossPlan;
    PlanStep boss = Step(PlanStepKind::Boss);
    boss.entry = 7604;
    boss.entries = {7608, 7606, 7605};
    CHECK(ai::NormalizePlanStep(boss) == ai::PlanStepRefusal::None);
    bossPlan.steps = {boss, Step(PlanStepKind::Move, 500)};
    Run b = Start(&bossPlan);
    Facts g = Party(0);
    g.units = crew;
    Tick(b, g);
    CHECK(b.step == 1 && b.note.find("boss is friendly to this party: left alone") != std::string::npos);
}

// ---- r15-19z33 Zul'Farrak: Bly's crew dead before the Bly leg (kPlanFlagDeadNpcIsDone, the units hold with nothing alive) ----

static const char* const kBlyDeadAnswer =
    "no-npc: No living NPC of that entry is within 40 yd of the bot: a dead one lies nearby: Sergeant Bly (7604) "
    "lies dead 3 yd away and respawns in 6 d 23 h (604800 s spawn timer).";
static const char* const kBlyDoneNote =
    "gossip 7604: Sergeant Bly lies dead, nothing to talk to: the step is counted done (the plan marks a dead NPC as done)";

// Bly's gossip row as the migration writes it: flag 512.
static PlanStep BlyGossip(bool flag)
{
    PlanStep s;
    s.kind = PlanStepKind::Gossip;
    s.entry = 7604;
    s.param = 1;
    s.flags = flag ? ai::kPlanFlagDeadNpcIsDone : 0u;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}

// Bly's crew hold: no done flag, radius 25, 300 s, the four entries.
static PlanStep CrewHold()
{
    PlanStep hold = Step(PlanStepKind::Hold);
    hold.radius = 25.0f;
    hold.timeoutS = 300;
    hold.entries = {7604, 7608, 7606, 7605};
    CHECK(ai::NormalizePlanStep(hold) == ai::PlanStepRefusal::None);
    return hold;
}

// Ticks the run `n` times, returns how many ticks it stayed on `step` (stops when it left or stopped running).
static int TicksOnStep(Run& r, const Facts& f, std::size_t step, int n)
{
    int stayed = 0;
    for (int i = 0; i < n && r.state == State::Running && r.step == step; ++i)
    {
        Tick(r, f);
        if (r.state == State::Running && r.step == step)
            ++stayed;
    }
    return stayed;
}

// Step 223 of r15: the order refused with the dead-NPC wording. With the flag the step is done and the next one runs; without it
// the run pauses plainly as before (Weegli 7607 stays a pause: the End Door cannot be opened without him).
static void TestGossipDeadIsDoneWithFlag()
{
    DungeonPlan plan;
    plan.steps = {BlyGossip(true), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string const order = Tick(r, f);
    CHECK(order == "gossip 7604 Tanky 1");
    OrderRefused(r, order, kBlyDeadAnswer);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note == kBlyDoneNote);
    CHECK(!r.orderSent);

    // The later failure (the NPC died on the bot's way, the task's ERR) is the same.
    Run q = Start(&plan);
    CHECK(Tick(q, f) == "gossip 7604 Tanky 1");
    f.taskReply = 2;
    f.taskText = "no-npc: The NPC died or despawned before the bot reached it: Sergeant Bly (7604) lies dead 3 yd away "
                 "and respawns in 6 d 23 h (604800 s spawn timer).";
    Tick(q, f);
    CHECK(q.state == State::Running && q.step == 1);
    CHECK(q.note == kBlyDoneNote);
    f.taskReply = 0;

    // The same refusal without the flag pauses with the plain dead-NPC text.
    DungeonPlan plain;
    plain.steps = {BlyGossip(false), Step(PlanStepKind::Rest)};
    Run p = Start(&plain);
    f = Party(0);
    OrderRefused(p, Tick(p, f), kBlyDeadAnswer);
    CHECK(p.state == State::Paused && p.step == 0);
    CHECK(p.note.find("gossip 7604 option 1 cannot be done") != std::string::npos);
    CHECK(p.note.find("counted done") == std::string::npos);
    Run pt = Start(&plain);
    Tick(pt, f);
    f.taskReply = 2;
    f.taskText = "no-npc: The NPC died or despawned before the bot reached it: Sergeant Bly (7604) lies dead 3 yd away.";
    Tick(pt, f);
    CHECK(pt.state == State::Paused && pt.step == 0 && pt.note.find("cannot be done") != std::string::npos);
}

// The flag counts a DEAD NPC as done, nothing else: every other refusal or failure of the order still pauses.
static void TestGossipDeadIsDoneOnlyForADeadNpc()
{
    DungeonPlan plan;
    plan.steps = {BlyGossip(true), Step(PlanStepKind::Rest)};
    for (const char* answer : {"no-npc: No living NPC of that entry is within 40 yd of the bot.",
             "not-allowed: That NPC is not in AiPlayerbot.BotGossipNpcs, so bots may not talk to it.",
             "busy: the bot is busy", "no-option: No option 1."})
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        OrderRefused(r, Tick(r, f), answer);
        CHECK(r.state == State::Paused && r.step == 0);
        Run t = Start(&plan);
        Tick(t, f);
        f.taskReply = 2;
        f.taskText = answer;
        Tick(t, f);
        CHECK(t.state == State::Paused && t.step == 0);
    }
    // A flagged gossip step's flag is not read on another order's refusal (a move on the way is a plain refusal).
    Run m = Start(&plan);
    Facts f = Party(0);
    Tick(m, f);
    OrderRefused(m, "move 5 5 5", kBlyDeadAnswer);
    CHECK(m.step == 0);
}

// A units hold whose four units lay dead before the hold (r15: "4 of 4 down" for 300 s): it ends once the spot has been in sight for
// kHoldAllDeadMs, and not before.
static void TestUnitsHoldAllDeadOnEntryEndsAfterTenSeconds()
{
    CHECK(kHoldAllDeadMs == 10000);
    DungeonPlan plan;
    plan.steps = {CrewHold(), Step(PlanStepKind::Move, 500)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.eventDown = 4; // the corpses are in sight, nothing alive
    // The first tick is the first look; the clock runs from the next one: 9 s later it still holds, at 10 s it is done.
    int ticks = 1;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 0 && r.eventTotal == 4 && !r.eventStarted);
    while (r.state == State::Running && r.step == 0 && ticks < 40)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(ticks == static_cast<int>(kHoldAllDeadMs / kTickMs) + 1);
    CHECK(r.note.find("event done: none of its units is alive (they died before the hold)") != std::string::npos);
    CHECK(r.note.find("4 of 4 down") != std::string::npos);
}

// The whole r15 step 223-224 replay: the crew is dead, the gossip flagged: no pause at the gossip, the hold ends by itself.
static void TestBlyCrewDeadBeforeTheLegRunsUnattended()
{
    DungeonPlan plan;
    plan.steps = {BlyGossip(true), CrewHold(), Step(PlanStepKind::Move, 500)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.eventDown = 4;
    std::string const order = Tick(r, f);
    CHECK(order == "gossip 7604 Tanky 1");
    OrderRefused(r, order, kBlyDeadAnswer);
    CHECK(r.state == State::Running && r.step == 1 && Current(r)->kind == PlanStepKind::Hold);
    int ticks = 0;
    while (r.state == State::Running && r.step == 1 && ticks < 60)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Running && r.step == 2 && ticks <= 15);
    CHECK(r.note.find("none of its units is alive") != std::string::npos);
}

// What the all-dead end must not do: end a hold early where the units are merely not there yet, or alive, or were alive, or where the
// event already started; and the ceiling stays for every other case.
static void TestUnitsHoldAllDeadDoesNotEndEarly()
{
    DungeonPlan plan;
    plan.steps = {CrewHold(), Step(PlanStepKind::Move, 500)};
    // The spot is not in sight: the hold approaches it, never counts the dead.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.seen = false;
        f.leadX = 80.0f;
        f.approachKnown = true;
        f.approachX = 40.0f;
        f.eventDown = 4;
        CHECK(Tick(r, f).find("move") == 0); // a hop toward the spot
        f.leadX = 40.0f;
        Tick(r, f);
        CHECK(r.state == State::Running && r.step == 0 && r.note.find("none of its units") == std::string::npos);
        CHECK(!r.eventOn && r.eventMs == 0); // the event's clock has not started: no look yet
        // In sight at last: the clock starts then, not before.
        f.seen = true;
        f.leadX = 0.0f;
        Tick(r, f);
        CHECK(r.step == 0 && r.state == State::Running);
        CHECK(TicksOnStep(r, f, 0, 8) == 8);
    }
    // Nothing alive and nothing dead in sight (the units are not spawned or not visible yet): no end, the old 300 s ceiling.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.eventDown = 0;
        CHECK(TicksOnStep(r, f, 0, 120) == 120);
        for (int i = 0; i < 400 && r.state == State::Running; ++i)
            Tick(r, f);
        CHECK(r.state == State::Paused && r.note.find("event not done after 300 s") != std::string::npos);
    }
    // A unit alive in the radius (friendly, it has not turned): the hold waits, up to the ceiling.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact bly = Unit(7604, 7604, 11.0f);
        bly.hostile = false;
        f.units = {bly};
        f.eventDown = 3;
        CHECK(TicksOnStep(r, f, 0, 60) == 60);
        for (int i = 0; i < 400 && r.state == State::Running; ++i)
            Tick(r, f);
        CHECK(r.state == State::Paused && r.note.find("event not done after 300 s") != std::string::npos);
    }
    // A unit was alive in this hold and then died while the event never started (a friendly one killed by something else): not the
    // all-dead end, which asks for none ever counted alive.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact bly = Unit(7604, 7604, 11.0f);
        bly.hostile = false;
        f.units = {bly};
        Tick(r, f);
        Tick(r, f);
        f.units.clear();
        f.eventDown = 4;
        CHECK(TicksOnStep(r, f, 0, 30) == 30);
    }
    // The event started (a unit seen awake): the old rule, every unit dead, ends it at once.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact bly = Unit(7604, 7604, 11.0f);
        f.units = {bly}; // hostile
        Tick(r, f);
        CHECK(r.eventStarted && r.step == 0);
        f.units.clear();
        f.eventDown = 4;
        Tick(r, f);
        CHECK(r.step == 1 && r.note.find("every unit of it is dead") != std::string::npos);
    }
}

// holdUnitSeen belongs to one hold: ResetStep clears it. A leftover true (a step before it, or the same hold before a pause and a resume)
// would keep a later all-dead hold waiting its whole 300 s.
static void TestHoldUnitSeenDoesNotLeakAcrossSteps()
{
    // Two units holds in a row: the first sees a unit alive (and awake), ends when they are dead; the second finds only corpses.
    {
        DungeonPlan plan;
        plan.steps = {CrewHold(), CrewHold(), Step(PlanStepKind::Move, 500)};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.units = {Unit(7604, 7604, 11.0f)};
        Tick(r, f);
        CHECK(r.step == 0 && r.holdUnitSeen);
        f.units.clear();
        f.eventDown = 4;
        Tick(r, f);
        CHECK(r.step == 1 && !r.holdUnitSeen); // the first hold ended ("every unit of it is dead"); nothing leaks into the second
        int ticks = 0;
        while (r.state == State::Running && r.step == 1 && ticks < 40)
        {
            Tick(r, f);
            ++ticks;
        }
        CHECK(r.state == State::Running && r.step == 2 && ticks <= 15);
        CHECK(r.note.find("none of its units is alive") != std::string::npos);
    }
    // Pause and resume of one units hold: the unit seen alive before the pause does not count after the resume.
    {
        DungeonPlan plan;
        plan.steps = {CrewHold(), Step(PlanStepKind::Move, 500)};
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact bly = Unit(7604, 7604, 11.0f);
        bly.hostile = false;
        f.units = {bly};
        Tick(r, f);
        CHECK(r.holdUnitSeen && r.step == 0);
        Pause(r, "test pause");
        CHECK(Resume(r));
        CHECK(!r.holdUnitSeen);
        f.units.clear();
        f.eventDown = 4;
        int ticks = 0;
        while (r.state == State::Running && r.step == 0 && ticks < 80)
        {
            Tick(r, f);
            ++ticks;
        }
        CHECK(r.state == State::Running && r.step == 1);
        CHECK(r.note.find("none of its units is alive") != std::string::npos);
    }
}

// Not for a data, object or encounter hold: they end by their own condition, units or no units.
static void TestAllDeadEndIsForAUnitsHoldOnly()
{
    for (std::uint32_t flags : {ai::kPlanFlagDoneData, ai::kPlanFlagDoneObject,
             ai::kPlanFlagDoneData | ai::kPlanFlagEncounter})
    {
        PlanStep hold = Step(PlanStepKind::Hold);
        hold.radius = 25.0f;
        hold.timeoutS = 300;
        hold.entry = 1;
        hold.param = (flags & ai::kPlanFlagDoneObject) ? 1 : 3;
        hold.flags = flags;
        hold.entries = {7604, 7608};
        CHECK(ai::NormalizePlanStep(hold) == ai::PlanStepRefusal::None);
        DungeonPlan plan;
        plan.steps = {hold, Step(PlanStepKind::Move, 500)};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.eventDown = 2;
        f.eventData = 1; // in progress: not done, not failed
        f.objectState = 0;
        for (int i = 0; i < 30 && r.state == State::Running; ++i)
            Tick(r, f);
        CHECK(r.step == 0);
        CHECK(r.note.find("none of its units") == std::string::npos);
    }
}

static void TestWaitObjectTimeoutIsBounded()
{
    DungeonPlan plan;
    PlanStep wait = Step(PlanStepKind::WaitObject);
    wait.entry = 5;
    wait.timeoutS = 600;
    plan.steps = {wait};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::uint32_t ticks = 0;
    while (r.state == State::Running && ticks < 2000)
    {
        Tick(r, f);
        ++ticks;
    }
    // The wait's own timeout ends it, not the generic step ceiling (300 s).
    CHECK(r.state == State::Paused && ticks >= 600 && ticks <= 602);
    CHECK(r.note.find("not found") != std::string::npos);
}

static void TestUnsupportedPausesAndSkip()
{
    DungeonPlan plan;
    // A kind the run does not know (the loader never lets one through; the
    // machine still pauses on it rather than guess).
    PlanStep unknown;
    unknown.kind = PlanStepKind::None;
    plan.steps = {unknown, Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("not supported yet") != std::string::npos);
    CHECK(!Supported(PlanStepKind::None) && Supported(PlanStepKind::Move));
    CHECK(Supported(PlanStepKind::Ritual) && Supported(PlanStepKind::Hold) && Supported(PlanStepKind::Gossip));
    // Skip from a pause goes on running (after the regroup).
    CHECK(Skip(r));
    CHECK(r.state == State::Running && r.step == 1 && r.regroup);
    CHECK(!Resume(r));
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
}

static void TestCommanderSummonKeepsTheRunGoing()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(30, {1})};
    Run r = Start(&plan);
    CHECK(!PauseForPlayer(r, "cmd summon") && r.state == State::Running);
    CHECK(PauseForPlayer(r, "cmd move") && r.state == State::Paused && PausedByPlayer(r));
    CHECK(r.note == "player order (cmd move): .bot run resume to go on");
    CHECK(Resume(r));
    CHECK(!PauseForPlayer(r, "cmd revive") && r.state == State::Running);
    CHECK(!PausedByPlayer(r));
    CHECK(PauseForPlayer(r, "chat 'summon' to Tanky") && r.state == State::Paused);
    CHECK(!PlayerOrderKeepsRun("cmd revivex") && !PlayerOrderKeepsRun("chat 'revive' to Tanky"));
}

static void TestPlayerOrderAndRefusalPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    CHECK(PauseForPlayer(r, "cmd attack"));
    CHECK(!PauseForPlayer(r, "cmd attack"));
    CHECK(r.state == State::Paused && r.note.find("cmd attack") != std::string::npos);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
    CHECK(Resume(r));
    OrderRefused(r, "rest", "failed: No bot of the group accepted the order.");
    CHECK(r.state == State::Running && r.note.find("rest refused") == 0);
    OrderRefused(r, "move 10.0 0.0 0.0", "no-path: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Paused);
    // The status line keeps the protocol's field count.
    r.note = "a|b";
    std::string line = StatusLine(r);
    int bars = 0;
    for (char c : line)
        bars += c == '|';
    CHECK(bars == 7);
}

// Issue #999: a gather refused (a live ZF run: "cmd move 1420.7 801.1 11.0
// refused unreachable" with the tank on that very spot) is sent once more,
// then the pull goes on without it instead of pausing the run.
static void TestRefusedGatherIsRetriedOnceThenLeftOut()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 30;
    f.units = {Unit(7, 1, 20), Unit(8, 1, 25)};
    std::string const gather = "move 0.0 0.0 0.0";
    CHECK(Tick(r, f) == gather);
    OrderRefused(r, gather, "unreachable: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Running && r.note.find("once more") != std::string::npos);
    CHECK(Tick(r, f) == gather);
    OrderRefused(r, gather, "unreachable: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Running && r.note.find("without it") != std::string::npos);
    CHECK(Tick(r, f) == "pullback 7");
    // The next pull gathers again: the waiver held for one pull only.
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(8, 1, 25)};
    CHECK(Tick(r, f) == gather);
    CHECK(r.state == State::Running);

    // A direct step: refused twice, the direct walk goes out ungathered.
    DungeonPlan direct;
    direct.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run d = Start(&direct);
    Facts g = Party(0);
    g.maxMemberYd = 30;
    CHECK(Tick(d, g) == gather);
    OrderRefused(d, gather, "unreachable");
    CHECK(Tick(d, g) == gather);
    OrderRefused(d, gather, "unreachable");
    CHECK(d.state == State::Running && Tick(d, g) == "move 5.0 0.0 0.0 direct");

    // Other refusals still pause, also a gather's text sent on an earlier tick.
    Run o = Start(&plan);
    OrderRefused(o, gather, "unreachable");
    CHECK(o.state == State::Paused);
}

// Issue #999: a wipe regroups by itself once the fight is over (revive,
// gather), then walks the plan's own move points back from the entrance to
// the step; it counts the wipe; the third at one step pauses as before.
static void TestWipeRegroupsAndWalksBack()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 50), Step(PlanStepKind::Move, 90),
        PullStep(130, {1})};
    Run r = Start(&plan, 3);
    Facts f = Party(130);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1 && r.autoRegroups == 1);
    CHECK(r.note.find("(1 wipe this run)") != std::string::npos);
    CHECK(Tick(r, f) == "revive party");
    for (int i = 0; i < 30; ++i)
        Tick(r, f); // the corpse run: still one wipe
    CHECK(r.wipes == 1 && r.state == State::Running);
    // Alive at the entrance, spread: gather, then the walk back.
    f = Party(0);
    f.maxMemberYd = 40;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    CHECK(Tick(r, f).empty()); // regrouped; the walk back starts with the facts of its point
    // 19z5: straight to the last point before the step (90), in navmesh hops.
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 2 && FactStep(r) == &plan.steps[2] && OwnerFrom(r) == 2);
    f.seen = false; // FactStep's point, 90 yd away
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.note.find("walk back: approaching") == 0);
    f.leadX = 40;
    CHECK(Tick(r, f).empty()); // at the hop
    // A pack that came back by the next hop is pulled before the move.
    f.hopUnits = {Unit(9, 4, 30)};
    CHECK(Tick(r, f) == "pullback 9");
    CHECK(r.walkBack && r.note.find("walk back: pulling") == 0);
    f.hopUnits.clear();
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    // The hops fail (no path from here): the plan's points are replayed from
    // the nearest one (50, from 40).
    f.approachKnown = false;
    Tick(r, f);
    CHECK(r.walkBack && !r.walkBackDirect && r.walkBackIdx == 1 && r.note.find("replaying") != std::string::npos);
    f.seen = true;
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    // A point the navmesh refuses now is passed for the next one.
    OrderRefused(r, "move 50.0 0.0 0.0", "unreachable");
    CHECK(r.state == State::Running);
    CHECK(Tick(r, f) == "move 90.0 0.0 0.0");
    f.leadX = 90;
    CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f).empty() && !r.walkBack && r.note.find("walked back") == 0);
    CHECK(FactStep(r) == &plan.steps[3] && OwnerFrom(r) == 3);
    f.units = {Unit(7, 1, 40)};
    CHECK(Tick(r, f) == "pullback 7");
    CHECK(r.step == 3);

    // Second wipe at the step: regroups once more; the third pauses.
    f = Party(130);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.wipes == 2 && r.autoRegroups == 2);
    f = Party(130); // revived where it stands, near the step: no walk back
    f.units = {Unit(7, 1, 40)};
    CHECK(Tick(r, f) == "pullback 7");
    CHECK(!r.regroup && !r.walkBack);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.wipes == 3 && r.note.find("did not hold") != std::string::npos);
    CHECK(r.note.find("(cmd revive party)") != std::string::npos);
    // A resume gives the step its regroups back; a new step too.
    CHECK(Resume(r) && r.autoRegroups == 0);
}

// Three dead while the other two still fight: the fight goes on (bounded by
// its ceiling), the regroup starts when it ends; no wipe is counted.
static void TestThreeDeadRegroupAfterTheFight()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    f.leadAlive = false;
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && !r.regroup && r.note.find("regroup follows") != std::string::npos);
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.wipes == 0 && r.nearWipes == 1);
    CHECK(r.note.find("(1 near wipe this run)") != std::string::npos);
    CHECK(Tick(r, f) == "revive party");
    // No earlier move point: nothing to walk back, the step goes on.
    f = Party(0);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.walkBack);
    // The done note carries the wipes.
    r.wipes = 2;
    f.leadX = 40;
    Tick(r, f);
    CHECK(r.state == State::Done && r.note == "plan done (2 wipes, 1 near wipe, 1 lead death)");
}

// Issue #999 (third observe run, step 35): the healer stood 82 yd behind and
// did not come; the gather paused the run again and again. At the gather's
// ceiling the farthest bot over 40 yd that came less than 5 yd closer since
// the last sample is summoned to the lead bot, once; one on its way is not.
static void TestGatherSummonsAStalledStraggler()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 90;
    f.members = {Mem("Walky", 90), Mem("Healy", 82), Mem("Nearby", 15), Mem("Stuck", 50)};
    f.units = {Unit(7, 1, 20)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather: first sample
    // By the ceiling Walky came 30 yd closer; Healy and Stuck did not.
    f.members = {Mem("Walky", 60), Mem("Healy", 81), Mem("Nearby", 15), Mem("Stuck", 50)};
    f.maxMemberYd = 81;
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f); // Walky keeps walking (not standing still)
        order = Tick(r, f);
    }
    CHECK(order == "summon Healy"); // the farthest stalled one
    CHECK(r.state == State::Running && r.phase == Phase::Gather && r.note.find("Healy") != std::string::npos);
    // A refused summon does not pause: the gather's ceiling does.
    OrderRefused(r, order, "lead-unavailable");
    CHECK(r.state == State::Running);
    // Next ceiling: Walky still comes (no summon), Stuck stalled since the last sample: summoned. On the way,
    // at 45 s out of the gather, the straggler ladder walks each bot still out to the lead by path, the
    // farthest first, one a tick.
    f.members = {Mem("Walky", 50), Mem("Healy", 81), Mem("Nearby", 15), Mem("Stuck", 49)};
    order.clear();
    std::vector<std::string> walks;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f); // Walky keeps walking (not standing still)
        order = Tick(r, f);
        if (order.compare(0, 5, "walk ") == 0)
        {
            walks.push_back(order);
            order.clear();
        }
    }
    CHECK(order == "summon Stuck");
    CHECK(walks.size() == 3 && walks[0] == "walk Healy 0.0 0.0 0.0" && walks[1] == "walk Walky 0.0 0.0 0.0" &&
        walks[2] == "walk Stuck 0.0 0.0 0.0");
    // Nobody left to summon by the ceiling's own rules (Walky still makes progress): the ladder walks and
    // summons every bot still out (one walk and one summon each), and only then does a ceiling pause.
    f.members = {Mem("Walky", 44), Mem("Healy", 81), Mem("Nearby", 15), Mem("Stuck", 49)};
    std::string orders;
    for (std::uint32_t i = 0; i < 200 && r.state == State::Running; ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f);
        orders += Tick(r, f) + "|";
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
    CHECK(orders.find("walk ") == std::string::npos); // one walk a bot (above)
    // The ladder's summon comes after its walk even for a bot the ceiling summoned before it (r26: summoned at
    // the top, the bot's own formation station walked it down the stair again).
    for (const char* who : {"Walky", "Healy", "Stuck"})
        CHECK(orders.find(std::string("summon ") + who + "|") != std::string::npos);

    // The summoned bot arrives: the pull goes out.
    Run g = Start(&plan);
    Facts h = Party(0);
    h.maxMemberYd = 82;
    h.members = {Mem("Healy", 82)};
    h.units = {Unit(7, 1, 20)};
    Tick(g, h);
    std::string got;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && got.empty(); ++i)
        got = Tick(g, h);
    CHECK(got == "summon Healy");
    h.maxMemberYd = 5;
    h.members.clear();
    CHECK(Tick(g, h) == "pullback 7");
}

// Live (events-5 00:55:59 and again in the regroup 00:57:35, grimlok pull 93):
// a bot stuck 34.9 yd out (a navmesh pocket at the pit) was never summoned --
// the gather (kGatherYd 20) waits for it, the summon's ceiling (kSummonYd 40)
// skips it -- so the run paused "the party did not gather within 30 s (a bot is
// 34.9 yd ...)" until a person summoned it.
static void TestStillBotInsideSummonRangeIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 34.9f;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 34.9f)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Stoutster");
    CHECK(r.state == State::Running && r.phase == Phase::Gather);
    CHECK(r.note.find("Stoutster") != std::string::npos && r.note.find("has not moved") != std::string::npos);
}

// ... and a busy bot is not stuck: one in a fight, or with something on it
// (units attacking it), is left alone, exactly as in a regroup
// (SummonStuckInRegroup) and as the revived bot's summon does -- `cmd summon`
// refuses a bot in combat, so the order would only be refused. The gather's
// ceiling then pauses as before.
static void TestStuckBotInCombatIsNotSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 34.9f;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 34.9f)};
    f.members[1].inCombat = true;
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

static void TestStuckBotWithAttackersIsNotSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 34.9f;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 34.9f)};
    f.members[1].attackers = 1; // a mob on it: it runs, it does not stand (RunBack)
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

// Live r4-19z21 slot 2 step 102 (13:57:42): the gather paused "the party did not
// gather within 30 s (a bot is 33.7 yd from the lead bot)" while that bot was
// coming at about 1 yd/s (54.9 -> 28.7 yd in the trace). A bot that keeps
// coming is waited for, within kGatherComingMaxMs (T5). A walking bot that
// does not come closer (it wobbles about 34 yd out) is summoned at the first
// ceiling, once; if it is still not here by the next, the run pauses.
static void TestMovingBotThatDoesNotGainIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 34.9f;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 34.9f)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    std::uint32_t ticks = 0;
    for (; ticks < 2 * kGatherCapMs / kTickMs + 6 && order.empty(); ++ticks)
    {
        float const yd = (ticks % 2) ? 33.7f : 35.0f; // walking, but not coming
        f.members[1] = Mem("Stoutster", yd);
        f.members[1].x += (ticks % 2 ? 3.0f : -3.0f);
        f.maxMemberYd = yd;
        order = Tick(r, f);
        CHECK(r.state == State::Running);
    }
    CHECK(order == "summon Stoutster");
    CHECK(ticks <= kGatherCapMs / kTickMs + 2); // at the first ceiling, not at 90 s
    CHECK(r.phase == Phase::Gather);
    // Summoned and still not here: no second summon by the ceiling; the straggler ladder walks it to the lead
    // by path at 45 s out of the gather and summons it 45 s after that walk; only then the pause.
    std::vector<std::string> orders;
    std::uint32_t pausedAt = 0;
    for (std::uint32_t i = 0; i < 200 && r.state == State::Running; ++i)
    {
        float const yd = (i % 2) ? 33.7f : 35.0f;
        f.members[1] = Mem("Stoutster", yd);
        f.members[1].x += (i % 2 ? 3.0f : -3.0f);
        f.maxMemberYd = yd;
        std::string const got = Tick(r, f);
        if (!got.empty())
            orders.push_back(got);
        pausedAt = ticks + i + 1;
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
    CHECK(orders.size() == 2 && orders[0] == "walk Stoutster 0.0 0.0 0.0" && orders[1] == "summon Stoutster");
    CHECK(pausedAt >= (kStragglerWalkMs + kStragglerSummonMs + kStragglerSettleMs) / kTickMs);
}

// T5: the trace's straggler, 54.9 yd out and walking in at 0.6 yd/s, is waited
// for past the 30 s ceiling; once within kGatherYd the pull goes out.
static void TestComingStragglerIsWaitedFor()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 54.9f;
    f.members = {Mem("Tanky", 0), Mem("Walky", 54.9f)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    bool sawComing = false;
    bool sawWalk = false;
    std::string order;
    std::uint32_t ticks = 1;
    for (; ticks < 120 && order.empty(); ++ticks)
    {
        float const yd = 54.9f - 0.6f * static_cast<float>(ticks);
        f.members[1] = Mem("Walky", yd);
        f.maxMemberYd = yd;
        order = Tick(r, f);
        CHECK(r.state == State::Running);
        CHECK(order.find("summon") == std::string::npos);
        if (r.note.find("Walky is coming") != std::string::npos)
            sawComing = true;
        if (order.compare(0, 5, "walk ") == 0) // the straggler ladder at 45 s out of the gather
        {
            CHECK(order == "walk Walky 0.0 0.0 0.0" && ticks >= kStragglerWalkMs / kTickMs);
            sawWalk = true;
            order.clear();
        }
    }
    CHECK(sawComing && sawWalk);
    CHECK(order == "pullback 7");
    CHECK(ticks > kGatherCapMs / kTickMs); // it went past the 30 s ceiling
}

// ... also at a direct step's gather and at the walk back's direct gather.
static void TestComingStragglerIsWaitedForAtDirectGathers()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5), PullStep(100, {1})};
    for (int walkBack = 0; walkBack < 2; ++walkBack)
    {
        Run r = Start(&plan, walkBack ? 1 : 0);
        if (walkBack)
        {
            r.walkBack = true;
            r.walkBackIdx = r.walkBackStart = 0;
            r.walkBackDirect = true;
        }
        Facts f = Party(0);
        f.maxMemberYd = 54.9f;
        f.members = {Mem("Tanky", 0), Mem("Walky", 54.9f)};
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
        CHECK(r.phase == Phase::Gather);
        for (std::uint32_t ticks = 1; ticks < kGatherCapMs / kTickMs + 10; ++ticks)
        {
            float const yd = 54.9f - 0.6f * static_cast<float>(ticks);
            f.members[1] = Mem("Walky", yd);
            f.maxMemberYd = yd;
            std::string const order = Tick(r, f);
            CHECK(r.state == State::Running && r.phase == Phase::Gather);
            CHECK(order.find("summon") == std::string::npos);
        }
        CHECK(r.note.find("Walky is coming") != std::string::npos);
        // The same bot standing at 34 yd: no gain, summoned at the next ceiling.
        for (std::uint32_t i = 0; i < 2 * kGatherCapMs / kTickMs; ++i)
        {
            f.members[1] = Mem("Walky", 34.0f);
            f.members[1].x += (i % 2 ? 3.0f : -3.0f);
            f.maxMemberYd = 34.0f;
            std::string const order = Tick(r, f);
            if (order == "summon Walky")
                break;
            CHECK(r.state == State::Running);
        }
        CHECK(r.state == State::Running && r.summoned.count("Walky") == 1);
    }
}

// T5: a bot that comes, but too slowly, is waited for until kGatherComingMaxMs
// and then summoned once (it is far and gaining, which SummonStraggler leaves
// alone); the run pauses only if the summon does not bring it.
static void TestComingStragglerIsSummonedAfterTheComingLimit()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 150;
    f.members = {Mem("Tanky", 0), Mem("Slowpoke", 150)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    std::uint32_t ticks = 1;
    for (; ticks < 200 && order.empty(); ++ticks)
    {
        float const yd = 150.0f - 0.5f * static_cast<float>(ticks);
        f.members[1] = Mem("Slowpoke", yd);
        f.maxMemberYd = yd;
        order = Tick(r, f);
        CHECK(r.state == State::Running);
        if (order.compare(0, 5, "walk ") == 0) // the straggler ladder's walk at 45 s; its summon comes 45 s on
        {
            CHECK(order == "walk Slowpoke 0.0 0.0 0.0");
            order.clear();
        }
    }
    CHECK(order == "summon Slowpoke");
    CHECK(ticks >= kGatherComingMaxMs / kTickMs - 5 && ticks <= kGatherComingMaxMs / kTickMs + 8);
    CHECK(r.phase == Phase::Gather && r.note.find("Slowpoke") != std::string::npos);
    // Once only: it is still far at the next ceiling (the summon was refused or did not bring it).
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        float const yd = 100.0f - 0.5f * static_cast<float>(i);
        f.members[1] = Mem("Slowpoke", yd);
        f.maxMemberYd = yd;
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

// A bot that is not walking in is not waited for even if it is nearer than at
// the last look: one that stands still in a fight (busy, `cmd summon` refuses
// it) pauses the run as before.
static void TestStillBotInAFightIsNotWaitedFor()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 45;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 45)};
    f.members[1].inCombat = true;
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.members[1] = Mem("Stoutster", 38); // nearer by 7 yd, then it stops
    f.members[1].inCombat = true;
    f.maxMemberYd = 38;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

// A bot far from the lead bot (past kSummonYd, or on another floor) that is in
// a fight, or has attackers on it, is never summoned by a gather's ceiling:
// the summon would drag the encounter to the lead bot or leave the bot alone
// in it, and `cmd summon` refuses a bot in combat. Walking or standing, it is
// not waited for as stuck either: the run pauses as before.
static void TestFarBusyBotIsNeverSummonedAtTheCeiling()
{
    for (int variant = 0; variant < 4; ++variant)
    {
        bool const inCombat = variant % 2 == 1; // attackers only, or in combat
        bool const otherFloor = variant >= 2;   // 60 yd out, or 10 yd away but 12 yd up
        DungeonPlan plan;
        plan.steps = {PullStep(100, {1})};
        Run r = Start(&plan);
        Facts f = Party(0);
        float const yd = otherFloor ? 25.0f : 60.0f;
        f.maxMemberYd = yd;
        f.members = {Mem("Tanky", 0), Mem("Busy", yd, otherFloor ? 12.0f : 0.0f)};
        f.units = {Unit(7, 1, 50)};
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
        for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
        {
            f.members[1] = Mem("Busy", yd, otherFloor ? 12.0f : 0.0f);
            if (i % 2 == 0)
                f.members[1].x += 3.0f; // some ticks walking, then standing still for 6 s
            f.members[1].inCombat = inCombat;
            f.members[1].attackers = inCombat ? 0 : 2;
            CHECK(Tick(r, f).find("summon") == std::string::npos);
        }
        CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
        CHECK(r.summoned.empty());
    }
    // The same bot, free of the fight, is summoned (the exclusion is the fight).
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 60;
    f.members = {Mem("Tanky", 0), Mem("Busy", 60)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Busy");
}

// Live (events-5 01:48:43 -> 01:50:42): after the wipe the regroup's gather
// waited its whole ceiling (60 s, a bot 363 yd out) for a bot that had not moved
// at all; it is summoned after kStuckSummonMs instead.
static void TestRegroupSummonsAStuckFarBotEarly()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 120;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 120)};
    Pause(r, "test");
    CHECK(Resume(r) && r.regroup);
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the regroup's gather
    std::string order;
    std::uint32_t ticks = 1;
    for (; ticks < 30 && order.empty(); ++ticks)
        order = Tick(r, f);
    CHECK(order == "summon Stoutster");
    CHECK(ticks >= kStuckSummonMs / kTickMs && ticks <= kStuckSummonMs / kTickMs + 2);
    CHECK(r.state == State::Running && r.regroup);
    CHECK(r.note.find("has not moved for 15 s") != std::string::npos && r.note.find("summoned") != std::string::npos);
}

// The 15 s are the gather's own clock. The stand-still watch runs from before
// the gather (it keeps counting while the dead are revived), so without this a
// bot that stood still 120 yd away during the revives was summoned one tick
// after the gather went out -- before anyone could have seen it walk.
static void TestStuckSummonCountsFromTheGather()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.total = 5;
    f.alive = 4;
    f.dead = 1;
    f.deadNames = {"Quelinator"};
    f.maxMemberYd = 120;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 120)};
    // The corpse run: 20 ticks of revive before the dead are up. The stand-still
    // watch of Stoutster runs all through them.
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    CHECK(r.still["Stoutster"].ms >= kStuckSummonMs);
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the regroup's gather: its clock starts
    CHECK(Tick(r, f).empty());               // NOT summoned yet
    std::string order;
    std::uint32_t ticks = 0;
    for (; ticks < 30 && order.empty(); ++ticks)
        order = Tick(r, f);
    CHECK(order == "summon Stoutster");
    CHECK(ticks >= kStuckSummonMs / kTickMs - 1 && ticks <= kStuckSummonMs / kTickMs + 1);
    CHECK(r.note.find("has not moved for 15 s") != std::string::npos);
}

// Codex on #1000: a walk-back point refused for the lead bot's state of the
// moment is sent again (3 times, then pause); only a path refusal passes it.
static void TestWalkBackRefusalKinds()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 50), PullStep(130, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    r.walkBackIdx = 0;
    r.gatherWaived = true;
    r.gatherRefusals = 2;
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
    for (std::uint32_t i = 1; i <= kWalkBackRetries; ++i)
    {
        OrderRefused(r, "move 10.0 0.0 0.0", "lead-unavailable: The lead bot is dead, on a taxi or teleporting.");
        CHECK(r.state == State::Running && r.walkBackIdx == 0 && r.note.find("again") != std::string::npos);
        CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
    }
    OrderRefused(r, "move 10.0 0.0 0.0", "busy: a task runs");
    CHECK(r.state == State::Paused && r.walkBackIdx == 0);

    Run p = Start(&plan, 2);
    p.walkBack = true;
    p.gatherWaived = true;
    p.gatherRefusals = 2;
    CHECK(Tick(p, f) == "move 10.0 0.0 0.0");
    OrderRefused(p, "move 10.0 0.0 0.0", "lead-unavailable: teleporting");
    OrderRefused(p, "move 10.0 0.0 0.0", "off-mesh: The point is not on walkable ground.");
    // Passed: the next point, its own refusal count and gather waiver fresh.
    CHECK(p.state == State::Running && p.walkBackIdx == 1 && p.walkBackRefusals == 0);
    CHECK(!p.gatherWaived && p.gatherRefusals == 0);
    CHECK(Tick(p, f) == "move 50.0 0.0 0.0");
}

// Cold review on #1000: a pullback drags the boss to the party, so his
// corpse 30 yd from his spot is normal (radius + kSeeYd); and a boss seen
// alive in this step and now dead near the lead bot is down wherever he fell.
static void TestBossKilledAwayFromHisSpot()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 6910;             // Uldaman: Revelosh, his adds first
    boss.entries = {7076, 7077};
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(5, 6910, 30)};
    CHECK(Tick(r, f) == "pullback 5");
    CHECK(r.bossSeenAlive);
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units.clear();
    f.bossDeadNearSpot = true; // the glue: his corpse 30 yd from the spot, within radius + kSeeYd
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "boss down");

    // Fell far from his spot but near the lead bot: down, because he was seen alive.
    Run n = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(5, 6910, 30)};
    CHECK(Tick(n, g) == "pullback 5");
    g.anyInCombat = true;
    Tick(n, g);
    g.anyInCombat = false;
    g.units.clear();
    g.bossDeadNearLead = true;
    Tick(n, g);
    CHECK(n.step == 1 && n.note == "boss down");
    // Never seen alive this step: a corpse near the lead bot alone is not enough.
    Run m = Start(&plan);
    Facts h = Party(0);
    h.bossDeadNearLead = true;
    Tick(m, h);
    CHECK(m.step == 0 && m.note.find("not at his spot") != std::string::npos);
}

// Cold review on #1000: after a wipe the party stands at the entrance and the
// nearest walk-back point is more than ReactDistance away: it is approached in
// navmesh hops (as a resume does), not sent as one refused move; with no path
// toward it at all it is passed. Walk-back pulls have their own count.
static void TestWalkBackFarPointIsApproached()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 230), PullStep(400, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    Facts f = Party(0);
    f.seen = false; // FactStep: the walk-back point, 200 yd away
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.note.find("walk back: approaching") == 0);
    f.leadX = 40;
    CHECK(Tick(r, f).empty()); // at the hop: the next one next tick
    f.approachX = 80;
    CHECK(Tick(r, f) == "move 80.0 0.0 0.0");
    f.leadX = 80;
    Tick(r, f);
    f.seen = true;
    CHECK(Tick(r, f) == "move 200.0 0.0 0.0");
    // A hostile on the way: pulled, counted apart from the step's pulls.
    f.hopUnits = {Unit(9, 3, 20)};
    CHECK(Tick(r, f) == "pullback 9");
    CHECK(r.pulls == 0 && r.walkBackPulls == 1);

    // No path toward a far point: passed for the next one.
    Run p = Start(&plan, 2);
    p.walkBack = true;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = false;
    CHECK(Tick(p, g).empty());
    CHECK(p.state == State::Running && p.walkBackIdx == 1 && p.note.find("no walkable path") != std::string::npos);
}

// Codex on #1000 / 19z5 step 57: only an invalid-target refusal of a pullback
// leaves the unit out (for the run, no pause); a busy tank or an unavailable
// lead bot pauses as before.
static void TestRefusedPullOnlyForInvalidTarget()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    OrderRefused(r, "pullback 7", "busy: the tank is pulling");
    CHECK(r.state == State::Paused && !r.unreachable.count(7));
    Run l = Start(&plan);
    OrderRefused(l, "pullback 7", "lead-unavailable: The lead bot is dead");
    CHECK(l.state == State::Paused && !l.unreachable.count(7));
    Run i = Start(&plan);
    OrderRefused(i, "pullback 7", "invalid-target: The group cannot attack that target.");
    CHECK(i.state == State::Running && i.unreachable.count(7) && i.note.find("left out") != std::string::npos);
}

// Codex on #1001: the dead lead bot already released to the graveyard map;
// the glue lets the tick run (its map guard pauses only a living lead bot off
// the map), and the wipe starts the regroup.
static void TestWipeWithTheLeadOnTheGraveyardMap()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(130, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(-9000); // the lead's ghost far away on another map
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    f.maxMemberYd = 0;
    f.seen = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1);
    CHECK(Tick(r, f) == "revive party");
}

// Codex on #1000: a near wipe that becomes a wipe, then the revive one by one
// (4 dead, 3 dead with someone up again), is one near wipe and one wipe.
static void TestNearWipeLatchHoldsThroughTheWipe()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.nearWipes == 1 && r.wipes == 0);
    f.anyInCombat = false;
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.nearWipes == 1 && r.wipes == 1);
    for (std::uint32_t dead : {4u, 3u})
    {
        f.dead = dead;
        f.alive = 5 - dead;
        Tick(r, f);
        CHECK(r.nearWipes == 1 && r.wipes == 1);
    }
    // All up again: a later near wipe counts anew.
    f = Party(0);
    f.units = {Unit(7, 1, 20)};
    Tick(r, f);
    CHECK(r.state == State::Running);
    f.anyInCombat = true;
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.nearWipes == 2 && r.wipes == 1);
}

// 19z5 steps 27 and 81: the lead bot died first; the run paused "the lead bot
// is dead" and the rest died while it waited. Now the others fight on; when
// they fall too it is a wipe and the run regroups by itself. A dead lead bot
// alone after the fight still pauses.
static void TestLeadDiesFirstThenTheWipeRegroups()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(130, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(130);
    f.units = {Unit(7, 1, 10)};
    f.anyInCombat = true;
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.note.find("the others fight on") != std::string::npos);
    f.dead = 5;
    f.alive = 0;
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1);

    Run p = Start(&plan, 1);
    Facts g = Party(130);
    g.units = {Unit(7, 1, 10)};
    g.leadAlive = false;
    g.dead = 1;
    g.alive = 4;
    Tick(p, g);
    CHECK(p.state == State::Running && p.regroup && p.note.find("the lead bot is dead") != std::string::npos);
}

// 19z5 step 80 (15:43): 4 dead and the tank alone past the fight ceiling
// paused as "a fight lasted more than 180 s". It is a wipe: the run regroups
// by itself, within the step's budget.
static void TestFightCeilingWithFourDeadRegroups()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    f.dead = 4;
    f.alive = 1;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && !r.regroup; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.autoRegroups == 1 && r.note.find("went past") != std::string::npos);
    // Over budget: the ceiling pauses as before.
    Run p = Start(&plan);
    p.autoRegroups = kAutoRegroupsPerStep;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && p.state == State::Running; ++i)
        Tick(p, f);
    CHECK(p.state == State::Paused);
}

// Review round 1 (lead decision): the regroup after deaths past the fight ceiling is also a self-regroup after
// deaths: it re-arms the rescue latch like the wipe block, so a second such regroup on the same step gets the
// rescue again; a hand resume still does not re-arm. Deleting the new call fails this test.
static void TestPastCeilingRegroupRearmsTheRescue()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    f.dead = 4;
    f.alive = 1;
    r.strandStep = 0; // the first regroup's rescue already used the latch on this step
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && !r.regroup; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.autoRegroups == 1 && r.note.find("went past") != std::string::npos);
    CHECK(r.strandStep == static_cast<std::size_t>(-1)); // re-armed by the regroup
    // A hand resume after it still does not re-arm.
    r.strandStep = 0;
    r.regroup = true;
    r.state = State::Paused;
    CHECK(Resume(r));
    CHECK(r.strandStep == 0);
}

// 19z5 step 83: Draezur 29 yd away but on another floor (z 21 against 43);
// the gather paused twice. A bot more than 8 yd up or down that comes no
// closer is a straggler too.
static void TestStragglerOnAnotherFloorIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 29.1f;
    MemberFact healer{"Draezur", 29.1f};
    healer.dz = 22;
    MemberFact near{"Nearby", 25};
    near.dz = 2;
    f.members = {near, healer};
    f.units = {Unit(7, 1, 20)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Draezur");
}

// 19z5 step 27 and 81: after a revive by hand and `.bot run resume` the party
// stood at the entrance and the step's approach paused (no path / 8 hops).
// A resume walks back too: straight to the last plan point, in hops.
static void TestResumeFromTheEntranceWalksBack()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 300), PullStep(330, {1})};
    Run r = Start(&plan, 2);
    Pause(r, "the lead bot is dead: revive it, then .bot run resume");
    CHECK(Resume(r));
    Facts f = Party(0);
    CHECK(Tick(r, f).empty()); // regrouped
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 1);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
}

// 19z5 steps 83-84: the approach hop left the party at about 1622 1051 z 18;
// the tank chased over a ridge to 1625 1075 z 44 in the fight, and the next
// gather (on the lead's own spot) pinned the party on the hilltop, where no
// path led down. A gather now goes back to the run's anchor (the last point
// the run walked to) when the lead bot is off it, and gathers there.
static void TestGatherGoesBackToTheAnchorAfterARidgeChase()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1639.4f, {5649, 5650})};
    plan.steps[0].y = 1106.8f;
    plan.steps[0].z = 9.0f;
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadX = 1584; f.leadY = 1082; f.leadZ = 10;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 1622; f.approachY = 1051; f.approachZ = 18;
    CHECK(Tick(r, f) == "move 1622.0 1051.0 18.0");
    f.leadX = 1622; f.leadY = 1051; f.leadZ = 18;
    Tick(r, f); // at the hop: the anchor
    CHECK(r.anchorKnown && r.anchorZ == 18.0f);
    // In sight; the fight drags the tank up the ridge.
    f.seen = true;
    f.units = {Unit(1, 5649, 25)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.leadX = 1625; f.leadY = 1075; f.leadZ = 44;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(2, 5650, 30)};
    f.maxMemberYd = 29.1f;
    // The gather walks the lead bot back down to the anchor, not up the hill.
    CHECK(Tick(r, f) == "move 1622.0 1051.0 18.0");
    CHECK(r.phase == Phase::Gather && r.note.find("gathering back at 1622.0 1051.0 18.0") == 0);
    // Even with the party close, no pull while the lead bot is still up there.
    f.maxMemberYd = 5;
    CHECK(Tick(r, f).empty());
    // Back at the anchor and gathered: the pull goes out.
    f.leadX = 1622; f.leadY = 1052; f.leadZ = 18;
    CHECK(Tick(r, f) == "pullback 2");

    // Same floor but no navmesh path back (a pocket): back to the anchor too.
    Run p = Start(&plan);
    p.anchorKnown = true;
    p.anchorX = 1622; p.anchorY = 1051; p.anchorZ = 18;
    Facts g = Party(0);
    g.leadX = 1627; g.leadY = 1078; g.leadZ = 22;
    g.units = {Unit(2, 5650, 30)};
    // The path back is read lazily: asked for on the first tick, no pull yet.
    CHECK(Tick(p, g).empty() && p.anchorPathWanted && p.pulls == 0 && p.fruitlessPulls == 0);
    g.anchorPathRead = true;
    g.anchorReachable = false;
    g.units = {Unit(2, 5650, 30)};
    CHECK(Tick(p, g) == "move 1622.0 1051.0 18.0");
    // On the anchor's floor with a path back: the gather stays on the lead's spot.
    Run q = Start(&plan);
    q.anchorKnown = true;
    q.anchorX = 1622; q.anchorY = 1051; q.anchorZ = 18;
    Facts h = Party(0);
    h.leadX = 1630; h.leadY = 1060; h.leadZ = 20;
    h.anchorPathRead = true;
    h.maxMemberYd = 30;
    h.units = {Unit(2, 5650, 30)};
    CHECK(Tick(q, h) == "move 1630.0 1060.0 20.0");
}

// Codex on #1000: the shortest navmesh path back ignores packs. A direct walk
// back whose path passes within a not-yet-cleared pull group's radius + 10 yd
// drops the shortcut and replays the plan's points; no move enters the pack.
static void TestDirectWalkBackAvoidsUnclearedPacks()
{
    DungeonPlan plan;
    PlanStep later = PullStep(60, {9});
    later.y = 20;   // a later pack, 20 yd off the straight line
    ai::NormalizePlanStep(later);
    plan.steps = {Step(PlanStepKind::Move, 30), Step(PlanStepKind::Move, 120), PullStep(150, {1}), later};
    // Geometry: the straight path from 0 to 120 passes 20 yd from the later
    // pack (radius 15 + 10); a path that bends away does not.
    std::vector<PathPoint> straight = {{0, 0, 0}, {120, 0, 0}};
    std::vector<PathPoint> around = {{0, 0, 0}, {60, -30, 0}, {120, 0, 0}};
    CHECK(PathCrossesUnclearedPack(plan, 2, straight, 0));
    CHECK(!PathCrossesUnclearedPack(plan, 2, around, 0));
    CHECK(!PathCrossesUnclearedPack(plan, 4, straight, 0)); // that pack is cleared (an earlier step)

    Run r = Start(&plan, 2);
    Facts f = Party(0);
    r.walkBack = true;
    r.walkBackDirect = true;
    r.walkBackIdx = 1;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    f.approachY = 0;
    f.directPathCrossesPack = true;
    std::string order = Tick(r, f);
    CHECK(order.empty() && !r.walkBackDirect && r.walkBack && r.walkBackIdx == 0);
    CHECK(r.note.find("passes an uncleared pack") != std::string::npos);
    // The replay walks the plan's first point (30 0 0), not the hop toward the pack.
    f.seen = true;
    f.directPathCrossesPack = false;
    CHECK(Tick(r, f) == "move 30.0 0.0 0.0");
}

// Cold review on #1000: a stalled arrival short of the point (off the mesh)
// anchors where the lead bot stands, not at the unreachable point; skipped
// packs count in the walk back's pack check, the path's last leg too.
static void TestStalledAnchorAndSkippedPacks()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.leadX = 35; // 5 yd short, stuck
    for (std::uint32_t i = 0; i <= kWalkRetryMs / kTickMs + 1 && r.step == 0; ++i)
        Tick(r, f);
    CHECK(r.step == 1 && r.anchorKnown && r.anchorX == 35.0f);

    DungeonPlan p;
    PlanStep skipped = PullStep(60, {9});
    skipped.y = 20;
    ai::NormalizePlanStep(skipped);
    PlanStep atEnd = PullStep(120, {8});
    ai::NormalizePlanStep(atEnd);
    p.steps = {skipped, Step(PlanStepKind::Move, 120), atEnd, Step(PlanStepKind::Move, 200)};
    std::vector<PathPoint> straight = {{0, 0, 0}, {119, 0, 0}};
    std::unordered_set<std::size_t> notCleared;
    CHECK(!PathCrossesUnclearedPack(p, 3, straight, 0, notCleared)); // both groups before step 3, cleared
    notCleared.insert(0);
    CHECK(PathCrossesUnclearedPack(p, 3, straight, 0, notCleared)); // the skipped one counts
    CHECK(PathCrossesUnclearedPack(p, 2, straight, 0));             // a group by the path's end counts too
}

// Issue #999 item 5 (Zul'Farrak): the Sandfury Witch Doctor 5650 heals its
// pack and drops a Greater Healing Ward 8179. Before the pull the mage
// polymorphs the best-ranked cc unit (never the pull target); the pull goes
// on a kill_first unit; in the fight the kill_first units are focused, the
// ward first as soon as it is up.
static PlanStep ZfPack()
{
    return PullStep(10, {5650, 5648, 7247});
}

static DungeonPlan ZfPlan()
{
    DungeonPlan plan;
    plan.steps = {ZfPack()};
    plan.ccPriority = {5650, 7247, 7246, 5648};
    plan.ccAlways = {5650, 7246};
    plan.killFirst = {8179, 5650, 7247};
    return plan;
}

static UnitFact Engaged(UnitFact u)
{
    u.engaged = true;
    return u;
}

// A Greater Healing Ward 8179 of `owner`; ownerEngaged: its owner fights the party.
static UnitFact Ward(std::uint64_t guid, std::uint64_t owner, bool ownerEngaged, float dist = 18)
{
    UnitFact u = Unit(guid, 8179, dist);
    u.owner = owner;
    u.ownerEngaged = ownerEngaged;
    return u;
}

// The gather left out for a pull after a hop was abandoned for a close unit
// holds for that pull only: combat that starts during the pre-pull crowd
// control ends it, and the next pull gathers (and goes back to the anchor).
static void TestHopWaiverEndsWhenCombatStartsDuringCrowdControl()
{
    DungeonPlan plan = ZfPlan();
    plan.killFirst = {8179, 7247}; // #1159: a kill_first unit is never held, so the witch doctor is cc only
    Run r = Start(&plan);
    r.restDue = false;
    r.anchorKnown = true; // the stand before, 20 yd below the lead bot
    r.anchorX = 0; r.anchorY = 0; r.anchorZ = -20;
    EnterPhase(r, Phase::Walk); // an approach hop the pack came in sight during
    r.hopWalk = true;
    r.hopX = r.walkX = 5; r.hopY = r.walkY = 0; r.hopZ = r.walkZ = 0;
    Facts f = Party(0);
    f.members = {MemberFact{"Magey", 5, true, true}};
    f.maxMemberYd = 40; // the followers are behind
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20), Unit(3, 7247, 25)};
    // The hop is over, no gather: the crowd control goes first.
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(r.gatherWaived && !r.hopWaiveUnits.empty());
    // Combat starts before the pullback.
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(!r.gatherWaived && r.hopWaiveUnits.empty());
    // The fight is over; the next pull gathers first, back at the anchor.
    f.anyInCombat = false;
    std::string order;
    for (int i = 0; i < 40 && order.empty() && r.state == State::Running; ++i)
        order = Tick(r, f);
    CHECK(order.compare(0, 5, "move ") == 0);
    CHECK(r.note.find("gathering") == 0);
}

// Uldaman's cc lists (20261021174717_world.sql): cc_priority 4853,4848,7321,7030,7012,7397. Grimlok's boss row
// (4854, adds 4855,4853,4863) and Galgann's (7291, add 7030) name listed units as the adds their kill order takes
// first: the run never crowd-controls the boss or one of those adds; another listed unit in the fight still can
// be, and a pull step (the cascades) keeps its crowd control.
static void TestBossStepNeverCrowdControlsItsAdds()
{
    auto boss = [](std::uint32_t entry, std::vector<std::uint32_t> adds) {
        PlanStep s;
        s.kind = PlanStepKind::Boss;
        s.entry = entry;
        s.entries = adds;
        ai::NormalizePlanStep(s);
        return s;
    };
    DungeonPlan plan;
    plan.ccPriority = {4853, 4848, 7321, 7030, 7012, 7397};
    plan.ccAlways = {4848, 7321};
    plan.steps = {boss(4854, {4855, 4853, 4863}), boss(7291, {7030}), PullStep(10, {4853, 4855})};
    Facts f = Party(0);
    f.members = {MemberFact{"Magey", 5, true, true}};
    // Grimlok: the Geomancer 4853 is ranked first, but it is his add.
    f.units = {Unit(1, 4854, 12), Unit(2, 4853, 15), Unit(3, 4855, 10), Unit(4, 4863, 14)};
    Run g = Start(&plan, 0);
    CHECK(PickCc(g, f, 0) == nullptr);
    CHECK(PickCc(g, f, 3) == nullptr);
    CHECK(BossStepUnit(Current(g), 4853) && BossStepUnit(Current(g), 4854) && !BossStepUnit(Current(g), 7321));
    // A listed unit that is not his (a wanderer that joined) is still taken.
    f.units.push_back(Unit(5, 7321, 20));
    const UnitFact* cc = PickCc(g, f, 0);
    CHECK(cc && cc->guid == 5);
    // Galgann: his 7030 is never sheeped.
    Run a = Start(&plan, 1);
    f.units = {Unit(1, 7291, 12), Unit(2, 7030, 15)};
    CHECK(PickCc(a, f, 0) == nullptr);
    // A pull step with the same units keeps its crowd control.
    Run p = Start(&plan, 2);
    f.units = {Unit(1, 4855, 10), Unit(2, 4853, 15)};
    cc = PickCc(p, f, 1);
    CHECK(cc && cc->guid == 2);
    CHECK(!BossStepUnit(Current(p), 4853) && !BossStepUnit(nullptr, 4853));
}

static void TestPullCrowdControlsTheHealer()
{
    DungeonPlan plan = ZfPlan();
    plan.killFirst = {8179, 7247}; // #1159: a kill_first unit is never held, so the witch doctor is cc only
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {MemberFact{"Magey", 5, true, true}};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20), Unit(3, 7247, 25)};
    // The witch doctor is the best cc rank: it is sheeped, not pulled.
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(r.note.find("crowd control on 5650") == 0 && r.state == State::Running);
    // The pull goes on the soul eater (kill_first) rather than the nearest.
    CHECK(Tick(r, f) == "pullback 3");
    CHECK(r.pulls == 1 && r.fruitlessPulls == 0);
    // In the fight: the soul eater is focused; the sheeped witch doctor is not.
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(2, 5650, 20)), Engaged(Unit(3, 7247, 5))};
    CHECK(Tick(r, f) == "focus 3");
    CHECK(Tick(r, f).empty()); // only on a change
    // A ward comes up: it is focused first.
    f.killFirst.push_back(Ward(9, 3, true, 12)); // a pull step: within kKillFirstMaxYd
    CHECK(Tick(r, f) == "focus 9");
    // The ward dies: back on the soul eater.
    f.killFirst.pop_back();
    CHECK(Tick(r, f) == "focus 3");
    // After the fight: the sheeped witch doctor is left while another can be
    // pulled, and no second cc goes out while it lives.
    f.anyInCombat = false;
    f.killFirst.clear();
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "pullback 2"); // the last one, sheeped or not
    // A refused cc or focus does not pause.
    OrderRefused(r, "cc moon 2 Magey", "no-bot");
    OrderRefused(r, "focus 3", "invalid-target");
    CHECK(r.state == State::Running);
}

static void TestNoCrowdControlWithoutAMageOrPlanList()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    // No mage: no cc; the pull still prefers kill_first.
    CHECK(Tick(r, f) == "pullback 2");

    DungeonPlan bare;
    bare.steps = {ZfPack()};
    Run b = Start(&bare);
    Facts g = Party(0);
    g.members = {MemberFact{"Magey", 5, true, true}};
    g.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(b, g) == "pullback 1"); // the nearest, as before
    g.anyInCombat = true;
    g.killFirst = {Engaged(Unit(2, 5650, 20))};
    CHECK(Tick(b, g).empty());

    // A lone cc unit is pulled, not sheeped.
    Run c = Start(&plan);
    Facts h = Party(0);
    h.members = {MemberFact{"Magey", 5, true, true}};
    h.units = {Unit(2, 5650, 20)};
    CHECK(Tick(c, h) == "pullback 2");
}

// Codex on #1001: the Polymorph is pending until its aura shows on that unit
// in the fight (the mage's cc strategy is a combat one): not within
// kCcLandMs, the mark once more; still not, noted, and the fight goes on.
static void TestCrowdControlMustLand()
{
    DungeonPlan plan = ZfPlan();
    plan.killFirst.clear();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {MemberFact{"Tanky", 0, false}, MemberFact{"Magey", 5, true, true}};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(r.ccPending == 2);
    CHECK(Tick(r, f) == "pullback 1"); // the mark is out; the pull starts the fight
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty() && r.ccPending == 2);
    f.units[1].polymorphed = true; // landed
    CHECK(Tick(r, f).empty());
    CHECK(r.ccPending == 0 && r.note.find("polymorphed") != std::string::npos);

    // Never lands: once more after kCcLandMs, then noted.
    Run n = Start(&plan);
    Facts g = Party(0);
    g.members = {MemberFact{"Magey", 5, true, true}};
    g.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(n, g) == "cc moon 2 Magey");
    CHECK(Tick(n, g) == "pullback 1");
    g.anyInCombat = true;
    std::string order;
    for (std::uint32_t i = 0; i <= kCcLandMs / kTickMs && order.empty(); ++i)
        order = Tick(n, g);
    CHECK(order == "cc moon 2 Magey" && n.ccResent && n.note.find("once more") != std::string::npos);
    order.clear();
    for (std::uint32_t i = 0; i <= kCcLandMs / kTickMs && n.ccPending; ++i)
        order += Tick(n, g);
    CHECK(order.empty() && n.ccPending == 0 && n.note.find("cc did not land on 5650") != std::string::npos);
    CHECK(n.state == State::Running);

    // A mage that does not know Polymorph is not the crowd control.
    Run m = Start(&plan);
    Facts h = Party(0);
    h.members = {MemberFact{"Lowmage", 5, false}};
    h.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(m, h) == "pullback 1" && m.ccSent.empty());
}

// Codex on #1001: kill_first is this fight's only. A ward whose owner fights
// the party is focused; a ward of an unrelated witch doctor nearby is not,
// nor a kill_first unit that is not engaged.
static void TestKillFirstIsThisFightsOnly()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Ward(20, 30, false), Unit(30, 5650, 25)}; // unrelated ward and its idle owner
    CHECK(Tick(r, f).empty());
    f.killFirst.push_back(Ward(21, 1, true, 12)); // the ward of a unit in this fight (a pull step: within kKillFirstMaxYd)
    CHECK(Tick(r, f) == "focus 21");

    // A kill_first unit whose aura is on no unit of this fight (a channeller
    // of another circle elsewhere): auraOnEngaged false, not engaged, never
    // focused, here or later.
    Run c = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(1, 5648, 10)};
    CHECK(Tick(c, g) == "pullback 1");
    g.anyInCombat = true;
    g.killFirst = {Unit(40, 7247, 8)}; // within kKillFirstMaxYd: only the engaged rule keeps it out
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(c, g).empty() && c.focusGuid == 0);
    // The same unit with its aura on a unit of this fight is taken.
    g.killFirst[0].auraOnEngaged = true;
    CHECK(Tick(c, g) == "focus 40");
}

// An auraOnEngaged unit on a pull step keeps the kKillFirstMaxYd bound: one
// farther off is left to come in, one within it is focused.
static void TestAuraOnEngagedKeepsThePullBound()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    UnitFact far = Unit(41, 7247, kKillFirstMaxYd + 5.0f);
    far.auraOnEngaged = true;
    f.killFirst = {far};
    CHECK(KillFirstBounded(r));
    CHECK(Tick(r, f).empty() && r.focusGuid == 0);
    f.killFirst[0].distLead = kKillFirstMaxYd - 1.0f;
    CHECK(Tick(r, f) == "focus 41");
}

// Frostmane Hollow r19-19z37 slot 1, step 413 (boss Hailar the Frigid 63130):
// the five Frostmane Ritualists 36519 (REACT_PASSIVE, never in combat, never
// attacked) channelled Frostmane Ritual 41592 (a periodic heal) on Hailar. The
// pull's Ritualist ...e95 was focused (3613.2 s) and died (3622.3 s); the four
// others were in no bot's attackers list, so f.killFirst was empty and no
// order went out for 680 s. With their aura on the engaged Hailar they are
// candidates (auraOnEngaged), and each death re-focuses the nearest left.
static void TestHailarRitualistsReplayR19_19z37()
{
    std::uint64_t const ritualist = 0xf130008ea7279e95ull; // ...e95
    std::uint64_t const e93 = ritualist - 2, e94 = ritualist - 1, e96 = ritualist + 1, e97 = ritualist + 2;
    std::uint64_t const hailar = 17379392021170986489ull;
    CHECK(ritualist == 17379391574712491669ull); // the guid in the trace's focus order
    DungeonPlan plan;
    plan.name = "frostmane_hollow";
    plan.killFirst = {36519};
    PlanStep boss = Step(PlanStepKind::Boss, 10);
    boss.entry = 63130;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(hailar, 63130, 3)};
    f.anyInCombat = true;
    // 3613.2 s: the pulled Ritualist fights the party.
    f.killFirst = {Engaged(Unit(ritualist, 36519, 24.5f))};
    CHECK(Tick(r, f) == "focus " + std::to_string(ritualist));
    CHECK(r.note == "fighting: kill 36519 first");
    // 3622.3 s: ...e95 died; the four others kept the Ritual on Hailar, out of
    // combat, at 28.2 / 39.9 / 43.6 / 47.9 yd (a boss step: unbounded).
    auto channeller = [](std::uint64_t guid, float dist)
    {
        UnitFact u = Unit(guid, 36519, dist);
        u.auraOnEngaged = true; // engaged stays false: cb:false in every frame
        return u;
    };
    f.killFirst = {channeller(e96, 39.9f), channeller(e94, 28.2f), channeller(e97, 47.9f), channeller(e93, 43.6f)};
    CHECK(Tick(r, f) == "focus " + std::to_string(e94));
    CHECK(Tick(r, f).empty()); // only on a change
    // Each dies in turn: the nearest left is focused.
    for (std::uint64_t const guid : {e96, e93, e97})
    {
        std::vector<UnitFact> left;
        for (const UnitFact& u : f.killFirst)
            if (u.guid != r.focusGuid)
                left.push_back(u);
        f.killFirst = left;
        CHECK(Tick(r, f) == "focus " + std::to_string(guid));
        CHECK(r.note == "fighting: kill 36519 first");
    }
    // The last dies: no order, the focus is over.
    f.killFirst.clear();
    CHECK(Tick(r, f).empty() && r.focusGuid == 0);
    CHECK(r.state == State::Running);
}

// The channeller rule as far as the policy sees it (the glue sets
// auraOnEngaged only for a creature whose channel is on an engaged unit now):
// on a boss step (unbounded) a kill_first unit without it stays unpicked even
// 8 yd away; a channeller that is gone (dead, or its channel stopped, so the
// glue no longer lists it) clears the focus with no order; a refused focus on
// a channeller passes it over for kFocusRefusedIgnoreMs, then it may be picked again once the
// focus is free (a same-rank focus is kept, 2b).
static void TestChannellerQualificationOnABossStep()
{
    std::uint64_t const e93 = 0xf130008ea7279e93ull, e94 = e93 + 1, e96 = e93 + 3;
    DungeonPlan plan;
    plan.name = "frostmane_hollow";
    plan.killFirst = {36519};
    PlanStep boss = Step(PlanStepKind::Boss, 10);
    boss.entry = 63130;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    CHECK(!KillFirstBounded(r));
    Facts f = Party(0);
    f.units = {Unit(17379392021170986489ull, 63130, 3)};
    f.anyInCombat = true;
    // Not engaged, no channel on an engaged unit: never picked, however near.
    f.killFirst = {Unit(e94, 36519, 8)};
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty() && r.focusGuid == 0);

    // A stale channeller: focused, then gone from the list (it died, or its
    // channel stopped): FocusGone clears the focus and nothing is sent.
    UnitFact ch = Unit(e94, 36519, 28.2f);
    ch.auraOnEngaged = true;
    f.killFirst = {ch};
    CHECK(Tick(r, f) == "focus " + std::to_string(e94));
    f.killFirst.clear();
    CHECK(Tick(r, f).empty() && r.focusGuid == 0);
    CHECK(Tick(r, f).empty() && r.focusGuid == 0);

    // A refused focus on the nearer channeller (out of reach): the next one is
    // focused at once, and the nearer one again only once the focus is free.
    Run q = Start(&plan);
    UnitFact far = Unit(e96, 36519, 39.9f);
    far.auraOnEngaged = true;
    f.killFirst = {ch, far};
    CHECK(Tick(q, f) == "focus " + std::to_string(e94));
    OrderRefused(q, "focus " + std::to_string(e94), "invalid-target");
    CHECK(q.focusGuid == 0 && q.state == State::Running);
    std::uint64_t const refusedAt = q.runMs;
    CHECK(Tick(q, f) == "focus " + std::to_string(e96));
    // Past the ignore window e94 may be picked again, but e96 (the same rank, focused) keeps the focus while it lives (2b).
    while (q.runMs < refusedAt + kFocusRefusedIgnoreMs + kTickMs)
        CHECK(Tick(q, f).empty() && q.focusGuid == e96);
    // e96 is gone (it died, or its channel stopped): the refused e94 is taken again, the refusal did not ban it.
    f.killFirst = {ch};
    CHECK(Tick(q, f) == "focus " + std::to_string(e94));
}

// A mob hitting the healer `dist` from the lead bot (Hitter is defined further down).
static UnitFact ZfHitter(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u = Unit(guid, entry, dist);
    u.hitting = true;
    u.victimHealer = true;
    return u;
}

// ZF r5-19z23 step 27, wipe 1 (15:10:58): the tank stood 20 yd from the
// antusul camp's 5650 when two witch doctors aggroed on it; "kill 5650 first"
// sent the tank and the party into the camp, next to the north group, and
// 13 mobs joined at once. A kill_first unit more than kKillFirstMaxYd from the
// lead bot is not focused (it is left to come to the tank, as in r3); within
// it, it is. The fight goes on with the bots' own picks in between.
static void TestKillFirstFarIsNotFocusedWipe1Replay()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true; // on firing stand 1
    f.units = {Unit(1, 5648, 24.5f)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    // The camp's two witch doctors, engaged with the tank from 20 and 23 yd.
    f.killFirst = {Engaged(Unit(30, 5650, 20)), Engaged(Unit(31, 5650, 23))};
    for (int i = 0; i < 20; ++i)
        CHECK(Tick(r, f).empty()); // no focus: the tank stays on its stand
    CHECK(r.focusGuid == 0 && r.state == State::Running);
    // One walks in to the tank: focused now.
    f.killFirst[0].distLead = kKillFirstMaxYd - 1.0f;
    CHECK(Tick(r, f) == "focus 30");
    // At exactly the bound it counts as near; just past it, not.
    Run b = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(1, 5648, 10)};
    CHECK(Tick(b, g) == "pullback 1");
    g.anyInCombat = true;
    g.killFirst = {Engaged(Unit(40, 5650, kKillFirstMaxYd + 0.5f))};
    CHECK(Tick(b, g).empty());
    g.killFirst[0].distLead = kKillFirstMaxYd;
    CHECK(Tick(b, g) == "focus 40");
}

// The bound picks a near unit over a far better-ranked one, and a focus once
// sent stays while its unit lives: a better-ranked unit that walks back and
// forth across kKillFirstMaxYd must not flip the focus every tick.
static void TestKillFirstBoundDoesNotFlipFlop()
{
    DungeonPlan plan = ZfPlan(); // kill_first 8179, 5650, 7247
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    // A 5650 (rank 1) far, a 7247 (rank 2) near: the near one.
    f.killFirst = {Engaged(Unit(30, 5650, 30)), Engaged(Unit(3, 7247, 5))};
    CHECK(Tick(r, f) == "focus 3");
    int focuses = 0;
    for (int i = 0; i < 40; ++i)
    {
        // The 5650 drifts in and out across the bound.
        f.killFirst[0].distLead = (i % 2) ? kKillFirstMaxYd - 2.0f : kKillFirstMaxYd + 2.0f;
        if (Tick(r, f).compare(0, 6, "focus ") == 0)
            ++focuses;
    }
    // One switch to the better rank once it came in, then it holds.
    CHECK(focuses == 1 && r.focusGuid == 30);
    // The focused 5650 steps out of the bound again: still the focus.
    f.killFirst[0].distLead = 30;
    CHECK(Tick(r, f).empty() && r.focusGuid == 30);
}

// A focus is never dropped for distance: no order takes it back (the skull and
// every bot's attack stay), so the policy must not think it over while the
// party still chases. A focused unit that walks back toward its camp keeps
// the focus (the known risk) and the guard keeps watching; it ends only when
// the unit is gone: dead (out of the list) or crowd-controlled.
static void TestHeldFocusEndsOnlyWhenItsUnitIsGone()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 12))};
    CHECK(Tick(r, f) == "focus 30");
    f.leadStaying = false;
    f.killFirst[0].distLead = 40; // back toward its camp
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).empty() && r.focusGuid == 30);
    // Alive but not pickable (evading): still held, the guard still watches.
    f.killFirst[0].evading = true;
    CHECK(Tick(r, f).empty() && r.focusGuid == 30);
    f.partyAttackers = {ZfHitter(50, 5649, 8)};
    CHECK(Tick(r, f) == "attack 50 Tanky" && r.guardOrders == 1);
    // Pick-ups stay available under the cap and the spacing.
    f.partyAttackers = {ZfHitter(51, 5649, 6)};
    int more = 0;
    for (int i = 0; i < 100; ++i)
        if (Tick(r, f).compare(0, 7, "attack ") == 0)
            ++more;
    CHECK(more == static_cast<int>(kGuardOrdersPerFight) - 1);
    // Crowd-controlled by the run: gone, the focus ends.
    Run c = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(1, 5648, 10)};
    CHECK(Tick(c, g) == "pullback 1");
    g.anyInCombat = true;
    g.killFirst = {Engaged(Unit(30, 5650, 12))};
    CHECK(Tick(c, g) == "focus 30");
    c.ccSent.insert(30);
    CHECK(Tick(c, g).empty() && c.focusGuid == 0);
}

// A boss step keeps the old rule: the plan's kill_first unit is focused
// wherever it stands (Zum'rah's Ward 7785 first while the tank holds his adds;
// the witch doctors, the 8179 ward and Anacondra are casters that keep ~25 yd).
static void TestKillFirstOnABossStepIsNotBounded()
{
    DungeonPlan plan = ZfPlan();
    PlanStep boss = Step(PlanStepKind::Boss, 10);
    boss.entry = 7271;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 7271, 10)};
    f.anyInCombat = true;
    f.killFirst = {Ward(9, 1, true, 20)};
    CHECK(Tick(r, f) == "focus 9");
    CHECK(KillFirstBounded(r) == false);
    // Far off and back: no drop on a boss step.
    f.killFirst[0].distLead = 38;
    CHECK(Tick(r, f).empty() && r.focusGuid == 9);
}

// The guard watches for a focus only while it is on: the focused unit dies
// (no kill_first unit left), the focus is cleared, and a mob on the healer
// with the tank off its stay and no pick-up or engage yet this fight is left
// to the tank's own AI, as before the focus rule.
static void TestGuardStopsWatchingWhenTheFocusEnds()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    f.leadStaying = false;
    f.killFirst.clear(); // it died
    CHECK(Tick(r, f).empty() && r.focusGuid == 0);
    f.partyAttackers = {ZfHitter(50, 5649, 8)};
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.guardOrders == 0);
}

// Scholomance r30 slot C t 531.5-973 2a: the fight is over, the focused unit went home (alive and listed, not
// engaged): the forget drops the skull too. Failing case fields-only: focusIconClear stays 0.
static void TestFocusSkullComesOffWhenTheFightEnds()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    CHECK(r.focusIconGuid == 30 && r.focusIconClear == 0);
    f.anyInCombat = false;
    f.killFirst = {Unit(30, 5650, 48)};
    Tick(r, f);
    CHECK(r.focusGuid == 0 && r.focusIconGuid == 0 && r.focusIconClear == 30);
}

// The wipe regroup's ResetStep drops the skull too.
// A pause clears the run's focus skull (the same clear path as a fight end); a resume picks a focus again as usual.
static void TestPauseDropsTheFocusSkull()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    CHECK(r.focusIconGuid == 30);
    Pause(r, "paused by hand");
    CHECK(r.state == State::Paused);
    CHECK(r.focusGuid == 0 && r.focusIconGuid == 0 && r.focusIconClear == 30);
    CHECK(Resume(r));
    f.anyInCombat = true;
    CHECK(Tick(r, f) == "focus 30");
}

// Documented limit: no ownership token. A player who puts the skull back on the run's own focus unit cannot be
// told apart, so the run clears a skull on its own focus unit when the focus drops, whoever set it last.
static void TestFocusClearTakesItsOwnSkullWhoeverSetIt()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    // The focus ends (the unit is gone): the recorded skull is handed to the glue to take off even if a player
    // re-marked the same unit meanwhile (indistinguishable).
    f.killFirst.clear();
    CHECK(Tick(r, f).empty());
    CHECK(r.focusGuid == 0 && r.focusIconClear == 30);
}

// The wipe regroup's ResetStep drops the skull too.
static void TestFocusSkullComesOffAtResetStep()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    ResetStep(r);
    CHECK(r.focusGuid == 0 && r.focusIconGuid == 0 && r.focusIconClear == 30);
}

// A refused focus takes its skull off; a stale refusal for another guid touches nothing.
static void TestRefusedFocusTakesItsSkullOff()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    OrderRefused(r, "focus 30", "failed: No bot of the group accepted the focus order.");
    CHECK(r.focusGuid == 0 && r.focusIconClear == 30);
    DungeonPlan plan2 = ZfPlan();
    Run s = Start(&plan2);
    Facts g = Party(0);
    g.units = {Unit(1, 5648, 10)};
    CHECK(Tick(s, g) == "pullback 1");
    g.anyInCombat = true;
    g.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(s, g) == "focus 30");
    OrderRefused(s, "focus 31", "failed: No bot of the group accepted the focus order.");
    CHECK(s.focusGuid == 30 && s.focusIconGuid == 30 && s.focusIconClear == 0);
}

// A refused new focus clears both the new pending clear and the previous skull: focus A, then a better-ranked B
// whose order is refused -> both A and B are pending clears. Mutation (dropping the prev field) must fail it.
static void TestRefusedNewFocusClearsBothSkulls()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    // A (rank 1) is focused first.
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    CHECK(r.focusGuid == 30 && r.focusIconGuid == 30 && r.prevFocusIconGuid == 0);
    // A better-ranked ward comes up: the new focus order goes out on B, remembering A's skull.
    f.killFirst.push_back(Ward(9, 30, true, 12));
    CHECK(Tick(r, f) == "focus 9");
    CHECK(r.focusGuid == 9 && r.focusIconGuid == 9 && r.prevFocusIconGuid == 30);
    // B's order is refused (not visible/found/dead): both clears are pending.
    OrderRefused(r, "focus 9", "failed: No bot of the group accepted the focus order.");
    CHECK(r.focusGuid == 0 && r.focusIconClear == 9 && r.prevFocusIconGuid == 30);
    CHECK(r.focusIconGuid == 0);
}

// A new focus moves the skull with no clear: pin for M5.
static void TestANewFocusMovesTheSkullWithNoClear()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    f.killFirst = {Engaged(Unit(31, 5650, 6))};
    CHECK(Tick(r, f) == "focus 31");
    CHECK(r.focusIconGuid == 31 && r.focusIconClear == 0);
    CHECK(r.prevFocusIconGuid == 30);
}

// FocusGone in a fight (cc landed on the focused unit) drops the skull too.
static void TestACrowdControlledFocusTakesItsSkullOff()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    r.ccSent.insert(30);
    CHECK(Tick(r, f).empty());
    CHECK(r.focusGuid == 0 && r.focusIconClear == 30);
}

// Scholomance r30 slot C t 1521.6-1530.7 2b: the two swap places but the focus holds. Failing case on the old
// code: focuses == 20, the CHECK after the loop.
static void TestKillFirstKeepsItsUnitAgainstAnEqualRank()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 6)), Engaged(Unit(31, 5650, 12))};
    CHECK(Tick(r, f) == "focus 30");
    int focuses = 0;
    for (int i = 0; i < 20; ++i)
    {
        f.killFirst[0].distLead = (i % 2) ? 6.0f : 13.0f;
        f.killFirst[1].distLead = (i % 2) ? 12.0f : 4.0f;
        if (Tick(r, f).compare(0, 6, "focus ") == 0)
            ++focuses;
    }
    CHECK(focuses == 0 && r.focusGuid == 30);
    f.killFirst = {Engaged(Unit(31, 5650, 4))};
    CHECK(Tick(r, f) == "focus 31");
}

// r30 t 398.8 2c: the tank leaves a caster at its camp alone. Common start per the spec.
static void TestGuardLeavesACasterAtItsCamp()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {10471, 10470})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f);
    CHECK(r.step == 1);
    f.units = {Unit(31, 10471, 40)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    UnitFact neophyte = Hitter(48840, 10470, 13.9f, true);
    neophyte.castingAtRange = true;
    neophyte.campIdle = 1;
    f.partyAttackers = {neophyte};
    CHECK(EngageTarget(r, f) == nullptr);
    CHECK(GuardTarget(r, f) == nullptr);
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).find("attack 48840") == std::string::npos);
    f.partyAttackers.push_back(Hitter(49002, 10489, 12.0f, true));
    CHECK(Tick(r, f) == "attack 49002 Tanky");
}

// Pin for M8: a camp-less caster is still taken.
static void TestGuardTakesACasterWithNoCampByIt()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {10471, 10470})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f);
    CHECK(r.step == 1);
    f.units = {Unit(31, 10471, 40)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    UnitFact neophyte = Hitter(48840, 10470, 13.9f, true);
    neophyte.castingAtRange = true;
    neophyte.campIdle = 0;
    f.partyAttackers = {neophyte};
    CHECK(Tick(r, f) == "attack 48840 Tanky");
}

// Pin for M9: a camp caster within a step of the tank is still taken.
static void TestGuardTakesACampCasterWithinAStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {10471, 10470})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f);
    CHECK(r.step == 1);
    f.units = {Unit(31, 10471, 40)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    UnitFact neophyte = Hitter(48840, 10470, 13.9f, true);
    neophyte.castingAtRange = true;
    neophyte.campIdle = 2;
    neophyte.distLead = 6.0f;
    f.partyAttackers = {neophyte};
    CHECK(Tick(r, f) == "attack 48840 Tanky");
}

// Pin for M12: a hold takes a camp caster in the hold; outside a hold the rule applies.
static void TestHoldGuardStillTakesACasterInTheHold()
{
    Facts h = Party(0);
    UnitFact keeperCaster = Hitter(7, 4857, 12.0f, true);
    keeperCaster.castingAtRange = true;
    keeperCaster.campIdle = 3;
    keeperCaster.distSpot = 5.0f;
    h.partyAttackers = {keeperCaster};
    CHECK(GuardPick(h, 20.0f) == &h.partyAttackers[0]);
    CHECK(GuardPick(h, 0.0f) == nullptr);
}

// ZF r5-19z23 step 27, wipe 2 (15:14:23): "kill 5650 first" took every bot off
// "stay" (CommanderFocus -> RelaxTacticalMovement), the lead included, with
// guardOrders 0 and engageGuid 0, so GuardTarget never watched: 0 pick-ups the
// whole fight while mobs hit the priest. A focus keeps the guard watching.
static void TestGuardWatchesAfterAFocusWipe2Replay()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    f.units = {Unit(1, 7247, 24.5f)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    // The focus ended the tank's stay; nothing has been picked up or engaged.
    f.leadStaying = false;
    CHECK(r.guardOrders == 0 && r.engageGuid == 0 && r.focusGuid == 30);
    // A 5649 hits the priest 8 yd from the tank: picked up at once (healer).
    f.partyAttackers = {ZfHitter(50, 5649, 8)};
    CHECK(Tick(r, f) == "attack 50 Tanky");
    CHECK(r.guardOrders == 1);
    CHECK(r.note.find("picks up 5649 off the healer") != std::string::npos);
    // No focus is re-sent: the pick-up does not change the kill_first pick.
    CHECK(Tick(r, f).empty() && r.focusGuid == 30);
}

// The order rate in the new state (focus on, the tank off its stay): healer
// attackers whose nearest changes every tick give at most kGuardOrdersPerFight
// pick-ups, each kGuardRepeatMs after the last order, and the focus is never
// re-sent. The same ten units hit the healer from the first tick: none joins
// later, so the budget is not refilled (CheckGuard; the refill has its own
// tests, TestGuardRefill*).
static void TestGuardAfterAFocusIsRateBounded()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 6))};
    CHECK(Tick(r, f) == "focus 30");
    f.leadStaying = false;
    int pickups = 0, focuses = 0, other = 0;
    std::uint32_t sinceLast = 0, minGap = ~0u;
    bool any = false;
    for (int i = 0; i < 300; ++i) // 30 s at kTickMs
    {
        f.partyAttackers.clear();
        for (int k = 0; k < 7; ++k)
            f.partyAttackers.push_back(ZfHitter(100 + k, 5649, 5.0f + ((i + k) % 7)));
        for (int k = 0; k < 3; ++k)
            f.partyAttackers.push_back(ZfHitter(200 + k, 5648, 4.5f + ((i + k) % 3)));
        std::string const o = Tick(r, f);
        sinceLast += kTickMs;
        if (o.compare(0, 7, "attack ") == 0)
        {
            ++pickups;
            if (any && sinceLast < minGap)
                minGap = sinceLast;
            any = true;
            sinceLast = 0;
        }
        else if (o.compare(0, 6, "focus ") == 0)
            ++focuses;
        else if (!o.empty())
            ++other;
    }
    CHECK(pickups == static_cast<int>(kGuardOrdersPerFight));
    CHECK(minGap >= kGuardRepeatMs);
    CHECK(focuses == 0 && other == 0);
}

// Deadmines r12-19z29 step 118, t 2207.9-2237: the kill_first 622 3569 was
// focused, turned to flee, and `focus <guid>` came back "No bot of the group
// accepted the focus order" (every bot refused an evading target). The focus
// stayed on it with no second order while it lived. Now the refusal clears
// the focus; the next tick focuses the next kill_first unit (622 356d), the
// refused one is passed over kFocusRefusedIgnoreMs and may be focused again
// after it; with no other unit the focus is none (the bots' own picks), and
// the guard keeps watching (the focus took every bot off its stay).
static void TestRefusedFocusIsPickedAgainDeadminesS118()
{
    std::uint64_t const fled = 0xf13000026e013569ull, next = 0xf13000026e01356dull;
    std::string const refusal = "failed: No bot of the group accepted the focus order.";
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1731})};
    plan.killFirst = {622};
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.leadIsTank = true;
        f.units = {Unit(1, 1731, 10)};
        CHECK(Tick(r, f) == "pullback 1");
        f.anyInCombat = true;
        f.killFirst = {Engaged(Unit(fled, 622, 4))};
        std::string const focusFled = "focus " + std::to_string(fled);
        CHECK(Tick(r, f) == focusFled && r.focusGuid == fled);
        OrderRefused(r, focusFled, refusal);
        CHECK(r.state == State::Running && r.focusGuid == 0);
        CHECK(r.note.find("the focus is picked again") != std::string::npos);
        // 356d (2218) joins: it is the focus now; the fled one is passed over.
        f.killFirst = {Engaged(Unit(fled, 622, 4)), Engaged(Unit(next, 622, 9))};
        CHECK(Tick(r, f) == "focus " + std::to_string(next) && r.focusGuid == next);
        // 356d dies; the fled one is still passed over: no focus, nothing held.
        f.killFirst = {Engaged(Unit(fled, 622, 4))};
        std::string o;
        int ticks = 1;
        for (; ticks < static_cast<int>(kFocusRefusedIgnoreMs / kTickMs) - 1; ++ticks)
            o += Tick(r, f);
        CHECK(o.empty() && r.focusGuid == 0);
        // After the wait: focused again (the retry).
        std::string again;
        for (int i = 0; i < 3 && again.empty(); ++i)
            again = Tick(r, f);
        CHECK(again == focusFled && r.focusGuid == fled);
    }
    {
        // No other kill_first unit: the focus is none, and the guard still
        // picks up a mob on the healer for the tank off its stay.
        Run r = Start(&plan);
        Facts f = Party(0);
        f.leadIsTank = true;
        f.units = {Unit(1, 1731, 10)};
        CHECK(Tick(r, f) == "pullback 1");
        f.anyInCombat = true;
        f.killFirst = {Engaged(Unit(fled, 622, 4))};
        std::string const focusFled = "focus " + std::to_string(fled);
        CHECK(Tick(r, f) == focusFled);
        OrderRefused(r, focusFled, refusal);
        CHECK(Tick(r, f).empty() && r.focusGuid == 0);
        f.partyAttackers = {ZfHitter(0xf1300006c301358full, 1731, 1)};
        CHECK(Tick(r, f) == "attack " + std::to_string(0xf1300006c301358full) + " Tanky");
        // A refusal of a focus that is no longer the current one leaves it.
        f.partyAttackers.clear();
        f.killFirst = {Engaged(Unit(next, 622, 6))};
        CHECK(Tick(r, f) == "focus " + std::to_string(next));
        OrderRefused(r, focusFled, refusal);
        CHECK(r.focusGuid == next);
        // The next fight starts with an empty list.
        f.anyInCombat = false;
        f.killFirst.clear();
        Tick(r, f);
        CHECK(r.focusRefused.empty() && !r.focusUnstayed);
    }
}

// 19z5: the mage enters the fight seconds after the pull (it waits for the
// tank's threat, then walks into range); its Polymorphs landed 7-8 s into its
// own fight. The window counts only the mage's fight time.
static void TestCcWindowCountsTheMagesFightTime()
{
    DungeonPlan plan = ZfPlan();
    plan.killFirst.clear();
    Run r = Start(&plan);
    Facts f = Party(0);
    MemberFact mage{"Magey", 5, true, false};
    f.members = {mage};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    for (int i = 0; i < 20; ++i)
        CHECK(Tick(r, f).empty()); // the mage not in the fight yet: not counted
    CHECK(r.ccPending == 2 && !r.ccResent);
    f.members[0].inCombat = true;
    std::string order;
    std::uint32_t ticks = 0;
    for (; ticks <= kCcLandMs / kTickMs && order.empty(); ++ticks)
        order = Tick(r, f);
    CHECK(order == "cc moon 2 Magey" && ticks == kCcLandMs / kTickMs);
}

// Issue #1159: the run's crowd control by the target's creature type and the
// spells the party knows (kRunCc): Polymorph (mage: humanoid, beast, critter),
// Shackle Undead (priest: undead), Banish (warlock: demon, elemental), Sap
// (rogue: humanoid, before the pull only). Entries 100-199 humanoid, 200-299
// undead, 300 demon, 400 elemental, 500 mechanical (the creature types are
// the facts', the entries only name them).
static UnitFact Typed(std::uint64_t guid, std::uint32_t entry, float dist, std::uint32_t type)
{
    UnitFact u = Unit(guid, entry, dist);
    u.creatureType = type;
    return u;
}

static MemberFact CcMem(const std::string& name, CcFamily family, bool inCombat = false)
{
    MemberFact m = Mem(name, 5);
    if (family == CcFamily::Polymorph)
        m.polymorph = true;
    else
        m.cc = CcBit(family);
    m.canSap = family == CcFamily::Sap; // a live client master (HasRealPlayerMaster)
    m.inCombat = inCombat;
    return m;
}

static DungeonPlan CcPlan(std::vector<std::uint32_t> ccPriority)
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1, 100, 101, 200, 201, 300, 400, 500})};
    plan.ccPriority = ccPriority;
    return plan;
}

static void TestPriestShacklesUndeadWithoutAMage()
{
    DungeonPlan plan = CcPlan({200});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), CcMem("Priesty", CcFamily::Shackle)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 200, 20, kCreatureUndead)};
    CHECK(Tick(r, f) == "cc moon 2 Priesty");
    CHECK(r.note.find("crowd control on 200") == 0 && r.note.find("Shackle Undead") != std::string::npos);
    CHECK(r.ccPending == 2);
    CHECK(Tick(r, f) == "pullback 1");
    // In the fight the shackle shows (the glue's held flag): landed.
    f.anyInCombat = true;
    f.members[1].inCombat = true;
    f.units[1].polymorphed = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.ccPending == 0 && r.note.find("200 is shackled") != std::string::npos);
}

static void TestWarlockBanishesDemonsAndElementals()
{
    for (std::uint32_t type : {kCreatureDemon, kCreatureElemental})
    {
        DungeonPlan plan = CcPlan({300});
        Run r = Start(&plan);
        Facts f = Party(0);
        f.members = {CcMem("Locky", CcFamily::Banish)};
        f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 300, 20, type)};
        CHECK(Tick(r, f) == "cc moon 2 Locky");
        CHECK(r.note.find("Banish") != std::string::npos);
    }
    // Fear is no run crowd control: a warlock on a humanoid sends nothing.
    DungeonPlan plan = CcPlan({100});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Locky", CcFamily::Banish)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "pullback 1" && r.ccSent.empty());
}

static void TestMagePolymorphsHumanoidsFirst()
{
    // A humanoid fits the mage and the rogue: the Polymorph goes first.
    DungeonPlan plan = CcPlan({100});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Roguey", CcFamily::Sap), CcMem("Magey", CcFamily::Polymorph)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    // The rogue does not take the same mob: the pull goes.
    CHECK(Tick(r, f) == "pullback 1");
    // Two of the same entry: the nearer gets the Polymorph, the other the Sap.
    Run s = Start(&plan);
    Facts g = Party(0);
    g.members = f.members;
    g.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 30, kCreatureHumanoid), Typed(3, 100, 12, kCreatureHumanoid)};
    CHECK(Tick(s, g) == "cc moon 3 Magey");
    CHECK(Tick(s, g) == "cc square 2 Roguey");
}

static void TestTwoCcBotsTakeTwoMobsNeverOne()
{
    DungeonPlan plan = CcPlan({100, 101});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Magey", CcFamily::Polymorph), CcMem("Magetwo", CcFamily::Polymorph)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid),
        Typed(3, 101, 20, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(Tick(r, f) == "cc square 3 Magetwo");
    CHECK(Tick(r, f) == "pullback 1");
    CHECK(r.ccHolds.size() == 2 && r.ccHolds.at(2).bot == "Magey" && r.ccHolds.at(3).bot == "Magetwo");
    // One mage alone holds one mob at a time.
    Run o = Start(&plan);
    Facts g = f;
    g.members = {CcMem("Magey", CcFamily::Polymorph)};
    CHECK(Tick(o, g) == "cc moon 2 Magey");
    CHECK(Tick(o, g) == "pullback 1");
    CHECK(o.ccSent.size() == 1);
    // The mark and the mage are free again once the sheep died: the next
    // listed mob takes them.
    Run m = Start(&plan);
    Facts h = g;
    h.units.pop_back();
    CHECK(Tick(m, h) == "cc moon 2 Magey");
    h.units.erase(h.units.begin() + 1); // the sheep died
    h.units.push_back(Typed(3, 101, 20, kCreatureHumanoid));
    CHECK(Tick(m, h) == "cc moon 3 Magey");
}

static void TestImmuneTypeIsNeverPicked()
{
    DungeonPlan plan = CcPlan({200, 500, 300, 100});
    Run r = Start(&plan);
    Facts f = Party(0);
    // A mage only: undead, mechanical and demon are immune to Polymorph.
    f.members = {CcMem("Magey", CcFamily::Polymorph)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 200, 20, kCreatureUndead),
        Typed(3, 500, 20, kCreatureMechanical), Typed(4, 300, 20, kCreatureDemon)};
    CHECK(Tick(r, f) == "pullback 1" && r.ccSent.empty());
    // Every cc class: the mechanical one still never; the undead goes to the
    // priest, the demon to the warlock.
    Run a = Start(&plan);
    Facts g = f;
    g.members = {CcMem("Magey", CcFamily::Polymorph), CcMem("Priesty", CcFamily::Shackle),
        CcMem("Locky", CcFamily::Banish), CcMem("Roguey", CcFamily::Sap)};
    CHECK(Tick(a, g) == "cc moon 2 Priesty");
    CHECK(Tick(a, g) == "cc square 4 Locky");
    CHECK(Tick(a, g) == "pullback 1");
    CHECK(!a.ccSent.count(3));
    // An unread type (0) is Polymorph's only, as before the table.
    Run u = Start(&plan);
    Facts h = Party(0);
    h.members = {CcMem("Priesty", CcFamily::Shackle), CcMem("Locky", CcFamily::Banish), CcMem("Roguey", CcFamily::Sap)};
    h.units = {Unit(1, 1, 10), Unit(2, 200, 20)};
    CHECK(Tick(u, h) == "pullback 1" && u.ccSent.empty());
    CHECK(!CcFits(CcFamily::Shackle, 0) && CcFits(CcFamily::Polymorph, 0));
}

static void TestCcPriorityOrderComesBeforeTheSpell()
{
    // The priest's undead is ranked first though a humanoid for the mage is
    // nearer and the more reliable spell.
    DungeonPlan plan = CcPlan({201, 100});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Magey", CcFamily::Polymorph), CcMem("Priesty", CcFamily::Shackle)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 12, kCreatureHumanoid),
        Typed(3, 201, 30, kCreatureUndead)};
    const UnitFact* cc = PickCc(r, f, 0);
    CHECK(cc && cc->guid == 3);
    CHECK(Tick(r, f) == "cc moon 3 Priesty");
    CHECK(Tick(r, f) == "cc square 2 Magey");
}

static void TestRogueSapsBeforeThePull()
{
    DungeonPlan plan = CcPlan({100});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), CcMem("Roguey", CcFamily::Sap)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Roguey");
    CHECK(r.note.find("Sap") != std::string::npos);
    // The pull waits for the Sap; the rogue walking off is not gathered back.
    f.maxMemberYd = 30;
    f.members[1].yd = 30;
    CHECK(Tick(r, f).empty() && r.note.find("waiting for Roguey's Sap on 100") == 0);
    CHECK(Tick(r, f).empty());
    // Sapped: the party gathers (the rogue comes back), then the pull.
    f.units[1].polymorphed = true;
    std::string const gather = Tick(r, f);
    CHECK(gather.compare(0, 5, "move ") == 0 && r.ccPending == 0);
    f.maxMemberYd = 0;
    f.members[1].yd = 5;
    CHECK(Tick(r, f) == "pullback 1");
    // Never once the party fights: Sap is a pre-pull spell.
    Run c = Start(&plan);
    Facts g = Party(0);
    g.members = {CcMem("Roguey", CcFamily::Sap)};
    g.units = f.units;
    g.units[1].polymorphed = false;
    g.anyInCombat = true;
    CHECK(PickCc(c, g, 0) == nullptr);
    g.anyInCombat = false;
    g.members[0].inCombat = true;
    CHECK(PickCc(c, g, 0) == nullptr);
    g.members[0].inCombat = false;
    CHECK(PickCc(c, g, 0) != nullptr);
}

static void TestSapThatNeverLandsLetsThePullGo()
{
    DungeonPlan plan = CcPlan({100, 101});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Roguey", CcFamily::Sap)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid),
        Typed(3, 101, 22, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Roguey");
    std::uint32_t ticks = 0;
    std::string order;
    for (; ticks <= kSapWaitMs / kTickMs + 1 && order.empty(); ++ticks)
        order = Tick(r, f);
    CHECK(ticks == kSapWaitMs / kTickMs);
    // The rogue is taken off the mark (and the icon cleared) before anything else.
    CHECK(order == "cc clear Roguey moon 2" && r.note.find("sap did not land on 100") == 0);
    // The rogue is walked back to the lead bot's spot (Shadowfang Keep r26 slot 7 step 46: left by its mob, it
    // went back and forth on the stair below the party), then no second Sap this step: the pull goes, and the
    // unit is not held.
    order = Tick(r, f);
    CHECK(order == "walk Roguey 0.0 0.0 0.0" && r.note.find("Roguey walked to the lead by path") != std::string::npos);
    order = Tick(r, f);
    CHECK(order == "pullback 1");
    CHECK(r.ccSapFailed && !r.ccSent.count(2) && r.ccHolds.empty() && r.ccPending == 0);
    CHECK(PickCc(r, f, 0) == nullptr);
    // Combat during the wait: the fight's check drops it, with no second Sap.
    Run c = Start(&plan);
    CHECK(Tick(c, f) == "cc moon 2 Roguey");
    Facts g = f;
    g.anyInCombat = true;
    g.members[0].inCombat = true;
    order.clear();
    for (std::uint32_t i = 0; i <= 2 * kCcLandMs / kTickMs; ++i)
        order += Tick(c, g);
    CHECK(order.find("cc ") == std::string::npos && c.ccWatch.empty() && c.ccPending == 0);
}

static void TestCcResendUsesItsOwnMark()
{
    DungeonPlan plan = CcPlan({100, 200});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Magey", CcFamily::Polymorph), CcMem("Priesty", CcFamily::Shackle)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid),
        Typed(3, 200, 20, kCreatureUndead)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(Tick(r, f) == "cc square 3 Priesty");
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.members[0].inCombat = true;
    f.members[1].inCombat = true;
    f.units[1].polymorphed = true; // the sheep landed; the shackle did not
    std::string orders;
    for (std::uint32_t i = 0; i <= kCcLandMs / kTickMs + 1; ++i)
        orders += Tick(r, f);
    CHECK(orders == "cc square 3 Priesty");
    CHECK(r.ccWatch.size() == 1 && r.ccWatch[0].resent && r.ccPending == 3 && r.ccResent);
}

// Review of fe5d4109: a Sap that times out stops the rogue (`cc clear`)
// before the gather calls the party together; no other Sap that step.
static void TestSapTimeoutClearsTheRogueBeforeTheGather()
{
    DungeonPlan plan = CcPlan({100, 101});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), CcMem("Roguey", CcFamily::Sap)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid),
        Typed(3, 101, 22, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Roguey");
    CHECK(r.ccIcons.count(4) && r.ccIcons.at(4) == 2);
    f.raidIconsRead = true;
    f.raidIcons[4] = 2; // the order put moon on it
    f.maxMemberYd = 30; // the rogue is off by its mob
    f.members[1].yd = 30;
    std::string order;
    for (std::uint32_t i = 0; i <= kSapWaitMs / kTickMs + 1 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "cc clear Roguey moon 2");
    CHECK(r.ccIcons.count(4) && r.ccIcons.at(4) == 2); // kept until the facts show the clear
    f.raidIcons[4] = 0; // the clear went through
    order = Tick(r, f);
    CHECK(r.ccIcons.empty());
    CHECK(order.compare(0, 5, "move ") == 0 && r.note.find("gathering before the pull") != std::string::npos);
    // After the gather (whose follow would undo it), the rogue's own walk back to the lead bot's spot.
    CHECK(Tick(r, f) == "walk Roguey 0.0 0.0 0.0");
    f.maxMemberYd = 0;
    f.members[1].yd = 5;
    CHECK(Tick(r, f) == "pullback 1");
    CHECK(PickCc(r, f, 0) == nullptr && r.ccSent.empty());
}

// Codex on fe5d4109: a watched Sap target that drops out of the facts for a
// tick (range, sight) is a cancelled Sap: the rogue is cleared and the unit
// is free again: back in the read, it is no held unit (pulled and focused as
// any other).
static void TestSapTargetGoneForATickIsCancelled()
{
    DungeonPlan plan = CcPlan({100});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Roguey", CcFamily::Sap)};
    f.units = {Typed(1, 1, 30, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Roguey");
    CHECK(Tick(r, f).empty()); // waiting
    Facts gone = f;
    gone.units.pop_back();
    CHECK(Tick(r, gone) == "cc clear Roguey moon 2");
    CHECK(r.ccSapFailed && !r.ccSent.count(2) && r.ccHolds.empty() && r.ccWatch.empty());
    // Back: the nearest unit, no longer held, is the pull (after the rogue's walk back to the lead bot).
    const UnitFact* pull = PickPackPull(r, f);
    CHECK(pull && pull->guid == 2);
    CHECK(Tick(r, f) == "walk Roguey 0.0 0.0 0.0");
    CHECK(Tick(r, f) == "pullback 2");
}

// Codex on fe5d4109: a raid icon set by someone else is never moved; the run
// takes the next free mark, none when all four are taken (noted), and reuses
// an icon it set itself once its unit is gone.
static void TestCcMarksOnlyFreeOrTheRunsOwn()
{
    DungeonPlan plan = CcPlan({100});
    Facts f = Party(0);
    f.members = {CcMem("Magey", CcFamily::Polymorph)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    f.raidIcons[4] = 99; // a player's moon
    Run r = Start(&plan);
    CHECK(Tick(r, f) == "cc square 2 Magey");
    // Every cc mark taken: no cc, noted with the pull.
    Run n = Start(&plan);
    Facts g = f;
    g.raidIcons[5] = 98;
    g.raidIcons[2] = 97;
    g.raidIcons[3] = 96;
    CHECK(Tick(n, g) == "pullback 1");
    CHECK(n.ccSent.empty() && n.note.find("no free raid mark for crowd control on 100") != std::string::npos);
    // The run's own moon, still on a unit that is gone, is taken again.
    Run o = Start(&plan);
    Facts h = Party(0);
    h.members = f.members;
    h.units = f.units;
    o.ccIcons[4] = 77;
    h.raidIcons[4] = 77;
    CHECK(Tick(o, h) == "cc moon 2 Magey");
    // Run end: the icons the run set, for the glue to clear.
    auto const clear = CcIconsToClear(o);
    CHECK(clear.size() == 1 && clear[0].first == 4 && clear[0].second == 2);
    CHECK(CcMarkIcon("moon") == 4 && CcMarkIcon("square") == 5 && CcMarkIcon("diamond") == 2 &&
        CcMarkIcon("triangle") == 3 && CcMarkIcon("skull") == -1);
}

// Codex on fe5d4109: kill_first wins: a unit whose entry is in both lists is
// killed, never held.
static void TestKillFirstUnitIsNeverCrowdControlled()
{
    DungeonPlan plan = CcPlan({100, 101});
    plan.killFirst = {100};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Magey", CcFamily::Polymorph)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid),
        Typed(3, 101, 25, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 3 Magey");
    CHECK(Tick(r, f) == "pullback 2"); // the kill_first unit is the pull
}

// Opus on fe5d4109: a warlock only for a demon or elemental within Banish's
// range and not immune to it (else its "fear on cc" row can fire on the mark),
// and taken off the mark when the Banish never lands. Immunity holds for every
// spell (a Polymorph-immune humanoid too).
static void TestWarlockOnlyWhereBanishCanLand()
{
    DungeonPlan plan = CcPlan({300});
    Facts f = Party(0);
    f.members = {CcMem("Locky", CcFamily::Banish, true)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 300, kBanishYd + 5, kCreatureDemon)};
    Run far = Start(&plan);
    CHECK(PickCc(far, f, 0) == nullptr);
    f.units[1].distLead = kBanishYd;
    f.units[1].ccImmune = CcBit(CcFamily::Banish);
    CHECK(PickCc(far, f, 0) == nullptr);
    f.units[1].ccImmune = CcBit(CcFamily::Polymorph);
    CHECK(PickCc(far, f, 0) != nullptr);
    Facts m = Party(0);
    m.members = {CcMem("Magey", CcFamily::Polymorph)};
    m.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    m.units[1].ccImmune = CcBit(CcFamily::Polymorph);
    DungeonPlan hp = CcPlan({100});
    Run mr = Start(&hp);
    CHECK(PickCc(mr, m, 0) == nullptr);
    // Never lands: once more, then `cc clear` for the warlock.
    Run r = Start(&plan);
    f.units[1].ccImmune = 0;
    CHECK(Tick(r, f) == "cc moon 2 Locky");
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    std::string orders;
    for (std::uint32_t i = 0; i <= 2 * kCcLandMs / kTickMs + 2; ++i)
        orders += Tick(r, f) + "|";
    CHECK(orders.find("cc moon 2 Locky|") != std::string::npos);
    CHECK(orders.find("cc clear Locky moon 2|") != std::string::npos);
    CHECK(orders.find("cc moon 2 Locky|") < orders.find("cc clear Locky moon 2|"));
    CHECK(r.ccHolds.empty() && r.ccWatch.empty());
    CHECK(r.ccIcons.count(4) && r.ccIcons.at(4) == 2); // no icon read here: the record stays for the run's end
}

// Codex round 2 on 66e3ad30: a refused `cc clear` (the bot left the group, no
// AI) keeps the run's record of the icon, so the run's end clears it; the
// record goes only once the facts show the icon off the unit (cleared, or
// moved by someone else, which the run's end then leaves alone).
static void TestRefusedCcClearKeepsTheIconForTheRunsEnd()
{
    DungeonPlan plan = CcPlan({100});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Roguey", CcFamily::Sap)};
    f.units = {Typed(1, 1, 30, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Roguey");
    f.raidIconsRead = true;
    f.raidIcons[4] = 2;
    Facts gone = f;
    gone.units.pop_back();
    CHECK(Tick(r, gone) == "cc clear Roguey moon 2");
    OrderRefused(r, "cc clear Roguey moon 2", "no-bot: That bot is not in your commanded group.");
    CHECK(Tick(r, f) == "walk Roguey 0.0 0.0 0.0");
    CHECK(Tick(r, f) == "pullback 2"); // the icon is still on it
    CHECK(r.ccIcons.count(4) && r.ccIcons.at(4) == 2);
    auto const clear = CcIconsToClear(r);
    CHECK(clear.size() == 1 && clear[0].first == 4 && clear[0].second == 2);
    // The run's end: still on the unit, cleared; moved by a player, left; no group now, kept to retry.
    CHECK(RunIconAtEnd(true, 2, 2) == RunIconEnd::Clear);
    CHECK(RunIconAtEnd(true, 55, 2) == RunIconEnd::Drop);
    CHECK(RunIconAtEnd(true, 0, 2) == RunIconEnd::Drop);
    CHECK(RunIconAtEnd(false, 0, 2) == RunIconEnd::Retry);
    // A player moves moon elsewhere: no longer the run's.
    f.raidIcons[4] = 55;
    Tick(r, f);
    CHECK(r.ccIcons.empty());
    // Icons not read this tick (no group): nothing is concluded.
    Run k = Start(&plan);
    k.ccIcons[4] = 2;
    Facts unread = f;
    unread.raidIconsRead = false;
    unread.raidIcons[4] = 0;
    Tick(k, unread);
    CHECK(k.ccIcons.count(4));
}

// The rogue's pre-pull Sap trigger runs only for a bot with a live client
// master (HasRealPlayerMaster): without one no Sap is ordered (no 15 s lost).
static void TestNoSapWithoutALiveMaster()
{
    DungeonPlan plan = CcPlan({100});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Roguey", CcFamily::Sap)};
    f.members[0].canSap = false;
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    CHECK(PickCc(r, f, 0) == nullptr);
    CHECK(Tick(r, f) == "pullback 1" && r.ccSent.empty());
}

static void TestStepIdleCeiling()
{
    DungeonPlan plan;
    PlanStep clear = Step(PlanStepKind::WaitClear, 0, 0, 0);
    clear.radius = 20;
    ai::NormalizePlanStep(clear);
    plan.steps = {clear};
    CHECK(StepIdleCapMs(plan.steps[0]) == kStepIdleCapMs);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.clearUnits = {HostileSpot{5.0f, 0.0f, 0.0f}, HostileSpot{-5.0f, 0.0f, 0.0f}};
    for (int i = 0; i < 100 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("hostiles still near") != std::string::npos);
}

// Shadowfang Keep r38 slotA step 308: the wait_clear spot at z 129 counted
// hostiles on other floors (z ~100-157) and paused four times on
// "waited 120 s; 3-4 hostiles still near the spot". Round 2: the normal wait
// counts every hostile in the 2D radius (no dz filter: a required mob idling
// on another level still holds the step); only the timeout boundary drops the
// bystanders. This test guards the count; the boundary has its own test below.
static void TestWaitClearCountsEveryHostileInRadius()
{
    CHECK(kWaitClearMaxDz == 10.0f);
    DungeonPlan plan;
    PlanStep clear = Step(PlanStepKind::WaitClear, -147.1f, 2171.6f, 129.0f);
    clear.radius = 35.0f;
    clear.timeoutS = 120;
    plan.steps = {clear};
    // (a) a non-engaged hostile at dz 27 HOLDS the step before the timeout.
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.clearUnits = {HostileSpot{-147.1f, 2171.6f, 156.0f}, HostileSpot{-140.0f, 2170.0f, 156.0f}};
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.note.find("waiting for the spot to clear") != std::string::npos);
    CHECK(WaitClearCount(plan.steps[0], f) == 2);
    // (b) a hostile within the radius at dz 5 still waits.
    Run r2 = Start(&plan);
    Facts f2 = Party(0);
    f2.seen = true;
    f2.clearUnits = {HostileSpot{-147.1f, 2171.6f, 156.0f}, HostileSpot{-147.1f, 2171.6f, 134.0f}};
    CHECK(Tick(r2, f2).empty());
    CHECK(r2.state == State::Running && r2.note.find("waiting for the spot to clear") != std::string::npos);
    CHECK(WaitClearCount(plan.steps[0], f2) == 2);
    // (c) the count keeps the 2D radius: outside it counts nothing.
    Run r3 = Start(&plan);
    Facts f3 = Party(0);
    f3.seen = true;
    f3.clearUnits = {HostileSpot{-100.0f, 2171.6f, 156.0f}};
    CHECK(Tick(r3, f3).empty());
    CHECK(r3.state == State::Done);
    CHECK(WaitClearCount(plan.steps[0], f3) == 0);
}

// Round 2: the timeout boundary. Past the step's own wait, bystanders left on
// other floors (every remaining hostile non-engaged and |dz| > 10) finish the
// step instead of pausing it; anything engaged or on the spot's floor pauses
// exactly as before. Uses a second step so the finish note survives DoneNote.
static void TestWaitClearTimeoutDropsOffFloorBystanders()
{
    auto Plan = []() {
        DungeonPlan plan;
        PlanStep clear = Step(PlanStepKind::WaitClear, -147.1f, 2171.6f, 129.0f);
        clear.radius = 35.0f;
        clear.timeoutS = 2;
        plan.steps = {clear, Step(PlanStepKind::Move, 9)};
        return plan;
    };
    auto Spot = [](float x, float y, float z, bool engaged) {
        HostileSpot h{x, y, z};
        h.engaged = engaged;
        return h;
    };
    auto AtTimeout = [](Run& r, const Facts& f) {
        CHECK(Tick(r, f).empty());
        CHECK(r.state == State::Running); // the first tick enters the wait
        CHECK(Tick(r, f).empty());
        CHECK(r.state == State::Running); // 1 s in, still waiting
        CHECK(Tick(r, f).empty());        // 2 s: the wait runs out here
    };
    // (b) the r38 Shadowfang case at the timeout: non-engaged bystanders at dz
    // 19-28 finish the step with the note, no pause.
    DungeonPlan plan = Plan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.clearUnits = {Spot(-147.1f, 2171.6f, 148.0f, false), Spot(-140.0f, 2170.0f, 157.0f, false),
        Spot(-150.0f, 2175.0f, 100.0f, false), Spot(-147.1f, 2171.6f, 156.0f, false)};
    AtTimeout(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note.find("the spot is clear on its floor: 4 hostiles left on other floors (|dz| > 10)") != std::string::npos);
    CHECK(WaitClearOffFloorOnly(plan.steps[0], f));
    // (c) at the timeout with one engaged hostile at dz 27: pause as before.
    DungeonPlan planC = Plan();
    Run rc = Start(&planC);
    Facts fc = Party(0);
    fc.seen = true;
    fc.clearUnits = {Spot(-147.1f, 2171.6f, 148.0f, false), Spot(-140.0f, 2170.0f, 100.0f, true)};
    AtTimeout(rc, fc);
    CHECK(rc.state == State::Paused && rc.note.find("hostiles still near the spot") != std::string::npos);
    CHECK(!WaitClearOffFloorOnly(planC.steps[0], fc));
    // (d) at the timeout with one non-engaged hostile at dz 5: pause as before.
    DungeonPlan planD = Plan();
    Run rd = Start(&planD);
    Facts fd = Party(0);
    fd.seen = true;
    fd.clearUnits = {Spot(-147.1f, 2171.6f, 148.0f, false), Spot(-147.1f, 2171.6f, 134.0f, false)};
    AtTimeout(rd, fd);
    CHECK(rd.state == State::Paused && rd.note.find("hostiles still near the spot") != std::string::npos);
    CHECK(!WaitClearOffFloorOnly(planD.steps[0], fd));
    // The boundary keeps the edge: exactly dz 10.0 still pauses, 10.1 finishes.
    DungeonPlan planE = Plan();
    Run re = Start(&planE);
    Facts fe = Party(0);
    fe.seen = true;
    fe.clearUnits = {Spot(-147.1f, 2171.6f, 139.0f, false)};
    AtTimeout(re, fe);
    CHECK(re.state == State::Paused && re.note.find("hostiles still near the spot") != std::string::npos);
    DungeonPlan planF = Plan();
    Run rf = Start(&planF);
    Facts ff = Party(0);
    ff.seen = true;
    ff.clearUnits = {Spot(-147.1f, 2171.6f, 139.1f, false)};
    AtTimeout(rf, ff);
    CHECK(rf.state == State::Running && rf.step == 1);
    CHECK(rf.note.find("the spot is clear on its floor: 1 hostiles left on other floors (|dz| > 10)") != std::string::npos);
}

// Round 2: the engaged flag still matters, but only at the boundary now. An
// engaged hostile at dz 27 holds the normal wait (as does any hostile), and at
// the timeout it pauses the step; bystanders alone finish it (covered above).
static void TestWaitClearEngagedPausesAtTimeout()
{
    DungeonPlan plan;
    PlanStep clear = Step(PlanStepKind::WaitClear, -147.1f, 2171.6f, 129.0f);
    clear.radius = 35.0f;
    clear.timeoutS = 120;
    plan.steps = {clear, Step(PlanStepKind::Move, 9)};
    auto Spot = [](float x, float y, float z, bool engaged) {
        HostileSpot h{x, y, z};
        h.engaged = engaged;
        return h;
    };
    // An engaged hostile at dz 27 holds the normal wait.
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.clearUnits = {Spot(-147.1f, 2171.6f, 156.0f, true)};
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.note.find("waiting for the spot to clear") != std::string::npos);
    CHECK(WaitClearCount(plan.steps[0], f) == 1);
    CHECK(!WaitClearOffFloorOnly(plan.steps[0], f));
    // Mixed before the timeout: one engaged far above plus bystanders below still waits.
    Run r3 = Start(&plan);
    Facts f3 = Party(0);
    f3.seen = true;
    f3.clearUnits = {Spot(-147.1f, 2171.6f, 148.0f, false), Spot(-140.0f, 2170.0f, 100.0f, true)};
    CHECK(Tick(r3, f3).empty());
    CHECK(r3.state == State::Running);
    CHECK(WaitClearCount(plan.steps[0], f3) == 2);
    CHECK(!WaitClearOffFloorOnly(plan.steps[0], f3));
}

static void TestTeamFlags()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {4851}, ai::kPlanFlagHorde), Step(PlanStepKind::Rest)};
    Facts f = Party(0);
    f.team = ai::kPlanFlagAlliance;
    f.seen = false;
    // Not seen yet: a pack for the other team is skipped without a walk.
    Run r = Start(&plan);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note.find("other team") != std::string::npos);
    // Seen and nothing hostile there: skipped too.
    Run r2 = Start(&plan);
    f.seen = true;
    f.units = {Unit(5, 4851, 10)};
    f.units[0].hostile = false;
    Tick(r2, f);
    CHECK(r2.step == 1);
    // Seen and hostile anyway: fought like any pack.
    Run r3 = Start(&plan);
    f.units[0].hostile = true;
    CHECK(Tick(r3, f) == "pullback 5");
    // The party's own team, or a step for both: normal.
    Run r4 = Start(&plan);
    f.team = ai::kPlanFlagHorde;
    CHECK(Tick(r4, f) == "pullback 5");
    CHECK(ai::StepForTeam(plan.steps[0], 0) && ai::StepForTeam(plan.steps[1], ai::kPlanFlagAlliance));
}

// Live bug (first bots-only Uldaman run): the Lost Dwarves (boss flagged
// horde) were skipped for an Alliance party before anybody looked at them.
static void TestOtherTeamBossIsLookedAtAndFoughtUnlessFriendly()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 100);
    boss.entry = 6906;
    boss.entries = {6907, 6908};
    boss.flags = ai::kPlanFlagHorde;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Facts f = Party(0);
    f.team = ai::kPlanFlagAlliance;
    f.seen = false;
    // Not in sight: walk to it, never skip.
    Run r = Start(&plan);
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.step == 0);
    // In sight, the dwarves neutral or hostile: fought, Eric first.
    f.seen = true;
    f.leadX = 70;
    f.units = {Unit(1, 6906, 30), Unit(3, 6907, 35)};
    Run r2 = Start(&plan);
    CHECK(Tick(r2, f) == "pullback 3");
    // In sight and all friendly: skipped, saying why.
    for (UnitFact& u : f.units)
        u.hostile = false;
    Run r3 = Start(&plan);
    Tick(r3, f);
    CHECK(r3.step == 1 && r3.note.find("friendly") != std::string::npos);
    // In sight and nobody there: skipped (not "boss down": it was never fought).
    f.units.clear();
    Run r4 = Start(&plan);
    Tick(r4, f);
    CHECK(r4.step == 1 && r4.note == "boss is for the other team and not here: skipped");
    // The party's own boss in sight and dead: down.
    f.team = ai::kPlanFlagHorde;
    f.bossDeadNearSpot = true;
    Run r5 = Start(&plan);
    Tick(r5, f);
    CHECK(r5.step == 1 && r5.note == "boss down");
}

// Fourth observe run (Theka, ZF steps 54-56): "boss down" 2 s after arriving,
// with no fight; Theka patrols. A boss neither alive nor dead in sight is
// waited for at his spot, then the run pauses; never counted down.
static void TestBossDownNeedsASeenDeath()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 7272;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && r.state == State::Running && r.note.find("not at his spot") != std::string::npos);
    // He walks in on his patrol: pulled.
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    f.units = {Unit(5, 7272, 40)};
    CHECK(Tick(r, f) == "pullback 5");
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    // Killed: his corpse is seen.
    f.units.clear();
    f.bossDeadNearSpot = true;
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "boss down");

    // Never there: paused after kBossSearchMs, "not found".
    Run p = Start(&plan);
    Facts g = Party(0);
    for (std::uint32_t i = 0; i <= kBossSearchMs / kTickMs + 1 && p.state == State::Running; ++i)
        Tick(p, g);
    CHECK(p.state == State::Paused && p.step == 0 && p.note.find("boss 7272 not found") == 0);
}

// Wailing Caverns' Kresh (3653): he walks a 525.9 yd round in the river, about 210 s at the walk speed, and passes the
// plan's boss point (his third waypoint) within the row's 30 yd radius at least every 96 s. The plan carries wait_s 300,
// which plan-export writes as the row's timeout_s: the away wait at his point is that, not the 60 s default (which
// missed him 9 runs in 10 and paused), and the step's idle ceiling grows with it. A boss without a timeout keeps 60 s.
static void TestBossAwayWaitIsTheRowsTimeout()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 3653;
    boss.timeoutS = 300;
    ai::NormalizePlanStep(boss);
    CHECK(boss.timeoutS == 300);
    CHECK(StepIdleCapMs(boss) == 300000u + 60000u);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && r.state == State::Running && r.note.find("not at his spot") != std::string::npos);
    // 200 s in he is still waited for: the default 60 s would have paused the run by now.
    for (std::uint32_t i = 0; i < 200 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 0 && r.note.find("not at his spot") != std::string::npos);
    // He walks in on his beat: pulled as usual.
    f.units = {Unit(9, 3653, 20)};
    CHECK(Tick(r, f) == "pullback 9");
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units.clear();
    f.bossDeadNearSpot = true;
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "boss down");

    // Never there: the instance search and the pause come after the row's 300 s, and the note says so.
    Run p = Start(&plan);
    Facts g = Party(0);
    for (std::uint32_t i = 0; i < 300 && p.state == State::Running; ++i)
        Tick(p, g);
    CHECK(p.state == State::Running);
    for (std::uint32_t i = 0; i < 5 && p.state == State::Running; ++i)
        Tick(p, g);
    CHECK(p.state == State::Paused && p.step == 0 && p.note.find("for 300 s") != std::string::npos);

    // The default row (timeout 0 before the loader): 60 s, as before.
    PlanStep plain = Step(PlanStepKind::Boss, 30);
    plain.entry = 7272;
    ai::NormalizePlanStep(plain);
    CHECK(plain.timeoutS == ai::kPlanDefaultTimeoutS && BossAwayWaitMs(plain) == kBossSearchMs);
}

// Fourth observe run (step 43 -> 45) and 19z5 step 57 (Sul'lithuz Hatchlings
// 8130): a pullback refused invalid-target. The unit is left out for the
// run without a pause; the pack is skipped as out of reach, and a later step
// that sees the same unit pulls the other one. A skip clears the pull state.
static void TestSkipDropsTheRefusedPullTarget()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1}), Step(PlanStepKind::Move, 20), PullStep(30, {1, 2})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    CHECK(Tick(r, f) == "pullback 7");
    OrderRefused(r, "pullback 7", "invalid-target: The group cannot attack that target.");
    CHECK(r.state == State::Running && r.pullGuid == 0);
    Tick(r, f);
    CHECK(r.step == 1 && r.note.find("out of reach") != std::string::npos);
    f = Party(20);
    Tick(r, f);
    CHECK(r.step == 2);
    f.units = {Unit(7, 1, 5), Unit(8, 2, 15)};
    CHECK(Tick(r, f) == "pullback 8");
    // A skip clears the step's pull state.
    CHECK(Skip(r));
    CHECK(r.pullGuid == 0 && r.pulls == 0 && r.pullTries.empty());
}

static void TestResumeWithLeadDead()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.leadAlive = false;
    Tick(r, f);
    // The owner's wait rule: no pause, the regroup waits for the lead bot.
    CHECK(r.state == State::Running && r.regroup);
    Pause(r, "test: the old pause");
    CHECK(Resume(r));
    CHECK(r.regroup);
    CHECK(Tick(r, f) == "revive party");
    // The ghost runs back: no pause while the regroup's ceiling holds.
    for (int i = 0; i < 100; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.regroup);
    // Alive again, gathered: the step goes on.
    f.dead = 0;
    f.alive = 5;
    f.leadAlive = true;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.regroup);
    // A lead bot that never comes back is bounded by kRegroupReviveMs.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    g.dead = 1;
    g.alive = 4;
    g.leadAlive = false;
    Tick(r2, g);
    Resume(r2);
    for (std::uint32_t i = 0; i <= kRegroupReviveMs / kTickMs + 2 && r2.state == State::Running; ++i)
        Tick(r2, g);
    CHECK(r2.state == State::Paused && r2.note.find("still dead") != std::string::npos);
}

static UnitFact Hitter(std::uint64_t guid, std::uint32_t entry, float dist, bool healer = false)
{
    UnitFact u = Unit(guid, entry, dist);
    u.hitting = true;
    u.victimHealer = healer;
    return u;
}

// Uldaman rerun 2026-10-17, the walk to Galgann at -10 337: the tank held
// "stay" with nothing on it while the party fought 13-31 yd away. With mobs
// hitting the party for kGuardWaitMs the tank picks the nearest one up
// (`attack <guid> <tank>`), and after the fight the run walks it back to the
// stand point.
static void TestTankGuardsThePartyFromItsStand()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855, 4853})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f); // on the stand point: the anchor, next step
    CHECK(r.step == 1 && r.anchorKnown);
    f.units = {Unit(31, 4853, 40)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    // The pullback's mobs aggroed on the healer and still run in: not hitting yet.
    UnitFact running = Unit(31, 4853, 30);
    running.victimHealer = true;
    f.partyAttackers = {running};
    for (int i = 0; i < 8; ++i)
        CHECK(Tick(r, f).empty());
    // They arrive and hit the party; nothing is on the tank.
    f.partyAttackers = {Hitter(41, 7320, 35), Hitter(51, 7320, 45), Hitter(31, 4853, 30)};
    for (std::uint32_t ms = kTickMs; ms < kGuardWaitMs; ms += kTickMs)
        CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f) == "attack 31 Tanky"); // the nearest one within kGuardYd
    CHECK(r.guardReturn);
    // The order ended the stay; the tank holds that mob now.
    f.leadStaying = false;
    f.leadHit = true;
    f.leadX = 28;
    f.partyAttackers = {Hitter(41, 7320, 12)};
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).empty()); // a mob on a DPS while the tank holds its own: the tank's own AI
    // The fight is over and the pack cleared: the step goes on from where the
    // tank stands (AiPlayerbot.DungeonRunWalkBackAfterWonFight 0; until
    // 2026-10-22 it walked back to the stand point first, as the key at 1 does).
    f.anyInCombat = false;
    f.leadHit = false;
    f.partyAttackers.clear();
    f.units.clear();
    Facts old = f;
    Run rOld = r;
    CHECK(Tick(r, f).empty());
    CHECK(!r.guardReturn && r.state == State::Done);
    old.walkBackAfterWonFight = true;
    CHECK(Tick(rOld, old) == "move 0.0 0.0 0.0");
    old.leadX = 1;
    old.leadStaying = true;
    CHECK(Tick(rOld, old).empty()); // arrived: the next tick reads fresh facts
    CHECK(!rOld.guardReturn);
    Tick(rOld, old);
    CHECK(rOld.state == State::Done);
}

// Uldaman run 3 (19z12, tests/uldaman-events-3), baelog: the leg's pulls have
// no move step, so the run's anchor stayed the leg's start point; after every
// guarded fight the tank walked 90 yd back there, and on arrival the step
// paused at once on "no walkable path toward the pack" (the arrival tick's
// facts had no approach query: the glue skips it while the run walks). The
// tank walks back to its own spot at the first pick-up, the arrival ends the
// tick, and the next tick approaches the unseen pack.
static void TestTankGuardReturnsToItsOwnSpot()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(150, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f); // the leg's start point: the anchor
    CHECK(r.step == 1 && r.anchorKnown && r.anchorX == 0.0f);
    // The pull goes out from 90 yd on (the pullback stand, no walk of the run's).
    f.leadX = 90;
    f.anchorPathRead = true;
    f.units = {Unit(31, 4851, 60), Unit(32, 4851, 62)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    f.partyAttackers = {Hitter(31, 4851, 12, true)};
    CHECK(Tick(r, f) == "attack 31 Tanky");
    CHECK(r.guardReturn && r.guardX == 90.0f);
    // The tank chased its mob 15 yd; the fight is over, one of the pack is left.
    f.leadStaying = false;
    f.leadX = 105;
    f.anyInCombat = false;
    f.partyAttackers.clear();
    f.units = {Unit(32, 4851, 60)};
    CHECK(Tick(r, f) == "move 90.0 0.0 0.0"); // its own spot, not the leg's start
    // Arrived; the pack is out of sight and this tick's facts have no approach.
    f.leadX = 90;
    f.leadStaying = true;
    f.seen = false;
    f.approachKnown = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && !r.guardReturn);
    f.approachKnown = true;
    f.approachX = 125;
    std::string order;
    for (int i = 0; i < 5 && order.empty() && r.state == State::Running; ++i)
        order = Tick(r, f);
    CHECK(order == "move 125.0 0.0 0.0");
    CHECK(r.state == State::Running);

    // A spot the fight took the tank far from is left: it stays near the fight.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    g.leadIsTank = true;
    g.leadStaying = true;
    Tick(r2, g);
    g.leadX = 90;
    g.anchorPathRead = true;
    g.units = {Unit(31, 4851, 60)};
    CHECK(Tick(r2, g) == "pullback 31");
    g.anyInCombat = true;
    g.partyAttackers = {Hitter(31, 4851, 12, true)};
    CHECK(Tick(r2, g) == "attack 31 Tanky");
    g.leadStaying = false;
    g.leadX = 90 + kGuardReturnMaxYd + 5;
    g.anyInCombat = false;
    g.partyAttackers.clear();
    std::string const next = Tick(r2, g);
    CHECK(next != "move 90.0 0.0 0.0" && next != "move 0.0 0.0 0.0");
    CHECK(!r2.guardReturn);
    // That spot is the party's anchor now (a later gather does not walk back).
    CHECK(r2.anchorKnown && r2.anchorX == g.leadX);

    // Uldaman run 3, pull 2: the tank on stay was still dragging the pullback
    // in (18 yd short of its stand) at the first pick-up. The walk back goes
    // to its stay point, not to where the pick-up found it.
    Run r3 = Start(&plan);
    Facts h = Party(0);
    h.leadIsTank = true;
    h.leadStaying = true;
    Tick(r3, h);
    h.leadX = 90;
    h.anchorPathRead = true;
    h.units = {Unit(31, 4851, 60)};
    CHECK(Tick(r3, h) == "pullback 31");
    h.anyInCombat = true;
    h.leadX = 108; // mid-drag
    h.leadStayKnown = true;
    h.leadStayX = 90;
    h.partyAttackers = {Hitter(31, 4851, 12, true)};
    CHECK(Tick(r3, h) == "attack 31 Tanky");
    CHECK(r3.guardX == 90.0f);
    h.leadStaying = false;
    h.leadStayKnown = false;
    h.anyInCombat = false;
    h.partyAttackers.clear();
    h.units = {Unit(32, 4851, 60)};
    CHECK(Tick(r3, h) == "move 90.0 0.0 0.0");
}

// Frostmane Hollow r20-19z39 slot 1 (2026-10-22), steps 277 and 278 (trace
// 3044-3107 s): the tank walked to firing stand 1 for 63134, pulled, engaged it
// on arrival and fought 26 s ahead of the stand; with nothing hostile left the
// run walked the tank and the party back to the stand ("back to the stand
// point after picking up the party's attackers", 3080-3082 s), said "pack
// cleared" and walked forward again for the next 63134. The owner: "why do they
// run back from the bosses?". `walkBack` is AiPlayerbot.DungeonRunWalkBackAfterWonFight.
static Run FrostmaneWonFight(DungeonPlan& plan, bool walkBack, Facts& f, const PlanStep* fightStep = nullptr)
{
    plan.steps = {Step(PlanStepKind::Move, 0), fightStep ? *fightStep : PullStep(40, {63134}), PullStep(70, {63134})};
    Run r = Start(&plan);
    f = Party(0);
    f.walkBackAfterWonFight = walkBack;
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f); // on the stand point: the anchor, step 277
    CHECK(r.step == 1 && r.anchorKnown);
    f.units = {Unit(31, 63134, 24.5f)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    // It comes in on the party ("the pull stops, 1 mob has aggro already"); on
    // the healer here, so the guard needs no kGuardWaitMs (live: off a pet).
    f.partyAttackers = {Hitter(31, 63134, 6.9f, true)};
    CHECK(Tick(r, f) == "attack 31 Tanky"); // engaged on arrival: the guard's stand is noted
    CHECK(r.guardReturn && r.guardX == 0.0f);
    // The tank fights it 20 yd ahead of the stand.
    f.leadStaying = false;
    f.leadHit = true;
    f.leadX = 20;
    f.partyAttackers.clear();
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).empty());
    // 3077 s: combat off, 63134 dead, nothing hostile left of the step.
    f.anyInCombat = false;
    f.leadHit = false;
    f.units.clear();
    f.anchorPathRead = true;
    f.anchorReachable = true;
    return r;
}

static void TestNoWalkBackAfterAWonFight()
{
    DungeonPlan plan;
    Facts f;
    Run r = FrostmaneWonFight(plan, false, f);
    std::string const first = Tick(r, f);
    CHECK(first.compare(0, 5, "move ") != 0); // no walk back to the stand
    CHECK(!r.guardReturn);
    CHECK(r.note.find("back to the stand point") == std::string::npos);
    CHECK(r.step == 2 && r.state == State::Running); // "pack cleared" at once
    // Step 278: the next 63134 is pulled from where the tank stands.
    f.leadStaying = true;
    f.units = {Unit(32, 63134, 24.5f)};
    std::string order;
    for (int i = 0; i < 5 && order.empty() && r.state == State::Running; ++i)
    {
        order = Tick(r, f);
        CHECK(order.compare(0, 5, "move ") != 0);
    }
    CHECK(order == "pullback 32");
    CHECK(r.state == State::Running);
}

// AiPlayerbot.DungeonRunWalkBackAfterWonFight = 1: the old walk back, as in the trace.
static void TestWalkBackAfterAWonFightOnTheOldKey()
{
    DungeonPlan plan;
    Facts f;
    Run r = FrostmaneWonFight(plan, true, f);
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(r.guardReturn && r.step == 1);
    CHECK(r.note == "back to the stand point after picking up the party's attackers");
    f.leadX = 1;
    f.leadStaying = true;
    CHECK(Tick(r, f).empty()); // arrived
    CHECK(!r.guardReturn);
    Tick(r, f);
    CHECK(r.step == 2);
}

// Kept with the key at 0: during the fight nothing walks back (the walk back is
// only ever ordered out of combat) and the stand stays noted for later; with
// one of the pack left after the fight the same step pulls again from the same
// stand, so the tank goes back to it first (TestTankGuardReturnsToItsOwnSpot
// covers the unseen pack after that walk). A move step's fight goes on forward
// to the step's point, not back to where the pick-up found the tank.
static void TestWalkBackKeptWhereItMatters()
{
    {
        // A fight still running: no walk back, the return stays pending.
        DungeonPlan plan;
        Facts f;
        Run r = FrostmaneWonFight(plan, false, f);
        f.anyInCombat = true;
        f.units = {Unit(33, 63134, 12)};
        f.partyAttackers = {Hitter(33, 63134, 12)};
        for (int i = 0; i < 10; ++i)
            CHECK(Tick(r, f).compare(0, 5, "move ") != 0);
        CHECK(r.guardReturn && r.fighting && r.step == 1);
        // That one dies too: the pack is cleared, the run goes on from here.
        f.anyInCombat = false;
        f.units.clear();
        f.partyAttackers.clear();
        CHECK(Tick(r, f).compare(0, 5, "move ") != 0);
        CHECK(!r.guardReturn && r.step == 2);
    }
    {
        // One of the step's pack left after the fight: the same stand pulls it.
        DungeonPlan plan;
        Facts f;
        Run r = FrostmaneWonFight(plan, false, f);
        f.units = {Unit(34, 63134, 30)};
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
        CHECK(r.guardReturn && r.step == 1);
    }
    {
        // A move step's fight: the walk goes on to the step's point.
        DungeonPlan plan;
        plan.steps = {Step(PlanStepKind::Move, 0), Step(PlanStepKind::Move, 60), PullStep(200, {4851})};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.leadIsTank = true;
        f.leadStaying = true;
        Tick(r, f);
        CHECK(r.step == 1);
        CHECK(Tick(r, f) == "move 60.0 0.0 0.0");
        f.leadX = 20;
        f.leadStayKnown = true;
        f.leadStayX = 20;
        f.anyInCombat = true;
        f.partyAttackers = {Hitter(41, 4851, 8, true)};
        CHECK(Tick(r, f) == "attack 41 Tanky");
        CHECK(r.guardReturn && r.guardX == 20.0f);
        f.leadStaying = false;
        f.leadStayKnown = false;
        f.leadX = 28;
        f.anyInCombat = false;
        f.partyAttackers.clear();
        std::string order;
        for (int i = 0; i < 5 && order.empty(); ++i)
            order = Tick(r, f);
        CHECK(order == "move 60.0 0.0 0.0");
        CHECK(!r.guardReturn && r.step == 1);
        // The key at 1: back to 20 first.
        Run r2 = Start(&plan);
        Facts g = Party(0);
        g.walkBackAfterWonFight = true;
        g.leadIsTank = true;
        g.leadStaying = true;
        Tick(r2, g);
        CHECK(Tick(r2, g) == "move 60.0 0.0 0.0");
        g.leadX = 20;
        g.leadStayKnown = true;
        g.leadStayX = 20;
        g.anyInCombat = true;
        g.partyAttackers = {Hitter(41, 4851, 8, true)};
        CHECK(Tick(r2, g) == "attack 41 Tanky");
        g.leadStaying = false;
        g.leadStayKnown = false;
        g.leadX = 28;
        g.anyInCombat = false;
        g.partyAttackers.clear();
        CHECK(Tick(r2, g) == "move 20.0 0.0 0.0");
    }
}

// Codex adversarial review of 7537eab8: an empty unit list is no won fight by
// itself. A boss whose death was not witnessed (away on a patrol, or his
// corpse outside the query) and a pack whose point is out of sight keep the
// walk back: the step goes on from the stand. The suppression follows the
// step's own end rule (FightStepEnd), as FightStep does.
static void TestWalkBackKeptWhenTheStepIsNotOver()
{
    {
        // A boss step: units gone, his death not seen.
        PlanStep boss = Step(PlanStepKind::Boss, 40);
        boss.entry = 63134;
        ai::NormalizePlanStep(boss);
        DungeonPlan plan;
        Facts f;
        Run r = FrostmaneWonFight(plan, false, f, &boss);
        CHECK(!f.bossDeadNearSpot && !f.bossDeadNearLead);
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
        CHECK(r.guardReturn && r.step == 1);
        CHECK(r.note == "back to the stand point after picking up the party's attackers");
        // Seen dead near his spot: boss down, no walk back.
        DungeonPlan plan2;
        Facts g;
        Run r2 = FrostmaneWonFight(plan2, false, g, &boss);
        g.bossDeadNearSpot = true;
        CHECK(Tick(r2, g).compare(0, 5, "move ") != 0);
        CHECK(!r2.guardReturn && r2.step == 2 && r2.note.find("boss down") != std::string::npos);
    }
    {
        // A pull whose point is out of sight: an empty list there is not a cleared pack.
        DungeonPlan plan;
        Facts f;
        Run r = FrostmaneWonFight(plan, false, f);
        f.seen = false;
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
        CHECK(r.guardReturn && r.step == 1);
    }
    {
        // A patroller of the pack still to come: the step waits from the stand.
        DungeonPlan plan;
        Facts f;
        Run r = FrostmaneWonFight(plan, false, f);
        f.patrollers = {Unit(35, 63134, 50)};
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
        CHECK(r.guardReturn && r.step == 1);
    }
}

// Cold review of 59589d67: a guard fight on an ambush pull step before the
// party walked through it. The pack shows itself only on the walk, so an empty
// list is no cleared pack: the walk back is kept.
static void TestWalkBackKeptOnAnAmbushNotWalkedThrough()
{
    PlanStep ambush = PullStep(40, {63134}, ai::kPlanFlagAmbush);
    DungeonPlan plan;
    Facts f;
    Run r = FrostmaneWonFight(plan, false, f, &ambush);
    CHECK(!r.walkedThrough);
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(r.guardReturn && r.step == 1);
    // Walked through already: the pack is cleared, no walk back.
    DungeonPlan plan2;
    Facts g;
    Run r2 = FrostmaneWonFight(plan2, false, g, &ambush);
    r2.walkedThrough = true;
    CHECK(Tick(r2, g).compare(0, 5, "move ") != 0);
    CHECK(!r2.guardReturn && r2.step == 2);
}

// Cold review of 59589d67: during a wipe's walk back the guard's walk back is
// judged on the replayed point's step (FactStep), a move: the replay goes on
// forward. The step the run returns to (a pull with its pack still up) would
// have kept it and walked the party back to the old stand.
static void TestGuardWalkBackJudgedOnTheReplayedPoint()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), Step(PlanStepKind::Move, 60), PullStep(120, {4851})};
    auto replay = [&plan](bool walkBack)
    {
        Run r = Start(&plan);
        r.step = 2;
        r.walkBack = walkBack;
        r.walkBackIdx = 1;
        r.guardReturn = true;
        r.guardX = 20;
        Facts f = Party(30);
        f.leadIsTank = true;
        f.anchorPathRead = true;
        f.anchorReachable = true;
        f.units = {Unit(31, 4851, 90)}; // the pull's pack, still up
        std::string const order = Tick(r, f);
        return std::make_pair(order, r.guardReturn);
    };
    auto const during = replay(true);
    CHECK(during.first != "move 20.0 0.0 0.0" && !during.second);
    // Not in a walk back: the pull with units left keeps it (the same stand).
    auto const after = replay(false);
    CHECK(after.first == "move 20.0 0.0 0.0" && after.second);
}

// Replay of keepers door pull 100 (trace r-...9bbf): a Brawler on the tank in
// melee reach, a Geomancer casting at the mage from 30 yd, then two Maulers on
// the mage and the healer. The tank keeps its Brawler while only the mage is
// hit; a mob hitting the healer is picked up (it needs no stay once the guard
// took the tank off it), the next one after a fresh wait, and the cap is
// reached in one fight.
static void TestTankGuardPull100Replay()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855, 4853})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f);
    f.units = {Unit(97, 4855, 40)};
    CHECK(Tick(r, f) == "pullback 97");
    f.anyInCombat = true;
    f.leadHit = true; // the Brawler, 1 yd
    f.partyAttackers = {Hitter(98, 4853, 30)}; // the Geomancer on the mage
    for (int i = 0; i < 8; ++i)
        CHECK(Tick(r, f).empty());
    // A Mauler reaches the healer at T (trace: 7478); she is dead at T+2-3 s
    // (botwatch 73 % at 03:07:22, 0 % at 03:07:26): the order goes out on the
    // first tick she is seen hit.
    f.partyAttackers = {Hitter(98, 4853, 30), Hitter(90, 7320, 14, true)};
    CHECK(Tick(r, f) == "attack 90 Tanky");
    // Off its stay now; the second Mauler turns on the healer: picked up once
    // kGuardRepeatMs have passed since the first, and the cap is reached in
    // one fight.
    f.leadStaying = false;
    f.partyAttackers = {Hitter(98, 4853, 30), Hitter(91, 7320, 20, true)};
    CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f) == "attack 91 Tanky");
    int orders = 2;
    for (int i = 0; i < 20; ++i)
        if (Tick(r, f).compare(0, 7, "attack ") == 0)
            ++orders;
    // The first order was an engage on arrival (a healer's attacker on the
    // staying tank), which the cap does not count: it and the cap's pick-ups.
    // Nothing joined, so no refill and no per-unit cap: the second Mauler
    // takes all 3 pick-ups (literals: the Deadmines refill must not change it).
    CHECK(orders == 4);
    CHECK(r.guardOrders == 3 && r.guardUnitOrders[91] == 3 && r.guardRefills == 0);
    CHECK(r.state == State::Running);
}

// Cold review 2026-10-22 of the Deadmines refill: pull 100 with one Mauler on
// the healer and nothing joining. No refill, so no per-unit cap: the engage on
// arrival and all 3 pick-ups go to that Mauler, as before the refill. Then a
// unit joins far off: the refill's orders pass the Mauler over (it had 4, the
// per-unit cap after a refill is 2) and go to the joiner, 2 at most.
static void TestTankGuardOneMaulerAndTheCapAfterARefill()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855, 4853})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f);
    f.units = {Unit(97, 4855, 40)};
    CHECK(Tick(r, f) == "pullback 97");
    f.anyInCombat = true;
    f.leadHit = true; // the Brawler, 1 yd
    f.partyAttackers = {Hitter(90, 7320, 14, true)};
    CHECK(Tick(r, f) == "attack 90 Tanky"); // engage on arrival
    f.leadStaying = false;
    int orders = 1;
    for (int i = 0; i < 30; ++i)
    {
        std::string const o = Tick(r, f);
        if (!o.empty())
        {
            CHECK(o == "attack 90 Tanky");
            ++orders;
        }
    }
    CHECK(orders == 4 && r.guardOrders == 3 && r.guardUnitOrders[90] == 4 && r.guardRefills == 0);
    // A joiner on the healer, 30 yd off: the refill; the Mauler is capped now.
    f.partyAttackers = {Hitter(90, 7320, 14, true), Hitter(92, 2520, 30, true)};
    std::vector<std::string> after;
    for (int i = 0; i < 30; ++i)
    {
        std::string const o = Tick(r, f);
        if (!o.empty())
            after.push_back(o);
    }
    CHECK(r.guardRefills == 1);
    CHECK(after.size() == 2 && after[0] == "attack 92 Tanky" && after[1] == "attack 92 Tanky");
    CHECK(r.guardUnitOrders[90] == 4 && r.guardUnitOrders[92] == 2);
}

// The guard stays out of the way: no tank, a tank off its stay the guard did
// not take off, a polymorphed, evading, too far or not yet hitting mob, a
// scripted event; a refused pick-up or walk back never pauses the run.
static void TestTankGuardLimits()
{
    auto guardRun = [](DungeonPlan& plan)
    {
        plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855})};
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        f.units = {Unit(31, 4855, 40)};
        Tick(r, f); // pullback 31
        return r;
    };
    auto fight = [](bool tank, bool staying, bool hit)
    {
        Facts f = Party(0);
        f.anyInCombat = true;
        f.leadIsTank = tank;
        f.leadStaying = staying;
        f.leadHit = hit;
        f.partyAttackers = {Hitter(31, 4855, 20)};
        return f;
    };
    auto ordersIn = [](Run& r, const Facts& f, int ticks)
    {
        int n = 0;
        for (int i = 0; i < ticks; ++i)
            if (Tick(r, f).compare(0, 7, "attack ") == 0)
                ++n;
        return n;
    };
    {
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight(true, true, true), 8) == 0);   // the tank holds a mob; the victim is no healer
        CHECK(ordersIn(r, fight(false, true, false), 8) == 0); // the lead bot is no tank
        CHECK(ordersIn(r, fight(true, false, false), 8) == 0); // off its stay by itself: its own AI goes already
        Facts f = fight(true, true, false);
        f.partyAttackers[0].polymorphed = true;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].polymorphed = false;
        f.partyAttackers[0].evading = true;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].evading = false;
        f.partyAttackers[0].hitting = false;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].hitting = true;
        f.partyAttackers[0].distLead = kGuardYd + 1;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].distLead = 20;
        CHECK(ordersIn(r, f, 5) == 1);
        OrderRefused(r, "attack 31 Tanky", "failed: That bot did not accept the attack.");
        CHECK(r.state == State::Running);
    }
    {
        // Inside a scripted event but not in its hold (a step leading into it):
        // nothing (the hold's own guard: tools/test_dungeon_event_policy.cpp).
        DungeonPlan plan;
        Run r = guardRun(plan);
        r.eventOn = true;
        CHECK(ordersIn(r, fight(true, true, false), 8) == 0);
    }
    {
        // A refused walk back to the stand point goes on from where it stands.
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight(true, true, false), 5) == 1);
        Facts after = Party(25);
        after.walkBackAfterWonFight = true; // the pack is gone: only the key at 1 walks back
        CHECK(Tick(r, after) == "move 0.0 0.0 0.0");
        OrderRefused(r, "move 0.0 0.0 0.0", "unreachable: The lead bot has no walkable path to that point.");
        CHECK(r.state == State::Running && !r.guardReturn);
    }
}

// Uldaman Pause 2 (scorpid_cave step 38, 2026-10-20): a Scorpid had a pet as its
// victim 15 yd from the tank and never came; the guard saw only mobs on bots,
// so the tank stood on "stay" and the fight ran past the 180 s ceiling. A mob
// on a party pet is now picked up too, but after every mob on a bot, never while
// the tank holds a mob of its own, and never from beyond kGuardYd of the tank
// (a pet's attacker beside a sleeping pack must not pull the tank across).
static UnitFact PetHitter(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u = Hitter(guid, entry, dist);
    u.victimPet = true;
    return u;
}

static void TestTankGuardPicksUpMobsOnPets()
{
    auto guardRun = [](DungeonPlan& plan)
    {
        plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855})};
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        f.units = {Unit(31, 4855, 40)};
        Tick(r, f); // pullback 31
        return r;
    };
    auto fight = [](std::vector<UnitFact> const& attackers, bool hit = false)
    {
        Facts f = Party(0);
        f.anyInCombat = true;
        f.leadIsTank = true;
        f.leadStaying = true;
        f.leadHit = hit;
        f.partyAttackers = attackers;
        return f;
    };
    std::string orderNote; // the run's note on the tick that sent the order
    auto ordersIn = [&orderNote](Run& r, const Facts& f, int ticks)
    {
        std::string last;
        int n = 0;
        for (int i = 0; i < ticks; ++i)
        {
            std::string const o = Tick(r, f);
            if (o.compare(0, 7, "attack ") == 0)
            {
                ++n;
                last = o;
                orderNote = r.note;
            }
        }
        return std::make_pair(n, last);
    };
    {
        // The Pause 2 case: only a pet has a mob on it, 15 yd away. After the
        // guard's wait the tank is sent at it, and the note says "off a pet".
        DungeonPlan plan;
        Run r = guardRun(plan);
        auto const res = ordersIn(r, fight({PetHitter(31, 4863, 15)}), 5);
        CHECK(res.first == 1 && res.second == "attack 31 Tanky");
        CHECK(orderNote.find("off a pet") != std::string::npos);
        CHECK(r.guardReturn);
    }
    {
        // Bounded by kGuardYd from the tank: a pet's attacker at the edge of a
        // sleeping pack, beyond the bound, draws nothing; just inside it does.
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight({PetHitter(31, 4863, kGuardYd + 1)}), 10).first == 0);
        CHECK(ordersIn(r, fight({PetHitter(31, 4863, kGuardYd - 1)}), 5).first == 1);
    }
    {
        // A mob on a bot goes first, though the pet's attacker is nearer; a pet's
        // attacker only when no mob on a bot qualifies (here: one is not hitting yet).
        DungeonPlan plan;
        Run r = guardRun(plan);
        auto const res = ordersIn(r, fight({PetHitter(31, 4863, 8), Hitter(41, 7320, 30)}), 5);
        CHECK(res.first == 1 && res.second == "attack 41 Tanky");
        CHECK(orderNote.find("off a pet") == std::string::npos);
        DungeonPlan plan2;
        Run r2 = guardRun(plan2);
        UnitFact running = Unit(41, 7320, 30);
        auto const res2 = ordersIn(r2, fight({PetHitter(31, 4863, 8), running}), 5);
        CHECK(res2.first == 1 && res2.second == "attack 31 Tanky");
    }
    {
        // The tank holds a mob of its own: it leaves a pet's attacker to its AI.
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight({PetHitter(31, 4863, 15)}, true), 10).first == 0);
    }
    {
        // Not hitting (still running in), evading or polymorphed: no pick-up.
        DungeonPlan plan;
        Run r = guardRun(plan);
        UnitFact u = PetHitter(31, 4863, 15);
        u.hitting = false;
        CHECK(ordersIn(r, fight({u}), 10).first == 0);
        u.hitting = true;
        u.evading = true;
        CHECK(ordersIn(r, fight({u}), 10).first == 0);
        u.evading = false;
        u.polymorphed = true;
        CHECK(ordersIn(r, fight({u}), 10).first == 0);
    }
}

// Engage on arrival (PLAN-THREAT-265 M1a). The tank on "stay" at a stand fights
// nothing (an attack action is a movement action): RFC 19z19 step 25 had it idle
// 21 s with a Trogg on the mage and the rogue 2-4 yd away, Uldaman 19z19 step 85
// three mobs on it with no target for 14 s. So a mob on the tank in melee reach
// (Facts::leadAttacker), or one hitting the healer, sends it at once
// (`attack <guid> <tank>`, which ends the stay), without the guard's wait and
// without using up a pick-up of the fight (kGuardOrdersPerFight).
static Run EngageRun(DungeonPlan& plan)
{
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855})};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f); // on the stand point: the anchor, next step
    f.units = {Unit(31, 4855, 40)};
    Tick(r, f); // pullback 31
    return r;
}

static Facts EngageFight(bool staying = true)
{
    Facts f = Party(0);
    f.anyInCombat = true;
    f.leadIsTank = true;
    f.leadStaying = staying;
    return f;
}

// A living mob on the lead bot in melee reach (what the glue reads for leadHit).
static void OnLead(Facts& f, std::uint64_t guid, std::uint32_t entry, float dist)
{
    f.leadHit = true;
    f.leadAttacker = Hitter(guid, entry, dist);
    f.leadHasAttacker = true;
}

static int OrdersIn(Run& r, const Facts& f, int ticks)
{
    int n = 0;
    for (int i = 0; i < ticks; ++i)
        if (Tick(r, f).compare(0, 7, "attack ") == 0)
            ++n;
    return n;
}

// T1: the first tick sends the tank at the mob on it; no pick-up is used.
static void TestEngageT1()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    CHECK(Tick(r, f) == "attack 60 Tanky");
    CHECK(r.guardOrders == 0 && r.engageGuid == 60);
    CHECK(r.note.find("the tank engages 11517 on arrival") != std::string::npos);
    CHECK(r.guardReturn);
}

// T2: the same mob gets no second order, however long its fight lasts. In a
// live run the order ends the stay at once; the stay stays on only when the bot
// holds the attack (stunned, feared: HoldsAttackUntilControl), and then the
// order must not be repeated every tick.
static void TestEngageT2()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    CHECK(Tick(r, f) == "attack 60 Tanky");
    CHECK(OrdersIn(r, f, 12) == 0);
    CHECK(r.guardOrders == 0);
}

// T3: a polymorphed or evading mob is not engaged.
static void TestEngageT3()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    f.leadAttacker.polymorphed = true;
    CHECK(OrdersIn(r, f, 6) == 0);
    f.leadAttacker.polymorphed = false;
    f.leadAttacker.evading = true;
    CHECK(OrdersIn(r, f, 6) == 0);
    CHECK(r.engageGuid == 0);
    f.leadAttacker.evading = false;
    CHECK(Tick(r, f) == "attack 60 Tanky"); // the same mob once it is neither
}

// T3b: the same for the healer's attacker.
static void TestEngageT3b()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    f.partyAttackers = {Hitter(41, 7320, 12, true)};
    f.partyAttackers[0].polymorphed = true;
    CHECK(OrdersIn(r, f, 3) == 0 && r.engageGuid == 0);
}

// T4: the healer's attacker first, then the mob on the tank.
static void TestEngageT4()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    f.partyAttackers = {Hitter(41, 7320, 20, true), Hitter(42, 7320, 12, true)};
    CHECK(Tick(r, f) == "attack 42 Tanky"); // the nearer of the healer's
    CHECK(r.guardOrders == 0 && r.engageGuid == 42);
// Off its stay now; the guard keeps watching (guardOrders is still 0):
// the other mob on the healer is picked up, a guard order this time.
    f.leadStaying = false;
    f.partyAttackers = {Hitter(41, 7320, 20, true)};
    CHECK(Tick(r, f).empty()); // kGuardRepeatMs after the engage, as after a pick-up
    CHECK(Tick(r, f) == "attack 41 Tanky");
    CHECK(r.guardOrders == 1);
}

// T5: only a DPS's attacker: no engage; the guard's wait rule fires as before.
static void TestEngageT5()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    f.partyAttackers = {Hitter(31, 4855, 20)};
    for (std::uint32_t ms = kTickMs; ms < kGuardWaitMs; ms += kTickMs)
        CHECK(Tick(r, f).empty());
    CHECK(r.engageGuid == 0 && r.guardOrders == 0);
    CHECK(Tick(r, f) == "attack 31 Tanky");
    CHECK(r.guardOrders == 1 && r.engageGuid == 0);
// ... and with a mob on the tank as well, the tank takes that one and
// not the DPS's.
    DungeonPlan plan2;
    Run r2 = EngageRun(plan2);
    Facts g = EngageFight();
    OnLead(g, 60, 11517, 2.0f);
    g.partyAttackers = {Hitter(31, 4855, 20)};
    CHECK(Tick(r2, g) == "attack 60 Tanky");
}

// T6: a tank that is not staying (already engaged) gets no order; nor does a
// lead bot that is no tank, or a dead one.
static void TestEngageT6()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight(false);
    OnLead(f, 60, 11517, 2.0f);
    f.partyAttackers = {Hitter(41, 7320, 20, true)};
    CHECK(OrdersIn(r, f, 8) == 0);
    Facts g = EngageFight();
    OnLead(g, 60, 11517, 2.0f);
    g.leadIsTank = false;
    CHECK(OrdersIn(r, g, 8) == 0);
    Facts h = EngageFight();
    OnLead(h, 60, 11517, 2.0f);
    h.leadAlive = false;
    CHECK(OrdersIn(r, h, 8) == 0);
    CHECK(r.engageGuid == 0);
}

// T7: inside an event (a hold, its ritual, the moves into it): no engage.
// The hold's own engage (HoldEngageTarget) is the event policy's.
static void TestEngageT7()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    CHECK(EngageTarget(r, f) != nullptr);
    r.eventOn = true;
    CHECK(EngageTarget(r, f) == nullptr);
    CHECK(OrdersIn(r, f, 8) == 0);
    CHECK(r.engageGuid == 0);
}

// T8: after the fight the guard return walks the tank back to its stand
// (AiPlayerbot.DungeonRunWalkBackAfterWonFight 1: with the pack cleared the
// default goes on from where the tank stands, TestNoWalkBackAfterAWonFight).
static void TestEngageT8()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    f.walkBackAfterWonFight = true;
    f.leadX = 6; // dragging the pullback in, short of its stand
    f.leadStayKnown = true;
    f.leadStayX = 0;
    OnLead(f, 60, 11517, 2.0f);
    CHECK(Tick(r, f) == "attack 60 Tanky");
    CHECK(r.guardReturn && r.guardX == 0.0f);
    f.leadStaying = false;
    f.leadStayKnown = false;
    f.leadX = 14; // chased it
    CHECK(OrdersIn(r, f, 5) == 0);
    f.anyInCombat = false;
    f.leadHit = false;
    f.leadHasAttacker = false;
    f.units.clear();
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.leadX = 1;
    f.leadStaying = true;
    CHECK(Tick(r, f).empty());
    CHECK(!r.guardReturn);
// The next fight starts over: the same mob guid is engaged again.
    CHECK(r.engageGuid == 0);
}

// T9: the attack is held (the stay stays on) and the nearest mob changes every
// tick: the order is not sent every tick. The fight's first engage is at once,
// a further one only kGuardRepeatMs after the last order, and three at most.
static void TestEngageT9()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<int> at;
    for (int i = 0; i < 16; ++i)
    {
        Facts f = EngageFight();
        OnLead(f, i % 2 == 0 ? 60 : 61, 11517, 2.0f);
        std::string const order = Tick(r, f);
        if (order.compare(0, 7, "attack ") == 0)
            at.push_back(i);
    }
    CHECK(at.size() == kGuardOrdersPerFight);
    CHECK(at[0] == 0);
    for (std::size_t i = 1; i < at.size(); ++i)
        CHECK(at[i] - at[i - 1] >= static_cast<int>(kGuardRepeatMs / kTickMs));
    CHECK(r.guardOrders == 0 && r.engageOrders == kGuardOrdersPerFight);
}

// T10: the end of the fight starts the engage over: the next fight, with the
// tank off its stay, is not watched by the guard on account of the old engage
// (no pick-up of a DPS's attacker, no spacing), and its first engage is at once
// again, the cap spent or not.
static void TestEngageT10()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    for (int i = 0; i < 8; ++i)
    {
        Facts f = EngageFight();
        OnLead(f, 60 + i % 2, 11517, 2.0f);
        Tick(r, f);
    }
    CHECK(r.engageOrders == kGuardOrdersPerFight && r.engageGuid != 0);
    // The fight is over: back to the stand, the step goes on (the key at 1:
    // the pack is gone, and at 0 the plan would end here).
    Facts over = EngageFight(false);
    over.walkBackAfterWonFight = true;
    over.anyInCombat = false;
    over.leadX = 14; // chased it
    CHECK(Tick(r, over) == "move 0.0 0.0 0.0");
    over.leadX = 1;
    over.leadStaying = true;
    Tick(r, over);
    CHECK(r.engageGuid == 0 && r.engageOrders == 0 && !r.guardReturn);
    // A new fight, the tank off its stay with a DPS being hit: its own AI; the
    // guard does not keep watching for the old engage.
    Facts next = EngageFight(false);
    next.partyAttackers = {Hitter(31, 4855, 20)};
    CHECK(OrdersIn(r, next, 8) == 0);
    // A new fight with the tank on its stay: the engage is at once again.
    Run r2 = r;
    Facts stay = EngageFight();
    OnLead(stay, 70, 11517, 2.0f);
    CHECK(Tick(r2, stay) == "attack 70 Tanky");
}

// T11: one clock for both kinds of order. After a pick-up (attack held, the
// stay still on) an engage waits kGuardRepeatMs too, and after an engage a
// pick-up candidate inside kGuardRepeatMs waits (the existing repeat rule).
static void TestEngageT11()
{
    {
        DungeonPlan plan;
        Run r = EngageRun(plan);
        Facts f = EngageFight();
        f.partyAttackers = {Hitter(31, 4855, 20)};
        for (std::uint32_t ms = kTickMs; ms < kGuardWaitMs; ms += kTickMs)
            CHECK(Tick(r, f).empty());
        CHECK(Tick(r, f) == "attack 31 Tanky"); // a pick-up; the attack is held, the stay stays on
        CHECK(r.guardOrders == 1 && r.engageOrders == 0);
        Facts g = EngageFight();
        OnLead(g, 60, 11517, 2.0f); // an engage target appears on the next tick
        CHECK(Tick(r, g).empty());  // 1 s after the pick-up
        CHECK(Tick(r, g) == "attack 60 Tanky"); // kGuardRepeatMs after it, exactly one
        CHECK(r.guardOrders == 1 && r.engageOrders == 1);
        CHECK(OrdersIn(r, g, 6) == 0);
    }
    {
        // The mirror: an engage, then a pick-up candidate (a mob on the healer)
        // inside kGuardRepeatMs; the stay is off (the order took effect).
        DungeonPlan plan;
        Run r = EngageRun(plan);
        Facts f = EngageFight();
        OnLead(f, 60, 11517, 2.0f);
        CHECK(Tick(r, f) == "attack 60 Tanky");
        f.leadStaying = false;
        f.partyAttackers = {Hitter(41, 7320, 12, true)};
        CHECK(Tick(r, f).empty()); // 1 s after the engage
        CHECK(Tick(r, f) == "attack 41 Tanky");
        CHECK(r.guardOrders == 1 && r.engageOrders == 1);
    }
    {
        // ... and with the stay still on (held attack), the same candidate is
        // an engage target: it waits the same 2 s, then one order.
        DungeonPlan plan;
        Run r = EngageRun(plan);
        Facts f = EngageFight();
        OnLead(f, 60, 11517, 2.0f);
        CHECK(Tick(r, f) == "attack 60 Tanky");
        f.partyAttackers = {Hitter(41, 7320, 12, true)};
        CHECK(Tick(r, f).empty());
        CHECK(Tick(r, f) == "attack 41 Tanky");
        CHECK(r.guardOrders == 0 && r.engageOrders == 2);
    }
}

// The guard's budget refill (Deadmines r12-19z29, DEADMINES-PACKS-2026-10-22
// Q1/Q3). Every tick's orders of a fight with the party attackers `att` (the
// tank off its stay unless `staying`), as (tick, order).
static std::string gGuardOrderNote; // the run's note on the tick of GuardOrders' last order
static std::vector<std::pair<int, std::string>> GuardOrders(Run& r, const std::vector<UnitFact>& att, int ticks,
    int start, bool staying = false)
{
    std::vector<std::pair<int, std::string>> out;
    for (int i = 0; i < ticks; ++i)
    {
        Facts f = EngageFight(staying);
        f.partyAttackers = att;
        std::string const o = Tick(r, f);
        if (!o.empty())
        {
            out.emplace_back(start + i, o);
            gGuardOrderNote = r.note;
        }
    }
    return out;
}

static std::string AttackOn(std::uint64_t guid)
{
    return "attack " + std::to_string(guid) + " Tanky";
}

// Mob guids of the trace (0xF130 creature, the entry in the high part) and the
// Remote-Controlled Golem summons (0xF140 pet guids).
constexpr std::uint64_t kDm622a = 0xf13000026e01356cull, kDm622b = 0xf13000026e013569ull;
constexpr std::uint64_t kDm1731 = 0xf1300006c301358full, kDmRcgA = 0xf14000035700001aull,
                        kDmRcgB = 0xf14000035700001bull;

// s118, t 2138-2188: 622 356c on the healer at the walk (engaged on arrival),
// 622 3569 and the golem summons 001a/001b join; the pick-ups are spent in the
// first seconds. At 2161 1731 358f joins and sits on the healer with the
// golems for 26 s, the tank 1 yd away sending nothing; she died at 2188.7.
// Now the joiner restores the budget: an order within kGuardRepeatMs.
static void TestGuardRefillDeadminesS118()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<UnitFact> att = {Hitter(kDm622a, 622, 3, true)};
    auto o = GuardOrders(r, att, 1, 2138, true);
    CHECK(o.size() == 1 && o[0].second == AttackOn(kDm622a)); // engage on arrival
    att = {Hitter(kDm622a, 622, 3, true), Hitter(kDm622b, 622, 6, true), Hitter(kDmRcgA, 2520, 4, true),
        Hitter(kDmRcgB, 2520, 5, true)};
    o = GuardOrders(r, att, 22, 2139);
    CHECK(o.size() == 3); // the pick-ups, spent by 2151
    CHECK(r.guardOrders == 3);
    // The old silence: the same units, no order however long.
    CHECK(GuardOrders(r, att, 10, 2161).empty());
    // 1731 358f joins on the healer, 1 yd from the tank.
    att.push_back(Hitter(kDm1731, 1731, 1, true));
    o = GuardOrders(r, att, 2, 2171);
    CHECK(o.size() == 1 && o[0].second == AttackOn(kDm1731));
    CHECK(gGuardOrderNote.find("picks up 1731 off the healer") != std::string::npos &&
        gGuardOrderNote.find("a new unit joined") != std::string::npos);
    // Before the refill the nearest 622 356c took the engage and all 3
    // pick-ups (no per-unit cap without a refill). The refilled budget goes on
    // to the units still on her, never to 356c (4 orders, over the cap of 2
    // after a refill), spaced as before, and then the fight is silent again.
    CHECK(r.guardUnitOrders[kDm622a] == 4 && r.guardRefills == 1);
    o = GuardOrders(r, att, 30, 2173);
    CHECK(o.size() == 2);
    for (std::size_t i = 1; i < o.size(); ++i)
        CHECK(o[i].first - o[i - 1].first >= 2);
    for (auto const& order : o)
        CHECK(order.second != AttackOn(kDm622a));
    CHECK(r.guardUnitOrders[kDm622a] == 4 && r.guardUnitOrders[kDm1731] <= 2);
    CHECK(GuardOrders(r, att, 20, 2203).empty());
}

// s90, t 1707-1777: the pull's 622s, a golem 61962 and the Taskmaster 4417
// spend the budget by 1718; then the Overseer 634 (1724), the Harvester 61963
// (1742) and golem summons 0016/0017 (1743) join one after another. Each
// joiner gets the tank again, within kGuardRepeatMs of its arrival.
static void TestGuardRefillDeadminesS90()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<UnitFact> att = {Hitter(0xf13000026e016a03ull, 622, 12, true),
        Hitter(0xf13000f20a016998ull, 61962, 23, true), Hitter(0xf130001141016a21ull, 4417, 9, true)};
    auto o = GuardOrders(r, att, 1, 1709, true);
    CHECK(o.size() == 1);
    o = GuardOrders(r, att, 14, 1710);
    CHECK(o.size() == 3 && r.guardOrders == 3);
    struct Join
    {
        int t;
        UnitFact u;
    };
    std::vector<Join> const joins = {{1724, Hitter(0xf13000027a016a12ull, 634, 8, true)},
        {1742, Hitter(0xf13000f20b01698cull, 61963, 6, true)},
        {1752, Hitter(0xf140000357000016ull, 2520, 4, true)},
        {1762, Hitter(0xf140000357000017ull, 2520, 3, true)}};
    int t = 1724;
    for (Join const& j : joins)
    {
        // Silent until the joiner: the budget is spent again.
        CHECK(GuardOrders(r, att, j.t - t, t).size() <= 2);
        att.push_back(j.u);
        bool const capped = r.fightOrders >= 12;
        o = GuardOrders(r, att, 2, j.t);
        // Each refill's first order goes to its joiner (on the healer, the
        // nearest); the rest of it to the units still on her. The fight's
        // 12 ends it: here the fourth joiner, after 12.
        if (!capped)
            CHECK(!o.empty() && o[0].second == AttackOn(j.u.guid));
        else
            CHECK(o.empty());
        t = j.t + 2;
    }
    CHECK(r.fightOrders == 12);
    CHECK(GuardOrders(r, att, 20, t).empty());
}

// s125, t 2556-2619: the boss camp's 1731s and 622s spend the budget; the
// golem summon 0021 lands at 2572 (the refused pick-up at 2576.1 was its
// order) and Gilnid 1763 joins at 2595: an order at each.
static void TestGuardRefillDeadminesS125()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<UnitFact> att = {Hitter(0xf1300006c3013564ull, 1731, 2, true),
        Hitter(0xf1300006c3013561ull, 1731, 9, true), Hitter(0xf13000026e01355cull, 622, 18, true),
        Hitter(0xf1300006c301355dull, 1731, 10, true)};
    CHECK(GuardOrders(r, att, 1, 2563, true).size() == 1);
    CHECK(GuardOrders(r, att, 9, 2564).size() == 3);
    att.push_back(Hitter(0xf14000035f000021ull, 2520, 5, true));
    auto o = GuardOrders(r, att, 2, 2573);
    CHECK(!o.empty() && o[0].second == AttackOn(0xf14000035f000021ull));
    GuardOrders(r, att, 20, 2575);
    att.push_back(Hitter(0xf1300006e3013566ull, 1763, 4, true)); // Gilnid on the healer
    o = GuardOrders(r, att, 2, 2595);
    CHECK(!o.empty() && o[0].second == AttackOn(0xf1300006e3013566ull));
}

// The bounds of the refill: a new unit every tick for 2 minutes (a stream of
// summons) never gives more than 12 orders a fight, never
// two within kGuardRepeatMs, and after a refill never a third at one unit. A joiner while the budget is not spent does not refill it (TestEngageT9
// is the alternating pair).
static void TestGuardRefillIsBounded()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<UnitFact> att;
    std::vector<int> at;
    for (int i = 0; i < 120; ++i)
    {
        att.push_back(Hitter(1000 + i, 2520, 1.0f + (i % 9), true));
        Facts f = EngageFight(i == 0);
        f.partyAttackers = att;
        auto const before = r.guardUnitOrders;
        bool const refilledBefore = r.guardRefills > 0;
        std::string const o = Tick(r, f);
        if (o.compare(0, 7, "attack ") == 0)
        {
            at.push_back(i);
            // Once a refill happened (on this tick or before), an order goes
            // only to a unit with fewer than 2 orders this fight.
            if (refilledBefore || r.guardRefills > 0)
            {
                std::uint64_t const guid = std::strtoull(o.c_str() + 7, nullptr, 10);
                auto const it = before.find(guid);
                CHECK(it == before.end() || it->second < 2);
            }
        }
    }
    CHECK(at.size() == 12 && r.fightOrders == 12 && r.guardRefills >= 1);
    for (std::size_t i = 1; i < at.size(); ++i)
        CHECK(at[i] - at[i - 1] >= 2);
    // The next fight starts over.
    Facts over = EngageFight(true);
    over.anyInCombat = false;
    Tick(r, over);
    CHECK(r.fightOrders == 0 && r.guardSeen.empty() && r.guardUnitOrders.empty());

    // A joiner while pick-ups are left: no refill (the budget serves it).
    DungeonPlan plan2;
    Run b = EngageRun(plan2);
    std::vector<UnitFact> two = {Hitter(1, 622, 3, true)};
    CHECK(GuardOrders(b, two, 1, 0, true).size() == 1);
    CHECK(GuardOrders(b, two, 2, 1).size() == 1 && b.guardOrders == 1); // a pick-up of the same unit
    two.push_back(Hitter(2, 622, 2, true));
    GuardOrders(b, two, 1, 3);
    CHECK(b.guardOrders >= 1 && !b.guardRefilled);
}

// A refill keeps the spacing and the watch. (a) The engage budget spent on
// the alternating pair of TestEngageT9 (the attack held, the stay still on),
// a third unit joins 1 s after the last order: it is engaged kGuardRepeatMs
// after that order, not at once. (b) Only pick-ups (mobs on a DPS, no engage,
// no focus), the tank off its stay: after the refill the budgets read 0, and
// the guard still watches (fightOrders), so the joiner is picked up.
static void TestGuardRefillKeepsSpacingAndWatch()
{
    {
        DungeonPlan plan;
        Run r = EngageRun(plan);
        std::vector<int> at;
        int i = 0;
        for (; i < 16 && r.engageOrders < 3; ++i)
        {
            Facts f = EngageFight();
            OnLead(f, i % 2 == 0 ? 60 : 61, 11517, 2.0f);
            if (Tick(r, f).compare(0, 7, "attack ") == 0)
                at.push_back(i);
        }
        CHECK(at.size() == 3 && at.back() == i - 1);
        Facts f = EngageFight();
        OnLead(f, i % 2 == 0 ? 60 : 61, 11517, 2.0f);
        f.partyAttackers = {Hitter(62, 2520, 3, true)}; // a summon on the healer, 1 s after the last order
        CHECK(Tick(r, f).empty());
        CHECK(Tick(r, f) == "attack 62 Tanky"); // kGuardRepeatMs after the last order
    }
    {
        DungeonPlan plan;
        Run r = EngageRun(plan);
        std::vector<UnitFact> att = {Hitter(31, 4855, 20), Hitter(32, 4855, 22)};
        std::size_t orders = GuardOrders(r, att, 4, 0, true).size();
        orders += GuardOrders(r, att, 20, 10).size();
        CHECK(orders == 3 && r.engageGuid == 0 && r.focusGuid == 0);
        CHECK(r.guardOrders == 3);
        att.push_back(Hitter(33, 2520, 5));
        auto const o = GuardOrders(r, att, 2, 30);
        CHECK(o.size() == 1 && o[0].second == "attack 33 Tanky");
    }
}

// R1: Ragefire Chasm 19z19, Oggleflint pull (step 25), t=862-875. Oggleflint is
// on Tanky from 862 while he stands on "stay" with no target; a Ragefire Trogg
// (be05) hits the mage Gera 4 yd away and later the rogue. The old guard sent
// nothing until 885 (a mob on the healer, and the tank held one in reach, so the
// DPS's Trogg was left alone). Now the tank is sent at Oggleflint on the first
// tick, and it keeps its mob: the Trogg on the mage draws no second order.
static void TestEngageReplayRfcOggleflint()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 161, 11517, 1.5f);                  // Oggleflint on Tanky
    f.partyAttackers = {Hitter(48645, 11318, 4.0f)}; // the Trogg (be05) on Gera
    CHECK(Tick(r, f) == "attack 161 Tanky");
    CHECK(r.guardOrders == 0);
    f.leadStaying = false; // the order ended the stay; 863-875 go on like that
    for (int i = 0; i < 13; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.guardOrders == 0 && r.state == State::Running);
}

// R2: Uldaman 19z19, step 85 (the firing stand for 4853), t=2613-2632. Three
// mobs are on Cedrias (staying, no target from 2616 to 2630); Katrien's Rain of
// Fire wakes more. The tank is sent at the nearest mob on it on the first tick
// (old: no order until 2632, for the healer), and from 2632 the guard still
// picks the healer's attacker up (a pick-up, not an engage, off the stay).
static void TestEngageReplayUldamanStep85()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 8201, 4853, 2.4f); // the nearest of the three; the glue reports one
    f.partyAttackers = {Hitter(8101, 4855, 9.0f), Hitter(8103, 4855, 11.0f)}; // on Katrien and the hunter
    CHECK(Tick(r, f) == "attack 8201 Tanky");
    CHECK(r.guardOrders == 0 && r.engageGuid == 8201);
    f.leadStaying = false;
    for (int i = 0; i < 18; ++i) // 2614-2631
        CHECK(Tick(r, f).empty());
    f.partyAttackers.push_back(Hitter(8204, 4853, 16.0f, true)); // 2632: 4853 on the priest
    CHECK(Tick(r, f) == "attack 8204 Tanky");
    CHECK(r.guardOrders == 1);
}

// Uldaman run 3 (19z12, 10:45-10:54): the priest and the mage revived at the
// entrance and walked ~270 yd back through the Shrike Bats (4 extra deaths);
// the regroup paused at 60 s with a bot 273.7 yd out. A revived bot far from
// the lead bot is summoned to it on its first tick alive and out of combat,
// before any gather walks it, once per revive.
static Run DeathsInAWalk(const DungeonPlan& plan, Facts& f, std::vector<std::string> dead)
{
    Run r = Start(&plan);
    f = Party(0);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.dead = static_cast<std::uint32_t>(dead.size());
    f.alive = 5 - f.dead;
    f.deadNames = dead;
    f.members = {Mem("Tanky", 0)};
    CHECK(Tick(r, f).empty()); // the wait rule starts the regroup
    CHECK(r.regroup);
    CHECK(Tick(r, f) == "revive party");
    return r;
}

static void TestRevivedBotSummonedBeforeWalking()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator", "Tobbert"});
    // The priest is up at the entrance, the mage still a ghost: she is summoned now.
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tobbert"};
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    CHECK(Tick(r, f) == "summon Quelinator");
    CHECK(r.note.find("summoned to it instead of walking back") != std::string::npos);
    // The mage up too: summoned on his first tick alive.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270), Mem("Tobbert", 268)};
    CHECK(Tick(r, f) == "summon Tobbert");
    // No gather walks them while their summons are on the way.
    for (int i = 0; i < 3; ++i)
        CHECK(Tick(r, f).empty());
    // Both at the lead bot: the regroup ends, no walk back through the dungeon.
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 3), Mem("Tobbert", 4)};
    f.maxMemberYd = 4;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.regroup && r.waitFor.empty() && r.reviveSummons.empty());
    // A bot revived near the lead bot (a resurrection in the party) needs none.
    Run n = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 30)};
    f.maxMemberYd = 30;
    CHECK(Tick(n, f) == "move 0.0 0.0 0.0"); // the gather, no summon
}

static void TestNoReviveSummonInCombat()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 200)};
    f.maxMemberYd = 200;
    // In combat on the way (the bats): never summoned (`cmd summon` refuses it).
    f.members[1].inCombat = true;
    f.anyInCombat = true;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    // A member flagged in combat while the party reads out of combat: not either.
    f.anyInCombat = false;
    for (int i = 0; i < 3; ++i)
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    // With attackers it runs back to the party (RunBack, #1054), no summon.
    f.members[1].inCombat = false;
    f.members[1].attackers = 1;
    f.members[1].yd = 100;
    f.maxMemberYd = 100;
    CHECK(Tick(r, f).find("summon") == std::string::npos);
    CHECK(!r.runBack.empty());
    // Its attackers gone, the run back goes on until it ends (here: it stalls);
    // then, out of combat and far, it is summoned.
    f.members[1].attackers = 0;
    std::string order;
    for (std::uint32_t ms = 0; ms < kWalkRetryMs + 3000 && order.empty(); ms += kTickMs)
    {
        bool const running = !r.runBack.empty();
        order = Tick(r, f);
        CHECK(!running || order.empty() || !r.runBack.empty() || order == "summon Quelinator");
    }
    CHECK(order == "summon Quelinator" && r.runBack.empty());
}

static void TestOneReviveSummonPerRevive()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    int summons = 0;
    for (int i = 0; i < 30; ++i)
    {
        f.members[1].x += (i % 2 ? 3.0f : -3.0f); // walking: no stand-still summon either
        if (Tick(r, f) == "summon Quelinator")
            ++summons;
    }
    CHECK(summons == 1);
    // She dies again on the way and is revived: her new revive gets its summon.
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Quelinator"};
    f.members = {Mem("Tanky", 0)};
    f.maxMemberYd = 0;
    std::string order;
    for (int i = 0; i < 3 && order != "revive party"; ++i)
        order = Tick(r, f);
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    order.clear();
    for (int i = 0; i < 3 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Quelinator");
}

static void TestFailedReviveSummonWalks()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    CHECK(Tick(r, f) == "summon Quelinator");
    // Refused (in combat by the time it arrived): she walks with the gather.
    OrderRefused(r, "summon Quelinator", "unavailable: That bot is dead, in combat, on a taxi or teleporting.");
    CHECK(r.state == State::Running && r.reviveSummons.empty() && r.note.find("it walks back") != std::string::npos);
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    // Acknowledged but she never came: after kReviveSummonWaitMs she walks.
    Run t = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    CHECK(Tick(t, f) == "summon Quelinator");
    std::string order;
    std::uint32_t ms = 0;
    for (; ms <= kReviveSummonWaitMs + kTickMs && order.empty(); ms += kTickMs)
        order = Tick(t, f);
    CHECK(order == "move 0.0 0.0 0.0" && ms >= kReviveSummonWaitMs && t.reviveSummons.empty());
}

static void TestRegroupGatherCapScales()
{
    CHECK(RegroupGatherCap(Party(0)) == kRegroupGatherMs);
    Facts far = Party(0);
    far.maxMemberYd = 273.7f;
    CHECK(RegroupGatherCap(far) > kRegroupGatherMs && RegroupGatherCap(far) == 91233u);
    far.maxMemberYd = 2000.0f;
    CHECK(RegroupGatherCap(far) == kRegroupGatherMaxMs);
    // A walker 273.7 yd out (its summon failed) is not paused on at 60 s.
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 273.7f)};
    f.maxMemberYd = 273.7f;
    CHECK(Tick(r, f) == "summon Quelinator");
    OrderRefused(r, "summon Quelinator", "unavailable: in combat");
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::uint32_t ms = 0;
    std::vector<std::string> orders;
    for (; r.state == State::Running && ms < 4 * kRegroupGatherMaxMs; ms += kTickMs)
    {
        f.members[1].x += (ms / kTickMs % 2 ? 3.0f : -3.0f);
        std::string const got = Tick(r, f);
        if (!got.empty())
            orders.push_back(got);
    }
    CHECK(r.state == State::Paused && ms > kRegroupGatherMs + 20000 && r.note.find("within 91 s") != std::string::npos);
    // Before the pause, the straggler ladder: its walk to the lead by path at 45 s, its summon 45 s later.
    CHECK(orders.size() == 2 && orders[0] == "walk Quelinator 0.0 0.0 0.0" && orders[1] == "summon Quelinator");
    // A bot whose summon is on its way does not count against the ceiling.
    Run p = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 50)};
    f.maxMemberYd = 50;
    p.regroupGatherSent = true;
    p.regroupMs = kRegroupGatherMs + 1000;
    CHECK(Tick(p, f) == "summon Quelinator");
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(p, f).empty());
    CHECK(p.state == State::Running);
}

static void TestEmptyPlan()
{
    DungeonPlan plan;
    Run r = Start(&plan);
    CHECK(r.state == State::Done);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
}

// (a) Uldaman 19z11 01:59-02:04: the revived priest walked part of the way
// (more than kStragglerGainYd since the gather began), then stood at the
// scorpid pit's navmesh hole (-163.5 298.7). The regroup's ceiling skipped
// her and paused. A bot that stands still now is summoned, once per regroup;
// then one more gather window, then the pause.
static void TestStuckStragglerThatMovedIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.maxMemberYd = 110;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 110)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the regroup's gather: first sample at 110 yd
    // She walks 30 yd, then stands at the hole, 80 yd out.
    f.members = {Mem("Tanky", 0), Mem("Fyran", 80)};
    f.maxMemberYd = 80;
    std::string order;
    for (std::uint32_t i = 0; i < kRegroupGatherMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Fyran");
    CHECK(r.state == State::Running && r.regroup && r.note.find("has not moved") != std::string::npos);
    // Bounded: one summon per bot per regroup by the ceiling; then the straggler ladder's walk and summon,
    // and the pause.
    std::string orders;
    for (std::uint32_t i = 0; i < 300 && r.state == State::Running; ++i)
        orders += Tick(r, f) + "|";
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
    CHECK(orders.find("walk Fyran 0.0 0.0 0.0|") != std::string::npos &&
        orders.find("walk Fyran 0.0 0.0 0.0|") < orders.find("summon Fyran|"));
    // A bot still on its way (moving every tick) is not summoned.
    Run w = Start(&plan);
    BeginRegroup(w);
    Facts g = Party(0);
    g.maxMemberYd = 110;
    g.members = {Mem("Tanky", 0), Mem("Walky", 110)};
    Tick(w, g);
    g.members[1].yd = 80;
    for (std::uint32_t i = 0; i < kRegroupGatherMs / kTickMs + 2 && w.state == State::Running; ++i)
    {
        g.members[1].x += (i % 2 ? 3.0f : -3.0f);
        CHECK(Tick(w, g).find("summon") == std::string::npos);
    }
    // ... until the straggler ladder's rungs (walk at 45 s out of the gather, summon 45 s on); then the pause.
    for (std::uint32_t i = 0; i < 300 && w.state == State::Running; ++i)
    {
        g.members[1].x += (i % 2 ? 3.0f : -3.0f);
        Tick(w, g);
    }
    CHECK(w.state == State::Paused && w.straggle.count("Walky") && w.straggle.at("Walky").rung == 2);
}

// Zul'Farrak r3-19z20 (PLAN-ZF-ZUMRAH-WALKBACK B-FIX-1/2): after the wipe the
// walk back pulled a Shadowhunter 7246 that stood on a ~3 x 3 yd navmesh island
// at 1588-1591 1044-1047; the tank chased it onto the island and from there
// every walk-back point was refused (no path) or an approach hop of zero length:
// 23 points passed in 13 minutes, 8 hops at the step, a pause, and a resume that
// did the same again. The slice: steps 63-67 of the plan's route and the
// Zum'rah step after them.
static DungeonPlan ZfIslandPlan()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(1903, {7271});
    pull.y = 1014;
    pull.z = 11.8f;
    plan.steps = {Step(PlanStepKind::Move, 1603, 1043, 9.8f), Step(PlanStepKind::Move, 1584, 1050, 9.0f),
        Step(PlanStepKind::Move, 1588, 1080, 9.1f), Step(PlanStepKind::Move, 1584, 1088, 9.1f),
        Step(PlanStepKind::Move, 1633.6f, 1130.7f, 9.9f), pull};
    return plan;
}

// The lead bot on the island: the glue's path toward the point is partial and
// ends 1.1 yd from it (its approach hop with it).
static Facts OnIsland()
{
    Facts f = Party(0);
    f.leadX = 1589.6f;
    f.leadY = 1046.9f;
    f.leadZ = 8.9f;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 1590.7f;
    f.approachY = 1046.7f;
    f.approachZ = 8.9f;
    f.leadIsland = true;
    f.goodSpotCut = true; // the second sign: the main mesh a few yards off cannot be reached
    return f;
}

static void TestLeadOnIslandReading()
{
    CHECK(LeadOnIsland(false, true, 1.1f, 315.1f));             // partial, ends at the lead bot
    CHECK(LeadOnIsland(false, true, 3.05f, 315.1f));            // ... or across the island (B5: step 65, 1591.2 1044.3)
    CHECK(LeadOnIsland(false, true, kGoodPathMinYd, 315.1f));
    CHECK(!LeadOnIsland(false, true, kGoodPathMinYd + 0.1f, 315.1f)); // partial and gets somewhere: the length cap
    CHECK(!LeadOnIsland(false, false, 1.1f, 315.1f));           // complete
    CHECK(!LeadOnIsland(false, true, -1.0f, 315.1f));           // not measured
    CHECK(!LeadOnIsland(false, true, 1.1f, kArriveYd));         // at the point already
    CHECK(!LeadOnIsland(false, true, 6.0f, 9.0f));              // the path ends at the point: not short of it
    // No path (r9-19z26 Uldaman: a partial path inside the smooth-path slop comes back NOPATH, its end the
    // point itself, 59.4 yd off for step 127 and 9.9 yd for step 124): one reading, however far its end.
    CHECK(LeadOnIsland(true, false, 59.4f, 59.4f));
    CHECK(LeadOnIsland(true, false, 9.9f, 9.9f));
    CHECK(LeadOnIsland(true, false, -1.0f, kArriveYd + 0.1f));
    CHECK(!LeadOnIsland(true, false, kArriveYd, kArriveYd));     // at the point already
    CHECK(LeadPathGood(false, false, 0.5f));
    CHECK(LeadPathGood(false, true, kGoodPathMinYd + 0.1f));
    CHECK(!LeadPathGood(false, true, kGoodPathMinYd));
    CHECK(!LeadPathGood(false, true, 3.05f));            // the 3.05 yd path across the island is no good path
    CHECK(!LeadPathGood(true, false, 50.0f));
    CHECK(kGoodPathMinYd > kIslandSizeYd + kArriveYd);
    // The ring search: the path must lead on from the spot AND from the lead bot
    // (a spot off the mesh snaps onto the island: its path ends at the lead bot).
    CHECK(RingSpotLeadsOn(false, true, kGoodPathMinYd + 1, kGoodPathMinYd + 1));
    CHECK(!RingSpotLeadsOn(false, true, kGoodPathMinYd + 1, 2.0f));
    CHECK(!RingSpotLeadsOn(false, true, 2.0f, kGoodPathMinYd + 1));
    CHECK(!RingSpotLeadsOn(true, false, 50.0f, 50.0f));
    // A complete path cannot start from a spot that snapped onto the island:
    // accepted, its end 5 yd away (the point is near) or not.
    CHECK(RingSpotLeadsOn(false, false, 5.0f, 5.0f));
    // The last good spot is probed for the second sign only off the lead bot, near, on its level.
    CHECK(GoodSpotProbeWanted(6.0f, 0.0f) && GoodSpotProbeWanted(kGoodSpotProbeYd, -kEscapeMaxVertical));
    CHECK(!GoodSpotProbeWanted(kArriveYd, 0.0f) && !GoodSpotProbeWanted(kGoodSpotProbeYd + 1, 0.0f) &&
        !GoodSpotProbeWanted(6.0f, kEscapeMaxVertical + 1));
    // The last good spot as the hop's end: not on the lead bot, within reach.
    CHECK(GoodSpotFits(1589.6f, 1046.9f, 8.9f, 1584.8f, 1040.2f, 8.9f));
    CHECK(!GoodSpotFits(1589.6f, 1046.9f, 8.9f, 1590.6f, 1046.9f, 8.9f)); // 1 yd: the island itself
    CHECK(!GoodSpotFits(0, 0, 0, kEscapeMaxYd + 0.5f, 0, 0));
    CHECK(!GoodSpotFits(0, 0, 0, 5, 0, kEscapeMaxVertical + 0.5f));
}

static void TestWalkBackEscapesNavmeshIsland()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    // On the main mesh (where the forward run's tank stood): the hop toward
    // step 5's point is a real one, and this spot is the last good one.
    Facts f = Party(0);
    f.leadX = 1584.8f;
    f.leadY = 1040.2f;
    f.leadZ = 8.9f;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 1600;
    f.approachY = 1080;
    f.approachZ = 9;
    f.leadPathGood = true;
    CHECK(Tick(r, f) == "move 1600.0 1080.0 9.0");
    CHECK(r.goodLeadKnown && r.goodLeadX == 1584.8f && r.goodLeadY == 1040.2f && r.goodLeadZ == 8.9f);
    // A fight on the way took the tank onto the island.
    f.anyInCombat = true;
    Tick(r, f);
    Facts g = OnIsland();
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 to 1584.8 1040.2 8.9 toward 1633.6 1130.7 9.9");
    CHECK(r.note.find("navmesh island at 1589.6 1046.9 8.9") != std::string::npos);
    CHECK(r.note.find("(1 of 2)") != std::string::npos && r.note.find("on to the next") == std::string::npos);
    CHECK(r.state == State::Running && r.walkBackIdx == 4 && r.approachHops == 1); // no hop counted for it
    // The hop is under way: nothing else goes out.
    g.leadMoving = true;
    CHECK(Tick(r, g).empty());
    CHECK(r.note.find("hops off the navmesh island") != std::string::npos);
    // At the good spot, standing: the hop is over; the walk back goes on.
    f.anyInCombat = false;
    CHECK(Tick(r, f).empty());
    CHECK(!r.escapeOn && r.islandEscapes == 0);
    CHECK(Tick(r, f) == "move 1600.0 1080.0 9.0");
    CHECK(r.note == "walk back: approaching the point of step 5" && r.walkBackIdx == 4);
}

static void TestIslandPausesAfterTwoEscapes()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    // A good spot 19.6 yd away is not hopped to straight: the glue looks around the lead bot.
    r.goodLeadKnown = true;
    r.goodLeadX = 1570;
    r.goodLeadY = 1046.9f;
    r.goodLeadZ = 8.9f;
    Facts g = OnIsland();
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
    CHECK(Tick(r, g).empty());
    CHECK(Tick(r, g).empty()); // the lead bot stands still: the hop is over
    // Still on the island: the second hop.
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
    CHECK(r.note.find("(2 of 2)") != std::string::npos);
    CHECK(Tick(r, g).empty());
    CHECK(Tick(r, g).empty());
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("the lead bot stands on a navmesh island at 1589.6 1046.9 8.9: no path toward the point of step 5") == 0);
    CHECK(r.walkBackIdx == 4); // no point passed
}

// Zul'Farrak r6-19z24 (PLAN-ZF-ISLAND-R6): going FORWARD, step 64's hop pull of
// the Shadowhunter 7246 at 1591.0 1047.7 left the tank on the island
// (1591.2 1044.3); 'back to the stand point refused (unreachable)', then
// `cmd move` to step 64's point (6-9 yd away, in sight) refused = pause 1, and
// step 65's the same = pause 2. The island escape covered only the walk back
// and the approach hop of a point out of sight.
static void TestForwardMoveEscapesNavmeshIsland()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1); // step 64: move 1584 1050 9.0, in sight
    // At the stand of the pull before it (main mesh): the path to the point is good.
    Facts f = Party(0);
    f.leadX = 1583.7f;
    f.leadY = 1040.3f;
    f.leadZ = 8.9f;
    f.leadPathGood = true;
    f.anyInCombat = true; // the hop pull's fight
    Tick(r, f);
    CHECK(r.goodLeadKnown && r.goodLeadX == 1583.7f && r.goodLeadY == 1040.3f);
    // The fight took the tank onto the island; its walk back to the stand was refused.
    r.guardReturn = true;
    r.guardX = 1583.7f;
    r.guardY = 1040.3f;
    r.guardZ = 8.9f;
    OrderRefused(r, MoveOrder(1583.7f, 1040.3f, 8.9f, false),
        "unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it).");
    CHECK(r.state == State::Running && r.note.find("back to the stand point refused") == 0);
    Facts g = Party(0);
    g.leadX = 1591.2f;
    g.leadY = 1044.3f;
    g.leadZ = 8.9f;
    g.leadIsland = true;  // the path toward the step's point ends on the island
    g.goodSpotCut = true; // ... and so does the one toward the last good spot (the second sign)
    CHECK(Tick(r, g) == "escape from 1591.2 1044.3 8.9 to 1583.7 1040.3 8.9 toward 1584.0 1050.0 9.0");
    CHECK(r.note.find("navmesh island at 1591.2 1044.3 8.9 (no path toward the point of step 2)") != std::string::npos);
    CHECK(r.state == State::Running && r.step == 1);
    g.leadMoving = true;
    CHECK(Tick(r, g).empty()); // the hop under way
    // At the good spot, standing: the hop is over; the step's move goes out.
    f.anyInCombat = false;
    CHECK(Tick(r, f).empty());
    CHECK(!r.escapeOn && r.islandEscapes == 0);
    CHECK(Tick(r, f) == "move 1584.0 1050.0 9.0");
}

// Uldaman r16-19z34 slot 5, step 39 (UNREACHABLE-ULDAMAN-S39-2026-10-22): the
// guard return walked a partial path to its end and left the tank on the dead-end
// tip of the plateau's ramp at -159.7 287.2 -50.2, 2.4 yd above the floor and not
// linked to it. Its walk back to the stand point (the step's own point, the stay
// anchor) was refused unreachable ('going on from here'), then the step's own move
// to the same point was refused with no ring point either: a pause, and a resume
// paused again. The one reading said island; no second sign came, so the island
// escape never ran. The slice: step 38's stand, step 39's point, a point after it.
static DungeonPlan UldamanS39Plan()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, -174.5f, 308.5f, -53.4f), Step(PlanStepKind::Move, -163.5f, 298.7f, -52.8f),
        Step(PlanStepKind::Move, -150.0f, 280.0f, -46.0f)};
    return plan;
}

// The tank on the ramp's tip: the path toward step 39's point is partial and ends
// 0.03 yd from it, 12.4 yd short of the point (one sign); no fresh good spot.
static Facts OnRampTip()
{
    Facts g = Party(0);
    g.leadX = -159.7f;
    g.leadY = 287.2f;
    g.leadZ = -50.2f;
    g.leadIsland = LeadOnIsland(false, true, 0.03f, 12.4f);
    return g;
}

// CommanderMove's answer for the r16 refusals: the path was incomplete and ended 0.03 yd from the lead bot
// (RemoteCommander::NoProgressFromLead), so the glue marks it (kLeadNoProgressMark).
static const std::string kNoWalkablePath =
    std::string("unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it). ") +
    kLeadNoProgressMark + ".";
// The same code without that evidence: a NOPATH (no polygon at the point), or an incomplete path that made real
// progress toward a point behind a closed door or on a patch of mesh it does not reach.
static const char* const kBareUnreachable =
    "unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it).";
static const char* const kNoRingPointEither = " (no reachable point within 15 yd of it either)";

static void SetGuardReturn(Run& r, float x, float y, float z)
{
    r.guardReturn = true;
    r.guardX = x;
    r.guardY = y;
    r.guardZ = z;
}

static void TestR16StandRefusalHopsOffIsland()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    CHECK(g.leadIsland && !g.goodSpotCut);
    Run r = Start(&plan, 1);
    SetGuardReturn(r, -163.5f, 298.7f, -52.8f); // the stay anchor: step 39's own point
    OrderRefused(r, MoveOrder(-163.5f, 298.7f, -52.8f, false), kNoWalkablePath);
    CHECK(r.state == State::Running && !r.guardReturn && r.islandSuspect && r.islandRefusals == 1);
    CHECK(r.note.find("back to the stand point refused (unreachable") == 0);
    CHECK(r.note.find("navmesh island") != std::string::npos && r.note.find("going on from here") == std::string::npos);
    // The next tick hops the tank off the tip instead of sending the same move.
    CHECK(Tick(r, g) == "escape from -159.7 287.2 -50.2 toward -163.5 298.7 -52.8");
    CHECK(r.escapeFromMove && r.stepEscapes == 1 && r.note.find("(1 of 2)") != std::string::npos);
}

static void TestR16StepMoveRefusalHopsOffIsland()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    Run r = Start(&plan, 1);
    // One sign: the move goes out (r16 at 1134.3 s) and is refused; the glue's detour search found no ring point.
    std::string const move = Tick(r, g);
    CHECK(move == "move -163.5 298.7 -52.8");
    CHECK(r.islandEvidence && !r.escapeOn);
    CHECK(RefusedFromIsland(r, move, kNoWalkablePath));
    CHECK(r.state == State::Running && r.islandSuspect && r.islandRefusals == 1 && r.phase == Phase::Begin);
    CHECK(r.note.find("cmd move -163.5 298.7 -52.8 refused (unreachable") == 0);
    CHECK(r.note.find("navmesh island") != std::string::npos);
    std::string const escape = Tick(r, g);
    CHECK(escape == "escape from -159.7 287.2 -50.2 toward -163.5 298.7 -52.8");
    // The hop finds no spot (the 2.4 yd drop off the tip may fail the line check): the move goes out once more
    // from that spot, and its refusal pauses with the old text: no second hop, no loop.
    OrderRefused(r, escape, "no-spot: no spot within 10 yd in the lead bot's sight has a floor, a clear line and a way on");
    CHECK(r.noSpotKnown && r.state == State::Running);
    CHECK(Tick(r, g) == move);
    CHECK(!RefusedFromIsland(r, move, kNoWalkablePath));
    OrderRefused(r, move, std::string(kNoWalkablePath) + kNoRingPointEither);
    CHECK(r.state == State::Paused);
    CHECK(r.note.find(std::string("cmd move -163.5 298.7 -52.8 refused: ") + kNoWalkablePath + kNoRingPointEither) == 0);
    // The open risk, not proven either way offline: the 3 yd ring spot west of the tip, on the floor at
    // -162.7 287.2 -52.7 (its path to the point is complete), passes the line check only if the floor read at the
    // first sample (1.5 yd out) is within kSegmentRiseYd of the tank's -50.2: the floor's -52.3 there is 2.1 yd down.
    // The one-sided limit: that spot leads on (RingSpotLeadsOn), so the 2.1 yd step down passes (kSegmentDropYd);
    // the last good spot's line (no leads-on read) keeps kSegmentRiseYd both ways.
    std::vector<SegmentSample> drop = EscapeSegmentSamples(-159.7f, 287.2f, -50.2f, -162.7f, 287.2f, -52.7f);
    CHECK(drop.size() == 2);
    for (SegmentSample& sample : drop)
    {
        sample.ground = true;
        sample.groundZ = sample.x > -162.0f ? -52.3f : -52.7f;
    }
    CHECK(RingSpotLeadsOn(false, false, 0.0f, 0.0f)); // its path to the point is complete
    CHECK(CheckEscapeSegment(-50.2f, drop, false, true) == SegmentVerdict::Ok);
    CHECK(CheckEscapeSegment(-50.2f, drop, false, false) == SegmentVerdict::Step);
    drop[0].groundZ = -51.5f; // the ramp's own slope under the first sample
    CHECK(CheckEscapeSegment(-50.2f, drop, false, false) == SegmentVerdict::Ok);
    // The walk back to the stand refused at a no-spot spot: 'going on from here', as before.
    Run n = Start(&plan, 1);
    Tick(n, g);
    n.noSpotKnown = true;
    n.noSpotX = g.leadX;
    n.noSpotY = g.leadY;
    n.noSpotZ = g.leadZ;
    SetGuardReturn(n, -163.5f, 298.7f, -52.8f);
    OrderRefused(n, move, kNoWalkablePath);
    CHECK(!n.islandSuspect && n.note.find("going on from here") != std::string::npos);
}

// After the caps (kIslandEscapes, kStepEscapes, and kStepEscapes refusals taken as
// the second sign in one step) a path refusal pauses with the existing text.
static void TestR16RefusalAfterTheCapsPauses()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    std::string const move = "move -163.5 298.7 -52.8";
    std::string const paused = std::string("cmd ") + move + " refused: " + kNoWalkablePath + kNoRingPointEither;
    for (int cap = 0; cap < 2; ++cap)
    {
        Run c = Start(&plan, 1);
        CHECK(Tick(c, g) == move);
        if (cap == 0)
            c.islandEscapes = kIslandEscapes;
        else
            c.stepEscapes = kStepEscapes;
        CHECK(!RefusedFromIsland(c, move, kNoWalkablePath) && !c.islandSuspect);
        OrderRefused(c, move, std::string(kNoWalkablePath) + kNoRingPointEither);
        CHECK(c.state == State::Paused && c.note.find(paused) == 0);
        // The walk back to the stand: 'going on from here', as before.
        Run s = Start(&plan, 1);
        s.islandEscapes = c.islandEscapes;
        s.stepEscapes = c.stepEscapes;
        SetGuardReturn(s, -163.5f, 298.7f, -52.8f);
        OrderRefused(s, move, kNoWalkablePath);
        CHECK(!s.islandSuspect && s.note.find("going on from here") != std::string::npos);
    }
    // Hops that are refused "wait" (a fight, the lead bot on the move) are no tries and give the escape caps back:
    // the refusals taken as the second sign are capped on their own.
    Run w = Start(&plan, 1);
    for (std::uint32_t i = 0; i < kStepEscapes; ++i)
    {
        CHECK(Tick(w, g) == move);
        CHECK(RefusedFromIsland(w, move, kNoWalkablePath));
        std::string const escape = Tick(w, g);
        CHECK(escape.compare(0, 12, "escape from ") == 0);
        OrderRefused(w, escape, "wait: a party bot is in combat");
        CHECK(w.stepEscapes == 0 && w.islandEscapes == 0);
    }
    CHECK(Tick(w, g) == move);
    CHECK(!RefusedFromIsland(w, move, kNoWalkablePath));
    OrderRefused(w, move, std::string(kNoWalkablePath) + kNoRingPointEither);
    CHECK(w.state == State::Paused && w.note.find(paused) == 0);
    // The next step has fresh refusals.
    Run x = Start(&plan, 1);
    x.islandRefusals = kStepEscapes;
    NextStep(x, "arrived");
    CHECK(x.islandRefusals == 0);
}

// Any other refusal is unchanged: not a path refusal of the lead bot's (lead-unavailable, off-mesh: the point is off
// the walkable ground), a direct step, a walk back, another step kind, or a path just read good.
static void TestNonPathRefusalUnchanged()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    std::string const move = "move -163.5 298.7 -52.8";
    for (const char* answer : {"lead-unavailable: The lead bot is dead, on a taxi or teleporting.",
             "off-mesh: The point is not on walkable ground (the path ends more than 5 yd from it).", "busy: a task"})
    {
        Run r = Start(&plan, 1);
        CHECK(Tick(r, g) == move);
        CHECK(!RefusedFromIsland(r, move, answer) && !r.islandSuspect && r.islandRefusals == 0);
        Run s = Start(&plan, 1);
        SetGuardReturn(s, -163.5f, 298.7f, -52.8f);
        OrderRefused(s, move, answer);
        CHECK(s.state == State::Running && !s.islandSuspect);
        CHECK(s.note == std::string("back to the stand point refused (") + answer + "): going on from here");
        CHECK(Tick(s, g) == move);
    }
    // Another order than the step's own move (a gather, an approach hop) is not the glue's to take.
    Run o = Start(&plan, 1);
    CHECK(!RefusedFromIsland(o, "move -160.0 290.0 -50.0", kNoWalkablePath) && !o.islandSuspect);
    // A direct step: its line crosses a navmesh hole; no island reading.
    DungeonPlan direct = plan;
    direct.steps[1].kind = PlanStepKind::MoveDirect;
    Run d = Start(&direct, 1);
    CHECK(!RefusedFromIsland(d, MoveOrder(-163.5f, 298.7f, -52.8f, true), kNoWalkablePath) && !d.islandSuspect);
    Run dg = Start(&direct, 1);
    SetGuardReturn(dg, -163.5f, 298.7f, -52.8f);
    OrderRefused(dg, move, kNoWalkablePath);
    CHECK(!dg.islandSuspect && dg.note.find("going on from here") != std::string::npos);
    // A walk back: its own rules (PassWalkBackPoint).
    Run b = Start(&plan, 2);
    b.walkBack = true;
    b.walkBackIdx = b.walkBackStart = 1;
    CHECK(!RefusedFromIsland(b, MoveOrder(-150.0f, 280.0f, -46.0f, false), kNoWalkablePath) && !b.islandSuspect);
    // A pull step's guard return: going on from here.
    DungeonPlan pull = plan;
    pull.steps[1] = PullStep(-163.5f, {7022});
    Run p = Start(&pull, 1);
    SetGuardReturn(p, -163.5f, 298.7f, -52.8f);
    OrderRefused(p, move, kNoWalkablePath);
    CHECK(!p.islandSuspect && p.note.find("going on from here") != std::string::npos);
    // The last read path got somewhere: not an island.
    Run k = Start(&plan, 1);
    k.leadPathGoodNow = true;
    CHECK(!RefusedFromIsland(k, move, kNoWalkablePath) && !k.islandSuspect);
    SetGuardReturn(k, -163.5f, 298.7f, -52.8f);
    OrderRefused(k, move, kNoWalkablePath);
    CHECK(!k.islandSuspect && k.note.find("going on from here") != std::string::npos);
    // A paused run takes nothing.
    Run q = Start(&plan, 1);
    q.state = State::Paused;
    CHECK(!RefusedFromIsland(q, move, kNoWalkablePath));
}

// Codex review of 51fc904f: a bare `unreachable` is no source-side evidence. From a lead bot on the main floor the
// same code comes back for a point with no polygon (NOPATH), behind a closed door, or on a patch the path got well
// toward: no hop, the old pause and 'going on from here', also after a resume. The r16 refusal carries the mark.
static void TestBareUnreachableIsNoIslandSign()
{
    // Where the glue tells them apart (CommanderMove marks only the no-progress one).
    RemoteCommander::MoveFacts m;
    m.distanceFromLead = 12.4f;
    m.maxDistance = 100.0f;
    m.fromRunLeader = true;
    m.pathType = RemoteCommander::kPathIncomplete;
    m.endFromLead = 0.03f; // r16: the path ends at the lead bot
    CHECK(RemoteCommander::CheckMove(m) == RemoteCommander::MoveVerdict::NoPath && RemoteCommander::NoProgressFromLead(m));
    m.pathType = RemoteCommander::kPathNoPath; // no polygon at the point
    CHECK(RemoteCommander::CheckMove(m) == RemoteCommander::MoveVerdict::NoPath && !RemoteCommander::NoProgressFromLead(m));
    m.pathType = RemoteCommander::kPathIncomplete; // a closed door: real progress, then the end
    m.endFromLead = 9.0f;
    m.endOffset = 6.0f;
    m.fromRunLeader = false; // (the run leader's own move with progress is accepted: no refusal at all)
    CHECK(RemoteCommander::CheckMove(m) == RemoteCommander::MoveVerdict::NoPath && !RemoteCommander::NoProgressFromLead(m));
    m.endFromLead = -1.0f; // not measured
    CHECK(!RemoteCommander::NoProgressFromLead(m));
    CHECK(LeadNoProgressRefusal(kNoWalkablePath) && !LeadNoProgressRefusal(kBareUnreachable));
    CHECK(!LeadNoProgressRefusal(std::string("off-mesh: x. ") + kLeadNoProgressMark));

    DungeonPlan plan = UldamanS39Plan();
    std::string const move = "move -163.5 298.7 -52.8";
    // The lead bot on the main floor near step 38's stand; its one reading toward the point said nothing.
    Facts f = Party(0);
    f.leadX = -170.0f;
    f.leadY = 305.0f;
    f.leadZ = -53.0f;
    // The step's move refused bare: the old pause; resumed (the walk back to step 38's stand, then the move), the
    // same refusal pauses again, and no tick in between sends a hop.
    Run r = Start(&plan, 1);
    CHECK(Tick(r, f) == move);
    CHECK(!RefusedFromIsland(r, move, kBareUnreachable) && !r.islandSuspect);
    OrderRefused(r, move, std::string(kBareUnreachable) + kNoRingPointEither);
    CHECK(r.state == State::Paused && r.note.find("cmd " + move + " refused: " + kBareUnreachable) == 0);
    CHECK(Resume(r));
    Facts atStand = f;
    atStand.leadX = -174.5f;
    atStand.leadY = 308.5f;
    atStand.leadZ = -53.4f;
    std::string sent;
    for (int i = 0; i < 40 && sent != move && r.state == State::Running; ++i)
    {
        sent = Tick(r, i == 0 ? f : atStand);
        CHECK(sent.find("escape") == std::string::npos);
    }
    CHECK(sent == move && !r.islandSuspect);
    CHECK(!RefusedFromIsland(r, move, kBareUnreachable));
    OrderRefused(r, move, std::string(kBareUnreachable) + kNoRingPointEither);
    CHECK(r.state == State::Paused && r.note.find("cmd " + move + " refused: " + kBareUnreachable) == 0);
    // The walk back to the stand refused bare: 'going on from here', and the next tick sends the move, not a hop.
    Run s = Start(&plan, 1);
    CHECK(Tick(s, f) == move);
    SetGuardReturn(s, -163.5f, 298.7f, -52.8f);
    OrderRefused(s, move, kBareUnreachable);
    CHECK(!s.islandSuspect && s.note.find("going on from here") != std::string::npos);
    CHECK(Tick(s, f) == move);
    // Even on the ramp's tip (one reading said island) a bare `unreachable` adds no sign.
    Run t = Start(&plan, 1);
    CHECK(Tick(t, OnRampTip()) == move);
    CHECK(!RefusedFromIsland(t, move, kBareUnreachable) && !t.islandSuspect);
    OrderRefused(t, move, std::string(kBareUnreachable) + kNoRingPointEither);
    CHECK(t.state == State::Paused);
}

// The glue's order for a refused own-step move (BotCommands.cpp, !accepted): MoveRefusedUnreachable (the
// detour, RefusedFromIsland, the skip), else OrderRefused. `detour` is whether NearestReachablePoint found a
// stand-in (dx dy dz; asked only when MoveFallbackWanted).
static void GlueRefused(Run& r, const std::string& order, const std::string& answer, bool detour, float dx = 0,
    float dy = 0, float dz = 0)
{
    const PlanStep* step = Current(r);
    bool const unreachable = OwnMoveOrder(r, order) && UnreachableAnswer(answer);
    bool const searched = unreachable && MoveFallbackWanted(r);
    bool const found = searched && detour;
    if (!(unreachable && MoveRefusedUnreachable(r, order, answer, found, dx, dy, dz)))
        OrderRefused(r, order, answer + (unreachable ? NoStandInNote(*step, searched) : std::string()));
}

// Razorfen Kraul r22-19z40 step 85: the plan's move to 2180.0 1604.0 81.2 (a navmesh pocket of its own) was
// refused unreachable after the walk around ('walking around: the plan's point is off the walkable way'), the
// run paused, and each resume walked back and paused on the same refusal (3 pauses). For a plain move between
// moves: the nearest reachable point within 25 yd, else the move is skipped with a note, and a second refusal of
// the step skips it. This fixture puts a move after step 85's (the real plan has the pull right after it, which
// pauses: TestR22RealStep85ApproachStandPauses).
static DungeonPlan RfkS85Plan()
{
    DungeonPlan plan;
    // A move after step 85's here: the move right before a pull is not skipped (TestR22MoveBeforeAFightIsNotSkipped).
    plan.steps = {Step(PlanStepKind::Move, 2153.0f, 1589.0f, 80.5f), Step(PlanStepKind::Move, 2180.0f, 1604.0f, 81.2f),
        Step(PlanStepKind::Move, 2185.0f, 1606.0f, 81.2f), PullStep(2183.1f, {4440}), Step(PlanStepKind::Rest)};
    return plan;
}

static Facts AtRfk(float x, float y, float z)
{
    Facts f = Party(0);
    f.leadX = x;
    f.leadY = y;
    f.leadZ = z;
    return f;
}

static void TestR22RefusedMoveWalksToAStandInWithin25Yd()
{
    DungeonPlan plan = RfkS85Plan();
    Facts f = AtRfk(2153.0f, 1589.0f, 80.5f);
    Run r = Start(&plan, 1);
    std::string const move = Tick(r, f);
    CHECK(move == "move 2180.0 1604.0 81.2");
    // A stand-in 20.0 yd off (past the old 15 yd, within 25): the step walks there.
    CHECK(MoveFallbackWanted(r));
    GlueRefused(r, move, kBareUnreachable, true, 2160.0f, 1604.0f, 81.2f);
    CHECK(r.state == State::Running && r.step == 1 && r.detour);
    CHECK(r.note.find("step 2: cmd move to the plan's point 2180.0 1604.0 81.2 refused (unreachable") == 0);
    CHECK(r.note.find("nearest reachable point 2160.0 1604.0 81.2, 20.0 yd from it") != std::string::npos);
    CHECK(Tick(r, f) == "move 2160.0 1604.0 81.2");
    Facts there = AtRfk(2160.0f, 1604.0f, 81.2f);
    Tick(r, there);
    CHECK(r.step == 2 && r.state == State::Running && r.note.find("arrived at the nearest reachable point") == 0);
    // 26 yd off is not a stand-in: the move is skipped instead.
    Run far = Start(&plan, 1);
    CHECK(Tick(far, f) == move);
    GlueRefused(far, move, kBareUnreachable, true, 2154.0f, 1604.0f, 81.2f);
    CHECK(!far.detour && far.step == 2 && far.state == State::Running);
    // A direct step keeps the 15 yd stand-in.
    PlanStep direct = Step(PlanStepKind::MoveDirect, 2180.0f, 1604.0f, 81.2f);
    CHECK(MoveFallbackYd(direct) == kDetourMaxYd && MoveFallbackYd(plan.steps[1]) == kMoveFallbackYd);
}

static void TestR22NoStandInSkipsTheMoveWithoutAPause()
{
    DungeonPlan plan = RfkS85Plan();
    Facts f = AtRfk(2153.0f, 1589.0f, 80.5f);
    Run r = Start(&plan, 1);
    std::string const move = Tick(r, f);
    GlueRefused(r, move, kBareUnreachable, false);
    CHECK(r.state == State::Running && r.step == 2);
    CHECK(r.note.find("step 2 move to 2180.0 1604.0 81.2 skipped: no walkable path; the party goes on to step 3 "
                      "(no reachable point within 25 yd of it: unreachable") == 0);
    // An off-mesh answer is the same.
    Run o = Start(&plan, 1);
    GlueRefused(o, Tick(o, f), "off-mesh: The point is not on the navmesh.", false);
    CHECK(o.state == State::Running && o.step == 2);
    // A refusal for the lead bot's state (not a path verdict) still pauses as before.
    Run b = Start(&plan, 1);
    GlueRefused(b, Tick(b, f), "busy: The lead bot is casting.", false);
    CHECK(b.state == State::Paused && b.step == 1);
}

static void TestR22SecondRefusalOfTheStepSkips()
{
    DungeonPlan plan = RfkS85Plan();
    Facts f = AtRfk(2153.0f, 1589.0f, 80.5f);
    // r22: the stand-in was the plan's point itself (on the ground), so the walk around sent the same order.
    Run r = Start(&plan, 1);
    std::string const move = Tick(r, f);
    GlueRefused(r, move, kBareUnreachable, true, 2180.0f, 1604.0f, 81.2f);
    CHECK(r.detour && r.state == State::Running);
    CHECK(Tick(r, f) == move);
    CHECK(r.note == "walking around: the plan's point is off the walkable way");
    CHECK(!MoveFallbackWanted(r));
    GlueRefused(r, move, kBareUnreachable, true, 2179.0f, 1604.0f, 81.2f);
    CHECK(r.state == State::Running && r.step == 2 && !r.detour);
    CHECK(r.note.find("step 2 move to 2180.0 1604.0 81.2 skipped: no walkable path; the party goes on to step 3 "
                      "(refused again after the walk to the nearest reachable point") == 0);
    // The stand-in's own order refused: the same skip.
    Run d = Start(&plan, 1);
    GlueRefused(d, Tick(d, f), kBareUnreachable, true, 2165.0f, 1600.0f, 81.0f);
    std::string const around = Tick(d, f);
    CHECK(around == "move 2165.0 1600.0 81.0" && OwnMoveOrder(d, around));
    GlueRefused(d, around, kBareUnreachable, false);
    CHECK(d.state == State::Running && d.step == 2);
    // After a step reset (what a resume does: the detour is cleared), the second refusal skips; no new stand-in.
    Run s = Start(&plan, 1);
    GlueRefused(s, Tick(s, f), kBareUnreachable, true, 2165.0f, 1600.0f, 81.0f);
    ResetStep(s);
    CHECK(!s.detour && !MoveFallbackWanted(s));
    CHECK(Tick(s, f) == move);
    GlueRefused(s, move, kBareUnreachable, true, 2165.0f, 1600.0f, 81.0f);
    CHECK(s.state == State::Running && s.step == 2 && !s.detour);
}

// Codex review of 44f083c8: the move right before a pull or a boss (the fight's approach stand) is not skipped:
// the pause as before; a stand-in is still taken. A move before another move is skipped as before.
static void TestR22MoveBeforeAFightIsNotSkipped()
{
    for (PlanStepKind const kind : {PlanStepKind::Pull, PlanStepKind::Boss})
    {
        DungeonPlan plan;
        plan.steps = {Step(PlanStepKind::Move, 40), Step(kind, 60), Step(PlanStepKind::Rest)};
        Run r = Start(&plan, 0);
        GlueRefused(r, "move 40.0 0.0 0.0", kBareUnreachable, false);
        CHECK(r.state == State::Paused && r.step == 0);
        CHECK(r.note.find(" (no reachable point within 25 yd of it either)") != std::string::npos);
        Run d = Start(&plan, 0);
        GlueRefused(d, "move 40.0 0.0 0.0", kBareUnreachable, true, 30.0f, 0.0f, 0.0f);
        CHECK(d.state == State::Running && d.detour && d.step == 0);
        // Its stand-in refused too: the pause, not a skip, and its note does not claim a search that did not run.
        GlueRefused(d, "move 30.0 0.0 0.0", kBareUnreachable, false);
        CHECK(d.state == State::Paused && d.step == 0);
        CHECK(d.note.find("within 25 yd") == std::string::npos &&
            d.note.find("(refused again in this step, after its walk to the nearest reachable point)") != std::string::npos);
        // Opus review of 44f083c8: resumed, the step is a first refusal again: a stand-in from the new spot is taken.
        CHECK(Resume(d));
        CHECK(MoveFallbackWanted(d) && d.moveRefusals == 0);
        GlueRefused(d, "move 40.0 0.0 0.0", kBareUnreachable, true, 35.0f, 0.0f, 0.0f);
        CHECK(d.state == State::Running && d.detour && d.detourX == 35.0f);
    }
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 60), Step(PlanStepKind::Rest)};
    Run m = Start(&plan, 0);
    GlueRefused(m, "move 40.0 0.0 0.0", kBareUnreachable, false);
    CHECK(m.state == State::Running && m.step == 1);
    // Opus re-review of db8689bf: steps that neither move nor fight are looked past (move, rest, pull; move,
    // wait_clear, boss; move, rest, hold): the move is still the fight's approach stand.
    for (PlanStepKind const between : {PlanStepKind::Rest, PlanStepKind::WaitClear})
        for (PlanStepKind const kind : {PlanStepKind::Pull, PlanStepKind::Boss, PlanStepKind::Hold})
        {
            DungeonPlan q;
            q.steps = {Step(PlanStepKind::Move, 40), Step(between, 40), Step(kind, 60), Step(PlanStepKind::Rest)};
            Run a = Start(&q, 0);
            CHECK(NeverSkipMove(a));
            GlueRefused(a, "move 40.0 0.0 0.0", kBareUnreachable, false);
            CHECK(a.state == State::Paused && a.step == 0);
        }
    // The plan's last move (only a rest after it): nothing to set up, skipped.
    DungeonPlan last;
    last.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Rest)};
    Run l = Start(&last, 0);
    CHECK(!NeverSkipMove(l));
    GlueRefused(l, "move 40.0 0.0 0.0", kBareUnreachable, false);
    CHECK(l.state == State::Running && l.step == 1);
    // ... but a move after the rest ends the look: move, rest, move, pull skips the first move.
    DungeonPlan q;
    q.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Rest), Step(PlanStepKind::Move, 50), PullStep(60, {1})};
    Run b = Start(&q, 0);
    CHECK(!NeverSkipMove(b));
    GlueRefused(b, "move 40.0 0.0 0.0", kBareUnreachable, false);
    CHECK(b.state == State::Running && b.step == 1);
}

// The real r22-19z40 shape: step 85's move (2180.0 1604.0 81.2) is pull 42's approach stand, right before the
// pull. Lead decision: such a stand with no path pauses (the plan data moves the stand); what this code removes
// is the loop. Refused, the stand-in refused too: the pause. Resumed, the step searches a stand-in afresh from the
// lead bot's new spot (and may hop it off an island) instead of pausing at once.
static void TestR22RealStep85ApproachStandPauses()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 2153.0f, 1589.0f, 80.5f), Step(PlanStepKind::Move, 2180.0f, 1604.0f, 81.2f),
        PullStep(2183.1f, {4440}), Step(PlanStepKind::Rest)};
    Facts f = AtRfk(2153.0f, 1589.0f, 80.5f);
    Run r = Start(&plan, 1);
    std::string const move = Tick(r, f);
    CHECK(move == "move 2180.0 1604.0 81.2" && NeverSkipMove(r));
    // r22: the stand-in was the plan's point itself; the walk around sent the same order, refused again.
    GlueRefused(r, move, kBareUnreachable, true, 2180.0f, 1604.0f, 81.2f);
    CHECK(r.state == State::Running && r.detour);
    GlueRefused(r, move, kBareUnreachable, true, 2180.0f, 1604.0f, 81.2f);
    CHECK(r.state == State::Paused && r.step == 1);
    CHECK(r.note == std::string("cmd ") + move + " refused: " + kBareUnreachable +
        " (refused again in this step, after its walk to the nearest reachable point)");
    CHECK(Resume(r));
    CHECK(r.moveRefusals == 0 && MoveFallbackWanted(r));
    GlueRefused(r, move, kBareUnreachable, true, 2165.0f, 1600.0f, 81.0f);
    CHECK(r.state == State::Running && r.detour && r.detourX == 2165.0f);
    // The island hop is allowed again after the resume (Uldaman r16's ramp tip, the move before a pull).
    DungeonPlan u = UldamanS39Plan();
    u.steps.insert(u.steps.begin() + 2, PullStep(-150.0f, {4860}));
    Facts g = OnRampTip();
    Run i = Start(&u, 1);
    std::string const um = Tick(i, g);
    CHECK(um == "move -163.5 298.7 -52.8" && NeverSkipMove(i));
    GlueRefused(i, um, kNoWalkablePath, true, -160.0f, 298.7f, -52.8f);
    CHECK(i.detour && !i.islandSuspect);
    GlueRefused(i, um, kNoWalkablePath, false);
    CHECK(i.state == State::Paused && !i.islandSuspect); // the second refusal: no hop, the pause
    CHECK(Resume(i) && i.moveRefusals == 0);
    CHECK(MoveRefusedUnreachable(i, um, kNoWalkablePath, false, 0, 0, 0));
    CHECK(i.state == State::Running && i.islandSuspect && i.note.find("navmesh island") != std::string::npos);
}

// Opus review of 44f083c8: a step the run goes back to (EventFailed, RearmEventOnWipe) starts its refusals and
// the run of skips over: the party comes forward again from elsewhere.
static void TestR22RewindForgetsMoveRefusals()
{
    Run r;
    r.moveRefusedStep = 4;
    r.moveRefusals = 1;
    r.unreachableSkips = {2, 3};
    ForgetMoveRefusals(r);
    CHECK(r.moveRefusedStep == static_cast<std::size_t>(-1) && r.moveRefusals == 0 && r.unreachableSkips.empty());
    // Both rewinds call it (a ritual plan: the ritual at step 1 starts the event, a hold at step 3).
    DungeonPlan plan;
    PlanStep ritual = Step(PlanStepKind::Ritual, 0);
    PlanStep hold = Step(PlanStepKind::Hold, 0);
    hold.flags = ai::kPlanFlagDoneData | ai::kPlanFlagEncounter;
    plan.steps = {Step(PlanStepKind::Move, 10), ritual, Step(PlanStepKind::Move, 20), hold, Step(PlanStepKind::Rest)};
    Run e = Start(&plan, 3);
    e.moveRefusedStep = 2;
    e.moveRefusals = 1;
    e.unreachableSkips = {2};
    EventFailed(e, "test");
    CHECK(e.step == 1 && e.moveRefusals == 0 && e.moveRefusedStep == static_cast<std::size_t>(-1) &&
        e.unreachableSkips.empty());
    Run w = Start(&plan, 3);
    w.moveRefusedStep = 2;
    w.moveRefusals = 1;
    w.unreachableSkips = {2};
    Facts f = Party(0);
    f.eventData = static_cast<long long>(ai::kPlanEncounterFail);
    CHECK(RearmEventOnWipe(w, f));
    CHECK(w.step == 1 && w.moveRefusals == 0 && w.unreachableSkips.empty());
    // A plain move resumed keeps its count (its second refusal skips it: r22).
    DungeonPlan p2;
    p2.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 60), Step(PlanStepKind::Rest)};
    Run m = Start(&p2, 0);
    GlueRefused(m, "move 40.0 0.0 0.0", kBareUnreachable, true, 30.0f, 0.0f, 0.0f);
    Pause(m, "test");
    CHECK(Resume(m) && m.moveRefusals == 1 && !MoveFallbackWanted(m));
}

// Opus review of 44f083c8 (4): a stand-in whose navmesh path from the lead bot is longer than 40 yd is not taken.
static void TestR22StandInPathIsCapped()
{
    CHECK(DetourPointReachable(false, false, 0.0f, 20.0f, 20.0f, 20.0f + kDetourMaxPathYd));
    CHECK(!DetourPointReachable(false, false, 0.0f, 20.0f, 20.0f, 20.0f + kDetourMaxPathYd + 0.1f));
    CHECK(!DetourPointReachable(false, false, 0.0f, 20.0f, 20.0f, 120.0f)); // r22: the walk round the west hall
    // The cap is on the extra way, not the whole path: a move goes out with the lead bot up to 60 yd off.
    // The lead bot 50 yd from the stand-in: a 75 yd path is accepted, a 95 yd one is not.
    CHECK(DetourPointReachable(false, false, 0.0f, 50.0f, 50.0f, 75.0f));
    CHECK(DetourPointReachable(false, false, 0.0f, 50.0f, 50.0f, 50.0f + kDetourMaxPathYd));
    CHECK(!DetourPointReachable(false, false, 0.0f, 50.0f, 50.0f, 95.0f));
}

// Lead decision: three plain moves skipped in a row (no step done in between) pause the run, naming them and
// the lead bot's spot; a step done the normal way starts the count over.
static void TestR22ThreeSkipsInARowPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 80), Step(PlanStepKind::Move, 120),
        Step(PlanStepKind::Move, 160), Step(PlanStepKind::Move, 200), Step(PlanStepKind::Rest)};
    Facts f = Party(0);
    Run r = Start(&plan, 0);
    r.leadLastX = 1.5f;
    r.leadLastY = 2.0f;
    r.leadLastZ = 3.0f;
    GlueRefused(r, "move 40.0 0.0 0.0", kBareUnreachable, false);
    GlueRefused(r, "move 80.0 0.0 0.0", kBareUnreachable, false);
    CHECK(r.state == State::Running && r.step == 2 && r.unreachableSkips.size() == 2);
    GlueRefused(r, "move 120.0 0.0 0.0", kBareUnreachable, false);
    CHECK(r.state == State::Paused && r.step == 0); // back to the first of them (TestNoPathPauseResumeTriesTheSameMovesAgain)
    CHECK(r.note == "three moves in a row had no walkable path: the lead bot may be in the wrong part of the instance "
                    "(steps 1, 2 and 3 skipped; the lead bot at 1.5 2.0 3.0): the run went back to step 1; a resume "
                    "walks the lead bot back to the plan's start, the last point it reached, and tries them again");
    // After the resume the count starts over: the next skip does not pause.
    CHECK(Resume(r));
    CHECK(r.unreachableSkips.empty());
    // A completed step in between resets it: skip, done, skip, skip = no pause.
    Run c = Start(&plan, 0);
    GlueRefused(c, "move 40.0 0.0 0.0", kBareUnreachable, false);
    CHECK(c.step == 1);
    f.leadX = 80;
    for (int i = 0; i < 5 && c.step == 1; ++i)
        Tick(c, f);
    CHECK(c.step == 2 && c.unreachableSkips.empty());
    GlueRefused(c, "move 120.0 0.0 0.0", kBareUnreachable, false);
    GlueRefused(c, "move 160.0 0.0 0.0", kBareUnreachable, false);
    CHECK(c.state == State::Running && c.step == 4 && c.unreachableSkips.size() == 2);
}

// Razorfen Kraul r24 slot 8 steps 331-343: 'three moves in a row had no walkable path'
// paused at 334, and each resume skipped three more from the same spot (337, 340, 343;
// the lead bot never moved). The pause now sends the run back to the first of them; the
// resume walks the lead bot back to the last point it reached and tries them again; the
// same three failing again pause with the route note, still at the first of them.
static void TestNoPathPauseResumeTriesTheSameMovesAgain()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 80), Step(PlanStepKind::Move, 120),
        Step(PlanStepKind::Move, 160), Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 240),
        Step(PlanStepKind::Move, 280), Step(PlanStepKind::Rest)};
    Run r = Start(&plan, 1);
    NoteCheckpoint(r, 0); // step 1 reached
    r.leadLastX = 52.0f;
    auto refuse3 = [&](Run& run, int from)
    {
        for (int i = from; i < from + 3; ++i)
            GlueRefused(run, "move " + Num(40.0f * (i + 1)) + " 0.0 0.0", kBareUnreachable, false);
    };
    refuse3(r, 1);
    CHECK(r.state == State::Paused && r.step == 1 && r.noPathSkips == std::vector<std::size_t>({1, 2, 3}));
    CHECK(r.note.find("three moves in a row had no walkable path: the lead bot may be in the wrong part of the instance "
        "(steps 2, 3 and 4 skipped; the lead bot at 52.0 0.0 0.0): the run went back to step 2; a resume walks the lead "
        "bot back to step 1, the last point it reached, and tries them again") == 0);
    // The resume: the walk back goes to step 1's point, then step 2's move goes out again.
    CHECK(Resume(r));
    Facts f = Party(52);
    std::string const back = Tick(r, f);
    CHECK(r.walkBack && r.walkBackIdx == 0);
    std::string sent = back;
    for (int i = 0; i < 6 && sent != "move 80.0 0.0 0.0"; ++i)
    {
        if (sent == "move 40.0 0.0 0.0")
            f.leadX = 40; // walked back to step 1's point
        sent = Tick(r, f);
    }
    CHECK(sent == "move 80.0 0.0 0.0" && r.step == 1 && !r.walkBack);
    // The same three refused again: the route note, still at step 2, nothing skipped past them.
    r.leadLastX = 40.0f;
    refuse3(r, 1);
    CHECK(r.state == State::Paused && r.step == 1);
    CHECK(r.note.find("the same three moves had no walkable path again (steps 2, 3 and 4; the lead bot at 40.0 0.0 0.0): "
        "the way on from step 1, the last point the lead bot reached, needs a fix in the plan (a ramp or a waypoint); "
        "the run stays at step 2, a resume tries them again from step 1") == 0);
    // And again: never past them.
    CHECK(Resume(r));
    refuse3(r, 1);
    CHECK(r.state == State::Paused && r.step == 1 && r.note.find("the same three moves") == 0);
    // Three others (the party got on): the first pause's note and its rewind, for those.
    Run o = Start(&plan, 1);
    o.noPathSkips = {1, 2, 3};
    o.step = 4;
    refuse3(o, 4);
    CHECK(o.state == State::Paused && o.step == 4 && o.note.find("three moves in a row") == 0);
    CHECK(LastReachedBefore(o, 4) == 3); // no checkpoint known: the last move point before them
    NoteCheckpoint(o, 0); // the checkpoint when known: steps 2-4 were skipped, never reached
    CHECK(LastReachedBefore(o, 4) == 0);
    // The resume's walk back goes to that reached point, not to the never-reached move point 4 before them.
    Run k = Start(&plan, 4);
    NoteCheckpoint(k, 0);
    k.noPathSkips = {4, 5, 6};
    Facts far = Party(400);
    StartWalkBack(k, far);
    CHECK(k.walkBack && k.walkBackIdx == 0);
    Run n = Start(&plan, 4); // no such pause: the last move point before the step, as before
    NoteCheckpoint(n, 0);
    StartWalkBack(n, far);
    CHECK(n.walkBack && n.walkBackIdx == 3);
}

// Sunken Temple r26 slot 6b step 235 (owner decision 2026-10-23): the lead bot sat on an island 13 yd below the
// route (-448.0 103.2 -191.0); each resume: the hop off it found no spot, then 'walk back: the point of step 234
// refused (unreachable)', then the same three moves skipped and the pause. As the last resort the run teleports
// the lead bot to the last point it reached (the route checkpoint), out of combat only, at most once a step
// (counted on arrival), 3 failed tries and 10 waits a step, the spot read afresh on each send; the party left
// on the island is summoned to it after.
static Facts StrandFacts(float x, float y, float z)
{
    Facts f = Party(x);
    f.leadY = y;
    f.leadZ = z;
    f.seen = true;
    MemberFact lead;
    lead.name = "Tanky";
    lead.x = x;
    lead.y = y;
    lead.z = z;
    f.members = {lead};
    return f;
}

static void TestStrandedLeadIsTeleportedToTheLastReachedPoint()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, -460.0f, 50.0f, -172.9f), Step(PlanStepKind::Move, -422.4f, 82.4f, -172.9f),
        Step(PlanStepKind::Move, -422.9f, 104.3f, -172.9f), Step(PlanStepKind::Move, -422.4f, 106.7f, -172.9f),
        Step(PlanStepKind::Move, -420.0f, 120.0f, -172.9f), Step(PlanStepKind::Rest)};
    std::string const back = MoveOrder(-422.4f, 82.4f, -172.9f, false);
    std::string const noSpot = "no-spot: no spot within 10 yd in the lead bot's sight has a floor, a clear line and a way on";
    auto stranded = [&]()
    {
        Run r = Start(&plan, 2);
        NoteCheckpoint(r, 1);
        r.noPathSkips = {2, 3, 4};
        r.walkBack = true;
        r.walkBackIdx = 1;
        r.walkBackStart = 1;
        r.leadLastX = -448.0f;
        r.leadLastY = 103.2f;
        r.leadLastZ = -191.0f;
        OrderRefused(r, "escape from -448.0 103.2 -191.0 toward -422.4 82.4 -172.9", noSpot);
        CHECK(r.noSpotKnown && r.note.find("the straight hop off the navmesh island was not made (no-spot") == 0);
        return r;
    };
    Facts f = StrandFacts(-448.0f, 103.2f, -191.0f);
    Facts atPoint = StrandFacts(-422.4f, 82.4f, -172.9f);
    // The four others still on the island, 30 yd below and away from the point.
    for (const char* name : {"Yssia", "Beardinator", "Wendrick", "Bowjangles"})
    {
        MemberFact m;
        m.name = name;
        m.yd = 30.0f;
        m.dz = -14.0f;
        atPoint.members.push_back(m);
    }
    atPoint.members[2].yd = 3.0f; // Beardinator: under the point, a floor below: summoned too
    atPoint.members[4].inCombat = true; // Bowjangles: in a fight, never summoned

    Run r = stranded();
    OrderRefused(r, back, kBareUnreachable);
    CHECK(r.strandDue && r.strandIdx == 1 && r.walkBackIdx == 1 && r.state == State::Running);
    CHECK(r.note == std::string("walk back: the point of step 2 refused (") + kBareUnreachable + ") where the hop off "
        "the island found no spot: the lead bot is stranded; it is teleported to the last reached point out of combat");
    // In combat: nothing goes out; it waits.
    Facts fight = f;
    fight.anyInCombat = true;
    std::string order;
    CHECK(StrandedTeleportTick(r, fight, kTickMs, order) && order.empty() && r.strandDue);
    CHECK(r.note == "the lead bot is stranded at -448.0 103.2 -191.0 with no way back: the teleport to the last reached "
        "point (step 2) waits for the fight to end");
    fight.anyInCombat = false;
    fight.leadInCombat = true;
    CHECK(StrandedTeleportTick(r, fight, kTickMs, order) && order.empty() && r.strandDue);
    // Out of combat: the teleport, through the walk back's own tick, from where the lead bot stands NOW
    // (it moved 3 yd on the island since the reading).
    Facts moved = StrandFacts(-445.0f, 101.0f, -190.0f);
    order = Tick(r, moved);
    CHECK(order == "rescue from -445.0 101.0 -190.0 to -422.4 82.4 -172.9");
    CHECK(r.note == "the lead bot is stranded at -445.0 101.0 -190.0 with no way back: teleporting it to the last reached "
        "point (step 2)");
    CHECK(!r.strandDue && r.strandOn && !r.noSpotKnown);
    CHECK(r.strandStep == static_cast<std::size_t>(-1)); // not used until it arrived
    // It settles: no other order for kStrandSettleMs.
    for (std::uint32_t ms = kTickMs; ms < kStrandSettleMs; ms += kTickMs)
        CHECK(Tick(r, atPoint).empty());
    // At the point: the step's teleport is used, the note says so.
    order = Tick(r, atPoint);
    CHECK(!r.strandOn && r.strandStep == 2 && r.strandSummon);
    CHECK(r.note == "the lead bot was stranded at -445.0 101.0 -190.0 with no way back: teleported to the last reached "
        "point (step 2)");
    // The party left on the island is summoned to it, one a tick; a bot in a fight is waited for, not dropped.
    Run fightLeft = r; // the same moment, for the 120 s bound below
    std::vector<std::string> summons;
    for (int i = 0; i < 8; ++i)
    {
        std::string const o = Tick(r, atPoint);
        if (o.compare(0, 7, "summon ") == 0)
            summons.push_back(o.substr(7));
    }
    CHECK(summons == std::vector<std::string>({"Yssia", "Beardinator", "Wendrick"}));
    CHECK(r.strandSummon && r.summoned.count("Yssia") && !r.summoned.count("Bowjangles"));
    CHECK(r.stepSummons["Wendrick"].count == 1);
    CHECK(r.note == "the lead bot was teleported to the last reached point (step 2): waiting for Bowjangles (left behind, "
        "30.0 yd from it) to leave its fight before summoning it");
    // Its fight over (no combat, no attackers): summoned on that tick, and the summons end.
    Facts freed = atPoint;
    freed.members[4].inCombat = false;
    CHECK(Tick(r, freed) == "summon Bowjangles");
    CHECK(r.summoned.count("Bowjangles"));
    CHECK(Tick(r, freed).compare(0, 7, "summon ") != 0 && !r.strandSummon && r.state == State::Running);
    // Attackers alone count as a fight too (asked of the summon tick itself: Tick's regroup takes such a
    // bot first, "running back with 1 attacker").
    Facts hit = atPoint;
    hit.members[4].inCombat = false;
    hit.members[4].attackers = 1;
    Run att = fightLeft;
    att.strandSummoned = {"Yssia", "Beardinator", "Wendrick"};
    std::string attOrder;
    CHECK(StrandedTeleportTick(att, hit, kTickMs, attOrder) && attOrder.empty() && att.strandSummon);
    CHECK(att.note.find("waiting for Bowjangles") != std::string::npos);
    // Still in its fight after 120 s: the pause names the bot and its spot.
    atPoint.members[4].x = -446.3f;
    atPoint.members[4].y = 92.4f;
    atPoint.members[4].z = -188.7f;
    for (std::uint32_t ms = 0; ms <= kStrandSummonWaitMs + 8 * kTickMs && fightLeft.state == State::Running; ms += kTickMs)
        Tick(fightLeft, atPoint);
    CHECK(fightLeft.state == State::Paused && fightLeft.summoned.count("Wendrick") && !fightLeft.summoned.count("Bowjangles"));
    CHECK(fightLeft.note == "the lead bot was teleported to the last reached point (step 2), but Bowjangles stayed in a "
        "fight 30.0 yd from it at -446.3 92.4 -188.7 for 120 s: bring it to the lead bot, then .bot run resume");
    CHECK(kStrandSummonWaitMs == 120000);

    // At most once a step: the same strand again on that step passes the point as before.
    Run again = stranded();
    again.strandStep = 2;
    OrderRefused(again, back, kBareUnreachable);
    CHECK(!again.strandDue && again.walkBackIdx == 2 && again.note.find(": on to the next") != std::string::npos);
    // ... and a resume (ResetStep) does not give it back, nor the counts.
    again.strandCountStep = 2;
    again.strandTries = 2;
    again.strandWaits = 4;
    ResetStep(again);
    CHECK(again.strandStep == 2 && again.strandTries == 2 && again.strandWaits == 4 && !again.strandSummon);

    // Not stranded: the hop off found a spot (or never ran) here.
    Run hop = stranded();
    hop.noSpotKnown = false;
    OrderRefused(hop, back, kBareUnreachable);
    CHECK(!hop.strandDue && hop.walkBackIdx == 2);
    Run elsewhere = stranded();
    elsewhere.leadLastX = -440.0f; // the no-spot reading was from another spot
    OrderRefused(elsewhere, back, kBareUnreachable);
    CHECK(!elsewhere.strandDue);
    // Not the last reached point: a point before the checkpoint is passed as before.
    Run early = stranded();
    early.walkBackIdx = 0;
    OrderRefused(early, MoveOrder(-460.0f, 50.0f, -172.9f, false), kBareUnreachable);
    CHECK(!early.strandDue && early.walkBackIdx == 1);
    // Not unreachable: another path refusal passes the point as before.
    Run los = stranded();
    OrderRefused(los, back, "no-los: the point is out of sight");
    CHECK(!los.strandDue && los.walkBackIdx == 2);
    // Not on a walk back: no teleport.
    Run fwd = stranded();
    fwd.walkBack = false;
    CHECK(!StrandedLead(fwd, kBareUnreachable));

    // A failed teleport (the glue refused it; nothing changed on the lead bot): due again, 3 tries a step,
    // never marked used.
    Run w = stranded();
    OrderRefused(w, back, kBareUnreachable);
    for (std::uint32_t t = 1; t <= kStrandTries; ++t)
    {
        order = Tick(w, f);
        CHECK(order == "rescue from -448.0 103.2 -191.0 to -422.4 82.4 -172.9");
        OrderRefused(w, order, "failed: the near teleport was refused");
        CHECK(!w.strandOn && w.strandStep == static_cast<std::size_t>(-1) && w.strandTries == t);
        CHECK(w.strandDue == (t < kStrandTries));
    }
    CHECK(w.note == "the teleport of the stranded lead bot failed 3 times (refused: failed: the near teleport was "
        "refused): the walk back goes on as before");
    // Used up: the point refused again is passed, no fourth try this step.
    w.noSpotKnown = true;
    w.noSpotX = w.leadLastX;
    w.noSpotY = w.leadLastY;
    w.noSpotZ = w.leadLastZ;
    CHECK(!StrandedLead(w, kBareUnreachable));
    // Not at the point after the settle: a failed try too.
    Run off = stranded();
    OrderRefused(off, back, kBareUnreachable);
    order = Tick(off, f);
    CHECK(order.compare(0, 7, "rescue ") == 0);
    for (std::uint32_t ms = 0; ms < kStrandSettleMs; ms += kTickMs)
        Tick(off, f);
    CHECK(!off.strandOn && off.strandDue && off.strandTries == 1 && off.strandStep == static_cast<std::size_t>(-1));
    CHECK(off.note.find("the teleport of the stranded lead bot failed (the lead bot stands ") == 0);

    // "wait" answers keep it due (not a try), 10 a step, then the pause names the spot.
    Run wt = stranded();
    OrderRefused(wt, back, kBareUnreachable);
    for (std::uint32_t i = 1; i < kStrandWaits; ++i)
    {
        order = Tick(wt, f);
        CHECK(order.compare(0, 7, "rescue ") == 0);
        OrderRefused(wt, order, "wait: the lead bot moved since it was read stranded");
        CHECK(wt.strandDue && wt.strandWaits == i && wt.strandTries == 0 && wt.state == State::Running);
    }
    CHECK(wt.note == "the teleport of the stranded lead bot waits (wait: the lead bot moved since it was read stranded) "
        "(9 of 10)");
    order = Tick(wt, f);
    OrderRefused(wt, order, "wait: a party bot is in combat");
    CHECK(wt.state == State::Paused);
    CHECK(wt.note == "the stranded lead bot at -448.0 103.2 -191.0 could not be teleported: the teleport waited 10 times "
        "this step (wait: a party bot is in combat); bring it to the last reached point (step 2), then .bot run resume");
    CHECK(kStrandTries == 3 && kStrandWaits == 10);
}

static void TestR22PullAndDirectStepsKeepTheirRefusals()
{
    DungeonPlan plan = RfkS85Plan();
    Facts f = AtRfk(2153.0f, 1589.0f, 80.5f);
    // A pull step's refused move (an approach or a gather): not the move fallback; the pause as before.
    Run p = Start(&plan, 3);
    std::string const hop = "move 2183.1 0.0 0.0";
    CHECK(!OwnMoveOrder(p, hop));
    CHECK(!MoveRefusedUnreachable(p, hop, kBareUnreachable, true, 2170.0f, 0.0f, 0.0f));
    CHECK(p.step == 3 && !p.detour && p.state == State::Running);
    GlueRefused(p, hop, kBareUnreachable, true, 2170.0f, 0.0f, 0.0f);
    CHECK(p.state == State::Paused && p.step == 3 && p.note == std::string("cmd ") + hop + " refused: " + kBareUnreachable);
    // A direct step, and the plain move right before one: never skipped (the hole's crossing needs them).
    DungeonPlan d;
    d.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::MoveDirect, 60), Step(PlanStepKind::Rest)};
    Run m = Start(&d, 0);
    std::string const before = Tick(m, Party(0));
    CHECK(before == "move 40.0 0.0 0.0");
    GlueRefused(m, before, kBareUnreachable, false);
    CHECK(m.state == State::Paused && m.step == 0);
    CHECK(m.note.find(std::string(kBareUnreachable) + " (no reachable point within 25 yd of it either)") != std::string::npos);
    Run x = Start(&d, 1);
    x.gatherWaived = true;
    x.step = 1;
    CHECK(!MoveRefusedUnreachable(x, "move 60.0 0.0 0.0 direct", kBareUnreachable, false, 0, 0, 0));
    CHECK(x.step == 1);
    // A direct step's stand-in stays within 15 yd.
    Run y = Start(&d, 1);
    CHECK(!UseDetour(y, 40, 0, 0) && UseDetour(y, 50, 0, 0));
    // A refusal from the lead bot's own spot (an island) past the hop's caps: the pause, not a skip of every step.
    Run i = Start(&plan, 1);
    std::string const move = Tick(i, f);
    i.islandEscapes = kIslandEscapes;
    GlueRefused(i, move, kNoWalkablePath, false);
    CHECK(i.state == State::Paused && i.step == 1);
}

// Opus review of 51fc904f: the guard's walk back to the stand point is the step's own point (the stay anchor, the
// plan's z): the glue hands it to RefusedFromIsland before OrderRefused, so its guard branch never runs. The walk
// back must not go out again on the next tick (Tick walks a guard return before MoveStep): the hop does.
static void TestGuardReturnThroughTheGlue()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    Run r = Start(&plan, 1);
    SetGuardReturn(r, -163.5f, 298.7f, -52.8f); // plan z == guard z
    std::string const back = MoveOrder(r.guardX, r.guardY, r.guardZ, false);
    CHECK(back == MoveOrder(plan.steps[1].x, plan.steps[1].y, plan.steps[1].z, false));
    GlueRefused(r, back, kNoWalkablePath, false);
    CHECK(r.state == State::Running && r.islandSuspect && !r.guardReturn);
    CHECK(r.note.find("cmd " + back + " refused (unreachable") == 0);
    CHECK(Tick(r, g) == "escape from -159.7 287.2 -50.2 toward -163.5 298.7 -52.8");
    // A detour found: the glue's detour, no sign taken (the guard branch is not this path's).
    Run d = Start(&plan, 1);
    SetGuardReturn(d, -163.5f, 298.7f, -52.8f);
    d.detour = false;
    GlueRefused(d, back, kNoWalkablePath, true, -160.0f, 298.7f, -52.8f);
    CHECK(!d.islandSuspect && d.state == State::Running);
    // A bare `unreachable` through the glue: the guard branch, 'going on from here', as before.
    Run b = Start(&plan, 1);
    SetGuardReturn(b, -163.5f, 298.7f, -52.8f);
    GlueRefused(b, back, kBareUnreachable, false);
    CHECK(!b.islandSuspect && !b.guardReturn && b.state == State::Running);
    CHECK(b.note.find("going on from here") != std::string::npos);
}

// Opus review of 51fc904f: the sign taken, a fight moves the tank off the tip; the new spot's path reads good:
// the suspicion is gone and the move goes out, no hop.
static void TestGoodPathClearsTheRefusalSign()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    Run r = Start(&plan, 1);
    std::string const move = Tick(r, g);
    CHECK(RefusedFromIsland(r, move, kNoWalkablePath) && r.islandSuspect);
    Facts f = Party(0);
    f.leadX = -162.1f;
    f.leadY = 287.0f;
    f.leadZ = -52.7f;
    f.leadPathGood = true;
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(!r.islandSuspect);
    f.anyInCombat = false;
    CHECK(Tick(r, f) == move);
    CHECK(!r.escapeOn && r.stepEscapes == 0);
}

// The detour's candidate check (NearestReachablePoint). r6-19z24: from the
// island at 1591.2 1044.3 the 3 yd ring point of step 64, 1587.0 1050.0, had a
// partial path ending at 1588.4 1046.5 (3.8 yd from it, 3.6 yd from the lead
// bot), and step 65's 10 yd ring point 1594.3 1048.0 one ending at 1590.9 1045.3
// (4.3 yd from it, 1.0 yd from the lead bot): both were taken as reachable (end
// within 5 yd of the point) and both `cmd move`s were refused, each a pause.
static void TestDetourPointReachable();

// A forward run on the main mesh: the lead bot 8 yd short of step 2's point,
// its path partial and ending 5 yd on (within 6 yd of the point: a ledge, a
// rock), and the last good spot read just before (goodSpotCut: e.g. a mob's
// body in the way). The forward check sends one hop; the glue finds no spot
// (no-spot); from that spot the move goes out as before: no second hop, no pause.
static Facts MainMeshPartial()
{
    Facts g = Party(0);
    g.leadX = 1584.0f;
    g.leadY = 1042.0f;
    g.leadZ = 9.0f;
    g.leadIsland = true;
    g.goodSpotCut = true;
    return g;
}

static void RecordGoodSpot(Run& r, float x, float y, float z)
{
    Facts f = Party(0);
    f.leadX = x;
    f.leadY = y;
    f.leadZ = z;
    f.leadPathGood = true;
    f.anyInCombat = true; // a fight tick: nothing goes out for the step
    Tick(r, f);
    CHECK(r.goodLeadKnown && r.goodLeadX == x && r.goodLeadY == y && r.goodLeadZ == z);
    CHECK(r.goodLeadMs == r.runMs && r.runMs > 0);
}

static void TestForwardEscapeNoSpotFallsBack()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    RecordGoodSpot(r, 1584.0f, 1036.0f, 9.0f);
    Facts g = MainMeshPartial();
    std::string const escape = Tick(r, g);
    CHECK(escape == "escape from 1584.0 1042.0 9.0 to 1584.0 1036.0 9.0 toward 1584.0 1050.0 9.0");
    CHECK(r.escapeFromMove);
    OrderRefused(r, escape, "no-spot: no spot within 10 yd of the lead bot leads on");
    CHECK(r.noSpotKnown && !r.escapeFromMove && r.state == State::Running);
    CHECK(Tick(r, g) == "move 1584.0 1050.0 9.0");
    CHECK(r.state == State::Running && r.islandEscapes == 1);
    // A wait refusal (a fight) is no try and leaves no mark: the hop goes out again.
    Run w = Start(&plan, 1);
    RecordGoodSpot(w, 1584.0f, 1036.0f, 9.0f);
    std::string const again = Tick(w, g);
    OrderRefused(w, again, "wait: a party bot is in combat");
    CHECK(!w.noSpotKnown && Tick(w, g) == again);
    // Any other hop off an island (the approach hop's, a walk back's) is not the
    // move check's; nor is one in the next step.
    std::string other;
    IslandEscape(w, g, 1603, 1043, 9.8f, "the next point", other);
    CHECK(!other.empty() && !w.escapeFromMove);
    w.escapeFromMove = true;
    ResetStep(w);
    CHECK(!w.escapeFromMove);
}

// A fall in a fight (a one-way drop within kGoodSpotFreshMs): the good spot is
// 6 yd above the lead bot, the path back up partial. The hop to it is sent; the
// glue's line check refuses it (a rise of more than kSegmentRiseYd per sample):
// no-spot, and the move goes out as before.
static void TestForwardEscapeAfterAFallFallsBack()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    RecordGoodSpot(r, 1584.0f, 1038.0f, 15.0f);
    Facts g = MainMeshPartial();
    std::string const escape = Tick(r, g);
    CHECK(escape == "escape from 1584.0 1042.0 9.0 to 1584.0 1038.0 15.0 toward 1584.0 1050.0 9.0");
    // What the glue's line check reads for it: the floor steps up 6 yd at once.
    std::vector<SegmentSample> line = EscapeSegmentSamples(1584.0f, 1042.0f, 9.0f, 1584.0f, 1038.0f, 15.0f);
    for (SegmentSample& sample : line)
    {
        sample.ground = true;
        sample.groundZ = sample.y <= 1040.0f ? 15.0f : 9.0f;
        sample.z = sample.groundZ;
    }
    CHECK(CheckEscapeSegment(9.0f, line, false) == SegmentVerdict::Step);
    OrderRefused(r, escape, "no-spot: no spot within 10 yd of the lead bot leads on");
    CHECK(Tick(r, g) == "move 1584.0 1050.0 9.0");
    CHECK(r.state == State::Running);
}

// Sign (a) for the forward check counts only for a good spot read within
// kGoodSpotFreshMs of run time.
static void TestForwardEscapeNeedsAFreshGoodSpot()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    RecordGoodSpot(r, 1584.0f, 1036.0f, 9.0f);
    r.runMs = r.goodLeadMs + kGoodSpotFreshMs + 1; // the spot is older than that
    CHECK(!GoodSpotFresh(r));
    Facts g = MainMeshPartial();
    CHECK(Tick(r, g) == "move 1584.0 1050.0 9.0"); // one sign only: the evidence is kept
    CHECK(r.islandEvidence && !r.escapeOn);
    Run e = Start(&plan, 1);
    RecordGoodSpot(e, 1584.0f, 1036.0f, 9.0f);
    e.runMs = e.goodLeadMs + kGoodSpotFreshMs - kTickMs; // the tick brings it to exactly the bound
    CHECK(Tick(e, g).compare(0, 7, "escape ") == 0);
    CHECK(GoodSpotFresh(e) && e.runMs - e.goodLeadMs == kGoodSpotFreshMs);
    Run n;
    CHECK(!GoodSpotFresh(n)); // none read
    // The run's own clock runs with every running tick.
    Run c = Start(&plan, 1);
    Tick(c, g, 700);
    CHECK(c.runMs == 700);
}

static void TestForwardMoveProbeGateAndCache()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    CHECK(r.phase == Phase::Begin);
    CHECK(ForwardMoveProbeWanted(r, true, true, false, 8.0f));
    CHECK(!ForwardMoveProbeWanted(r, false, true, false, 8.0f)); // not a move step
    CHECK(!ForwardMoveProbeWanted(r, true, false, false, 8.0f)); // out of sight: the approach hop reads
    CHECK(!ForwardMoveProbeWanted(r, true, true, true, 8.0f));   // a fight
    CHECK(!ForwardMoveProbeWanted(r, true, true, false, kArriveYd)); // at the point
    Run w = r;
    w.phase = Phase::Walk;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w.phase = Phase::Rest;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w.phase = Phase::AwaitPull;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w = r;
    w.regroup = true;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w = r;
    w.walkBack = true; // the walk back reads its own point, ungated as before
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w = r;
    w.state = State::Paused;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));

    MoveProbeCache c;
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 0));
    CHECK(!MoveProbeHit(c, 0, 0.5f, 0.5f, 0, 0, 0, 100)); // nothing read: no hit, whatever matches the zeros
    StoreMoveProbe(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 10000, true, false);
    CHECK(c.island && !c.good);
    CHECK(MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 10000));
    CHECK(MoveProbeHit(c, 1, 1590.1f, 1045.9f, 1584, 1050, 9, 10000 + kMoveProbeCacheMs - 1)); // same 2 yd cell
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 10000 + kMoveProbeCacheMs)); // 5 s on
    CHECK(!MoveProbeHit(c, 1, 1592.1f, 1044.3f, 1584, 1050, 9, 10001)); // another cell (x)
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1046.1f, 1584, 1050, 9, 10001)); // another cell (y)
    CHECK(!MoveProbeHit(c, 2, 1591.2f, 1044.3f, 1584, 1050, 9, 10001)); // another step
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1603, 1050, 9, 10001)); // another point (x)
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1043, 9, 10001)); // (y)
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9.8f, 10001)); // (z)
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 9999)); // a clock before it (a new run)
}

static void TestDetourPointReachable()
{
    CHECK(!DetourPointReachable(false, true, 3.8f, 3.6f, 20.0f));     // r6 step 64's detour 1587 1050
    CHECK(!DetourPointReachable(false, true, 4.3f, 1.0f, 20.0f));     // r6 step 65's detour 1594.3 1048
    CHECK(!DetourPointReachable(false, true, 0.5f, 2.0f, 20.0f)); // CheckMove's refusal (RemoteCommander::kNoProgressYd)
    CHECK(!DetourPointReachable(false, true, 4.0f, kGoodPathMinYd, 20.0f)); // partial and gets nowhere past the island
    CHECK(DetourPointReachable(false, true, 4.0f, kGoodPathMinYd + 0.5f, 20.0f)); // partial, leads on, ends near the point
    CHECK(!DetourPointReachable(false, true, kDetourEndYd + 0.5f, 30.0f, 20.0f)); // ... but short of it
    CHECK(DetourPointReachable(false, false, 0.3f, 1.0f, 20.0f));     // complete: accepted, however short
    CHECK(DetourPointReachable(false, false, kDetourEndYd, 1.0f, 20.0f));
    CHECK(!DetourPointReachable(false, false, kDetourEndYd + 0.5f, 20.0f, 20.0f)); // complete but ends off the point
    CHECK(!DetourPointReachable(true, false, 0.0f, 20.0f, 20.0f));    // no path
    // r9-19z26 step 124: the ring point 136 298.7 on the tank's own island, 1.3 yd from it, its path complete.
    CHECK(!DetourPointReachable(false, false, 0.0f, 1.3f, 1.3f));
    CHECK(!DetourPointReachable(false, false, 0.0f, 20.0f, kArriveYd));
    CHECK(DetourPointReachable(false, false, 0.0f, 20.0f, kArriveYd + 0.1f));
}

// Uldaman r13-19z30 slot 5/slot 1 (T3): the tank fell into the 6 x 6 yd pit at
// x 39.7-46.1, y 370.7-376.8, z -38.9 (no navmesh exit). The detour of step 96
// (50.8 379.3) took the pit's own ring point 42.1 374.3 as 'the nearest reachable
// point': the lead's path to it is complete (same polygon group), but the point's own
// path toward 50.8 379.3 is partial and ends 1.1 yd from it, in the pit.
static void TestDetourCandidateLeadsOn()
{
    float const fromPoint = 10.0f; // 42.1 374.3 is 10.0 yd from 50.8 379.3
    // The pit point: lead at 45.3 374.4 (3.2 yd away), the candidate's probe partial,
    // ending 1.1 yd from it and 1.1 + 3.2 yd from the lead.
    CHECK(DetourPointReachable(false, false, 0.0f, 20.0f, 3.2f));
    CHECK(!DetourCandidateLeadsOn(false, true, 1.1f, 4.3f, fromPoint, 10.0f, 0.0f));
    // Same pit point, lead at 42.5 376.3 (2.0 yd): refused as before by the arrive rule.
    CHECK(!DetourPointReachable(false, false, 0.0f, 20.0f, 2.0f));
    // A pit point whose probe ends in the pit at more than 10 yd from the lead and from itself would lead on
    // (the same rule as a ring spot's): the lead's own end distance stays the second gate.
    CHECK(!DetourCandidateLeadsOn(false, true, kGoodPathMinYd + 1, 3.0f, fromPoint, 10.0f, 0.0f)); // ends near the lead
    CHECK(!DetourCandidateLeadsOn(false, true, 3.0f, kGoodPathMinYd + 1, fromPoint, 10.0f, 0.0f)); // ends near the candidate
    // A main-mesh candidate outside the pit: its probe is complete -> accepted.
    CHECK(DetourCandidateLeadsOn(false, false, 0.0f, 12.0f, fromPoint, 10.0f, 0.0f));
    // ... or partial but leading on well past both.
    CHECK(DetourCandidateLeadsOn(false, true, kGoodPathMinYd + 1, kGoodPathMinYd + 1, fromPoint, 10.0f, 0.0f));
    // No path at all from the candidate: refused.
    CHECK(!DetourCandidateLeadsOn(true, false, 0.0f, 12.0f, fromPoint, 10.0f, 0.0f));
    // The plan's own point (or one within kArriveYd of it): asked since the r22-19z40 review. Its probe complete,
    // ending at the point, on the point's level: accepted.
    CHECK(DetourCandidateLeadsOn(false, false, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f));
    CHECK(DetourCandidateLeadsOn(false, false, 2.0f, 12.0f, kArriveYd, kArriveYd, kDetourCloseMaxDz));
    // Across a wall: the probe found no path, or a partial one: refused.
    CHECK(!DetourCandidateLeadsOn(true, false, 0.0f, 12.0f, 2.0f, 2.0f, 0.0f));
    CHECK(!DetourCandidateLeadsOn(false, true, 0.0f, 12.0f, 2.0f, 2.0f, 0.0f));
    // A complete probe that ends short of the point (snapped onto another polygon): refused.
    CHECK(!DetourCandidateLeadsOn(false, false, 0.0f, 12.0f, 2.0f, kArriveYd + 0.1f, 0.0f));
    // Up or down a ledge (more than 4 yd): refused.
    CHECK(!DetourCandidateLeadsOn(false, false, 0.0f, 12.0f, 1.0f, 0.5f, kDetourCloseMaxDz + 0.1f));
    CHECK(!DetourCandidateLeadsOn(false, false, 0.0f, 12.0f, 1.0f, 0.5f, -(kDetourCloseMaxDz + 0.1f)));
    // Past kArriveYd the ring spot's rule, as before.
    CHECK(!DetourCandidateLeadsOn(false, true, 0.0f, 0.0f, kArriveYd + 0.1f, 5.0f, 0.0f));
    // The lead itself within kArriveYd of the candidate: still refused before the probe is read.
    CHECK(!DetourPointReachable(false, false, 0.0f, 0.0f, kArriveYd));
}

// One reading is no island (a ledge, a closed door): the forward move goes out
// as before; a direct step's point never escapes; out of sight the approach hop decides.
static void TestForwardMoveIslandNeedsSecondSign()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    RecordGoodSpot(r, 1583.7f, 1040.3f, 8.9f); // fresh: sign (a) would count
    Facts g = Party(0);
    g.leadX = 1591.2f;
    g.leadY = 1044.3f;
    g.leadZ = 8.9f;
    g.leadIsland = true;
    CHECK(Tick(r, g) == "move 1584.0 1050.0 9.0");
    CHECK(r.islandEvidence && !r.escapeOn);
    // The walk under way is not cut short by a reading (the glue reads none while it walks).
    g.goodSpotCut = true;
    CHECK(r.phase == Phase::Walk && Tick(r, g).find("escape") == std::string::npos && !r.escapeOn);

    DungeonPlan direct = ZfIslandPlan();
    direct.steps[1].kind = PlanStepKind::MoveDirect;
    Run d = Start(&direct, 1);
    RecordGoodSpot(d, 1583.7f, 1040.3f, 8.9f);
    CHECK(Tick(d, g).find("escape") == std::string::npos);
    CHECK(!d.escapeOn);

    // At the point already: nothing to escape toward.
    Run a = Start(&plan, 1);
    RecordGoodSpot(a, 1583.7f, 1040.3f, 8.9f);
    Facts h = g;
    h.leadX = 1584.5f;
    h.leadY = 1049.0f;
    h.leadZ = 9.0f;
    CHECK(Tick(a, h).find("escape") == std::string::npos);
}

// Uldaman r9-19z26 (descent leg): the fight of step 124 left the tank on a patch of
// mesh at 132-139 / 296-304, z -23.8, above the -26.5 floor. From 137.3 298.7 -23.3
// every path came back NOPATH (a partial one ending 0.0-0.2 yd off, inside the smooth
// path's slop): step 124's move (141 290, 9.9 yd) and step 127's (158.8 243.8, 59.4 yd)
// were refused unreachable, the second one 'no reachable point within 15 yd of it
// either' = pause, twice. The reading said no island, so the escape never ran.
static void TestForwardMoveNoPathLedgeEscapes()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 121, 280.5f, -26.3f), Step(PlanStepKind::Move, 141, 290, -26.4f),
        Step(PlanStepKind::Move, 158.8f, 243.8f, -30.7f)};
    float const lx = 137.3f, ly = 298.7f, lz = -23.3f;
    auto onLedge = [&](float tx, float ty, float tz) {
        Facts g = Party(0);
        g.leadX = lx;
        g.leadY = ly;
        g.leadZ = lz;
        float const to = ai::PlanDistance(lx, ly, lz, tx, ty, tz);
        g.leadIsland = LeadOnIsland(true, false, to, to); // NOPATH: the end is the point itself
        g.leadPathGood = LeadPathGood(true, false, to);
        return g;
    };
    // Step 127 (here step 3): the last good spot was read at the via point 24.6 yd off,
    // 94 s before; the path from the ledge to it is partial (the second sign).
    Run r = Start(&plan, 2);
    RecordGoodSpot(r, 121, 280.5f, -26.3f);
    Facts g = onLedge(158.8f, 243.8f, -30.7f);
    CHECK(g.leadIsland && !g.leadPathGood);
    g.goodSpotCut = true;
    CHECK(Tick(r, g) == "escape from 137.3 298.7 -23.3 toward 158.8 243.8 -30.7"); // the good spot is beyond kEscapeMaxYd
    CHECK(r.note.find("navmesh island at 137.3 298.7 -23.3 (no path toward the point of step 3)") != std::string::npos);
    CHECK(r.state == State::Running && r.escapeFromMove && r.goodLeadX == 121);
    // Step 124 (here step 2) after the rest, 9.9 yd from its point: the same.
    Run s = Start(&plan, 1);
    RecordGoodSpot(s, 121, 280.5f, -26.3f);
    Facts h = onLedge(141, 290, -26.4f);
    h.goodSpotCut = true;
    CHECK(Tick(s, h) == "escape from 137.3 298.7 -23.3 toward 141.0 290.0 -26.4");
    // One sign alone (no fresh good spot, no second reading): the move goes out as before.
    Run o = Start(&plan, 2);
    Facts k = onLedge(158.8f, 243.8f, -30.7f);
    CHECK(Tick(o, k) == "move 158.8 243.8 -30.7");
    CHECK(o.islandEvidence && !o.escapeOn);
    // The documented limit: the good spot is beyond the straight hop, and a ring spot 3 yd off on the -26.5 floor
    // fails the line check (3.2 yd down at the first sample) unless the rubble's own slope steps down 2 yd or less.
    CHECK(!GoodSpotFits(lx, ly, lz, 121, 280.5f, -26.3f));
    std::vector<SegmentSample> line = EscapeSegmentSamples(lx, ly, lz, lx + 3, ly, -26.5f);
    for (SegmentSample& sample : line)
    {
        sample.ground = true;
        sample.groundZ = -26.5f;
    }
    CHECK(CheckEscapeSegment(lz, line, false) == SegmentVerdict::Step);
    line.front().groundZ = -24.6f; // a slope like the one the tank walked up in r9 (-25.8 to -24.2 over 3.5 yd)
    CHECK(CheckEscapeSegment(lz, line, false) == SegmentVerdict::Ok);
}

static void TestResumeEscapesFirst()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    Facts g = OnIsland();
    for (int i = 0; i < 20 && r.state == State::Running; ++i)
        Tick(r, g);
    CHECK(r.state == State::Paused && r.note.find("navmesh island") != std::string::npos);
    // `.bot run resume`: the regroup on the lead bot, the walk back from the
    // same spot, and its first order is the hop off the island, not a hop.
    CHECK(Resume(r));
    g.members = {Mem("Tanky", 0), Mem("Healy", 4)};
    g.maxMemberYd = 4;
    CHECK(Tick(r, g).empty());
    CHECK(r.walkBack && r.walkBackIdx == 4 && !r.regroup);
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
    CHECK(r.islandEscapes == 1 && r.state == State::Running);
}

static void TestWalkBackPointInSightOnIsland()
{
    DungeonPlan plan = ZfIslandPlan();
    // Step 3's point is in sight: no hop is read, the glue reads the lead bot's path to it.
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = 2;
    Facts g = OnIsland();
    g.seen = true;
    g.approachKnown = false;
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1588.0 1080.0 9.1");

    // No island read (the backstop): two points refused from one spot with no
    // path there that got anywhere; the second is not passed, the lead bot is hopped off first.
    Run q = Start(&plan, 5);
    q.walkBack = true;
    q.walkBackIdx = 1;
    Facts h = g;
    h.leadIsland = false;
    std::string const unreachable =
        "unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it).";
    CHECK(Tick(q, h) == "move 1584.0 1050.0 9.0");
    OrderRefused(q, "move 1584.0 1050.0 9.0", unreachable);
    CHECK(q.walkBackIdx == 2 && q.note.find("on to the next") != std::string::npos);
    CHECK(Tick(q, h) == "move 1588.0 1080.0 9.1");
    OrderRefused(q, "move 1588.0 1080.0 9.1", unreachable);
    CHECK(q.state == State::Running && q.walkBackIdx == 2 && q.islandSuspect);
    CHECK(Tick(q, h) == "escape from 1589.6 1046.9 8.9 toward 1588.0 1080.0 9.1");

    // A spot whose path read got somewhere: refused points are passed as before.
    Run o = Start(&plan, 5);
    o.walkBack = true;
    o.walkBackIdx = 1;
    Facts k = h;
    k.leadPathGood = true;
    CHECK(Tick(o, k) == "move 1584.0 1050.0 9.0");
    OrderRefused(o, "move 1584.0 1050.0 9.0", "off-mesh: The point is not on walkable ground.");
    CHECK(Tick(o, k) == "move 1588.0 1080.0 9.1");
    OrderRefused(o, "move 1588.0 1080.0 9.1", "off-mesh: The point is not on walkable ground.");
    CHECK(o.walkBackIdx == 3 && !o.islandSuspect && o.note.find("on to the next") != std::string::npos);

    // Out of sight, a hop that ends where the lead bot stands (no island read):
    // no path toward the point at once, not a hop counted (r3-19z20: 8 per point).
    Run w = Start(&plan, 5);
    w.walkBack = true;
    w.walkBackIdx = w.walkBackStart = 4;
    w.walkBackRestarted = true;
    Facts z = OnIsland();
    z.leadIsland = false;
    CHECK(Tick(w, z).empty());
    CHECK(w.approachHops == 0 && w.walkBackIdx == 5);
    CHECK(w.note == "walk back: no walkable path toward the point of step 5: on to the next");
}

// The step's own approach (r3-19z20 step 102: 8 hops of zero length, then the
// pause 315.1 yd out): on the island it hops off first; a hop that ends where
// the lead bot stands is no path at once, not 8 hops.
static void TestApproachHopIslandAndZeroHop()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.restDue = false;
    Facts g = OnIsland();
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1903.0 1014.0 11.8");
    CHECK(r.approachHops == 0 && r.note.find("no path toward the pack") != std::string::npos);

    Run z = Start(&plan, 5);
    z.restDue = false;
    Facts h = OnIsland();
    h.leadIsland = false;
    CHECK(Tick(z, h).empty());
    CHECK(z.state == State::Running && z.approachStandWanted && z.note.find("no walkable path toward the pack") == 0);
    CHECK(z.note.find("escape") == std::string::npos);
}


// Cold review on bcc23124 (Opus M1): from the frozen spot the path to step 65
// ended at 1591.2 1044.3, 3.05 yd from the lead bot: over the old 2 yd island
// line and the old 3 yd "good path" line, so the island spot became the last
// good one and each good read reset the escape count (hop, escape, hop ...).
// With the real numbers it now reads as island and resets nothing.
static void TestIslandGapIsNotAGoodPath()
{
    float const lx = 1589.6f, ly = 1046.9f, lz = 8.9f;
    float const end = ai::PlanDistance(lx, ly, lz, 1591.2f, 1044.3f, 8.9f);
    float const toStep65 = ai::PlanDistance(lx, ly, lz, 1588, 1080, 9.1f);
    CHECK(end > 3.0f && end < 3.1f);
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    r.goodLeadKnown = true;
    r.goodLeadX = 1584.8f;
    r.goodLeadY = 1040.2f;
    r.goodLeadZ = 8.9f;
    Facts g = OnIsland();
    g.approachX = 1591.2f;
    g.approachY = 1044.3f;
    g.leadIsland = LeadOnIsland(false, true, end, toStep65);
    g.leadPathGood = LeadPathGood(false, true, end);
    CHECK(g.leadIsland && !g.leadPathGood);
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 to 1584.8 1040.2 8.9 toward 1633.6 1130.7 9.9");
    for (int i = 0; i < 2; ++i)
        CHECK(Tick(r, g).empty());
    CHECK(r.islandEscapes == 1 && r.goodLeadX == 1584.8f && r.goodLeadY == 1040.2f); // not overwritten, not reset
    CHECK(Tick(r, g).compare(0, 7, "escape ") == 0);
    CHECK(r.islandEscapes == 2);
    for (int i = 0; i < 3; ++i)
        Tick(r, g);
    CHECK(r.state == State::Paused && r.note.find("navmesh island at 1589.6 1046.9 8.9") != std::string::npos);
    // A last good spot on the lead bot itself (within kArriveYd) is not hopped to.
    Run n = Start(&plan, 5);
    n.walkBack = true;
    n.walkBackIdx = n.walkBackStart = 4;
    n.goodLeadKnown = true;
    n.goodLeadX = 1590.6f;
    n.goodLeadY = 1046.9f;
    n.goodLeadZ = 8.9f;
    CHECK(Tick(n, OnIsland()) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
}

// Codex on bcc23124: a hop refused because a fight started or the lead bot
// moved since the read ("wait: ...") is not a try; "no-spot" is.
static void TestEscapeWaitIsNotATry()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    Facts g = OnIsland();
    std::string const order = Tick(r, g);
    CHECK(order.compare(0, 7, "escape ") == 0 && r.islandEscapes == 1);
    OrderRefused(r, order, "wait: a party bot is in combat");
    CHECK(r.islandEscapes == 0 && r.stepEscapes == 0 && !r.escapeOn && r.state == State::Running &&
        r.note.find("not counted") != std::string::npos);
    CHECK(Tick(r, g) == order);
    OrderRefused(r, order, "no-spot: no spot within 10 yd");
    CHECK(r.islandEscapes == 1 && r.note.find("not counted") == std::string::npos);
}

// Codex on bcc23124: the straight line is checked before the lead bot moves.
static void TestEscapeSegmentCheck()
{
    std::vector<SegmentSample> s = EscapeSegmentSamples(0, 0, 0, 10, 0, 0);
    CHECK(s.size() == 5 && s.back().x == 10.0f && s.front().x == 2.0f);
    CHECK(EscapeSegmentSamples(0, 0, 0, 30, 0, 0).size() == kSegmentSamples);
    CHECK(EscapeSegmentSamples(0, 0, 0, 0.5f, 0, 0).size() == 1);
    auto flat = [&]()
    {
        std::vector<SegmentSample> v = EscapeSegmentSamples(0, 0, 0, 10, 0, 0);
        for (SegmentSample& x : v)
        {
            x.ground = true;
            x.groundZ = 0.0f;
        }
        return v;
    };
    CHECK(CheckEscapeSegment(0, flat(), false) == SegmentVerdict::Ok);
    CHECK(CheckEscapeSegment(0, flat(), true) == SegmentVerdict::Hostile);
    CHECK(CheckEscapeSegment(0, {}, false) == SegmentVerdict::NoGround);
    std::vector<SegmentSample> v = flat();
    v[2].ground = false;
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::NoGround);
    v = flat();
    v[2].groundZ = -(kSegmentGroundYd + 0.5f); // a floor far under the line (a drop)
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::NoGround);
    v = flat();
    v[2].groundZ = kSegmentRiseYd + 0.5f;
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Step);
    v = flat();
    v[0].groundZ = kSegmentRiseYd + 0.5f; // the first sample against the lead bot's own height
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Step);
    v = flat();
    v[3].liquid = true;
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Liquid);
    // One-sided steps (Uldaman r16-19z34): a rise over kSegmentRiseYd is refused even where the spot leads on ...
    v = flat();
    v[0].groundZ = kSegmentRiseYd + 0.1f;
    CHECK(CheckEscapeSegment(0, v, false, true) == SegmentVerdict::Step);
    // ... a step down of 2.1-4.0 yd passes only to a spot whose path leads on ...
    for (float down : {kSegmentRiseYd + 0.1f, 3.0f, kSegmentDropYd})
    {
        v = flat();
        for (SegmentSample& x : v)
        {
            x.groundZ = -down;
            x.z = -down;
        }
        CHECK(CheckEscapeSegment(0, v, false, true) == SegmentVerdict::Ok);
        CHECK(CheckEscapeSegment(0, v, false, false) == SegmentVerdict::Step);
    }
    // ... and one over kSegmentDropYd never.
    v = flat();
    for (SegmentSample& x : v)
    {
        x.groundZ = -(kSegmentDropYd + 0.1f);
        x.z = x.groundZ;
    }
    CHECK(CheckEscapeSegment(0, v, false, true) == SegmentVerdict::Step);
    // The step down counts from sample to sample: two 3 yd steps down (6 yd in all, within kEscapeMaxVertical) pass.
    v = flat();
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        v[i].groundZ = i == 0 ? -3.0f : -6.0f;
        v[i].z = v[i].groundZ;
    }
    CHECK(CheckEscapeSegment(0, v, false, true) == SegmentVerdict::Ok);
    // Uldaman r13-19z30's closed pit stays refused: 2.6 yd below the crest, its spot's own path ends 1.1 yd from it
    // (no leads-on), so the glue never asks the line with the larger drop, and the plain line check refuses it.
    CHECK(!RingSpotLeadsOn(false, true, 1.1f, 4.3f));
    std::vector<SegmentSample> pit = EscapeSegmentSamples(42.7f, 376.5f, -36.3f, 42.7f, 373.5f, -38.9f);
    for (SegmentSample& x : pit)
    {
        x.ground = true;
        x.groundZ = -38.9f;
    }
    CHECK(CheckEscapeSegment(-36.3f, pit, false, RingSpotLeadsOn(false, true, 1.1f, 4.3f)) == SegmentVerdict::Step);
    // A ramp of 1.9 yd per sample up a line to 9.5 yd: no step, too far up in all.
    v = EscapeSegmentSamples(0, 0, 0, 10, 0, 9.5f);
    for (SegmentSample& x : v)
    {
        x.ground = true;
        x.groundZ = x.z;
    }
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Climb);
    v.pop_back();
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Ok);
    CHECK(std::string(SegmentVerdictName(SegmentVerdict::Liquid)) == "liquid on the line");
}

// Opus notes on bcc23124: the island never hops a lead bot off a direct step's
// point (it may stand at its launch spot), a direct point passed clears the
// suspicion, and so does a point reached.
static void TestIslandLeavesDirectStepsAlone()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(500, {1});
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::MoveDirect, 200), Step(PlanStepKind::Move, 220), pull};
    Run r = Start(&plan, 3);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 1;
    r.islandSuspect = true;
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // A crossed direct point refused: passed, and the suspicion with it.
    Run d = Start(&plan, 3);
    d.walkBack = true;
    d.walkBackIdx = 1;
    d.islandSuspect = true;
    d.routeKnown = true;
    d.checkpoint = 1;
    OrderRefused(d, "move 200.0 0.0 0.0 direct", "no-los: no line of sight");
    CHECK(d.walkBackIdx == 2 && !d.islandSuspect);
    // A point reached clears it too (the lead bot stands on it: no hop off it).
    Run q = Start(&plan, 3);
    q.walkBack = true;
    q.walkBackIdx = 0;
    q.islandSuspect = true;
    Facts a = Party(10);
    CHECK(Tick(q, a).empty());
    CHECK(q.walkBackIdx == 1 && !q.islandSuspect);
}


// Codex/Opus on 269503e0: one partial path toward one point does not prove an
// island (an unreachable ledge, a closed door, a point off the mesh). From the
// main mesh with no second sign, the walk back and the approach hop do what
// they did before (2012dfee): no escape order.
static void TestSingleIslandReadingChangesNothing()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(300, {1});
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 200), pull};
    // A ledge point in sight: the path toward it ends 5 yd out, short of it.
    Run r = Start(&plan, 2);
    r.walkBack = true;
    r.walkBackIdx = 0;
    Facts f = Party(0);
    f.leadIsland = LeadOnIsland(false, true, 5.0f, 40.0f);
    CHECK(f.leadIsland && !f.goodSpotCut);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    OrderRefused(r, "move 40.0 0.0 0.0", "unreachable: no navmesh route reaches it");
    CHECK(r.walkBackIdx == 1 && r.note.find("on to the next") != std::string::npos);
    // The next point out of sight: the hop is walked (5 yd), as before.
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = true;
    g.approachX = 5;
    g.leadIsland = LeadOnIsland(false, true, 5.0f, 200.0f);
    // ... read toward a point 160 yd from the first one from the same spot: that
    // is the second sign (b), so this check uses another spot (another cell).
    g.leadX = f.leadX = 20;
    g.approachX = 25;
    Run w = Start(&plan, 2);
    w.walkBack = true;
    w.walkBackIdx = 1;
    CHECK(Tick(w, g) == "move 25.0 0.0 0.0");
    // The approach hop of a pull step: the hop, not an escape.
    Run a = Start(&plan, 2);
    a.restDue = false;
    Facts h = Party(0);
    h.seen = false;
    h.approachKnown = true;
    h.approachX = 5;
    h.leadIsland = LeadOnIsland(false, true, 5.0f, 300.0f);
    CHECK(Tick(a, h) == "move 5.0 0.0 0.0");
    // ... and a zero-length hop with one reading only: no path, the stand search (B-FIX-2), not an escape.
    Run z = Start(&plan, 2);
    z.restDue = false;
    h.approachX = 1;
    h.leadIsland = LeadOnIsland(false, true, 1.0f, 300.0f);
    std::string const islandOrder = Tick(z, h);
    CHECK(islandOrder.empty()); // never a straight-line guess on the first no-path reading
    CHECK(z.state == State::Running && z.approachStandWanted && z.note.find("no walkable path toward the pack") == 0);
}

// The second sign (b) with the trace's numbers: from 1589.6 1046.9 the path to
// step 65 (1588 1080) ended 3.05 yd away, and the one to step 70 (1633.6 1130.7)
// came back partial too. The first reading changes nothing (the move goes out,
// is refused and passed); the second, toward a point 58 yd from the first, hops
// the lead bot off. Without the good spot probe (no good spot known yet).
static void TestSecondReadingConfirmsTheIsland()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(1903, {7271});
    pull.y = 1014;
    pull.z = 11.8f;
    plan.steps = {Step(PlanStepKind::Move, 1603, 1043, 9.8f), Step(PlanStepKind::Move, 1588, 1080, 9.1f),
        Step(PlanStepKind::Move, 1633.6f, 1130.7f, 9.9f), pull};
    Run r = Start(&plan, 3);
    r.walkBack = true;
    r.walkBackIdx = 1;
    Facts g = OnIsland();
    g.goodSpotCut = false;
    g.seen = true;
    g.approachKnown = false;
    g.leadIsland = LeadOnIsland(false, true, 3.05f, ai::PlanDistance(1589.6f, 1046.9f, 8.9f, 1588, 1080, 9.1f));
    CHECK(Tick(r, g) == "move 1588.0 1080.0 9.1");
    CHECK(r.islandEvidence);
    OrderRefused(r, "move 1588.0 1080.0 9.1", "unreachable: no navmesh route reaches it");
    CHECK(r.walkBackIdx == 2);
    Facts h = OnIsland();
    h.goodSpotCut = false;
    h.approachX = 1591.2f;
    h.approachY = 1044.3f;
    h.leadIsland = LeadOnIsland(false, true, 3.05f, ai::PlanDistance(1589.6f, 1046.9f, 8.9f, 1633.6f, 1130.7f, 9.9f));
    CHECK(Tick(r, h) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
    // The same point read again is no second sign.
    Run s = Start(&plan, 3);
    s.walkBack = true;
    s.walkBackIdx = 2;
    s.walkBackStart = 2;
    s.walkBackRestarted = true;
    Facts k = h;
    k.approachX = 1600; // a hop that goes somewhere
    k.approachY = 1060;
    CHECK(Tick(s, k) == "move 1600.0 1060.0 8.9");
    EnterPhase(s, Phase::Begin); // the hop did not move it: the same point is read again
    CHECK(Tick(s, k) == "move 1600.0 1060.0 8.9");
    CHECK(s.islandEvidence && s.state == State::Running);
    // The evidence goes with the spot: the lead bot moved out of the cell, and a
    // reading there toward the other point is a first one again.
    Run m = Start(&plan, 3);
    m.walkBack = true;
    m.walkBackIdx = 1;
    CHECK(Tick(m, g) == "move 1588.0 1080.0 9.1");
    OrderRefused(m, "move 1588.0 1080.0 9.1", "unreachable: no navmesh route reaches it");
    Facts moved = h;
    moved.leadX = 1580.0f; // 9.6 yd west: another cell
    moved.approachX = 1590;
    moved.approachY = 1060;
    CHECK(Tick(m, moved) == "move 1590.0 1060.0 8.9");
    CHECK(m.evidenceX == 1580.0f);
    Facts away = moved; // no reading on this tick: the old cell's evidence is gone all the same
    away.leadIsland = false;
    away.leadX = 1570.0f;
    Run e = Start(&plan, 3);
    e.walkBack = true;
    e.walkBackIdx = 1;
    CHECK(Tick(e, g) == "move 1588.0 1080.0 9.1");
    CHECK(e.islandEvidence);
    EnterPhase(e, Phase::Begin);
    Tick(e, away);
    CHECK(!e.islandEvidence);
    // In the same cell but 2.7 yd from the first reading's spot: no second sign.
    Run n = Start(&plan, 3);
    n.walkBack = true;
    n.walkBackIdx = 1;
    CHECK(Tick(n, g) == "move 1588.0 1080.0 9.1");
    OrderRefused(n, "move 1588.0 1080.0 9.1", "unreachable: no navmesh route reaches it");
    Facts near = h;
    near.leadY = 1044.2f;
    near.approachX = 1600;
    near.approachY = 1060;
    CHECK(IslandCell(near.leadY) == IslandCell(1046.9f));
    CHECK(Tick(n, near) == "move 1600.0 1060.0 8.9");
    // That reading from 2.7 yd off does not replace the first: back on the first
    // spot, the reading toward the other point is the second sign.
    CHECK(n.evidenceY == 1046.9f);
    EnterPhase(n, Phase::Begin);
    Facts back = h;
    back.approachX = 1600;
    back.approachY = 1060;
    n.walkBackIdx = 2;
    CHECK(Tick(n, back).compare(0, 7, "escape ") == 0);
    // ... and a good read clears it.
    Run c = Start(&plan, 3);
    c.walkBack = true;
    c.walkBackIdx = 1;
    CHECK(Tick(c, g) == "move 1588.0 1080.0 9.1");
    Facts good = g;
    good.leadIsland = false;
    good.leadPathGood = true;
    Tick(c, good);
    CHECK(!c.islandEvidence);
}

// Opus on 269503e0: a walk back's hop that found no spot does not pause the
// run from an unchanged spot: the walk back goes on as before (the point passed).
static void TestWalkBackNoSpotPassesThePoint()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    r.walkBackRestarted = true;
    Facts g = OnIsland();
    g.walkBackRescue = false; // the pass itself (IslandWalkBackRescue off)
    std::string const order = Tick(r, g);
    CHECK(order.compare(0, 7, "escape ") == 0);
    OrderRefused(r, order, "no-spot: no spot within 10 yd");
    CHECK(r.noSpotKnown && r.state == State::Running);
    // Same spot: no second hop; the zero hop is no path and the point is passed.
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Running && r.walkBackIdx == 5 && r.note.find("on to the next") != std::string::npos);
    CHECK(r.islandEscapes == 1);
    // Off that spot (another island read 5 yd away): the mark is gone, the hop goes out again.
    Run q = Start(&plan, 5);
    q.walkBack = true;
    q.walkBackIdx = q.walkBackStart = 4;
    q.walkBackRestarted = true;
    std::string const first = Tick(q, g);
    OrderRefused(q, first, "no-spot: no spot within 10 yd");
    Facts there = g;
    there.leadX += 5.0f;
    CHECK(Tick(q, there).compare(0, 7, "escape ") == 0);
    // The mark goes once the lead bot leaves the spot, even when it comes back there.
    Run b = Start(&plan, 5);
    b.walkBack = true;
    b.walkBackIdx = b.walkBackStart = 4;
    b.walkBackRestarted = true;
    std::string const hop = Tick(b, g);
    OrderRefused(b, hop, "no-spot: no spot within 10 yd");
    Facts off = g;
    off.leadIsland = false;
    off.leadX += 5.0f;
    off.approachX = off.leadX + 20; // a hop elsewhere, not walked
    Tick(b, off);
    CHECK(!b.noSpotKnown);
    EnterPhase(b, Phase::Begin);
    CHECK(Tick(b, g).compare(0, 7, "escape ") == 0);
}

// Dire Maul North r31 slot 6: revived on the entrance island, the hop off it found no spot, and the walk back passed
// every point ('no walkable path toward the point of step N: on to the next') to a pause, four times. The walk back
// and the island hop both failed: the rescue teleports the lead bot to the safe point on the way it came
// (IslandWalkBackRescue), out of combat, once a step; refused kStrandTries times, the walk back goes on as before.
static void TestIslandWalkBackIsRescued()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 1;
    r.walkBackRestarted = true;
    Facts g = OnIsland();
    std::string const hop = Tick(r, g);
    CHECK(hop.compare(0, 7, "escape ") == 0);
    OrderRefused(r, hop, "no-spot: no spot within 10 yd");
    CHECK(r.noSpotKnown);
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Running && r.strandDue && r.strandIdx == 4 && r.walkBackIdx == 4);
    CHECK(r.note == "walk back: no walkable path toward the point of step 2 from a navmesh island the hop could not "
        "leave: the lead bot is teleported to a safe point on the way it came (step 5) out of combat");
    std::string const rescue = Tick(r, g);
    CHECK(rescue == "rescue from 1589.6 1046.9 8.9 to 1633.6 1130.7 9.9");
    // In a fight: no rescue is due (RescueAllowed), nothing is passed.
    Run c = Start(&plan, 5);
    c.walkBack = true;
    c.walkBackIdx = c.walkBackStart = 1;
    c.walkBackRestarted = true;
    std::string const chop = Tick(c, g);
    OrderRefused(c, chop, "no-spot: no spot within 10 yd");
    Facts fight = g;
    fight.anyInCombat = true;
    Tick(c, fight);
    CHECK(!c.strandDue && c.state == State::Running);
    // Refused every time: kStrandTries tries, then no more teleports: the walk back goes on as before (one more hop
    // off the island, then the points passed) and ends.
    OrderRefused(r, rescue, "failed: the near teleport was refused");
    std::uint32_t sent = 1;
    for (int i = 0; i < 60 && r.state == State::Running && r.walkBack; ++i)
    {
        std::string const order = Tick(r, g);
        if (order.compare(0, 7, "rescue ") == 0)
        {
            ++sent;
            OrderRefused(r, order, "failed: the near teleport was refused");
        }
        else if (order.compare(0, 7, "escape ") == 0)
            OrderRefused(r, order, "no-spot: no spot within 10 yd");
    }
    CHECK(sent == kStrandTries && r.strandTries == kStrandTries);
    CHECK(!r.walkBack && r.note.find("walked back to step 6") == 0);
    // Used on this step already: the point is passed as before.
    Run u = Start(&plan, 5);
    u.strandStep = 5;
    u.walkBack = true;
    u.walkBackIdx = u.walkBackStart = 1;
    u.walkBackRestarted = true;
    std::string const uhop = Tick(u, g);
    OrderRefused(u, uhop, "no-spot: no spot within 10 yd");
    Tick(u, g);
    CHECK(!u.strandDue && u.walkBackIdx == 2 && u.note.find("on to the next") != std::string::npos);
}

// Round 1 (2): IslandWalkBackRescue fires only while the lead bot still stands within kArriveYd of the spot where
// the island hop found no spot. Moved off it, the no-way point is passed as before.
static void TestIslandRescueNeedsTheNoSpotSpot()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 1;
    r.walkBackRestarted = true;
    Facts g = OnIsland();
    std::string const hop = Tick(r, g);
    CHECK(hop.compare(0, 7, "escape ") == 0);
    OrderRefused(r, hop, "no-spot: no spot within 10 yd");
    CHECK(r.noSpotKnown);
    Facts off = g;
    off.leadX += kArriveYd + 1.0f; // more than kArriveYd from the no-spot spot: the no-spot half is gone
    CHECK(!IslandWalkBackRescue(r, off, "no walkable path toward the point of step 2")); // ... even with noWay true
    CHECK(Tick(r, off).compare(0, 7, "escape ") == 0); // a hop again, not the rescue
    CHECK(r.state == State::Running && !r.strandDue && r.walkBackIdx == 1 && !r.noSpotKnown);
}
// With a real player in the party the run keeps walking back with him: the same eligible island/no-spot state
// passes the point as before (no strandDue); the bot-only control still rescues.
static void TestIslandWalkBackWithPlayerPassesThePoint()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 1;
    r.walkBackRestarted = true;
    Facts g = OnIsland();
    PlayerFact p;
    p.name = "Hypeer";
    p.commander = true;
    g.players.push_back(p);
    std::string const hop = Tick(r, g);
    CHECK(hop.compare(0, 7, "escape ") == 0);
    OrderRefused(r, hop, "no-spot: no spot within 10 yd");
    CHECK(r.noSpotKnown);
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Running && !r.strandDue && r.walkBackIdx == 2 &&
        r.note.find("on to the next") != std::string::npos);
    Run b = Start(&plan, 5);
    b.walkBack = true;
    b.walkBackIdx = b.walkBackStart = 1;
    b.walkBackRestarted = true;
    Facts f = OnIsland();
    std::string const bhop = Tick(b, f);
    OrderRefused(b, bhop, "no-spot: no spot within 10 yd");
    CHECK(Tick(b, f).empty());
    CHECK(b.state == State::Running && b.strandDue && b.walkBackIdx == 4);
}

// Round 3: an IslandWalkBackRescue armed bot-only, then a real player joins before the send: no rescue order goes
// out (no try spent); the point is passed as before, exactly as a refused rescue of that kind does (no safe point,
// a player, the latch used). The bot-only control still teleports.
static void TestJoinedPlayerCancelsTheIslandRescue()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 1;
    r.walkBackRestarted = true;
    Facts g = OnIsland();
    std::string const hop = Tick(r, g);
    CHECK(hop.compare(0, 7, "escape ") == 0);
    OrderRefused(r, hop, "no-spot: no spot within 10 yd");
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Running && r.strandDue && r.strandBotOnly && r.strandIdx == 4);
    PlayerFact p;
    p.name = "Hypeer";
    p.commander = true;
    g.players.push_back(p); // joins between the arm and the send
    CHECK(Tick(r, g).empty()); // no rescue order: he walks back with the party
    CHECK(r.state == State::Running && !r.strandDue && !r.strandBotOnly && !r.strandOn && r.strandTries == 0 &&
        r.walkBackIdx == 5 && r.note.find("on to the next") != std::string::npos);
    Run b = Start(&plan, 5);
    b.walkBack = true;
    b.walkBackIdx = b.walkBackStart = 1;
    b.walkBackRestarted = true;
    Facts f = OnIsland();
    std::string const bhop = Tick(b, f);
    OrderRefused(b, bhop, "no-spot: no spot within 10 yd");
    CHECK(Tick(b, f).empty());
    CHECK(Tick(b, f) == "rescue from 1589.6 1046.9 8.9 to 1633.6 1130.7 9.9"); // control: still teleports
    CHECK(b.strandOn && !b.strandDue && b.strandTries == 0);
}


// A good read between the hops gives fresh tries, but one step sends at most kStepEscapes.
static void TestStepEscapeCap()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    Facts g = OnIsland();
    Facts good = g;
    good.leadIsland = false;
    good.leadPathGood = true;
    good.approachX = 1600;
    good.approachY = 1080;
    std::uint32_t escapes = 0;
    for (int i = 0; i < 40 && r.state == State::Running; ++i)
    {
        std::string const order = Tick(r, g);
        if (order.compare(0, 7, "escape ") == 0)
        {
            ++escapes;
            Tick(r, g);
            Tick(r, g);   // the hop settles
            Tick(r, good); // a good read in between: fresh tries ...
            EnterPhase(r, Phase::Begin);
        }
    }
    CHECK(escapes == kStepEscapes);
    CHECK(r.state == State::Paused && r.note.find("navmesh island") != std::string::npos);
}


// Final Opus check on d7e94c3b: the probe. After a hop that found no spot, the
// walk back from that spot looped: a point refused from it set the island
// suspicion instead of being passed, the no-spot mark cleared it, and the move
// went out again every tick (3000 ticks at walk-back index 1). The driver here
// answers every hop "no-spot" and every move "unreachable", as from the island.
static void TestNoSpotWalkBackNeverSpins()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(1903, {7271});
    pull.y = 1014;
    pull.z = 11.8f;
    plan.steps = {Step(PlanStepKind::Move, 1603, 1043, 9.8f), Step(PlanStepKind::Move, 1588, 1080, 9.1f),
        Step(PlanStepKind::Move, 1584, 1088, 9.1f), Step(PlanStepKind::Move, 1633.6f, 1130.7f, 9.9f), pull};
    Run r = Start(&plan, 4);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    r.walkBackRestarted = true;
    std::uint32_t sameOrder = 0, longestRepeat = 0, moves = 0;
    std::string last;
    std::size_t lastIdx = 0;
    int endedAt = -1;
    for (int t = 0; t < 3000 && r.state == State::Running; ++t)
    {
        Facts f = OnIsland();
        const PlanStep* fs = FactStep(r);
        float const to = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, fs->x, fs->y, fs->z);
        f.seen = to <= kSeeYd;
        f.approachKnown = !f.seen;
        f.leadIsland = LeadOnIsland(false, true, 3.05f, to);
        f.goodSpotCut = false;
        f.walkBackRescue = false; // the pass bound itself (IslandWalkBackRescue off)
        std::string const order = Tick(r, f);
        if (!r.walkBack && endedAt < 0)
            endedAt = t;
        if (order.empty())
            continue;
        if (order == last && r.walkBackIdx == lastIdx)
            longestRepeat = std::max(longestRepeat, ++sameOrder);
        else
            sameOrder = 1;
        last = order;
        lastIdx = r.walkBackIdx;
        if (order.compare(0, 7, "escape ") == 0)
            OrderRefused(r, order, "no-spot: no spot within 10 yd");
        else if (order.compare(0, 5, "move ") == 0)
        {
            ++moves;
            OrderRefused(r, order, "unreachable: no navmesh route reaches it");
        }
    }
    CHECK(endedAt >= 0 && endedAt < 200); // every point passed, the walk back is over
    CHECK(longestRepeat <= 1); // each point refused once and passed (as on 2012dfee), never sent again
    CHECK(moves <= 3);
    CHECK(r.state == State::Paused); // the step's own approach then pauses (bounded)
}

// Sign (b) from the main mesh (two unreachable points read from one spot): one
// hop off per spot, at most kStepEscapes in the step, and the run ends bounded.
static void TestSignBFromTheMainMeshIsBounded()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(600, {1});
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 40, 30), Step(PlanStepKind::Move, 40, 60),
        Step(PlanStepKind::Move, 40, 90), pull};
    Run r = Start(&plan, 4);
    r.walkBack = true;
    r.walkBackRestarted = true;
    float leadX = 0.0f;
    std::uint32_t escapes = 0;
    std::vector<float> escapedFrom;
    for (int t = 0; t < 3000 && r.state == State::Running; ++t)
    {
        Facts f = Party(leadX);
        const PlanStep* fs = FactStep(r);
        float const to = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, fs->x, fs->y, fs->z);
        f.seen = to <= kSeeYd;
        f.approachKnown = !f.seen;
        f.approachX = leadX + 5;
        f.leadIsland = LeadOnIsland(false, true, 5.0f, to);
        std::string const order = Tick(r, f);
        if (order.compare(0, 7, "escape ") == 0)
        {
            ++escapes;
            for (float x : escapedFrom)
                CHECK(std::fabs(x - leadX) > kArriveYd); // one hop off per spot
            escapedFrom.push_back(leadX);
            leadX -= 6.0f; // the glue moved it 6 yd: another spot, another cell
        }
        else if (order.compare(0, 5, "move ") == 0 && f.seen)
            OrderRefused(r, order, "unreachable: no navmesh route reaches it");
    }
    CHECK(escapes >= 1 && escapes <= kStepEscapes);
    CHECK(r.state == State::Paused);
}


// The defensive bound on its own: a walk-back point tried more than
// kWalkBackResends times from one spot is passed, whatever brought it back
// (here a refusal handled elsewhere that left the point standing), in sight
// (its move) or out of sight (its way found missing).
static void TestWalkBackResendBound()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 200), PullStep(400, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    Facts f = Party(0);
    for (std::uint32_t i = 0; i < kWalkBackResends; ++i)
    {
        CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
        EnterPhase(r, Phase::Begin);
    }
    CHECK(Tick(r, f).empty());
    CHECK(r.walkBackIdx == 1 && r.note.find("tried 6 times from this spot: on to the next") != std::string::npos);
    // Out of sight, the way found missing each time without passing (the
    // suspicion kept standing): the point is passed after kWalkBackResends.
    Run o = Start(&plan, 2);
    o.walkBack = true;
    o.walkBackIdx = 1;
    o.walkBackStart = 1;
    o.walkBackRestarted = true;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = false;
    for (std::uint32_t i = 0; i < kWalkBackResends; ++i)
    {
        o.passSpotKnown = true; // a point given up here before, no good path: the suspicion
        o.passX = o.passY = o.passZ = 0.0f;
        o.noSpotKnown = true;   // ... and a no-spot mark that keeps it from a hop
        o.noSpotX = 50.0f;      // (not on this spot: PassWalkBackPoint does not pass)
        Tick(o, g);
        CHECK(o.walkBackIdx == 1 && o.state == State::Running);
        o.islandSuspect = false;
    }
    Tick(o, g);
    CHECK(o.walkBackIdx == 2);
}

// (b) Uldaman 19z11 02:50: the approach paused after 8 hops while the priest
// stood still on a navmesh pocket (43 371). Before the pause a far bot that
// stands still is summoned, once; a walker is not; then the pause (the lead
// bot's own way is the reason).
static void TestApproachHopSummonsBeforePause()
{
    DungeonPlan plan;
    plan.steps = {PullStep(300, {1}, ai::kPlanFlagNoDefer)}; // never deferred: it pauses (PullMayBeDeferred)
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    f.maxMemberYd = 15;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 150), Mem("Walky", 90)};
    f.members[2].yd = 10; // close enough for the pull's gather
    f.maxMemberYd = 150;
    r.approachHops = kMaxApproachHops;
    Run::StillState st;
    st.x = 150;
    st.ms = kStillMs;
    r.still["Fyran"] = st;
    std::string const order = Tick(r, f);
    CHECK(order == "summon Fyran");
    CHECK(r.state == State::Running && r.note.find("approach: Fyran") == 0);
    CHECK(StandTick(r, f).find("summon") == std::string::npos); // Walky moves: not summoned (no stand found)
    CHECK(r.state == State::Paused && r.note.find("approach hops") != std::string::npos);
}

// (c) Uldaman 19z11 02:48: only the lead bot died in the fight; its own
// release put it at the entrance 300 yd away while the others won. It is the
// wipe recovery: regroup, then the walk back over the plan's points. Bounded
// by kAutoRegroupsPerStep. A lead bot raised where it fell needs nothing.
static void TestLeadOnlyDeathRecovers()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.anyInCombat = true;
    Tick(r, f); // fighting, the lead bot alive at 210
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f);
    CHECK(r.leadDied && r.state == State::Running);
    // Back on its feet at the entrance, the fight won.
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.anyInCombat = false;
    f.leadX = -90;
    f.maxMemberYd = 300;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 305)};
    f.members[1].x = 215; // where the party won the fight, 5 yd from where the lead bot fell
    Tick(r, f);
    CHECK(r.regroup && r.walkBackDue && r.autoRegroups == 1 && r.state == State::Running);
    CHECK(r.note.find("the lead bot died and came back") == 0);
    // The living party holds; the lead bot walks back to it, nobody is
    // gathered on (or summoned to) the entrance.
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran");
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -50;
    std::string order;
    for (int i = 0; i < 3; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("summon") != 0 && order != "move -90.0 0.0 0.0");
    }
    CHECK(!r.regroup && r.walkBack && r.hold.size() == 1);
    // Back with it: the hold ends.
    f.leadX = 200;
    f.members[1].yd = 15;
    Tick(r, f);
    CHECK(r.hold.empty());
    // Bounded: past kAutoRegroupsPerStep it pauses.
    Run p = Start(&plan, 2);
    p.autoRegroups = kAutoRegroupsPerStep;
    Facts g = Party(210);
    g.anyInCombat = true;
    Tick(p, g);
    g.leadAlive = false;
    g.dead = 1;
    g.alive = 4;
    Tick(p, g);
    g.leadAlive = true;
    g.dead = 0;
    g.alive = 5;
    g.anyInCombat = false;
    g.leadX = -90;
    Tick(p, g);
    CHECK(p.state == State::Paused && p.note.find("came back") != std::string::npos);
    // Raised where it fell (a resurrection in the party): no regroup of its own.
    Run q = Start(&plan, 2);
    Facts h = Party(210);
    h.anyInCombat = true;
    Tick(q, h);
    h.leadAlive = false;
    h.dead = 1;
    h.alive = 4;
    Tick(q, h);
    h.leadAlive = true;
    h.dead = 0;
    h.alive = 5;
    h.anyInCombat = false;
    Tick(q, h);
    CHECK(!q.regroup && q.autoRegroups == 0 && !q.leadDied);
}

// The owner's rule (2026-10-17): "Party should wait if a party member dies
// until it is back." A death in a walk holds the walk; the bot alive again but
// far is waited for (running back); within kGatherYd the step goes on.
static void TestDeathWhileWalkingBlocksTheStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for Fyran (dead)") == 0);
    // Released, ran back, alive at the entrance: still waited for.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.maxMemberYd = 90;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 90)};
    std::string order;
    for (int i = 0; i < 20; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("move 40") != 0);
    }
    CHECK(r.regroup && r.note.find("Fyran (running back)") != std::string::npos);
    // Back with the party: the walk goes on.
    f.maxMemberYd = 5;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 5)};
    for (int i = 0; i < 5 && order.find("move 40") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "move 40.0 0.0 0.0" && !r.regroup && r.waitFor.empty());
}

// The lead bot dead out of a fight: the party waits for it (no pause).
static void TestLeadDeathBlocks()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, f).find("pullback") != 0);
    CHECK(r.state == State::Running && r.regroup && r.note.find("Tanky (dead)") != std::string::npos);
}

// Events cannot be left mid-way: the wait comes after the event.
static void TestWaitRuleEventException()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 80)};
    Run r = Start(&plan);
    r.eventOn = true;
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.regroup && r.waitFor.count("Fyran"));
    r.eventOn = false; // the event is over
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for Fyran (dead)") == 0);
}

// N1 (Uldaman 19z11: 17 of 63 pulls with a member dead): the pull itself
// holds while anyone is dead, outside an event.
static void TestNoPullWithADeadMember()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    std::string order;
    PullUnit(r, f, f.units[0], order);
    CHECK(order.empty() && r.note.find("no pull while a party member is dead") != std::string::npos);
    for (int i = 0; i < 40; ++i)
        CHECK(Tick(r, f).find("pullback") != 0);
    // Inside an event the pull goes out (it cannot wait).
    Run e = Start(&plan);
    e.eventOn = true;
    order.clear();
    PullUnit(e, f, f.units[0], order);
    CHECK(order == "pullback 7");
}

// N3 (Uldaman 01:43, 02:31): three dead, the two rogues still fighting. The
// regroup neither revives nor gathers nor walks while a living bot fights:
// the fight first, then the regroup.
static void TestRegroupWaitsForTheFight()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(100, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(100);
    f.dead = 3;
    f.alive = 2;
    f.deadNames = {"Tanky", "Fyran", "Iront"};
    f.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order.empty() || order.find("focus") == 0 || order.find("cc ") == 0);
    }
    CHECK(r.state == State::Running && r.note.find("fighting on") != std::string::npos);
    // A regroup already going (a resume) holds in a fight too.
    Run g = Start(&plan, 1);
    BeginRegroup(g);
    Facts h = Party(100);
    h.dead = 1;
    h.alive = 4;
    h.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(g, h).find("revive") != 0);
    h.anyInCombat = false;
    CHECK(Tick(g, h) == "revive party");
}

// A death during the regroup's gather: back to reviving.
static void TestDeathDuringRegroup()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.maxMemberYd = 60;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 60)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    f.members = {Mem("Tanky", 0)};
    f.maxMemberYd = 0;
    CHECK(Tick(r, f) == "revive party");
    CHECK(r.regroup && r.note.find("Fyran (dead)") != std::string::npos);
}

// Review of #1060: the common case, the fight ends while the lead bot is
// still a ghost. The regroup revives it; once it is up at the entrance the
// living party deep inside holds (no gather to the entrance, no summon there).
static void TestLeadDeadAfterTheFightHolds()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.anyInCombat = true;
    Tick(r, f); // fighting, the lead bot alive at 210
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f); // it falls in the fight
    // The fight is over, the lead bot still dead: the regroup, its hold armed.
    f.anyInCombat = false;
    f.members = {Mem("Fyran", 5)};
    f.members[0].x = 215;
    f.maxMemberYd = 0;
    Tick(r, f);
    CHECK(r.regroup && r.holdDue && r.walkBackDue && r.autoRegroups == 1 && r.hold.empty());
    CHECK(Tick(r, f) == "revive party");
    // Up at the entrance, 300 yd from where it fell.
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.leadX = -90;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 305)};
    f.members[1].x = 215;
    f.maxMemberYd = 305;
    CHECK(Tick(r, f).empty());
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran" && !r.holdDue);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -50;
    for (int i = 0; i < 20; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order.find("summon") != 0 && order != "move -90.0 0.0 0.0");
    }
    CHECK(r.state == State::Running && !r.regroup && r.walkBack && r.hold.size() == 1);
    // A player's order pauses the run: the hold ends (the glue lets them go).
    CHECK(PauseForPlayer(r, "follow"));
    CHECK(r.hold.empty() && !r.holdDue);
    // Raised where it fell (a resurrection in the party): no hold.
    Run q = Start(&plan, 2);
    Facts h = Party(210);
    h.anyInCombat = true;
    Tick(q, h);
    h.leadAlive = false;
    h.dead = 1;
    h.alive = 4;
    Tick(q, h);
    h.anyInCombat = false;
    h.members = {Mem("Fyran", 30)};
    h.members[0].x = 240;
    Tick(q, h);
    CHECK(q.holdDue);
    h.leadAlive = true;
    h.dead = 0;
    h.alive = 5;
    Tick(q, h);
    CHECK(q.hold.empty() && !q.holdDue);
}

// The run's own time limit and the stop note: the 3 h cap used to read as a plain stop.
static void TestRunTimeLimitAndStopNote()
{
    CHECK(kRunMaxMinutesDefault == 180);
    CHECK(!RunTimeLimitReached(0, 180));
    CHECK(!RunTimeLimitReached(180ull * 60000ull - 1, 180));
    CHECK(RunTimeLimitReached(180ull * 60000ull, 180));
    CHECK(RunTimeLimitReached(4000ull * 3600000ull, 180));
    CHECK(!RunTimeLimitReached(4000ull * 3600000ull, 0)); // 0 = no limit
    CHECK(RunTimeLimitNote(180) == "time limit reached (3h)");
    CHECK(RunTimeLimitNote(60) == "time limit reached (1h)");
    CHECK(RunTimeLimitNote(90) == "time limit reached (90 min)");

    CHECK(RunStopNote("") == "stopped by .bot run stop");
    CHECK(RunStopNote("   ") == "stopped by .bot run stop");
    CHECK(RunStopNote("time limit reached (3h)") == "stopped: time limit reached (3h)");
    CHECK(RunStopNote("a|b  c") == "stopped: a b c"); // no protocol separator, spaces squeezed
    CHECK(RunStopNote(std::string(300, 'x')).size() == std::string("stopped: ").size() + kRunStopReasonMax);
}

// `.bot run start [plan] [from ...]`.
static DungeonPlan TwoLegPlan()
{
    DungeonPlan plan;
    plan.name = "test";
    plan.map = 70;
    plan.legs.push_back(ai::PlanLeg{0, "entrance"});
    plan.legs.push_back(ai::PlanLeg{3, "Trogg_West"});
    for (std::uint8_t leg : {0, 0, 3, 3, 3})
    {
        PlanStep st = Step(PlanStepKind::Move, 1, 2, 3);
        st.leg = leg;
        plan.steps.push_back(st);
    }
    return plan;
}

static void TestStartFromParsing()
{
    StartArgs a = ParseStartArgs("");
    CHECK(a.ok && a.plan.empty() && a.from == StartFrom::Beginning);
    a = ParseStartArgs("uldaman");
    CHECK(a.ok && a.plan == "uldaman" && a.from == StartFrom::Beginning);
    a = ParseStartArgs("uldaman from 4");
    CHECK(a.ok && a.plan == "uldaman" && a.from == StartFrom::Leg && a.value == "4");
    a = ParseStartArgs("uldaman from leg trogg_west");
    CHECK(a.ok && a.from == StartFrom::Leg && a.value == "trogg_west");
    a = ParseStartArgs("uldaman from step 57");
    CHECK(a.ok && a.from == StartFrom::Step && a.value == "57");
    a = ParseStartArgs("from step 2"); // the lead bot's map names the plan
    CHECK(a.ok && a.plan.empty() && a.from == StartFrom::Step && a.value == "2");
    a = ParseStartArgs("  uldaman   from   leg   baelog ");
    CHECK(a.ok && a.plan == "uldaman" && a.value == "baelog");
    CHECK(!ParseStartArgs("uldaman from").ok);
    CHECK(!ParseStartArgs("uldaman from step").ok);
    CHECK(!ParseStartArgs("uldaman from 3 4").ok);
    CHECK(!ParseStartArgs("uldaman two from 3").ok);
    CHECK(!ParseStartArgs("uldaman fromm 3").ok);
}

static void TestStartFromResolve()
{
    DungeonPlan plan = TwoLegPlan();
    std::size_t step = 99;
    CHECK(ResolveStartStep(plan, StartFrom::Beginning, "", step) == FromResult::Ok && step == 0);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "3", step) == FromResult::Ok && step == 2); // first step of leg 3
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "0", step) == FromResult::Ok && step == 0);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "trogg_west", step) == FromResult::Ok && step == 2); // name, any case
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "7", step) == FromResult::NoSuchLeg);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "nowhere", step) == FromResult::NoSuchLeg);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "1", step) == FromResult::Ok && step == 0);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "5", step) == FromResult::Ok && step == 4);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "6", step) == FromResult::NoSuchStep);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "0", step) == FromResult::NoSuchStep);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "x", step) == FromResult::NoSuchStep);

    // The run begins on that step, running, with the usual first-step state.
    Run r = Start(&plan, 2);
    CHECK(r.step == 2 && r.state == State::Running);
}

// The Uldaman Keepers: the ritual ends leg 10, the hold opens leg 11. A start on the hold goes back
// to the ritual, or the event never starts.
static DungeonPlan EventPlan()
{
    DungeonPlan plan;
    plan.name = "event";
    plan.map = 70;
    plan.legs.push_back(ai::PlanLeg{10, "keepers"});
    plan.legs.push_back(ai::PlanLeg{11, "archaedas"});
    struct Row { std::uint8_t leg; PlanStepKind kind; };
    for (Row row : {Row{10, PlanStepKind::Move}, Row{10, PlanStepKind::Move}, Row{10, PlanStepKind::Ritual},
                    Row{11, PlanStepKind::Hold}, Row{11, PlanStepKind::Move}, Row{11, PlanStepKind::Pull},
                    Row{11, PlanStepKind::Move}, Row{11, PlanStepKind::Open}, Row{11, PlanStepKind::Move},
                    Row{11, PlanStepKind::Hold}})
    {
        PlanStep st = Step(row.kind, 1, 2, 3);
        st.leg = row.leg;
        plan.steps.push_back(st);
    }
    return plan;
}

static void TestStartInsideEventGoesBack()
{
    DungeonPlan plan = EventPlan();
    std::size_t step = 99;
    bool back = false;
    // Leg 11 begins on the hold (step index 3): back to the ritual (index 2).
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "11", step, &back) == FromResult::Ok && step == 2 && back);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "archaedas", step, &back) == FromResult::Ok && step == 2 && back);
    // A step number on the hold (step 4), and on the moves before the ritual (steps 1, 2) leading into... the ritual.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "4", step, &back) == FromResult::Ok && step == 2 && back);
    // The ritual itself and the leg before it stay as asked.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "3", step, &back) == FromResult::Ok && step == 2 && !back);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "10", step, &back) == FromResult::Ok && step == 0 && !back);
    // A move that leads into the second hold (index 8) goes back to the open step (index 7); the hold itself too.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "9", step, &back) == FromResult::Ok && step == 7 && back);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "10", step, &back) == FromResult::Ok && step == 7 && back);
    // A pull in the middle of the leg is not an event start.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "6", step, &back) == FromResult::Ok && step == 5 && !back);
    // A move that leads into no hold stays put.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "7", step, &back) == FromResult::Ok && step == 6 && !back);
    // A hold with no ritual/open/use_item before it (only moves, then the plan's start) stays put.
    DungeonPlan bare;
    bare.map = 70;
    for (PlanStepKind k : {PlanStepKind::Move, PlanStepKind::Hold})
        bare.steps.push_back(Step(k, 1, 2, 3));
    CHECK(EventStartStep(bare, 1) == 1 && EventStartStep(bare, 0) == 0);
    // The run that starts there begins with the ritual, not the hold.
    Run r = Start(&plan, 2);
    CHECK(Current(r)->kind == PlanStepKind::Ritual && EventHoldAhead(r) != nullptr);
}

static void TestPartialStartGuard()
{
    CHECK(CheckPartialStart(true, 5, 5) == PartialStartVerdict::Ok);
    CHECK(CheckPartialStart(false, 5, 5) == PartialStartVerdict::NotInstance);
    CHECK(CheckPartialStart(true, 5, 4) == PartialStartVerdict::PartyScattered);
    CHECK(CheckPartialStart(true, 0, 0) == PartialStartVerdict::Ok);
}


// ---- Exact waypoints (ai::kPlanFlagExact, ExactWalkTo) ----

static PlanStep ExactStep(float x, float y, float z)
{
    PlanStep s;
    s.kind = PlanStepKind::Move;
    s.x = x;
    s.y = y;
    s.z = z;
    s.flags = ai::kPlanFlagExact;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}

// The hop after an exact point: a move_hop_exact row (exactLaunch).
static PlanStep DirectExact(float x, float y, float z)
{
    PlanStep s = Step(PlanStepKind::MoveDirect, x, y, z);
    s.exactLaunch = true;
    return s;
}

// The Wailing Caverns leg 10 rows (export of 2026-10-18): three path rows, the
// exact foot 2.25 yd from the row before it, the direct hop down to the
// Serpentis ramp (47 yd drop under it), then a rest.
static DungeonPlan SerpentisPlan()
{
    DungeonPlan plan;
    plan.name = "wailing_caverns";
    plan.steps = {Step(PlanStepKind::Move, -308.6239f, -32.13346f, -60.944225f),
        Step(PlanStepKind::Move, -292.80005f, -3.4667964f, -58.160522f),
        Step(PlanStepKind::Move, -292.00006f, -1.3334637f, -57.860523f),
        ExactStep(-293.3f, 0.5f, -58.0f),
        DirectExact(-287.5f, 5.0f, -63.6f),
        Step(PlanStepKind::Rest)};
    return plan;
}

static bool IsDirectOrder(const std::string& order)
{
    return order.find("direct") != std::string::npos;
}

// The Serpentis case: the lead bot stands 2.3 yd from the foot (within the plain
// move's 3 yd). Before the exact rule the foot counted as reached at once and the
// direct hop went out from there. Now: no direct order until the proof; the lead
// bot (too near for a move onto the point) walks back to the first plan point at
// least 6.5 yd behind, comes onto the foot, stands 2 s, and only then the hop.
static void TestExactSerpentisFootIsNotSkipped()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 3);
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadY = 0.5f - 2.3f;
    f.leadZ = -58.0f;
    std::string back;
    for (int i = 0; i < 6 && back.empty(); ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(!IsDirectOrder(order));
        CHECK(r.step == 3 && r.state == State::Running);
        back = order;
    }
    // The rows 1 and 2 are within 6.5 yd of the foot on the ground plane: row 0.
    CHECK(back == "move -308.6 -32.1 -60.9");
    // Walking there: no order over the move.
    f.leadMoving = true;
    f.leadX = -300.0f;
    f.leadY = -15.0f;
    CHECK(Tick(r, f).empty());
    // Arrived and standing: after 3 s, onto the foot.
    f.leadMoving = false;
    f.leadX = -308.6f;
    f.leadY = -32.1f;
    f.leadZ = -60.9f;
    std::string onto;
    for (int i = 0; i < 5 && onto.empty(); ++i)
    {
        onto = Tick(r, f);
        CHECK(!IsDirectOrder(onto) && r.step == 3);
    }
    CHECK(onto == "move -293.3 0.5 -58.0");
    // On the foot but still moving: no proof.
    f.leadX = -293.2f;
    f.leadY = 0.4f;
    f.leadZ = -58.4f;
    f.leadMoving = true;
    for (int i = 0; i < 4; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(r.step == 3);
    }
    // Standing: three looks on one spot (2 s), then the step is done ...
    f.leadMoving = false;
    CHECK(Tick(r, f).empty() && r.step == 3);
    CHECK(Tick(r, f).empty() && r.step == 3);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 4 && r.exactProved == 3 && r.note.find("exact point") != std::string::npos);
    // ... and the direct hop goes out from the foot.
    CHECK(Tick(r, f) == "move -287.5 5.0 -63.6 direct");
}

// A lead bot that never comes onto the point: bounded, and a pause with a clear
// note; the direct step never goes out.
static void TestExactNeverProvedPauses()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 3);
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadY = 0.5f - 2.3f;
    f.leadZ = -58.0f;
    int ticks = 0;
    for (; ticks < 200 && r.state == State::Running; ++ticks)
        CHECK(!IsDirectOrder(Tick(r, f)));
    CHECK(r.state == State::Paused && r.step == 3);
    CHECK(ticks <= static_cast<int>(kExactCapMs / kTickMs) + 2);
    CHECK(r.note.find("exact point of step 4") != std::string::npos);
    CHECK(r.note.find("not proved") != std::string::npos);
    CHECK(r.note.find(".bot run resume") != std::string::npos);
}

// 1.5-3 yd away with no plan point behind it to walk back to: the lead bot is
// sent onto the point again (a re-issued move), never counted as arrived by the
// 3 yd rule, nor by the stalled arrival within 6 yd.
static void TestExactReissuesTheMoveAndIgnoresTheStallRule()
{
    DungeonPlan plan;
    plan.steps = {ExactStep(10, 0, 0), Step(PlanStepKind::MoveDirect, 15, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(12.3f);
    std::vector<std::string> orders;
    for (int i = 0; i < 8; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(!IsDirectOrder(order) && r.step == 0);
        if (!order.empty())
            orders.push_back(order);
    }
    CHECK(!orders.empty() && orders[0] == "move 10.0 0.0 0.0");
    // A stall 4.5 yd short (the plain walk's 3 s / 6 yd arrival): not done either.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    CHECK(Tick(r2, g) == "move 10.0 0.0 0.0");
    g.leadX = 5.5f;
    for (int i = 0; i < 10; ++i)
    {
        CHECK(!IsDirectOrder(Tick(r2, g)));
        CHECK(r2.step == 0);
    }
}

// A direct step launched from an exact point checks the proof itself: started
// (or resumed) at the direct step with the lead bot 2.3 yd off the foot, the hop
// waits for the tighten; on the foot and still, it goes out.
static void TestDirectFromExactPointWaitsForTheProof()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 4);
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadY = 0.5f - 2.3f;
    f.leadZ = -58.0f;
    for (int i = 0; i < 6; ++i)
        CHECK(!IsDirectOrder(Tick(r, f)));
    Run r2 = Start(&plan, 4);
    f.leadY = 0.4f;
    std::string order;
    for (int i = 0; i < 6 && order.empty(); ++i)
        order = Tick(r2, f);
    CHECK(order == "move -287.5 5.0 -63.6 direct");
    // A stand-in point (detour) is never taken for the exact point nor its hop.
    Run r3 = Start(&plan, 3);
    f.leadX = -320.0f;
    f.leadY = -40.0f;
    Tick(r3, f);
    CHECK(!UseDetour(r3, -295.0f, 0.5f, -58.0f));
}

// A walk back after a regroup replays the exact point as its step does: the
// foot is not passed by the 3 yd rule, and the hop waits for its proof.
static void TestWalkBackReplaysTheExactPoint()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 3;
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadY = 0.5f - 2.3f;
    f.leadZ = -58.0f;
    std::string back;
    for (int i = 0; i < 7 && (back.empty() || back == "rest"); ++i) // the rest step's gate asks for a rest first
    {
        back = Tick(r, f);
        CHECK(!IsDirectOrder(back) && r.walkBackIdx == 3 && r.walkBack);
    }
    CHECK(back == "move -308.6 -32.1 -60.9");
    // Skipping the foot's replay is caught by the hop's own check.
    Run r2 = Start(&plan, 5);
    r2.walkBack = true;
    r2.walkBackIdx = r2.walkBackStart = 4;
    for (int i = 0; i < 7; ++i)
        CHECK(!IsDirectOrder(Tick(r2, f)));
    // On the foot and still: the hop is replayed.
    f.leadY = 0.4f;
    Run r3 = Start(&plan, 5);
    r3.walkBack = true;
    r3.walkBackIdx = r3.walkBackStart = 3;
    std::string order;
    for (int i = 0; i < 8 && !IsDirectOrder(order); ++i)
        order = Tick(r3, f);
    CHECK(order == "move -287.5 5.0 -63.6 direct" && r3.walkBackIdx == 4);
}

// Brings a Serpentis run to the hop (step 4) with the foot proved; the lead
// bot stands on the foot.
static Run ProvedAtTheFoot(const DungeonPlan& plan, Facts& f)
{
    Run r = Start(&plan, 3);
    f = Party(0);
    f.leadX = -293.2f;
    f.leadY = 0.4f;
    f.leadZ = -58.4f;
    for (int i = 0; i < 6 && r.step == 3; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.step == 4 && r.exactProved == 3);
    return r;
}

// Proved, then the lead bot is 3 yd off the foot before the hop goes out (a
// knock, a player's move): no direct order; the tighten runs again.
static void TestHopRechecksTheLeadOnTheFoot()
{
    DungeonPlan plan = SerpentisPlan();
    Facts f;
    Run r = ProvedAtTheFoot(plan, f);
    f.leadY = 0.5f - 3.0f;
    f.leadX = -293.3f;
    f.leadZ = -58.0f;
    for (int i = 0; i < 4; ++i)
        CHECK(!IsDirectOrder(Tick(r, f)));
    CHECK(r.step == 4 && r.exactProved != 3);
}

// A lead bot that drifts 1 yd a tick while staying within 1.5 yd of the point,
// with the moving fact false, is never proved.
static void TestDriftWithinTheRadiusIsNoProof()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 3);
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadZ = -58.0f;
    for (int i = 0; i < 10; ++i)
    {
        f.leadY = (i % 2 == 0) ? 0.0f : 1.0f; // 0.5 yd either side of the foot, 1 yd per tick
        CHECK(!IsDirectOrder(Tick(r, f)));
        CHECK(r.step == 3);
    }
    // ... nor one that swings 0.9 yd a tick about one spot (each look within
    // 0.5 yd of it: only the tick-to-tick move shows it) ...
    Run r2 = Start(&plan, 3);
    for (int i = 0; i < 10; ++i)
    {
        f.leadY = 0.5f + ((i == 0) ? 0.0f : (i % 2 ? 0.45f : -0.45f));
        CHECK(!IsDirectOrder(Tick(r2, f)));
        CHECK(r2.step == 3);
    }
    // ... nor one that creeps 0.4 yd a tick across the point (each tick under
    // 0.5 yd: only the distance from the first spot shows it).
    Run r3 = Start(&plan, 3);
    for (int i = 0; i < 7; ++i)
    {
        f.leadY = 0.5f - 1.2f + 0.4f * static_cast<float>(i);
        CHECK(!IsDirectOrder(Tick(r3, f)));
        CHECK(r3.step == 3);
    }
}

// After a fight, and after a pause and resume, the lead bot still on the foot:
// the proof is taken again (three looks) before the hop goes out.
static void TestProofIsRetakenAfterAFightAndAResume()
{
    DungeonPlan plan = SerpentisPlan();
    Facts f;
    Run r = ProvedAtTheFoot(plan, f);
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    int quiet = 0;
    std::string order;
    for (int i = 0; i < 8 && !IsDirectOrder(order); ++i)
    {
        order = Tick(r, f);
        if (!IsDirectOrder(order))
            ++quiet;
    }
    CHECK(order == "move -287.5 5.0 -63.6 direct");
    CHECK(quiet >= 3);

    Run r2 = ProvedAtTheFoot(plan, f);
    Pause(r2, "by hand");
    CHECK(Resume(r2));
    quiet = 0;
    order.clear();
    for (int i = 0; i < 12 && !IsDirectOrder(order); ++i)
    {
        order = Tick(r2, f);
        if (!IsDirectOrder(order))
            ++quiet;
    }
    CHECK(order == "move -287.5 5.0 -63.6 direct");
    CHECK(quiet >= 3);
}

// The walk back to a point behind an exact point never goes back over a direct
// step: with a hole behind, the lead bot is sent onto the point instead.
static void TestBackPointStopsAtADirectStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, -30, 0, 0), Step(PlanStepKind::MoveDirect, -8, 0, 0),
        Step(PlanStepKind::Move, -2, 0, 0), ExactStep(0, 0, 0), DirectExact(5, 5, -5)};
    for (int which = 0; which < 2; ++which)
    {
        plan.steps[1].exactLaunch = false;
        if (which == 1)
        {
            // ... a move_hop_exact behind too (with its own exact point before it).
            plan.steps[0].flags = ai::kPlanFlagExact;
            plan.steps[1].exactLaunch = true;
        }
        Run r = Start(&plan, 3);
        Facts f = Party(0);
        f.leadX = 2.3f;
        std::string first;
        for (int i = 0; i < 6 && first.empty(); ++i)
            first = Tick(r, f);
        CHECK(first == "move 0.0 0.0 0.0");
    }
}

// A move_hop_exact with no exact move right before it is not sent: the run
// pauses (at the step and on a walk back). Then a narrow check of the rows an
// older module would load: with the hop's row skipped (the parser test in
// test_dungeon_plan_policy.cpp shows the old kind parser returns None for it)
// and the flag ignored, the plan holds no direct step, and THIS runtime run on
// those rows emits no direct order. It does not model the old runtime, and it
// says nothing about where its plain move to seq 5 ends: that deploy state is
// unsupported (the migration and the module ship together).
static void TestBrokenHopPausesAndSkippedHopRowHasNoDirectOrder()
{
    DungeonPlan plan = SerpentisPlan();
    plan.steps[3].flags = 0; // the foot without its flag
    Run r = Start(&plan, 4);
    Facts f = Party(0);
    f.leadX = -293.2f;
    f.leadY = 0.4f;
    f.leadZ = -58.4f;
    CHECK(!IsDirectOrder(Tick(r, f)));
    CHECK(r.state == State::Paused && r.note.find("move_hop_exact") != std::string::npos);
    // ... on a walk back too.
    Run w = Start(&plan, 5);
    w.walkBack = true;
    w.walkBackIdx = w.walkBackStart = 4;
    for (int i = 0; i < 4 && w.state == State::Running; ++i)
        CHECK(!IsDirectOrder(Tick(w, f)));
    CHECK(w.state == State::Paused && w.note.find("walk back") != std::string::npos &&
        w.note.find("move_hop_exact") != std::string::npos);

    DungeonPlan old = SerpentisPlan();
    old.steps[3].flags = 0;                  // the flag dropped, as an older module ignores it
    old.steps.erase(old.steps.begin() + 4);  // the hop's row skipped, as an older loader does (UnknownKind)
    old.steps.insert(old.steps.begin() + 4, Step(PlanStepKind::Move, -270.9f, 24.3f, -60.0f)); // seq 5, on the ramp
    Run o = Start(&old, 0);
    Facts g = Party(0);
    g.leadX = -320.0f;
    g.leadY = -40.0f;
    g.leadZ = -61.0f;
    for (int i = 0; i < 120 && o.state == State::Running; ++i)
        CHECK(!IsDirectOrder(Tick(o, g)));
}

// AiPlayerbot.DungeonRunAggroEstimate: a pull expected to bring more joiners
// than the ceiling waits once, on the rest wait, for the party to be rested.
static UnitFact Joiners(std::uint64_t guid, std::uint32_t entry, float dist, std::uint32_t joiners, bool patrol = false)
{
    UnitFact u = Unit(guid, entry, dist);
    u.joiners = joiners;
    u.joinersPatrol = patrol;
    return u;
}

static void TestAggroEstimateOffChangesNothing()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852}), PullStep(40, {4851})};
    Run plain = Start(&plan);
    Run off = Start(&plan);
    Facts a = Party(0);
    a.units = {Unit(31, 4852, 54.7f)};
    a.minHealthPct = 85;
    Facts b = a;
    b.units = {Joiners(31, 4852, 54.7f, 40, true)}; // the estimate is read, the ceiling is off
    CHECK(b.joinerCeiling == 0);
    for (int i = 0; i < 40; ++i)
    {
        std::string const oa = Tick(plain, a);
        std::string const ob = Tick(off, b);
        CHECK(oa == ob);
        CHECK(plain.note == off.note && plain.phase == off.phase && plain.step == off.step);
        CHECK(StatusLine(plain) == StatusLine(off));
        CHECK(!off.joinerRest && off.joinerHeld.empty());
        if (i == 3)
        {
            a.anyInCombat = b.anyInCombat = true;
            a.units.clear();
            b.units.clear();
        }
        if (i == 5)
            a.anyInCombat = b.anyInCombat = false;
    }
}

static void TestAggroEstimateHoldsOnceForARest()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Facts f = Party(0);
    f.joinerCeiling = 6;
    f.units = {Joiners(31, 4852, 54.7f, 7)};
    f.minHealthPct = 85; // above the start lines, under the end lines
    // Over the ceiling: a note, no order; then the rest wait; then the pull.
    Run r = Start(&plan);
    CHECK(Tick(r, f).empty());
    CHECK(r.note == "7 joiners expected" && r.joinerHeld.count(31));
    CHECK(r.phase != Phase::AwaitPull && r.pulls == 0);
    CHECK(Tick(r, f) == "rest");
    CHECK(r.phase == Phase::Rest && r.note == "resting: 7 joiners expected");
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).find("pullback") == std::string::npos);
    f.minHealthPct = 100;
    std::string order;
    for (int i = 0; i < 5 && order.find("pullback") == std::string::npos; ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 31");
    // Only once per unit: after a fruitless pull the same unit goes out again at once.
    CHECK(r.joinerHeld.size() == 1 && !r.joinerRest);

    // Already rested: the note, then the pull, no rest order.
    Run e = Start(&plan);
    CHECK(Tick(e, f).empty());
    CHECK(e.note == "7 joiners expected");
    CHECK(Tick(e, f) == "pullback 31");
    CHECK(e.phase == Phase::AwaitPull);

    // At the ceiling is not over it, and 0 expected joiners never waits.
    f.minHealthPct = 85;
    f.units = {Joiners(31, 4852, 54.7f, 6)};
    Run at = Start(&plan);
    CHECK(Tick(at, f) == "pullback 31");
    f.units = {Joiners(31, 4852, 54.7f, 0)};
    Run none = Start(&plan);
    CHECK(Tick(none, f) == "pullback 31");

    // A patrol being the whole reason is said.
    f.units = {Joiners(31, 4852, 54.7f, 8, true)};
    Run patrol = Start(&plan);
    CHECK(Tick(patrol, f).empty());
    CHECK(patrol.note == "8 joiners expected (a patrol is the cause)");

    // Inside a scripted event the rest wait is not entered.
    f.units = {Joiners(31, 4852, 54.7f, 8)};
    Run ev = Start(&plan);
    ev.eventOn = true;
    CHECK(Tick(ev, f).empty());
    CHECK(Tick(ev, f) == "pullback 31");
    CHECK(ev.phase != Phase::Rest);

    // A fight that begins before the rest is due does not start one.
    Run fight = Start(&plan);
    CHECK(Tick(fight, f).empty());
    CHECK(fight.joinerRest);
    f.anyInCombat = true;
    CHECK(Tick(fight, f).empty());
    CHECK(!fight.joinerRest && fight.phase != Phase::Rest && fight.fighting);

    // A normal rest after a joiner rest counts as idle time again.
    {
        f.anyInCombat = false;
        f.units = {Joiners(31, 4852, 54.7f, 7)};
        f.minHealthPct = 85;
        Run x = Start(&plan);
        Tick(x, f);
        CHECK(Tick(x, f) == "rest" && x.restForJoiners);
        f.minHealthPct = 100;
        for (int i = 0; i < 3 && x.phase == Phase::Rest; ++i)
            Tick(x, f);
        CHECK(x.phase != Phase::Rest);
        f.minHealthPct = 55; // under the light start line too (LightRestAhead: a small pull)
        x.restDue = true;
        x.phase = Phase::Begin;
        CHECK(Tick(x, f) == "rest" && x.phase == Phase::Rest && !x.restForJoiners);
        f.minHealthPct = 85;
        // ... and so does a meal wait.
        Run y = Start(&plan);
        Tick(y, f);
        Tick(y, f);
        CHECK(y.restForJoiners && y.phase == Phase::Rest);
        y.phase = Phase::Begin;
        f.eating = 1;
        f.leadResting = true; // the tank eats (the light rest lines hold only for a seated lead bot)
        Tick(y, f);
        CHECK(y.phase == Phase::Rest && y.restForEating && !y.restForJoiners);
        f.eating = 0;
        f.leadResting = false;
    }

    // A step's reset (also a resume) forgets the hold.
    Run reset = Start(&plan);
    f.anyInCombat = false;
    CHECK(Tick(reset, f).empty() && reset.joinerRest && !reset.joinerHeld.empty());
    ResetStep(reset);
    reset.joinerRests = 2;
    ResetStep(reset);
    CHECK(!reset.joinerRest && reset.joinerHeld.empty() && reset.joinerNote.empty());
    CHECK(reset.joinerRests == 0 && !reset.restForJoiners);

    // The nearest unit is the one judged: a quiet pick goes out, and the
    // loud one is held when it is picked.
    f.anyInCombat = false;
    f.units = {Joiners(31, 4852, 54.7f, 9), Joiners(32, 4852, 30.0f, 1)};
    Run two = Start(&plan);
    CHECK(Tick(two, f) == "pullback 32");
}


// Six loud packs in one pull step: the joiner rests (60 s each when the party
// stays under the end lines) are not step idle time and are three a step, so
// the run neither pauses on the 300 s idle ceiling nor rests a fourth time.
static void TestJoinerRestsAreBudgetedAndNotIdleTime()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.joinerCeiling = 6;
    f.minHealthPct = 85; // above the start lines, under the end lines: it never gets rested
    std::vector<UnitFact> alive;
    for (std::uint64_t g = 31; g <= 36; ++g)
        alive.push_back(Joiners(g, 4852, 40.0f + static_cast<float>(g), 9));
    int rests = 0;
    std::vector<std::string> notes;
    for (std::size_t unit = 0; unit < 6; ++unit)
    {
        f.units = std::vector<UnitFact>(alive.begin() + static_cast<long>(unit), alive.end());
        f.anyInCombat = false;
        std::string order;
        Phase prev = r.phase;
        for (int i = 0; i < 400 && order.compare(0, 9, "pullback ") != 0; ++i)
        {
            order = Tick(r, f);
            if (r.phase == Phase::Rest && prev != Phase::Rest)
                ++rests;
            prev = r.phase;
            CHECK(r.state == State::Running);
        }
        CHECK(order == "pullback " + std::to_string(31 + unit));
        notes.push_back(r.note);
        f.anyInCombat = true;
        for (int i = 0; i < 3; ++i)
            Tick(r, f);
        CHECK(r.state == State::Running);
    }
    CHECK(rests == 3);
    CHECK(r.joinerRests == 3);
    CHECK(notes[3] == "9 joiners expected, rest budget used");
    CHECK(notes[5] == "9 joiners expected, rest budget used");
    CHECK(notes[0] != notes[3]);
    // 180 s of joiner rest are not idle time.
    CHECK(r.stepIdleMs < kStepIdleCapMs / 2);
}

// A loud pick the party is already rested for spends no rest slot: the three
// slots go to the rests that really happen later in the step.
static void TestJoinerRestSlotsAreSpentByRealRestsOnly()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.joinerCeiling = 6;
    std::vector<UnitFact> alive;
    for (std::uint64_t g = 31; g <= 36; ++g)
        alive.push_back(Joiners(g, 4852, 40.0f + static_cast<float>(g), 9));
    int rests = 0;
    std::vector<std::string> notes;
    for (std::size_t unit = 0; unit < 6; ++unit)
    {
        // The first two picks find the party full, the rest find it at 85 %.
        f.minHealthPct = unit < 2 ? 100 : 85;
        f.units = std::vector<UnitFact>(alive.begin() + static_cast<long>(unit), alive.end());
        f.anyInCombat = false;
        std::string order;
        Phase prev = r.phase;
        for (int i = 0; i < 400 && order.compare(0, 9, "pullback ") != 0; ++i)
        {
            order = Tick(r, f);
            if (r.phase == Phase::Rest && prev != Phase::Rest)
                ++rests;
            prev = r.phase;
            CHECK(r.state == State::Running);
        }
        CHECK(order == "pullback " + std::to_string(31 + unit));
        notes.push_back(r.note);
        if (unit == 1)
            CHECK(r.joinerRests == 0 && rests == 0);
        f.anyInCombat = true;
        for (int i = 0; i < 3; ++i)
            Tick(r, f);
    }
    CHECK(rests == 3 && r.joinerRests == 3);
    CHECK(notes[5] == "9 joiners expected, rest budget used");
    CHECK(notes[4] != notes[5]);
}

// Both keys on (AiPlayerbot.DungeonRunAggroEstimate and DungeonRunRezHold): a loud pull, a death in its
// fight, then the regroup, then the next loud pick. The joiner rest is a pull-step thing (cleared by
// ResetStep, kept by BeginRegroup, which a single death reaches without a ResetStep); the rez hold's clocks
// are a regroup thing (restarted by BeginRegroup, never by a step reset). Neither may reset the other.
static void TestAggroEstimateAndRezHoldDoNotCrossTheirResets()
{
    using namespace TortoiseBots::RezHold;
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.joinerCeiling = 6;
    f.rezHold = true;
    f.minHealthPct = 85; // under the end lines: the joiner rest really starts
    f.units = {Joiners(21, 4852, 40.0f, 9), Joiners(22, 4852, 45.0f, 9)};
    // 1. Loud pick 21: the joiner rest starts (not idle time), then the pull goes out.
    for (int i = 0; i < 4 && r.phase != Phase::Rest; ++i)
        Tick(r, f);
    CHECK(r.phase == Phase::Rest && r.restForJoiners && r.joinerRests == 1 && r.joinerHeld.count(21));
    f.minHealthPct = 100;
    std::string order;
    for (int i = 0; i < 20 && order.compare(0, 9, "pullback ") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 21");
    // 2. The fight: the mage (index 2) falls, 21 dies; 22 is still up. Out of combat the single death
    // starts the regroup with no ResetStep.
    f.anyInCombat = true;
    for (int i = 0; i < 3; ++i)
        Tick(r, f);
    f.dead = 1;
    f.alive = 4;
    f.units = {Joiners(22, 4852, 45.0f, 9)};
    f.rezMembers = RezRoster(true, 500);
    Tick(r, f);
    f.anyInCombat = false;
    for (int i = 0; i < 4 && !r.regroup; ++i)
        Tick(r, f);
    CHECK(r.regroup && r.regroupMs < 3 * kTickMs && r.rezAbleMs == 0 && r.reviveBaseMs == 0);
    // The regroup's restart left the step's joiner state alone: 21 was rested for, one slot is spent.
    CHECK(r.joinerRests == 1 && r.joinerHeld.count(21) == 1);
    // 3. The rez hold decides the regroup (well past the old 15 s) and the joiner rest does not move.
    std::uint32_t revivedAt = 0;
    std::uint32_t const idleBefore = r.stepIdleMs;
    for (std::uint32_t ms = kTickMs; ms <= 200000 && !revivedAt; ms += kTickMs)
    {
        if (Tick(r, f) == "revive party")
            revivedAt = ms;
        CHECK(r.joinerRests == 1 && r.joinerHeld.count(21) == 1 && !r.joinerRest);
    }
    CHECK(revivedAt >= kHoldMs && revivedAt <= kHoldMs + 4 * kTickMs);
    CHECK(r.stepIdleMs == idleBefore); // a regroup is not step idle time
    // The invariant in both directions: a step reset leaves the regroup clocks, a regroup start leaves
    // the joiner state.
    {
        Run c = r;
        c.rezAbleMs = 7000;
        c.reviveBaseMs = 9000;
        c.regroupMs = 11000;
        ResetStep(c);
        CHECK(c.rezAbleMs == 7000 && c.reviveBaseMs == 9000 && c.regroupMs == 11000);
        CHECK(c.joinerRests == 0 && c.joinerHeld.empty() && !c.restForJoiners);
        Run d = r;
        d.restForJoiners = true;
        BeginRegroup(d);
        CHECK(d.joinerRests == 1 && d.joinerHeld.count(21) == 1 && d.restForJoiners);
    }
    // 4. The party is raised: the regroup ends, the step goes on with 22. It is rested for (a second
    // slot, 21's kept), the pull goes out, and the whole of it was no step idle time.
    f.dead = 0;
    f.alive = 5;
    f.rezMembers = RezRoster(false, 500);
    f.minHealthPct = 85;
    for (int i = 0; i < 12 && r.regroup; ++i)
        Tick(r, f);
    CHECK(!r.regroup);
    for (int i = 0; i < 6 && r.phase != Phase::Rest; ++i)
        Tick(r, f);
    CHECK(r.phase == Phase::Rest && r.restForJoiners && r.joinerRests == 2);
    CHECK(r.joinerHeld.count(21) == 1 && r.joinerHeld.count(22) == 1);
    CHECK(r.note.find("9 joiners expected") != std::string::npos);
    f.minHealthPct = 100;
    order.clear();
    for (int i = 0; i < 20 && order.compare(0, 9, "pullback ") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 22");
    CHECK(r.state == State::Running && r.stepIdleMs < kStepIdleCapMs / 2);
    // 5. A wipe in a later step reset (ResetStep, then BeginRegroup): fresh joiner budget and fresh clocks.
    ResetStep(r);
    BeginRegroup(r);
    CHECK(r.joinerRests == 0 && r.joinerHeld.empty() && r.regroupMs == 0 && r.rezAbleMs == 0);
}

// Both keys off: a run fed the estimate's joiners and the rez roster (the glue reads neither with the keys
// off, but a stray value must change nothing) ticks exactly like one fed neither, through a loud pull, a
// death in its fight and the regroup, tick by tick.
static void TestBothKeysOffChangeNothing()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run plain = Start(&plan);
    Run noisy = Start(&plan);
    Facts a = Party(0);
    a.minHealthPct = 85;
    a.units = {Unit(21, 4852, 40.0f), Unit(22, 4852, 45.0f)};
    a.rezzer = true;
    Facts b = a;
    b.units = {Joiners(21, 4852, 40.0f, 9, true), Joiners(22, 4852, 45.0f, 9)};
    b.rezMembers = RezRoster(true, 0, 500.0f);
    CHECK(b.joinerCeiling == 0 && !b.rezHold);
    for (int i = 0; i < 300; ++i)
    {
        if (i == 20)
        {
            a.minHealthPct = b.minHealthPct = 100;
        }
        if (i == 30)
            a.anyInCombat = b.anyInCombat = true;
        if (i == 40)
        {
            a.anyInCombat = b.anyInCombat = false;
            a.dead = b.dead = 1;
            a.alive = b.alive = 4;
            a.units = {Unit(22, 4852, 45.0f)};
            b.units = {Joiners(22, 4852, 45.0f, 9)};
        }
        if (i == 150)
        {
            a.dead = b.dead = 0;
            a.alive = b.alive = 5;
        }
        std::string const oa = Tick(plain, a);
        std::string const ob = Tick(noisy, b);
        CHECK(oa == ob);
        CHECK(plain.note == noisy.note && plain.phase == noisy.phase && plain.regroup == noisy.regroup);
        CHECK(plain.regroupMs == noisy.regroupMs && plain.reviveSent == noisy.reviveSent);
        CHECK(StatusLine(plain) == StatusLine(noisy));
        CHECK(!noisy.joinerRest && noisy.joinerHeld.empty() && noisy.joinerRests == 0 && noisy.rezAbleMs == 0 &&
              noisy.reviveBaseMs == 0);
    }
}

// The pull stand (AiPlayerbot.DungeonRunPullStand, HoldForShot). Shoot Bow: the
// module's GetRange 29.5, so the tank shoots from where it stands within 24.5 yd.
static Facts StandParty()
{
    Facts f = Party(0);
    f.pullStand = true;
    f.leadShotYd = 29.5f;
    return f;
}

static UnitFact Seen(std::uint64_t guid, std::uint32_t entry, float dist, bool los = true)
{
    UnitFact u = Unit(guid, entry, dist);
    u.losLead = los;
    return u;
}

static bool Has(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// The glue's read of a stand for the unit the policy asked for (Run::shotStandFor).
static void TestPullNoteCarriesTheJoinerCount()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Facts f = Party(0);
    f.joinerCeiling = 6;
    std::string order;

    // Ceiling 6, the glue read 3 joiners for the unit: the count rides on the note.
    {
        Run r = Start(&plan);
        UnitFact u = Joiners(31, 4852, 40.0f, 3);
        u.joinersRead = true;
        f.units = {u};
        order.clear();
        PullUnit(r, f, f.units[0], order);
        CHECK(order == "pullback 31" && r.note == "pulling 4852; 3 joiners expected");
    }
    // Ceiling 6, the unit was held or out of the glue's index, so no count this step: the note says so,
    // and it never claims the stale count below.
    {
        Run r = Start(&plan);
        UnitFact u = Unit(31, 4852, 40.0f);
        u.joiners = 5;   // a stale read kept, but marked not read
        u.joinersPatrol = true;
        f.units = {u};
        order.clear();
        PullUnit(r, f, f.units[0], order);
        CHECK(order == "pullback 31" && r.note == "pulling 4852; joiners not read");
    }
    // Ceiling on, the pull stand on: the count goes after the stand text.
    {
        Run r = Start(&plan);
        Facts s = StandParty();
        s.joinerCeiling = 6;
        UnitFact u = Joiners(11, 4851, 24.5f, 3);
        u.joinersRead = true;
        u.losLead = true;
        s.units = {u};
        CHECK(Tick(r, s) == "pullback 11");
        CHECK(r.note ==
            "pulling 4851 0x000000000000000b (order 1; shot from where it stands); 3 joiners expected");
    }

    // Ceiling off: a stray count and stray read in the facts change nothing, both stand variants.
    f.joinerCeiling = 0;
    {
        Run r = Start(&plan);
        UnitFact u = Joiners(31, 4852, 40.0f, 9);
        u.joinersRead = true;
        f.units = {u};
        order.clear();
        PullUnit(r, f, f.units[0], order);
        CHECK(order == "pullback 31" && r.note == "pulling 4852");
    }
    {
        Run r = Start(&plan);
        Facts s = StandParty();
        UnitFact u = Seen(11, 4851, 24.5f);
        u.joiners = 9;
        u.joinersRead = true;
        u.joinersPatrol = true;
        s.units = {u};
        CHECK(Tick(r, s) == "pullback 11");
        CHECK(r.note == "pulling 4851 0x000000000000000b (order 1; shot from where it stands)");
    }
}

// The glue's read of a stand for the unit the policy asked for (Run::shotStandFor).
static void GiveStand(Facts& f, const Run& r, float x, float yd)
{
    f.shotStandGuid = r.shotStandFor;
    f.shotStandKnown = true;
    f.shotStandX = x;
    f.shotStandY = 0.0f;
    f.shotStandZ = 0.0f;
    f.shotStandYd = yd;
}

static void ClearStand(Facts& f)
{
    f.shotStandGuid = 0;
    f.shotStandKnown = false;
}

// In sight and within 24.5 yd (the edge included), standing still: the order on the first tick, its note
// carrying the guid, the order count and how the shot stood.
static void TestPullShootsFromStand()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 24.5f)};
    CHECK(Tick(r, f) == "pullback 11");
    CHECK(r.phase == Phase::AwaitPull);
    CHECK(r.note == "pulling 4851 0x000000000000000b (order 1; shot from where it stands)");
    CHECK(r.shotStandTries[11] == 0 && r.shotStandFor == 0);
    // No fight: the re-order after kPullNoFightMs has a note of its own (the trace keeps only changes).
    std::string order;
    for (int i = 0; i < 40 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 11");
    CHECK(r.note == "pulling 4851 0x000000000000000b (order 2; shot from where it stands)");
}

// A moving tank holds the order up to kShotStillWaitMs, then it goes out anyway; one that stops goes at once.
static void TestPullWaitsForStillTank()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 20.0f)};
        f.leadMoving = true;
        for (std::uint32_t ms = 0; ms < kShotStillWaitMs; ms += kTickMs)
        {
            CHECK(Tick(r, f).empty());
            CHECK(r.note == "waiting for the tank to stand still before pulling 4851 0x000000000000000b");
        }
        CHECK(Tick(r, f) == "pullback 11");
        CHECK(Has(r.note, "(order 1; shot from where it stands, still moving)"));
    }
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 20.0f)};
        f.leadMoving = true;
        CHECK(Tick(r, f).empty());
        f.leadMoving = false;
        CHECK(Tick(r, f) == "pullback 11");
    }
}

// 27 yd (inside the shot's 29.5, outside its 24.5 with the margin): the stand is read, the party walks to
// it, and the order goes out from there.
static void TestPullWalksToStandOutOfRange()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 27.0f)};
    CHECK(Tick(r, f).empty());
    CHECK(r.shotStandFor == 11);
    CHECK(r.note == "reading a firing stand for 4851 0x000000000000000b");
    GiveStand(f, r, 5.0f, 23.0f);
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0");
    CHECK(r.phase == Phase::Walk && r.shotStandTries[11] == 1 && r.shotStandFor == 0);
    CHECK(r.note == "to firing stand 1 for 4851 0x000000000000000b (23.0 yd from it; was 27.0 yd, los yes)");
    ClearStand(f);
    f.leadX = 1.0f;
    f.leadMoving = true;
    f.units[0].distLead = 26.0f;
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "to firing stand 1 for 4851"));
    f.leadX = 5.0f;
    f.leadMoving = false;
    f.units[0].distLead = 22.0f;
    CHECK(Tick(r, f) == "pullback 11");
    CHECK(r.note == "pulling 4851 0x000000000000000b (order 1; shot from firing stand 1)");
    // The anchor is the stand: the pullback's return leg and the next gather are there.
    CHECK(r.anchorKnown && r.anchorX == 5.0f);
}

// ... and a firing stand 3 yd below the tank: the stand walk ends by the same rule, the pull goes out from
// the tank's spot instead of giving the stand up as unreachable.
static void TestFiringStandOnALedgeCountsByGroundDistance()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 27.0f)};
    CHECK(Tick(r, f).empty());
    GiveStand(f, r, 0.0f, 23.0f);
    f.leadX = 5.4f;
    f.leadZ = 3.0f; // 6.2 yd from the stand in 3D, 5.4 on the ground
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    ClearStand(f);
    f.units[0].distLead = 22.0f;
    std::string order;
    for (int i = 0; i < 300 && order.compare(0, 9, "pullback ") != 0 && r.state == State::Running; ++i)
        order = Tick(r, f);
    CHECK(r.state == State::Running);
    CHECK(order == "pullback 11");
    CHECK(Has(r.note, "shot from firing stand 1"));
}

// 22 yd, in range but out of sight: a stand too; a tank still moving on arrival is waited for.
static void TestPullWalksToStandNoLos()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 22.0f, false)};
    CHECK(Tick(r, f).empty());
    CHECK(r.shotStandFor == 11);
    GiveStand(f, r, -6.0f, 20.0f);
    CHECK(Tick(r, f) == "move -6.0 0.0 0.0");
    CHECK(Has(r.note, "was 22.0 yd, los no"));
    ClearStand(f);
    f.leadX = -6.0f;
    f.leadMoving = true;
    f.units[0] = Seen(11, 4851, 20.0f, true);
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "waiting for the tank to stand still"));
    f.leadMoving = false;
    CHECK(Tick(r, f) == "pullback 11");
    CHECK(Has(r.note, "shot from firing stand 1"));
}

// At most kShotStandTries stands per unit; then, and with no stand found, the order as before.
static void TestPullStandTriesBounded()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        f.anchorPathRead = true; // the glue's answer to the way back (the stand before)
        std::vector<std::string> orders;
        for (int i = 0; i < 30 && (orders.empty() || orders.back().compare(0, 9, "pullback ") != 0); ++i)
        {
            if (r.shotStandFor)
                GiveStand(f, r, f.leadX + 6.0f, 34.0f);
            else
                ClearStand(f);
            std::string const o = Tick(r, f);
            if (!o.empty())
                orders.push_back(o);
            if (r.phase == Phase::Walk)
                f.leadX = r.walkX; // arrives on the next tick; still not in sight
        }
        CHECK(orders.size() == 3);
        CHECK(orders[0] == "move 6.0 0.0 0.0" && orders[1] == "move 12.0 0.0 0.0" && orders[2] == "pullback 11");
        CHECK(r.shotStandTries[11] == kShotStandTries);
        CHECK(r.note == "pulling 4851 0x000000000000000b (order 1; firing stands used up (2))");
    }
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        CHECK(Tick(r, f).empty());
        f.shotStandGuid = 11; // read, nothing found
        f.shotStandKnown = false;
        CHECK(Tick(r, f) == "pullback 11");
        CHECK(Has(r.note, "(order 1; no firing stand found)"));
    }
}

// A body-pull tank (leadShotYd 0) with the key on: the same orders as with the key off, tick by tick
// (only the order's note is longer). The key off with stray stand facts: everything the same, notes too.
static void TestPullNoShotDataKeepsOldOrder()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851, 4852})};
    for (int variant = 0; variant < 2; ++variant)
    {
        Run plain = Start(&plan);
        Run other = Start(&plan);
        Facts a = Party(0);
        // 12 is shootable from where the tank stands, 11 nearer and out of sight: F2 would pick 12.
        a.units = {Unit(11, 4851, 18), Unit(12, 4852, 23)};
        a.maxMemberYd = 35;
        Facts b = a;
        if (variant == 0)
        {
            b.pullStand = true; // key on, no ranged pull
            b.leadShotYd = 0.0f;
        }
        else
        {
            b.pullStand = false; // key off, stray facts
            b.leadShotYd = 29.5f;
            b.shotStandGuid = 12;
            b.shotStandKnown = true;
            b.shotStandX = 9.0f;
        }
        b.units[0].losLead = false;
        b.units[1].losLead = true;
        b.leadMoving = a.leadMoving = true;
        bool sawFirst = false;
        for (int i = 0; i < 120; ++i)
        {
            if (i == 2)
                a.maxMemberYd = b.maxMemberYd = 5;
            if (i == 60)
                a.anyInCombat = b.anyInCombat = true;
            if (i == 70)
            {
                a.anyInCombat = b.anyInCombat = false;
                a.units = {Unit(11, 4851, 18)};
                b.units = {a.units[0]};
            }
            std::string const oa = Tick(plain, a);
            std::string const ob = Tick(other, b);
            CHECK(oa == ob);
            CHECK(oa != "pullback 12" || i >= 70 || sawFirst); // the nearest (11) goes first
            sawFirst = sawFirst || oa == "pullback 11";
            CHECK(plain.phase == other.phase && plain.step == other.step && plain.state == other.state);
            CHECK(other.shotStandFor == 0 && !other.shotStandWalk && !other.shotStandPrep);
            if (variant == 1)
            {
                CHECK(plain.note == other.note);
                CHECK(StatusLine(plain) == StatusLine(other));
            }
            else if (oa.compare(0, 9, "pullback ") == 0)
                CHECK(Has(other.note, "no ranged weapon pull: stand rule off"));
        }
        CHECK(sawFirst);
    }
}

// (review 1) A shot range at or under the margin (4.5, 5.0): the glue reads no stand for it, so the rule
// is off and the order goes out on the first tick (it used to read "reading a firing stand" for 300 s).
static void TestPullStandNeedsMoreRangeThanTheMargin()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    for (float shot : {4.5f, 5.0f})
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.leadShotYd = shot;
        f.units = {Seen(11, 4851, 20.0f, false)};
        CHECK(Tick(r, f) == "pullback 11");
        CHECK(Has(r.note, "no ranged weapon pull: stand rule off") && r.shotStandFor == 0);
    }
}

// (review 2, 6) A stand request lives one tick: the next tick answers or drops it, whether the gate runs
// (a still-wait) or not (a fight); the hold's prep, walk and wait end with any tick the gate did not run on.
static void TestStandRequestIsReadOnce()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        CHECK(Tick(r, f).empty());
        CHECK(r.shotStandFor == 11 && r.shotStandPrep);
        f.anyInCombat = true; // a fight comes before the read is used
        Tick(r, f);
        CHECK(r.shotStandFor == 0 && !r.shotStandPrep);
        for (int i = 0; i < 5; ++i)
            Tick(r, f);
        CHECK(r.shotStandFor == 0 && !r.shotStandPrep && !r.shotStandWalk && r.shotStillMs == 0);
    }
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        CHECK(Tick(r, f).empty());
        GiveStand(f, r, 10.0f, 30.0f);
        f.leadMoving = true; // the read tick goes to the still-wait: the read is dropped, not asked again
        CHECK(Tick(r, f).empty());
        CHECK(r.shotStandFor == 0 && Has(r.note, "waiting for the tank to stand still"));
        CHECK(Tick(r, f).empty());
        CHECK(r.shotStandFor == 0);
    }
    {
        // A gather breaks the stand walk off: the hold ends (prep and walk off), the stand stays counted.
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        Tick(r, f);
        GiveStand(f, r, 10.0f, 30.0f);
        CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
        ClearStand(f);
        f.maxMemberYd = 30.0f;
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather on the lead bot's spot
        CHECK(!r.shotStandPrep && !r.shotStandWalk && r.shotStandTries[11] == 1);
    }
}

// (review 4) A stand walk that gains nothing for kWalkRetryMs, or runs past kShotWalkCapMs, gives the stand
// up and orders from where the tank is; the run never pauses for it.
static void TestStandWalkStuckOrdersFromHere()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    for (int longWalk = 0; longWalk < 2; ++longWalk)
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 120.0f, false)};
        Tick(r, f);
        GiveStand(f, r, 100.0f, 24.0f);
        CHECK(Tick(r, f) == "move 100.0 0.0 0.0");
        ClearStand(f);
        f.anchorPathRead = true;
        std::string order;
        std::uint32_t ms = 0;
        for (; ms < 60000 && order.empty(); ms += kTickMs)
        {
            if (longWalk)
                f.leadX += 2.1f; // headway every tick, never there in time
            order = Tick(r, f);
            CHECK(r.state == State::Running);
        }
        CHECK(order == "pullback 11");
        CHECK(Has(r.note, "(order 1; firing stand unreachable: ordering from here)"));
        // The give-up tick holds once more (the way back is checked on the next): the order a tick later.
        CHECK(longWalk ? ms == kShotWalkCapMs + kTickMs : ms == kWalkRetryMs + kTickMs);
        CHECK(r.shotStandTries[11] == kShotStandTries);
    }
}

// (review 4) The stand walk is driven on every tick while the way back to the old anchor is read on every
// other one (the glue reads the path the tick after it is asked for).
static void TestStandWalkIsDrivenEveryTick()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 40.0f, false)};
    Tick(r, f);
    GiveStand(f, r, 20.0f, 20.0f);
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0");
    ClearStand(f);
    r.anchorKnown = true; // the anchor the walk leaves: 2 yd behind the tank
    r.anchorX = -2.0f;
    for (int i = 0; i < 5; ++i)
    {
        f.anchorPathRead = r.anchorPathWanted;
        f.leadX += 3.0f;
        f.leadMoving = true;
        CHECK(Tick(r, f).empty());
        CHECK(Has(r.note, "to firing stand 1 for 4851"));
    }
}

// (review round 2) The stuck give-up holds its own tick and drops the hold's pass on the way back, so the
// next tick checks the anchor from where the tank got stuck: more than kStragglerDzYd below it (another
// floor) the party gathers back there first; the same floor with a way back, the order goes out.
static void TestStuckStandChecksTheWayBackFirst()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    for (int otherFloor = 0; otherFloor < 2; ++otherFloor)
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 120.0f, false)};
        r.anchorKnown = true; // the anchor: (0, 0, 0)
        Tick(r, f);
        GiveStand(f, r, 100.0f, 24.0f);
        CHECK(Tick(r, f) == "move 100.0 0.0 0.0");
        ClearStand(f);
        f.leadX = 30.0f; // stuck there
        f.leadZ = otherFloor ? -12.0f : 0.0f;
        std::string order;
        int ticks = 0;
        for (; ticks < 30 && order.empty() && !Has(r.note, "firing stand unreachable"); ++ticks)
            order = Tick(r, f);
        CHECK(order.empty()); // the give-up tick sends nothing
        CHECK(r.note == "firing stand unreachable for 4851 0x000000000000000b: checking the way back before the order");
        CHECK(!r.shotStandPrep && r.shotStandTries[11] == kShotStandTries && r.state == State::Running);
        if (otherFloor)
        {
            CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather back to the anchor
            CHECK(r.phase == Phase::Gather);
        }
        else
        {
            f.anchorPathRead = true; // the glue's read: the way back is there
            CHECK(Tick(r, f) == "pullback 11");
            CHECK(Has(r.note, "(order 1; firing stand unreachable: ordering from here)"));
        }
    }
}

// (review round 2) The pick switching to another unit in the middle of a hold: the new unit's first tick
// checks the way back (the hold's pass belonged to the old unit).
static void TestPickSwitchChecksTheWayBack()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 40.0f, false)};
    r.anchorKnown = true;
    r.anchorX = -10.0f; // the lead bot is off it: the way back has to be read
    f.anchorPathRead = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.shotStandPrep && r.shotHoldGuid == 11);
    f.anchorPathRead = false;
    f.units = {Seen(11, 4851, 40.0f, false), Seen(12, 4851, 30.0f, false)}; // 12 is nearer now
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "checking the way back"));
    CHECK(!r.shotStandPrep);
    // A move step's hop pull (PullUnit alone, no FightStep): the same.
    DungeonPlan move;
    move.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    Run m = Start(&move);
    Facts g = StandParty();
    g.hopUnits = {Seen(11, 4851, 40.0f, false)};
    m.anchorKnown = true;
    m.anchorX = -10.0f;
    g.anchorPathRead = true;
    CHECK(Tick(m, g).empty());
    CHECK(m.shotStandPrep && m.shotHoldGuid == 11);
    g.anchorPathRead = false;
    g.hopUnits = {Seen(11, 4851, 40.0f, false), Seen(12, 4851, 30.0f, false)};
    CHECK(Tick(m, g).empty());
    CHECK(Has(m.note, "checking the way back") && !m.shotStandPrep);
}

// F2: among units of the same kill_first rank one the tank can shoot from where it stands goes before a
// nearer one it cannot; the rank still comes first; the key off picks the nearest.
static void TestPickPackPullPrefersShootable()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851, 4852})};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 18.0f, false), Seen(12, 4852, 23.0f, true)};
        CHECK(Tick(r, f) == "pullback 12");
    }
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.pullStand = false;
        f.units = {Seen(11, 4851, 18.0f, false), Seen(12, 4852, 23.0f, true)};
        CHECK(Tick(r, f) == "pullback 11");
    }
    {
        DungeonPlan ranked = plan;
        ranked.killFirst = {4851};
        Run r = Start(&ranked);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 18.0f, false), Seen(12, 4852, 23.0f, true)};
        CHECK(Tick(r, f).empty()); // the kill_first unit, out of sight: its stand is read
        CHECK(r.shotStandFor == 11);
    }
}

// The ticks a stand rule holds a pull are one pull: a pack with a fruitless pull behind it is not skipped
// while the tank walks to its stand (FightStep's kFruitlessPulls).
static void TestStandTicksAreNotFruitlessPulls()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 20.0f), Seen(12, 4851, 40.0f)};
    CHECK(Tick(r, f) == "pullback 11");
    // 11 never comes (no fight); it is ordered again, then passed, and 12 needs a stand.
    f.units[0].losLead = false;
    f.units[0].distLead = 30.0f;
    f.anchorPathRead = true;
    std::string order;
    for (int i = 0; i < 200 && r.step == 0 && order != "pullback 12"; ++i)
    {
        if (r.shotStandFor)
            GiveStand(f, r, f.leadX + 3.5f + static_cast<float>(i % 3), 24.0f);
        else
            ClearStand(f);
        order = Tick(r, f);
        if (r.phase == Phase::Walk)
        {
            // Two ticks on the way, then there.
            f.leadMoving = true;
            Tick(r, f);
            Tick(r, f);
            f.leadMoving = false;
            f.leadX = r.walkX;
        }
    }
    CHECK(r.step == 0 && r.state == State::Running);
    CHECK(order == "pullback 12");
}

// The stand helper: beyond range, the ranged reach's own point; in range but out of sight, the first path
// point nearer the unit that sees it; nothing in sight, no stand.
static void TestShotStandOnPath()
{
    using ai::ApproachPoint;
    auto P = [](float x, float y) { ApproachPoint p; p.x = x; p.y = y; return p; };
    ApproachPoint const unit = P(0, 0);
    auto seesAll = [](const ApproachPoint&) { return true; };
    {
        std::vector<ApproachPoint> path = {P(40, 0), P(30, 0), P(20, 0), P(0, 0)};
        ShotStand const s = ShotStandOnPath(path, unit, 24.5f, seesAll);
        CHECK(s.known && std::fabs(s.x - 24.5f) < 0.01f);
    }
    {
        // 22 yd from it, the pillar hides it until the corner at (10, 10).
        std::vector<ApproachPoint> path = {P(22, 0), P(18, 6), P(10, 10), P(0, 0)};
        auto corner = [](const ApproachPoint& p) { return p.x <= 10.0f; };
        ShotStand const s = ShotStandOnPath(path, unit, 24.5f, corner);
        CHECK(s.known && s.x == 10.0f && s.y == 10.0f);
        auto none = [](const ApproachPoint&) { return false; };
        CHECK(!ShotStandOnPath(path, unit, 24.5f, none).known);
        CHECK(!ShotStandOnPath(path, unit, 0.0f, seesAll).known);
    }
    // Sunken Temple r26 slot 6b step 233: firing stand 1 for 5228 lay 13 yd down a ledge (the lead bot went from
    // z -172.9 to -185.7 onto an island with no way back). The path is cut before its first point more than 6 yd
    // below the lead bot's floor: the stand is the last point on that floor, or none.
    {
        auto Q = [](float x, float y, float z) { ApproachPoint p; p.x = x; p.y = y; p.z = z; return p; };
        ApproachPoint const below = Q(0, 0, -186.0f);
        std::vector<ApproachPoint> path = {Q(40, 0, -172.9f), Q(30, 0, -173.2f), Q(20, 0, -185.7f), Q(0, 0, -186.0f)};
        ShotStand const s = ShotStandOnPath(path, below, 24.5f, seesAll);
        CHECK(s.known && s.x == 30.0f && s.z > -173.5f); // not 24.5 yd from it, down the ledge
        // A stair down of under 6 yd is no ledge: the stand is the ranged reach's own point.
        std::vector<ApproachPoint> stair = {Q(40, 0, -172.9f), Q(30, 0, -175.0f), Q(20, 0, -178.8f), Q(0, 0, -178.8f)};
        ShotStand const t = ShotStandOnPath(stair, Q(0, 0, -178.8f), 24.5f, seesAll);
        CHECK(t.known && t.x < 30.0f && t.x > 20.0f); // between the stair's points, 24.5 yd from the unit (3D)
        CHECK(std::fabs(std::sqrt(t.x * t.x + (t.z + 178.8f) * (t.z + 178.8f)) - 24.5f) < 0.05f);
        // The ledge right at the lead bot's feet: no stand (the order goes out as before).
        std::vector<ApproachPoint> cliff = {Q(40, 0, -172.9f), Q(38, 0, -185.0f), Q(20, 0, -186.0f), Q(0, 0, -186.0f)};
        CHECK(!ShotStandOnPath(cliff, below, 24.5f, seesAll).known);
        // In range but out of sight: no stand past the ledge either.
        std::vector<ApproachPoint> near = {Q(22, 0, -172.9f), Q(15, 0, -185.0f), Q(10, 0, -186.0f), Q(0, 0, -186.0f)};
        auto low = [](const ApproachPoint& p) { return p.z < -180.0f; };
        CHECK(!ShotStandOnPath(near, below, 24.5f, low).known);
        // Down the ledge and back up again: nothing past the drop counts, even back on the lead bot's floor.
        std::vector<ApproachPoint> dip = {Q(40, 0, -172.9f), Q(30, 0, -185.0f), Q(25, 0, -172.9f), Q(0, 0, -172.9f)};
        CHECK(!ShotStandOnPath(dip, Q(0, 0, -172.9f), 24.5f, seesAll).known);
        CHECK(kStandLedgeDropYd == 6.0f);
    }
}

// Uldaman run 4, step 38: a fight past the 180 s ceiling paused the run, the
// fight ended 15 s later, and the run stayed paused for 470 s. A ceiling pause
// goes on by itself once the party is out of combat kCeilingQuietMs, once per
// step; a player's pause and every other pause never do.
static void TestCeilingPauseResumesByItself()
{
    DungeonPlan plan;
    PlanStep wait = Step(PlanStepKind::WaitObject, 1, 2, 3);
    wait.entry = 124369;
    wait.param = 1;
    plan.steps = {wait, Step(PlanStepKind::Move, 9)};
    auto ceilingPause = [&](Run& r)
    {
        r.fighting = true;
        r.fightMs = kFightCapMs + 1000;
        Facts g = Party(0);
        g.anyInCombat = true;
        Tick(r, g);
    };
    // The fight past the ceiling pauses; ticks with the party in combat stay paused.
    Run r = Start(&plan);
    ceilingPause(r);
    CHECK(r.state == State::Paused && r.ceilingPaused &&
        r.note.find("a fight lasted more than 180 s") != std::string::npos);
    for (int i = 0; i < 10; ++i)
        CHECK(!CeilingFightOverWhilePaused(r, true, 1000) && r.state == State::Paused);
    // A quiet stretch broken by one combat tick starts over.
    CHECK(!CeilingFightOverWhilePaused(r, false, 4000));
    CHECK(!CeilingFightOverWhilePaused(r, true, 1000) && r.ceilingQuietMs == 0);
    CHECK(!CeilingFightOverWhilePaused(r, false, 4000) && r.state == State::Paused);
    // kCeilingQuietMs out of combat: it goes on by itself, regrouping.
    CHECK(CeilingFightOverWhilePaused(r, false, 1000));
    CHECK(r.state == State::Running && !r.ceilingPaused && r.step == 0);
    CHECK(r.note.find("going on by itself") != std::string::npos);
    // A second ceiling on the same step stays paused for good.
    ceilingPause(r);
    CHECK(r.state == State::Paused && r.ceilingPaused);
    CHECK(!CeilingFightOverWhilePaused(r, false, kCeilingQuietMs * 2) && r.state == State::Paused);
    // A player's pause never self-resumes, and clears the ceiling mark.
    Run p = Start(&plan);
    ceilingPause(p);
    PausePlayer(p, "player order (stay): .bot run resume to go on");
    CHECK(!p.ceilingPaused);
    CHECK(!CeilingFightOverWhilePaused(p, false, kCeilingQuietMs * 2) && p.state == State::Paused);
    // ... and the guard holds on its own even if the mark were left set.
    p.ceilingPaused = true;
    CHECK(!CeilingFightOverWhilePaused(p, false, kCeilingQuietMs * 2) && p.state == State::Paused);
    // Any other pause after the ceiling's clears it.
    Run o = Start(&plan);
    ceilingPause(o);
    Pause(o, "the lead bot is not on the plan's map (70)");
    CHECK(!o.ceilingPaused);
    CHECK(!CeilingFightOverWhilePaused(o, false, kCeilingQuietMs * 2) && o.state == State::Paused);
    // A manual resume and a skip clear it as well.
    Run m = Start(&plan);
    ceilingPause(m);
    CHECK(Resume(m) && !m.ceilingPaused);
    CHECK(!CeilingFightOverWhilePaused(m, false, kCeilingQuietMs * 2));
    Run k = Start(&plan);
    ceilingPause(k);
    CHECK(Skip(k) && !k.ceilingPaused && k.step == 1);
    // A running run is untouched.
    Run u = Start(&plan);
    CHECK(!CeilingFightOverWhilePaused(u, false, kCeilingQuietMs * 2) && u.state == State::Running);
}

// pip S2 Deadmines 2026-10-26: the lead bot left map 36, the run paused on "the
// lead bot is not on the plan's map", and nothing resumed it (> 2 min with the
// lead back beside the player). The pause goes on by itself once the lead bot
// is alive, in world, not teleporting and on the plan's map for
// kLeadBackSettleMs, once per step; a player's pause never resumes by itself.
static void TestLeadOffMapPauseResumesByItself()
{
    DungeonPlan plan;
    PlanStep wait = Step(PlanStepKind::WaitObject, 1, 2, 3);
    wait.entry = 124369;
    wait.param = 1;
    plan.steps = {wait, Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    PauseLeadOffMap(r, 36);
    CHECK(r.state == State::Paused && r.leadOffMapPaused &&
        r.note == "the lead bot is not on the plan's map (36)");
    // Off the map (or not back long enough): still paused.
    CHECK(!LeadBackWhilePaused(r, false, 10000) && r.state == State::Paused);
    CHECK(!LeadBackWhilePaused(r, true, 3000) && r.state == State::Paused);
    CHECK(!LeadBackWhilePaused(r, false, 1000) && r.state == State::Paused && r.leadBackMs == 0);
    CHECK(!LeadBackWhilePaused(r, true, 4000) && r.state == State::Paused);
    CHECK(!LeadBackWhilePaused(r, true, 1000) && r.state == State::Paused);
    // Back past 5 s in a row: it goes on by itself, regrouping.
    CHECK(LeadBackWhilePaused(r, true, 4000));
    CHECK(r.state == State::Running && !r.leadOffMapPaused);
    CHECK(r.note.find("the lead bot is back on the plan's map") != std::string::npos);
    CHECK(r.regroup);
    // A second off-map pause on the same step stays paused for good.
    PauseLeadOffMap(r, 36);
    CHECK(!LeadBackWhilePaused(r, true, 10000) && r.state == State::Paused);
    // A player's pause never self-resumes, and clears the lead-back mark.
    Run p = Start(&plan);
    PauseLeadOffMap(p, 36);
    PausePlayer(p, "paused by .bot run pause");
    CHECK(!p.leadOffMapPaused);
    CHECK(!LeadBackWhilePaused(p, true, 10000) && p.state == State::Paused);
    // ... and the guard holds on its own even if the mark were left set.
    p.leadOffMapPaused = true;
    CHECK(!LeadBackWhilePaused(p, true, 10000) && p.state == State::Paused);
    // Any other pause after the lead-back one clears it.
    Run o = Start(&plan);
    PauseLeadOffMap(o, 36);
    Pause(o, "the lead bot is not on the plan's map (70)");
    CHECK(!o.leadOffMapPaused);
    CHECK(!LeadBackWhilePaused(o, true, 10000) && o.state == State::Paused);
    // A manual resume and a skip clear it as well.
    Run m = Start(&plan);
    PauseLeadOffMap(m, 36);
    CHECK(Resume(m) && !m.leadOffMapPaused);
    CHECK(!LeadBackWhilePaused(m, true, 10000));
    Run k = Start(&plan);
    PauseLeadOffMap(k, 36);
    CHECK(Skip(k) && !k.leadOffMapPaused && k.step == 1);
    // A running run is untouched.
    Run u = Start(&plan);
    CHECK(!LeadBackWhilePaused(u, true, 10000) && u.state == State::Running);
}

// O2 (owner decision 2026-10-26): the 5 min player-hold cap (kPlayerHoldCapMs)
// also ended in a pause only a resume ended (run 1: the ghost was away 7.5
// min). That pause resumes by itself once its cause is gone: the player alive
// and back on the plan's map within kGatherYd. Same rules as the lead-back
// pause: 5 s back in a row, once per step, never a player's own pause.
static PlayerFact HoldPlayer(const std::string& name, bool alive, bool onMap, float yd)
{
    PlayerFact p;
    p.name = name;
    p.alive = alive;
    p.onMap = onMap;
    p.yd = yd;
    return p;
}

static void TestPlayerHoldPauseResumesByItself()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {1234}), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    // A ghost out of the instance holds the pull; 5 min of it pauses.
    Facts away;
    away.players = {HoldPlayer("Pipwren", false, false, 1000.0f)};
    away.players.front().ghost = true;
    std::string const hold = PlayerHoldNote(r, away, *Current(r));
    CHECK(!hold.empty());
    PausePlayerHold(r, hold);
    CHECK(r.state == State::Paused && r.playerHoldPaused &&
        r.note.find("Pipwren is a ghost") != std::string::npos);
    // Still away (or not back long enough): still paused.
    CHECK(!PlayerHoldBackWhilePaused(r, away, 10000) && r.state == State::Paused);
    Facts back = away;
    back.players = {HoldPlayer("Pipwren", true, true, 10.0f)};
    CHECK(!PlayerHoldBackWhilePaused(r, back, 3000) && r.state == State::Paused);
    CHECK(!PlayerHoldBackWhilePaused(r, away, 1000) && r.state == State::Paused && r.playerHoldBackMs == 0);
    CHECK(!PlayerHoldBackWhilePaused(r, back, 4000) && r.state == State::Paused);
    CHECK(!PlayerHoldBackWhilePaused(r, back, 1000) && r.state == State::Paused);
    // Back past 5 s in a row: it goes on by itself, regrouping.
    CHECK(PlayerHoldBackWhilePaused(r, back, 4000));
    CHECK(r.state == State::Running && !r.playerHoldPaused);
    CHECK(r.note.find("the player is back with the party") != std::string::npos);
    CHECK(r.regroup);
    // A second player-hold pause on the same step stays paused for good.
    PausePlayerHold(r, hold);
    Facts g = away;
    g.players = {HoldPlayer("Pipwren", true, true, 10.0f)};
    CHECK(!PlayerHoldBackWhilePaused(r, g, 10000) && r.state == State::Paused);
    // A player's pause never self-resumes, and clears the hold mark.
    Run h = Start(&plan);
    PausePlayerHold(h, hold);
    PausePlayer(h, "paused by .bot run pause");
    CHECK(!h.playerHoldPaused);
    CHECK(!PlayerHoldBackWhilePaused(h, g, 10000) && h.state == State::Paused);
    // ... and the guard holds on its own even if the mark were left set.
    h.playerHoldPaused = true;
    CHECK(!PlayerHoldBackWhilePaused(h, g, 10000) && h.state == State::Paused);
    // Any other pause after the hold one clears it.
    Run o = Start(&plan);
    PausePlayerHold(o, hold);
    Pause(o, "other");
    CHECK(!o.playerHoldPaused);
    CHECK(!PlayerHoldBackWhilePaused(o, g, 10000) && o.state == State::Paused);
    // A dead or far player is not "back".
    Run d = Start(&plan);
    PausePlayerHold(d, hold);
    Facts dead;
    dead.players = {HoldPlayer("Pipwren", false, true, 10.0f)};
    Facts far;
    far.players = {HoldPlayer("Pipwren", true, true, 1000.0f)};
    CHECK(!PlayerHoldBackWhilePaused(d, dead, 10000) && d.state == State::Paused);
    CHECK(!PlayerHoldBackWhilePaused(d, far, 10000) && d.state == State::Paused);
    // A manual resume and a skip clear it as well.
    Run m = Start(&plan);
    PausePlayerHold(m, hold);
    CHECK(Resume(m) && !m.playerHoldPaused);
    CHECK(!PlayerHoldBackWhilePaused(m, g, 10000));
    Run k = Start(&plan);
    PausePlayerHold(k, hold);
    CHECK(Skip(k) && !k.playerHoldPaused && k.step == 1);
    // A running run is untouched.
    Run u = Start(&plan);
    CHECK(!PlayerHoldBackWhilePaused(u, g, 10000) && u.state == State::Running);
}

// Zul'Farrak r3-19z20 / r6-19z24 (PLAN-ZF-NOFIGHT): the island Shadowhunter 7246
// (spawn 81610 at 1591.0 1047.7) stood 7.4 yd from step 64's move point and was
// hop-pulled there; it drew no fight. Passed was cleared every step, so r3 pulled
// it 6 times over 3 steps, and the walk back pulled it again. A hop unit that
// drew no fight from one pullback is passed for the run: step 65, the walk back
// over step 64 and a resume do not pull it again.
static void TestHopUnitThatNeverCameIsNotPulledAgain()
{
    std::uint64_t const island = 0xF130001C4E013ECAull;
    std::string const pull = "pullback " + std::to_string(island);
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 1584.0f, 1050.0f, 8.9f), Step(PlanStepKind::Move, 1594.3f, 1048.0f, 9.4f),
        PullStep(1700, {5649})};
    Run r = Start(&plan);
    Facts f = Party(1585.1f); // row 63's pull point
    f.leadY = 1035.5f;
    f.leadZ = 9.0f;
    f.hopUnits = {Unit(island, 7246, 7.4f)};
    CHECK(Tick(r, f) == pull);
    int pulls = 1;
    std::string order;
    for (int i = 0; i < 40 && order.compare(0, 5, "move ") != 0; ++i)
    {
        order = Tick(r, f);
        if (order == pull)
            ++pulls;
    }
    CHECK(pulls == 1);
    CHECK(order == "move 1584.0 1050.0 8.9");
    CHECK(r.neverCame.count(island));
    // Step 65: the same unit within 15 yd of its point (12.9 yd): no pull.
    f.leadX = 1584.0f;
    f.leadY = 1050.0f;
    f.leadZ = 8.9f;
    Tick(r, f);
    CHECK(r.step == 1);
    for (int i = 0; i < 3 && order.compare(0, 5, "move ") != 0; ++i)
        CHECK((order = Tick(r, f)) != pull);
    order = Tick(r, f);
    CHECK(order == "move 1594.3 1048.0 9.4");
    // The walk back over step 64: no pull on the way either.
    r.walkBack = true;
    r.walkBackIdx = 0;
    r.gatherWaived = true;
    r.gatherRefusals = 2;
    f.leadX = 1594.3f;
    f.leadY = 1048.0f;
    f.leadZ = 9.4f;
    order = Tick(r, f);
    CHECK(order == "move 1584.0 1050.0 8.9");
    CHECK(r.walkBackPulls == 0);
    // A resume regroups and judges the hop units afresh (the set is cleared):
    // with the key off the unit gets its one pullback again.
    Pause(r, "test");
    CHECK(Resume(r));
    CHECK(r.neverCame.empty());
    r.walkBack = false;
    pulls = 0;
    for (int i = 0; i < 40 && pulls == 0; ++i)
        if (Tick(r, f) == pull)
            ++pulls;
    CHECK(pulls == 1);
}

// Review of 8dee2ee1: the run-wide set holds hop units only. A pull row's own
// unit is passed for its step only (a later row that lists it pulls it again),
// a boss that never comes is pulled again after a resume (the pause's
// "not found ... skip if already dead" keeps working), and a hold's idle pull
// (no hop) never enters the set.
static void TestStepUnitsNeverEnterTheRunWideSet()
{
    {
        DungeonPlan plan;
        plan.steps = {PullStep(10, {4851}), PullStep(12, {4851})};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.units = {Unit(7, 4851, 10)};
        int first = 0, second = 0;
        for (int i = 0; i < 200 && r.state == State::Running; ++i)
        {
            std::size_t const step = r.step;
            if (Tick(r, f) == "pullback 7")
                ++(step == 0 ? first : second);
        }
        CHECK(first == static_cast<int>(kPullTriesPerUnit));
        CHECK(second == static_cast<int>(kPullTriesPerUnit));
        CHECK(r.neverCame.empty());
    }
    {
        DungeonPlan plan;
        PlanStep boss = Step(PlanStepKind::Boss, 10);
        boss.entry = 4854;
        ai::NormalizePlanStep(boss);
        plan.steps = {boss};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.units = {Unit(77, 4854, 10)};
        int before = 0, after = 0;
        for (int i = 0; i < 400 && r.state == State::Running; ++i)
            if (Tick(r, f) == "pullback 77")
                ++before;
        CHECK(before == static_cast<int>(kPullTriesPerUnit));
        CHECK(r.state == State::Paused && r.neverCame.empty());
        CHECK(Resume(r));
        for (int i = 0; i < 400 && r.state == State::Running; ++i)
            if (Tick(r, f) == "pullback 77")
                ++after;
        CHECK(after == static_cast<int>(kPullTriesPerUnit));
        CHECK(r.neverCame.empty());
    }
    {
        // A step pull's no-fight timeout (the hold's idle pull is one): no run-wide entry.
        DungeonPlan plan;
        plan.steps = {PullStep(10, {4851})};
        Run r = Start(&plan);
        r.pullGuid = 5;
        r.pullTries[5] = 1;
        r.pullHop = false;
        EnterPhase(r, Phase::AwaitPull);
        r.phaseMs = kPullNoFightMs;
        AwaitPull(r);
        CHECK(r.phase == Phase::Begin && r.neverCame.empty());
    }
}

// The 25 s window: a hop unit whose fight starts at 24 s is not marked; one
// with no fight is marked on the tick its pull is 25 s old, not before. A
// marked unit that turns up as an attacker of the party leaves the set at once.
static void TestHopNeverCameWindowAndLateAttacker()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 4852, 10)};
        CHECK(Tick(r, f) == "pullback 9");
        for (int i = 0; i < 23; ++i)
            CHECK(Tick(r, f).empty());
        CHECK(r.phase == Phase::AwaitPull && r.phaseMs == 23000);
        f.anyInCombat = true; // the fight starts at 24 s
        f.leadInCombat = true;
        Tick(r, f);
        f.hopUnits.clear(); // killed
        for (int i = 0; i < 5; ++i)
            Tick(r, f);
        f.anyInCombat = false;
        f.leadInCombat = false;
        for (int i = 0; i < 30; ++i)
            Tick(r, f);
        CHECK(!r.neverCame.count(9));
    }
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 4852, 10)};
        CHECK(Tick(r, f) == "pullback 9");
        for (int i = 0; i < 24; ++i)
            Tick(r, f);
        CHECK(r.phase == Phase::AwaitPull && !r.neverCame.count(9)); // 24 s: still waiting
        Tick(r, f);
        CHECK(r.neverCame.count(9)); // 25 s
        // It came after all: on a party bot.
        f.anyInCombat = true;
        f.partyAttackers = {Unit(9, 4852, 6)};
        Tick(r, f);
        CHECK(!r.neverCame.count(9));
        // Or on the lead bot itself.
        r.neverCame.insert(9);
        f.partyAttackers.clear();
        f.leadHasAttacker = true;
        f.leadAttacker = Unit(9, 4852, 2);
        Tick(r, f);
        CHECK(!r.neverCame.count(9));
    }
}

// r6-19z24 step 64 (17:28:12): "pulling 7246 (order 1; no firing stand found)",
// the tank walked at it, engaged it on arrival and followed it onto the island.
// With the pull stand on, a hop unit the tank cannot shoot from where it stands,
// once the stand rule gave up, is not pulled at all: it is passed for the run and
// the move goes on. A shootable hop unit is pulled as before, and so is every hop
// unit with the key off (no sight is read then) and a pull row's own unit.
static void TestHopPullWithoutAShotSendsNoOrder()
{
    std::uint64_t const island = 0xF130001C4E013ECAull;
    std::string const pull = "pullback " + std::to_string(island);
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.hopUnits = {Seen(island, 7246, 7.4f, false)};
        CHECK(Tick(r, f).empty());
        CHECK(r.shotStandFor == island);
        f.shotStandGuid = island; // read, nothing found
        f.shotStandKnown = false;
        CHECK(Tick(r, f).empty());
        CHECK(r.neverCame.count(island) && !r.passed.count(island));
        CHECK(Has(r.note, "passed: the tank cannot shoot it from here (no firing stand found)"));
        f.shotStandGuid = 0;
        CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
        // A regroup (a wipe recovery, a resume) clears the set; the same rule
        // leaves the still unshootable unit out again, with no pull.
        BeginRegroup(r);
        CHECK(r.neverCame.empty());
        r.regroup = false;
        EnterPhase(r, Phase::Begin);
        for (int i = 0; i < 6 && !r.neverCame.count(island); ++i)
        {
            if (r.shotStandFor == island)
            {
                f.shotStandGuid = island;
                f.shotStandKnown = false;
            }
            else
                f.shotStandGuid = 0;
            CHECK(Tick(r, f) != pull);
        }
        CHECK(r.neverCame.count(island));
    }
    {
        // The walk back's hop pull: the same.
        DungeonPlan two;
        two.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Move, 90, 0, 0)};
        Run r = Start(&two, 1);
        r.walkBack = true;
        r.walkBackIdx = 0;
        r.gatherWaived = true;
        r.gatherRefusals = 2;
        Facts f = StandParty();
        f.hopUnits = {Seen(island, 7246, 7.4f, false)};
        CHECK(Tick(r, f).empty());
        f.shotStandGuid = island;
        f.shotStandKnown = false;
        CHECK(Tick(r, f).empty());
        CHECK(r.neverCame.count(island) && r.walkBackPulls == 0);
        f.shotStandGuid = 0;
        CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    }
    {
        // In sight and in range: pulled.
        Run r = Start(&plan);
        Facts f = StandParty();
        f.hopUnits = {Seen(island, 7246, 20.0f, true)};
        CHECK(Tick(r, f) == pull);
    }
    {
        // The key off: pulled as before.
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Seen(island, 7246, 7.4f, false)};
        CHECK(Tick(r, f) == pull);
    }
    {
        // A pull row's own unit keeps the old order.
        DungeonPlan pack;
        pack.steps = {PullStep(40, {7246})};
        Run r = Start(&pack);
        Facts f = StandParty();
        f.units = {Seen(island, 7246, 40.0f, false)};
        CHECK(Tick(r, f).empty());
        f.shotStandGuid = island;
        f.shotStandKnown = false;
        CHECK(Tick(r, f) == pull);
        CHECK(!r.neverCame.count(island));
    }
}

// --- Uldaman r10-19z29 step 66: the Stone Steward 4860 (guid 0x6b51) walks its lane north, away from the
// step's 10 yd radius at (-39.6, 272.8), while the party stands at the south end. The step said "pack cleared"
// in 0 s in r8, r9 and r10; the Steward then came into the Keeper's fight at step 68.

static UnitFact Patroller(std::uint64_t guid, std::uint32_t entry, float distLead, float distSpot)
{
    UnitFact u = Unit(guid, entry, distLead);
    u.distSpot = distSpot;
    u.x = -42.0f; // on its lane, south of the step's radius
    u.y = 250.0f;
    u.z = -48.3f;
    return u;
}

static DungeonPlan StewardPlan(std::uint32_t timeoutS = 60)
{
    DungeonPlan plan;
    PlanStep pull = PullStep(-39.6f, {4860});
    pull.y = 272.8f;
    pull.radius = 10.0f;
    pull.timeoutS = timeoutS;
    plan.steps = {pull, Step(PlanStepKind::Move, 100, 0, 0)};
    return plan;
}

static void TestPatrollerOutsideTheRadiusIsWaitedForThenPulled()
{
    DungeonPlan plan = StewardPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
    // On the north half of its lane: no "pack cleared", the step waits for it.
    for (int i = 0; i < 10; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(r.step == 0 && r.state == State::Running);
        CHECK(r.note == "waiting for the patroller 4860 0x0000000000006b51 (outside the 10.0 yd radius; up to 60 s)");
    }
    // Close and in sight, still outside the radius: no pull out there, the step waits on.
    f.patrollers[0].distLead = 12.0f;
    f.patrollers[0].losLead = true;
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "waiting for the patroller 4860"));
    // It walks into the radius: one of the step's units now, pulled by the usual pick.
    f.patrollers.clear();
    f.units = {Unit(0x6b51, 4860, 28.0f)};
    CHECK(Tick(r, f) == "pullback 27473");
    CHECK(r.step == 0 && r.phase == Phase::AwaitPull);
    // Killed: nothing of the step is left (the glue lists no corpse), the step is cleared.
    f.units.clear();
    r.phase = Phase::Begin;
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "pack cleared");
}

// The wait cap: kPatrolWaitMs (60 s), or the row's timeout_s when that is shorter.
static void WaitOut(std::uint32_t timeoutS, std::uint32_t expectS)
{
    DungeonPlan plan = StewardPlan(timeoutS);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
    for (std::uint32_t i = 0; i < expectS; ++i)
    {
        Tick(r, f);
        CHECK(r.step == 0);
    }
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running);
    CHECK(r.note == "pack cleared; the patroller 4860 0x0000000000006b51 did not come within " +
        std::to_string(expectS) + " s");
}

static void TestPatrollerWaitIsBounded()
{
    WaitOut(60, 60);  // NormalizePlanStep's default
    WaitOut(600, 60); // a 600 s row never waits 600 s
    WaitOut(20, 20);  // a shorter row caps it
    WaitOut(0, 60);   // no timeout at all
    // The loader's path: a row with no timeout_s is normalized to 60 s.
    DungeonPlan plan;
    PlanStep pull = PullStep(-39.6f, {4860});
    CHECK(pull.timeoutS == ai::kPlanDefaultTimeoutS);
    plan.steps = {pull, Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
    CHECK(Tick(r, f).empty() && Has(r.note, "up to 60 s"));
}

// A patroller given up on (its wait ran out, or its pulls drew no fight) is not waited for by a later step that
// lists the same entry; a regroup (a resume, a wipe recovery) judges it afresh.
static void TestPatrollerGiveUpIsRunWide()
{
    DungeonPlan plan = StewardPlan(5);
    PlanStep again = plan.steps[0];
    again.x = -20.0f;
    plan.steps = {plan.steps[0], again, Step(PlanStepKind::Move, 100, 0, 0)};
    // The wait ran out at step 1.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        for (int i = 0; i < 6; ++i)
            Tick(r, f);
        CHECK(r.step == 1 && Has(r.note, "did not come within 5 s"));
        CHECK(r.patrolGaveUp.count(0x6b51));
        Tick(r, f); // step 2 lists 4860 too: no second wait for the same guid
        CHECK(r.step == 2 && r.note == "pack cleared");
        // A regroup clears it: the patroller is waited for again.
        Run q = Start(&plan, 1);
        q.patrolGaveUp.insert(0x6b51);
        BeginRegroup(q);
        CHECK(q.patrolGaveUp.empty());
    }
    // Pulled when it walked into the radius, but two pulls drew no fight: passed for the step and given up run-wide.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        CHECK(Tick(r, f).empty());
        f.patrollers.clear();
        f.units = {Unit(0x6b51, 4860, 28.0f)};
        std::string order;
        int pulls = 0;
        for (int i = 0; i < 80 && !r.patrolGaveUp.count(0x6b51); ++i)
            if (Tick(r, f) == "pullback 27473")
                ++pulls;
        CHECK(pulls == 2 && r.patrolGaveUp.count(0x6b51));
        // It walked out again: no step waits for it any more (this one, nor the next that lists 4860 too).
        f.units.clear();
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        for (int i = 0; i < 3 && r.step < 2; ++i)
        {
            Tick(r, f);
            CHECK(!Has(r.note, "waiting for the patroller"));
        }
        CHECK(r.step == 2 && r.note == "pack cleared");
    }
}

static void TestNoPatrollerWaitWithoutALivingHostilePatroller()
{
    DungeonPlan plan = StewardPlan();
    // Not seen at all (dead, out of sight): "pack cleared" at once, as before.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        CHECK(r.step == 1 && r.note == "pack cleared");
    }
    // Friendly to the party: no wait.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        f.patrollers[0].hostile = false;
        Tick(r, f);
        CHECK(r.step == 1 && r.note == "pack cleared");
    }
    // Given up on this run (out of reach): no wait.
    {
        Run r = Start(&plan);
        r.unreachable.insert(0x6b51);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        Tick(r, f);
        CHECK(r.step == 1 && r.note == "pack cleared");
    }
    // The party in a fight: the step logic does not wait for it (called directly; Tick runs the fight gate).
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.anyInCombat = true;
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        std::string order;
        FightStep(r, f, plan.steps[0], {}, kTickMs, order);
        CHECK(r.step == 1 && r.note == "pack cleared");
    }
    // A boss step is not a pull step: its own search rule stands.
    {
        DungeonPlan boss;
        PlanStep b = Step(PlanStepKind::Boss, 40, 0, 0);
        b.entry = 4860;
        boss.steps = {b, Step(PlanStepKind::Move, 100, 0, 0)};
        Run r = Start(&boss);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        Tick(r, f);
        CHECK(Has(r.note, "not at his spot: waiting for him"));
    }
}

// A later pull step lists the same entry, and this patroller stands in that step's radius (within kPatrolSeeYd of
// this step's radius): it is that step's unit; this step neither waits for it nor pulls it.
static void TestPatrollerOwnedByALaterStepIsLeftToIt()
{
    DungeonPlan plan = StewardPlan();
    PlanStep later = plan.steps[0];
    later.x = -42.0f;
    later.y = 250.0f;
    later.z = -48.3f;
    later.radius = 8.0f;
    plan.steps = {plan.steps[0], Step(PlanStepKind::Move, -45, 245, -48), later};
    Run r = Start(&plan);
    Facts f = Party(0);
    UnitFact u = Patroller(0x6b52, 4860, 20.0f, 23.0f); // at (-42, 250): in the later step's radius
    u.losLead = true;
    f.patrollers = {u};
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note == "pack cleared");
    CHECK(!r.patrolGaveUp.count(0x6b52));
}

// --- Uldaman r10-19z29 step 79: pull 7405 x10 (9 joiners expected) from a firing stand 10 yd from the pack,
// up a ramp. The healer (Vist) and the mage (Dizzy) were 27-30 yd behind when the lead reached the stand.

static Facts ScorpidParty(float leadX)
{
    Facts f = StandParty();
    f.joinerCeiling = 6;
    f.leadX = leadX;
    f.members = {Mem("Tanky", 0.0f), Mem("Vist", 4.0f), Mem("Dizzy", 5.0f)};
    for (MemberFact& m : f.members)
        m.x = leadX - m.yd; // behind the lead on the x axis
    f.maxMemberYd = 5.0f;
    return f;
}

static void SetBehind(Facts& f, const std::string& name, float yd)
{
    for (MemberFact& m : f.members)
        if (m.name == name)
        {
            m.yd = yd;
            m.x = f.leadX - yd;
        }
    f.maxMemberYd = 0.0f;
    for (const MemberFact& m : f.members)
        f.maxMemberYd = std::max(f.maxMemberYd, m.yd);
}

// Starts the r10 pull: the joiner rest (rested: no wait), then the stand read and the walk to (24.4, 0, 0).
// Returns with the walk's move sent; the unit is held from here, so the glue sends no count for it any more.
static void StartScorpidWalk(Run& r, Facts& f, std::uint32_t joiners)
{
    UnitFact u = Joiners(0x6b55, 7405, 34.3f, joiners);
    u.joinersRead = true;
    f.units = {u};
    CHECK(Tick(r, f).empty()); // over the ceiling: the joiner rest's pick (the party is rested)
    f.units[0].joinersRead = false; // held: ReadPullJoiners skips it from now on
    f.units[0].joiners = 0;
    std::string order;
    for (int i = 0; i < 4 && r.shotStandFor == 0; ++i)
        order = Tick(r, f);
    CHECK(r.shotStandFor == 0x6b55);
    GiveStand(f, r, 24.4f, 10.2f);
    CHECK(Tick(r, f) == "move 24.4 0.0 0.0");
    ClearStand(f);
    CHECK(r.phase == Phase::Walk && r.shotStandWalk);
}

static void TestBigPackStandWalkWaitsForTheParty()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 9);
    CHECK(r.bigWalk && r.bigJoiners == 9);
    // Up the ramp: the lead at 12, the healer 28 yd behind (40 yd from the stand). The lead holds where it
    // stands (a gather on its own spot), with the reason on the note.
    f.leadX = 12.0f;
    f.leadMoving = true;
    f.units[0].distLead = 22.3f;
    SetBehind(f, "Vist", 28.0f);
    CHECK(Tick(r, f) == "move 12.0 0.0 0.0");
    CHECK(IsGatherOrder(r, "move 12.0 0.0 0.0"));
    CHECK(r.note == "big pack (9 joiners expected, 8 or more): the firing stand walk waits for the party (Vist 28.0 yd behind)");
    // Held while the healer comes up the ramp; the stand walk stays the pull stand's.
    f.leadMoving = false;
    SetBehind(f, "Vist", 24.0f);
    for (int i = 0; i < 5; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(Has(r.note, "waits for the party") && r.shotStandWalk && r.phase == Phase::Walk);
    }
    // A second unit of the pack (another 9-joiner read: its joiner rest's pick) on the next tick does not
    // let the walk run on unwatched: the r10 leak.
    UnitFact other = Joiners(0x6b54, 7405, 21.0f, 9);
    other.joinersRead = true;
    f.units.push_back(other);
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "waits for the party") && r.shotStandWalk);
    f.units.pop_back();
    // The healer is within kGatherYd of the lead: the walk goes on to the same stand (no new stand read).
    SetBehind(f, "Vist", 8.0f);
    CHECK(Tick(r, f) == "move 24.4 0.0 0.0");
    CHECK(r.note == "big pack (9 joiners expected, 8 or more): the party gathered, on to the firing stand");
    CHECK(r.shotStandTries[0x6b55] == 1);
    // At the stand with the party together: the pull goes out from there, and the gate is over.
    f.leadX = 24.4f;
    SetBehind(f, "Vist", 6.0f);
    f.units[0].distLead = 10.2f;
    f.units[0].losLead = true;
    std::string order;
    for (int i = 0; i < 5 && order.compare(0, 9, "pullback ") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 27477");
    CHECK(Has(r.note, "shot from firing stand 1"));
    CHECK(!r.bigWalk);
}

// The r10 tick itself: the healer falls back on the very tick another unit's joiner rest takes the pick (before
// PullUnit's own gather could see it). Without the gate no order goes out and the lead walks on to the stand.
static void TestBigPackGateHoldsOnAJoinerRestTick()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 9);
    f.leadX = 12.0f;
    f.leadMoving = true;
    f.units[0].distLead = 22.3f;
    UnitFact other = Joiners(0x6b54, 7405, 21.0f, 9);
    other.joinersRead = true;
    f.units.push_back(other);
    SetBehind(f, "Vist", 28.0f);
    CHECK(Tick(r, f) == "move 12.0 0.0 0.0");
    CHECK(r.bigHold && r.shotStandWalk);
}

// A bot near the stand already (ahead of the lead) is no reason to hold.
static void TestBigPackGateCountsTheStandPoint()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 9);
    f.leadX = 2.0f;
    for (MemberFact& m : f.members)
        if (m.name == "Dizzy")
        {
            m.yd = 21.0f;
            m.x = 23.0f; // 1.4 yd from the stand
        }
    f.maxMemberYd = 21.0f;
    Tick(r, f);
    CHECK(!r.bigHold && !Has(r.note, "big pack"));
}

static void TestBigPackGateGivesUpAtTheGatherCeiling()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 9);
    f.leadX = 12.0f;
    SetBehind(f, "Vist", 28.0f);
    CHECK(Tick(r, f) == "move 12.0 0.0 0.0");
    std::string order;
    std::uint32_t held = 0;
    for (; held < kGatherCapMs + 5 * kTickMs; held += kTickMs)
    {
        order = Tick(r, f);
        if (!order.empty())
            break;
    }
    CHECK(held + kTickMs == kGatherCapMs); // the ceiling, not forever
    CHECK(order == "move 24.4 0.0 0.0");
    CHECK(r.note == "big pack (9 joiners expected, 8 or more): the party did not gather within 30 s (Vist 28.0 yd "
                    "behind): proceeding anyway");
    // Spent for this walk: the gate does not hold again on the way.
    Tick(r, f);
    CHECK(!r.bigHold && !Has(r.note, "waits for the party"));
}

// kBigPackJoiners is a boundary: exactly 8 joiners expected gates the walk, 7 does not.
static void TestBigPackBoundaryIsEight()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    for (std::uint32_t joiners : {8u, 7u})
    {
        Run r = Start(&plan);
        Facts f = ScorpidParty(0.0f);
        StartScorpidWalk(r, f, joiners);
        CHECK(r.bigWalk == (joiners == 8));
        f.leadX = 12.0f;
        f.units[0].distLead = 22.3f;
        SetBehind(f, "Vist", 28.0f);
        Tick(r, f);
        CHECK(r.bigHold == (joiners == 8));
        CHECK(Has(r.note, "big pack (8 joiners expected, 8 or more)") == (joiners == 8));
    }
}

static void TestSmallPackStandWalkIsNotGated()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 7); // one under kBigPackJoiners
    CHECK(!r.bigWalk);
    f.leadX = 12.0f;
    SetBehind(f, "Vist", 28.0f);
    Tick(r, f);
    CHECK(!r.bigHold && !Has(r.note, "big pack"));
    CHECK(r.note == "gathering before a pull"); // the old gate, as before
}

// ---------------------------------------------------------------------------
// The lead bot in another part of the instance (SM-GRAVEYARD-2026-10-22,
// Scarlet Monastery r20-19z39 slot 7). The test plans lie on the x axis from
// 10 to 220; the instance has two doors: its entry at -90 (100 yd from the
// plan's first point) and another wing's at 2000 (1780 yd from every point).
static std::vector<DoorFact> TwoDoors()
{
    DoorFact entry;
    entry.x = -90.0f;
    DoorFact other;
    other.x = 2000.0f;
    return {entry, other};
}

// A near wipe with the lead bot among the dead: the fight over, the regroup
// arms the hold as WaitForFallen does (the living stay at the fight spot), and
// once the lead bot is up at the entrance the living bot there holds.
static void TestNearWipeHoldsTheLivingAtTheFightSpot()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.doors = TwoDoors();
    f.anyInCombat = true;
    Tick(r, f); // fighting, the lead bot alive at 210
    f.leadAlive = false;
    f.dead = 3;
    f.alive = 2;
    f.deadNames = {"Tanky", "Healy", "Magey"};
    Tick(r, f); // the lead bot and two more fall: a near wipe
    CHECK(r.leadDied);
    f.anyInCombat = false;
    f.members = {Mem("Fyran", 5)};
    f.members[0].x = 215;
    f.maxMemberYd = 0;
    Tick(r, f);
    CHECK(r.regroup && r.walkBackDue && r.autoRegroups == 1);
    CHECK(r.note.find("3 dead (") == 0);
    CHECK(r.holdDue); // the near wipe's regroup arms the hold
    CHECK(Tick(r, f) == "revive party");
    // The lead bot up at the plan's entry door: the living bot at the fight spot holds.
    f.leadAlive = true;
    f.dead = 2;
    f.alive = 3;
    f.deadNames = {"Healy", "Magey"};
    f.leadX = -90;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 305)};
    f.members[1].x = 215;
    f.maxMemberYd = 305;
    CHECK(Tick(r, f).empty());
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran" && !r.holdDue && r.state == State::Running);
    CHECK(!r.leadOtherPart); // the entry door, 100 yd from the plan: the run's own part
    // The others up at the entry door too: the regroup never gathers or summons Fyran.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Healy", 3), Mem("Magey", 3), Mem("Fyran", 305)};
    f.members[3].x = 215;
    for (int i = 0; i < 40 && r.regroup; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order != "summon Fyran");
    }
    CHECK(r.state == State::Running && r.hold.size() == 1 && r.hold[0] == "Fyran");
}

// The lead bot up at another wing's door: no gather, summon or island hop can
// bring the party together. The run pauses with the true reason and the
// living bot keeps holding at the fight spot while paused.
static void TestLeadInOtherPartPausesWithTheTrueReason()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.doors = TwoDoors();
    f.anyInCombat = true;
    Tick(r, f);
    f.leadAlive = false;
    f.dead = 3;
    f.alive = 2;
    f.deadNames = {"Tanky", "Healy", "Magey"};
    Tick(r, f);
    f.anyInCombat = false;
    f.members = {Mem("Fyran", 5)};
    f.members[0].x = 215;
    f.maxMemberYd = 0;
    Tick(r, f);
    CHECK(Tick(r, f) == "revive party");
    // Raised at the other wing's door (2000 0 0), the others still dead; the rescue off
    // (AiPlayerbot.DungeonRunWalkBackRescue): the pause as before (with it on, the regroup waits for the dead and then
    // teleports: TestDireMaulWestR40SecondRevivalWaitsForTheDead).
    f.leadAlive = true;
    f.dead = 2;
    f.alive = 3;
    f.deadNames = {"Healy", "Magey"};
    f.leadX = 2000;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 1785)};
    f.members[1].x = 215;
    f.maxMemberYd = 1785;
    f.walkBackRescue = false;
    CHECK(Tick(r, f).empty());
    CHECK(!r.otherPartResumeRescue); // the rescue off: a resume is refused while the lead bot is there
    CHECK(r.state == State::Paused && r.leadOtherPart);
    CHECK(r.note == "the lead bot revived in another part of the instance (2000.0 0.0 0.0), 1780.0 yd from the run: "
        "it cannot walk back; bring it to the party, then .bot run resume (the party holds where it is)");
    CHECK(r.note.find("island") == std::string::npos);
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran" && r.holdWhilePaused && HoldStandsNow(r));
    CHECK(Tick(r, f).empty()); // paused: no gather, no summon
    // A resume with the lead bot still there and the rescue off (AiPlayerbot.DungeonRunWalkBackRescue) pauses again
    // (no walk back, no island hop); with it on, the rescue teleport (TestLeadInOtherPartAfterAWipeIsRescued).
    CHECK(Resume(r));
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.walkBackRescue = false;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).find("summon") != 0);
    CHECK(r.state == State::Paused && r.note.find("the lead bot revived in another part of the instance") == 0);
    // A player's own pause lets the held bots go (the glue's rule unchanged).
    CHECK(Resume(r));
    CHECK(PauseForPlayer(r, "follow"));
    CHECK(!r.holdWhilePaused && !HoldStandsNow(r));
    // Moved back to the party by hand: the lead bot is near the plan again, the rule is off.
    CHECK(Resume(r));
    f.leadX = 214;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 1)};
    f.members[1].x = 215;
    f.maxMemberYd = 1;
    Tick(r, f);
    CHECK(!r.leadOtherPart && r.state != State::Paused); // regrouped, and the step (no units left) is done
}

// The resume of a pause for a lead bot in another part (SM-REVIVE-PATH-2026-10-23
// fix 5; r21-19z40e slot 7 paused three times in a row with the same note and no
// step progress): `.bot run resume` (the agent's dungeon_resume) with the lead bot
// still far from the plan is refused with a plain answer and the pause, its note
// and its hold stay; no recovery exists for it (a module summon of the lead bot
// is an owner decision). With the lead bot brought near the plan it resumes.
static Run PausedForOtherPart(DungeonPlan& plan, Facts& f)
{
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    f = Party(210);
    f.doors = TwoDoors();
    f.anyInCombat = true;
    Tick(r, f);
    f.leadAlive = false;
    f.dead = 3;
    f.alive = 2;
    f.deadNames = {"Tanky", "Healy", "Magey"};
    Tick(r, f);
    f.anyInCombat = false;
    f.members = {Mem("Fyran", 5)};
    f.members[0].x = 215;
    f.maxMemberYd = 0;
    Tick(r, f);
    Tick(r, f); // revive party
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.leadX = 2000;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 1785)};
    f.members[1].x = 215;
    f.maxMemberYd = 1785;
    f.walkBackRescue = false; // the rescue off: the pause (with it on, TestLeadInOtherPartAfterAWipeIsRescued)
    Tick(r, f);
    return r;
}

static LeadSpot SpotAt(float x, bool alive = true, bool onPlanMap = true)
{
    LeadSpot spot;
    spot.read = true;
    spot.alive = alive;
    spot.onPlanMap = onPlanMap;
    spot.x = x;
    return spot;
}

static void TestOtherPartResumeIsRefusedWhileTheLeadIsAway()
{
    DungeonPlan plan;
    Facts f;
    Run r = PausedForOtherPart(plan, f);
    CHECK(r.state == State::Paused && r.otherPartPaused);
    std::string const note = r.note;
    std::string refusal;
    CHECK(!ResumeChecked(r, SpotAt(2000), refusal));
    CHECK(refusal == "the lead bot is still in another part of the instance (2000.0 0.0 0.0), 1780.0 yd away from "
        "the run: bring it to the party first, then .bot run resume");
    CHECK(r.state == State::Paused && r.otherPartPaused && r.note == note);
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran" && r.holdWhilePaused && HoldStandsNow(r));
    // Refused again and again: no resume, no re-pause, nothing ordered.
    for (int i = 0; i < 3; ++i)
    {
        CHECK(!ResumeChecked(r, SpotAt(2000), refusal) && !refusal.empty());
        CHECK(Tick(r, f).empty() && r.state == State::Paused && r.note == note);
    }
    // Walked off the door but still in the other wing: still refused.
    CHECK(!ResumeChecked(r, SpotAt(1900), refusal));
    CHECK(refusal.find("(1900.0 0.0 0.0), 1680.0 yd away") != std::string::npos);
    // Brought to the party: it resumes and regroups.
    CHECK(ResumeChecked(r, SpotAt(214), refusal));
    CHECK(refusal.empty() && r.state == State::Running && !r.otherPartPaused && r.note == "resumed: regrouping");
}

static void TestOtherPartResumeRefusalOnlyForThatPause()
{
    std::string refusal;
    // The lead bot not read (between maps), dead, or off the plan's map: resumed as before.
    {
        DungeonPlan plan;
        Facts f;
        Run r = PausedForOtherPart(plan, f);
        LeadSpot unread;
        CHECK(ResumeChecked(r, unread, refusal) && refusal.empty() && r.state == State::Running);
    }
    {
        DungeonPlan plan;
        Facts f;
        Run r = PausedForOtherPart(plan, f);
        CHECK(ResumeChecked(r, SpotAt(2000, false), refusal) && refusal.empty());
    }
    {
        DungeonPlan plan;
        Facts f;
        Run r = PausedForOtherPart(plan, f);
        CHECK(ResumeChecked(r, SpotAt(2000, true, false), refusal) && refusal.empty());
    }
    // Another pause with the lead bot far: not this rule (a player's pause resumes as before).
    {
        DungeonPlan plan;
        Facts f;
        Run r = PausedForOtherPart(plan, f);
        CHECK(Resume(r));
        CHECK(PauseForPlayer(r, "follow"));
        CHECK(!r.otherPartPaused);
        CHECK(ResumeChecked(r, SpotAt(2000), refusal) && refusal.empty());
    }
    // Paused again for another reason without a resume between: that pause is not this rule either.
    {
        DungeonPlan plan;
        Facts f;
        Run r = PausedForOtherPart(plan, f);
        Pause(r, "regroup: 2 still dead after 240 s (cmd revive <bot>, then .bot run resume)");
        CHECK(!r.otherPartPaused);
        CHECK(ResumeChecked(r, SpotAt(2000), refusal) && refusal.empty());
    }
    // A running run: nothing to resume, no refusal text.
    {
        DungeonPlan plan;
        plan.steps = {Step(PlanStepKind::Move, 10)};
        Run r = Start(&plan);
        CHECK(!ResumeChecked(r, SpotAt(2000), refusal) && refusal.empty());
    }
}

// A run holds its dead (SM-REVIVE-PATH-2026-10-23 fix 3): a hired bot of the
// bound party stays dead at its body while the run runs or is paused (the
// regroup's resurrection or `revive party`, or the player, releases it); once
// the run is done (its plan's end, `.bot run stop`, the time limit, the switch
// off) the bot's own auto release works again. The glue erases the run on a
// stop, the time limit and an unbind, so no run is found then either.
static void TestRunHoldsItsDeadUntilItEnds()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200)};
    Run r = Start(&plan);
    CHECK(r.state == State::Running && HoldsItsDead(r));
    Pause(r, "3 dead: regroup (cmd revive party), then .bot run resume");
    CHECK(HoldsItsDead(r)); // a paused run still holds its dead
    PausePlayer(r, "paused by .bot run pause");
    CHECK(HoldsItsDead(r));
    CHECK(Resume(r) && HoldsItsDead(r));
    // `.bot run stop` and the time limit set Done before the glue erases the run.
    r.state = State::Done;
    r.note = RunTimeLimitNote(180);
    CHECK(!HoldsItsDead(r));
    // The plan's end.
    Run end = Start(&plan, 1);
    Facts f = Party(200);
    for (int i = 0; i < 5 && end.state != State::Done; ++i)
        Tick(end, f);
    CHECK(end.state == State::Done && !HoldsItsDead(end));
    // A plan without steps never ran.
    DungeonPlan empty;
    CHECK(!HoldsItsDead(Start(&empty)));
    Run none;
    none.plan = nullptr;
    CHECK(!HoldsItsDead(none));
}

// A door that is not the plan's entry but lies near the plan (the Uldaman and
// Gnomeregan back doors: 50-60 yd from their plans' points) is no other part:
// the walk back goes on as before. And with no doors read, no rule at all.
static void TestBackDoorNearThePlanIsNotAnotherPart()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(270); // a back door 50 yd past the last point
    DoorFact back;
    back.x = 270.0f;
    f.doors = {back};
    CHECK(!LeadInOtherPart(r, f));
    // Far from the plan but on no door (a walk, a chase): not by itself.
    f.leadX = 900;
    CHECK(!LeadInOtherPart(r, f));
    // On the far door, no doors read: no rule.
    Facts g = Party(2000);
    CHECK(!LeadInOtherPart(r, g));
    // Within kDoorSpotYd of the far door: the rule.
    g.doors = TwoDoors();
    g.leadX = 2000 + kDoorSpotYd - 0.5f;
    CHECK(LeadInOtherPart(r, g));
    // It holds while the lead bot walks off the door, far from the plan
    // (r20: the island pause came 60 yd from it, at 1617.6 1002.7).
    g.leadX = 1940;
    CHECK(LeadInOtherPart(r, g));
    // Back within kOtherPartYd of a plan point: off.
    g.leadX = 220 + kOtherPartYd - 1;
    CHECK(!LeadInOtherPart(r, g));
    // A dead lead bot: off.
    g.leadX = 2000;
    g.leadAlive = false;
    CHECK(!LeadInOtherPart(r, g));
    CHECK(kDoorSpotYd == 10.0f && kOtherPartYd == 300.0f);
}

// The lead bot died alone and came back at another wing's door (LeadRevivedAway), the rescue off
// (AiPlayerbot.DungeonRunWalkBackRescue): the pause, not the regroup and walk back that end on an island. With it on,
// the rescue teleport (TestLeadRevivedAwayInOtherPartIsRescued).
static void TestLeadRevivedAwayInOtherPartPauses()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.doors = TwoDoors();
    f.anyInCombat = true;
    Tick(r, f);
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f);
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.anyInCombat = false;
    f.leadX = 2000;
    f.maxMemberYd = 1785;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 1785)};
    f.members[1].x = 215;
    f.walkBackRescue = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.note.find("the lead bot revived in another part of the instance") == 0);
    CHECK(!r.regroup && r.autoRegroups == 0 && !r.strandDue);
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran" && r.holdWhilePaused);
    CHECK(r.strandStep == static_cast<std::size_t>(-1)); // a death: the latch armed for the resume
}

// Dire Maul West r36 slot D (a full bot-led run, 2026-10-24): after a wipe the whole party came back at another
// wing's door, 350 yd from every point of the plan, and the run paused 'the lead bot revived in another part of the
// instance' eight times; each was fixed by hand (the bots summoned to the party's way, then the walk back). In a
// bot-only party with everyone up the rescue teleport instead (OtherPartRescue): to the safe point on the way it
// came, then the bots left at the door are summoned to it and the walk back goes on.
static Run WipedAtTheOtherDoor(DungeonPlan& plan, Facts& f)
{
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    f = Party(210);
    f.doors = TwoDoors();
    f.anyInCombat = true;
    Tick(r, f);
    f.leadAlive = false;
    f.dead = 5;
    f.alive = 0;
    f.deadNames = {"Tanky", "Healy", "Magey", "Fyran", "Stabby"};
    f.members.clear();
    Tick(r, f);
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(r.regroup && r.note.find("5 dead") == 0);
    CHECK(Tick(r, f) == "revive party");
    // All five up together at the other wing's door (2000 0 0).
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.leadX = 2000;
    f.members = {Mem("Tanky", 0), Mem("Healy", 1), Mem("Magey", 1), Mem("Fyran", 1), Mem("Stabby", 1)};
    for (MemberFact& m : f.members)
        m.x = 2000 + m.yd;
    f.maxMemberYd = 1;
    return r;
}

static void TestLeadInOtherPartAfterAWipeIsRescued()
{
    DungeonPlan plan;
    Facts f;
    Run r = WipedAtTheOtherDoor(plan, f);
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && !r.regroup && !r.otherPartPaused && r.leadOtherPart);
    CHECK(r.strandDue && r.strandBotOnly && r.walkBack && r.strandIdx == 0 && r.walkBackIdx == 0);
    CHECK(r.note == "the lead bot revived in another part of the instance (2000.0 0.0 0.0), 1780.0 yd from the run: it "
        "is teleported to a safe point on the way it came (step 1) out of combat");
    CHECK(Tick(r, f) == "rescue from 2000.0 0.0 0.0 to 10.0 0.0 0.0");
    // Landed: the four left at the door are summoned to it, one a tick, then the walk back goes on.
    f.leadX = 10;
    for (MemberFact& m : f.members)
        if (m.name != "Tanky")
            m.yd = 1990;
    f.maxMemberYd = 1990;
    std::string order;
    for (int i = 0; i < 3 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Healy");
    CHECK(Tick(r, f) == "summon Magey");
    CHECK(Tick(r, f) == "summon Fyran");
    CHECK(Tick(r, f) == "summon Stabby");
    CHECK(r.state == State::Running && r.strandStep == r.step && !LeadInOtherPart(r, f));
}

// A joined player cancels an armed other-part rescue (strandFromOtherPart): no rescue order goes out (no try
// spent); the run pauses for the other part (PauseOtherPart of OtherPartBlock::Player: the old player-present
// pause and hold, no walk-back advance) instead of passing the walk-back point (the island rescue's behaviour,
// TestJoinedPlayerCancelsTheIslandRescue). Fails on be9cd92d (PassWalkBackPoint advanced the walk-back). The
// bot-only control still teleports.
static void TestJoinedPlayerCancelsTheOtherPartRescue()
{
    DungeonPlan plan;
    Facts f;
    Run r = WipedAtTheOtherDoor(plan, f);
    std::size_t const walkBackBefore = r.walkBackIdx;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.strandDue && r.strandBotOnly && r.strandFromOtherPart);
    PlayerFact p;
    p.name = "Hypeer";
    p.commander = true;
    f.players.push_back(p); // joins between the arm and the send
    CHECK(Tick(r, f).empty()); // no rescue order: the Player pause instead
    CHECK(r.state == State::Paused && r.otherPartPaused && !r.otherPartResumeRescue && !r.strandDue &&
        !r.strandBotOnly && !r.strandFromOtherPart && !r.strandOn && r.strandTries == 0);
    CHECK(r.walkBackIdx == walkBackBefore); // no walk-back advance
    CHECK(r.note == "the lead bot revived in another part of the instance (2000.0 0.0 0.0), 1780.0 yd from the run: "
        "it cannot walk back; bring it to the party, then .bot run resume");
    CHECK(r.hold.empty() && !r.holdWhilePaused && !HoldStandsNow(r)); // at the door: nobody far to hold
    std::string refusal;
    CHECK(!ResumeChecked(r, SpotAt(2000), refusal) && !refusal.empty() && r.state == State::Paused);
    DungeonPlan control;
    Facts g;
    Run b = WipedAtTheOtherDoor(control, g);
    CHECK(Tick(b, g).empty());
    CHECK(Tick(b, g) == "rescue from 2000.0 0.0 0.0 to 10.0 0.0 0.0"); // control: still teleports
    CHECK(b.strandOn && !b.strandDue && b.strandTries == 0);
}

static void TestLeadInOtherPartRescueBounds()
{
    {
        DungeonPlan plan;
        Facts f;
        Run r = WipedAtTheOtherDoor(plan, f);
        PlayerFact pl;
        pl.name = "Hypeer";
        pl.yd = 1;
        f.players.push_back(pl);
        Tick(r, f);
        CHECK(r.state == State::Paused && r.otherPartPaused && !r.strandDue);
        // With a real player nothing changes: the note and the resume refusal as before.
        CHECK(r.note == "the lead bot revived in another part of the instance (2000.0 0.0 0.0), 1780.0 yd from the "
            "run: it cannot walk back; bring it to the party, then .bot run resume");
        CHECK(!r.otherPartResumeRescue);
        std::string refusal;
        CHECK(!ResumeChecked(r, SpotAt(2000), refusal) && !refusal.empty() && r.state == State::Paused);
    }
    {
        DungeonPlan plan;
        Facts f;
        Run r = WipedAtTheOtherDoor(plan, f);
        f.walkBackRescue = false;
        Tick(r, f);
        CHECK(r.state == State::Paused && r.otherPartPaused && !r.strandDue);
    }
    {
        DungeonPlan plan;
        Facts f;
        Run r = WipedAtTheOtherDoor(plan, f);
        r.strandStep = r.step; // the step's rescue used since the wipe
        Tick(r, f);
        CHECK(r.state == State::Paused && r.otherPartPaused && !r.strandDue);
        CHECK(r.note == "the lead bot revived in another part of the instance (2000.0 0.0 0.0), 1780.0 yd from the "
            "run: the rescue teleport of this step is used; .bot run resume teleports it to the run's way");
        // The resume arms the latch again and the regroup's next tick runs the rescue.
        CHECK(r.otherPartResumeRescue);
        std::string refusal;
        CHECK(ResumeChecked(r, SpotAt(2000), refusal) && refusal.empty() && r.state == State::Running);
        CHECK(r.strandStep == static_cast<std::size_t>(-1));
        CHECK(Tick(r, f).empty() && r.strandDue && r.strandBotOnly && !r.regroup);
    }
    {
        DungeonPlan plan;
        Facts f;
        Run r = WipedAtTheOtherDoor(plan, f);
        r.strandCountStep = r.step; // the step's teleport tries spent
        r.strandTries = kStrandTries;
        Tick(r, f);
        CHECK(r.state == State::Paused && r.otherPartPaused && !r.strandDue && !r.otherPartResumeRescue);
        CHECK(r.note.find("it cannot walk back; bring it to the party") != std::string::npos);
    }
}

// The lead bot died alone and came back at another wing's door (LeadRevivedAway) in a bot-only party: the rescue
// teleport, counted as one of the step's regroups by itself; past them the pause as before.
static void TestLeadRevivedAwayInOtherPartIsRescued()
{
    for (std::uint32_t used : {0u, kAutoRegroupsPerStep})
    {
        DungeonPlan plan;
        plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
        Run r = Start(&plan, 2);
        Facts f = Party(210);
        f.doors = TwoDoors();
        f.anyInCombat = true;
        Tick(r, f);
        f.leadAlive = false;
        f.dead = 1;
        f.alive = 4;
        f.deadNames = {"Tanky"};
        Tick(r, f);
        r.autoRegroups = used;
        r.strandStep = r.step; // used before the death: the death arms it again
        r.islandEscapes = 1;   // the step's state before the death: started over (ResetStep), as the regroup's
        f.leadAlive = true;
        f.dead = 0;
        f.alive = 5;
        f.deadNames.clear();
        f.anyInCombat = false;
        f.leadX = 2000;
        f.maxMemberYd = 1785;
        f.members = {Mem("Tanky", 0), Mem("Fyran", 1785)};
        f.members[1].x = 215;
        CHECK(Tick(r, f).empty());
        if (used == 0)
        {
            CHECK(r.state == State::Running && r.strandDue && r.strandBotOnly && r.autoRegroups == 1 && !r.regroup);
            CHECK(r.islandEscapes == 0);
            CHECK(r.note.find("the lead bot revived in another part of the instance (2000.0 0.0 0.0), ") == 0 &&
                r.note.find("it is teleported to a safe point on the way it came (step 1) out of combat") !=
                    std::string::npos);
            CHECK(Tick(r, f) == "rescue from 2000.0 0.0 0.0 to 10.0 0.0 0.0");
        }
        else
        {
            // Past the step's regroups by itself: still rescued (kOtherPartRescuesPerStep bounds it, not them).
            CHECK(r.state == State::Running && r.strandDue && r.strandBotOnly && !r.regroup);
            CHECK(r.autoRegroups == kAutoRegroupsPerStep + 1 && r.otherPartRescues == 1);
        }
    }
}

// Dire Maul West r40 slot 6, step 147 (2026-10-25; r39 slot 6 steps 147, 217, 218 the same): three wipes at one step.
// After the first the whole party came up at the door at 47.5 -153.7 -2.7 (350 yd from the plan) together and the
// rescue teleported the lead bot back. After the second the lead bot was up at the door while one bot (Duskfury) was
// still a ghost for a few seconds: OtherPartRescueTarget's `f.dead > 0` refused the rescue, the regroup paused at once
// ('it cannot walk back'), and every resume was refused (lead-other-part) until a GM moved the party. The regroup now
// waits for the dead and then runs the rescue.
static void TestDireMaulWestR40SecondRevivalWaitsForTheDead()
{
    DungeonPlan plan;
    Facts f;
    Run r = WipedAtTheOtherDoor(plan, f);
    // Wipe 1: all five up together at the door: the rescue (as TestLeadInOtherPartAfterAWipeIsRescued).
    CHECK(Tick(r, f).empty() && r.strandDue && r.otherPartRescues == 1 && r.otherPartRescueStep == r.step);
    CHECK(Tick(r, f) == "rescue from 2000.0 0.0 0.0 to 10.0 0.0 0.0");
    f.leadX = 10;
    for (MemberFact& m : f.members)
        if (m.name != "Tanky")
            m.yd = 1990;
    f.maxMemberYd = 1990;
    for (int i = 0; i < 12 && r.strandStep != r.step; ++i)
        Tick(r, f);
    CHECK(r.strandStep == r.step); // landed, the party summoned: the step's latch used
    // Back at the pack, wipe 2 at the same step.
    f.leadX = 210;
    for (MemberFact& m : f.members)
        m.yd = 1;
    f.maxMemberYd = 1;
    f.anyInCombat = true;
    Tick(r, f);
    f.leadAlive = false;
    f.dead = 5;
    f.alive = 0;
    f.deadNames = {"Tanky", "Healy", "Magey", "Fyran", "Stabby"};
    f.members.clear();
    Tick(r, f);
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(r.regroup && r.note.find("regrouping by itself (2 of 2 at this step)") != std::string::npos);
    CHECK(Tick(r, f) == "revive party");
    // The lead bot up at the door, Magey still a ghost (the trace's frame at t 2596191: one bot "st":"d").
    f.leadAlive = true;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Magey"};
    f.leadX = 2000;
    f.members = {Mem("Tanky", 0), Mem("Healy", 1), Mem("Fyran", 1), Mem("Stabby", 1)};
    for (MemberFact& m : f.members)
        m.x = 2000 + m.yd;
    f.maxMemberYd = 1;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.regroup && !r.strandDue && !r.otherPartPaused); // was: paused
    CHECK(r.note == "regroup: the lead bot revived in another part of the instance: waiting for Magey (dead) before "
        "the rescue teleport");
    // Magey up at the same door a tick later: the rescue, the second at this step.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members.push_back(Mem("Magey", 1));
    f.members.back().x = 2001;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && !r.regroup && r.strandDue && r.strandBotOnly && r.otherPartRescues == 2);
    CHECK(r.note.find("it is teleported to a safe point on the way it came (step 1) out of combat") !=
        std::string::npos);
    CHECK(Tick(r, f) == "rescue from 2000.0 0.0 0.0 to 10.0 0.0 0.0");
}

// The wait for the dead is bounded as the regroup's revive (kRegroupReviveMs, `revive party` again each minute); then
// the pause names it, and `.bot run resume` resumes (no lead-other-part refusal) into the regroup that rescues.
static void TestOtherPartDeadWaitIsBoundedAndResumeRescues()
{
    DungeonPlan plan;
    Facts f;
    Run r = WipedAtTheOtherDoor(plan, f);
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Magey"};
    f.members.erase(f.members.begin() + 2); // Magey
    int revives = 0;
    std::uint32_t ticks = 0;
    for (; ticks < kRegroupReviveMs / kTickMs + 10 && r.state == State::Running; ++ticks)
        if (Tick(r, f) == "revive party")
            ++revives;
    CHECK(r.state == State::Paused && r.otherPartPaused && r.otherPartResumeRescue && !r.strandDue);
    CHECK(ticks > kRegroupReviveMs / kTickMs - 10 && revives >= 3);
    CHECK(r.note == "the lead bot revived in another part of the instance (2000.0 0.0 0.0), 1780.0 yd from the run: "
        "the rescue teleport waits for the dead (Magey); .bot run resume teleports it to the run's way");
    std::string refusal;
    CHECK(ResumeChecked(r, SpotAt(2000), refusal) && refusal.empty());
    CHECK(r.state == State::Running && r.regroup && !r.otherPartPaused);
    CHECK(r.note == "resumed: the lead bot is in another part of the instance (1780.0 yd from the run): regrouping, "
        "then the rescue teleport");
    CHECK(Tick(r, f) == "revive party" && r.state == State::Running); // still dead: the wait again
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members.push_back(Mem("Magey", 1));
    f.members.back().x = 2001;
    CHECK(Tick(r, f).empty() && r.strandDue && r.strandBotOnly && !r.regroup);
}

// The lead bot died alone and came back at another wing's door with another bot dead (LeadRevivedAway): no pause, the
// regroup, which waits for the dead and then rescues.
static void TestLeadRevivedAwayWithADeadBotRegroupsThenRescues()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.doors = TwoDoors();
    f.anyInCombat = true;
    Tick(r, f);
    f.leadAlive = false;
    f.dead = 2;
    f.alive = 3;
    f.deadNames = {"Tanky", "Healy"};
    Tick(r, f);
    f.anyInCombat = false;
    f.leadAlive = true;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Healy"};
    f.leadX = 2000;
    f.maxMemberYd = 1785;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 1785)};
    f.members[1].x = 215;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.regroup && !r.strandDue && r.autoRegroups == 1);
    CHECK(r.walkBackDue); // LeadRevivedAway's Dead branch arms the regroup's walk back (drop it: no walk back after)
    CHECK(r.note == "the lead bot revived in another part of the instance: regrouping by itself, the rescue teleport "
        "after the dead (Healy) are up");
    CHECK(Tick(r, f) == "revive party");
    CHECK(r.note.find("waiting for Healy (dead) before the rescue teleport") != std::string::npos);
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    CHECK(Tick(r, f).empty() && r.strandDue && !r.regroup && r.state == State::Running);
}

static void TestOtherPartRescueCapThenResume()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.doors = TwoDoors();
    f.anyInCombat = true;
    Tick(r, f);
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f);
    r.otherPartRescueStep = r.step;
    r.otherPartRescues = kOtherPartRescuesPerStep;
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.anyInCombat = false;
    f.leadX = 2000;
    f.maxMemberYd = 1785;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 1785)};
    f.members[1].x = 215;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.otherPartPaused && r.otherPartResumeRescue && !r.strandDue);
    CHECK(r.note == "the lead bot revived in another part of the instance (2000.0 0.0 0.0), 1780.0 yd from the run: 3 "
        "rescue teleports at this step did not hold; .bot run resume teleports it to the run's way (the party holds "
        "where it is)");
    std::string refusal;
    CHECK(ResumeChecked(r, SpotAt(2000), refusal) && refusal.empty() && r.otherPartRescues == 0);
    CHECK(Tick(r, f).empty() && r.strandDue && r.strandBotOnly && r.strandFromOtherPart);
    CHECK(r.otherPartRescues == 1 && r.otherPartRescueStep == r.step);
    // A new step counts from zero: the same count at another step is not the cap (the step comparison pins it:
    // dropping `r.otherPartRescueStep == r.step` fails here). The scratch copy drops the armed teleport so the
    // check below reads the cap branch, not RescueAllowed's Busy.
    Run probe = r;
    probe.strandDue = false;
    probe.strandOn = false;
    probe.otherPartRescueStep = probe.step;
    probe.otherPartRescues = kOtherPartRescuesPerStep; // at the cap at this step again
    CHECK(OtherPartRescueWhy(probe, f) == OtherPartBlock::Cap);
    probe.step = 1; // the rescue's own step is over: the same count is not the cap
    CHECK(OtherPartRescueWhy(probe, f) != OtherPartBlock::Cap);
    CHECK(kOtherPartRescuesPerStep == 3);
}

// Busy (a fight, a rescue already due or on, the run not running, the lead bot dead) counts as resumable
// (OtherPartResumeRescues): the pause it names resumes into the regroup that runs the rescue, same as the Dead
// pause (TestOtherPartDeadWaitIsBoundedAndResumeRescues). The no-Busy mutation
// (drop `|| why == OtherPartBlock::Busy`) fails the first line.
static void TestOtherPartBusyResumesIntoTheRescue()
{
    CHECK(OtherPartResumeRescues(OtherPartBlock::Busy));
    CHECK(OtherPartResumeRescues(OtherPartBlock::Dead));
    CHECK(OtherPartResumeRescues(OtherPartBlock::Cap));
    CHECK(OtherPartResumeRescues(OtherPartBlock::Used));
    CHECK(!OtherPartResumeRescues(OtherPartBlock::Player));
    CHECK(!OtherPartResumeRescues(OtherPartBlock::Off));
    CHECK(!OtherPartResumeRescues(OtherPartBlock::Tries));
    CHECK(!OtherPartResumeRescues(OtherPartBlock::NoPoint));
}

static void TestIslandEscapeInOtherPartRescues()
{
    for (bool rescueOn : {true, false})
    {
        DungeonPlan plan;
        plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
        Run r = Start(&plan, 2);
        Facts f = Party(2000);
        f.doors = TwoDoors();
        f.walkBackRescue = rescueOn;
        CHECK(LeadInOtherPart(r, f));
        std::string order;
        IslandEscape(r, f, 220, 0, 0, "the pack", order);
        CHECK(order.empty() && r.islandEscapes == 0);
        if (rescueOn)
            CHECK(r.state == State::Running && r.strandDue && r.strandBotOnly && r.otherPartRescues == 1);
        else
            CHECK(r.state == State::Paused && r.otherPartPaused && !r.otherPartResumeRescue && !r.strandDue);
    }
}

// The regroup's summons never pull a bot toward a lead bot in another part:
// the last member at the fight spot stays (r20: Nightcrest, summoned 1000 yd
// into the Graveyard wing at 444.8).
static void TestNoSummonTowardALeadInAnotherPart()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    auto stuckRun = [&plan]()
    {
        Run r = Start(&plan, 2);
        BeginRegroup(r);
        r.regroupMs = kStuckSummonMs;
        r.still["Fyran"].ms = kStuckSummonMs;
        r.waitFor.insert("Fyran");
        return r;
    };
    Facts f = Party(2000);
    f.doors = TwoDoors();
    f.members = {Mem("Tanky", 0), Mem("Fyran", 1000)};
    std::string order;
    Run a = stuckRun();
    CHECK(!SummonStuckInRegroup(a, f, order) && order.empty());
    Run b = stuckRun();
    CHECK(!SummonRevived(b, f, order) && order.empty());
    // The same lead bot at the plan's entry: both summon as before.
    f.leadX = -90;
    Run c = stuckRun();
    CHECK(SummonStuckInRegroup(c, f, order) && order == "summon Fyran");
    order.clear();
    Run d = stuckRun();
    CHECK(SummonRevived(d, f, order) && order == "summon Fyran");
}

// The island hop never starts for a lead bot in another part: the pause says why.
static void TestNoIslandHopForALeadInAnotherPart()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(1940); // walked 60 yd off the other wing's door
    f.doors = TwoDoors();
    f.walkBackRescue = false; // the rescue off: the pause (with it on, the rescue: TestIslandEscapeInOtherPartRescues)
    r.leadOtherPart = true; // seen at the door
    std::string order;
    IslandEscape(r, f, 220, 0, 0, "the pack", order);
    CHECK(order.empty() && r.islandEscapes == 0 && !r.escapeOn);
    CHECK(r.state == State::Paused && r.note.find("another part of the instance (1940.0 0.0 0.0), 1720.0 yd") != std::string::npos);
    // Elsewhere the hop goes out as before.
    Run q = Start(&plan, 2);
    Facts g = Party(219);
    g.doors = TwoDoors();
    IslandEscape(q, g, 220, 0, 0, "the pack", order);
    CHECK(order.find("escape from ") == 0 && q.islandEscapes == 1);
}

// The r20-19z39 slot 7 sequence at the policy level (tests/r20-19z39/slot7
// trace): scarlet_library, step 8 (pull group 7). Deaths: Nazra 356, Wrexxar
// (lead) 370 at 178.8 -267.8, Ribbon 372, Duskstorm 378; Nightcrest alive at
// 189.9 -257.6. 381: 4 dead, the near-wipe regroup; 382: revive party. Then
// (1) as it ran: the lead bot raised at the Graveyard wing's door (1687.27
// 1050.09): the run now pauses at once with that reason, Nightcrest held in
// the Library (no gather on the lead, no summon of it, no island text);
// (2) with the corpse run fixed: raised at the Library door (253.672 -206.624):
// Nightcrest holds, the others gather on the lead bot, the walk back starts.
static DungeonPlan ScarletLibraryHead()
{
    DungeonPlan plan;
    plan.name = "scarlet_library";
    plan.map = 189;
    plan.steps = {
        Step(PlanStepKind::Move, 240.84f, -229.627f, 18.6141f),
        PullStep(240.88f, {4287}), PullStep(218.38f, {4287, 4296}), PullStep(201.14f, {4304, 4288}),
        PullStep(201.0f, {4296}), PullStep(193.48f, {4287, 4296}), PullStep(188.11f, {4304, 4288}),
        PullStep(182.41f, {4288, 4304}), PullStep(182.79f, {4287}),
    };
    float const ys[] = {-229.627f, -231.54f, -226.36f, -231.66f, -231.96f, -228.07f, -258.54f, -274.64f, -289.65f};
    float const zs[] = {18.6141f, 18.61f, 18.61f, 18.53f, 18.53f, 18.61f, 18.75f, 18.79f, 19.28f};
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
    {
        plan.steps[i].y = ys[i];
        plan.steps[i].z = zs[i];
    }
    return plan;
}

static std::vector<DoorFact> ScarletDoorDests()
{
    std::vector<DoorFact> doors(4);
    doors[0].x = 1687.27f; doors[0].y = 1050.09f; doors[0].z = 18.6773f;  // 45 Graveyard
    doors[1].x = 253.672f; doors[1].y = -206.624f; doors[1].z = 18.6773f; // 614 Library
    doors[2].x = 1608.1f; doors[2].y = -318.919f; doors[2].z = 18.6714f;  // 612 Armory
    doors[3].x = 853.179f; doors[3].y = 1319.18f; doors[3].z = 18.6714f;  // 610 Cathedral
    return doors;
}

static MemberFact At(const std::string& name, float x, float y, float z, float leadX, float leadY, float leadZ)
{
    MemberFact m;
    m.name = name;
    m.x = x;
    m.y = y;
    m.z = z;
    m.yd = ai::PlanDistance(x, y, z, leadX, leadY, leadZ);
    return m;
}

static Run ScarletNearWipe(const DungeonPlan& plan, Facts& f)
{
    Run r = Start(&plan, 7); // step 8 of 88
    f = Party(0);
    f.carrier = "Wrexxar";
    f.doors = ScarletDoorDests();
    f.leadX = 178.8f;
    f.leadY = -267.8f;
    f.leadZ = 18.7f;
    f.seen = true;
    f.anyInCombat = true;
    Tick(r, f); // fighting at the pull of step 8
    // 370-378: the lead bot and three more fall, Nightcrest fights on.
    f.leadAlive = false;
    f.dead = 4;
    f.alive = 1;
    f.deadNames = {"Nazra", "Wrexxar", "Ribbon", "Duskstorm"};
    f.members = {At("Nightcrest", 189.9f, -257.6f, 18.7f, 178.8f, -267.8f, 18.7f)};
    Tick(r, f);
    CHECK(r.leadDied && r.state == State::Running);
    // 381: the fight is over: the near-wipe regroup, its hold armed.
    f.anyInCombat = false;
    f.maxMemberYd = 0;
    Tick(r, f);
    CHECK(r.regroup && r.holdDue && r.walkBackDue && r.note.find("4 dead (1 near wipe this run)") == 0);
    // 382: revive party.
    CHECK(Tick(r, f) == "revive party");
    return r;
}

static void TestScarletLibraryR20Replay()
{
    DungeonPlan const plan = ScarletLibraryHead();
    // (1) As it ran: the lead bot up at the Graveyard wing's door, three still dead. The build had no rescue teleport
    // (OtherPartRescue came 2026-10-24): the rescue off. With it on the regroup waits for the dead and then teleports
    // (TestDireMaulWestR40SecondRevivalWaitsForTheDead).
    Facts f;
    Run r = ScarletNearWipe(plan, f);
    f.walkBackRescue = false;
    f.leadAlive = true;
    f.dead = 3;
    f.alive = 2;
    f.deadNames = {"Nazra", "Ribbon", "Duskstorm"};
    f.leadX = 1687.8f;
    f.leadY = 1054.6f;
    f.leadZ = 18.7f;
    f.members = {At("Wrexxar", 1687.8f, 1054.6f, 18.7f, 1687.8f, 1054.6f, 18.7f),
                 At("Nightcrest", 189.9f, -257.6f, 18.7f, 1687.8f, 1054.6f, 18.7f)};
    f.maxMemberYd = f.members[1].yd;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.leadOtherPart);
    CHECK(r.note.find("the lead bot revived in another part of the instance (1687.8 1054.6 18.7), ") == 0);
    CHECK(r.note.find("it cannot walk back") != std::string::npos && r.note.find("island") == std::string::npos);
    CHECK(r.hold.size() == 1 && r.hold[0] == "Nightcrest" && HoldStandsNow(r));
    // The three come up beside it at 393, Nightcrest stood still 15 s: nothing goes out.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    r.still["Nightcrest"].ms = kStuckSummonMs * 4;
    for (int i = 0; i < 60; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.hold.size() == 1);

    // (2) With the corpse run fixed: up at the Library door.
    Facts g;
    Run q = ScarletNearWipe(plan, g);
    g.leadAlive = true;
    g.dead = 3;
    g.alive = 2;
    g.deadNames = {"Nazra", "Ribbon", "Duskstorm"};
    g.leadX = 253.7f;
    g.leadY = -206.6f;
    g.leadZ = 18.7f;
    g.members = {At("Wrexxar", 253.7f, -206.6f, 18.7f, 253.7f, -206.6f, 18.7f),
                 At("Nightcrest", 189.9f, -257.6f, 18.7f, 253.7f, -206.6f, 18.7f)};
    g.maxMemberYd = g.members[1].yd;
    CHECK(Tick(q, g).empty());
    CHECK(q.state == State::Running && !q.leadOtherPart);
    CHECK(q.hold.size() == 1 && q.hold[0] == "Nightcrest"); // held at the fight spot, 82 yd from the lead bot
    // The others up beside the lead bot: the regroup gathers them (not Nightcrest) and the walk back follows.
    g.dead = 0;
    g.alive = 5;
    g.deadNames.clear();
    g.members.push_back(At("Nazra", 253.0f, -207.0f, 18.7f, 253.7f, -206.6f, 18.7f));
    g.members.push_back(At("Ribbon", 254.0f, -207.0f, 18.7f, 253.7f, -206.6f, 18.7f));
    g.members.push_back(At("Duskstorm", 253.5f, -206.0f, 18.7f, 253.7f, -206.6f, 18.7f));
    bool walked = false;
    for (int i = 0; i < 10 && !walked; ++i)
    {
        std::string const order = Tick(q, g);
        CHECK(order.find("summon") != 0);
        walked = q.walkBack;
    }
    CHECK(walked && q.state == State::Running && q.hold.size() == 1 && q.hold[0] == "Nightcrest");
}

// Gnomeregan r21-19z40e step 180: the Alarm-a-bomb 2600 (7897, 0xF130001ED90075B6) kept the party in combat,
// every `attack` on it was refused invalid-target ("The group cannot attack that target."), and "a fight lasted
// more than 180 s" paused the run four times on that step. Combat with units the group cannot attack only is no
// fight: no fight timer, no ceiling pause, and the step goes on.
static constexpr std::uint64_t kBombGuid = 0xF130001ED90075B6ull;

static UnitFact Bomb()
{
    UnitFact u = Engaged(Unit(kBombGuid, 7897, 4));
    u.attackable = false;
    return u;
}

static void TestUnattackableOnlyFightIsNoFight()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206}), Step(PlanStepKind::Move, 9)};
    Facts f = Party(0);
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.engagedRead = true;
    f.engagedUnits = {Bomb()};
    // The same bomb-only set for the grace (kUnattackableGraceMs of ticks in a row): the fight goes on and the
    // step stays until then; on the grace's last tick the fight is over, with no pause.
    Run r = Start(&plan);
    CHECK(UnattackableOnlyFight(f) && UnattackableFightLetGo(r, f));
    r.fighting = true;
    r.fightMs = 10000;
    std::uint32_t const graceTicks = kUnattackableGraceMs / kTickMs;
    for (std::uint32_t i = 1; i < graceTicks; ++i)
    {
        Tick(r, f);
        CHECK(r.state == State::Running && r.fighting && r.step == 0);
    }
    CHECK(r.fightMs == 10000 + (graceTicks - 1) * kTickMs);
    Tick(r, f);
    CHECK(r.state == State::Running && !r.fighting);
    CHECK(r.note.find("lasted more than") == std::string::npos);
    CHECK(r.note.find("in combat with units the group cannot attack only (entry 7897 (0xF130001ED90075B6) "
        "cannot be attacked): not counted as a fight") != std::string::npos);
    // The pull step (nothing to pull) is done and the run walks on with the bomb still on the party: the next
    // ticks run no fight timer.
    for (int i = 0; i < 20 && r.step == 0 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && !r.fighting);
    for (int i = 0; i < 5; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.phase == Phase::Walk && !r.fighting);
    // A live mob that is attackable, then unattackable 1..9 s (a burrow, a phase), then attackable again: the
    // fight and the step stay on all through, its timer runs on.
    UnitFact mobNow = Engaged(Unit(0xF13000183E00763Dull, 6206, 3));
    for (std::uint32_t gap = 1; gap < graceTicks; ++gap)
    {
        Run m = Start(&plan);
        Facts on = f;
        on.engagedUnits = {mobNow};
        Facts off = on;
        off.engagedUnits[0].attackable = false;
        Tick(m, on);
        for (std::uint32_t i = 0; i < gap; ++i)
        {
            Tick(m, off);
            CHECK(m.state == State::Running && m.fighting && m.step == 0);
        }
        Tick(m, on);
        CHECK(m.state == State::Running && m.fighting && m.step == 0 && m.unattackableMs == 0);
        CHECK(m.fightMs == (gap + 2) * kTickMs);
    }
    // A changed set starts the grace over: 6 s of the bomb, then 6 s of the bomb and another unattackable unit,
    // still a fight at 12 s; the second set's own 10 s end it.
    Run cs = Start(&plan);
    Facts two = f;
    UnitFact other = Bomb();
    other.guid = 0xF130001ED90075B7ull;
    two.engagedUnits = {Bomb(), other};
    for (int i = 0; i < 6; ++i)
        Tick(cs, f);
    for (std::uint32_t i = 0; i < 6; ++i)
        Tick(cs, two);
    CHECK(cs.state == State::Running && cs.fighting && cs.step == 0 && cs.unattackableMs == 6 * kTickMs);
    for (std::uint32_t i = 6; i < graceTicks; ++i)
        Tick(cs, two);
    CHECK(cs.state == State::Running && !cs.fighting);
    // The note is made once per stretch: a later tick does not add it again.
    CHECK(r.note.find("cannot attack only") == std::string::npos);
    // One attackable unit with it: a real fight, its timer runs, and the ceiling pause names both units.
    Run q = Start(&plan);
    q.fighting = true;
    q.fightMs = kFightCapMs + 5000;
    Facts g = f;
    g.engagedUnits = {Engaged(Unit(0xF13000183E00763Dull, 6206, 3)), Bomb()};
    CHECK(!UnattackableOnlyFight(g) && FightOn(q, g, kTickMs));
    Tick(q, g);
    CHECK(q.state == State::Paused && q.ceilingPaused);
    CHECK(q.note == "a fight lasted more than 180 s (kept on by entry 6206 (0xF13000183E00763D) attackable, "
        "entry 7897 (0xF130001ED90075B6) cannot be attacked)");
    // Facts without the read (a policy test, an old caller): the old rules and the old note.
    Run o = Start(&plan);
    o.fighting = true;
    o.fightMs = kFightCapMs + 5000;
    Facts h = f;
    h.engagedRead = false;
    CHECK(!UnattackableOnlyFight(h) && FightOn(o, h, kTickMs));
    Tick(o, h);
    CHECK(o.state == State::Paused && o.note == "a fight lasted more than 180 s");
    // Read and nothing engaged is the stale flag's (its grace, then its ceiling), not this rule.
    Facts e = f;
    e.engagedUnits.clear();
    e.staleCombat = true;
    CHECK(!UnattackableOnlyFight(e) && FightOn(o, e, kTickMs));
    CHECK(EngagedUnitsNote(e) == "nothing hostile engaged");
    // Out of combat it is nothing at all.
    Facts c = f;
    c.anyInCombat = false;
    CHECK(!UnattackableOnlyFight(c) && !FightOn(o, c, kTickMs));
    // The note names at most kEngagedNoteMax units, then counts the rest.
    Facts many = g;
    many.engagedUnits.clear();
    for (std::uint64_t i = 0; i < kEngagedNoteMax + 2; ++i)
        many.engagedUnits.push_back(Engaged(Unit(100 + i, 6206, 3)));
    CHECK(EngagedUnitsNote(many).find(", 2 more") != std::string::npos);
    // The ceiling pause's self-resume reads the fight the same way (the glue passes FightOn, which ticks the
    // grace): only the bomb left is quiet once the grace is over, then kCeilingQuietMs more.
    int resumedAt = 0;
    for (int i = 1; i <= 40 && q.state == State::Paused; ++i)
        if (CeilingFightOverWhilePaused(q, FightOn(q, f, kTickMs), kTickMs))
            resumedAt = i;
    CHECK(q.state == State::Running &&
        resumedAt == static_cast<int>((kUnattackableGraceMs + kCeilingQuietMs) / kTickMs) - 1);
    // A boss step and an event keep the fight: a boss or an event unit may stand unattackable in its own fight
    // (a phase); its timer runs and its ceiling pauses, naming the unit.
    DungeonPlan bossPlan;
    PlanStep boss = Step(PlanStepKind::Boss);
    boss.entry = 7897;
    boss.entries = {7897};
    bossPlan.steps = {boss};
    Run b = Start(&bossPlan);
    CHECK(!UnattackableFightLetGo(b, f) && FightOn(b, f, kTickMs));
    b.fighting = true;
    b.fightMs = kBossFightCapMs + 5000;
    Tick(b, f);
    CHECK(b.state == State::Paused && b.note == "a fight lasted more than 420 s (kept on by entry 7897 "
        "(0xF130001ED90075B6) cannot be attacked)");
    DungeonPlan holdPlan;
    PlanStep hold = Step(PlanStepKind::Hold);
    hold.radius = 30;
    holdPlan.steps = {hold};
    Run ev = Start(&holdPlan);
    CHECK(InEvent(ev) && !UnattackableFightLetGo(ev, f) && FightOn(ev, f, kTickMs));
}

// A unit the group cannot attack is never pulled, picked up, engaged, focused or crowd-controlled by the run,
// and no wait holds the party for it (the guard's wait on a healer's attacker included).
static void TestUnattackableUnitIsNeverPulledOrPickedUp()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {7897, 6206}), Step(PlanStepKind::Move, 9)};
    plan.killFirst = {7897};
    Run r = Start(&plan);
    Facts f = Party(0);
    UnitFact bomb = Unit(kBombGuid, 7897, 5);
    bomb.attackable = false;
    UnitFact mob = Unit(2, 6206, 20);
    // The pull picks: the far attackable mob, never the near bomb.
    f.units = {bomb, mob};
    CHECK(!Pullable(r, bomb) && Pullable(r, mob));
    CHECK(PickPull(r, f.units, 0) == &f.units[1]);
    CHECK(PickPull(r, f.units, 7897) == nullptr);
    CHECK(PickPackPull(r, f) == &f.units[1]);
    CHECK(OpenUnits(r, f.units) == 1);
    // Only the bomb left on the pull step: nothing to pull and nothing open, the step is done (no pullback on it).
    f.units = {bomb};
    std::string const order = Tick(r, f);
    CHECK(order.find(std::to_string(kBombGuid)) == std::string::npos);
    CHECK(OpenUnits(r, f.units) == 0);
    // A boss step keeps waiting on its boss while it cannot be attacked yet.
    DungeonPlan bossPlan;
    PlanStep boss = Step(PlanStepKind::Boss);
    boss.entry = 7897;
    boss.entries = {7897};
    bossPlan.steps = {boss};
    Run b = Start(&bossPlan);
    CHECK(OpenUnits(b, f.units) == 1);
    // In a fight: the guard does not pick up, nor engage on arrival, a healer's attacker it cannot attack.
    Facts g = Party(0);
    g.anyInCombat = true;
    g.leadIsTank = true;
    g.leadStaying = true;
    UnitFact onHealer = Hitter(kBombGuid, 7897, 6, true);
    onHealer.attackable = false;
    g.partyAttackers = {onHealer};
    CHECK(GuardPick(g, 0.0f) == nullptr);
    CHECK(EngageTarget(r, g) == nullptr);
    g.partyAttackers[0].attackable = true;
    CHECK(GuardPick(g, 0.0f) == &g.partyAttackers[0]);
    CHECK(EngageTarget(r, g) == &g.partyAttackers[0]);
    // ... nor the tank's own mob in melee reach.
    Facts t = g;
    t.partyAttackers.clear();
    t.leadHasAttacker = true;
    t.leadAttacker = Hitter(kBombGuid, 7897, 2);
    t.leadAttacker.attackable = false;
    CHECK(EngageTarget(r, t) == nullptr);
    // A kill_first unit it cannot attack is never focused (no skull, no `focus`).
    Run k = Start(&plan);
    Facts kf = g;
    kf.partyAttackers.clear();
    UnitFact focus = Engaged(Unit(kBombGuid, 7897, 6));
    focus.attackable = false;
    kf.killFirst = {focus};
    CHECK(PickKillFirst(k, kf) == nullptr);
    kf.killFirst[0].attackable = true;
    CHECK(PickKillFirst(k, kf) == &kf.killFirst[0]);
    // In a hold the tank on stay is never sent at an awake event unit it cannot attack.
    DungeonPlan holdPlan;
    PlanStep hold = Step(PlanStepKind::Hold);
    hold.radius = 30;
    holdPlan.steps = {hold};
    Run hr = Start(&holdPlan);
    hr.eventOn = true;
    Facts hf = g;
    hf.partyAttackers.clear();
    UnitFact awake = Engaged(Unit(kBombGuid, 7897, 6));
    awake.attackable = false;
    hf.units = {awake};
    CHECK(HoldEngageTarget(hr, hf) == nullptr);
    hf.units[0].attackable = true;
    CHECK(HoldEngageTarget(hr, hf) == &hf.units[0]);
    // Through Tick: a fight with the bomb on the healer (it cannot be attacked) and an attackable mob on the
    // tank: no order ever names the bomb.
    Run w = Start(&plan);
    Facts wf = g;
    wf.partyAttackers = {onHealer};
    wf.engagedRead = true;
    wf.engagedUnits = {Bomb(), Engaged(Unit(2, 6206, 2))};
    wf.units = {bomb, Engaged(Unit(2, 6206, 2))};
    for (int i = 0; i < 60 && w.state == State::Running; ++i)
        CHECK(Tick(w, wf).find(std::to_string(kBombGuid)) == std::string::npos);
}
// Issue #1160: the run's crowd control on a unit that has not landed yet keeps
// the guard and the engage on arrival off it (CcOrderOut on Run::ccWatch).
static Facts CcWatchParty()
{
    Facts f = Party(0);
    f.anyInCombat = true;
    f.leadIsTank = true;
    f.leadStaying = true;
    return f;
}

static void TestEngageOnArrivalLeavesCcMarkToItsCcBot()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206})};
    Run r = Start(&plan);
    Run::CcWatch w;
    w.guid = 90;
    r.ccWatch = {w};
    Facts g = CcWatchParty();
    g.partyAttackers = {Hitter(90, 6206, 5, true), Hitter(91, 6206, 8, true)};
    CHECK(EngageTarget(r, g) == &g.partyAttackers[1]);
    r.ccWatch.clear();
    SyncCcWatch(r);
    CHECK(EngageTarget(r, g) == &g.partyAttackers[0]);
    // Lead-attacker branch: the watched mark is not engaged either.
    Facts t = CcWatchParty();
    t.partyAttackers.clear();
    t.leadHasAttacker = true;
    t.leadAttacker = Hitter(90, 6206, 2);
    Run wr = Start(&plan);
    Run::CcWatch lw;
    lw.guid = 90;
    wr.ccWatch = {lw};
    CHECK(EngageTarget(wr, t) == nullptr);
    wr.ccWatch.clear();
    SyncCcWatch(wr);
    CHECK(EngageTarget(wr, t) == &t.leadAttacker);
}

static void TestGuardLeavesCcMarkToItsCcBot()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206})};
    Run r = Start(&plan);
    Run::CcWatch w;
    w.guid = 90;
    r.ccWatch = {w};
    Facts g = CcWatchParty();
    g.partyAttackers = {Hitter(90, 6206, 5), Hitter(91, 6206, 8)};
    CHECK(GuardTarget(r, g) == &g.partyAttackers[1]);
    r.ccWatch.clear();
    SyncCcWatch(r);
    CHECK(GuardTarget(r, g) == &g.partyAttackers[0]);
}

static void TestHoldEngageLeavesCcMarkToItsCcBot()
{
    DungeonPlan holdPlan;
    PlanStep hold = Step(PlanStepKind::Hold);
    hold.radius = 30;
    holdPlan.steps = {hold};
    Run hr = Start(&holdPlan);
    hr.eventOn = true;
    Run::CcWatch w;
    w.guid = 90;
    hr.ccWatch = {w};
    Facts hf = CcWatchParty();
    hf.units = {Engaged(Unit(90, 6206, 6)), Engaged(Unit(91, 6206, 8))};
    CHECK(HoldEngageTarget(hr, hf) == &hf.units[1]);
    hr.ccWatch.clear();
    SyncCcWatch(hr);
    CHECK(HoldEngageTarget(hr, hf) == &hf.units[0]);
}
// Issue #1160: the CC bot gone from the members (dead: the glue lists living
// bots only) before the sheep lands must not hold the mark off the fight for
// the rest of the step: CheckCc drops the watch, the engage takes the mob.
static void TestEngageTakesCcMarkOfAGoneCcBot()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206})};
    Run r = Start(&plan);
    r.ccHolds[90] = Run::CcHold{"Magey", "moon", CcFamily::Polymorph};
    Run::CcWatch w;
    w.guid = 90;
    r.ccWatch = {w};
    Facts g = CcWatchParty();
    g.members = {Mem("Tanky", 0)}; // Magey not among the living members
    g.units = {Unit(90, 6206, 5)};
    g.partyAttackers = {Hitter(90, 6206, 5, true)};
    CHECK(EngageTarget(r, g) == nullptr);
    std::string order;
    CHECK(!CheckCc(r, g, kTickMs, order) && order.empty());
    CHECK(r.ccWatch.empty());
    CHECK(EngageTarget(r, g) == &g.partyAttackers[0]);
}

// Issue #1160: the CC bot dead while its mark is watched: the guard picks the
// mob up instead of leaving it to a dead bot for the rest of the step.
static void TestGuardTakesCcMarkOfADeadCcBot()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206})};
    Run r = Start(&plan);
    r.ccHolds[90] = Run::CcHold{"Magey", "moon", CcFamily::Polymorph};
    Run::CcWatch w;
    w.guid = 90;
    r.ccWatch = {w};
    Facts g = CcWatchParty();
    g.members = {Mem("Tanky", 0)}; // Magey dead: the glue lists living bots only
    g.dead = 1;
    g.alive = 4;
    g.deadNames = {"Magey"};
    g.units = {Unit(90, 6206, 5)};
    g.partyAttackers = {Hitter(90, 6206, 5)};
    CHECK(GuardPick(g, 0.0f, &r, &r) == nullptr);
    std::string order;
    CheckCc(r, g, kTickMs, order);
    CHECK(r.ccWatch.empty());
    CHECK(GuardPick(g, 0.0f, &r, &r) == &g.partyAttackers[0]);
}


static void TestCcLandingQueuesTheOrderDrop()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1, 100})};
    plan.ccPriority = {100};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {CcMem("Magey", CcFamily::Polymorph)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(90, 100, 20, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 90 Magey");
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.members[0].inCombat = true;
    f.units[1].polymorphed = true; // the sheep landed
    CHECK(Tick(r, f).empty());
    CHECK(r.ccWatch.empty());
    CHECK(r.ccLandedDrop.size() == 1 && r.ccLandedDrop[0] == 90);
    // Not landed yet and not past the window: no drop queued.
    Run n = Start(&plan);
    Facts g = Party(0);
    g.members = {CcMem("Magey", CcFamily::Polymorph)};
    g.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(90, 100, 20, kCreatureHumanoid)};
    CHECK(Tick(n, g) == "cc moon 90 Magey");
    CHECK(Tick(n, g) == "pullback 1");
    g.anyInCombat = true;
    g.members[0].inCombat = true;
    CHECK(Tick(n, g).empty());
    CHECK(n.ccLandedDrop.empty());
}

static void TestSapLandingQueuesTheOrderDrop()
{
    DungeonPlan plan = CcPlan({100});
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), CcMem("Roguey", CcFamily::Sap)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Roguey");
    CHECK(r.ccPending == 2);
    f.units[1].polymorphed = true; // sapped
    f.maxMemberYd = 30;
    f.members[1].yd = 30;
    Tick(r, f);
    CHECK(r.ccLandedDrop.size() == 1 && r.ccLandedDrop[0] == 2);
}

static void TestCcStateResetClearsTheDrop()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1, 100})};
    Run r = Start(&plan);
    r.ccLandedDrop = {90};
    ResetStep(r);
    CHECK(r.ccLandedDrop.empty());
}


// Gnomeregan r23 step 181: the bomb 7897 held the priest Varcus in combat 35 yd from the lead bot for 8 min; the
// gather paused the run four times. The run drops the combat of a bot held by units the group cannot attack only,
// once the same set has stood the grace, and no gather waits for it meanwhile.
static Facts HeldParty(const std::vector<UnitFact>& held)
{
    Facts f = Party(0);
    f.anyInCombat = true;
    f.engagedRead = true;
    f.engagedUnits = held;
    MemberFact lead = Mem("Tanky", 0);
    MemberFact priest = Mem("Varcus", 35);
    priest.inCombat = true;
    priest.attackers = 1;
    priest.engagedRead = true;
    priest.engaged = held;
    f.members = {lead, priest, Mem("Dinna", 3)};
    f.maxMemberYd = 35;
    return f;
}

static void TestBotHeldByUnattackableDropsCombat()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206}), Step(PlanStepKind::Move, 9)};
    Facts f = HeldParty({Bomb()});
    f.units = {Unit(2, 6206, 25)};
    std::uint32_t const graceTicks = kUnattackableGraceMs / kTickMs;
    Run r = Start(&plan);
    // Inside the grace: no drop, and the gather still waits for the priest.
    for (std::uint32_t i = 1; i < graceTicks; ++i)
    {
        Tick(r, f);
        CHECK(r.combatDrops.empty() && r.state == State::Running);
        CHECK(r.note.find("dropped combat") == std::string::npos);
    }
    // The grace's last tick: the drop, with its note.
    Tick(r, f);
    CHECK(r.combatDrops == std::vector<std::string>{"Varcus"});
    CHECK(r.note.find("Varcus dropped combat with entry 7897 (cannot be attacked)") != std::string::npos);
    // At most once per kCombatDropRepeatMs per bot: the bomb back on it does not drop it again at once.
    std::uint32_t again = 0;
    for (std::uint32_t i = 1; i <= kCombatDropRepeatMs / kTickMs && r.state == State::Running; ++i)
    {
        Tick(r, f);
        if (!r.combatDrops.empty() && again == 0)
            again = i;
    }
    CHECK(again == kCombatDropRepeatMs / kTickMs);
    // An attackable unit in its set starts the grace over.
    Run a = Start(&plan);
    Facts mixed = f;
    mixed.members[1].engaged = {Bomb(), Engaged(Unit(3, 6206, 30))};
    for (int i = 0; i < 5; ++i)
        Tick(a, f);
    Tick(a, mixed);
    for (std::uint32_t i = 1; i < graceTicks; ++i)
    {
        Tick(a, f);
        CHECK(a.combatDrops.empty());
    }
    Tick(a, f);
    CHECK(a.combatDrops == std::vector<std::string>{"Varcus"});
    // A changed set starts it over too.
    Run c = Start(&plan);
    Facts other = f;
    UnitFact second = Bomb();
    second.guid = kBombGuid + 1;
    other.members[1].engaged = {Bomb(), second};
    for (int i = 0; i < 5; ++i)
        Tick(c, f);
    for (std::uint32_t i = 1; i < graceTicks; ++i)
    {
        Tick(c, other);
        CHECK(c.combatDrops.empty());
    }
    Tick(c, other);
    CHECK(c.combatDrops == std::vector<std::string>{"Varcus"});
    // Never on a boss step or in an event, never while the run is not running.
    DungeonPlan bossPlan;
    PlanStep boss = Step(PlanStepKind::Boss);
    boss.entry = 6206;
    boss.entries = {6206};
    bossPlan.steps = {boss};
    Run b = Start(&bossPlan);
    for (std::uint32_t i = 0; i < 2 * graceTicks; ++i)
    {
        Tick(b, f);
        CHECK(b.combatDrops.empty() && b.combatDrop.empty());
    }
    DungeonPlan holdPlan;
    PlanStep hold = Step(PlanStepKind::Hold);
    hold.radius = 30;
    holdPlan.steps = {hold};
    Run e = Start(&holdPlan);
    for (std::uint32_t i = 0; i < 2 * graceTicks; ++i)
    {
        Tick(e, f);
        CHECK(e.combatDrops.empty());
    }
    Run p = Start(&plan);
    Pause(p, "a player's order");
    for (std::uint32_t i = 0; i < 2 * graceTicks; ++i)
    {
        Tick(p, f);
        CHECK(p.combatDrops.empty());
    }
    // A bot with nothing engaged, or with an attackable unit only, is not held.
    MemberFact free = Mem("Free", 30);
    CHECK(!HeldByUnattackable(free));
    free.engagedRead = true;
    free.engaged = {Engaged(Unit(3, 6206, 30))};
    CHECK(!HeldByUnattackable(free));
    free.engaged[0].attackable = false;
    CHECK(HeldByUnattackable(free));
}

static void TickTimes(Run& r, const Facts& f, std::uint32_t n, std::string& notes)
{
    for (std::uint32_t i = 0; i < n; ++i)
    {
        Tick(r, f);
        notes += r.note;
    }
}

// The guard of CombatDropTick: a bot held only by units the group cannot attack keeps its combat while one of
// them is a plan boss (a later Boss step) or a hold entry (a Hold step's entries), however long the set stands.
// The control is the same facts with that entry on another unit: the drop comes at the grace's last tick.
static void TestCombatDropNeverWhileHeldByAPlanBossOrHoldUnit()
{
    std::uint32_t const graceTicks = kUnattackableGraceMs / kTickMs;
    Facts f = HeldParty({Bomb()});
    f.units = {Unit(2, 6206, 25)};
    PlanStep boss = Step(PlanStepKind::Boss, 60);
    boss.entry = 7897;
    PlanStep hold = Step(PlanStepKind::Hold, 90);
    hold.radius = 30;
    hold.entries = {7897};
    DungeonPlan bossPlan;
    bossPlan.steps = {PullStep(0, {6206}), Step(PlanStepKind::Move, 9), boss};
    DungeonPlan holdPlan;
    holdPlan.steps = {PullStep(0, {6206}), Step(PlanStepKind::Move, 9), hold};
    for (const DungeonPlan* plan : {&bossPlan, &holdPlan})
    {
        Run r = Start(plan);
        std::string notes;
        TickTimes(r, f, 2 * graceTicks, notes);
        CHECK(r.state == State::Running);
        CHECK(r.combatDrops.empty());
        CHECK(r.combatDrop.count("Varcus") == 0);
        CHECK(notes.find("dropped combat") == std::string::npos);
    }
    // Control: the entry on another unit, the same facts. No drop inside the grace, the drop on its last tick.
    bossPlan.steps.back().entry = 3976;
    holdPlan.steps.back().entries = {3977};
    for (const DungeonPlan* plan : {&bossPlan, &holdPlan})
    {
        Run r = Start(plan);
        std::string notes;
        TickTimes(r, f, graceTicks - 1, notes);
        CHECK(r.combatDrops.empty());
        TickTimes(r, f, 1, notes);
        CHECK(r.combatDrops == std::vector<std::string>{"Varcus"});
        CHECK(notes.find("Varcus dropped combat with entry 7897 (cannot be attacked)") != std::string::npos);
    }
}

// Dire Maul West r36 slot D step 421 (wipe 4): the party came back at the other wing's door in combat with six
// Residual Monstrosities (11484, attackable, 100 %) that had chased it from the wipe and stood 305 yd away for over
// 3 min; the fight ceiling paused the run twice and `.combatstop` by hand ended it. With the lead bot far from the plan
// (LeadFarFromPlan), a bot near it held only by units farther than kFarHoldYd drops its combat after kFarHoldGraceMs
// (CombatDropTick), on any step kind.
static Facts FarHeldParty(float unitYd)
{
    Facts f = Party(2000); // the other wing's door: 1780 yd from the plan's nearest point
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.engagedRead = true;
    UnitFact far = Engaged(Unit(0xF130002CDC0564C3ull, 11484, unitYd));
    far.attackable = true;
    f.engagedUnits = {far};
    MemberFact lead = Mem("Tanky", 0);
    MemberFact priest = Mem("Healy", 1);
    for (MemberFact* m : {&lead, &priest})
    {
        m->inCombat = true;
        m->engagedRead = true;
        m->engaged = {far};
    }
    f.members = {lead, priest};
    f.maxMemberYd = 1;
    return f;
}

static void TestFarHoldIsDroppedAwayFromThePlan()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), Step(PlanStepKind::Boss, 220)};
    plan.steps.back().entry = 9999;
    std::uint32_t const graceTicks = kFarHoldGraceMs / kTickMs;
    Facts const f = FarHeldParty(305.0f);
    Run r = Start(&plan, 2); // a boss step: the far kind is not bound to UnattackableLetGoStep
    CHECK(HeldByFarUnitsOnly(f.members[0]) && LeadFarFromPlan(r, f));
    for (std::uint32_t i = 0; i + 1 < graceTicks; ++i)
    {
        CHECK(CombatDropTick(r, f, kTickMs).empty());
        CHECK(r.combatDrops.empty());
    }
    std::string const note = CombatDropTick(r, f, kTickMs);
    CHECK((r.combatDrops == std::vector<std::string>{"Tanky", "Healy"}));
    CHECK(note == "Tanky dropped combat with entry 11484 (305.0 yd away); Healy dropped combat with entry 11484 (305.0 yd "
        "away)");
    // Not far enough (kFarHoldYd), the lead bot near the plan, or the bot itself away from the lead bot: never.
    Facts near = FarHeldParty(kFarHoldYd);
    Facts onPlan = FarHeldParty(305.0f);
    onPlan.leadX = 214;
    Facts strayed = FarHeldParty(305.0f);
    strayed.members[1].yd = kGatherYd + 1;
    for (Facts* g : {&near, &onPlan, &strayed})
    {
        Run q = Start(&plan, 2);
        for (std::uint32_t i = 0; i < 2 * graceTicks; ++i)
            CombatDropTick(q, *g, kTickMs);
        CHECK(q.combatDroppedAt.count("Healy") == 0);
        CHECK(q.combatDroppedAt.count("Tanky") == (g == &strayed ? 1u : 0u)); // the lead bot itself stays far-held
    }
    CHECK(kFarHoldYd == 100.0f && kFarHoldGraceMs == 15000);
}

// A boss or a scripted hold unit far away is no far hold: with the lead bot far from the plan, a bot held only
// by units farther than kFarHoldYd keeps its combat while one of them is the current step's boss entry or hold
// entry (the plan boss / event / hold-unit guard covers the far-held kind too), however long the set stands. No
// timer starts and no combat drops. The control is the same facts with entries the plan does not fight: the drop
// comes on the grace's last tick. The `!planUnit` mutation in the far classification drops the boss as well.
static void TestFarHoldNeverDropsAPlanBossOrHoldUnit()
{
    std::uint32_t const graceTicks = kFarHoldGraceMs / kTickMs;
    Facts const f = FarHeldParty(305.0f); // entry 11484, attackable, 305 yd off, lead far from the plan
    DungeonPlan bossPlan;
    bossPlan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), Step(PlanStepKind::Boss, 220)};
    bossPlan.steps.back().entry = 11484; // the current step's boss: the engaged unit
    DungeonPlan holdPlan;
    holdPlan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), Step(PlanStepKind::Hold, 220)};
    holdPlan.steps.back().radius = 30;
    holdPlan.steps.back().entries = {11484}; // the current step's hold entry
    for (const DungeonPlan* plan : {&bossPlan, &holdPlan})
    {
        Run r = Start(plan, 2);
        CHECK(PlanBossOrHoldEntry(r, 11484) && LeadFarFromPlan(r, f) && HeldByFarUnitsOnly(f.members[0]));
        for (std::uint32_t i = 0; i < 2 * graceTicks; ++i)
            CHECK(CombatDropTick(r, f, kTickMs).empty());
        CHECK(r.combatDrops.empty() && r.combatDrop.empty());
    }
    // Control: the same facts with entries the plan does not fight: the drop on the grace's last tick.
    bossPlan.steps.back().entry = 3976;
    holdPlan.steps.back().entries = {3977};
    for (const DungeonPlan* plan : {&bossPlan, &holdPlan})
    {
        Run r = Start(plan, 2);
        CHECK(!PlanBossOrHoldEntry(r, 11484) && LeadFarFromPlan(r, f));
        for (std::uint32_t i = 0; i + 1 < graceTicks; ++i)
        {
            CHECK(CombatDropTick(r, f, kTickMs).empty());
            CHECK(r.combatDrops.empty());
        }
        std::string const note = CombatDropTick(r, f, kTickMs);
        CHECK((r.combatDrops == std::vector<std::string>{"Tanky", "Healy"}));
        CHECK(note.find("Tanky dropped combat with entry 11484 (305.0 yd away)") != std::string::npos);
        CHECK(note.find("Healy dropped combat with entry 11484 (305.0 yd away)") != std::string::npos);
    }
}

// The r36 slot D wipe 4 end to end: up at the other wing's door in combat with units 305 yd away. Their combat is
// dropped after kFarHoldGraceMs (no fight-ceiling pause), and out of combat the regroup's rescue teleport follows.
static void TestDireMaulWestR36FarCombatThenRescue()
{
    DungeonPlan plan;
    Facts f;
    Run r = WipedAtTheOtherDoor(plan, f);
    UnitFact far = Engaged(Unit(0xF130002CDC0564C3ull, 11484, 305.0f));
    far.attackable = true;
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.engagedRead = true;
    f.engagedUnits = {far};
    for (MemberFact& m : f.members)
    {
        m.inCombat = true;
        m.engagedRead = true;
        m.engaged = {far};
    }
    std::uint32_t ticks = 0;
    while (ticks < 60 && r.combatDrops.empty())
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(ticks == kFarHoldGraceMs / kTickMs && r.combatDrops.size() == 5 && r.state == State::Running);
    CHECK(r.note.find("Tanky dropped combat with entry 11484 (305.0 yd away)") != std::string::npos);
    // The glue dropped it (DropUnattackableCombat): out of combat, the rescue.
    f.anyInCombat = false;
    f.leadInCombat = false;
    f.engagedUnits.clear();
    for (MemberFact& m : f.members)
    {
        m.inCombat = false;
        m.engaged.clear();
    }
    Tick(r, f);
    CHECK(r.state == State::Running && r.strandDue && !r.regroup);
    CHECK(Tick(r, f) == "rescue from 2000.0 0.0 0.0 to 10.0 0.0 0.0");
}

// A gather's ceiling does not pause on bots held only by units the group cannot attack (their combat is being
// dropped): it waits again, within the coming straggler's budget (kGatherComingMaxMs), then pauses as before.
static void TestGatherWaitsForABotHeldByUnattackable()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206}), Step(PlanStepKind::Move, 9)};
    Facts f = HeldParty({Bomb()});
    f.anyInCombat = false; // the party's own fight is over; the priest alone stands in the bomb's combat
    Run r = Start(&plan);
    CHECK(!HeldStragglerWait(r, f)); // not held yet (no CombatDropTick)
    CombatDropTick(r, f, kTickMs);
    std::uint32_t waits = 0;
    while (waits < 100 && HeldStragglerWait(r, f))
        ++waits;
    CHECK(waits == (kGatherComingMaxMs - 1) / kGatherCapMs);
    CHECK(r.note.find("Varcus (35.0 yd) held in combat by units the group cannot attack only") != std::string::npos);
    // A held bot is not sent on a run back (its attacker cannot be fought at the party either), and a run back
    // under way ends once it is held.
    Run rb = Start(&plan);
    rb.regroup = true;
    rb.reviveSent = true;
    CombatDropTick(rb, f, kTickMs);
    CHECK(!RunBack(rb, f, kTickMs) && rb.runBack.empty());
    rb.combatDrop.clear();
    CHECK(RunBack(rb, f, kTickMs) && rb.runBack.size() == 1);
    CombatDropTick(rb, f, kTickMs);
    CHECK(!RunBack(rb, f, kTickMs) && rb.runBack.empty());
    // A far bot that is not held keeps the old rule: no wait.
    Run o = Start(&plan);
    Facts g = f;
    g.members.push_back(Mem("Helmina", 36));
    CombatDropTick(o, g, kTickMs);
    CHECK(!HeldStragglerWait(o, g));
    // A step's gather ceiling (GatherCeiling) waits the same way, and pauses for a far bot that is not held.
    Run gc = Start(&plan);
    CombatDropTick(gc, f, kTickMs);
    std::string gorder;
    GatherCeiling(gc, f, gorder, "the party did not gather within 30 s");
    CHECK(gc.state == State::Running && gc.phase == Phase::Gather && gorder.empty() &&
        gc.note.find("held in combat") != std::string::npos);
    Facts gh = f;
    MemberFact hunter = Mem("Helmina", 36);
    hunter.inCombat = true;
    gh.members.push_back(hunter);
    Run gp = Start(&plan);
    CombatDropTick(gp, gh, kTickMs);
    GatherCeiling(gp, gh, gorder, "the party did not gather within 30 s");
    CHECK(gp.state == State::Paused && gp.note == "the party did not gather within 30 s");
    // Through the regroup: its gather ceiling past, the run does not pause while the held priest's budget lasts.
    Run q = Start(&plan);
    q.regroup = true;
    q.reviveSent = true;
    q.regroupGatherSent = true;
    q.regroupGatherCapMs = kGatherCapMs;
    q.regroupMs = kGatherCapMs + 1000;
    Tick(q, f);
    CHECK(q.state == State::Running && q.note.find("held in combat") != std::string::npos);
    Run n = Start(&plan);
    n.regroup = true;
    n.reviveSent = true;
    n.regroupGatherSent = true;
    n.regroupGatherCapMs = kGatherCapMs;
    n.regroupMs = kGatherCapMs + 1000;
    n.straggle["Helmina"].rung = 2; // the straggler ladder walked and summoned her already
    n.straggle["Helmina"].rungMs = kStragglerSettleMs;
    Tick(n, g);
    CHECK(n.state == State::Paused && n.note.find("regroup: the party did not gather within") != std::string::npos);
}

// Codex adversarial review of 5017a644: a bot whose pet alone is held by the bomb is a bot in combat (the glue's
// MemberInCombat: Facts::anyInCombat, MemberFact::inCombat, and the pet's attackers in its count). Before the drop
// it is not summoned, sent on a run back or pulled past; the regroup waits for it.
static void TestPetHeldByUnattackableKeepsTheBotInTheFight()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206}), Step(PlanStepKind::Move, 9)};
    Facts f = HeldParty({Bomb()}); // the hunter's pet held: the glue reads her in combat with 1 attacker (the pet's)
    f.members[1].name = "Helmina";
    f.units = {Unit(2, 6206, 25)};
    std::uint32_t const graceTicks = kUnattackableGraceMs / kTickMs;
    // Not summoned (a bot in combat or with attackers never is).
    Run s = Start(&plan);
    std::string order;
    CHECK(!SummonStraggler(s, f, order) && order.empty());
    // No pull before the drop: the party is in combat until the grace is over.
    Run r = Start(&plan);
    for (std::uint32_t i = 1; i < graceTicks; ++i)
    {
        CHECK(Tick(r, f).compare(0, 9, "pullback ") != 0);
        CHECK(r.combatDrops.empty() && r.runBack.empty());
    }
    Tick(r, f);
    CHECK(r.combatDrops == std::vector<std::string>{"Helmina"});
    // In a regroup: no run back, no summon, and its ceiling waits for her instead of pausing.
    Run q = Start(&plan);
    q.regroup = true;
    q.reviveSent = true;
    q.regroupGatherSent = true;
    q.regroupGatherCapMs = kGatherCapMs;
    q.regroupMs = kGatherCapMs + 1000;
    Facts g = f;
    g.anyInCombat = false; // the party's fight is over; her pet's is not
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        std::string const o = Tick(q, g);
        CHECK(o.compare(0, 7, "summon ") != 0 && q.runBack.empty() && q.state == State::Running);
        if (i == 0)
            CHECK(q.note.find("Helmina (35.0 yd) held in combat") != std::string::npos);
    }
}
// A unit the group first cannot attack, then can but only far away (a phase ending with it standing far off):
// the same guid changes the drop kind, so the timer starts over. 9.9 s accumulated as unattackable, then the same
// unit attackable + far-held: no drop with only a further 14.9 s stood far-held, the drop after the full 15 s.
// The no-reset mutation (keep ms across the kind change) drops 0.1 s early and fails the CHECK.
static void TestCombatDropKindChangeStartsTheGraceOver()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206}), Step(PlanStepKind::Move, 9), Step(PlanStepKind::Move, 200)};
    std::uint64_t const guid = 0xF130002CDC0564C4ull;
    UnitFact phase = Engaged(Unit(guid, 11484, 305.0f));
    phase.attackable = false;
    Facts f = Party(2000); // the other wing's door: 1780 yd from the plan's nearest point
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.engagedRead = true;
    f.engagedUnits = {phase};
    MemberFact lead = Mem("Tanky", 0);
    MemberFact priest = Mem("Varcus", 1);
    for (MemberFact* m : {&lead, &priest})
    {
        m->inCombat = true;
        m->engagedRead = true;
        m->engaged = {phase};
    }
    f.members = {lead, priest};
    f.maxMemberYd = 1;
    Run r = Start(&plan, 2);
    std::uint32_t const heldTicks = kUnattackableGraceMs / kTickMs - 1; // 9.9 s as unattackable
    for (std::uint32_t i = 0; i < heldTicks; ++i)
    {
        Tick(r, f);
        CHECK(r.combatDrops.empty());
    }
    CHECK(r.combatDrop.count("Varcus") == 1 && r.combatDrop.at("Varcus").ms == heldTicks * kTickMs);
    // The phase ends: the same unit is attackable now, still 305 yd off. The kind changes unattackable -> far-held.
    UnitFact loose = Engaged(Unit(guid, 11484, 305.0f));
    loose.attackable = true;
    Facts g = f;
    g.engagedUnits = {loose};
    for (MemberFact& m : g.members)
        m.engaged = {loose};
    std::uint32_t const farTicks = kFarHoldGraceMs / kTickMs;
    for (std::uint32_t i = 1; i < farTicks; ++i) // 1..14.9 s stood far-held: the carried 9.9 s must not count
    {
        Tick(r, g);
        CHECK(r.combatDrops.empty());
    }
    Tick(r, g);
    CHECK((r.combatDrops == std::vector<std::string>{"Tanky", "Varcus"}));
    CHECK(r.note.find("Varcus dropped combat with entry 11484 (305.0 yd away)") != std::string::npos);
}
// The lead bot died alone and came back at another wing's door during a fight state (the run counted a fight while
// the lead was down): the rescue path leaves fighting false, so the run does not keep a fight it already won.
// The no-clear mutation (drop `r.fighting = false` in the LeadRevivedAway rescue arm) leaves fighting true.
static void TestLeadRevivedAwayRescueLeavesFightingFalse()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.doors = TwoDoors();
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(r.fighting);
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f); // the fight is on while the lead bot is down
    CHECK(r.fighting);
    f.anyInCombat = false; // the fight is over with the lead bot down, but the flag stands
    r.islandEscapes = 7; // ResetStep would keep it: the rescue path must clear the fight flag the same way
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.leadX = 2000;
    f.maxMemberYd = 1785;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 1785)};
    f.members[1].x = 215;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.strandDue && !r.fighting);
    CHECK(r.note.find("it is teleported to a safe point on the way it came") != std::string::npos);
}

// The far drop never runs while the run is paused: paused ticks keep no per-bot timer and make no drops.
// The `r.state == State::Running` mutation in farOff keeps dropping through a pause.
static void TestFarHoldNeverDropsWhilePaused()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), Step(PlanStepKind::Boss, 220)};
    plan.steps.back().entry = 9999;
    Facts const f = FarHeldParty(305.0f);
    std::uint32_t const graceTicks = kFarHoldGraceMs / kTickMs;
    Run r = Start(&plan, 2);
    Pause(r, "a player's order");
    for (std::uint32_t i = 0; i < 2 * graceTicks; ++i)
        CHECK(CombatDropTick(r, f, kTickMs).empty());
    CHECK(r.combatDrops.empty() && r.combatDrop.empty());
    // The same run resumed drops on the grace's last tick.
    Run q = Start(&plan, 2);
    for (std::uint32_t i = 0; i + 1 < graceTicks; ++i)
        CHECK(CombatDropTick(q, f, kTickMs).empty());
    CHECK(!CombatDropTick(q, f, kTickMs).empty() &&
        (q.combatDrops == std::vector<std::string>{"Tanky", "Healy"}));
}

// A bot with its engaged set read but empty stands in no held stretch: no timer runs, no drop, no note.
// The `m.engaged.empty()` mutation drops the next bot's stretch under its name (or drops it with nothing engaged).
static void TestCombatDropEmptyEngagedSetDropsNothing()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {6206}), Step(PlanStepKind::Move, 9), Step(PlanStepKind::Move, 200)};
    Facts f = Party(2000); // the other wing's door: 1780 yd from the plan's nearest point
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.engagedRead = true;
    f.engagedUnits = {Bomb()};
    MemberFact lead = Mem("Tanky", 0);
    lead.inCombat = true;
    lead.engagedRead = true; // nothing engaged: outside any stretch
    MemberFact priest = Mem("Varcus", 1);
    priest.inCombat = true;
    priest.engagedRead = true;
    UnitFact held = Bomb();
    held.distLead = 305.0f;
    priest.engaged = {held};
    f.members = {lead, priest};
    f.maxMemberYd = 1;
    Run r = Start(&plan, 2);
    std::string notes;
    TickTimes(r, f, 2 * kFarHoldGraceMs / kTickMs, notes);
    CHECK(r.combatDrop.count("Tanky") == 0);
    CHECK(notes.find("Tanky dropped combat") == std::string::npos);
    CHECK(r.combatDrop.count("Varcus") == 1);
}
// The regroup's rescue teleport at the site clears a non-empty hold (the living party held at the fight spot):
// the bots left at the door are summoned to the teleported lead bot instead of staying held away from the party.
// The `r.hold.clear()` mutation (drop the clear at the regroup site) leaves the stale hold standing.
static void TestRegroupRescueClearsTheHold()
{
    DungeonPlan plan;
    Facts f;
    Run r = WipedAtTheOtherDoor(plan, f);
    // Two bots 100+ yd from the lead bot at the door: UpdateHold keeps them held to the site tick.
    f.members[1].yd = 100;
    f.members[2].yd = 120;
    f.maxMemberYd = 120;
    r.hold = {"Healy", "Magey"}; // set before the site tick: the living party held at the fight spot
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && !r.regroup && !r.holdDue && r.strandDue);
    CHECK(r.hold.empty());
    CHECK(Tick(r, f) == "rescue from 2000.0 0.0 0.0 to 10.0 0.0 0.0");
}




// Scarlet Cathedral r23 slot 5, step 66 (fix 2): Mograine 3976 was fought on a pull step; his fake death (not
// selectable) and a charmed judge read the fight as unattackable only, the grace let it go and the regroup released
// the dead into his and Whitemane's fight. An engaged unit that is a plan boss (a boss step's entry) or an event
// unit (a hold step's entries) keeps the fight, whatever the step; another unattackable unit is still let go.
static void TestUnattackableGraceNeverLetsAPlanBossOrHoldUnitGo()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 60);
    boss.entry = 3976;
    PlanStep hold = Step(PlanStepKind::Hold, 90);
    hold.radius = 30;
    hold.entries = {4299, 3977};
    plan.steps = {PullStep(0, {4300}), Step(PlanStepKind::Move, 30), boss, hold};
    Facts f = Party(0);
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.engagedRead = true;
    std::uint32_t const graceTicks = kUnattackableGraceMs / kTickMs;
    for (std::uint32_t entry : {3976u, 3977u})
    {
        UnitFact u = Engaged(Unit(0xF130000F88009C5Dull, entry, 6));
        u.attackable = false;
        f.engagedUnits = {u};
        Run r = Start(&plan);
        CHECK(UnattackableOnlyFight(f) && !InEvent(r) && !UnattackableFightLetGo(r, f));
        CHECK(PlanBossOrHoldEntry(r, entry) && EngagedPlanBossOrHold(r, f));
        r.fighting = true;
        for (std::uint32_t i = 0; i < graceTicks + 5; ++i)
            Tick(r, f);
        CHECK(r.state == State::Running && r.fighting && r.step == 0 && r.unattackableMs == 0);
        CHECK(r.note.find("not counted as a fight") == std::string::npos);
        // With another, plain unattackable unit beside it, still a fight.
        UnitFact bomb = Bomb();
        f.engagedUnits = {bomb, u};
        CHECK(!UnattackableFightLetGo(r, f) && FightOn(r, f, kTickMs));
    }
    // An unattackable unit the plan does not fight as a boss or an event (the bomb), alone: let go as before.
    f.engagedUnits = {Bomb()};
    Run o = Start(&plan);
    CHECK(!EngagedPlanBossOrHold(o, f) && UnattackableFightLetGo(o, f));
    // A boss step's adds (its `entries`, not its own entry) are no boss: the hold's entries and the boss entry only.
    CHECK(!PlanBossOrHoldEntry(o, 4300) && !PlanBossOrHoldEntry(o, 0));
    Run none;
    CHECK(!PlanBossOrHoldEntry(none, 3976));
}

// Dire Maul West r38 slot B: the Gordok Mauler 11442 (spawn 51062, map 1, Feralas outside the instance) on the hunter
// Pozzle, who stood outside the instance: read from the lead bot inside as "cannot be attacked" at kOtherMapYd.
static const std::uint64_t kMaulerGuid = 0xF130002CB200C776ull;
static UnitFact Mauler()
{
    UnitFact u = Engaged(Unit(kMaulerGuid, 11442, kOtherMapYd));
    u.attackable = false;
    u.health = 7359;
    u.maxHealth = 7359;
    u.partyHits = true;
    return u;
}
static MemberFact OutsidePozzle(bool fighting = true)
{
    MemberFact m = Mem("Pozzle", kOtherMapYd);
    m.offPlanMap = true;
    m.inCombat = fighting;
    m.attackers = fighting ? 1 : 0;
    m.engagedRead = fighting;
    if (fighting)
        m.engaged = {Mauler()};
    return m;
}
// The lead bot "Tanky" out of combat in the instance, Pozzle outside in the Mauler's fight (as the glue reads it).
static Facts OutsideFight()
{
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), OutsidePozzle()};
    f.maxMemberYd = kOtherMapYd;
    f.anyInCombat = true;
    f.engagedRead = true;
    f.engagedUnits = {Mauler()};
    return f;
}
// The tick after the glue's DropUnattackableCombat: Pozzle out of combat for one tick (then the Mauler again).
static Facts OutsideDropped()
{
    Facts f = OutsideFight();
    f.members[1] = OutsidePozzle(false);
    f.anyInCombat = false;
    f.engagedUnits.clear();
    return f;
}
static DungeonPlan GraveyardPlan()
{
    DungeonPlan plan;
    plan.map = 429;
    plan.steps = {PullStep(40, {11473}), Step(PlanStepKind::Move, 400)};
    return plan;
}
static void TestOutsideBotFightIsNoPartyFight()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    r.restDue = false;
    r.fighting = true;                 // the step's fight was on
    r.fightMs = kFightCapMs + 5000;
    Facts f = OutsideFight();
    Facts view;
    CHECK(PlanMapFightView(f, view) && !view.anyInCombat && !view.staleCombat);
    Tick(r, f);
    CHECK(r.state == State::Running && !r.fighting);
    for (int i = 0; i < 30 && r.state == State::Running; ++i)
    {
        Tick(r, f);
        CHECK(!r.fighting && r.note.find("lasted more than") == std::string::npos);
    }
}
static void TestOutsideBotDoesNotHoldTheRegroup()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts fight = OutsideFight();
    fight.alive = 4;
    fight.dead = 1;
    fight.deadNames = {"Hilde"};
    Facts dropped = OutsideDropped();
    dropped.alive = 4;
    dropped.dead = 1;
    dropped.deadNames = {"Hilde"};
    fight.walkBackRescue = false;   // no return teleport (part 7): this test pins the fight view alone
    dropped.walkBackRescue = false;
    CHECK(Tick(r, fight) == "revive party");
    CHECK(r.regroupMs == kTickMs && !r.fighting);
    CHECK(Has(r.note, "regroup: waiting for Hilde (dead), Pozzle (outside the instance), reviving"));
    bool dropNoted = false;
    bool afterDrop = false;
    for (std::uint32_t ms = kTickMs; ms <= kRegroupReviveMs + 2 * kTickMs && r.state == State::Running; ms += kTickMs)
    {
        Tick(r, afterDrop ? dropped : fight);
        afterDrop = !r.combatDrops.empty(); // the glue dropped Pozzle's combat: out one tick, then the Mauler again
        dropNoted = dropNoted || Has(r.note, "Pozzle dropped combat with entry 11442 (outside the instance)");
        CHECK(!r.fighting);
    }
    CHECK(dropNoted);
    CHECK(r.state == State::Paused && Has(r.note, "regroup: 1 still dead after 240 s"));
    CHECK(r.note.find("lasted more than") == std::string::npos);
}
static void TestOutsideBotFightLetsTheWipeRegroup()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    r.fighting = true;
    r.fightMs = 60000;
    Facts f = OutsideFight();
    f.leadAlive = false;
    f.total = 5;
    f.alive = 1;
    f.dead = 4;
    f.deadNames = {"Tanky", "Tobian", "Hilde", "Gougemeister"};
    f.members = {OutsidePozzle()};
    Tick(r, f);
    CHECK(r.regroup && Has(r.note, "regrouping by itself (1 of 2 at this step)"));
    CHECK(r.note.find("fighting on") == std::string::npos);
}
static void TestFightOnThePlanMapStillCounts()
{
    DungeonPlan plan = GraveyardPlan();
    UnitFact mob = Engaged(Unit(0xF13000183E00763Dull, 11473, 12)); // attackable, in the instance
    // (a) a bot in the instance fights too (the lead bot does not): the fight stays, the Mauler leaves the engaged units
    Facts a = OutsideFight();
    MemberFact healer = Mem("Tobian", 10);
    healer.inCombat = true;
    healer.attackers = 1;
    healer.engagedRead = true;
    healer.engaged = {mob};
    a.members.push_back(healer);
    a.engagedUnits = {Mauler(), mob};
    Facts va;
    CHECK(PlanMapFightView(a, va) && va.anyInCombat && !va.staleCombat);
    CHECK(va.engagedUnits.size() == 1 && va.engagedUnits[0].entry == 11473);
    Run ra = Start(&plan);
    Tick(ra, a);
    CHECK(ra.fighting);
    // (b) a living real player in combat keeps the fight
    Facts b = OutsideFight();
    PlayerFact p;
    p.name = "Hypeer";
    p.inCombat = true;
    b.players = {p};
    Facts vb;
    CHECK(PlanMapFightView(b, vb) && vb.anyInCombat && vb.engagedUnits.empty() && vb.staleCombat);
    // (c) 1000 yd without the flag is a bot in the instance: no change
    Facts c = OutsideFight();
    c.members[1].offPlanMap = false;
    Facts vc;
    CHECK(!PlanMapFightView(c, vc));
    // (d) the paused self-resume: units all off the map are quiet; one in the instance, or none read, is a fight
    Run rd = Start(&plan);
    Facts d;
    d.anyInCombat = true;
    d.engagedRead = true;
    d.engagedUnits = {Mauler()};
    CHECK(!FightOn(rd, d, kTickMs));
    d.engagedUnits.push_back(mob);
    CHECK(FightOn(rd, d, kTickMs));
    d.engagedUnits.clear();
    CHECK(FightOn(rd, d, kTickMs));
    // (e) the lead bot dead (its ghost may be on another map): a bot in the instance fighting a unit read at 1000 yd
    // keeps it
    Facts e = OutsideFight();
    e.leadAlive = false;
    MemberFact rogue = Mem("Gougemeister", kOtherMapYd);
    rogue.inCombat = true;
    rogue.attackers = 1;
    UnitFact far = mob;
    far.distLead = kOtherMapYd;
    e.members = {OutsidePozzle(), rogue};
    e.engagedUnits = {Mauler(), far};
    Facts ve;
    CHECK(!PlanMapFightView(e, ve));
    // (f) a bot in the instance with a combat flag and nothing on it, Pozzle outside: the stale flag's grace (no clock)
    Facts s = OutsideFight();
    MemberFact flagged = Mem("Tobian", 10);
    flagged.inCombat = true;
    s.members.push_back(flagged);
    Facts vs;
    CHECK(PlanMapFightView(s, vs) && vs.anyInCombat && vs.engagedUnits.empty() && vs.staleCombat);
    Run rs = Start(&plan);
    for (int i = 0; i < 5; ++i)
        Tick(rs, s);
    CHECK(rs.fighting && rs.fightMs == 0);
}
static void TestRegroupQuietTickEndsTheFightClock()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts quiet = Party(0);
    quiet.alive = 4;
    quiet.dead = 1;
    quiet.deadNames = {"Hilde"};
    quiet.members = {Mem("Tanky", 0)};
    Facts fight = quiet;
    fight.anyInCombat = true;
    fight.leadInCombat = true;
    fight.members[0].inCombat = true;
    fight.engagedRead = true;
    fight.engagedUnits = {Engaged(Unit(0xF13000183E00763Dull, 11473, 5))};
    for (int i = 0; i < 100; ++i)
        Tick(r, fight);
    CHECK(r.state == State::Running && r.fighting && r.fightMs == 100 * kTickMs);
    Tick(r, quiet);
    CHECK(r.regroup && r.fightMs == 0);
    Tick(r, fight);
    CHECK(r.fightMs == kTickMs);
    for (int i = 0; i < 100; ++i)
        Tick(r, fight);
    CHECK(r.state == State::Running);
}

static void TestOutsideBotIsReturnedInABotOnlyRun()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts f = OutsideFight();
    std::string order;
    CHECK(OffMapReturnTick(r, f, order) && order == "return Pozzle 429");
    CHECK(r.offMapReturnTries["Pozzle"] == 1 && r.state == State::Running);
    CHECK(Has(r.note, "Pozzle is outside the instance: teleported back to the lead bot (try 1 of 3 at this step)"));
    // The 30 s: not before, then once more
    order.clear();
    r.runMs += kOffMapReturnRepeatMs - 1000;
    CHECK(!OffMapReturnTick(r, f, order) && order.empty());
    r.runMs += 1000;
    CHECK(OffMapReturnTick(r, f, order) && order == "return Pozzle 429" && r.offMapReturnTries["Pozzle"] == 2);
    // Through Tick: a tick with no order of its own sends it (out of combat too: whether it fights does not matter)
    Run t = Start(&plan);
    BeginRegroup(t);
    Facts g = OutsideDropped();          // Pozzle outside, out of combat
    g.alive = 4;
    g.dead = 1;
    g.deadNames = {"Hilde"};
    CHECK(Tick(t, g) == "revive party"); // the step's own order first
    CHECK(Tick(t, g) == "return Pozzle 429");
    // A refusal is no pause
    OrderRefused(t, "return Pozzle 429", "player: a real player is in the party");
    CHECK(t.state == State::Running && Has(t.note, "refused: player: a real player is in the party: tried again in 30 s"));
}
static void TestOutsideBotIsNotReturnedWhenGuarded()
{
    DungeonPlan plan = GraveyardPlan();
    std::string order;
    // (a) a real player in the party
    Run a = Start(&plan);
    Facts fa = OutsideFight();
    PlayerFact p;
    p.name = "Hypeer";
    fa.players = {p};
    CHECK(!OffMapReturnTick(a, fa, order) && order.empty() && a.offMapReturnTries.empty());
    // (b) the lead bot dead
    Run b = Start(&plan);
    Facts fb = OutsideFight();
    fb.leadAlive = false;
    CHECK(!OffMapReturnTick(b, fb, order) && order.empty());
    // (c) the owner's switch off
    Run c = Start(&plan);
    Facts fc = OutsideFight();
    fc.walkBackRescue = false;
    CHECK(!OffMapReturnTick(c, fc, order) && order.empty());
    // (d) a lead bot teleport due or settling
    Run d = Start(&plan);
    d.strandDue = true;
    CHECK(!OffMapReturnTick(d, OutsideFight(), order) && order.empty());
    d.strandDue = false;
    d.strandOn = true;
    CHECK(!OffMapReturnTick(d, OutsideFight(), order) && order.empty());
    // (e) a bot at 1000 yd on the plan's map (no flag), and a revive summon under way
    Run e = Start(&plan);
    Facts fe = OutsideFight();
    fe.members[1].offPlanMap = false;
    CHECK(!OffMapReturnTick(e, fe, order) && order.empty());
    Facts fs = OutsideFight();
    e.reviveSummons["Pozzle"] = 0;
    CHECK(!OffMapReturnTick(e, fs, order) && order.empty());
    // (f) the tick's own order is never displaced
    Run g = Start(&plan);
    order = "revive party";
    CHECK(!OffMapReturnTick(g, OutsideFight(), order) && order == "revive party");
    // (g) paused: nothing
    Run h = Start(&plan);
    Pause(h, "x");
    order.clear();
    CHECK(!OffMapReturnTick(h, OutsideFight(), order) && order.empty());
}
static void TestOutsideBotReturnsArePausedAfterThreeAStep()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts f = OutsideFight();
    std::string order;
    for (std::uint32_t i = 1; i <= kOffMapReturnTries; ++i)
    {
        order.clear();
        CHECK(OffMapReturnTick(r, f, order) && order == "return Pozzle 429" && r.offMapReturnTries["Pozzle"] == i);
        r.runMs += kOffMapReturnRepeatMs;
    }
    order.clear();
    CHECK(OffMapReturnTick(r, f, order) && order.empty() && r.state == State::Paused);
    CHECK(r.note == "Pozzle is outside the instance (not on map 429) after 3 returns at this step: bring it to the lead "
        "bot, then .bot run resume");
    // A resume gives three more
    CHECK(Resume(r));
    CHECK(r.offMapReturnTries.empty());
    // A new step starts the count over too
    Run s = Start(&plan);
    s.offMapReturnStep = 0;
    s.offMapReturnTries["Pozzle"] = kOffMapReturnTries;
    s.step = 1;
    order.clear();
    CHECK(OffMapReturnTick(s, f, order) && order == "return Pozzle 429" && s.offMapReturnTries["Pozzle"] == 1);
}

// Round 1 finding 1a (2026-10-24): four bots outside, each returned once, is no pause: the tries count per bot.
static void TestOutsideBotReturnsCountPerBot()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members.clear();
    static char const* names[] = {"Pozzle", "Hilde", "Tobian", "Gougemeister"};
    for (char const* name : names)
    {
        MemberFact m = Mem(name, kOtherMapYd);
        m.offPlanMap = true;
        f.members.push_back(m);
    }
    for (int i = 0; i < 4; ++i)
    {
        std::string order;
        Facts one = Party(0);
        one.members = {f.members[static_cast<std::size_t>(i)]};
        CHECK(OffMapReturnTick(r, one, order) && order == "return " + f.members[static_cast<std::size_t>(i)].name + " 429");
        CHECK(r.state == State::Running);
        r.runMs += kOffMapReturnRepeatMs;
    }
    for (char const* name : names)
        CHECK(r.offMapReturnTries[name] == 1);
}
// Round 1 finding 1b: one bot returned 3 times at one step, outside again, pauses naming it.
static void TestOutsideBotPauseNamesTheBotThatNeededFour()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts poz = Party(0);
    poz.members = {OutsidePozzle()};
    Facts hil = Party(0);
    MemberFact hilde = Mem("Hilde", kOtherMapYd);
    hilde.offPlanMap = true;
    hil.members = {hilde};
    std::string order;
    for (std::uint32_t i = 1; i <= kOffMapReturnTries; ++i)
    {
        order.clear();
        CHECK(OffMapReturnTick(r, poz, order) && order == "return Pozzle 429");
        r.runMs += kOffMapReturnRepeatMs;
    }
    order.clear();
    CHECK(OffMapReturnTick(r, hil, order) && order == "return Hilde 429" && r.state == State::Running);
    r.runMs += kOffMapReturnRepeatMs;
    order.clear();
    CHECK(OffMapReturnTick(r, poz, order) && order.empty() && r.state == State::Paused);
    CHECK(Has(r.note, "Pozzle is outside the instance"));
}
// Round 1 finding 1c: Resume and a new step reset every bot's count.
static void TestOutsideBotPerBotCountsReset()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    MemberFact poz = OutsidePozzle();
    MemberFact hilde = Mem("Hilde", kOtherMapYd);
    hilde.offPlanMap = true;
    f.members = {poz, hilde};
    std::string order;
    CHECK(OffMapReturnTick(r, f, order) && order == "return Pozzle 429");
    r.runMs += kOffMapReturnRepeatMs;
    Facts g = Party(0);
    g.members = {hilde};
    order.clear();
    CHECK(OffMapReturnTick(r, g, order) && order == "return Hilde 429");
    CHECK(r.offMapReturnTries["Pozzle"] == 1 && r.offMapReturnTries["Hilde"] == 1);
    Pause(r, "setup");
    CHECK(Resume(r));
    CHECK(r.offMapReturnTries.empty());
    Run s = Start(&plan);
    s.offMapReturnStep = 0;
    s.offMapReturnTries["Pozzle"] = kOffMapReturnTries;
    s.offMapReturnTries["Hilde"] = kOffMapReturnTries;
    s.step = 1;
    order.clear();
    CHECK(OffMapReturnTick(s, f, order) && order == "return Pozzle 429" && s.offMapReturnTries["Pozzle"] == 1);
}
// Round 1 finding 2: a bot outside the instance but not in the open world gets a note, never a return or a pause.
static void TestOutsideBotNotInTheOpenWorldIsSkipped()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts f = OutsideFight();
    f.members[1].offMapReturnable = false;
    std::string order;
    CHECK(!OffMapReturnTick(r, f, order) && order.empty() && r.state == State::Running);
    CHECK(Has(r.note, "Pozzle is outside the instance but cannot be returned (not in the open world): left alone"));
    CHECK(r.offMapReturnTries.empty());
    r.runMs += kOffMapReturnRepeatMs;
    order.clear();
    CHECK(!OffMapReturnTick(r, f, order) && order.empty() && r.state == State::Running);
}
// Round 2 (2026-10-24): (a) a non-returnable bot outside for 10 ticks: the note is identical after tick 1, no growth.
static void TestOutsideBotNotReturnableNotedOncePerStep()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts f = OutsideFight();
    f.members[1].offMapReturnable = false;
    std::string order;
    CHECK(!OffMapReturnTick(r, f, order) && order.empty() && r.state == State::Running);
    std::string const first = r.note;
    CHECK(Has(first, "Pozzle is outside the instance but cannot be returned (not in the open world): left alone"));
    for (int i = 0; i < 9; ++i)
    {
        order.clear();
        r.runMs += kTickMs;
        CHECK(!OffMapReturnTick(r, f, order) && order.empty() && r.state == State::Running);
        CHECK(r.note == first);
    }
}
// Round 2 (2026-10-24): (b) a new step reports the same bot once again; coming back drops its entry.
static void TestOutsideBotNotReturnableNotedAgainOnNewStep()
{
    DungeonPlan plan = GraveyardPlan();
    Facts f = OutsideFight();
    f.members[1].offMapReturnable = false;
    Run r = Start(&plan);
    r.step = 3;
    std::string order;
    CHECK(!OffMapReturnTick(r, f, order) && r.offMapNotedStep == 3 && r.offMapNoted.count("Pozzle"));
    std::string const first = r.note;
    order.clear();
    CHECK(!OffMapReturnTick(r, f, order) && r.note == first);
    // The bot came back inside: its entry is dropped, so going out again reports it.
    Facts back = OutsideFight();
    back.members[1].offPlanMap = false;
    order.clear();
    CHECK(!OffMapReturnTick(r, back, order) && !r.offMapNoted.count("Pozzle") && r.note == first);
    order.clear();
    CHECK(!OffMapReturnTick(r, f, order) && r.offMapNoted.count("Pozzle") && Has(r.note, first));
    // A new step starts the report over too (NextStep over the plan's last point finishes the run, so step on
    // the facts instead: the noted set follows the step, like the per-bot tries).
    r.step = 4;
    order.clear();
    CHECK(!OffMapReturnTick(r, f, order) && r.offMapNotedStep == 4 && r.offMapNoted.count("Pozzle"));
}
// Round 3 (2026-10-25): (a) a reported bot comes back while the tick already has an order, then leaves again
// at the same step: the guarded return tick still drops its entry, so leaving reports it again.
static void TestOutsideBotComebackUnderOrderReportsAgain()
{
    DungeonPlan plan = GraveyardPlan();
    Facts f = OutsideFight();
    f.members[1].offMapReturnable = false;
    Run r = Start(&plan);
    r.step = 3;
    std::string order;
    CHECK(!OffMapReturnTick(r, f, order) && r.offMapNoted.count("Pozzle"));
    std::string const first = r.note;
    // The bot came back inside on a tick whose order TickBody already set: no report, but the entry is dropped.
    Facts back = OutsideFight();
    back.members[1].offPlanMap = false;
    order = "revive party";
    CHECK(!OffMapReturnTick(r, back, order) && order == "revive party");
    CHECK(!r.offMapNoted.count("Pozzle"));
    // Out again at the same step: reported again (the note piece is replaced in place, so the note reads the same).
    order.clear();
    CHECK(!OffMapReturnTick(r, f, order) && r.offMapNoted.count("Pozzle") && r.note == first);
    CHECK(Has(r.note, first));
}
// Round 3 (2026-10-25): (b) the same when the return tick is guarded by a strand rescue, or by the dead lead.
static void TestOutsideBotComebackUnderStrandOrDeadLeadReportsAgain()
{
    DungeonPlan plan = GraveyardPlan();
    Facts f = OutsideFight();
    f.members[1].offMapReturnable = false;
    Facts back = OutsideFight();
    back.members[1].offPlanMap = false;
    // A strand rescue active on the return tick.
    Run r = Start(&plan);
    r.step = 3;
    std::string order;
    CHECK(!OffMapReturnTick(r, f, order) && r.offMapNoted.count("Pozzle"));
    std::string const first = r.note;
    r.strandDue = true;
    CHECK(!OffMapReturnTick(r, back, order));
    CHECK(!r.offMapNoted.count("Pozzle"));
    r.strandDue = false;
    order.clear();
    CHECK(!OffMapReturnTick(r, f, order) && r.offMapNoted.count("Pozzle") && r.note == first);
    // The lead dead on the return tick.
    Run d = Start(&plan);
    d.step = 3;
    order.clear();
    CHECK(!OffMapReturnTick(d, f, order) && d.offMapNoted.count("Pozzle"));
    std::string const dfirst = d.note;
    Facts deadBack = back;
    deadBack.leadAlive = false;
    CHECK(!OffMapReturnTick(d, deadBack, order));
    CHECK(!d.offMapNoted.count("Pozzle"));
    order.clear();
    CHECK(!OffMapReturnTick(d, f, order) && d.offMapNoted.count("Pozzle") && d.note == dfirst);
}
// Round 2 (2026-10-24): (c) the busy wording versus the open-world wording.
static void TestOutsideBotNotReturnableBusyWording()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts f = OutsideFight();
    f.members[1].offMapReturnable = false;
    f.members[1].offMapBlock = MemberFact::OffMapBlock::Busy;
    std::string order;
    CHECK(!OffMapReturnTick(r, f, order) && order.empty() && r.state == State::Running);
    CHECK(Has(r.note, "Pozzle is outside the instance and busy (summon or teleport pending): left alone"));
    CHECK(r.note.find("not in the open world") == std::string::npos);
    Run o = Start(&plan);
    Facts g = OutsideFight();
    g.members[1].offMapReturnable = false;
    g.members[1].offMapBlock = MemberFact::OffMapBlock::OpenWorld;
    order.clear();
    CHECK(!OffMapReturnTick(o, g, order) && order.empty() && o.state == State::Running);
    CHECK(Has(o.note, "Pozzle is outside the instance but cannot be returned (not in the open world): left alone"));
    CHECK(o.note.find("and busy") == std::string::npos);
}
// Round 1 finding 3: the lead bot's own fact off the plan's map is never returned (the m.name == f.carrier skip).
static void TestOutsideLeadBotIsNeverReturned()
{
    DungeonPlan plan = GraveyardPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    MemberFact lead = Mem("Tanky", kOtherMapYd);
    lead.offPlanMap = true;
    f.members = {lead};
    f.carrier = "Tanky";
    std::string order;
    CHECK(!OffMapReturnTick(r, f, order) && order.empty() && r.state == State::Running);
    CHECK(r.offMapReturnTries.empty());
}

// Scarlet Graveyard r23 slot 7 (Scorn, event 81) and Codex review round 1: the skip reads every event association of
// every spawn row; a row also listed under a running event, a row with no event and a removal row all keep the pause.
static void TestSpawnEventsOffReadsEveryAssociation()
{
    auto activeIs12 = [](std::uint32_t id) { return id == 12; };
    // One spawn, one inactive event: off, named.
    SpawnEventGate g = SpawnEventsOff({{81}}, 0, activeIs12);
    CHECK(g.off && g.eventId == 81);
    // One spawn in an inactive AND an active event: no skip (the old pause), whichever is listed first.
    CHECK(!SpawnEventsOff({{81, 12}}, 0, activeIs12).off);
    CHECK(!SpawnEventsOff({{12, 81}}, 0, activeIs12).off);
    // A spawn event plus a removal row: no skip.
    CHECK(!SpawnEventsOff({{81, -81}}, 0, activeIs12).off);
    // Two spawns, one without any event: no skip.
    CHECK(!SpawnEventsOff({{81}, {}}, 0, activeIs12).off);
    // No spawn row: no skip.
    CHECK(!SpawnEventsOff({}, 0, activeIs12).off);
    // Two event-gated spawns: off, the own spawn's event named.
    g = SpawnEventsOff({{81}, {7, 9}}, 1, activeIs12);
    CHECK(g.off && g.eventId == 7);
    // The own spawn unknown (no index): the first row's event.
    g = SpawnEventsOff({{81}, {7}}, 5, activeIs12);
    CHECK(g.off && g.eventId == 81);
}

// Shadowfang Keep r36 slot A step 304: Fenrus 4274 stood at his spot behind the Sorcerer's Gate (z 128.8), the party 33 yd
// below him on z 95. Two pulls drew no fight (AwaitPull passed him after kPullTriesPerUnit), and the step said 'boss
// 4274 not at his spot: waiting for him', waited kBossSearchMs and, after the instance search, paused 'is alive at
// -135.6 2168.7 128.8, 0.0 yd from his spot': twice, 60 s each. A boss alive in sight by his spot whom the pulls passed
// is not away: the step pauses at once and says where he stands from the lead bot. The height is his real one, not the
// plan point's (Codex review round 1: a boss inside the radius but at another height read the plan's height).
static void TestPassedBossAtHisSpotPausesAtOnce()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 0, 0, 0);
    boss.entry = 4274;
    boss.entries = {7076};
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    UnitFact seen = Unit(5, 4274, 33.1f);
    seen.z = 12.0f; // his real height: 12 yd above the plan point, still inside the radius
    f.units = {seen};
    int pulls = 0;
    for (std::uint32_t i = 0; i < 2 * kPullNoFightMs / kTickMs + 10 && r.state == State::Running; ++i)
        pulls += Tick(r, f) == "pullback 5" ? 1 : 0;
    CHECK(pulls == 2 && r.passed.count(5));
    CHECK(r.state == State::Paused && r.step == 0 && r.bossWaitMs == 0 && !r.bossSearchWanted);
    CHECK(r.note == "boss 4274 is alive by his spot, 33.1 yd from the lead bot (12.0 yd above it), and 2 pulls drew no "
        "fight with him: bring the party where it can reach him, then .bot run resume, or .bot run skip");
    // An add passed, the boss not in sight: he is away, the wait as before.
    Run a = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(6, 7076, 20.0f)};
    a.passed.insert(6);
    Tick(a, g);
    CHECK(a.state == State::Running && a.note == "boss 4274 not at his spot: waiting for him");
    // The boss in sight and NOT passed is pulled, never paused (the control).
    Run c = Start(&plan);
    CHECK(Tick(c, f) == "pullback 5" && c.state == State::Running);
}

// The instance search's alive pause names where he stands from the lead bot too (r36 slot A: 0.0 yd from his spot,
// and the party 33 yd below him, which the note did not say).
static void TestAliveSearchPauseSaysWhereTheLeadBotIs()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 3976;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run a = Start(&plan);
    Facts f = Party(0);
    f.leadZ = 10.0f;
    for (std::uint32_t i = 0; i < kBossSearchMs / kTickMs + 2 && !a.bossSearchWanted && a.state == State::Running; ++i)
        Tick(a, f);
    CHECK(a.bossSearchWanted);
    Facts alive = f;
    alive.bossSearchRead = true;
    alive.bossSearchAlive = true;
    alive.bossAliveX = 30.0f;
    alive.bossAliveZ = 43.0f;
    Tick(a, alive);
    CHECK(a.state == State::Paused);
    CHECK(a.note == "boss 3976 is alive at 30.0 0.0 43.0, 43.0 yd from his spot (an instance search after 60 s), 44.6 yd "
        "from the lead bot (33.0 yd above it): bring the party to him or wait for him, then .bot run resume, or .bot run skip");
}
// Stormwrought Ruins leg 9 (specs/sw-eldermaw.md, Spec A): Eldermaw 62661 swims a pool whose bed navmesh is cut off
// from the shore, so the boss row carries kPlanFlagSkipUnreachable. Where the step would pause on him ALIVE it ends
// "not cleared" with a note instead, and the run goes on.
static void TestSkipUnreachableBossAliveElsewhereEndsNotCleared()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 3976;
    ai::NormalizePlanStep(boss);
    boss.flags |= ai::kPlanFlagSkipUnreachable;
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run a = Start(&plan);
    Facts f = Party(0);
    f.leadZ = 10.0f;
    for (std::uint32_t i = 0; i < kBossSearchMs / kTickMs + 2 && !a.bossSearchWanted && a.state == State::Running; ++i)
        Tick(a, f);
    Facts alive = f;
    alive.bossSearchRead = true;
    alive.bossSearchAlive = true;
    alive.bossAliveX = 30.0f;
    alive.bossAliveZ = 43.0f;
    Tick(a, alive);
    CHECK(a.state == State::Running);
    CHECK(a.step == 1);
    CHECK(a.notCleared.count(0));
    CHECK(a.note == "boss 3976 is alive at 30.0 0.0 43.0, 43.0 yd from his spot (an instance search after 60 s), 44.6 yd "
        "from the lead bot (33.0 yd above it): skipped, not cleared (the plan marks him out of reach)");
}

static void TestSkipUnreachableBossPassedBySpotEndsNotCleared()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 0, 0, 0);
    boss.entry = 3976;
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    // Today's behaviour without the flag: the step pauses and says the pulls drew no fight with him.
    Run p = Start(&plan);
    Facts f = Party(0);
    UnitFact seen = Unit(7, 3976, 33.1f);
    seen.hostile = true;
    f.seen = true;
    f.units = {seen};
    p.passed.insert(7);
    p.pullTries[7] = 3;
    Tick(p, f);
    CHECK(p.state == State::Paused);
    CHECK(p.note.find("and 3 pulls drew no fight with him") != std::string::npos);
    // With the flag: not cleared with a note, and the run goes on.
    plan.steps[0].flags |= ai::kPlanFlagSkipUnreachable;
    Run a = Start(&plan);
    a.passed.insert(7);
    a.pullTries[7] = 3;
    Tick(a, f);
    CHECK(a.state == State::Running);
    CHECK(a.step == 1);
    CHECK(a.notCleared.count(0));
    CHECK(a.note.size() >= 56 && a.note.compare(a.note.size() - 56, 56,
        ": skipped, not cleared (the plan marks him out of reach)") == 0);
    CHECK(a.note.find("and 3 pulls drew no fight with him") != std::string::npos);
}

// Spec B: a boss row with StandPull holds at the move row right before it (the stand) and pulls from there once the
// boss is in the tank's shot. Setup shared by the tests below.
static DungeonPlan StandPullPlan(bool skip)
{
    DungeonPlan plan;
    PlanStep stand = Step(PlanStepKind::Move, -5842.0f, -3807.0f, 200.7f);
    stand.leg = 0;
    PlanStep boss = Step(PlanStepKind::Boss, -5820.0f, -3797.19f, 201.241f);
    boss.leg = 0;
    boss.entry = 3976;
    boss.radius = 95.0f;
    boss.flags = ai::kPlanFlagStandPull | (skip ? ai::kPlanFlagSkipUnreachable : 0);
    ai::NormalizePlanStep(boss);
    plan.steps = {stand, boss, Step(PlanStepKind::Move, 60)};
    return plan;
}

// A stand-pull add: seen by the tank, so the stand rule ranks it when it is nearer than the boss.
static UnitFact StandPullAdd(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u = Unit(guid, entry, dist);
    u.losLead = true;
    return u;
}

// A stand-pull boss step with two adds (entries 3979/3980): with the adds nearer than the boss, the
// stand rule ranks a shootable unit first, so a pull of another unit leaves the boss's for the next tick.
static DungeonPlan StandPullPlanWithAdds()
{
    DungeonPlan plan;
    PlanStep stand = Step(PlanStepKind::Move, -5842.0f, -3807.0f, 200.7f);
    stand.leg = 0;
    PlanStep boss = Step(PlanStepKind::Boss, -5820.0f, -3797.19f, 201.241f);
    boss.leg = 0;
    boss.entry = 3976;
    boss.entries = {3979, 3980};
    boss.radius = 95.0f;
    boss.flags = ai::kPlanFlagStandPull;
    ai::NormalizePlanStep(boss);
    plan.steps = {stand, boss, Step(PlanStepKind::Move, 60)};
    return plan;
}
static Facts StandPullFacts(float dist, bool los = true, float shot = 30.0f)
{
    Facts f = Party(0);
    f.leadX = -5842.0f;
    f.leadY = -3807.0f;
    f.leadZ = 200.7f;
    f.seen = true;
    f.pullStand = true;
    f.leadShotYd = shot;
    UnitFact u = Unit(9, 3976, dist);
    u.hostile = true;
    u.losLead = los;
    f.units = {u};
    return f;
}

// Out of shot (50 yd): the step waits at the stand, no order, no firing-stand read.
static void TestStandPullWaitsAtTheStandOutOfShot()
{
    DungeonPlan plan = StandPullPlan(false);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(50.0f);
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note == "boss 3976 patrols: waiting at the stand for him to come within 29.0 yd (now 50.0 yd)");
    CHECK(r.shotStandFor == 0);
}

// In shot (28 yd): the pullback goes out on the first tick.
static void TestStandPullPullsFromTheStandInShot()
{
    DungeonPlan plan = StandPullPlan(false);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(28.0f);
    CHECK(Tick(r, f) == "pullback 9");
}

// In range but out of sight: wait, saying so.
static void TestStandPullWaitsWhileOutOfSight()
{
    DungeonPlan plan = StandPullPlan(false);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(20.0f, false);
    CHECK(Tick(r, f).empty());
    CHECK(r.note == "boss 3976 patrols: waiting at the stand for him to come within 29.0 yd (now 20.0 yd, out of sight)");
}

// Out of combat 12 yd from the stand: the order walks back to the stand.
static void TestStandPullWalksBackToTheStand()
{
    DungeonPlan plan = StandPullPlan(false);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(50.0f);
    f.leadX = -5830.0f;
    f.leadY = -3807.0f;
    f.leadZ = 200.7f;
    CHECK(Tick(r, f) == "move -5842.0 -3807.0 200.7");
}

// 600 s out of shot: with the skip flag the step is not cleared and the run goes on; without it the run pauses.
// One tick before the ceiling the step still waits (a 599 s wait is not 600 s: kStandPullWaitMs 600000, not 2000).
static void TestStandPullCeilingSkipsWithTheSkipFlag()
{
    DungeonPlan plan = StandPullPlan(true);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(50.0f);
    std::string order;
    for (std::uint32_t i = 0; i < kStandPullWaitMs / kTickMs - 1 && r.step == 1 && r.state == State::Running; ++i)
        order = Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(order.empty());
    CHECK(r.note == "boss 3976 patrols: waiting at the stand for him to come within 29.0 yd (now 50.0 yd)");
    CHECK(r.standPullMs == kStandPullWaitMs - kTickMs);
    Tick(r, f);
    CHECK(r.state == State::Running);
    CHECK(r.step == 2);
    CHECK(r.notCleared.count(1));
    CHECK(r.note == "boss 3976 did not come within 29.0 yd of the stand in 600 s: skipped, not cleared (the plan marks him out of reach)");
    DungeonPlan plain = StandPullPlan(false);
    Run p = Start(&plain, 1);
    for (std::uint32_t i = 0; i < kStandPullWaitMs / kTickMs && p.step == 1 && p.state == State::Running; ++i)
        Tick(p, f);
    CHECK(p.state == State::Paused);
    CHECK(p.note == "boss 3976 did not come within 29.0 yd of the stand in 600 s: bring him to the party, then .bot run resume, or .bot run skip");
}

// The 600 s is cumulative per step across pauses, resumes and wipe recoveries of the SAME step: 300 s waited, a
// pause without the flag, a resume, 299 s more still waits, the 300th s after it ends (not later). Skipping the
// flag waits without one, so the run pauses there.
static void TestStandPullCeilingAccumulatesAcrossPauseAndResume()
{
    // The ceiling pause cannot be resumed directly (Tick would pause again at once on the same step), so the
    // resume leg goes through a no-flag plan: the timer it carries is what the test pins.
    DungeonPlan plain = StandPullPlan(false);
    Run r = Start(&plain, 1);
    Facts f = StandPullFacts(50.0f);
    for (std::uint32_t i = 0; i < 300 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.standPullMs == 300 * kTickMs);
    Pause(r, "test pause");
    CHECK(Resume(r));
    for (std::uint32_t i = 0; i < 299 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.standPullMs == 599 * kTickMs);
    CHECK(r.note == "boss 3976 patrols: waiting at the stand for him to come within 29.0 yd (now 50.0 yd)");
    Tick(r, f);
    CHECK(r.state == State::Paused);
    CHECK(r.standPullMs == kStandPullWaitMs);
    CHECK(r.note == "boss 3976 did not come within 29.0 yd of the stand in 600 s: bring him to the party, then .bot run resume, or .bot run skip");
    // The same 300 + 301 with the skip flag ends it instead.
    DungeonPlan plan = StandPullPlan(true);
    Run s = Start(&plan, 1);
    for (std::uint32_t i = 0; i < 300 && s.state == State::Running; ++i)
        Tick(s, f);
    Pause(s, "test pause");
    CHECK(Resume(s));
    for (std::uint32_t i = 0; i < 300 && s.state == State::Running && s.step == 1; ++i)
        Tick(s, f);
    CHECK(s.state == State::Running && s.step == 2 && s.notCleared.count(1));
    CHECK(s.note == "boss 3976 did not come within 29.0 yd of the stand in 600 s: skipped, not cleared (the plan marks him out of reach)");
    CHECK(s.standPullStep == 2); // NextStep moved on: the timer belongs to the new step
    DungeonPlan two;
    {
        PlanStep stand = Step(PlanStepKind::Move, -5842.0f, -3807.0f, 200.7f);
        stand.leg = 0;
        PlanStep first = Step(PlanStepKind::Boss, -5820.0f, -3797.19f, 201.241f);
        first.leg = 0;
        first.entry = 3976;
        first.radius = 95.0f;
        first.flags = ai::kPlanFlagStandPull | ai::kPlanFlagSkipUnreachable;
        ai::NormalizePlanStep(first);
        PlanStep back = Step(PlanStepKind::Move, -5842.0f, -3807.0f, 200.7f);
        back.leg = 0;
        PlanStep second = Step(PlanStepKind::Boss, -5820.0f, -3797.19f, 201.241f);
        second.leg = 0;
        second.entry = 3977;
        second.radius = 95.0f;
        second.flags = ai::kPlanFlagStandPull | ai::kPlanFlagSkipUnreachable;
        ai::NormalizePlanStep(second);
        two.steps = {stand, first, back, second, Step(PlanStepKind::Move, 60)};
    }
    Run t = Start(&two, 1);
    for (std::uint32_t i = 0; i < 300 && t.state == State::Running && t.step == 1; ++i)
        Tick(t, f);
    CHECK(t.standPullMs == 300 * kTickMs);
    NextStep(t, "test past the boss");
    CHECK(t.step == 2 && t.standPullMs == 0 && t.standPullStep == 2);
}

// A stand-pull read with the key off still waits and pulls: for a stand-pull boss step the glue reads leadShotYd
// and losLead whatever the key says (the key governs ordinary pulls only). Wired at the policy level: the gate
// uses leadShotYd 30 with pullStand false, not the shipped default's 'no ranged pull'.
static void TestStandPullGateReadsShotWithTheKeyOff()
{
    DungeonPlan plan = StandPullPlan(false);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(50.0f);
    f.pullStand = false; // the shipped default: ReadPullStand returns early for ordinary pulls
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note == "boss 3976 patrols: waiting at the stand for him to come within 29.0 yd (now 50.0 yd)");
    Facts in = StandPullFacts(28.0f);
    in.pullStand = false;
    CHECK(Tick(r, in) == "pullback 9");
}

// The no-ranged branch with the key off: leadShotYd 0 still gives up at once.
static void TestStandPullNoRangedPullKeyOff()
{
    DungeonPlan plan = StandPullPlan(true);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(50.0f, true, 0.0f);
    f.pullStand = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2 && r.notCleared.count(1));
    CHECK(r.note == "the tank has no ranged pull for a stand pull: skipped, not cleared (the plan marks him out of reach)");
}

// No ranged pull (leadShotYd 0): the give-up comes at once, skip and pause variants.
static void TestStandPullNoRangedPull()
{
    DungeonPlan plan = StandPullPlan(true);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(50.0f, true, 0.0f);
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2 && r.notCleared.count(1));
    CHECK(r.note == "the tank has no ranged pull for a stand pull: skipped, not cleared (the plan marks him out of reach)");
    DungeonPlan plain = StandPullPlan(false);
    Run p = Start(&plain, 1);
    Tick(p, f);
    CHECK(p.state == State::Paused);
    CHECK(p.note == "the tank has no ranged pull for a stand pull: bring him to the party, then .bot run resume, or .bot run skip");
}

// Without a stand (boss as step 0): the step pulls as before (the stand read is asked).
static void TestStandPullWithoutStandPullsAsBefore()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, -5820.0f, -3797.19f, 201.241f);
    boss.entry = 3976;
    boss.radius = 95.0f;
    boss.flags = ai::kPlanFlagStandPull;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.pullStand = true;
    f.leadShotYd = 30.0f;
    UnitFact u = Unit(9, 3976, 28.0f);
    u.hostile = true;
    u.losLead = true;
    f.units = {u};
    CHECK(Tick(r, f).empty());
    CHECK(r.shotStandFor == 9);
}

// The move row before the boss in another leg is no stand (prev.leg != s.leg): the old path, the stand read asked.
static void TestStandPullStandInAnotherLegPullsAsBefore()
{
    DungeonPlan plan;
    PlanStep stand = Step(PlanStepKind::Move, -5842.0f, -3807.0f, 200.7f);
    stand.leg = 1;
    PlanStep boss = Step(PlanStepKind::Boss, -5820.0f, -3797.19f, 201.241f);
    boss.leg = 0;
    boss.entry = 3976;
    boss.radius = 95.0f;
    boss.flags = ai::kPlanFlagStandPull;
    ai::NormalizePlanStep(boss);
    plan.steps = {stand, boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan, 1);
    Facts f = Party(0);
    f.leadX = -5842.0f;
    f.leadY = -3807.0f;
    f.leadZ = 200.7f;
    f.seen = true;
    f.pullStand = true;
    f.leadShotYd = 30.0f;
    UnitFact u = Unit(9, 3976, 28.0f);
    u.hostile = true;
    u.losLead = true;
    f.units = {u};
    CHECK(Tick(r, f).empty());
    CHECK(r.shotStandFor == 9);
}

// The gate needs a quiet party: with the combat flag up (here stale: nothing engaged after the last kill) the
// fight block owns the tick (note "fighting", empty order) and no stand wait accrues, instead of the gate's wait.
static void TestStandPullGateOffWhileInCombat()
{
    DungeonPlan plan = StandPullPlan(false);
    Run r = Start(&plan, 1);
    Facts f = StandPullFacts(28.0f);
    f.anyInCombat = true;
    f.staleCombat = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.fighting && r.note == "fighting" && r.standPullMs == 0);
}
// The authorization clears on a held-up pull path: with it set for the boss (a stale guid from an earlier
// in-shot tick), a gather first clears it, then the boss in shot still pulls from the stand (shotStandFor stays 0).
static void TestStandPullShotLatchClearsOnGather()
{
    DungeonPlan plan = StandPullPlan(false);
    Run r = Start(&plan, 1);
    Facts wait = StandPullFacts(28.0f);
    wait.maxMemberYd = kGatherYd + 1.0f;
    r.standPullShotGuid = 9; // a stale authorization: the wait path clears it since the pull never went out
    std::string gather = Tick(r, wait);
    CHECK(!gather.empty() && r.note == "gathering before a pull");
    CHECK(r.standPullShotGuid == 0);
    Facts back = StandPullFacts(28.0f);
    CHECK(Tick(r, back) == "pullback 9");
    CHECK(r.standPullShotGuid == 0);
}

// The in-shot authorization covers only the judged unit: the boss in shot sets it, the way-back read holds the
// tick and clears it (the pull never went out), then the nearer add becomes the pick and HoldForShot runs for
// it (on the bool latch the add pulled from its spot).
static void TestStandPullAuthAnchorReadThenOtherUnitHolds()
{
    DungeonPlan plan = StandPullPlanWithAdds();
    Run r = Start(&plan, 1);
    r.anchorKnown = true; // the way back not read yet: the anchor on the stand's floor, 10 yd off
    r.anchorX = -5832.0f;
    r.anchorY = -3807.0f;
    r.anchorZ = 200.7f;
    Facts read = StandPullFacts(28.0f); // the boss alone in shot: the gate judges him, the way-back read holds
    CHECK(Tick(r, read).empty());
    CHECK(Has(r.note, "checking the way back"));
    CHECK(r.standPullShotGuid == 0); // cleared on the hold: the pull never went out
    Facts next = StandPullFacts(28.0f);
    next.anchorPathRead = true;
    next.anchorReachable = true;
    next.units.push_back(StandPullAdd(10, 3979, 40.0f)); // too far to shoot: the stand rule holds for the nearer add
    next.units.push_back(StandPullAdd(11, 3980, 41.0f));
    CHECK(Tick(r, next).empty()); // not "pullback 10": the stand rule holds for the nearer add first
    CHECK(Has(r.note, "reading a firing stand for 3979"));
    CHECK(r.shotStandFor == 10);
}

// The same through the joiner rest: the boss in shot sets the authorization, the rest holds the tick and clears
// it, then the nearer add becomes the pick and HoldForShot runs for it.
static void TestStandPullAuthJoinerRestThenOtherUnitHolds()
{
    DungeonPlan plan = StandPullPlanWithAdds();
    Run r = Start(&plan, 1);
    r.anchorKnown = true; // the way back known: the anchor is the stand itself
    r.anchorX = -5842.0f;
    r.anchorY = -3807.0f;
    r.anchorZ = 200.7f;
    Facts loud = StandPullFacts(28.0f); // the boss alone in shot: the gate judges him, the rest holds the tick
    loud.joinerCeiling = 6;
    loud.units[0].joiners = 7;
    loud.units[0].joinersRead = true;
    loud.minHealthPct = 85;
    CHECK(Tick(r, loud).empty());
    CHECK(r.note == "7 joiners expected");
    CHECK(r.standPullShotGuid == 0); // cleared on the hold: the pull never went out
    Facts next = StandPullFacts(28.0f);
    next.joinerCeiling = 6;
    next.minHealthPct = 100; // rested: the rest wait is moot, the nearer add is judged for its own pull
    next.units.push_back(StandPullAdd(10, 3979, 40.0f)); // too far to shoot: the stand rule holds for the nearer add
    next.units.push_back(StandPullAdd(11, 3980, 41.0f));
    CHECK(Tick(r, next).empty()); // not "pullback 10": the stand rule holds for the nearer add first
    CHECK(Has(r.note, "reading a firing stand for 3979"));
    CHECK(r.shotStandFor == 10);
}
// The final line must not hide a skipped boss: the flagged boss's step ends
// "not cleared", the run finishes the rest of the plan, and the done note
// names him. (Stormwrought Ruins leg 9: Eldermaw swims out of reach; the run
// goes on to legs 10-11 while he stays alive.)
static void TestSkippedBossNamedInDoneNoteAliveElsewhere()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 3976;
    ai::NormalizePlanStep(boss);
    boss.flags |= ai::kPlanFlagSkipUnreachable;
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadZ = 10.0f;
    for (std::uint32_t i = 0; i < kBossSearchMs / kTickMs + 2 && !r.bossSearchWanted && r.state == State::Running; ++i)
        Tick(r, f);
    Facts alive = f;
    alive.bossSearchRead = true;
    alive.bossSearchAlive = true;
    alive.bossAliveX = 30.0f;
    alive.bossAliveZ = 43.0f;
    Tick(r, alive);
    CHECK(r.state == State::Running && r.step == 1 && r.notCleared.count(0));
    Facts g = Party(60);
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Done);
    CHECK(r.note == "plan done (boss skipped, not seen killed: boss 3976 (step 1))");
    Run w = r;
    w.wipes = 1;
    CHECK(DoneNote(w) == "plan done (1 wipe; boss skipped, not seen killed: boss 3976 (step 1))");
    // Both features in one note (merge of test-speed and skip-unreachable): speed label first, skipped bosses last.
    w.testSpeed = 2.0f;
    CHECK(DoneNote(w) == "plan done (speed 2.0 (test); 1 wipe; boss skipped, not seen killed: boss 3976 (step 1))");
    Run sp = r;
    sp.testSpeed = 2.0f;
    CHECK(DoneNote(sp) == "plan done (speed 2.0 (test); boss skipped, not seen killed: boss 3976 (step 1))");
}

static void TestSkippedBossNamedInDoneNotePassedBySpot()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 0, 0, 0);
    boss.entry = 3976;
    ai::NormalizePlanStep(boss);
    boss.flags |= ai::kPlanFlagSkipUnreachable;
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    UnitFact seen = Unit(7, 3976, 33.1f);
    seen.hostile = true;
    f.seen = true;
    f.units = {seen};
    r.passed.insert(7);
    r.pullTries[7] = 3;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.notCleared.count(0));
    Facts g = Party(60);
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Done);
    CHECK(r.note == "plan done (boss skipped, not seen killed: boss 3976 (step 1))");
}

static void TestDoneNoteUnchangedWithNoSkippedBoss()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(50);
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1);
    Facts g = Party(60);
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Done && r.note == "plan done");
    CHECK(!r.notCleared.count(0) && !r.notCleared.count(1));
}

// Scarlet Cathedral r23 slot 5, step 97 (fix 6): the party killed Mograine at the door, 290 yd from his spot, and the
// boss step paused "not found" three times until the corpse decay moved him home 600 s later. After kBossSearchMs at
// his spot the step asks the glue for one instance-wide search (next tick): dead anywhere is boss down with a note
// and his corpse remembered; alive elsewhere pauses with his position and sends no move; nothing found pauses as before.
static void TestBossNotAtHisSpotIsSearchedForInTheInstance()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 3976;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    auto toWindow = [](Run& r, const Facts& f)
    {
        for (std::uint32_t i = 0; i < kBossSearchMs / kTickMs + 2 && !r.bossSearchWanted && r.state == State::Running; ++i)
            CHECK(Tick(r, f).empty());
        CHECK(r.state == State::Running && r.bossSearchWanted && r.step == 0);
        CHECK(r.note == "boss 3976 not at his spot for 60 s: searching the instance for him");
    };
    // Dead at the door: boss down, with where, and the corpse a later loot step goes to.
    Run d = Start(&plan);
    Facts f = Party(0);
    toWindow(d, f);
    Facts dead = f;
    dead.bossSearchRead = true;
    dead.bossSearchDead = true;
    dead.bossSearchCorpseGuid = 0xF130000F88009C5Dull;
    dead.bossSearchCorpseX = 330.0f;
    Tick(d, dead);
    CHECK(d.state == State::Running && d.step == 1);
    CHECK(d.note == "boss down: his corpse lies at 330.0 0.0 0.0, 300.0 yd from his spot (an instance search after 60 s)");
    CHECK(d.corpses.count(3976) && d.corpses[3976].guid == 0xF130000F88009C5Dull && d.corpses[3976].x == 330.0f);
    // Alive elsewhere: a pause with his position, no order; alive wins over a corpse of another spawn.
    Run a = Start(&plan);
    toWindow(a, f);
    Facts alive = dead;
    alive.bossSearchAlive = true;
    alive.bossAliveX = 130.0f;
    alive.bossAliveY = 5.0f;
    CHECK(Tick(a, alive).empty());
    CHECK(a.state == State::Paused && a.step == 0 && !a.bossSearchWanted);
    CHECK(a.note.find("boss 3976 is alive at 130.0 5.0 0.0, 100.1 yd from his spot (an instance search after 60 s)") == 0);
    // Resume: a fresh window, and one search again at its end.
    LeadSpot spot;
    std::string refusal;
    CHECK(ResumeChecked(a, spot, refusal) && refusal.empty() && !a.bossSearchWanted && a.bossWaitMs == 0);
    // Read, nothing anywhere: the old pause, saying the instance was searched.
    Run n = Start(&plan);
    toWindow(n, f);
    Facts nothing = f;
    nothing.bossSearchRead = true;
    Tick(n, nothing);
    CHECK(n.state == State::Paused && n.note.find("boss 3976 not found") == 0 &&
        n.note.find("for 60 s nor anywhere in the instance (.bot run skip if he is already dead)") != std::string::npos);
    // Read with a spawn in a grid that is not loaded: the note says only the loaded parts were searched.
    Run un = Start(&plan);
    toWindow(un, f);
    Facts unloaded = nothing;
    unloaded.bossSearchUnloaded = true;
    Tick(un, unloaded);
    CHECK(un.state == State::Paused && un.note.find("boss 3976 not found") == 0 &&
        un.note.find("for 60 s nor in the loaded parts of the instance (an unloaded area was not searched) (.bot run "
            "skip if he is already dead)") != std::string::npos && un.note.find("nor anywhere") == std::string::npos);
    // His own spawn lies dead while a second spawn row of the entry lives elsewhere: boss down, not a pause.
    Run ow = Start(&plan);
    toWindow(ow, f);
    Facts ownDead = alive;
    ownDead.bossSearchOwnDead = true;
    Tick(ow, ownDead);
    CHECK(ow.state == State::Running && ow.step == 1 && ow.note.find("boss down: his corpse lies at 330.0") == 0);
    // No spawn row on the map (a script summons him): the note says the instance was not searched.
    Run ns = Start(&plan);
    toWindow(ns, f);
    Facts noSpawn = nothing;
    noSpawn.bossSearchNoSpawn = true;
    Tick(ns, noSpawn);
    CHECK(ns.state == State::Paused && ns.note.find("for 60 s; the instance was not searched: he has no spawn row on this "
        "map (a script summons him) (.bot run skip if he is already dead)") != std::string::npos);
    // Dead in a loaded part with another spawn unloaded: still boss down.
    Run du = Start(&plan);
    toWindow(du, f);
    Facts deadUnloaded = dead;
    deadUnloaded.bossSearchUnloaded = true;
    Tick(du, deadUnloaded);
    CHECK(du.state == State::Running && du.step == 1 && du.note.find("boss down: his corpse lies at 330.0") == 0);
    // Not read (no glue answer): the old pause and note.
    Run o = Start(&plan);
    toWindow(o, f);
    Tick(o, f);
    CHECK(o.state == State::Paused && o.note.find("boss 3976 not found") == 0 &&
        o.note.find("nor anywhere") == std::string::npos);
    // Scarlet Graveyard r23 slot 7: every spawn of his belongs to a game event that is not active (Scorn, event 81
    // "Scourge Invasion"): the step is done with a note, no pause.
    Run ev = Start(&plan);
    toWindow(ev, f);
    Facts eventOff = nothing;
    eventOff.bossSearchEventOff = true;
    eventOff.bossSearchEventId = 81;
    eventOff.bossSearchEventName = "Scourge Invasion";
    CHECK(Tick(ev, eventOff).empty());
    CHECK(ev.state == State::Running && ev.step == 1);
    CHECK(ev.note == "boss skipped: spawn is in inactive game event 81 (Scourge Invasion)");
    CHECK(BossEventOffNote(81, "") == "boss skipped: spawn is in inactive game event 81");
    // The same facts without the search read (no glue answer): the old pause.
    Run evNotRead = Start(&plan);
    toWindow(evNotRead, f);
    Facts eventOffNotRead = eventOff;
    eventOffNotRead.bossSearchRead = false;
    Tick(evNotRead, eventOffNotRead);
    CHECK(evNotRead.state == State::Paused && evNotRead.note.find("boss 3976 not found") == 0);
    // An event spawn that is alive or dead after all (the event runs after all): the old outcomes win.
    Run evAlive = Start(&plan);
    toWindow(evAlive, f);
    Facts eventAlive = alive;
    eventAlive.bossSearchEventOff = true;
    Tick(evAlive, eventAlive);
    CHECK(evAlive.state == State::Paused && evAlive.note.find("boss 3976 is alive at") == 0);
    Run evDead = Start(&plan);
    toWindow(evDead, f);
    Facts eventDead = dead;
    eventDead.bossSearchEventOff = true;
    Tick(evDead, eventDead);
    CHECK(evDead.state == State::Running && evDead.step == 1 && evDead.note.find("boss down") == 0);
    // He walks in while the search is asked: pulled, the window starts over.
    Run w = Start(&plan);
    toWindow(w, f);
    Facts in = f;
    in.units = {Unit(5, 3976, 40)};
    CHECK(Tick(w, in) == "pullback 5");
    CHECK(w.bossWaitMs == 0);
}

// Scarlet Cathedral r23 slot 5, step 98 (fix 7): Whitemane sat at 1 hp of 5284 under her script's invincibility (her
// revive of a dead Mograine never came), and the fight ceiling's pause was resumed four times into the same fight. A
// unit at exactly 1 hp (or 1 % or less, unchanged) that a party member has as its victim for kHpHeldMs pauses with
// its own note. Only that band: a healed, shielded or full-health target never pauses (a victim is no proof of
// damage landing, Codex review round 1). The resume is allowed.
static void TestUnitThatCannotDieNowPauses()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 0);
    boss.entry = 3977;
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    auto whitemane = [](std::uint32_t hp)
    {
        UnitFact u = Engaged(Unit(0xF130000F89009C0Aull, 3977, 3));
        u.health = hp;
        u.maxHealth = 5284;
        u.partyHits = true;
        return u;
    };
    Facts f = Party(0);
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.engagedRead = true;
    f.engagedUnits = {whitemane(1)};
    f.units = {Unit(0xF130000F89009C0Aull, 3977, 3)};
    std::uint32_t const heldTicks = kHpHeldMs / kTickMs;
    auto held = [&](const Facts& g, Run& r)
    {
        r = Start(&plan);
        r.fighting = true;
        for (std::uint32_t i = 0; i <= heldTicks + 5 && r.state == State::Running; ++i)
            Tick(r, g);
        return r.state == State::Paused && r.note.find("stays at") != std::string::npos;
    };
    Run r;
    CHECK(held(f, r) && r.step == 0);
    CHECK(r.note.find("entry 3977 stays at 1 hp: it cannot die now (an event or script holds it); unit "
        "0xF130000F89009C0A: .bot run skip goes on without this step, .bot run stop ends the run") == 0);
    // Not one tick early.
    Run e = Start(&plan);
    e.fighting = true;
    for (std::uint32_t i = 0; i < heldTicks; ++i)
        Tick(e, f);
    CHECK(e.state == State::Running);
    // The resume goes through (the stop rule bounds the repeats).
    LeadSpot spot;
    std::string refusal;
    CHECK(ResumeChecked(r, spot, refusal) && refusal.empty() && r.state == State::Running);
    // 1 % or less of its most, unchanged: held too, with its hp in the note.
    Facts low = f;
    low.engagedUnits = {whitemane(52)};
    Run l;
    CHECK(held(low, l) && l.note.find("entry 3977 stays at 52 hp (1 % or less): it cannot die now") == 0);
    // Never held: full health (no loss at all), 2 % hp unchanged (a shield), healed up and down at low hp, a target
    // whose hp changes every tick at 1 %, not a victim of the party, unattackable (Mograine's fake death), evading,
    // hp not read.
    for (int k = 0; k < 9; ++k)
    {
        Run u = Start(&plan);
        u.fighting = true;
        for (std::uint32_t i = 0; i <= heldTicks + 5; ++i)
        {
            Facts g = f;
            UnitFact w = whitemane(1);
            if (k == 0)
                w.health = 5284;
            if (k == 1)
                w.health = 106;
            if (k == 2)
                w.health = i % 20 < 10 ? 1 : 40; // a heal now and then
            if (k == 3)
                w.health = 30 + i % 3;
            if (k == 4)
                w.partyHits = false;
            if (k == 5)
                w.attackable = false;
            if (k == 6)
                w.evading = true;
            if (k == 7)
                w.health = 0;
            if (k == 8)
            {
                // 300 damage and a 400 heal in turn every 10 s from 350: 350, 50, 450, 150, ... (50 is 1 % or less
                // for 10 s only).
                std::uint32_t const step = i / 10;
                w.health = 350 + 100 * (step / 2) - (step % 2 ? 300 : 0);
            }
            g.engagedUnits = {w, Engaged(Unit(77, 4300, 5))};
            Tick(u, g);
        }
        CHECK(u.state == State::Running);
    }
    // The 60 s are continuous targeting (Codex review round 2): 30 s targeted, a 10 s gap with nobody on it, 30 s
    // targeted again is no pause; 60 s targeted after the gap is.
    Run b = Start(&plan);
    b.fighting = true;
    Facts off = f;
    off.engagedUnits = {whitemane(1)};
    off.engagedUnits[0].partyHits = false;
    for (std::uint32_t i = 0; i < heldTicks / 2; ++i)
        Tick(b, f);
    for (int i = 0; i < 10; ++i)
        Tick(b, off);
    CHECK(b.state == State::Running);
    for (std::uint32_t i = 0; i <= heldTicks / 2 + 1; ++i)
        Tick(b, f);
    CHECK(b.state == State::Running);
    for (std::uint32_t i = 0; i <= heldTicks && b.state == State::Running; ++i)
        Tick(b, f);
    CHECK(b.state == State::Paused && b.note.find("entry 3977 stays at 1 hp") == 0);
    // Out of combat between two stretches: the watch starts over.
    Run c = Start(&plan);
    c.fighting = true;
    for (std::uint32_t i = 0; i < heldTicks - 5; ++i)
        Tick(c, f);
    CHECK(!c.hpWatch.empty());
    Facts quiet = Party(0);
    quiet.units = f.units;
    Tick(c, quiet);
    CHECK(c.hpWatch.empty());
    for (std::uint32_t i = 0; i < heldTicks - 5; ++i)
        Tick(c, f);
    CHECK(c.state == State::Running);
    // A wipe (kWipeDead or more dead) with a lone survivor still on the 1-hp boss: no 1-hp pause; the regroup
    // follows once the fight is over (Opus cold review of bfd5aa1e).
    Run w = Start(&plan);
    w.fighting = true;
    Facts wiped = f;
    wiped.alive = 1;
    wiped.dead = 4;
    wiped.leadAlive = false;
    wiped.deadNames = {"Tanky", "A", "B", "C"};
    for (std::uint32_t i = 0; i <= heldTicks + 5; ++i)
    {
        Tick(w, wiped);
        CHECK(w.state == State::Running && w.note.find("stays at") == std::string::npos);
    }
    CHECK(w.hpWatch.empty());
    Facts over = wiped;
    over.anyInCombat = false;
    over.leadInCombat = false;
    over.engagedUnits.clear();
    Tick(w, over);
    CHECK(w.state == State::Running && w.regroup && w.note.find("regrouping by itself") != std::string::npos);
    // A charmed party bot as the only thing fighting the party: the note names it, not the stale wording.
    Facts charmed = f;
    charmed.engagedUnits.clear();
    charmed.charmedFighting = {"Hele"};
    CHECK(EngagedUnitsNote(charmed) == "nothing hostile engaged but a charmed party member fighting the party (Hele)");
    charmed.charmedFighting.push_back("Garet");
    CHECK(EngagedUnitsNote(charmed) == "nothing hostile engaged but a charmed party member fighting the party (Hele, Garet)");
    charmed.engagedUnits = f.engagedUnits; // with a real unit engaged the units are named as before
    CHECK(EngagedUnitsNote(charmed).find("entry 3977") == 0);
}

// Dragonmaw Retreat r25 slot 7 (2026-10-23): step 260 paused four times on "a fight lasted more than 180 s (kept on
// by entry 62072 ... attackable)" while the boss Searistrasz (62072) lost hp steadily from 100 % to 7 % over about
// 20 min with the party fine; he died on his own after the run was stopped. The ceiling now pauses only when no
// engaged unit lost kFightProgressPermille in the last kFightProgressWindowMs (FightProgressTick), with a hard cap
// (kFightHardCapMs), and a plan boss engaged on any step gets the boss ceiling.
static UnitFact HpUnit(std::uint64_t guid, std::uint32_t entry, std::uint32_t hp, std::uint32_t maxHp)
{
    UnitFact u = Engaged(Unit(guid, entry, 5));
    u.health = hp;
    u.maxHealth = maxHp;
    u.partyHits = true;
    return u;
}

static Facts FightFacts(std::vector<UnitFact> units)
{
    Facts f = Party(0);
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.engagedRead = true;
    f.engagedUnits = std::move(units);
    return f;
}

static void TestSlowSteadyBossFightNeverPausesBeforeTheHardCap()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 80);
    boss.entry = 62072;
    plan.steps = {PullStep(10, {1}), boss};
    Run r = Start(&plan);
    std::uint32_t const maxHp = 1000000;
    // One percentage point every 20 s (3 a minute; r25 lost about 4.6 a minute).
    auto hpAt = [&](std::uint32_t s) { return maxHp - (s / 20) * (maxHp / 100); };
    std::uint32_t s = 0;
    bool sawHeldNote = false;
    for (; s < kFightHardCapMs / kTickMs && r.state == State::Running; ++s)
    {
        Tick(r, FightFacts({HpUnit(0xF13000F27800A001ull, 62072, hpAt(s), maxHp)}));
        if (r.note.find("past the 420 s ceiling: an engaged unit lost hp in the last 60 s") != std::string::npos)
            sawHeldNote = true;
    }
    CHECK(r.state == State::Running && r.step == 0 && r.fightMs == kFightHardCapMs);
    CHECK(sawHeldNote);
    // The hard cap pauses anyway, naming the units and their hp.
    for (int i = 0; i < 3 && r.state == State::Running; ++i, ++s)
        Tick(r, FightFacts({HpUnit(0xF13000F27800A001ull, 62072, hpAt(s), maxHp)}));
    CHECK(r.state == State::Paused && r.ceilingPaused);
    CHECK(r.note == "a fight lasted more than 1800 s, the hard cap, though an engaged unit still lost hp (kept on by "
        "entry 62072 (0xF13000F27800A001) attackable at 10 %)");
    // The same on the boss's own step.
    Run b = Start(&plan);
    b.step = 1;
    std::uint32_t t = 0;
    for (; t < kFightHardCapMs / kTickMs && b.state == State::Running; ++t)
        Tick(b, FightFacts({HpUnit(0xF13000F27800A001ull, 62072, hpAt(t), maxHp)}));
    CHECK(b.state == State::Running && b.step == 1);
    for (int i = 0; i < 3 && b.state == State::Running; ++i, ++t)
        Tick(b, FightFacts({HpUnit(0xF13000F27800A001ull, 62072, hpAt(t), maxHp)}));
    CHECK(b.state == State::Paused && b.note.find("a fight lasted more than 1800 s, the hard cap") == 0);
}

static void TestFightWithNoHpLossPastTheCeilingPauses()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    std::uint32_t const maxHp = 3000;
    // Down to 50 % in the first 100 s, then flat: paused on the first tick past 180 s with the reason.
    auto flatFrom = [&](std::uint32_t s, std::uint32_t until) { return s < until ? maxHp - s * 15 : maxHp - until * 15; };
    Run r = Start(&plan);
    std::uint32_t s = 0;
    for (; s < kFightCapMs / kTickMs && r.state == State::Running; ++s)
        Tick(r, FightFacts({HpUnit(0xF130001836000001ull, 4300, flatFrom(s, 100), maxHp)}));
    CHECK(r.state == State::Running && r.fightMs == kFightCapMs);
    Tick(r, FightFacts({HpUnit(0xF130001836000001ull, 4300, flatFrom(s, 100), maxHp)}));
    CHECK(r.state == State::Paused && r.ceilingPaused);
    CHECK(r.note == "a fight lasted more than 180 s and no engaged unit lost hp in 60 s (kept on by entry 4300 "
        "(0xF130001836000001) attackable at 50 %)");
    // Losing hp past the ceiling until 300 s, then flat: held while it lost, paused once the last 60 s show no loss.
    Run p = Start(&plan);
    std::uint32_t at = 0;
    for (std::uint32_t i = 0; i < 600 && p.state == State::Running; ++i)
    {
        Tick(p, FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp - std::min<std::uint32_t>(i, 300) * 5, maxHp)}));
        at = i;
    }
    CHECK(p.state == State::Paused && p.note.find("no engaged unit lost hp in 60 s") != std::string::npos);
    // The last new low came at tick 300: held while it is within the window, paused on the first tick after.
    CHECK(at == 361);
    // Too slow is a stall: 1 point every 200 s sets its first new low (2 points) at 400 s, after the ceiling.
    Run slow = Start(&plan);
    std::uint32_t sl = 0;
    for (; sl < 600 && slow.state == State::Running; ++sl)
        Tick(slow, FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp - (sl / 200) * 30, maxHp)}));
    CHECK(slow.state == State::Paused && sl == kFightCapMs / kTickMs + 1);
    // Exactly 2 points in the window is progress: 1 point every 30 s.
    Run two = Start(&plan);
    for (std::uint32_t i = 0; i < 600 && two.state == State::Running; ++i)
        Tick(two, FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp - (i / 30) * 30, maxHp)}));
    CHECK(two.state == State::Running);
    // A party member dying in the last 60 s pauses at the ceiling though the unit loses hp.
    Run d = Start(&plan);
    std::uint32_t ds = 0;
    for (; ds < 400 && d.state == State::Running; ++ds)
    {
        Facts g = FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp - ds * 5, maxHp)});
        if (ds >= 170)
        {
            g.dead = 1;
            g.alive = 4;
            g.deadNames = {"Healy"};
        }
        Tick(d, g);
    }
    CHECK(d.state == State::Paused && ds == kFightCapMs / kTickMs + 1);
    CHECK(d.note.find("a fight lasted more than 180 s and a party member died in the last 60 s") == 0);
    // ... but one dead before the fight began is no loss in it (the watch's first tick takes the count as it is).
    Run first = Start(&plan);
    Facts g1 = FightFacts({HpUnit(0xF130001836000001ull, 4300, 100, maxHp)});
    g1.dead = 1;
    CHECK(FightProgressTick(first, g1, kTickMs) == FightProgress::Young && !first.fightHpDeath);
    g1.dead = 2;
    CHECK(FightProgressTick(first, g1, kTickMs) == FightProgress::Dying && first.fightHpDeath);
    Run e = Start(&plan);
    for (std::uint32_t i = 0; i < 400 && e.state == State::Running; ++i)
    {
        Facts g = FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp - i * 5, maxHp)});
        g.dead = 1;
        g.alive = 4;
        g.deadNames = {"Healy"};
        Tick(e, g);
    }
    CHECK(e.state == State::Running);
    // A wipe (kWipeDead or more dead) regroups at the ceiling as before, whatever the hp do.
    Run w = Start(&plan);
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && !w.regroup; ++i)
    {
        Facts g = FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp - i * 5, maxHp)});
        g.dead = 4;
        g.alive = 1;
        Tick(w, g);
    }
    CHECK(w.regroup && w.note.find("went past 180 s") != std::string::npos);
    // Hp not read (0): the old rule and the old note at once.
    Run u = Start(&plan);
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && u.state == State::Running; ++i)
        Tick(u, FightFacts({HpUnit(0xF130001836000001ull, 4300, 0, 0)}));
    CHECK(u.state == State::Paused && u.fightMs == kFightCapMs + kTickMs &&
        u.note == "a fight lasted more than 180 s (kept on by entry 4300 (0xF130001836000001) attackable)");
    // Two adds healed in turn (Opus cold review of fa482263): each swings 60 -> 50 -> 60 % over 40 s, 20 s out of
    // phase, so one of them always stands 2 points or more under its 60 s high. No net loss: after the first trough
    // neither sets a new low, and the ceiling pauses as a stall once the window has passed it.
    auto swing = [&](std::uint32_t t) -> std::uint32_t
    {
        std::uint32_t const ph = t % 40;
        return maxHp * 60 / 100 - (ph < 20 ? ph : 40 - ph) * (maxHp / 100) / 2; // 60 % down to 50 % and back
    };
    Run sw = Start(&plan);
    std::uint32_t swt = 0;
    for (; swt < 1000 && sw.state == State::Running; ++swt)
        Tick(sw, FightFacts({HpUnit(0xF130001836000001ull, 4300, swing(swt), maxHp),
            HpUnit(0xF130001836000002ull, 4300, swing(swt + 20), maxHp)}));
    CHECK(sw.state == State::Paused && swt == kFightCapMs / kTickMs + 1 &&
        sw.note.find("a fight lasted more than 180 s and no engaged unit lost hp in 60 s") == 0);
    // A unit the group cannot attack, not a party member's victim, losing hp holds nothing (an attackable unit
    // stays in the fight at full hp so the fight is not let go as an unattackable one).
    Run ua = Start(&plan);
    std::uint32_t uat = 0;
    for (; uat < 1000 && ua.state == State::Running; ++uat)
    {
        UnitFact bomb = HpUnit(0xF130001ED90075B6ull, 7897, maxHp - uat * 5, maxHp);
        bomb.attackable = false;
        bomb.partyHits = false;
        Tick(ua, FightFacts({bomb, HpUnit(0xF130001836000001ull, 4300, maxHp, maxHp)}));
    }
    CHECK(ua.state == State::Paused && uat == kFightCapMs / kTickMs + 1 &&
        ua.note.find("no engaged unit lost hp in 60 s") != std::string::npos);
    // Loss taken while it could not be hit never counts later (Codex review of b466e9ad): the unit falls to 50 % in
    // 130-170 s unattackable and nobody's victim, then turns attackable at that same hp from 170 s on (its last low
    // within 60 s of the ceiling): paused on the first tick past the ceiling.
    Run late = Start(&plan);
    std::uint32_t lt = 0;
    for (; lt < 1000 && late.state == State::Running; ++lt)
    {
        UnitFact unit = HpUnit(0xF130001ED90075B6ull, 7897, lt < 130 ? maxHp : lt < 170 ? maxHp - (lt - 130) * 38 : maxHp / 2, maxHp);
        unit.attackable = lt >= 170;
        unit.partyHits = lt >= 170;
        Tick(late, FightFacts({unit, HpUnit(0xF130001836000001ull, 4300, maxHp, maxHp)}));
    }
    CHECK(late.state == State::Paused && lt == kFightCapMs / kTickMs + 1 &&
        late.note.find("no engaged unit lost hp in 60 s") != std::string::npos);
    // ... and its mark moved down meanwhile (to about 50 %), so only a later fall under that is a new low.
    CHECK(late.fightHp.at(0xF130001ED90075B6ull).low <= 520 && !late.fightHp.at(0xF130001ED90075B6ull).fell);
    // ... while the same unit as a party member's victim does hold it.
    Run uh = Start(&plan);
    for (std::uint32_t i = 0; i < 400 && uh.state == State::Running; ++i)
    {
        UnitFact bomb = HpUnit(0xF130001ED90075B6ull, 7897, maxHp - i * 5, maxHp);
        bomb.attackable = false;
        Tick(uh, FightFacts({bomb, HpUnit(0xF130001836000001ull, 4300, maxHp, maxHp)}));
    }
    CHECK(uh.state == State::Running);
    // A hittable unit leaving the read counts as its death once (the glue lists living units only); back in the read
    // it keeps its low mark, so its swing sets no new low after (Opus re-review of b466e9ad). The swinging add is
    // missing on tick 150 only: held until 60 s after that tick, then paused.
    Run fl = Start(&plan);
    std::uint32_t flt = 0;
    for (; flt < 1000 && fl.state == State::Running; ++flt)
    {
        Facts g = FightFacts({HpUnit(0xF130001836000001ull, 4300, swing(flt), maxHp),
            HpUnit(0xF130001836000002ull, 4300, maxHp, maxHp)});
        if (flt == 150)
            g.engagedUnits.erase(g.engagedUnits.begin());
        Tick(fl, g);
    }
    CHECK(fl.state == State::Paused && flt == 212 && fl.note.find("no engaged unit lost hp in 60 s") != std::string::npos);
    // ... a unit the party could not hit on its last read leaving it is no death of the fight's.
    Run gone = Start(&plan);
    std::uint32_t gt = 0;
    for (; gt < 1000 && gone.state == State::Running; ++gt)
    {
        UnitFact bomb = HpUnit(0xF130001ED90075B6ull, 7897, maxHp, maxHp);
        bomb.attackable = false;
        bomb.partyHits = false;
        Facts g = FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp, maxHp)});
        if (gt < 170)
            g.engagedUnits.push_back(bomb);
        Tick(gone, g);
    }
    CHECK(gone.state == State::Paused && gt == kFightCapMs / kTickMs + 1);
    // Three adds killed one after the other past 180 s beside a boss at flat hp (the glue drops a dead unit from
    // the read; the second add's hp reads 0 on its last tick): each death is a new low, so the ceiling holds until
    // 60 s after the last death, then pauses.
    Run kills = Start(&plan);
    std::uint32_t kt = 0;
    for (; kt < 1000 && kills.state == State::Running; ++kt)
    {
        Facts g = FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp / 2, maxHp)});
        // Add k is fought from 150 + 30 k s at full hp and dies (leaves the read) at 175 + 30 k s; no 2-point low
        // while it lives (it stays at full hp until it dies), so only its death counts.
        for (std::uint32_t k = 0; k < 3; ++k)
        {
            std::uint32_t const from = 150 + 30 * k;
            if (kt >= from && kt < from + 25)
                g.engagedUnits.push_back(HpUnit(0xF13000183600A000ull + k, 4301, maxHp, maxHp));
            if (k == 1 && kt == from + 25)
                g.engagedUnits.push_back(HpUnit(0xF13000183600A000ull + k, 4301, 0, maxHp)); // read dead
        }
        Tick(kills, g);
        if (kt == 181)
            CHECK(kills.state == State::Running);
    }
    // A unit read at 0 hp is a death even from under 2 %: 1 % (10 permille, no 2-point fall left) to 0 is a new low.
    Run z = Start(&plan);
    Facts z1 = FightFacts({HpUnit(0xF130001836000001ull, 4300, 30, maxHp)});
    CHECK(FightProgressTick(z, z1, kTickMs) == FightProgress::Young);
    z1.engagedUnits[0].health = 0;
    CHECK(FightProgressTick(z, z1, kTickMs) == FightProgress::Losing && z.fightHp.begin()->second.at == 2 * kTickMs);
    CHECK(FightProgressTick(z, z1, kTickMs) == FightProgress::Losing && z.fightHp.begin()->second.at == 2 * kTickMs);
    // The last add left the read on tick 235: held through tick 295, paused on tick 296.
    CHECK(kills.state == State::Paused && kt == 297 && kills.note.find("no engaged unit lost hp in 60 s") != std::string::npos);
    // ResetStep forgets the watch (a step change, a resume).
    Run rs = Start(&plan);
    for (int i = 0; i < 5; ++i)
        Tick(rs, FightFacts({HpUnit(0xF130001836000001ull, 4300, maxHp - i * 100, maxHp)}));
    rs.fightHpDeath = true;
    rs.fightHpDead = 2;
    CHECK(!rs.fightHp.empty() && rs.fightHpMs == 5 * kTickMs);
    ResetStep(rs);
    CHECK(rs.fightHp.empty() && rs.fightHpMs == 0 && rs.fightHpDead == 0 && !rs.fightHpDeath && rs.fightHpDeathAt == 0);
    // Out of combat the watch is forgotten (a far move, so the step does not end with the fight).
    DungeonPlan far;
    far.steps = {Step(PlanStepKind::Move, 500)};
    Run c = Start(&far);
    Tick(c, FightFacts({HpUnit(0xF130001836000001ull, 4300, 100, maxHp)}));
    CHECK(c.fightHp.size() == 1 && c.fightHpMs == kTickMs);
    Facts quiet = Party(0);
    Tick(c, quiet);
    CHECK(c.fightHp.empty() && c.fightHpMs == 0);
}

static void TestPlanBossOnAMoveStepGetsTheBossCeiling()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 80);
    boss.entry = 62072;
    PlanStep hold = Step(PlanStepKind::Hold, 90);
    hold.entries = {9999};
    plan.steps = {Step(PlanStepKind::Move, 60), boss, hold};
    auto pausedAt = [&](std::uint32_t entry)
    {
        Run r = Start(&plan);
        std::uint32_t i = 0;
        for (; i < 1000 && r.state == State::Running; ++i)
            Tick(r, FightFacts({HpUnit(0xF13000F27800A001ull, entry, 0, 0)}));
        CHECK(r.state == State::Paused && r.step == 0);
        return std::make_pair(i, r.note);
    };
    auto const bossOne = pausedAt(62072);
    CHECK(bossOne.first == kBossFightCapMs / kTickMs + 1);
    CHECK(bossOne.second == "a fight lasted more than 420 s (kept on by entry 62072 (0xF13000F27800A001) attackable)");
    // Not a plan boss: the 180 s ceiling; a hold's unit is no boss step's entry.
    CHECK(pausedAt(4300).first == kFightCapMs / kTickMs + 1);
    CHECK(pausedAt(9999).first == kFightCapMs / kTickMs + 1);
    // The boss's entry counts only when read and attackable: unattackable (beside an attackable add, so the fight is
    // one), or facts without the engaged read, keep 180 s.
    {
        Run r = Start(&plan);
        std::uint32_t i = 0;
        for (; i < 1000 && r.state == State::Running; ++i)
        {
            UnitFact boss62072 = HpUnit(0xF13000F27800A001ull, 62072, 0, 0);
            boss62072.attackable = false;
            Tick(r, FightFacts({boss62072, HpUnit(0xF130001836000001ull, 4300, 0, 0)}));
        }
        CHECK(r.state == State::Paused && i == kFightCapMs / kTickMs + 1);
        Facts unread = FightFacts({HpUnit(0xF13000F27800A001ull, 62072, 0, 0)});
        unread.engagedRead = false;
        Run q = Start(&plan);
        CHECK(!EngagedPlanBoss(q, unread));
        unread.engagedRead = true;
        CHECK(EngagedPlanBoss(q, unread));
    }
    // An event's fight keeps its own ceiling: hp loss does not hold it.
    DungeonPlan holdPlan;
    PlanStep h = Step(PlanStepKind::Hold);
    h.radius = 30;
    holdPlan.steps = {h};
    Run ev = Start(&holdPlan);
    std::uint32_t i = 0;
    for (; i < 1000 && ev.state == State::Running; ++i)
        Tick(ev, FightFacts({HpUnit(0xF130001836000001ull, 4300, 1000000 - i * 500, 1000000)}));
    CHECK(ev.state == State::Paused && i == kBossFightCapMs / kTickMs + 1 &&
        ev.note.find("a fight lasted more than 420 s") == 0);
}

// Shadowfang Keep r26 slot 7 step 46 (tests/r26/slot7): after the wipe and a Sap that did not land, Thunderward
// went back and forth on the stair between (-263 2304 z 78) and (-290 2307 z 91), 35-38 yd from the lead bot at the
// top (z 96.8), on its own follow and formation station; a summon to the top was undone by its formation station,
// and the gather paused the run four times. The straggler ladder: out of the gather 45 s (out of combat), the run
// leader walks it to the lead by path; 45 s after that walk and still out, it is summoned to the lead; the gather's
// ceiling pauses only after both (and the summon's settle time).
static MemberFact StairBot(std::uint32_t tick)
{
    // Down the stair and up again: never still, never 5 yd closer for long (the trace's oscillation).
    MemberFact m = Mem("Thunderward", (tick % 2) ? 35.8f : 38.1f, (tick % 2) ? 14.3f : 6.0f);
    m.x = (tick % 2) ? -263.0f : -290.0f;
    return m;
}

static void TestStragglerLadderWalksThenSummonsBeforeThePause()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), StairBot(0)};
    f.maxMemberYd = 38.1f;
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather
    std::vector<std::pair<std::uint32_t, std::string>> orders;
    std::vector<std::string> notes;
    std::uint32_t tick = 1;
    for (; tick < 400 && r.state == State::Running; ++tick)
    {
        f.members[1] = StairBot(tick);
        std::string const got = Tick(r, f);
        if (!got.empty())
        {
            orders.emplace_back(tick, got);
            notes.push_back(r.note);
        }
    }
    CHECK(r.state == State::Paused && r.note.find("the party did not gather within 30 s") == 0);
    // The ceiling's own summon (30 s, "does not come closer") first, then the ladder's walk and summon.
    std::size_t walk = orders.size(), ladderSummon = orders.size();
    for (std::size_t i = 0; i < orders.size(); ++i)
    {
        if (orders[i].second == "walk Thunderward 0.0 0.0 0.0")
            walk = i;
        if (orders[i].second == "summon Thunderward" && walk < i)
            ladderSummon = i;
    }
    CHECK(walk < orders.size() && ladderSummon < orders.size());
    CHECK(orders[walk].first == kStragglerWalkMs / kTickMs);
    CHECK(orders[ladderSummon].first == orders[walk].first + kStragglerSummonMs / kTickMs);
    CHECK(notes[walk].find("Thunderward walked to the lead by path") != std::string::npos);
    CHECK(notes[ladderSummon].find("Thunderward summoned to the lead") != std::string::npos);
    // No pause before the summon had its settle time.
    CHECK(tick - 1 >= orders[ladderSummon].first + kStragglerSettleMs / kTickMs);
    // Bounded: one walk and one ladder summon, the pause within one more ceiling.
    std::size_t walks = 0;
    for (const auto& o : orders)
        walks += o.second.compare(0, 5, "walk ") == 0;
    CHECK(walks == 1);
    CHECK(tick - 1 <= orders[ladderSummon].first + kGatherCapMs / kTickMs + 2);
    // A gather that waits with the ladder pending says so.
    Run w = Start(&plan);
    Facts g = f;
    Tick(w, g);
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 1; ++i)
    {
        g.members[1] = StairBot(i);
        Tick(w, g);
    }
    CHECK(w.state == State::Running);
}

// The ladder's clock stands while the bot fights or has attackers, and while the party fights; a bot back in the
// gather starts over; no ladder order toward a dead lead bot, and a dead lead bot never holds a pause.
static void TestStragglerLadderClocks()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Straggly", 30.0f)};
    f.maxMemberYd = 30.0f;
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    for (std::uint32_t i = 0; i < 10; ++i)
        Tick(r, f);
    CHECK(r.straggle.count("Straggly") && r.straggle.at("Straggly").outMs == 10 * kTickMs);
    f.members[1].attackers = 1;
    Tick(r, f);
    f.members[1].attackers = 0;
    f.members[1].inCombat = true;
    Tick(r, f);
    CHECK(r.straggle.at("Straggly").outMs == 10 * kTickMs); // stood
    f.members[1].inCombat = false;
    // Back within the gather radius for a tick: off the ladder; out again, from 0.
    f.members[1].yd = 10.0f;
    Tick(r, f);
    CHECK(!r.straggle.count("Straggly"));
    // Up or down a floor counts as out of the gather (kStragglerDzYd) at any distance.
    f.members[1].dz = kStragglerDzYd + 1.0f;
    Tick(r, f);
    CHECK(r.straggle.count("Straggly") && r.straggle.at("Straggly").outMs == kTickMs);
    // A dead lead bot: no walk, no summon, and no hold of the pause.
    Run d = Start(&plan);
    d.phase = Phase::Gather;
    d.straggle["Straggly"].outMs = kStragglerWalkMs;
    Facts dead = f;
    dead.leadAlive = false;
    std::string order;
    CHECK(!StragglerLadderTick(d, dead, order) && order.empty());
    CHECK(!StragglerLadderPending(d, dead));
    // Not waiting for a gather (the party walks): the ladder is forgotten.
    Run m = Start(&plan);
    m.straggle["Straggly"].outMs = kStragglerWalkMs;
    m.phase = Phase::Walk;
    UpdateStraggle(m, f, kTickMs);
    CHECK(m.straggle.empty());
    // A bot in a fight is never walked or summoned by the ladder, and never holds the pause.
    Run c = Start(&plan);
    c.phase = Phase::Gather;
    c.straggle["Straggly"].outMs = kStragglerWalkMs;
    Facts fight = f;
    fight.members[1].inCombat = true;
    CHECK(!StragglerLadderTick(c, fight, order) && !StragglerLadderPending(c, fight));
    // The rogue on its Sap is no straggler (it walks to its mob on the run's order).
    Run s = Start(&plan);
    s.phase = Phase::Gather;
    s.ccHolds[2].bot = "Straggly";
    Run::CcWatch watch;
    watch.guid = 2;
    s.ccWatch.push_back(watch);
    UpdateStraggle(s, f, kTickMs);
    CHECK(s.straggle.empty());
}

// The ladder's walk refused (no navmesh path for the bot): its summon goes out on the next tick, not 45 s later;
// a refused walk never pauses.
static void TestStragglerWalkRefusedSummonsNext()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(4.0f);
    f.leadY = 2.0f;
    f.leadZ = 96.8f;
    f.members = {Mem("Tanky", 0), Mem("Straggly", 30.0f)};
    f.maxMemberYd = 30.0f;
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 4.0 2.0 96.8");
    std::string order;
    for (std::uint32_t i = 0; i < 100 && order.compare(0, 5, "walk ") != 0; ++i)
    {
        f.members[1].x += (i % 2 ? 3.0f : -3.0f);
        order = Tick(r, f);
    }
    CHECK(order == "walk Straggly 4.0 2.0 96.8"); // to the lead bot's own spot
    OrderRefused(r, order, "unreachable: Straggly has no navmesh path to the point (it ends 30 yd short)");
    CHECK(r.state == State::Running && r.note.find("summoned next") != std::string::npos);
    f.members[1].x += 3.0f;
    CHECK(Tick(r, f) == "summon Straggly");
    CHECK(r.straggle.at("Straggly").rung == 2);
    // Another order's refusal is not the ladder's.
    Run o = Start(&plan);
    CHECK(!StragglerWalkRefused(o, "summon Straggly", "busy"));
}

// The regroup's gather (after a wipe: r26 slot 7 19:18-19:22, "regroup: waiting for Thunderward (running back)")
// climbs the same ladder, its notes marked as the regroup's, and its ceiling pauses only after it.
static void TestStragglerLadderInTheRegroup()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), StairBot(0)};
    f.maxMemberYd = 38.1f;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the regroup's gather
    std::string orders;
    std::string walkNote;
    for (std::uint32_t i = 1; i < 400 && r.state == State::Running; ++i)
    {
        f.members[1] = StairBot(i);
        std::string const got = Tick(r, f);
        if (got.compare(0, 5, "walk ") == 0)
            walkNote = r.note;
        orders += got + (got.empty() ? "" : "|");
    }
    CHECK(r.state == State::Paused && r.note.find("regroup: the party did not gather within") == 0);
    CHECK(orders.find("walk Thunderward 0.0 0.0 0.0|") != std::string::npos);
    CHECK(orders.find("summon Thunderward|", orders.find("walk Thunderward")) != std::string::npos);
    CHECK(walkNote.find("regroup: gather: Thunderward") == 0 && walkNote.find("walked to the lead by path") != std::string::npos);
}

// Shadowfang Keep r26 slot 7 step 46, three times: "sap did not land on 3855 in 15 s: the rogue is taken off it",
// and the rogue left the party again. Now it is walked back to the lead bot's spot after the clear (and after the
// gather, whose follow would undo the walk), and stays there; never a dead rogue or one in a fight.
static void TestFailedSapWalksTheRogueBack()
{
    DungeonPlan plan = CcPlan({100, 101});
    Run r = Start(&plan);
    Facts f = Party(4.0f);
    f.leadY = 2.0f;
    f.leadZ = 96.8f;
    f.members = {Mem("Tanky", 0), CcMem("Thunderward", CcFamily::Sap)};
    f.units = {Typed(1, 1, 10, kCreatureHumanoid), Typed(2, 100, 20, kCreatureHumanoid),
        Typed(3, 101, 22, kCreatureHumanoid)};
    CHECK(Tick(r, f) == "cc moon 2 Thunderward");
    f.maxMemberYd = 36.0f;
    f.members[1].yd = 36.0f;
    f.members[1].dz = 14.0f;
    std::string order;
    for (std::uint32_t i = 0; i <= kSapWaitMs / kTickMs + 1 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "cc clear Thunderward moon 2" && r.sapWalkDue == "Thunderward");
    CHECK(Tick(r, f).compare(0, 5, "move ") == 0); // the gather
    CHECK(Tick(r, f) == "walk Thunderward 4.0 2.0 96.8");
    CHECK(r.note == "the Sap did not land: Thunderward walked to the lead by path, it stays with the party");
    CHECK(r.sapWalkDue.empty());
    // In a fight by then: no walk (the fight comes first).
    Run c = Start(&plan);
    Facts g = f;
    g.members[1].yd = 5.0f;
    g.members[1].dz = 0.0f;
    g.maxMemberYd = 5.0f;
    CHECK(Tick(c, g) == "cc moon 2 Thunderward");
    order.clear();
    for (std::uint32_t i = 0; i <= kSapWaitMs / kTickMs + 1 && order.empty(); ++i)
        order = Tick(c, g);
    CHECK(order == "cc clear Thunderward moon 2");
    g.members[1].attackers = 1;
    std::string const next = Tick(c, g);
    CHECK(next.compare(0, 5, "walk ") != 0 && c.sapWalkDue.empty());
    // Dead by then (not among the living members): no walk.
    Run d = Start(&plan);
    Facts h = g;
    h.members[1].attackers = 0;
    CHECK(Tick(d, h) == "cc moon 2 Thunderward");
    order.clear();
    for (std::uint32_t i = 0; i <= kSapWaitMs / kTickMs + 1 && order.empty(); ++i)
        order = Tick(d, h);
    h.members.pop_back();
    CHECK(Tick(d, h).compare(0, 5, "walk ") != 0 && d.sapWalkDue.empty());
    // A landed Sap: no walk back (the rogue stays by its sapped mob until the pull).
    Run l = Start(&plan);
    Facts k = g;
    k.members[1].attackers = 0;
    CHECK(Tick(l, k) == "cc moon 2 Thunderward");
    k.units[1].polymorphed = true;
    std::string all;
    for (int i = 0; i < 4; ++i)
        all += Tick(l, k) + "|";
    CHECK(all.find("walk ") == std::string::npos && l.sapWalkDue.empty());
    // The walk order the glue reads.
    CHECK(WalkOrder("Thunderward", -250.7f, 2332.5f, 96.8f) == "walk Thunderward -250.7 2332.5 96.8");
}

// Codex adversarial review of 95f66dd8 (FIX FIRST 1): the walk took any path that ended at the point. A path more
// than kDetourMaxPathYd longer than the straight line, one past a pack not cleared yet, or one within kHopPullYd of
// a hostile out of combat is refused (BotWalkRefusal), and the ladder's summon goes out on the next tick instead.
static void TestBotWalkRefusedOnADetourOrAPack()
{
    std::vector<PathPoint> straight = {{0, 0, 0}, {30, 0, 0}};
    std::unordered_set<std::size_t> none;
    CHECK(BotWalkRefusal(straight, 30.0f, 30.0f, nullptr, 0, 0, none, {}).empty());
    // The long way round: 30 yd straight, 71 yd along the path (the cap is +40).
    std::vector<PathPoint> around = {{0, 0, 0}, {0, 20, 0}, {30, 20, 0}, {30, 0, 0}};
    CHECK(BotWalkRefusal(around, 30.0f, 70.0f, nullptr, 0, 0, none, {}).empty());
    CHECK(BotWalkRefusal(around, 30.0f, 70.1f, nullptr, 0, 0, none, {}).compare(0, 7, "detour:") == 0);
    // Codex review of 6c8297ec: never a path longer than the glue's hostile scan reaches (kBotWalkMaxPathYd),
    // though within the detour cap (110 yd straight, 121 yd along it).
    std::vector<PathPoint> longWay = {{0, 0, 0}, {110, 0, 0}};
    CHECK(BotWalkRefusal(longWay, 110.0f, kBotWalkMaxPathYd, nullptr, 0, 0, none, {}).empty());
    CHECK(BotWalkRefusal(longWay, 110.0f, 121.0f, nullptr, 0, 0, none, {}).compare(0, 9, "too-long:") == 0);
    // Through a pack not cleared yet (the current step's or a later one), not past one cleared before.
    DungeonPlan plan;
    plan.steps = {PullStep(15, {1}), PullStep(500, {2})};
    plan.steps[0].y = 0;
    plan.steps[0].radius = 5;
    CHECK(BotWalkRefusal(straight, 30.0f, 30.0f, &plan, 0, 0, none, {}).compare(0, 5, "pack:") == 0);
    CHECK(BotWalkRefusal(straight, 30.0f, 30.0f, &plan, 1, 0, none, {}).empty());
    std::unordered_set<std::size_t> skipped = {0};
    CHECK(BotWalkRefusal(straight, 30.0f, 30.0f, &plan, 1, 0, skipped, {}).compare(0, 5, "pack:") == 0);
    // Past a hostile out of combat within kHopPullYd of the path.
    StandHostile mob;
    mob.x = 15;
    mob.y = kHopPullYd - 1.0f;
    mob.entry = 3855;
    CHECK(BotWalkRefusal(straight, 30.0f, 30.0f, nullptr, 0, 0, none, {mob}) ==
        "hostile: the path passes within 15.0 yd of 3855");
    mob.y = kHopPullYd + 1.0f;
    CHECK(BotWalkRefusal(straight, 30.0f, 30.0f, nullptr, 0, 0, none, {mob}).empty());
    // The ladder: its walk refused for a detour (the glue's answer) -> the summon on the next tick, no pause.
    DungeonPlan one;
    one.steps = {PullStep(100, {1})};
    Run r = Start(&one);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Straggly", 30.0f)};
    f.maxMemberYd = 30.0f;
    f.units = {Unit(7, 1, 50)};
    Tick(r, f);
    std::string order;
    for (std::uint32_t i = 0; i < 100 && order.compare(0, 5, "walk ") != 0; ++i)
    {
        f.members[1].x += (i % 2 ? 3.0f : -3.0f);
        order = Tick(r, f);
    }
    for (const char* why : {"unreachable: Straggly: detour: the path is 90.0 yd for 30.0 yd in a straight line",
             "unreachable: Straggly: too-long: the path is 121.0 yd, more than 120.0 yd",
             "unreachable: Straggly: pack: the path passes a pack not cleared yet"})
    {
        Run t = r;
        OrderRefused(t, order, why);
        CHECK(t.state == State::Running);
        Facts g = f;
        g.members[1].x += 3.0f;
        CHECK(Tick(t, g) == "summon Straggly");
    }
}

// FIX FIRST 1 (2): a walker kept on the run's stay only while it is still a commanded member of the lead bot's
// group, the lead bot (and the binding) there, on the lead bot's map; otherwise its stay is cleared before it
// leaves the list (WalkerVerdict: Release).
static void TestWalkerVerdict()
{
    // found, onStay, alive, inCombat, running, leadAvailable, inLeadGroup, sameMap
    CHECK(WalkerVerdict(true, true, true, false, true, true, true, true) == WalkerFate::Keep);
    CHECK(WalkerVerdict(false, true, true, false, true, true, true, true) == WalkerFate::Drop);
    CHECK(WalkerVerdict(true, false, true, false, true, true, true, true) == WalkerFate::Drop); // formed up already
    CHECK(WalkerVerdict(true, true, true, false, true, true, false, true) == WalkerFate::Release); // left the group
    CHECK(WalkerVerdict(true, true, true, false, true, false, true, true) == WalkerFate::Release); // lead/binding gone
    CHECK(WalkerVerdict(true, true, true, false, true, true, true, false) == WalkerFate::Release); // another map
    CHECK(WalkerVerdict(true, true, true, true, true, true, true, true) == WalkerFate::Release);   // in a fight
    CHECK(WalkerVerdict(true, true, true, false, false, true, true, true) == WalkerFate::Release); // run not running
    CHECK(WalkerVerdict(true, true, false, false, true, true, true, true) == WalkerFate::Release); // dead
}

// Opus review of 95f66dd8 + 6c8297ec (1): the rogue whose Sap failed stands at its target, inside the current
// pull's pack and beside a living hostile out of combat; the walk back from there was refused by the pack and the
// hostile checks, so it stayed on its own follow (the r26 bug). A pack or hostile already within its threshold at
// the bot's own spot is exempt while the path moves away from it (Codex review of 5a146681: no blanket 20 yd); any
// other pack or hostile, and the exempt one once the path turns back toward it, still refuses it.
static void TestBotWalkLeavesTheStartOutOfThePackChecks()
{
    DungeonPlan plan;
    plan.steps = {PullStep(0, {3855}), PullStep(500, {2})};
    plan.steps[0].radius = 8; // reach 18 yd around (0 0 0), where the rogue stands
    std::unordered_set<std::size_t> none;
    StandHostile target;
    target.x = -1.0f; // the Sap target beside the rogue, behind it as it walks back
    target.entry = 3855;
    std::vector<PathPoint> back = {{0, 0, 0}, {30, 0, 0}, {36, 2, 10}};
    CHECK(BotWalkRefusal(back, 37.0f, 37.0f, &plan, 0, 0, none, {target}).empty());
    // A pack in the middle of the path still refuses it.
    DungeonPlan mid = plan;
    mid.steps[1].x = 30.0f;
    mid.steps[1].radius = 5;
    CHECK(BotWalkRefusal(back, 37.0f, 37.0f, &mid, 0, 0, none, {target}).compare(0, 5, "pack:") == 0);
    // ... and so does a hostile beside the path past the start's 20 yd.
    StandHostile onTheWay;
    onTheWay.x = 28.0f;
    onTheWay.y = 10.0f;
    onTheWay.entry = 3854;
    CHECK(BotWalkRefusal(back, 37.0f, 37.0f, &plan, 0, 0, none, {target, onTheWay}) ==
        "hostile: the path passes within 15.0 yd of 3854");
    // A path that stays within the start's 20 yd: neither check.
    std::vector<PathPoint> shortWay = {{0, 0, 0}, {12, 0, 0}};
    CHECK(BotWalkRefusal(shortWay, 12.0f, 12.0f, &plan, 0, 0, none, {target}).empty());
    // A path that comes back into its start's 20 yd after leaving it: that part is left out too.
    // Codex review of 5a146681: a hostile 16-20 yd from the start that the walk crosses within 15 yd: refused.
    for (float x : {16.0f, 18.0f, 20.0f})
    {
        StandHostile ahead;
        ahead.x = x;
        ahead.y = 3.0f;
        ahead.entry = 3854;
        CHECK(BotWalkRefusal(back, 37.0f, 37.0f, &plan, 0, 0, none, {target, ahead}) ==
            "hostile: the path passes within 15.0 yd of 3854");
    }
    // ... and another pack not cleared yet crossed before the path is 20 yd from its start: refused.
    DungeonPlan near = plan;
    near.steps[1].x = 15.0f;
    near.steps[1].radius = 0; // reach 10 yd, 15 yd from the start
    CHECK(BotWalkRefusal(back, 37.0f, 37.0f, &near, 0, 0, none, {target}).compare(0, 5, "pack:") == 0);
    // A path that first moves away from its own (exempt) pack and target, then turns back toward them: refused.
    std::vector<PathPoint> turn = {{0, 0, 0}, {10, 0, 0}, {2, 0, 0}};
    CHECK(BotWalkRefusal(turn, 2.0f, 18.0f, &plan, 0, 0, none, {}).compare(0, 5, "pack:") == 0);
    CHECK(BotWalkRefusal(turn, 2.0f, 18.0f, nullptr, 0, 0, none, {target}).compare(0, 8, "hostile:") == 0);
    // The exemption is for a unit within its threshold at the start only: one just past it (15.02 yd) that the
    // path dips within 15 yd of, however slightly, is refused.
    std::vector<PathPoint> dip = {{0, 0, 0}, {0, 0.03f, 0}, {20, 0.03f, 0}};
    StandHostile edge;
    edge.y = 15.02f;
    edge.entry = 7;
    CHECK(BotWalkRefusal(dip, 20.0f, 20.03f, nullptr, 0, 0, none, {edge}).compare(0, 8, "hostile:") == 0);
    edge.y = 14.98f; // within it at the start: exempt, and a 0.03 yd dip is within the kBotWalkAwayEpsYd slack
    CHECK(BotWalkRefusal(dip, 20.0f, 20.03f, nullptr, 0, 0, none, {edge}).empty());
    // Moving away and then on past it, never nearer: accepted (a sideways step keeps the distance).
    std::vector<PathPoint> side = {{0, 0, 0}, {0, 20, 0}, {0, 40, 0}};
    CHECK(BotWalkRefusal(side, 40.0f, 40.0f, &plan, 0, 0, none, {}).empty());
    std::vector<PathPoint> loop = {{0, 0, 0}, {25, 0, 0}, {5, 0, 0}};
    StandHostile far;
    far.x = 25.0f;
    far.y = 16.0f;
    far.entry = 1;
    CHECK(BotWalkRefusal(loop, 5.0f, 45.0f, nullptr, 0, 0, none, {far}).empty());
    far.y = 14.0f;
    CHECK(!BotWalkRefusal(loop, 5.0f, 45.0f, nullptr, 0, 0, none, {far}).empty());
}

// Opus review (2): a bot that is summoned in and walked out again by its own formation station (r26) kept starting
// the ladder over the first tick it was back in the gather, so StragglerLadderPending held the ceiling off for ever.
// Its rung now stands for the rest of the step or regroup: walk, summon, settle, then the ceiling pauses.
static void TestStragglerLadderKeepsItsRungWhenTheBotCycles()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    // Pinned (25 yd out, in a fight of its own: never on the ladder) keeps the gather open while Thunderward is in.
    MemberFact pinned = Mem("Pinned", 25.0f);
    pinned.inCombat = true;
    f.members = {Mem("Tanky", 0), StairBot(0), pinned};
    f.maxMemberYd = 38.1f;
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::size_t walks = 0, ladderSummons = 0;
    std::uint32_t tick = 1;
    for (; tick < 600 && r.state == State::Running; ++tick)
    {
        // Down the stair until its walk (45 s); from then on, every 20 s at the top for 3 s (in the gather: the
        // walk or the summon brought it; never on a ceiling's tick), then down the stair again on its formation
        // station.
        bool const in = tick > kStragglerWalkMs / kTickMs && tick % 20 >= 5 && tick % 20 < 8;
        f.members[1] = in ? Mem("Thunderward", 4.0f) : StairBot(tick);
        f.maxMemberYd = in ? 25.0f : f.members[1].yd;
        std::string const got = Tick(r, f);
        walks += got.compare(0, 5, "walk ") == 0;
        if (got == "summon Thunderward" && r.note.find("summoned to the lead") != std::string::npos)
            ++ladderSummons;
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
    CHECK(walks == 1 && ladderSummons == 1);
    CHECK(r.straggle.at("Thunderward").rung == 2);
    CHECK(tick < 200); // bounded, not held off for ever
    // A rung-0 bot back in the gather still starts over; a new step clears every rung.
    Run c = Start(&plan);
    c.phase = Phase::Gather;
    c.straggle["A"].outMs = 30000;
    c.straggle["B"].rung = 1;
    Facts g = Party(0);
    g.members = {Mem("Tanky", 0), Mem("A", 5), Mem("B", 5)};
    UpdateStraggle(c, g, kTickMs);
    CHECK(!c.straggle.count("A") && c.straggle.count("B") && c.straggle.at("B").rung == 1);
    // A kept rung due while its bot is back in the gather: no summon of a bot at the lead bot.
    c.straggle["B"].rungMs = kStragglerSummonMs;
    std::string none;
    CHECK(!StragglerLadderTick(c, g, none) && none.empty());
    c.phase = Phase::Walk;
    UpdateStraggle(c, g, kTickMs);
    CHECK(c.straggle.count("B")); // the step goes on: kept
    ResetStep(c);
    CHECK(c.straggle.empty());
    BeginRegroup(c);
    CHECK(c.straggle.empty());
}
// The test speed (`.bot run start ... speed=<x>`, owner request 2026-10-25).
static void TestTakeSpeedWords()
{
    SpeedArgs a = TakeSpeed("");
    CHECK(!a.given && a.ok && a.rate == 1.0f && a.rest.empty());
    a = TakeSpeed("uldaman from step 57");
    CHECK(!a.given && a.ok && a.rest == "uldaman from step 57");
    a = TakeSpeed("uldaman speed=2 from step 57");
    CHECK(a.given && a.ok && a.rate == 2.0f && a.rest == "uldaman from step 57");
    StartArgs const s = ParseStartArgs(a.rest);
    CHECK(s.ok && s.plan == "uldaman" && s.from == StartFrom::Step && s.value == "57");
    a = TakeSpeed("speed=1.5");
    CHECK(a.given && a.ok && a.rate == 1.5f && a.rest.empty());
    a = TakeSpeed("  uldaman  from 4   speed=3.0 ");
    CHECK(a.ok && a.rate == 3.0f && a.rest == "uldaman from 4");
    a = TakeSpeed("speed=1");
    CHECK(a.given && a.ok && a.rate == 1.0f);
    a = TakeSpeed("SPEED=2");
    CHECK(a.given && a.ok && a.rate == 2.0f);
    for (const char* bad : {"speed=", "speed=3.1", "speed=0.9", "speed=0", "speed=2.25", "speed=2x", "speed=.5",
             "speed=-2", "speed=2,0", "speed=10", "speed=2 speed=2"})
        CHECK(!TakeSpeed(bad).ok);
}

static void TestCheckTestSpeedOrder()
{
    CHECK(CheckTestSpeed(1.0f, true, "Pip", false).code.empty());
    CHECK(CheckTestSpeed(2.0f, true, "", false).code == "real-player");
    CHECK(CheckTestSpeed(2.0f, true, "", false).text ==
        "speed= is for bot-only test runs: a player-led run walks at normal speed.");
    CHECK(CheckTestSpeed(2.0f, false, "Pipwren", false).code == "real-player");
    CHECK(CheckTestSpeed(2.0f, false, "Pipwren", false).text.find("Pipwren is a real player in the party") !=
        std::string::npos);
    CHECK(CheckTestSpeed(2.0f, false, "Pipwren", true).code == "real-player");
    CHECK(CheckTestSpeed(2.0f, false, "", false).code == "forbidden");
    CHECK(CheckTestSpeed(2.0f, false, "", false).text.find("without speed= the run walks at normal speed") !=
        std::string::npos);
    CHECK(CheckTestSpeed(3.0f, false, "", true).code.empty());
}

static void TestSpeedRisesOnlyAfterTheWalkSettles()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 400)};
    Run r = Start(&plan);
    r.restDue = false;
    r.testSpeed = 2.0f;
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 200.0 0.0 0.0" && r.phase == Phase::Walk);
    CHECK(SpeedRateNow(r, f, kTickMs) == 1.0f && r.speedOnMs == kTickMs);    // the settle: one tick
    f.leadX = 20;
    Tick(r, f);
    CHECK(SpeedRateNow(r, f, kTickMs) == 2.0f && r.speedNow == 2.0f);
    f.anyInCombat = true;
    f.leadInCombat = true;
    CHECK(SpeedRateNow(r, f, kTickMs) == 1.0f && r.speedOnMs == 0 && r.speedNow == 1.0f);   // a fight: at once
    f.anyInCombat = false;
    f.leadInCombat = false;
    CHECK(SpeedRateNow(r, f, kTickMs) == 1.0f);
    CHECK(SpeedRateNow(r, f, kTickMs) == 2.0f);
    Run plain = Start(&plan);
    plain.restDue = false;
    Facts g = Party(0);
    CHECK(Tick(plain, g) == "move 200.0 0.0 0.0");
    for (int i = 0; i < 5; ++i)
        CHECK(SpeedRateNow(plain, g, kTickMs) == 1.0f);
    CHECK(plain.speedOnMs == 0 && plain.speedNow == 1.0f);
}

static void TestSpeedWalkingRule()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 400)};
    DungeonPlan hold;
    hold.steps = {Step(PlanStepKind::Hold, 50)};
    DungeonPlan pull;
    pull.steps = {PullStep(40, {1}), Step(PlanStepKind::Move, 400)};
    Run base = Start(&plan);
    base.restDue = false;
    base.testSpeed = 2.0f;
    base.phase = Phase::Walk;
    Facts const f = Party(0);
    auto settled = [](Run r, const Facts& g) { r.speedOnMs = kSpeedSettleMs; return SpeedRateNow(r, g, kTickMs); };
    CHECK(settled(base, f) == 2.0f);
    for (Phase p : {Phase::Begin, Phase::Gather})
    {
        Run r = base;
        r.phase = p;
        CHECK(settled(r, f) == 2.0f);
    }
    for (Phase p : {Phase::AwaitPull, Phase::AwaitTask, Phase::Rest, Phase::Wait, Phase::Hold})
    {
        Run r = base;
        r.phase = p;
        CHECK(settled(r, f) == 1.0f);
    }
    { Facts g = f; g.anyInCombat = true; CHECK(settled(base, g) == 1.0f); }
    { Facts g = f; g.leadInCombat = true; CHECK(settled(base, g) == 1.0f); }
    { Facts g = f; g.eating = 1; CHECK(settled(base, g) == 1.0f); }
    { Run r = base; r.fighting = true; CHECK(settled(r, f) == 1.0f); }
    { Run r = base; r.pullGuid = 9; CHECK(settled(r, f) == 1.0f); }
    { Run r = base; r.ccWatch.push_back(Run::CcWatch()); CHECK(settled(r, f) == 1.0f); }
    { Run r = base; r.shotStandPrep = true; CHECK(settled(r, f) == 1.0f); }
    { Run r = base; Pause(r, "x"); CHECK(settled(r, f) == 1.0f); }
    {
        Run r = Start(&hold);
        r.testSpeed = 2.0f;
        r.phase = Phase::Walk;
        Facts g = f;
        g.seen = false;
        CHECK(settled(r, g) == 1.0f);                  // InEvent
    }
    {
        Run r = Start(&pull);
        r.testSpeed = 2.0f;
        r.phase = Phase::Walk;
        Facts g = f;
        g.seen = false;
        CHECK(settled(r, g) == 2.0f);                  // the approach to a pack out of sight
        g.seen = true;
        CHECK(settled(r, g) == 1.0f);                  // the pack in sight
    }
}

static void TestSpeedSettleNeedsAnUnbrokenWalk()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200)};
    Run r = Start(&plan);
    r.restDue = false;
    r.testSpeed = 2.0f;
    r.phase = Phase::Walk;
    Facts const f = Party(0);
    CHECK(SpeedRateNow(r, f, kTickMs) == 1.0f);
    r.phase = Phase::Rest;
    CHECK(SpeedRateNow(r, f, kTickMs) == 1.0f && r.speedOnMs == 0);
    r.phase = Phase::Walk;
    CHECK(SpeedRateNow(r, f, kTickMs) == 1.0f);
    CHECK(SpeedRateNow(r, f, kTickMs) == 2.0f);
    SpeedFightSeen(r);
    CHECK(r.speedOnMs == 0 && r.speedNow == 1.0f);
    CHECK(SpeedRateNow(r, f, kTickMs) == 1.0f);
    r.testSpeed = 3.0f;
    CHECK(SpeedRateNow(r, f, kTickMs) == 3.0f);
}

static void TestSpeedRealPlayerBlocksTheRestOfTheRun()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200)};
    Run r = Start(&plan);
    r.restDue = false;
    r.testSpeed = 2.0f;
    r.phase = Phase::Walk;
    Facts const f = Party(0);
    SpeedRateNow(r, f, kTickMs);
    CHECK(SpeedRateNow(r, f, kTickMs) == 2.0f);
    Facts g = f;
    PlayerFact p;
    p.name = "Hypeer";
    g.players.push_back(p);
    CHECK(SpeedRateNow(r, g, kTickMs) == 1.0f && r.speedPlayerBlocked);
    for (int i = 0; i < 4; ++i)
        CHECK(SpeedRateNow(r, f, kTickMs) == 1.0f);   // he left: still 1.0
    CHECK(SpeedLabel(r) == "speed 2.0 (test; 1.0 since a real player joined)");
}
// A GM commander starts a bot-only test-speed run from outside the bound group, then joins it while the bots run at
// 3x: his slot blocks the run on that same scan, like any other real player (SyncRunSpeed latches and restores at
// once). Only bots never block.
static void TestSpeedCommanderJoinBlocksTheRun()
{
    std::uint64_t const commander = 4242;
    CHECK(SpeedMemberBlocksRun(commander, commander, false)); // the commander himself, joining: blocks
    CHECK(SpeedMemberBlocksRun(7777, commander, false));      // another real player: blocks
    CHECK(!SpeedMemberBlocksRun(1234, commander, true));      // a commanded bot: never blocks
    CHECK(!SpeedMemberBlocksRun(1234, 9999, true));           // a bot of another run: never blocks
}
// Round 1 (Opus): a tick that advances onto a pull or boss step never starts fast. f.seen was read on the step
// before Tick, so one tick of fast approach could go out when Tick moves onto a pull/boss step whose pack is
// already in sight. The glue caps the rate to 1.0 on such a tick (SpeedCapAfterTick).
static void TestSpeedCappedOnTheTickThatReachesAPullStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50), PullStep(60, {1})};
    Run r = Start(&plan);
    r.restDue = false;
    r.testSpeed = 2.0f;
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0" && r.phase == Phase::Walk);
    // Settled fast walk while approaching the move point.
    r.speedOnMs = kSpeedSettleMs;
    CHECK(SpeedRateNow(r, f, kTickMs) == 2.0f);
    // The lead bot reaches the move point; Tick advances onto the pull step. The pack is already in sight of
    // the new step's point (as the next tick's facts will say), but this tick's facts were read on the move.
    f.leadX = 50;
    std::size_t const stepBefore = r.step;
    CHECK(stepBefore == 0);
    Tick(r, f);
    CHECK(r.step == 1);
    CHECK(SpeedCapAfterTick(r, stepBefore));
    // Same step: no cap (the seen rule in SpeedWalking judges the current step).
    Run same = Start(&plan);
    same.restDue = false;
    CHECK(!SpeedCapAfterTick(same, same.step));
    // Advancing onto a move step: no cap either.
    DungeonPlan moves;
    moves.steps = {Step(PlanStepKind::Move, 50), Step(PlanStepKind::Move, 400)};
    Run m = Start(&moves);
    m.restDue = false;
    Facts g = Party(0);
    CHECK(Tick(m, g) == "move 50.0 0.0 0.0");
    g.leadX = 50;
    std::size_t const moveBefore = m.step;
    Tick(m, g);
    CHECK(m.step == 1);
    CHECK(!SpeedCapAfterTick(m, moveBefore));
}


// Karazhan Crypt shaft dive (2026-10-25): the test speed must wait while a bot
// swims a spline. A rate change re-launches DispatchMovement's stale
// PointMovementGenerator, whose Initialize drops the module's dive spline.
static void TestSpeedChangeWaitsWhileSwimmingASpline()
{
    CHECK(SpeedChangeWaits(true, true) == true);
    CHECK(SpeedChangeWaits(true, false) == false);
    CHECK(SpeedChangeWaits(false, true) == false);
    CHECK(SpeedChangeWaits(false, false) == false);
}

// The speed-restore wait is bounded (lead decision 2026-10-25): a run a real
// player joined restores at once, never waits; otherwise a refused restore
// waits at most kSpeedRestoreWaitMs from the first refusal, then restores anyway.
static void TestSpeedRestoreWaitIsBounded()
{
    CHECK(SpeedRestoreWaits(true, 0) == false);        // a real player latched: restore on the first call
    CHECK(SpeedRestoreWaits(true, 10000) == false);
    CHECK(SpeedRestoreWaits(false, 0) == true);
    CHECK(SpeedRestoreWaits(false, 10000) == true);    // 10 s: still waiting
    CHECK(SpeedRestoreWaits(false, 14999) == true);
    CHECK(SpeedRestoreWaits(false, 15000) == false);   // 15 s: restore anyway
    CHECK(SpeedRestoreWaits(false, 16000) == false);   // 16 s: restored
}
// The test speed's leash (lead decision 2026-10-25, SCHOLO-REFAST-AUDIT 1.5): a fast walk holds once a living bot is
// more than 25 yd (kSpeedLeashYd) from the lead bot, and walks on at 20 yd (kGatherYd) or after 30 s (kGatherCapMs).
// (The common fixture keeps Facts::seen from Party: an unseen move step reads the approach hops, never WalkTo.)
static Run SpeedLeashRun(DungeonPlan& plan, Facts& f, float yd)
{
    plan.steps = {Step(PlanStepKind::Move, 400)};
    Run r = Start(&plan);
    r.restDue = false;
    r.testSpeed = 3.0f;
    r.speedNow = 3.0f;
    f = Party(0);
    f.members = {Mem("Krah", yd)};
    f.maxMemberYd = yd;
    return r;
}

static void SpeedLeashStraggler(Facts& f, float yd)
{
    f.members = {Mem("Krah", yd)};
    f.maxMemberYd = yd;
}

static void TestSpeedLeashHoldsAFastWalkForAStraggler()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    f.leadX = 40;
    SpeedLeashStraggler(f, 30);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.speedLeash);
    CHECK(r.note.find("waits for the party (Krah 30.0 yd behind)") != std::string::npos);
    CHECK(r.phase == Phase::Walk);
    CHECK(SpeedWalking(r, f));
    CHECK(Tick(r, f) == "");
    CHECK(r.speedLeash);
    SpeedLeashStraggler(f, 22);
    CHECK(Tick(r, f) == "");
    CHECK(r.speedLeash);
    SpeedLeashStraggler(f, 18);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    CHECK(!r.speedLeash);
    CHECK(r.note.find("closed up") != std::string::npos);
    // Inside the band (20-25 yd): walking on, no new hold.
    SpeedLeashStraggler(f, 22);
    CHECK(Tick(r, f) == "");
    CHECK(!r.speedLeash);
}

static void TestSpeedLeashLeavesOtherRunsAlone()
{
    for (int which = 0; which < 3; ++which)
    {
        DungeonPlan plan;
        Facts f;
        Run r = SpeedLeashRun(plan, f, 10);
        std::string order;
        CHECK(!WalkTo(r, f, 400, 0, 0, false, kTickMs, order) && order == "move 400.0 0.0 0.0");
        f.leadX = 40;
        SpeedLeashStraggler(f, 30);
        if (which == 0)
        {
            r.testSpeed = 1.0f;
            r.speedNow = 1.0f;
        }
        else if (which == 1)
            r.speedPlayerBlocked = true;
        else
            r.speedNow = 1.0f; // a meal, a pull stand, a pack in sight
        order.clear();
        CHECK(!WalkTo(r, f, 400, 0, 0, false, kTickMs, order));
        CHECK(order == "" && !r.speedLeash);
    }
}

static void TestSpeedLeashSkipsDirectWaterFightsAndEvents()
{
    for (int which = 0; which < 6; ++which)
    {
        DungeonPlan plan;
        Facts f;
        Run r = SpeedLeashRun(plan, f, 30);
        if (which == 1)
            f.leadInWater = true;
        else if (which == 2)
            f.anyInCombat = true;
        else if (which == 3)
            f.leadInCombat = true;
        else if (which == 4)
            r.fighting = true;
        else if (which == 5)
            r.eventOn = true;
        std::string order;
        bool const direct = which == 0;
        CHECK(!WalkTo(r, f, 400, 0, 0, direct, kTickMs, order));
        CHECK(order == (direct ? "move 400.0 0.0 0.0 direct" : "move 400.0 0.0 0.0") && !r.speedLeash);
    }
}

static void TestSpeedLeashHoldsBeforeAFreshWalk()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 40);
    std::string order;
    CHECK(!WalkTo(r, f, 400, 0, 0, false, kTickMs, order));
    CHECK(order == "move 0.0 0.0 0.0"); // the lead bot holds where it stands
    CHECK(r.speedLeash);
    CHECK(r.phase == Phase::Walk);
    CHECK(r.walkX == 400);
    DungeonPlan together;
    Facts g;
    Run s = SpeedLeashRun(together, g, 10);
    order.clear();
    CHECK(!WalkTo(s, g, 400, 0, 0, false, kTickMs, order));
    CHECK(order == "move 400.0 0.0 0.0" && !s.speedLeash);
}

static void TestSpeedLeashIsBoundedByTheGatherCap()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    f.leadX = 40;
    SpeedLeashStraggler(f, 60);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.speedLeash);
    for (int tick = 1; tick <= static_cast<int>(kGatherCapMs / kTickMs); ++tick)
    {
        std::string order = Tick(r, f);
        if (tick < static_cast<int>(kGatherCapMs / kTickMs))
        {
            CHECK(order == "");
            CHECK(r.walkStillMs == 0);
            CHECK(r.walkTries == 1);
            CHECK(r.state == State::Running);
        }
        else
        {
            CHECK(order == "move 400.0 0.0 0.0");
            CHECK(r.note.find("did not close up within 30 s") != std::string::npos);
            CHECK(r.speedLeashSpent);
        }
    }
    CHECK(Tick(r, f) == ""); // spent: walking on, no new hold
    CHECK(!r.speedLeash);
    std::string order;
    CHECK(!WalkTo(r, f, 500, 0, 0, false, kTickMs, order));
    CHECK(order == "move 500.0 0.0 0.0"); // spent for this step: the new point walks on, no new hold
    CHECK(!r.speedLeash);
}

static void TestSpeedLeashNeedsNoRealPlayerThisTick()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    f.leadX = 40;
    SpeedLeashStraggler(f, 30);
    PlayerFact p;
    p.name = "Hypeer";
    f.players.push_back(p); // joins this tick: speedPlayerBlocked still false
    CHECK(!r.speedPlayerBlocked);
    CHECK(Tick(r, f) == "");
    CHECK(!r.speedLeash); // a real player in the party this tick: no hold order
}

static void TestSpeedLeashIgnoresOffMapBots()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    f.leadX = 40;
    MemberFact off;
    off.name = "Krah";
    off.yd = 1000;
    off.x = 1000;
    off.offPlanMap = true;
    f.members = {off};
    f.maxMemberYd = 1000;
    f.walkBackRescue = false; // no `return` order: isolate the leash
    CHECK(Tick(r, f) == "");
    CHECK(!r.speedLeash); // an off-map bot never holds the lead
}

// Karazhan Crypt kcleg6-t1028 step 293: the leash's hold on the lead bot's own spot (swimming under the shaft's ramp
// lip, -11209.8 -1496.3 1.0) was snapped onto the floor above and refused unreachable, and the refusal paused the run.
// The refused hold changed nothing on the lead bot (cmd move refuses before it touches it): the walk goes on, the
// leash is spent for the step, never a pause.
static void TestSpeedLeashRefusedHoldWalksOn()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    f.leadX = 40;
    SpeedLeashStraggler(f, 30);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.speedLeash);
    OrderRefused(r, "move 40.0 0.0 0.0",
        "unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it).");
    CHECK(r.state == State::Running);
    CHECK(!r.speedLeash);
    CHECK(r.speedLeashSpent);
    CHECK(r.phase == Phase::Walk);
    CHECK(r.note.find("the hold for the party was refused") != std::string::npos);
    CHECK(Tick(r, f) == ""); // the walk to 400 is still under way: no new hold, no pause
    CHECK(!r.speedLeash);
    CHECK(r.state == State::Running);
    // Any other refused move on the walk still pauses as before.
    OrderRefused(r, "move 40.0 0.0 0.0", "unreachable: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Paused);
}

static void TestSpeedLeashNeedsSpeedWalking()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    CHECK(SpeedWalking(r, f));
    f.leadX = 40;
    SpeedLeashStraggler(f, 30);
    f.eating = 1; // a meal: SpeedWalking false on the tick's walk facts
    CHECK(!SpeedWalking(r, f));
    CHECK(Tick(r, f) == "");
    CHECK(!r.speedLeash); // the first tick after a pull/shot-stand/meal sends no stop order
}

static void TestSpeedLeashResumesWhenTheRateDrops()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    f.leadX = 40;
    SpeedLeashStraggler(f, 30);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.speedLeash);
    r.speedNow = 1.0f;
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    CHECK(!r.speedLeash && !r.speedLeashSpent);
}

static void TestSpeedLeashResumesTheLegOnALegWalk()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    r.legOn = true;
    r.legX = 100;
    r.legY = 0;
    r.legZ = 0;
    f.leadX = 40;
    SpeedLeashStraggler(f, 30);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    SpeedLeashStraggler(f, 10);
    CHECK(Tick(r, f) == "move 100.0 0.0 0.0");
    CHECK(r.legBestYd == 60.0f);
}

static void TestSpeedLeashClearedByANewStep()
{
    DungeonPlan plan;
    Facts f;
    Run r = SpeedLeashRun(plan, f, 10);
    CHECK(Tick(r, f) == "move 400.0 0.0 0.0");
    f.leadX = 40;
    SpeedLeashStraggler(f, 30);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.speedLeash);
    ResetStep(r);
    CHECK(!r.speedLeash && r.speedLeashMs == 0 && !r.speedLeashSpent);
}

static void TestSpeedShowsInTheLines()
{
    DungeonPlan plan;
    plan.name = "t";
    plan.steps = {Step(PlanStepKind::Move, 50)};
    Run r = Start(&plan);
    CHECK(SpeedLabel(r).empty());
    std::string const plain = StatusLine(r);
    CHECK(plain.find("speed") == std::string::npos);
    r.testSpeed = 2.0f;
    CHECK(SpeedLabel(r) == "speed 2.0 (test)");
    CHECK(StatusLine(r) == plain + "|speed 2.0 (test)");
    r.deferredPulls.push_back(0);
    CHECK(StatusLine(r) == plain + "|left 1|speed 2.0 (test)");
    r.deferredPulls.clear();
    CHECK(DoneNote(r) == "plan done (speed 2.0 (test))");
    r.wipes = 1;
    CHECK(DoneNote(r) == "plan done (speed 2.0 (test); 1 wipe)");
    r.testSpeed = 1.0f;
    CHECK(DoneNote(r) == "plan done (1 wipe)");
    Run e = Start(&plan);
    e.restDue = false;
    e.testSpeed = 1.5f;
    Facts f = Party(0);
    CHECK(Tick(e, f) == "move 50.0 0.0 0.0");
    f.leadX = 50;
    for (int i = 0; i < 3 && e.state == State::Running; ++i)
        Tick(e, f);
    CHECK(e.state == State::Done && e.note == "plan done (speed 1.5 (test))");
    CHECK(StatusLine(e) == "TBM:RUN|t|done|1/1|0|move|begin|plan done (speed 1.5 (test))|speed 1.5 (test)");
}

static PlanStep InLeg(PlanStep s, std::uint8_t leg)
{
    s.leg = leg;
    return s;
}

static PlanStep At(PlanStepKind kind, float x, float y, float z, float radius = -1.0f)
{
    PlanStep s = Step(kind, x, y, z);
    if (radius >= 0.0f)
        s.radius = radius;
    return s;
}

// The hops toward a far pack (out of sight), each dragged back to `base`: the cap asks for a stand.
static void CapApproach(Run& r, Facts& g, float base)
{
    g.seen = false;
    g.approachKnown = true;
    for (int i = 0; i < 100 && r.state == State::Running && !r.approachStandWanted; ++i)
    {
        g.leadX = base;
        g.approachX = base + 40;
        Tick(r, g);
        if (r.approachStandWanted)
            break;
        g.leadX = base + 40;
        Tick(r, g);
    }
    g.leadX = base;
}

// The first order the run sends (a rest gate or a reading tick may come first).
static std::string FirstOrder(Run& r, const Facts& f)
{
    std::string o;
    for (int i = 0; i < 10 && o.empty() && r.state == State::Running; ++i)
        o = Tick(r, f);
    return o;
}

static Facts FarPack(float x)
{
    Facts g = Party(x);
    g.seen = false;
    g.approachKnown = true;
    return g;
}

// The pull at the current step deferred the way r26 slot 5b met it: hops dragged back, no stand.
static void DeferByNoStand(Run& r, Facts& g, float base)
{
    r.restDue = false;
    CHECK(PullMayBeDeferred(r));
    CapApproach(r, g, base);
    CHECK(r.approachStandWanted && r.state == State::Running);
    CHECK(!ApproachStandFound(r, g, false, 0, 0, 0));
}

// Walks a plain move step that is in sight to its point.
static void WalkMove(Run& r, Facts& g, float x, float y = 0, float z = 0)
{
    g.seen = true;
    CHECK(FirstOrder(r, g) == "move " + Num(x) + " " + Num(y) + " " + Num(z));
    g.leadX = x;
    g.leadY = y;
    g.leadZ = z;
    Tick(r, g);
}

// A run in the retry of the deferred pull at `idx`, going on with `ret` afterwards.
static Run InRetry(const DungeonPlan& plan, std::size_t idx, std::size_t ret)
{
    Run r = Start(&plan, idx);
    r.restDue = false;
    r.deferRetryStep = idx;
    r.deferReturnStep = ret;
    r.deferLeg = plan.steps[idx].leg;
    return r;
}

// Windhorn Canyon r26 slot 5b step 68 (2026-10-23): the pack on the z 242 terrace, the party on
// the floor below: 'no safe stand 15.0-25.0 yd from it' paused 4 times, each resume walked back
// to step 59 and hopped into the same pause, and the stop rule ended the run at step 68 of 1890.
// Such a pull is deferred: noted, counted not cleared, the run goes on; once a later move (or a
// won fight) of the same leg leaves the lead bot within 40 yd and 6 yd of height of it, it is
// pulled from there, and the run goes on where it left off. No walk back to it, ever.
static void TestUnreachablePackIsDeferredThenPulledOnTheWay()
{
    std::size_t const npos = static_cast<std::size_t>(-1);
    DungeonPlan plan;
    plan.steps = {InLeg(PullStep(1000, {1}), 0), InLeg(Step(PlanStepKind::Move, 970), 0),
        InLeg(Step(PlanStepKind::Move, 1100), 0), InLeg(Step(PlanStepKind::Move, 1200), 1)};
    plan.name = "windhorn_canyon";
    plan.legs.push_back(ai::PlanLeg{0, "walgan_bloodcaller"});
    Run r = Start(&plan);
    Facts g = FarPack(0);
    DeferByNoStand(r, g, 0);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note.find("pack at 1000.0 0.0 0.0 deferred: not reachable from this floor; the run goes on (8 approach hops "
        "did not bring the party within 60.0 yd of the pack") == 0);
    CHECK(r.note.find("no safe stand 20.0-25.0 yd from it the lead bot has a path to") != std::string::npos);
    CHECK(r.notCleared.count(0) && r.deferredPulls == std::vector<std::size_t>{0});
    CHECK(r.deferLog.find("step 1: pack at 1000.0 0.0 0.0 deferred") == 0);
    CHECK(StatusLine(r).find("TBM:RUN|windhorn_canyon|running|2/4|walgan_bloodcaller|move|") == 0);
    CHECK(StatusLine(r).size() > 7 && StatusLine(r).compare(StatusLine(r).size() - 7, 7, "|left 1") == 0);
    // The move to 970 ends 30 yd from the pack, level with it: pulled from there.
    WalkMove(r, g, 970);
    CHECK(r.state == State::Running && r.step == 0 && r.deferRetryStep == 0 && r.deferReturnStep == 2);
    CHECK(!r.notCleared.count(0) && r.deferredPulls.empty());
    CHECK(r.note.find("; the deferred pack of step 1 at 1000.0 0.0 0.0 is 30.0 yd from the lead bot now: pulled from "
        "here") != std::string::npos);
    CHECK(r.approachStandStep == npos && r.approachHops == 0 && !r.walkBackDue && !r.walkBack);
    // In sight from there: the pull goes out, no walk anywhere first.
    Facts in = g;
    in.seen = true;
    in.units = {Unit(7, 1, 30)};
    CHECK(FirstOrder(r, in) == "pullback 7");
    // Cleared: out of notCleared, nothing left; the run goes on with the step it left.
    NextStep(r, "pack cleared");
    CHECK(r.step == 2 && r.note == "pack cleared" && !r.notCleared.count(0) && r.deferGivenUp.empty());
    CHECK(r.deferRetryStep == npos && StatusLine(r).find("|left") == std::string::npos);
    WalkMove(r, g, 1100);
    WalkMove(r, g, 1200);
    CHECK(r.state == State::Done && r.note == "plan done");
}

// A deferred pack the party never comes near in its leg is skipped at the leg's end: not
// cleared, listed at the run's end and on the status line. Near but 10 yd below is not near.
static void TestDeferredPackNeverReachedIsSkippedAtTheLegEnd()
{
    DungeonPlan plan;
    plan.steps = {InLeg(PullStep(1000, {1}), 0), InLeg(Step(PlanStepKind::Move, 990, 0, 10), 0),
        InLeg(Step(PlanStepKind::Move, 50), 0), InLeg(Step(PlanStepKind::Move, 60), 1)};
    plan.name = "windhorn_canyon";
    plan.legs.push_back(ai::PlanLeg{0, "walgan_bloodcaller"});
    Run r = Start(&plan);
    Facts g = FarPack(0);
    DeferByNoStand(r, g, 0);
    CHECK(r.step == 1);
    WalkMove(r, g, 990, 0, 10); // 14 yd from it, 10 yd above: no retry
    CHECK(r.step == 2 && r.deferredPulls == std::vector<std::size_t>{0} && r.deferRetryStep != 0);
    WalkMove(r, g, 50);
    // The end of leg 0: skipped, not cleared, never walked back to.
    CHECK(r.state == State::Running && r.step == 3 && r.deferredPulls.empty());
    CHECK(r.note.find("; the end of leg walgan_bloodcaller: the deferred pack of step 1 at 1000.0 0.0 0.0 was never "
        "within reach: skipped, not cleared") != std::string::npos);
    CHECK(r.notCleared.count(0) && r.deferGivenUp == std::vector<std::size_t>{0});
    WalkMove(r, g, 60);
    CHECK(r.state == State::Done);
    CHECK(r.note == "plan done (1 pack left alive (deferred, not reachable from this floor): step 1 at 1000.0 0.0 0.0)");
    // The status line: the seven fields as before, then the count.
    CHECK(StatusLine(r) == "TBM:RUN|windhorn_canyon|done|4/4|1|move|begin|plan done (1 pack left alive (deferred, not reachable from "
        "this floor): step 1 at 1000.0 0.0 0.0)|left 1");
    // The wipe counts and the list share the end note.
    Run w = r;
    w.wipes = 1;
    CHECK(DoneNote(w) == "plan done (1 wipe; 1 pack left alive (deferred, not reachable from this floor): step 1 at "
        "1000.0 0.0 0.0)");
    // A pull at the plan's end: deferred, and given up at once (no leg after it).
    DungeonPlan last;
    last.steps = {PullStep(1000, {1})};
    Run e = Start(&last);
    Facts k = FarPack(0);
    DeferByNoStand(e, k, 0);
    CHECK(e.state == State::Done && e.deferGivenUp == std::vector<std::size_t>{0} && e.deferRetryStep != 0);
}

// The Codex review of 6566f84d: every way a retry ends with the pack alive gives it up (listed,
// not cleared), and a deferred pack the run takes up again in order leaves both lists.
static void TestEveryRetryExitThatLeavesThePackAliveGivesItUp()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {7078}), Step(PlanStepKind::Move, 30), Step(PlanStepKind::Move, 40)};
    // (a) One unit pulled and leashed home again and again: out of reach.
    {
        Run r = InRetry(plan, 0, 1);
        Facts f = Party(0);
        f.units = {Unit(42, 7078, 10)};
        for (int i = 0; i < 2000 && r.state == State::Running && r.step == 0; ++i)
        {
            f.anyInCombat = false;
            if (Tick(r, f) == "pullback 42")
            {
                f.anyInCombat = true;
                for (int k = 0; k < 5; ++k)
                    Tick(r, f);
            }
        }
        CHECK(r.state == State::Running && r.step == 1 && r.note.find("skipped, not cleared") != std::string::npos);
        CHECK(r.deferGivenUp == std::vector<std::size_t>{0} && r.notCleared.count(0));
        CHECK(StatusLine(r).compare(StatusLine(r).size() - 7, 7, "|left 1") == 0);
    }
    // (b) Pulls that kill nothing.
    {
        Run r = InRetry(plan, 0, 1);
        Facts f = Party(0);
        for (std::uint64_t g = 1; g <= 10; ++g)
            f.units.push_back(Unit(g, 7078, 10 + g));
        for (int i = 0; i < 3000 && r.state == State::Running && r.step == 0; ++i)
        {
            f.anyInCombat = false;
            if (Tick(r, f).compare(0, 9, "pullback ") == 0)
            {
                f.anyInCombat = true;
                for (int k = 0; k < 5; ++k)
                    Tick(r, f);
            }
        }
        CHECK(r.state == State::Running && r.step == 1 && r.note.find("skipped, not cleared: 10 units") != std::string::npos);
        CHECK(r.deferGivenUp == std::vector<std::size_t>{0} && r.notCleared.count(0));
    }
    // (c) No walkable path toward it on the retry: given up, outside a retry the stand search comes first.
    {
        Facts f = FarPack(0);
        f.approachKnown = false;
        Run r = InRetry(plan, 0, 1);
        Tick(r, f);
        CHECK(r.state == State::Running && r.step == 1);
        CHECK(r.note == "pack at 10.0 0.0 0.0 skipped, not cleared: still not reachable from this floor on its retry (no "
            "walkable path toward the pack at 10.0 0.0 0.0)");
        CHECK(r.deferGivenUp == std::vector<std::size_t>{0} && r.notCleared.count(0));
        Run p = Start(&plan);
        p.restDue = false;
        Tick(p, f);
        CHECK(p.state == State::Running && p.approachStandWanted && p.note.find("no walkable path toward the pack") == 0);
    }
    // (d) Still not reachable on the retry (the hops and the stand search again): given up, no second deferral.
    {
        DungeonPlan far;
        far.steps = {PullStep(1000, {1}), Step(PlanStepKind::Move, 50)};
        Run r = InRetry(far, 0, 1);
        Facts g = FarPack(0);
        CapApproach(r, g, 0);
        CHECK(!ApproachStandFound(r, g, false, 0, 0, 0));
        CHECK(r.step == 1 && r.deferredPulls.empty() && r.deferGivenUp == std::vector<std::size_t>{0});
        CHECK(r.note.find("pack at 1000.0 0.0 0.0 skipped, not cleared: still not reachable from this floor on its retry ("
            "8 approach hops") == 0);
    }
    // (e) `.bot run skip` on the retry.
    {
        Run r = InRetry(plan, 0, 1);
        CHECK(Skip(r) && r.step == 1 && r.deferGivenUp == std::vector<std::size_t>{0});
    }
    // (f) A pause in the retry, then a resume (the Opus note on d7aca6b5): the lead bot still within 40 yd and 6 yd of
    // height of the pack at the regroup's end, the retry goes on, no walk back; farther, or too far below, it is given
    // up and the run is back at the step it left (no walk back to the pack, ever).
    {
        DungeonPlan back;
        back.steps = {Step(PlanStepKind::Move, -200), PullStep(10, {7078}), Step(PlanStepKind::Move, 30),
            Step(PlanStepKind::Move, 40)};
        auto resumed = [&](float x, float z)
        {
            Run r = InRetry(back, 1, 3);
            Pause(r, "fight ceiling");
            CHECK(Resume(r) && r.step == 1 && DeferRetryOn(r) && r.deferGivenUp.empty());
            Facts f = Party(x);
            f.leadZ = z;
            f.seen = false;
            f.approachKnown = true;
            f.approachX = x - 20; // a hop toward the pack, should the step go on in the same tick
            for (int i = 0; i < 20 && r.state == State::Running && r.regroup; ++i)
                Tick(r, f);
            CHECK(!r.regroup);
            return r;
        };
        Run near = resumed(40, 0); // 30 yd from the pack
        CHECK(near.step == 1 && DeferRetryOn(near) && !near.walkBack && near.deferGivenUp.empty());
        Run far = resumed(500, 0);
        CHECK(far.step != 1 && !DeferRetryOn(far) && far.deferGivenUp == std::vector<std::size_t>{1} &&
            far.notCleared.count(1));
        Run below = resumed(15, -10); // 11 yd off, 10 yd below
        CHECK(!DeferRetryOn(below) && below.deferGivenUp == std::vector<std::size_t>{1});
        // A walk back that comes up while a retry is on goes to the step it left, never the retry.
        Run w = InRetry(back, 1, 3);
        Facts f = Party(500);
        StartWalkBack(w, f);
        CHECK(w.step == 3 && w.deferRetryStep != 1 && w.deferGivenUp == std::vector<std::size_t>{1});
    }
    // (g) The stop, the time limit: the retry and the pulls still deferred are listed in the note.
    {
        DungeonPlan three;
        three.steps = {PullStep(10, {1}), Step(PlanStepKind::Move, 30), PullStep(500, {2}), Step(PlanStepKind::Move, 40)};
        Run r = InRetry(three, 0, 1);
        r.deferredPulls = {2};
        r.notCleared.insert(2);
        CHECK(StatusLine(r).compare(StatusLine(r).size() - 7, 7, "|left 2") == 0);
        EndRunEarly(r, "stopped by .bot run stop");
        CHECK(r.state == State::Done && r.note == "stopped by .bot run stop (2 packs left alive (deferred, not reachable "
            "from this floor): step 1 at 10.0 0.0 0.0, step 3 at 500.0 0.0 0.0)");
        CHECK(StatusLine(r).compare(StatusLine(r).size() - 7, 7, "|left 2") == 0);
        Run q = Start(&three, 1);
        EndRunEarly(q, "stopped by .bot run stop");
        CHECK(q.note == "stopped by .bot run stop" && StatusLine(q).find("|left") == std::string::npos);
    }
    // (h) A rewind brings the run back to a deferred (or given-up) pull: taken up fresh, out of both lists.
    {
        DungeonPlan rw;
        rw.steps = {Step(PlanStepKind::Move, 5), PullStep(10, {1}), Step(PlanStepKind::Move, 30)};
        Run r = Start(&rw);
        r.deferredPulls = {1};
        r.notCleared.insert(1);
        NextStep(r, "moved");
        CHECK(r.step == 1 && r.deferredPulls.empty() && !r.notCleared.count(1));
        NextStep(r, "pack cleared");
        CHECK(r.step == 2 && !r.notCleared.count(1) && r.deferGivenUp.empty());
        Run g = Start(&rw);
        g.deferGivenUp = {1};
        g.notCleared.insert(1);
        NextStep(g, "moved");
        CHECK(g.step == 1 && g.deferGivenUp.empty() && !g.notCleared.count(1));
    }
}

// A stand that was walked to and did not bring the pack in sight defers the pull too.
static void TestStandThatDidNotBringThePackInSightDefers()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1000, {1}), Step(PlanStepKind::Move, 50), PullStep(2000, {2})};
    Run r = Start(&plan);
    r.restDue = false;
    Facts g = FarPack(0);
    CapApproach(r, g, 0);
    CHECK(ApproachStandFound(r, g, true, 980, 0, 0));
    CHECK(Tick(r, g) == "move 980.0 0.0 0.0");
    g.leadX = 980;
    Tick(r, g); // arrived, still out of sight (a wall)
    Tick(r, g);
    CHECK(r.state == State::Running && r.step == 1 && r.deferredPulls == std::vector<std::size_t>{0});
    CHECK(r.note.find("pack at 1000.0 0.0 0.0 deferred: not reachable from this floor; the run goes on (the stand "
        "980.0 0.0 0.0 did not bring it within 60.0 yd") == 0);
}

// The Codex and Opus reviews of 6566f84d: a pull is deferred only when nothing later needs it
// gone: never a boss, never with no_defer, never when a later step names one of its entries
// (or the plan kills it first), never with a gating step of its leg near it. Otherwise the pause.
static void TestGatingPullsAndBossesAreNeverDeferred()
{
    auto may = [](std::vector<PlanStep> steps, std::vector<std::uint32_t> killFirst = {})
    {
        DungeonPlan plan;
        plan.steps = steps;
        plan.killFirst = killFirst;
        Run r = Start(&plan);
        return PullMayBeDeferred(r);
    };
    PlanStep const pull = PullStep(1000, {5}); // radius 15
    PlanStep const move = Step(PlanStepKind::Move, 50);
    CHECK(pull.radius == 15.0f);
    CHECK(may({pull}));
    CHECK(may({pull, move, Step(PlanStepKind::Rest), PullStep(2000, {5})})); // a later pack of the same mobs: no gate
    CHECK(!may({PullStep(1000, {5}, ai::kPlanFlagNoDefer)}));
    CHECK(!may({At(PlanStepKind::Boss, 1000, 0, 0)}));
    CHECK(!may({move, PullStep(2000, {5})})); // not a pull at all
    CHECK(!may({pull}, {5}));                  // killed first plan-wide
    CHECK(may({pull}, {6}));
    // A later step naming one of its entries, anywhere later in the plan (another leg too, far away).
    PlanStep hold = At(PlanStepKind::Hold, 5000, 0, 0, 30);
    hold.entries = {5};
    hold.leg = 3;
    CHECK(!may({pull, hold}));
    PlanStep adds = At(PlanStepKind::Boss, 5000, 0, 0, 30);
    adds.entry = 99;
    adds.entries = {5};
    CHECK(!may({pull, adds}));
    PlanStep clear = At(PlanStepKind::WaitClear, 5000, 0, 0, 20);
    clear.entries = {5};
    CHECK(!may({pull, clear}));
    PlanStep talk = At(PlanStepKind::Gossip, 5000, 0, 0);
    talk.entry = 5;
    CHECK(!may({pull, talk}));
    PlanStep other = hold;
    other.entries = {6};
    CHECK(may({pull, other}));
    // The Codex review of d7aca6b5: a later boss whose ai_boss_tactic rows name one of its entries (a kill_first
    // target, an interrupt caster: PlanStep::tacticEntries, filled at load by JoinBossTactics), far away in another leg.
    PlanStep tactic = At(PlanStepKind::Boss, 5000, 0, 0, 30);
    tactic.entry = 99;
    tactic.leg = 3;
    tactic.tacticEntries = {5};
    CHECK(!may({pull, move, tactic}));
    tactic.tacticEntries = {6, 99};
    CHECK(may({pull, move, tactic}));
    // The Codex review of 7cfcbff6: a run in progress keeps its old plan (its boss steps joined the old rows) while a
    // `.bot run reloadplans` swaps the tactic table; the gate reads the live table too (Run::liveTactics, set by the
    // glue each tick), by JoinBossTactics' rule: the plan's map, the boss and its adds.
    {
        DungeonPlan old;
        old.map = 48;
        PlanStep later = At(PlanStepKind::Boss, 5000, 0, 0, 30);
        later.entry = 4887;
        later.entries = {4888};
        later.tacticEntries = {77}; // what the plan joined at its load
        old.steps = {pull, move, later};
        auto live = [](std::uint32_t map, std::uint32_t boss, std::uint32_t target)
        {
            auto table = std::make_shared<ai::BossTacticTable>();
            ai::BossTacticRow row;
            row.map = map;
            row.bossEntry = boss;
            row.kind = ai::BossTacticKind::KillFirst;
            row.targetEntry = target;
            CHECK(table->Add(row));
            table->Seal();
            return table;
        };
        Run run = Start(&old);
        CHECK(PullMayBeDeferred(run)); // the old rows name 77, not the pull's 5
        auto const added = live(48, 4887, 5);  // the reload made 5 a kill_first target of the boss
        run.liveTactics = added.get();
        CHECK(!PullMayBeDeferred(run));
        auto const viaAdd = live(48, 4888, 5); // keyed on the boss step's add
        run.liveTactics = viaAdd.get();
        CHECK(!PullMayBeDeferred(run));
        auto const otherMap = live(47, 4887, 5);
        run.liveTactics = otherMap.get();
        CHECK(PullMayBeDeferred(run));
        DungeonPlan elsewhere = old; // the same steps on map 47: the map-48 rows are not its
        elsewhere.map = 47;
        Run e = Start(&elsewhere);
        e.liveTactics = added.get();
        CHECK(PullMayBeDeferred(e));
        e.liveTactics = otherMap.get();
        CHECK(!PullMayBeDeferred(e));
        auto const otherTarget = live(48, 4887, 6);
        run.liveTactics = otherTarget.get();
        CHECK(PullMayBeDeferred(run));
        ai::BossTacticTable unsealed;
        run.liveTactics = &unsealed;
        CHECK(PullMayBeDeferred(run));
        // The active run with the added row: the unreachable pull pauses, never deferred.
        Run k = Start(&old);
        k.restDue = false;
        k.liveTactics = added.get();
        Facts kg = FarPack(0);
        CapApproach(k, kg, 0);
        CHECK(!ApproachStandFound(k, kg, false, 0, 0, 0));
        CHECK(k.state == State::Paused && k.step == 0 && k.deferredPulls.empty());
    }
    // ... and such a pull pauses as before instead of being deferred.
    {
        PlanStep named = tactic;
        named.tacticEntries = {5};
        DungeonPlan keep;
        keep.steps = {pull, move, named};
        Run k = Start(&keep);
        k.restDue = false;
        Facts kg = FarPack(0);
        CapApproach(k, kg, 0);
        CHECK(!ApproachStandFound(k, kg, false, 0, 0, 0));
        CHECK(k.state == State::Paused && k.step == 0 && k.deferredPulls.empty());
    }
    // A gating step of the same leg within its radius (40 yd without one) + the pack's 15 yd of the pack.
    CHECK(!may({pull, At(PlanStepKind::Hold, 1040, 0, 0, 30)}));  // 40 <= 45
    CHECK(may({pull, At(PlanStepKind::Hold, 1046, 0, 0, 30)}));   // 46 > 45
    CHECK(!may({pull, At(PlanStepKind::Boss, 1040, 0, 0, 30)}));
    CHECK(!may({pull, At(PlanStepKind::Open, 1054, 0, 0, 0)}));   // 54 <= 40 + 15
    CHECK(may({pull, At(PlanStepKind::Open, 1056, 0, 0, 0)}));
    CHECK(!may({pull, At(PlanStepKind::WaitObject, 1020, 0, 0, 0)}));
    CHECK(!may({pull, At(PlanStepKind::Loot, 1020, 0, 0, 0)}));
    CHECK(!may({pull, At(PlanStepKind::MoveDirect, 1050, 0, 0, 0)}));
    CHECK(!may({pull, At(PlanStepKind::Hold, 1000, 0, 40, 30)}));  // 3D: 40 yd above, within 45
    CHECK(may({pull, At(PlanStepKind::Hold, 1000, 0, 50, 30)}));   // 3D: 50 yd above (straight over it), past 45
    PlanStep exact = At(PlanStepKind::Move, 1050, 0, 0, 0);
    exact.flags = ai::kPlanFlagExact;
    CHECK(!may({pull, exact}));
    CHECK(may({pull, At(PlanStepKind::Move, 1010, 0, 0, 0)})); // a plain move beside it: no gate
    PlanStep nextLeg = At(PlanStepKind::Hold, 1040, 0, 0, 30);
    nextLeg.leg = 1;
    CHECK(may({pull, nextLeg})); // another leg's step near it: not this leg's gate
    // A gated pull pauses as before, its note unchanged.
    DungeonPlan gated;
    gated.steps = {pull, At(PlanStepKind::Hold, 1040, 0, 0, 30)};
    Run r = Start(&gated);
    r.restDue = false;
    Facts g = FarPack(0);
    CapApproach(r, g, 0);
    CHECK(!ApproachStandFound(r, g, false, 0, 0, 0));
    CHECK(r.state == State::Paused && r.step == 0 && r.deferredPulls.empty() && r.deferLog.empty());
    CHECK(r.note.find("; the pack at 1000.0 0.0 0.0 is not reachable from this floor: no safe stand") != std::string::npos);
}

// The Opus review of 6566f84d (its probe): during a retry the run's step is the deferred pull,
// and the stand search counted every pull after it as live, the ones the run had cleared since
// too. The search (ApproachStandReject, PickApproachStand: the glue's ApproachStandSearch keeps
// what they keep) counts as live only the step the run goes on with and later ones, and earlier
// ones notCleared names.
static void TestStandSearchDuringARetryCountsOnlyLivePacks()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1000, {1}), PullStep(300, {2}), Step(PlanStepKind::Move, 400), PullStep(700, {3}),
        InLeg(Step(PlanStepKind::Move, 500), 1)};
    const PlanStep& a = plan.steps[0];
    StandCandidate c;
    c.x = 980;
    c.reachable = true;
    c.los = true;
    c.pathYd = 900;
    // Past B (300, cleared on the way), then 60 yd north of C (700, not reached yet), to the stand.
    c.path = {{200, 0, 0}, {350, 0, 0}, {350, 60, 0}, {980, 60, 0}, {980, 0, 0}};
    std::vector<StandHostile> const hostiles = {{1000, 0, 0, 1}};
    Run r = InRetry(plan, 0, 2);
    CHECK(DeferRetryOn(r) && LiveFrom(r, false) == 2 && LiveFrom(r, true) == 2);
    CHECK(ApproachStandReject(r, a, c, hostiles, 0).empty());
    CHECK(PickApproachStand(r, a, {c}, hostiles, 0) == 0);
    // B skipped earlier (notCleared): live again.
    Run s = r;
    s.notCleared.insert(1);
    CHECK(ApproachStandReject(s, a, c, hostiles, 0) == "its path passes an uncleared pack");
    // A path past C (after the step the run goes on with): live.
    StandCandidate d = c;
    d.path = {{400, 0, 0}, {980, 0, 0}};
    CHECK(ApproachStandReject(r, a, d, hostiles, 0) == "its path passes an uncleared pack");
    // Not a retry (the run at step 0 the first time): B is ahead, live.
    Run n = Start(&plan);
    CHECK(!DeferRetryOn(n) && LiveFrom(n, false) == 1 && LiveFrom(n, true) == 0);
    CHECK(ApproachStandReject(n, a, c, hostiles, 0) == "its path passes an uncleared pack");
    // The retry reached the opportunistic way (the probe's shape): A deferred, B cleared, then the
    // move at 400 ends 600 yd off: no retry; one that ends 30 yd off: the retry, with return step 3.
    DungeonPlan p2;
    p2.steps = {PullStep(1000, {1}), PullStep(300, {2}), Step(PlanStepKind::Move, 970), PullStep(1500, {3}),
        InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run o = Start(&p2, 1);
    o.deferredPulls = {0};
    o.notCleared.insert(0);
    o.leadSeen = true;
    o.leadLastX = 300;
    NextStep(o, "pack cleared");
    CHECK(o.step == 2 && !DeferRetryOn(o));
    o.leadLastX = 970;
    NextStep(o, "moved");
    CHECK(o.step == 0 && DeferRetryOn(o) && o.deferReturnStep == 3 && LiveFrom(o, false) == 3);
    CHECK(ApproachStandReject(o, p2.steps[0], c, hostiles, 0).empty());
}

// Windhorn Canyon r26 slot 5b: each resume of the step 68 pause walked back to step 59 and hopped
// the same ~100 s loop into the same pause. A resume whose lead bot still stands where the pause
// left it walks nowhere: the stand search goes out from there, and none found defers the pull.
static void TestResumeOfTheApproachPauseSearchesInPlaceThenDefers()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, -200), PullStep(1000, {1}), Step(PlanStepKind::Move, 50),
        PullStep(2000, {2})};
    auto pausedRun = [&](float leadAt)
    {
        Run r = Start(&plan, 1);
        r.restDue = false;
        // The pause as a 19z44e run left it: the hops, then the stand search spent.
        Facts p = FarPack(leadAt);
        r.approachStandStep = 1;
        r.approachStand = 3;
        PauseApproach(r, p, "8 approach hops did not bring the party within 60.0 yd of the pack; the pack at 1000.0 0.0 "
            "0.0 is not reachable from this floor: no safe stand");
        return r;
    };
    Run r = pausedRun(0);
    CHECK(r.state == State::Paused && r.approachPauseKnown && r.approachPauseX == 0.0f);
    CHECK(Resume(r) && r.approachStandResumed);
    Facts g = FarPack(0);
    g.approachX = 40;
    bool walkedBack = false, hopped = false;
    for (int i = 0; i < 60 && r.state == State::Running && r.step == 1; ++i)
    {
        std::string const o = Tick(r, g);
        walkedBack = walkedBack || o == "move -200.0 0.0 0.0" || r.walkBack;
        hopped = hopped || o == "move 40.0 0.0 0.0";
        if (r.approachStandWanted)
            ApproachStandFound(r, g, false, 0, 0, 0);
    }
    CHECK(!walkedBack && !hopped); // the regroup ended in place (its note gives way to the search's on that tick)
    CHECK(r.state == State::Running && r.step == 2 && r.deferredPulls == std::vector<std::size_t>{1});
    CHECK(r.note.find("pack at 1000.0 0.0 0.0 deferred: not reachable from this floor; the run goes on (resumed: no safe "
        "stand") == 0);
    // A lead bot a player moved away from the pause's spot: the walk back as before.
    Run m = pausedRun(0);
    CHECK(Resume(m));
    Facts away = FarPack(-100);
    bool backed = false;
    for (int i = 0; i < 30 && m.state == State::Running && !backed; ++i)
    {
        Tick(m, away);
        backed = m.walkBack;
    }
    CHECK(backed);
}

// BFD r29 slot 6 step 580 (pauses 2-4): a hop that finds no way (none, or ending at the lead bot) gets the stand
// search first, then the deferral; a stand found walks there, no deferral.
static void TestNoPathTowardThePackSearchesAStandThenDefers()
{
    DungeonPlan plan;
    plan.steps = {InLeg(PullStep(300, {1}), 0), InLeg(Step(PlanStepKind::Move, 400), 0), InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = FarPack(0);
    f.approachRead = true;
    f.approachKnown = false;       // the hop read finds no way
    CHECK(PullMayBeDeferred(r));
    Tick(r, f);
    CHECK(r.state == State::Running && r.approachStandWanted);
    CHECK(r.note == "no walkable path toward the pack at 300.0 0.0 0.0: looking for a stand within 25.0 yd of it");
    ApproachStandFound(r, f, false, 0, 0, 0);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.deferredPulls == std::vector<std::size_t>{0} && r.notCleared.count(0));
    CHECK(r.note.find("pack at 300.0 0.0 0.0 deferred: not reachable from this floor") == 0);
    CHECK(r.note.find("no walkable path toward the pack at 300.0 0.0 0.0; no safe stand") != std::string::npos);
    // A stand found: the party walks there, no deferral.
    Run s = Start(&plan);
    s.restDue = false;
    Tick(s, f);
    CHECK(s.approachStandWanted);
    ApproachStandFound(s, f, true, 280, 0, 0);
    CHECK(s.state == State::Running && s.step == 0 && s.approachStand == 2 && s.deferredPulls.empty());
}

// A boss with no way on: the stand search, then the approach pause (a boss is never deferred).
static void TestNoPathTowardABossPausesAfterTheStandSearch()
{
    DungeonPlan plan;
    PlanStep b = Step(PlanStepKind::Boss, 300);
    b.entry = 4832;
    ai::NormalizePlanStep(b);
    b.entry = 4832;
    ai::NormalizePlanStep(b);
    plan.steps = {InLeg(b, 0), InLeg(Step(PlanStepKind::Move, 400), 0), InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = FarPack(0);
    f.approachRead = true;
    f.approachKnown = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.approachStandWanted);
    ApproachStandFound(r, f, false, 0, 0, 0);
    CHECK(r.state == State::Paused && r.note.find("no walkable path toward the boss at 300.0 0.0 0.0; the boss at 300.0 0.0 0.0 is not reachable from this floor: no safe stand") == 0 && r.approachPauseKnown);
}

// Pause 1: the lead bot in water, the hop walk stuck after kWalkTries, gets the stand search then the deferral.
static void TestStuckApproachHopWalkSearchesAStandThenDefers()
{
    DungeonPlan plan;
    plan.steps = {InLeg(PullStep(300, {1}), 0), InLeg(Step(PlanStepKind::Move, 400), 0), InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = FarPack(0);
    f.approachRead = true;
    f.approachKnown = true;
    f.approachX = 40;
    f.leadInWater = true;
    CHECK(FirstOrder(r, f) == "move 40.0 0.0 0.0");   // the hop
    // The lead bot never moves (stays at 0 0 0).
    for (int i = 0; i < 400 && r.state == State::Running && !r.approachStandWanted; ++i)
    {
        Tick(r, f);
        if (r.stallStandInWanted)
            StallStandIn(r, false, 0, 0, 0);
    }
    CHECK(r.state == State::Running && r.approachStandWanted);
    CHECK(r.note.find("the lead bot is stuck 40.0 yd from 40.0 0.0 0.0 (lead in water: yes") == 0);
    CHECK(r.note.find("on the way toward the pack at 300.0 0.0 0.0: looking for a stand within 25.0 yd of it") != std::string::npos);
    ApproachStandFound(r, f, false, 0, 0, 0);
    CHECK(r.state == State::Running && r.step == 1 && r.deferredPulls == std::vector<std::size_t>{0});
}

// Review round 1 (spec rule 2b(ii) unpinned): the stand searched and found (~280 yd, as the probe), the lead bot
// never moves, loop Tick + the stall feed until kWalkTries is spent: the pull is deferred (not paused), the step
// advanced. Replacing the spent branch's DeferUnreachablePack with false (M9) leaves every suite green otherwise.
static void TestStuckStandWalkAfterTheStandWasTriedDefers()
{
    DungeonPlan plan;
    plan.steps = {InLeg(PullStep(300, {1}), 0), InLeg(Step(PlanStepKind::Move, 400), 0), InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = FarPack(0);
    f.approachRead = true;
    f.approachKnown = false; // the hop read finds no way: stand search first
    Tick(r, f);
    CHECK(r.state == State::Running && r.approachStandWanted);
    ApproachStandFound(r, f, true, 280, 0, 0); // the stand ~280 yd as in the reviewer's probe
    CHECK(r.state == State::Running && r.step == 0 && r.approachStand == 2 && r.deferredPulls.empty());
    // The lead bot never moves (stays at 0 0 0): the walk to the stand stalls kWalkTries times, then defers.
    for (int i = 0; i < 400 && r.state == State::Running && r.step == 0 && r.deferredPulls.empty(); ++i)
    {
        Tick(r, f);
        if (r.stallStandInWanted)
            StallStandIn(r, false, 0, 0, 0);
    }
    CHECK(r.state == State::Running && r.step == 1 && r.deferredPulls == std::vector<std::size_t>{0});
    CHECK(r.note.find("the lead bot is stuck 280.0 yd from 280.0 0.0 0.0") != std::string::npos);
    CHECK(r.note.find("deferred: not reachable from this floor") != std::string::npos);
}

// The boss twin: the same walk on a boss step ends in the approach pause (a boss is never deferred).
static void TestStuckStandWalkOnABossStepPausesAfterTheStandWasTried()
{
    DungeonPlan plan;
    PlanStep b = Step(PlanStepKind::Boss, 300);
    b.entry = 4832;
    b.radius = 10; // a tight boss: the 20 yd ring stand below is legal (min ring 15)
    ai::NormalizePlanStep(b);
    b.entry = 4832;
    ai::NormalizePlanStep(b);
    plan.steps = {InLeg(b, 0), InLeg(Step(PlanStepKind::Move, 400), 0), InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = FarPack(0);
    f.approachRead = true;
    f.approachKnown = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.approachStandWanted);
    ApproachStandFound(r, f, true, 280, 0, 0); // the stand 20 yd from the boss
    CHECK(r.state == State::Running && r.step == 0 && r.approachStand == 2 && r.deferredPulls.empty());
    for (int i = 0; i < 400 && r.state == State::Running && r.step == 0; ++i)
    {
        Tick(r, f);
        if (r.stallStandInWanted)
            StallStandIn(r, false, 0, 0, 0);
    }
    CHECK(r.state == State::Paused && r.step == 0 && r.deferredPulls.empty() && r.approachPauseKnown);
    CHECK(r.note.find("the lead bot is stuck") == 0);
}

// Review round 1 (walk-back guard pin): a walk back active with a stalled hop walk: ApproachWalkStuck does not
// fire (no deferral, no approach pause during the walk back); the walk's own stuck pause rules. Dropping
// `r.walkBack ||` from ApproachWalkStuck survives otherwise.
static void TestStuckWalkDuringAWalkBackStaysAPlainPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), InLeg(PullStep(300, {1}), 0), InLeg(Step(PlanStepKind::Move, 400), 0)};
    Run r = Start(&plan, 1);
    r.restDue = false;
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    r.hopWalk = true;
    r.hopX = 40; r.hopY = 0; r.hopZ = 0;
    Facts f = Party(0);
    std::string order;
    for (int i = 0; i < 400 && r.state == State::Running; ++i)
    {
        order.clear();
        WalkTo(r, f, 40, 0, 0, false, kTickMs, order);
        if (r.stallStandInWanted)
            StallStandIn(r, false, 0, 0, 0);
    }
    CHECK(r.state == State::Paused && r.note.find("the lead bot is stuck 40.0 yd from 40.0 0.0 0.0") == 0);
    CHECK(!r.approachStandWanted && r.deferredPulls.empty() && !r.approachPauseKnown);
}

// The guard on the hop's point: a stuck walk that is not the step's approach walk still pauses.
static void TestStuckWalkThatIsNotTheHopStillPauses()
{
    DungeonPlan plan;
    plan.steps = {InLeg(PullStep(300, {1}), 0), InLeg(Step(PlanStepKind::Move, 400), 0), InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run r = Start(&plan);
    r.restDue = false;
    r.hopWalk = true;              // stale from an earlier hop
    r.hopX = 40; r.hopY = 0; r.hopZ = 0;
    Facts f = Party(0);
    std::string order;
    for (int i = 0; i < 400 && r.state == State::Running; ++i)
    {
        order.clear();
        WalkTo(r, f, 80, 0, 0, false, kTickMs, order);
        if (r.stallStandInWanted)
            StallStandIn(r, false, 0, 0, 0);
    }
    CHECK(r.state == State::Paused && r.note.find("the lead bot is stuck 80.0 yd from 80.0 0.0 0.0") == 0 && !r.approachStandWanted);
}

static const char* const kFiringRefusal = "unreachable: The tank could not start moving to a firing position for the pull.";
static const char* const kFiringWhy =
    "cmd pullback 7 refused: unreachable: The tank could not start moving to a firing position for the pull";
static DungeonPlan FiringPlan(std::uint32_t flags = 0)
{
    DungeonPlan plan;
    plan.steps = {InLeg(PullStep(40, {1}, flags), 0), InLeg(Step(PlanStepKind::Move, 400), 0),
        InLeg(Step(PlanStepKind::Move, 500), 1)};
    return plan;
}

// Stormwrought Ruins r36 slot J step 553 / Windhorn Canyon r36 slot G step 1854: the step's
// pull went out with the pack in sight but out of the tank's shot, its firing stands spent;
// the refused pull counts no pull and gets the approach stand search, then (none found) the deferral.
static void TestRefusedFiringPositionSearchesAStandThenDefers()
{
    DungeonPlan plan = FiringPlan();
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.units = {Unit(7, 1, 40)};
    CHECK(PullMayBeDeferred(r));
    CHECK(FirstOrder(r, f) == "pullback 7");
    OrderRefused(r, "pullback 7", kFiringRefusal);
    CHECK(r.state == State::Running && r.approachStandWanted && r.refusedPullStandStep == 0);
    CHECK(r.pullGuid == 0 && r.pulls == 0 && r.pullTries[7] == 0 && r.phase == Phase::Begin);
    CHECK(r.note == std::string(kFiringWhy) + ": looking for a stand within 25.0 yd of it");
    // The next tick (the pack still in sight) is the stand's: no second pullback before the glue's answer.
    CHECK(Tick(r, f).empty());
    CHECK(r.approachStandWanted && r.note.find("approach: looking for a stand within 25.0 yd of the pack at 40.0 0.0 0.0") == 0);
    ApproachStandFound(r, f, false, 0, 0, 0);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.deferredPulls == std::vector<std::size_t>{0} && r.notCleared.count(0));
    CHECK(r.note.find("pack at 40.0 0.0 0.0 deferred: not reachable from this floor") == 0);
    CHECK(Has(r.note, std::string(kFiringWhy) + "; no safe stand"));
    CHECK(r.refusedPullStandStep == static_cast<std::size_t>(-1));
}

// A stand found: the party walks there, and the pull refused from it defers at once (the stand is spent).
static void TestRefusedFiringPositionPullsFromTheStandThenDefers()
{
    DungeonPlan plan = FiringPlan();
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.units = {Unit(7, 1, 40)};
    CHECK(FirstOrder(r, f) == "pullback 7");
    OrderRefused(r, "pullback 7", kFiringRefusal);
    CHECK(Tick(r, f).empty());
    CHECK(ApproachStandFound(r, f, true, 20, 0, 0));          // 20 yd from the pack
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0");                 // the walk to the stand, no pullback
    f.leadX = 20;
    f.units[0].distLead = 20;
    CHECK(FirstOrder(r, f) == "pullback 7");                  // from the stand
    CHECK(r.pullTries[7] == 1);                               // the refused one was not counted
    OrderRefused(r, "pullback 7", kFiringRefusal);
    CHECK(r.state == State::Running && r.step == 1 && !r.approachStandWanted);
    CHECK(r.deferredPulls == std::vector<std::size_t>{0} && r.note.find("pack at 40.0 0.0 0.0 deferred") == 0);
    CHECK(Has(r.note, std::string("(") + kFiringWhy + ")"));
    CHECK(r.deferLog.find("step 1: pack at 40.0 0.0 0.0 deferred") == 0);
}

// Blackrock Depths r35 slot 6 step 91: the pull stand on, no stand found; the refusal still takes the way out.
static void TestRefusedFiringPositionAfterNoFiringStand()
{
    DungeonPlan plan = FiringPlan();
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = StandParty();
    f.units = {Seen(7, 1, 40, false)};     // out of sight: not shootable
    f.shotStandGuid = 7;                   // the glue's read answers every request: none known
    CHECK(FirstOrder(r, f) == "pullback 7");
    CHECK(Has(r.note, "no firing stand found"));
    CHECK(r.shotStandTries[7] == kShotStandTries);
    OrderRefused(r, "pullback 7", kFiringRefusal);
    CHECK(r.state == State::Running && r.approachStandWanted);
    CHECK(r.shotStandTries[7] == kShotStandTries);          // no firing-stand stage (1c)
}

// A boss is never deferred: the stand search, then the approach pause.
static void TestRefusedFiringPositionOfABossPausesAfterTheStandSearch()
{
    DungeonPlan plan;
    PlanStep b = Step(PlanStepKind::Boss, 40);
    b.entry = 4832;
    ai::NormalizePlanStep(b);
    plan.steps = {InLeg(b, 0), InLeg(Step(PlanStepKind::Move, 400), 0), InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.units = {Unit(7, 4832, 40)};
    CHECK(FirstOrder(r, f) == "pullback 7");
    OrderRefused(r, "pullback 7", kFiringRefusal);
    CHECK(r.state == State::Running && r.approachStandWanted);
    CHECK(Tick(r, f).empty());
    ApproachStandFound(r, f, false, 0, 0, 0);
    CHECK(r.state == State::Paused && r.approachPauseKnown);
    CHECK(r.note.find(std::string(kFiringWhy) + "; the boss at 40.0 0.0 0.0 is not reachable from this floor: no safe stand") == 0);
}

// A deferred pull's retry refused again is given up, never paused.
static void TestRefusedFiringPositionOnARetryIsGivenUp()
{
    DungeonPlan plan;
    plan.steps = {InLeg(PullStep(40, {1}), 0), InLeg(Step(PlanStepKind::Move, 400), 0),
        InLeg(Step(PlanStepKind::Move, 450), 0), InLeg(Step(PlanStepKind::Move, 500), 1)};
    Run r = InRetry(plan, 0, 2);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 40)};
    CHECK(FirstOrder(r, f) == "pullback 7");
    OrderRefused(r, "pullback 7", kFiringRefusal);
    CHECK(r.state == State::Running && r.step == 2 && !r.approachStandWanted);
    CHECK(r.notCleared.count(0) && r.deferGivenUp == std::vector<std::size_t>{0});
    CHECK(r.note.find("pack at 40.0 0.0 0.0 skipped, not cleared: still not reachable from this floor on its retry (" +
        std::string(kFiringWhy) + ")") == 0);
}

// The guards: no pull of the run out, another refusal reason, a hop pull. All still pause.
static void TestOtherPullbackRefusalsStillPause()
{
    DungeonPlan plan = FiringPlan();
    // (a) no pull of the run out (pullGuid 0, not AwaitPull)
    Run a = Start(&plan);
    OrderRefused(a, "pullback 7", kFiringRefusal);
    CHECK(a.state == State::Paused && a.note == std::string("cmd pullback 7 refused: ") + kFiringRefusal);
    // (b) the step's pull out, refused for another reason
    Run b = Start(&plan);
    b.restDue = false;
    Facts f = Party(0);
    f.units = {Unit(7, 1, 40)};
    CHECK(FirstOrder(b, f) == "pullback 7");
    OrderRefused(b, "pullback 7", "busy: the tank is pulling");
    CHECK(b.state == State::Paused && !b.approachStandWanted);
    // (c) a move's hop pull refused unreachable
    DungeonPlan moves;
    moves.steps = {Step(PlanStepKind::Move, 100)};
    Run c = Start(&moves);
    c.restDue = false;
    Facts g = Party(0);
    g.hopUnits = {Unit(9, 4, 30)};
    CHECK(FirstOrder(c, g) == "pullback 9" && c.pullHop);
    OrderRefused(c, "pullback 9", kFiringRefusal);
    CHECK(c.state == State::Paused && c.note == std::string("cmd pullback 9 refused: ") + kFiringRefusal);
}
// Round 1 (a): a walk-back hop pull on a PULL step, refused for the firing position, still
// pauses (not the T6(c) move-step shape, whose kind check already rejects it: the current
// step here is a pull step, so only the walkBack term of the guard (~6337) rejects it).
static void TestRefusedWalkBackHopPullOnPullStepPauses()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(130, {1})};
    Run r = Start(&plan, 1);
    r.walkBack = true;
    r.walkBackDirect = false;
    r.walkBackIdx = r.walkBackStart = 0;
    r.restDue = false;
    Facts f = Party(130);
    f.hopUnits = {Unit(9, 4, 30)};
    CHECK(FirstOrder(r, f) == "pullback 9" && r.walkBack);
    std::uint32_t const pullsBefore = r.pulls;
    std::uint32_t const walkPullsBefore = r.walkBackPulls;
    OrderRefused(r, "pullback 9", kFiringRefusal);
    CHECK(r.state == State::Paused && !r.approachStandWanted);
    CHECK(r.note == std::string("cmd pullback 9 refused: ") + kFiringRefusal);
    CHECK(r.pulls == pullsBefore && r.walkBackPulls == walkPullsBefore);
    CHECK(r.walkBack && r.pullGuid == 9);
}

// Round 1 (b): the step's own pull out while a walk back is on (walkBack=true, hop=false),
// refused the same way, still pauses (only the walkBack term of the guard rejects it).
static void TestRefusedWalkBackReplayPullPauses()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(130, {1})};
    Run r = Start(&plan, 1);
    r.restDue = false;
    Facts f = Party(130);
    f.units = {Unit(7, 1, 40)};
    CHECK(FirstOrder(r, f) == "pullback 7");
    CHECK(!r.pullHop && r.pullGuid == 7);
    r.walkBack = true; // the walk back's replay underway (walkBack=true, hop=false)
    r.walkBackDirect = false;
    r.walkBackIdx = r.walkBackStart = 0;
    OrderRefused(r, "pullback 7", kFiringRefusal);
    CHECK(r.state == State::Paused && !r.approachStandWanted);
    CHECK(r.note == std::string("cmd pullback 7 refused: ") + kFiringRefusal);
    CHECK(r.pullGuid == 7 && r.pulls == 1);
}

// Round 1 (c): a pull step while the event is on (InEvent via eventOn, ~1620; EventHoldAhead
// covers the start steps leading into a hold). Its firing-position refusal still pauses
// (only the InEvent term rejects it).
static void TestRefusedFiringPositionBeforeEventHoldPauses()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {1}), Step(PlanStepKind::Move, 80), Step(PlanStepKind::Hold, 120)};
    Run r = Start(&plan);
    r.restDue = false;
    r.eventOn = true; // the event leading into the hold is on
    Facts f = Party(0);
    f.units = {Unit(7, 1, 40)};
    CHECK(InEvent(r));
    CHECK(FirstOrder(r, f) == "pullback 7");
    OrderRefused(r, "pullback 7", kFiringRefusal);
    CHECK(r.state == State::Paused && !r.approachStandWanted);
    CHECK(r.note == std::string("cmd pullback 7 refused: ") + kFiringRefusal);
}

// Round 1 (d): the FightStep hook's own guard (~8091). A stand found out of sight
// (approach state 2) whose pack is in sight on the next tick, with NO refusal on this
// step: the pack is pulled from here, never walked to the stand.
static void TestStandFoundThenSeenPullsWithoutWalking()
{
    DungeonPlan plan = FiringPlan(ai::kPlanFlagNoDefer); // never deferred: the pull goes out
    Run r = Start(&plan);
    r.restDue = false;
    r.approachStandStep = r.step; // the glue found a stand out of sight...
    r.approachStand = 2;
    r.approachStandX = 20;
    r.approachStandY = 0;
    r.approachStandZ = 0;
    CHECK(r.refusedPullStandStep != r.step); // ...but no pullback was refused on this step
    Facts f = Party(0); // the pack in sight on the next tick
    f.units = {Unit(7, 1, 40)};
    CHECK(FirstOrder(r, f) == "pullback 7");
    CHECK(r.approachStand == 2 && !r.approachStandWanted);
}

// A `no_defer` pull: two refusals (before and after the stand), then the approach pause; a resume,
// at most two more, then the pause again.
static void TestRefusedFiringPositionNeverLoops()
{
    DungeonPlan plan = FiringPlan(ai::kPlanFlagNoDefer);
    Run r = Start(&plan);
    r.restDue = false;
    CHECK(!PullMayBeDeferred(r));
    Facts f = Party(0);
    f.units = {Unit(7, 1, 40)};
    auto drive = [&](int& refusals)
    {
        for (int i = 0; i < 80 && r.state == State::Running; ++i)
        {
            std::string const o = Tick(r, f);
            if (r.approachStandWanted)
                ApproachStandFound(r, f, true, 20, 0, 0);
            if (o == "pullback 7")
            {
                ++refusals;
                OrderRefused(r, o, kFiringRefusal);
            }
            else if (o == "move 20.0 0.0 0.0")
            {
                f.leadX = 20;
                f.units[0].distLead = 20;
            }
        }
    };
    int first = 0;
    drive(first);
    CHECK(first == 2 && r.state == State::Paused && r.note == kFiringWhy);
    CHECK(r.approachPauseKnown && r.approachPauseX == 20.0f);
    CHECK(Resume(r));
    int second = 0;
    drive(second);
    CHECK(second >= 1 && second <= 2 && r.state == State::Paused);
}

int main()
{
    TestRunGossipStepOfReadsTheCurrentStep();
    TestRunArmsAMoveTriggerUntilItFired();
    TestPatrollerOutsideTheRadiusIsWaitedForThenPulled();
    TestPatrollerWaitIsBounded();
    TestPatrollerGiveUpIsRunWide();
    TestPatrollerOwnedByALaterStepIsLeftToIt();
    TestNoPatrollerWaitWithoutALivingHostilePatroller();
    TestBigPackStandWalkWaitsForTheParty();
    TestBigPackGateHoldsOnAJoinerRestTick();
    TestBigPackGateCountsTheStandPoint();
    TestBigPackGateGivesUpAtTheGatherCeiling();
    TestSmallPackStandWalkIsNotGated();
    TestBigPackBoundaryIsEight();
    TestHopUnitThatNeverCameIsNotPulledAgain();
    TestStepUnitsNeverEnterTheRunWideSet();
    TestHopNeverCameWindowAndLateAttacker();
    TestHopPullWithoutAShotSendsNoOrder();
    TestGoneChestWithFlagIsDone();
    TestGoneChestWithoutFlagPauses();
    TestGoneIsDoneOnlyForAGoneObject();
    TestWaitObjectGoneAfterSeenIsDone();
    TestWaitObjectUnreadableMissIsNotGone();
    TestWaitObjectOneMissThenFoundIsNotGone();
    TestWaitObjectSightingsAreKeyedByTarget();
    TestWaitObjectSightingDuringWalkBackIsIgnored();
    TestWalkBackTickResetsGoneMisses();
    TestWaitObjectGoneWinsOverAStaleFight();
    TestWaitObjectNeverSeenWaitsOutItsTimeout();
    TestWaitObjectGoneWithoutFlagPauses();
    TestWaitObjectSightSurvivesAPause();
    TestHopRechecksTheLeadOnTheFoot();
    TestDriftWithinTheRadiusIsNoProof();
    TestProofIsRetakenAfterAFightAndAResume();
    TestBackPointStopsAtADirectStep();
    TestBrokenHopPausesAndSkippedHopRowHasNoDirectOrder();
    TestWalkBackReplaysTheExactPoint();
    TestExactSerpentisFootIsNotSkipped();
    TestExactNeverProvedPauses();
    TestExactReissuesTheMoveAndIgnoresTheStallRule();
    TestDirectFromExactPointWaitsForTheProof();
    TestRunTimeLimitAndStopNote();
    TestStartFromParsing();
    TestStartFromResolve();
    TestStartInsideEventGoesBack();
    TestPartialStartGuard();
    TestMove();
    TestWalkStuckIsBounded();
    TestShortHopArrivesWithoutTheRetryWait();
    TestShortDirectHopKeepsTheLongWait();
    TestShortHopThatMovesIsNotCut();
    TestStalledNearPointCountsAsArrived();
    TestStandOnALedgeCountsByGroundDistance();
    TestStalledLeadInFollowRangeCountsAsArrived();
    TestStuckPauseNamesTheLeadsWaterState();
    TestStalledWalkWithAFailingMoveTriesDirectThenADryStandIn();
    TestPullLoopOnOneTargetPauses();
    TestOutOfReachPackIsSkippedNotCleared();
    TestMissingItemPausesOnce();
    TestResumeRegroups();
    TestResumeWithLeadDead();
    TestFarMoveWalksBackInHops();
    TestDirectStepGathersFirst();
    TestDirectGateAsksTheGluesWithLeadTest();
    TestDirectGatherCeilingSummonsAgain();
    TestRefusedDirectStepSummonsAgainAndPausesLate();
    TestRefusedDirectStepNeedsTwoSummonsBeforeThePause();
    TestDirectSummonAfterADeathIsANewCase();
    TestCombatLockedDirectStragglerPausesOnTime();
    TestWalkBackReadsTheDirectStepPastAPull();
    TestDirectOrderWaitsForTheRead();
    TestDirectRefusalClockStartsOverAtTheNextPoint();
    TestSkipWhileRunning();
    TestUnreachableMoveDetours();
    TestPullGatherAndFight();
    TestLeadHoldsStayWhileResting();
    TestRestFightLeavesTheLeadOffStay();
    TestRestFightFocusLeavesTheLeadOffStay();
    TestRestStaleCombatStillHoldsTheLead();
    TestRunBackAllowedInRest();
    TestRunBackListsRestFightMembers();
    TestSentinelRestFightReplay();
    TestUnseenPackIsApproachedNotCleared();
    TestFarPackHopIsNotUndoneByTheGatherBack();
    TestApproachHopCapCountsHopsWithoutProgress();
    TestApproachCapTriesAStandNearThePackBeforeThePause();
    TestApproachStandIsASafeRingSpot();
    TestApproachStandWalkIsAHopAndRewindsForgetIt();
    TestUnreachablePackIsDeferredThenPulledOnTheWay();
    TestDeferredPackNeverReachedIsSkippedAtTheLegEnd();
    TestEveryRetryExitThatLeavesThePackAliveGivesItUp();
    TestStandThatDidNotBringThePackInSightDefers();
    TestGatingPullsAndBossesAreNeverDeferred();
    TestStandSearchDuringARetryCountsOnlyLivePacks();
    TestResumeOfTheApproachPauseSearchesInPlaceThenDefers();
    TestNoPathTowardThePackSearchesAStandThenDefers();
    TestNoPathTowardABossPausesAfterTheStandSearch();
    TestStuckApproachHopWalkSearchesAStandThenDefers();
    TestStuckStandWalkAfterTheStandWasTriedDefers();
    TestStuckStandWalkOnABossStepPausesAfterTheStandWasTried();
    TestStuckWalkDuringAWalkBackStaysAPlainPause();
    TestStuckWalkThatIsNotTheHopStillPauses();
    TestRefusedFiringPositionSearchesAStandThenDefers();
    TestRefusedFiringPositionPullsFromTheStandThenDefers();
    TestRefusedFiringPositionAfterNoFiringStand();
    TestRefusedFiringPositionOfABossPausesAfterTheStandSearch();
    TestRefusedFiringPositionOnARetryIsGivenUp();
    TestRefusedWalkBackHopPullOnPullStepPauses();
    TestRefusedWalkBackReplayPullPauses();
    TestRefusedFiringPositionBeforeEventHoldPauses();
    TestStandFoundThenSeenPullsWithoutWalking();
    TestOtherPullbackRefusalsStillPause();
    TestRefusedFiringPositionNeverLoops();
    TestMoveRefusedTooFarGoesInLegsAlongThePath();
    TestTooFarLegsWalkALongWindingWay();
    TestTooFarLegBackToAnEarlierLegIsARepeat();
    TestResumeWalksBackNoFartherThanTheLastClearedFight();
    TestItemCarrierIsPickedFromTheStepsOwnInputs();
    TestUseItemPausesOnSplitInputs();
    TestLootStepIsDoneWhenAnyPartyBotHasTheItem();
    TestLootRefusalTriesTheTurnBotThenEachBot();
    TestLootFfaOrderSequence();
    TestLootFfaRestoresOnAnErrAnswerAndNeverTwice();
    TestLootFfaStaysLiveThroughAPause();
    TestLootFfaRestoresWhenTheTaskEndsUnanswered();
    TestLootFfaRefusedFallsBackToTheOldPath();
    TestLootFfaConditions();
    TestLootFfaRegroupBetweenAckAndRestoreRestores();
    TestLootFfaRestoreRefusedRetriesThenPauses();
    TestLootFfaFlagEndsOnlyWhenTheMethodIsNotFfa();
    TestLootFfaOrderedWaitIsBounded();
    TestLootFfaOrderedThenFallenRegroupRestores();
    TestLootFfaKeyOffIsTheOldPath();
    TestOutstandingCarrierKeepsTheOrdersBot();
    TestRetiredUseItemOrderGoesOutAgain();
    TestWaitObjectDoneWinsOverAStaleFightTimer();
    TestPullThatStartsNoFightIsPassed();
    TestAmbushWalksThrough();
    TestBossKillOrderAndTeamGate();
    TestHopPullsNearUnitsFirst();
    TestHopNeverPullsAnEventUnit();
    TestWipeAndLeadDeathPause();
    TestFewDeadReviveBeforePull();
    TestFewDeadRuleSkipsEvents();
    TestRegroupFightsFirst();
    TestRegroupWaitsForPartyRez();
    TestRegroupRevivePauses();
    TestMealWaitBeforePull();
    TestWalkAfterRestIsSentAgain();
    TestFightCeiling();
    TestTaskSteps();
    TestOpenWithoutKeyAndTaskError();
    TestOpenProofDoneWhenTheProofShows();
    TestOpenProofWaitsForTheTaskToAnswer();
    TestOpenProofAckWithoutProofPauses();
    TestOpenProofLateProofIsDone();
    TestOpenProofNotReadIsNoProof();
    TestOpenProofAlreadyUpSkipsTheUse();
    TestOpenProofResendsAnInCombatAnswer();
    TestProofStepIsTheCurrentOpenProofOnly();
    TestOpenProofWalkBackStepDoesNotFinishIt();
    TestOpenProofCreatureName();
    TestPlainOpenStillPausesWithoutChange();
    TestTaskResendNotForOtherFailures();
    TestTaskResendAfterInterruptedCast();
    TestTaskResendInCombatAndBusy();
    TestTaskResendCounterResetsPerStep();
    TestTaskResendKeepsTheLootBot();
    TestWaitObjectTimeoutIsBounded();
    TestUnsupportedPausesAndSkip();
    TestCommanderSummonKeepsTheRunGoing();
    TestPlayerOrderAndRefusalPause();
    TestStepIdleCeiling();
    TestWaitClearCountsEveryHostileInRadius();
    TestWaitClearTimeoutDropsOffFloorBystanders();
    TestWaitClearEngagedPausesAtTimeout();
    TestTeamFlags();
    TestOtherTeamBossIsLookedAtAndFoughtUnlessFriendly();
    TestRefusedGatherIsRetriedOnceThenLeftOut();
    TestWipeRegroupsAndWalksBack();
    TestThreeDeadRegroupAfterTheFight();
    TestGatherSummonsAStalledStraggler();
    TestStillBotInsideSummonRangeIsSummoned();
    TestStuckBotInCombatIsNotSummoned();
    TestStuckBotWithAttackersIsNotSummoned();
    TestMovingBotThatDoesNotGainIsSummoned();
    TestComingStragglerIsWaitedFor();
    TestComingStragglerIsWaitedForAtDirectGathers();
    TestComingStragglerIsSummonedAfterTheComingLimit();
    TestStillBotInAFightIsNotWaitedFor();
    TestFarBusyBotIsNeverSummonedAtTheCeiling();
    TestRegroupSummonsAStuckFarBotEarly();
    TestStuckSummonCountsFromTheGather();
    TestWalkBackRefusalKinds();
    TestBossDownNeedsASeenDeath();
    TestBossAwayWaitIsTheRowsTimeout();
    TestSkipDropsTheRefusedPullTarget();
    TestBossKilledAwayFromHisSpot();
    TestWalkBackFarPointIsApproached();
    TestRefusedPullOnlyForInvalidTarget();
    TestWipeWithTheLeadOnTheGraveyardMap();
    TestNearWipeLatchHoldsThroughTheWipe();
    TestLeadDiesFirstThenTheWipeRegroups();
    TestFightCeilingWithFourDeadRegroups();
    TestPastCeilingRegroupRearmsTheRescue();
    TestStragglerOnAnotherFloorIsSummoned();
    TestResumeFromTheEntranceWalksBack();
    TestGatherGoesBackToTheAnchorAfterARidgeChase();
    TestDirectWalkBackAvoidsUnclearedPacks();
    TestStalledAnchorAndSkippedPacks();
    TestHopWaiverEndsWhenCombatStartsDuringCrowdControl();
    TestPullCrowdControlsTheHealer();
    TestBossStepNeverCrowdControlsItsAdds();
    TestNoCrowdControlWithoutAMageOrPlanList();
    TestCrowdControlMustLand();
    TestKillFirstIsThisFightsOnly();
    TestAuraOnEngagedKeepsThePullBound();
    TestHailarRitualistsReplayR19_19z37();
    TestChannellerQualificationOnABossStep();
    TestKillFirstFarIsNotFocusedWipe1Replay();
    TestKillFirstBoundDoesNotFlipFlop();
    TestFocusSkullComesOffWhenTheFightEnds();
    TestFocusSkullComesOffAtResetStep();
    TestPauseDropsTheFocusSkull();
    TestFocusClearTakesItsOwnSkullWhoeverSetIt();
    TestRefusedFocusTakesItsSkullOff();
    TestRefusedNewFocusClearsBothSkulls();
    TestANewFocusMovesTheSkullWithNoClear();
    TestACrowdControlledFocusTakesItsSkullOff();
    TestKillFirstKeepsItsUnitAgainstAnEqualRank();
    TestGuardLeavesACasterAtItsCamp();
    TestGuardTakesACasterWithNoCampByIt();
    TestGuardTakesACampCasterWithinAStep();
    TestHoldGuardStillTakesACasterInTheHold();
    TestHeldFocusEndsOnlyWhenItsUnitIsGone();
    TestKillFirstOnABossStepIsNotBounded();
    TestGuardStopsWatchingWhenTheFocusEnds();
    TestGuardWatchesAfterAFocusWipe2Replay();
    TestGuardAfterAFocusIsRateBounded();
    TestRefusedFocusIsPickedAgainDeadminesS118();
    TestCcWindowCountsTheMagesFightTime();
    TestPriestShacklesUndeadWithoutAMage();
    TestWarlockBanishesDemonsAndElementals();
    TestMagePolymorphsHumanoidsFirst();
    TestTwoCcBotsTakeTwoMobsNeverOne();
    TestImmuneTypeIsNeverPicked();
    TestCcPriorityOrderComesBeforeTheSpell();
    TestRogueSapsBeforeThePull();
    TestSapThatNeverLandsLetsThePullGo();
    TestCcResendUsesItsOwnMark();
    TestSapTimeoutClearsTheRogueBeforeTheGather();
    TestSapTargetGoneForATickIsCancelled();
    TestCcMarksOnlyFreeOrTheRunsOwn();
    TestKillFirstUnitIsNeverCrowdControlled();
    TestWarlockOnlyWhereBanishCanLand();
    TestNoSapWithoutALiveMaster();
    TestRefusedCcClearKeepsTheIconForTheRunsEnd();
    TestTankGuardsThePartyFromItsStand();
    TestTankGuardReturnsToItsOwnSpot();
    TestNoWalkBackAfterAWonFight();
    TestWalkBackAfterAWonFightOnTheOldKey();
    TestWalkBackKeptWhereItMatters();
    TestWalkBackKeptWhenTheStepIsNotOver();
    TestWalkBackKeptOnAnAmbushNotWalkedThrough();
    TestGuardWalkBackJudgedOnTheReplayedPoint();
    TestTankGuardPull100Replay();
    TestTankGuardOneMaulerAndTheCapAfterARefill();
    TestTankGuardLimits();
    TestTankGuardPicksUpMobsOnPets();
    TestEngageT1();
    TestEngageT2();
    TestEngageT3();
    TestEngageT3b();
    TestEngageT4();
    TestEngageT5();
    TestEngageT6();
    TestEngageT7();
    TestEngageT8();
    TestEngageT9();
    TestEngageT10();
    TestEngageT11();
    TestGuardRefillDeadminesS118();
    TestGuardRefillDeadminesS90();
    TestGuardRefillDeadminesS125();
    TestGuardRefillIsBounded();
    TestGuardRefillKeepsSpacingAndWatch();
    TestEngageReplayRfcOggleflint();
    TestEngageReplayUldamanStep85();
    TestEmptyPlan();
    TestStuckStragglerThatMovedIsSummoned();
    TestApproachHopSummonsBeforePause();
    TestLeadOnlyDeathRecovers();
    TestDeathWhileWalkingBlocksTheStep();
    TestLeadDeathBlocks();
    TestWaitRuleEventException();
    TestNoPullWithADeadMember();
    TestRegroupWaitsForTheFight();
    TestDeathDuringRegroup();
    TestLeadDeadAfterTheFightHolds();
    TestNearWipeHoldsTheLivingAtTheFightSpot();
    TestLeadInOtherPartPausesWithTheTrueReason();
    TestRunHoldsItsDeadUntilItEnds();
    TestOtherPartResumeIsRefusedWhileTheLeadIsAway();
    TestOtherPartResumeRefusalOnlyForThatPause();
    TestBackDoorNearThePlanIsNotAnotherPart();
    TestLeadRevivedAwayInOtherPartPauses();
    TestLeadInOtherPartAfterAWipeIsRescued();
    TestJoinedPlayerCancelsTheOtherPartRescue();
    TestLeadInOtherPartRescueBounds();
    TestLeadRevivedAwayInOtherPartIsRescued();
    TestDireMaulWestR40SecondRevivalWaitsForTheDead();
    TestOtherPartDeadWaitIsBoundedAndResumeRescues();
    TestLeadRevivedAwayWithADeadBotRegroupsThenRescues();
    TestOtherPartRescueCapThenResume();
    TestOtherPartBusyResumesIntoTheRescue();
    TestIslandEscapeInOtherPartRescues();
    TestNoSummonTowardALeadInAnotherPart();
    TestNoIslandHopForALeadInAnotherPart();
    TestScarletLibraryR20Replay();
    TestRevivedBotSummonedBeforeWalking();
    TestNoReviveSummonInCombat();
    TestOneReviveSummonPerRevive();
    TestFailedReviveSummonWalks();
    TestRegroupGatherCapScales();
    TestAggroEstimateOffChangesNothing();
    TestAggroEstimateHoldsOnceForARest();
    TestJoinerRestsAreBudgetedAndNotIdleTime();
    TestJoinerRestSlotsAreSpentByRealRestsOnly();
    TestRezHoldOffIsUnchanged();
    TestRezHoldOn();
    TestRezHoldClocks();
    TestRezHoldNudge();
    TestRezNudgeUnreachable();
    TestAggroEstimateAndRezHoldDoNotCrossTheirResets();
    TestBothKeysOffChangeNothing();
    TestPullNoteCarriesTheJoinerCount();
    TestPullShootsFromStand();
    TestPullWaitsForStillTank();
    TestPullWalksToStandOutOfRange();
    TestPullWalksToStandNoLos();
    TestFiringStandOnALedgeCountsByGroundDistance();
    TestPullStandTriesBounded();
    TestPullNoShotDataKeepsOldOrder();
    TestPickPackPullPrefersShootable();
    TestStandTicksAreNotFruitlessPulls();
    TestShotStandOnPath();
    TestStrandedLeadIsTeleportedToTheLastReachedPoint();
    TestPullStandNeedsMoreRangeThanTheMargin();
    TestStandRequestIsReadOnce();
    TestStandWalkStuckOrdersFromHere();
    TestStandWalkIsDrivenEveryTick();
    TestStuckStandChecksTheWayBackFirst();
    TestPickSwitchChecksTheWayBack();
    TestGossipOrderOnceThenAck();
    TestGossipErrPauses();
    TestGossipNotAllowedNamesTheConfigKey();
    TestGossipDeadNpcPausesPlainly();
    TestGossipNoNpcKeepsItsText();
    TestGossipDeadNpcTaskReply();
    TestGossipNoAnswerPauses();
    TestGossipWaitsForABusyLeadBot();
    TestGossipWalksToFarPointFirst();
    TestGossipStartsAnEvent();
    TestGossipAfterAHoldThenTheDoor();
    TestUnitsHoldWaitsForAFriendlyBossToTurn();
    TestGossipDeadIsDoneWithFlag();
    TestGossipDeadIsDoneOnlyForADeadNpc();
    TestUnitsHoldAllDeadOnEntryEndsAfterTenSeconds();
    TestBlyCrewDeadBeforeTheLegRunsUnattended();
    TestUnitsHoldAllDeadDoesNotEndEarly();
    TestAllDeadEndIsForAUnitsHoldOnly();
    TestHoldUnitSeenDoesNotLeakAcrossSteps();
    TestUseOnOneTargetedOrderThenAck();
    TestOptionalLegSkippedWithoutItem();
    TestOptionalLegRunsWithCarrier();
    TestOptionalLegWaitsOutCombat();
    TestSkippedLegNotReplayed();
    TestDoorHoldAsksPlayerToOpen();
    TestDoorHoldOpensAndResumes();
    TestBotKeysFlagOpensDoor();
    TestDoorHoldReleasesOnlyWhenOpen();
    TestDoorNotFoundIsNotClosed();
    TestDoorHoldResumeWhileOpen();
    TestDoorHoldResumeOutOfSightNamesIt();
    TestDoorHoldOnlyNearTheDoor();
    TestBotKeysFlagDoorThatNeverOpensHolds();
    TestBotKeysFlagWithoutCarrierHolds();
    TestSkipPastClosedDoorDecidesLeg();
    TestDoorHoldFlagOffHolds();
    TestUseOnPausesNamingPlayerCarrier();
    TestUseOnWithoutTheItemPauses();
    TestUseOnResendsOnceThenPauses();
    TestUseOnNoAnswerPauses();
    TestUseOnStartsAnEvent();
    TestUseOnAckNeedsProof();
    TestUseOnNoResendOnceTheActStarted();
    TestUseOnResetAfterAckDoesNotUseAgain();
    TestUseOnNamesADeadHolder();
    TestLeadOnIslandReading();
    TestWalkBackEscapesNavmeshIsland();
    TestForwardMoveEscapesNavmeshIsland();
    TestR16StandRefusalHopsOffIsland();
    TestR16StepMoveRefusalHopsOffIsland();
    TestR16RefusalAfterTheCapsPauses();
    TestNonPathRefusalUnchanged();
    TestBareUnreachableIsNoIslandSign();
    TestGuardReturnThroughTheGlue();
    TestR22RefusedMoveWalksToAStandInWithin25Yd();
    TestR22NoStandInSkipsTheMoveWithoutAPause();
    TestR22SecondRefusalOfTheStepSkips();
    TestR22PullAndDirectStepsKeepTheirRefusals();
    TestR22ThreeSkipsInARowPause();
    TestNoPathPauseResumeTriesTheSameMovesAgain();
    TestR22MoveBeforeAFightIsNotSkipped();
    TestR22RewindForgetsMoveRefusals();
    TestR22RealStep85ApproachStandPauses();
    TestR22StandInPathIsCapped();
    TestGoodPathClearsTheRefusalSign();
    TestForwardMoveIslandNeedsSecondSign();
    TestForwardMoveNoPathLedgeEscapes();
    TestDetourPointReachable();
    TestDetourCandidateLeadsOn();
    TestForwardEscapeNoSpotFallsBack();
    TestForwardEscapeAfterAFallFallsBack();
    TestForwardEscapeNeedsAFreshGoodSpot();
    TestForwardMoveProbeGateAndCache();
    TestIslandPausesAfterTwoEscapes();
    TestResumeEscapesFirst();
    TestWalkBackPointInSightOnIsland();
    TestApproachHopIslandAndZeroHop();
    TestIslandGapIsNotAGoodPath();
    TestEscapeWaitIsNotATry();
    TestEscapeSegmentCheck();
    TestIslandLeavesDirectStepsAlone();
    TestSingleIslandReadingChangesNothing();
    TestSecondReadingConfirmsTheIsland();
    TestWalkBackNoSpotPassesThePoint();
    TestIslandWalkBackIsRescued();
    TestIslandRescueNeedsTheNoSpotSpot();
    TestIslandWalkBackWithPlayerPassesThePoint();
    TestJoinedPlayerCancelsTheIslandRescue();
    TestStepEscapeCap();
    TestNoSpotWalkBackNeverSpins();
    TestSignBFromTheMainMeshIsBounded();
    TestWalkBackResendBound();
    TestCeilingPauseResumesByItself();
    TestLeadOffMapPauseResumesByItself();
    TestPlayerHoldPauseResumesByItself();
    TestUnattackableOnlyFightIsNoFight();
    TestUnattackableUnitIsNeverPulledOrPickedUp();
    TestBotHeldByUnattackableDropsCombat();
    TestEngageOnArrivalLeavesCcMarkToItsCcBot();
    TestEngageTakesCcMarkOfAGoneCcBot();
    TestGuardLeavesCcMarkToItsCcBot();
    TestGuardTakesCcMarkOfADeadCcBot();
    TestHoldEngageLeavesCcMarkToItsCcBot();
    TestCcLandingQueuesTheOrderDrop();
    TestSapLandingQueuesTheOrderDrop();
    TestCcStateResetClearsTheDrop();
    TestCombatDropNeverWhileHeldByAPlanBossOrHoldUnit();
    TestCombatDropKindChangeStartsTheGraceOver();
    TestLeadRevivedAwayRescueLeavesFightingFalse();
    TestFarHoldNeverDropsWhilePaused();
    TestCombatDropEmptyEngagedSetDropsNothing();
    TestFarHoldIsDroppedAwayFromThePlan();
    TestFarHoldNeverDropsAPlanBossOrHoldUnit();
    TestDireMaulWestR36FarCombatThenRescue();
    TestRegroupRescueClearsTheHold();
    TestGatherWaitsForABotHeldByUnattackable();
    TestPetHeldByUnattackableKeepsTheBotInTheFight();
    TestUnattackableGraceNeverLetsAPlanBossOrHoldUnitGo();
    TestOutsideBotFightIsNoPartyFight();
    TestOutsideBotDoesNotHoldTheRegroup();
    TestOutsideBotFightLetsTheWipeRegroup();
    TestFightOnThePlanMapStillCounts();
    TestRegroupQuietTickEndsTheFightClock();
    TestOutsideBotIsReturnedInABotOnlyRun();
    TestOutsideBotIsNotReturnedWhenGuarded();
    TestOutsideBotReturnsArePausedAfterThreeAStep();
    TestOutsideBotReturnsCountPerBot();
    TestOutsideBotPauseNamesTheBotThatNeededFour();
    TestOutsideBotPerBotCountsReset();
    TestOutsideBotNotInTheOpenWorldIsSkipped();
    TestOutsideBotComebackUnderOrderReportsAgain();
    TestOutsideBotComebackUnderStrandOrDeadLeadReportsAgain();
    TestOutsideBotNotReturnableNotedOncePerStep();
    TestOutsideBotNotReturnableNotedAgainOnNewStep();
    TestOutsideBotNotReturnableBusyWording();
    TestOutsideLeadBotIsNeverReturned();
    TestBossNotAtHisSpotIsSearchedForInTheInstance();
    TestPassedBossAtHisSpotPausesAtOnce();
    TestAliveSearchPauseSaysWhereTheLeadBotIs();
    TestSkipUnreachableBossAliveElsewhereEndsNotCleared();
    TestSkipUnreachableBossPassedBySpotEndsNotCleared();
    TestStandPullWaitsAtTheStandOutOfShot();
    TestStandPullPullsFromTheStandInShot();
    TestStandPullWaitsWhileOutOfSight();
    TestStandPullWalksBackToTheStand();
    TestStandPullCeilingSkipsWithTheSkipFlag();
    TestStandPullCeilingAccumulatesAcrossPauseAndResume();
    TestStandPullGateReadsShotWithTheKeyOff();
    TestStandPullNoRangedPullKeyOff();
    TestStandPullNoRangedPull();
    TestStandPullWithoutStandPullsAsBefore();
    TestStandPullStandInAnotherLegPullsAsBefore();
    TestStandPullGateOffWhileInCombat();
    TestStandPullShotLatchClearsOnGather();
    TestStandPullAuthAnchorReadThenOtherUnitHolds();
    TestStandPullAuthJoinerRestThenOtherUnitHolds();
    TestSkippedBossNamedInDoneNoteAliveElsewhere();
    TestSkippedBossNamedInDoneNotePassedBySpot();
    TestDoneNoteUnchangedWithNoSkippedBoss();
    TestSpawnEventsOffReadsEveryAssociation();
    TestUnitThatCannotDieNowPauses();
    TestSlowSteadyBossFightNeverPausesBeforeTheHardCap();
    TestFightWithNoHpLossPastTheCeilingPauses();
    TestPlanBossOnAMoveStepGetsTheBossCeiling();
    TestStragglerLadderWalksThenSummonsBeforeThePause();
    TestStragglerLadderClocks();
    TestStragglerWalkRefusedSummonsNext();
    TestStragglerLadderInTheRegroup();
    TestFailedSapWalksTheRogueBack();
    TestBotWalkRefusedOnADetourOrAPack();
    TestWalkerVerdict();
    TestBotWalkLeavesTheStartOutOfThePackChecks();
    TestStragglerLadderKeepsItsRungWhenTheBotCycles();
    TestTakeSpeedWords();
    TestCheckTestSpeedOrder();
    TestSpeedRisesOnlyAfterTheWalkSettles();
    TestSpeedWalkingRule();
    TestSpeedSettleNeedsAnUnbrokenWalk();
    TestSpeedCommanderJoinBlocksTheRun();
    TestSpeedRealPlayerBlocksTheRestOfTheRun();
    TestSpeedCappedOnTheTickThatReachesAPullStep();
    TestSpeedShowsInTheLines();
    TestSpeedChangeWaitsWhileSwimmingASpline();
    TestSpeedRestoreWaitIsBounded();
    TestSpeedLeashHoldsAFastWalkForAStraggler();
    TestSpeedLeashNeedsNoRealPlayerThisTick();
    TestSpeedLeashIgnoresOffMapBots();
    TestSpeedLeashRefusedHoldWalksOn();
    TestSpeedLeashNeedsSpeedWalking();
    TestSpeedLeashLeavesOtherRunsAlone();
    TestSpeedLeashSkipsDirectWaterFightsAndEvents();
    TestSpeedLeashHoldsBeforeAFreshWalk();
    TestSpeedLeashIsBoundedByTheGatherCap();
    TestSpeedLeashResumesWhenTheRateDrops();
    TestSpeedLeashResumesTheLegOnALegWalk();
    TestSpeedLeashClearedByANewStep();
    std::cout << "test_dungeon_run_policy: ok\n";
    return 0;
}
