// Standalone check for the run leader's time-sink fixes of 2026-10-23 (runtime/DungeonRunPolicy.h; the evidence is
// TortoiseBots-audit WASTED-TIME-2026-10-23.md sinks 2, 3 and 5 and RUN-LESSONS r26/r27 gnomeregan slot 4):
//   (A) the walk back after a wipe or a resume: never before the run's start step, a straight target out of every
//       live pack's reach, a target moved back when the straight path passes an uncleared pack, and the replay
//       leaving out a stretch of the route that comes back by the point just reached;
//   (B) the rest gate rests to what the next fight needs;
//   (C) after a won fight the next pack close by is pulled from where the tank stands;
//   (D) a walk whose lead bot moves (a spiral ramp) is not stuck.
//   g++ -std=c++17 -Wall -Wextra tools/test_run_time_sinks_policy.cpp -o /tmp/t && /tmp/t
#include "../runtime/DungeonRunPolicy.h"

#include <cmath>
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

static PlanStep Move(float x, float y, float z = 0.0f)
{
    PlanStep s;
    s.kind = PlanStepKind::Move;
    s.x = x;
    s.y = y;
    s.z = z;
    ai::NormalizePlanStep(s);
    return s;
}

static PlanStep Direct(float x, float y, float z = 0.0f)
{
    PlanStep s = Move(x, y, z);
    s.kind = PlanStepKind::MoveDirect;
    return s;
}

static PlanStep Pull(float x, float y, std::uint32_t entry, float radius = 15.0f, float z = 0.0f)
{
    PlanStep s;
    s.kind = PlanStepKind::Pull;
    s.x = x;
    s.y = y;
    s.z = z;
    s.radius = radius;
    s.entries = {entry};
    ai::NormalizePlanStep(s);
    return s;
}

static Facts Party(float x, float y = 0.0f, float z = 0.0f)
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.leadX = x;
    f.leadY = y;
    f.leadZ = z;
    f.carrier = "Tanky";
    return f;
}

static bool ParseMove(const std::string& order, float& x, float& y, float& z)
{
    return order.compare(0, 5, "move ") == 0 && std::sscanf(order.c_str() + 5, "%f %f %f", &x, &y, &z) == 3;
}

// The glue's part of a walk back, on a flat world with no walls: the step facts for FactStep (seen within kSeeYd, an
// approach hop of kApproachHopYd along the straight line, the straight walk back's path checked against every live
// pack), and every `move` carried out at once. Returns the yards the lead bot walked until the walk back was over
// (or `ticks` ran out); `lowest` gets the lowest plan index a walk back point had.
struct WalkBackSim
{
    bool shortcutOk = true; // the glue's read of a replay shortcut's path (a flat world: direct)
    int teleports = 0;
    float walked = 0.0f;
    std::size_t lowest = static_cast<std::size_t>(-1);
    std::size_t points = 0; // walk back points sent (one `move` each, hops included)
    std::vector<std::string> notes;
};

// The glue's answer to the rescue's scan request: the point it asked for, with no live hostile there.
// StrandedTeleportTick sends nothing onto a point nobody read, and these tests drive the policy by hand.
static void AnswerRescueScan(Run& r, Facts& f)
{
    if (!r.rescueScanWanted)
        return;
    f.rescueRead = true;
    f.rescueReadX = r.rescueScanX;
    f.rescueReadY = r.rescueScanY;
    f.rescueReadZ = r.rescueScanZ;
    f.rescueReach.clear();
}

static WalkBackSim SimulateWalkBack(Run& r, Facts f, int ticks = 4000, bool shortcutOk = true)
{
    WalkBackSim out;
    out.shortcutOk = shortcutOk;
    bool started = false;
    for (int i = 0; i < ticks && r.state == State::Running; ++i)
    {
        const PlanStep* p = FactStep(r);
        if (!p)
            break;
        float const d = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p->x, p->y, p->z);
        f.seen = d <= kSeeYd;
        f.approachRead = true;
        f.approachKnown = true;
        float const t = d > kApproachHopYd ? kApproachHopYd / d : 1.0f;
        f.approachX = f.leadX + (p->x - f.leadX) * t;
        f.approachY = f.leadY + (p->y - f.leadY) * t;
        f.approachZ = f.leadZ + (p->z - f.leadZ) * t;
        f.directPathCrossesPack = false;
        if (r.walkBack && r.walkBackDirect && !f.seen)
        {
            std::vector<PathPoint> path(2);
            path[0].x = f.leadX;
            path[0].y = f.leadY;
            path[0].z = f.leadZ;
            path[1].x = p->x;
            path[1].y = p->y;
            path[1].z = p->z;
            f.directPathCrossesPack = PathCrossesUnclearedPack(*r.plan, LiveFrom(r, true), path, f.team,
                UnclearedForPath(r));
        }
        f.shortcutRead = r.shortcutWanted != static_cast<std::size_t>(-1);
        f.shortcutFor = r.shortcutWanted;
        f.shortcutOk = out.shortcutOk;
        AnswerRescueScan(r, f);
        std::string const order = Tick(r, f);
        float tx = 0, ty = 0, tz = 0, fx = 0, fy = 0, fz = 0;
        if (std::sscanf(order.c_str(), "rescue from %f %f %f to %f %f %f", &fx, &fy, &fz, &tx, &ty, &tz) == 6)
        {
            ++out.teleports;
            f.leadX = tx;
            f.leadY = ty;
            f.leadZ = tz;
        }
        if (r.walkBack)
        {
            started = true;
            out.lowest = std::min(out.lowest, r.walkBackIdx);
        }
        if (out.notes.empty() || out.notes.back() != r.note)
            out.notes.push_back(r.note);
        float x = 0, y = 0, z = 0;
        if (ParseMove(order, x, y, z))
        {
            out.walked += ai::PlanDistance(f.leadX, f.leadY, f.leadZ, x, y, z);
            f.leadX = x;
            f.leadY = y;
            f.leadZ = z;
            if (r.walkBack)
                ++out.points;
        }
        if (started && !r.walkBack)
            break;
    }
    return out;
}

static bool NoteSeen(const WalkBackSim& s, const std::string& part)
{
    for (const std::string& n : s.notes)
        if (n.find(part) != std::string::npos)
            return true;
    return false;
}

static std::size_t IndexOf(const DungeonPlan& plan, float x, float y)
{
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
        if (plan.steps[i].kind == PlanStepKind::Move && plan.steps[i].x == x && plan.steps[i].y == y)
            return i;
    return static_cast<std::size_t>(-1);
}

// A Gnomeregan-shaped plan (gnomeregan 647 steps, RUN-LESSONS r26 slot 4): the entrance corridor east, a dead-end side
// leg south and back up the same corridor (its pulls at the far end), the main route on east, and the last pull's
// stand right beside its pack (the pull that wiped the party). The route folds back on itself at the side leg.
static DungeonPlan GnomereganShape(std::size_t& wipeStep, std::size_t& sideLegEnd)
{
    DungeonPlan plan;
    plan.name = "gnome-shape";
    for (int i = 0; i <= 30; ++i)
        plan.steps.push_back(Move(10.0f * i, 0.0f)); // corridor 0 -> 300
    for (int i = 1; i <= 30; ++i)
        plan.steps.push_back(Move(300.0f, -10.0f * i)); // side leg down to 300 -300
    plan.steps.push_back(Pull(300.0f, -330.0f, 7001));
    plan.steps.push_back(Pull(310.0f, -340.0f, 7002));
    for (int i = 29; i >= 0; --i)
        plan.steps.push_back(Move(303.0f, -10.0f * i)); // back up the same corridor, 3 yd east of it
    sideLegEnd = plan.steps.size() - 1;
    for (int i = 1; i <= 30; ++i)
    {
        plan.steps.push_back(Move(300.0f + 10.0f * i, 0.0f)); // on east, 300 -> 600
        if (i % 10 == 0)
            plan.steps.push_back(Pull(300.0f + 10.0f * i, 40.0f, 7100 + i)); // packs 40 yd off the corridor
    }
    plan.steps.push_back(Move(608.0f, 0.0f)); // the stand beside the pack that wiped the party
    plan.steps.push_back(Pull(620.0f, 0.0f, 7200));
    wipeStep = plan.steps.size() - 1;
    plan.steps.push_back(Move(700.0f, 0.0f));
    return plan;
}

// The yards a replay of every move point from `from` to the step walks (the old rule: ReplayWalkBack from the point
// nearest the lead bot, each point in turn).
static float RouteYards(const DungeonPlan& plan, std::size_t from, std::size_t to, float fx, float fy)
{
    float yd = 0.0f;
    float x = fx, y = fy, z = 0.0f;
    for (std::size_t i = from; i < to; ++i)
    {
        const PlanStep& p = plan.steps[i];
        if (!IsMovePoint(p))
            continue;
        yd += ai::PlanDistance(x, y, z, p.x, p.y, p.z);
        x = p.x;
        y = p.y;
        z = p.z;
    }
    return yd;
}

// r26 slot 4: the last move point before the wiped step is the stand beside its pack, so a straight walk back to it
// always "passed an uncleared pack" and the replay walked every point from the entrance. Now the straight target is
// the latest point out of every live pack's reach, and the yards walked shrink to the straight line plus the last
// few points.
static void TestWipeWalkBackOnAGnomereganShapedPlan()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    Run r = Start(&plan, wipe);
    Facts f = Party(0.0f, 0.0f); // revived at the entrance
    StartWalkBack(r, f);
    CHECK(r.walkBack && r.walkBackDirect);
    // 590 0: the stand at 608 0 and the point at 600 0 lie within the wiping pack's radius 15 + 10 of 620 0.
    CHECK(r.walkBackStart == IndexOf(plan, 590.0f, 0.0f));
    CHECK(r.note.find("lies by an uncleared pack: straight to step") != std::string::npos);
    WalkBackSim const sim = SimulateWalkBack(r, f);
    CHECK(r.state == State::Running && !r.walkBack && r.step == wipe);
    float const oldYd = RouteYards(plan, 0, wipe, 0.0f, 0.0f);
    std::cout << "gnomeregan-shaped wipe walk back: old replay " << oldYd << " yd (" << oldYd / 7.0f / 60.0f
              << " min at 7 yd/s), new " << sim.walked << " yd (" << sim.walked / 7.0f / 60.0f << " min)\n";
    CHECK(oldYd > 1150.0f);
    CHECK(sim.walked < 650.0f); // the straight 600 yd and the stand
}

// The same plan when the straight way is no good (a pack the party skipped lies on it, notCleared): the replay of
// the plan's points leaves out the side leg that comes back by its own start (RouteShortcut), so it walks the corridor
// once, not down and up again.
static void TestReplayLeavesOutTheFoldedSideLeg()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    // A pack the run left uncleared halfway along the main corridor: the straight line from the entrance passes it.
    plan.steps.insert(plan.steps.begin() + 15, Pull(150.0f, 5.0f, 7300));
    ++wipe;
    ++side;
    Run r = Start(&plan, wipe);
    r.notCleared.insert(15);
    Facts f = Party(0.0f, 0.0f);
    f.walkBackRescue = false; // the replay itself (ReplayRescue off)
    StartWalkBack(r, f);
    WalkBackSim const sim = SimulateWalkBack(r, f);
    CHECK(r.state == State::Running && !r.walkBack && r.step == wipe);
    CHECK(NoteSeen(sim, "replaying the plan's points") || NoteSeen(sim, "left out"));
    float const oldYd = RouteYards(plan, 0, wipe, 0.0f, 0.0f);
    std::cout << "gnomeregan-shaped replay with the side leg: old " << oldYd << " yd, new " << sim.walked << " yd\n";
    CHECK(sim.walked < oldYd - 500.0f); // the 600 yd down and up the side leg are left out
}

// RUN-LESSONS r27 gnomeregan slot 4: a run placed at step 543 wiped at 615 and the walk back replayed from step 53.
// A walk back never goes to a point before the run's start step (Run::startStep, the glue sets it next to Start).
static void TestPlacedRunNeverWalksBackBeforeItsStartStep()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    std::size_t const start = side + 1; // placed at the main route's first point
    Run r = Start(&plan, wipe);
    r.startStep = start;
    r.notCleared.insert(wipe - 3); // even with a skipped pack that sends it into the replay
    Facts f = Party(0.0f, 0.0f);
    CHECK(ReplayFloor(r) == start);
    StartWalkBack(r, f);
    CHECK(r.walkBack && r.walkBackIdx >= start);
    // Up behind its start step (the entrance): teleported at once to the safe point by the wipe (590 0: 600 0 and the
    // stand 608 0 lie within the wiping pack's reach), never before the start step (Hateforge r32 slot E).
    CHECK(r.strandDue && r.walkBackIdx == IndexOf(plan, 590.0f, 0.0f) &&
        r.note.find("is up behind the run's start step") != std::string::npos);
    WalkBackSim const sim = SimulateWalkBack(r, f);
    CHECK(sim.lowest >= start && sim.teleports == 1);
    // AiPlayerbot.DungeonRunWalkBackRescue 0: no teleport, the walk from there (never before the start step).
    Run walk = Start(&plan, wipe);
    walk.startStep = start;
    Facts nf = f;
    nf.walkBackRescue = false;
    StartWalkBack(walk, nf);
    CHECK(!walk.strandDue);
    WalkBackSim const walked = SimulateWalkBack(walk, nf);
    CHECK(walked.lowest >= start && walked.teleports == 0);
    // Revived among the run's own points (within kSeeYd of one): no teleport.
    Run near = Start(&plan, wipe);
    near.startStep = start;
    Facts nearF = Party(303.0f, -5.0f); // the side leg's way back is nearest (before the start), step start 8.6 yd off
    StartWalkBack(near, nearF);
    CHECK(!near.strandDue);
    CHECK(r.state == State::Running && r.step == wipe);
    // The pre-start steps count as uncleared for the straight path (UnclearedForPath), the run's later ones as before.
    std::unordered_set<std::size_t> const uncleared = UnclearedForPath(r);
    CHECK(uncleared.count(0) && uncleared.count(start - 1) && !uncleared.count(start) && uncleared.count(wipe - 3));
    // From the beginning (startStep 0) the floor is the old one.
    Run from0 = Start(&plan, wipe);
    from0.notCleared.insert(wipe - 3);
    CHECK(ReplayFloor(from0) == 0 && UnclearedForPath(from0).size() == 1);
    // Placed at the very step that wiped: no move point of the run before it, so no walk back at all (the step's own
    // approach goes out from the entrance), never one to a point before it.
    Run at = Start(&plan, wipe);
    at.startStep = wipe;
    StartWalkBack(at, Party(0.0f, 0.0f));
    CHECK(!at.walkBack);
}

// The straight path that passes an uncleared pack: its target moves back to a clear point at least kStraightBackoffYd
// before it, kStraightBackoffs times; then the replay as before.
static void TestStraightTargetBacksOffBeforeTheReplay()
{
    DungeonPlan plan;
    for (int i = 0; i <= 20; ++i)
        plan.steps.push_back(Move(20.0f * i, 0.0f)); // 0 .. 400
    plan.steps.push_back(Pull(420.0f, 100.0f, 9));
    Run r = Start(&plan, plan.steps.size() - 1);
    Facts f = Party(-300.0f, 0.0f);
    f.seen = false;
    f.walkBackRescue = false; // the replay itself (ReplayRescue off)
    StartWalkBack(r, f);
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackStart == 20);
    f.approachKnown = true;
    f.approachRead = true;
    f.approachX = -260.0f;
    f.directPathCrossesPack = true;
    Tick(r, f);
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackStart == 18 && r.straightBackoffs == 1);
    CHECK(r.note.find("passes an uncleared pack: straight to step 19 instead (1 of 2)") != std::string::npos);
    Tick(r, f);
    CHECK(r.walkBackStart == 16 && r.straightBackoffs == 2);
    Tick(r, f);
    CHECK(!r.walkBackDirect && r.note.find("replaying the plan's points") != std::string::npos);
}

// RouteShortcut: the latest plain move point within kRouteLinkYd and kRouteLinkDzYd of the point reached; never past a
// direct step, an exact point or an uncleared pack; never onto another floor (Razorfen Kraul r26 slot 8: the z~46
// spawn side under the z~64 floor of Agathelos' pack).
static void TestRouteShortcutRules()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0), Move(0, -50), Move(0, -100), Move(0, -50), Move(4, 2), Move(50, 0)};
    Run r = Start(&plan, plan.steps.size());
    CHECK(RouteShortcut(r, 0) == 4);  // out and back: 4 yd from point 0
    CHECK(RouteShortcut(r, 1) == 3);  // and the way back's middle
    CHECK(RouteShortcut(r, 4) == 4);  // nothing comes back: the next point in turn
    // Only before the current step.
    Run early = Start(&plan, 3);
    CHECK(RouteShortcut(early, 0) == 0);
    // Not past a direct step.
    DungeonPlan hole = plan;
    hole.steps[2] = Direct(0, -100);
    Run h = Start(&hole, hole.steps.size());
    CHECK(RouteShortcut(h, 0) == 0 && RouteShortcut(h, 1) == 1);
    // Not past an uncleared pack.
    DungeonPlan pack = plan;
    pack.steps.insert(pack.steps.begin() + 2, Pull(0, -105, 5));
    Run p = Start(&pack, pack.steps.size());
    p.notCleared.insert(2);
    CHECK(RouteShortcut(p, 0) == 0);
    p.notCleared.clear();
    CHECK(RouteShortcut(p, 0) == 5);
    // Not onto another floor: 5 yd away on the ground, 18 yd below.
    DungeonPlan pit;
    pit.steps = {Move(0, 0, 64), Move(0, -40, 64), Move(30, -40, 46), Move(5, 0, 46), Move(60, 0, 46)};
    Run q = Start(&pit, pit.steps.size());
    CHECK(RouteShortcut(q, 0) == 0);
    CHECK(GroundDistance(0, 0, 5, 0) <= kRouteLinkYd);
}

// Razorfen Kraul r27 slot 8: the straight walk back's hops toward the den (z 63) climbed a high route to a dead-end
// ledge at z 104 and paused stuck, three times. A straight walk back whose navmesh path is partial (it ends short of
// the point: Facts::approachIncomplete) replays the plan's own points instead (the leg's route down from the z~46 side).
static void TestIncompletePathToTheTargetReplaysThePlansPoints()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0, 46), Move(40, 0, 46), Move(80, 10, 60), Move(100, 20, 66), Move(110, 40, 64),
        Pull(130, 40, 4519, 15.0f, 64)};
    std::size_t const step = plan.steps.size() - 1;
    auto begin = [&](bool incomplete, Run& r)
    {
        r = Start(&plan, step);
        Facts f = Party(-200, 0, 46); // revived at the entrance side
        f.walkBackRescue = false; // the replay itself (ReplayRescue off)
        StartWalkBack(r, f);
        CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 3); // 110 40 is within 25 yd of the pack
        f.seen = false;
        f.approachRead = true;
        f.approachKnown = true;
        f.approachX = -160;
        f.approachZ = 50;
        f.approachIncomplete = incomplete;
        return Tick(r, f);
    };
    Run r;
    std::string const hop = begin(false, r);
    CHECK(hop == "move -160.0 0.0 50.0" && r.walkBackDirect); // a complete path: the hop as before
    Run q;
    std::string const none = begin(true, q);
    CHECK(none.empty() && q.walkBack && !q.walkBackDirect && q.walkBackIdx == 0);
    CHECK(q.note == "walk back: no complete path to the point of step 4 from here: replaying the plan's points");
}

// The same run when the replay's walk also sticks short of its point (the hops end on the ledge): no pause, the
// stranded lead bot's rescue teleports it to the last point it reached on the plan, out of combat, once a step.
static void TestStuckWalkBackIsRescuedNotPaused()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0, 46), Move(40, 0, 46), Move(80, 10, 60), Pull(130, 40, 4519, 15.0f, 64)};
    Run r = Start(&plan, 3);
    Facts f = Party(10, 30, 104); // on the ledge, 30+ yd from every point, in sight of point 2
    f.seen = true;
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 2;
    std::string order;
    bool rescue = false;
    for (int i = 0; i < 60 && r.state == State::Running && !rescue; ++i)
    {
        AnswerRescueScan(r, f);
        order = Tick(r, f);
        rescue = order.compare(0, 7, "rescue ") == 0;
    }
    CHECK(r.state == State::Running && rescue);
    CHECK(order == "rescue from 10.0 30.0 104.0 to 80.0 10.0 60.0");
    CHECK(r.strandIdx == 2 && r.walkBackIdx == 2);
    // The rescue is done: the lead bot stands on the point and the walk back goes on from there.
    f.leadX = 80;
    f.leadY = 10;
    f.leadZ = 60;
    for (int i = 0; i < 4; ++i)
        Tick(r, f);
    CHECK(r.strandStep == 3 && r.state == State::Running);
    // Stuck again at the same step: the rescue is spent, the old pause.
    Run again = Start(&plan, 3);
    again.strandStep = 3;
    again.walkBack = true;
    again.walkBackIdx = again.walkBackStart = 2;
    Facts g = Party(10, 30, 104);
    g.seen = true;
    for (int i = 0; i < 60 && again.state == State::Running; ++i)
        Tick(again, g);
    CHECK(again.state == State::Paused && again.note.find("the lead bot is stuck") == 0);
}

// Stratholme r29 slot 4 / Maraudon r28b slot B: a full run's walk back after a wipe whose straight way failed (here it
// passes a pack the run left uncleared, after both backoffs) would replay the plan's points from the entrance (1,205 yd
// here). The rescue teleports the lead bot to the safe point on the way it came instead, once (ReplayRescue).
static void TestLongReplayIsRescuedInAFullRun()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    plan.steps.insert(plan.steps.begin() + 15, Pull(150.0f, 5.0f, 7300));
    ++wipe;
    Run r = Start(&plan, wipe);
    r.notCleared.insert(15);
    Facts f = Party(0.0f, 0.0f); // revived at the entrance
    StartWalkBack(r, f);
    CHECK(r.walkBack && r.walkBackDirect && !r.strandDue); // the straight walk first, as before
    WalkBackSim const sim = SimulateWalkBack(r, f);
    CHECK(r.state == State::Running && !r.walkBack && r.step == wipe);
    CHECK(sim.teleports == 1);
    CHECK(NoteSeen(sim, "passes an uncleared pack: the replay of the plan's points would walk"));
    CHECK(NoteSeen(sim, "is teleported to a safe point on the way it came (step " +
        std::to_string(IndexOf(plan, 590.0f, 0.0f) + 1) + ") out of combat"));
    CHECK(!NoteSeen(sim, "replaying the plan's points"));
    CHECK(sim.walked < 60.0f); // 590 0 to the step's own point, not the corridor
    std::cout << "full-run replay rescue: walked " << sim.walked << " yd after the teleport (old replay "
              << RouteYards(plan, 0, wipe, 0.0f, 0.0f) << " yd)\n";
}

// The bounds of ReplayRescue: a replay shorter than kReplayRescueYd is walked; a real player in the party, the rescue
// already used on this step (a resume) or AiPlayerbot.DungeonRunWalkBackRescue 0 keep the replay.
static void TestShortReplayAndPlayersKeepTheReplay()
{
    DungeonPlan plan;
    for (int i = 0; i <= 5; ++i)
        plan.steps.push_back(Move(20.0f * i, 0.0f)); // 0 .. 100
    plan.steps.push_back(Pull(120.0f, 100.0f, 9));
    std::size_t const step = plan.steps.size() - 1;
    // Three ticks with the straight way past a pack: two backoffs, then the replay or the rescue.
    auto run = [&](float leadX, Facts f, Run& r)
    {
        r = Start(&plan, step);
        f.leadX = leadX;
        f.seen = false;
        StartWalkBack(r, f);
        CHECK(r.walkBack && r.walkBackDirect && r.walkBackStart == 5);
        f.approachKnown = true;
        f.approachRead = true;
        f.approachX = leadX + 40.0f;
        f.directPathCrossesPack = true;
        for (int i = 0; i < 3; ++i)
            Tick(r, f);
    };
    Run a;
    run(-150.0f, Party(0.0f), a); // 150 yd to point 0, then 100 yd of points: 250 yd
    CHECK(!a.strandDue && a.walkBack && !a.walkBackDirect && a.walkBackIdx == 0);
    CHECK(a.note == "walk back: the straight way to the point of step 2 passes an uncleared pack: replaying the plan's points");
    Run b;
    run(-210.0f, Party(0.0f), b); // 310 yd
    CHECK(b.strandDue && b.strandIdx == 5 && b.walkBackIdx == 5);
    CHECK(b.note == "walk back: the straight way to the point of step 2 passes an uncleared pack: the replay of the "
        "plan's points would walk 310.0 yd: the lead bot is teleported to a safe point on the way it came (step 6) out of "
        "combat");
    // A real player in the party: he walks back with it, the replay as before.
    Facts withPlayer = Party(0.0f);
    PlayerFact p;
    p.name = "Hypeer";
    p.commander = true;
    withPlayer.players.push_back(p);
    Run c;
    run(-210.0f, withPlayer, c);
    CHECK(!c.strandDue && c.walkBack && !c.walkBackDirect && c.walkBackIdx == 0);
    // The rescue off.
    Facts off = Party(0.0f);
    off.walkBackRescue = false;
    Run d;
    run(-210.0f, off, d);
    CHECK(!d.strandDue && d.walkBack && d.walkBackIdx == 0);
    // Used on this step already (a resume keeps the latch): the replay.
    Run e = Start(&plan, step);
    e.strandStep = step;
    Facts g = Party(-210.0f);
    g.seen = false;
    StartWalkBack(e, g);
    g.approachKnown = true;
    g.approachRead = true;
    g.approachX = -170.0f;
    g.directPathCrossesPack = true;
    for (int i = 0; i < 3; ++i)
        Tick(e, g);
    CHECK(!e.strandDue && e.walkBack && e.walkBackIdx == 0);
}

// Round 1 (1): a Part B ReplayRescue refused kStrandTries times replays the plan's points (spec 2B) instead of hopping
// straight toward the refused target.
static void TestRefusedReplayRescueReplaysThePlansPoints()
{
    DungeonPlan plan;
    for (int i = 0; i <= 5; ++i)
        plan.steps.push_back(Move(20.0f * i, 0.0f)); // 0 .. 100
    plan.steps.push_back(Pull(120.0f, 100.0f, 9));
    std::size_t const step = plan.steps.size() - 1;
    Run r = Start(&plan, step);
    Facts f = Party(-210.0f);
    f.seen = false;
    StartWalkBack(r, f);
    f.approachKnown = true;
    f.approachRead = true;
    f.approachX = -170.0f;
    f.directPathCrossesPack = true;
    for (int i = 0; i < 3; ++i)
        Tick(r, f); // two backoffs, then the rescue (310 yd)
    CHECK(r.strandDue && r.strandReplay && r.strandIdx == 5 && r.walkBackIdx == 5);
    CHECK(r.note == "walk back: the straight way to the point of step 2 passes an uncleared pack: the replay of the "
        "plan's points would walk 310.0 yd: the lead bot is teleported to a safe point on the way it came (step 6) out of "
        "combat");
    AnswerRescueScan(r, f);
    std::string const rescue = Tick(r, f); // the send
    CHECK(rescue == "rescue from -210.0 0.0 0.0 to 100.0 0.0 0.0");
    for (std::uint32_t t = 1; t <= kStrandTries; ++t)
    {
        OrderRefused(r, rescue, "failed: the near teleport was refused");
        CHECK(r.state == State::Running && r.walkBack && r.strandTries == t);
        if (t < kStrandTries)
            CHECK(r.strandDue && r.strandReplay); // refused again: due once more
    }
    // Given up: the replay's first point (spec 2B), not a straight move toward the refused target.
    CHECK(r.walkBack && !r.walkBackDirect && r.walkBackIdx == 0 && r.walkBackStart == 0 && !r.strandReplay);
    CHECK(r.note == "the teleport of the stranded lead bot failed 3 times (refused: failed: the near teleport was "
        "refused): replaying the plan's points");
    CHECK(!r.strandOn && !r.strandDue && !r.strandReplay); // the replay answered the failure: no second send
}

// Round 3: a ReplayRescue armed bot-only, then a real player joins before the send: no rescue order goes out (no
// try spent); the walk back replays the plan's points, exactly as a refused rescue of that kind does. The
// bot-only control still teleports.
static void TestJoinedPlayerCancelsTheReplayRescue()
{
    DungeonPlan plan;
    for (int i = 0; i <= 5; ++i)
        plan.steps.push_back(Move(20.0f * i, 0.0f)); // 0 .. 100
    plan.steps.push_back(Pull(120.0f, 100.0f, 9));
    std::size_t const step = plan.steps.size() - 1;
    auto arm = [&](Run& r, Facts& f)
    {
        r = Start(&plan, step);
        f = Party(-210.0f);
        f.seen = false;
        StartWalkBack(r, f);
        f.approachKnown = true;
        f.approachRead = true;
        f.approachX = -170.0f;
        f.directPathCrossesPack = true;
        for (int i = 0; i < 3; ++i)
            Tick(r, f); // two backoffs, then the rescue (310 yd)
        CHECK(r.strandDue && r.strandReplay && r.strandBotOnly && r.strandIdx == 5 && r.walkBackIdx == 5);
    };
    Run r;
    Facts f = Party(0.0f);
    arm(r, f);
    PlayerFact p;
    p.name = "Hypeer";
    p.commander = true;
    f.players.push_back(p); // joins between the arm and the send
    CHECK(Tick(r, f).empty()); // no rescue order: he walks back with the party
    CHECK(r.state == State::Running && r.walkBack && !r.walkBackDirect && r.walkBackIdx == 0 &&
        r.walkBackStart == 0);
    CHECK(!r.strandDue && !r.strandBotOnly && !r.strandOn && r.strandTries == 0 && r.strandStep ==
        static_cast<std::size_t>(-1));
    CHECK(r.note == "walk back: a real player joined after the rescue was armed: replaying the plan's points");
    Run b;
    Facts g = Party(0.0f);
    arm(b, g); // control: nobody joined, the teleport still goes out
    AnswerRescueScan(b, g);
    CHECK(Tick(b, g) == "rescue from -210.0 0.0 0.0 to 100.0 0.0 0.0");
    CHECK(b.strandOn && !b.strandDue && b.strandTries == 0);
}

// Round 1 (3): ReplayRescue never teleports the lead bot onto its own spot: already within kArriveYd of the target,
// no rescue is due and the latch stays unused (a later real need still gets it). A MoveDirect hole before the target
// forces the replay to start at the first point (ReplayStartPoint with a hole), so the replay is 800 yd even with
// the lead bot on the target: without the guard it would rescue onto its own spot.
static void TestReplayRescueNeedsDistanceToTheTarget()
{
    DungeonPlan plan;
    plan.steps.push_back(Move(0.0f, 0.0f));     // 0
    plan.steps.push_back(Move(100.0f, 0.0f));   // 1
    PlanStep hole = Move(200.0f, 0.0f);
    hole.kind = PlanStepKind::MoveDirect;
    plan.steps.push_back(hole);                 // 2: the hole
    plan.steps.push_back(Move(300.0f, 0.0f));   // 3
    plan.steps.push_back(Move(400.0f, 0.0f));   // 4: the safe point (target)
    plan.steps.push_back(Pull(420.0f, 100.0f, 9));
    std::size_t const step = plan.steps.size() - 1;
    Run r = Start(&plan, step);
    r.walkBack = true; // the straight walk just failed with walkBackIdx < step: ask the rescue directly
    r.walkBackIdx = r.walkBackStart = 3;
    r.walkBackDirect = true;
    Facts on = Party(400.0f); // the lead bot exactly on the rescue's target (step 5): 0 yd away, 800 yd replay
    CHECK(!ReplayRescue(r, on, "the straight way to the point of step 1 passes an uncleared pack")); // guard: no teleport onto itself
    CHECK(!r.strandDue && !r.strandOn && !r.strandReplay && r.strandStep == static_cast<std::size_t>(-1));
    Facts far = Party(0.0f); // control: the same failure 400 yd off DOES rescue (replay is long)
    CHECK(ReplayRescue(r, far, "the straight way to the point of step 1 passes an uncleared pack"));
    CHECK(r.strandDue && r.strandReplay && r.strandIdx == 4);
}

// The other two straight-walk failures take the rescue too: no complete path (Dire Maul North r31 slot 6 'no complete
// path to the point of step 112') and no walkable path (Maraudon r28b slot B 'no walkable path toward the point of step
// 564').
static void TestReplayRescueAfterEveryStraightFailure()
{
    DungeonPlan plan;
    for (int i = 0; i <= 5; ++i)
        plan.steps.push_back(Move(20.0f * i, 0.0f));
    plan.steps.push_back(Pull(120.0f, 100.0f, 9));
    std::size_t const step = plan.steps.size() - 1;
    Run r = Start(&plan, step);
    Facts f = Party(-400.0f);
    f.seen = false;
    StartWalkBack(r, f);
    f.approachRead = true;
    f.approachKnown = true;
    f.approachX = -360.0f;
    f.approachIncomplete = true;
    Tick(r, f);
    CHECK(r.strandDue && r.strandIdx == 5);
    CHECK(r.note.find("walk back: no complete path to the point of step 6 from here: the replay of the plan's points would walk") == 0);
    Run q = Start(&plan, step);
    Facts g = Party(-400.0f);
    g.seen = false;
    StartWalkBack(q, g);
    g.approachRead = true;
    g.approachKnown = false; // no walkable path at all
    Tick(q, g);
    CHECK(q.strandDue && q.strandIdx == 5);
    CHECK(q.note.find("walk back: no walkable path toward the point of step 6: the replay of the plan's points would walk") == 0);
}

// Shadowfang Keep r36 slot A (wipes 2 and 3 at step 43): the straight way to step 42 passed an uncleared pack, the first
// backoff went to step 16, the second to step 1 (every point between lies within reach of a later leg's pack on the
// floors above), where the lead bot was revived. The straight walk "worked" at once and the walk back walked the points
// 1-42 (307 yd, 98-132 s) without asking ReplayRescue, which only a failed straight walk asks. A backoff whose plan points
// from its new target walk kReplayRescueYd or more before the safe point on the way it came is rescued instead
// (BackoffRescue); a shorter one backs off as before, and the rescue's bounds hold.
static void TestBackoffToTheEntranceIsRescued()
{
    DungeonPlan plan;
    // 0-6 the route (0 the revive spot; 1-2 and 4-5 within reach of a later step's pack), 7 the wiped pull, 8-9 the later
    // packs (their steps not reached yet: live), 10 a point after.
    plan.steps = {Move(0, 0), Move(0, 20), Move(0, 40), Move(0, 100), Move(100, 100), Move(200, 100), Move(200, 40),
        Pull(200, -10, 9), Pull(10, 30, 21), Pull(150, 110, 22, 50.0f), Move(300, -10)};
    std::size_t const step = 7;
    auto run = [&](Facts f, Run& r, int ticks)
    {
        r = Start(&plan, step);
        f.seen = false;
        StartWalkBack(r, f);
        CHECK(r.walkBack && r.walkBackDirect && r.walkBackStart == 6);
        f.approachKnown = true;
        f.approachRead = true;
        f.approachX = 2.0f;
        f.approachY = 40.0f;
        f.directPathCrossesPack = true;
        for (int i = 0; i < ticks && !r.strandDue; ++i)
            Tick(r, f);
    };
    // The first backoff (to step 4): its points walk 260 yd: backed off as before.
    Run a;
    run(Party(2.0f), a, 1);
    CHECK(!a.strandDue && a.walkBackDirect && a.walkBackStart == 3 && a.straightBackoffs == 1);
    CHECK(a.note == "walk back: the straight way to the point of step 7 passes an uncleared pack: straight to step 4 "
        "instead (1 of 2)");
    // The second (to step 1, where the lead bot stands): its points walk 360 yd: the rescue instead.
    Run b;
    run(Party(2.0f), b, 2);
    CHECK(b.strandDue && b.strandIdx == 6 && b.walkBackIdx == 6 && b.straightBackoffs == 1);
    CHECK(b.strandBotOnly && b.strandReplay);
    CHECK(b.note == "walk back: the straight way to the point of step 4 passes an uncleared pack: the plan's points from "
        "step 1 would walk 360.0 yd: the lead bot is teleported to a safe point on the way it came (step 7) out of combat");
    // A real player in the party: the backoff as before (he walks back with the party).
    Facts withPlayer = Party(2.0f);
    PlayerFact p;
    p.name = "Hypeer";
    p.commander = true;
    withPlayer.players.push_back(p);
    Run c;
    run(withPlayer, c, 2);
    CHECK(!c.strandDue && c.walkBackDirect && c.walkBackStart == 0 && c.straightBackoffs == 2);
    // The rescue off.
    Facts off = Party(2.0f);
    off.walkBackRescue = false;
    Run d;
    run(off, d, 2);
    CHECK(!d.strandDue && d.walkBackStart == 0 && d.straightBackoffs == 2);
    // Used on this step already (a resume keeps the latch): the backoff.
    Run e;
    {
        e = Start(&plan, step);
        e.strandStep = step;
        Facts g = Party(2.0f);
        g.seen = false;
        StartWalkBack(e, g);
        g.approachKnown = true;
        g.approachRead = true;
        g.approachX = 2.0f;
        g.approachY = 40.0f;
        g.directPathCrossesPack = true;
        for (int i = 0; i < 2; ++i)
            Tick(e, g);
    }
    CHECK(!e.strandDue && e.walkBackStart == 0 && e.straightBackoffs == 2);
    // In a fight: no rescue (RescueAllowed); the fight gate holds the walk back, nothing backs off either.
    Facts fight = Party(2.0f);
    fight.anyInCombat = true;
    Run g2 = Start(&plan, step);
    g2.walkBack = true;
    g2.walkBackDirect = true;
    g2.walkBackIdx = g2.walkBackStart = 3;
    g2.straightBackoffs = 1;
    CHECK(!BackoffRescue(g2, fight, "the straight way to the point of step 4 passes an uncleared pack", 0));
    CHECK(!g2.strandDue);
    // The measure: the plan's points from the backoff target to the safe point, point to point (no lead-bot leg).
    CHECK(std::fabs(PointsYards(b, 0, 6) - 360.0f) < 0.01f && std::fabs(PointsYards(b, 3, 6) - 260.0f) < 0.01f);
    CHECK(PointsYards(b, 6, 6) == 0.0f && PointsYards(b, 6, 3) == 0.0f);
    CHECK(std::fabs(PointsYards(b, 0, 10) - 471.8f) < 0.1f); // the pulls 7-9 between are no points
    // Never onto the lead bot's own spot: on the safe point already, no rescue (the latch stays unused).
    Run h = Start(&plan, step);
    h.walkBack = true;
    h.walkBackDirect = true;
    h.walkBackIdx = h.walkBackStart = 3;
    CHECK(!BackoffRescue(h, Party(200.0f, 40.0f), "the straight way to the point of step 4 passes an uncleared pack", 0));
    CHECK(!h.strandDue);
    CHECK(BackoffRescue(h, Party(2.0f), "the straight way to the point of step 4 passes an uncleared pack", 0)); // control
}

// The retreat at two dead (AiPlayerbot.DungeonRunRetreatAt2Dead): two dead, the lead bot alive, no engaged unit lost
// hp for kRetreatQuietMs: one `move` to the last stand, once a fight, bounded; off by default; never for an event boss.
static Facts LosingFight(std::uint32_t hp)
{
    Facts f = Party(20, 0);
    f.seen = true;
    f.anyInCombat = true;
    f.leadInCombat = true;
    f.dead = 2;
    f.alive = 3;
    f.engagedRead = true;
    UnitFact u;
    u.guid = 77;
    u.entry = 4519;
    u.attackable = true;
    u.health = hp;
    u.maxHealth = 1000;
    f.engagedUnits = {u};
    return f;
}

static std::string FightFor(Run& r, int ticks, bool falling, bool key, std::uint32_t& hp, int& at)
{
    std::string got;
    at = -1;
    for (int i = 0; i < ticks && r.state == State::Running; ++i)
    {
        Facts f = LosingFight(hp);
        f.retreatAt2Dead = key;
        if (falling)
            hp = hp > 30 ? hp - 30 : 0;
        std::string const o = Tick(r, f);
        if (got.empty() && !o.empty())
        {
            got = o;
            at = i;
        }
    }
    return got;
}

static void TestRetreatAtTwoDead()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0), Pull(40, 0, 4519)};
    auto fighting = [&](std::uint32_t flags = 0, PlanStepKind kind = PlanStepKind::Pull)
    {
        plan.steps[1].kind = kind;
        plan.steps[1].entry = 4519;
        plan.steps[1].flags = flags;
        Run r = Start(&plan, 1);
        r.anchorKnown = true; // the stand the run walked to before the pull
        r.anchorX = 0;
        r.restDue = false;
        r.fighting = true;
        return r;
    };
    std::uint32_t hp = 900;
    int at = -1;
    Run r = fighting();
    std::string const order = FightFor(r, 15, false, true, hp, at);
    int const first = at;
    CHECK(r.note.find("retreat: 2 dead and no engaged unit lost hp in 15 s: the living pull back to the last stand") == 0);
    CHECK(order == "move 0.0 0.0 0.0" && first == 14); // fightHpMs reached kRetreatQuietMs
    CHECK(FightFor(r, 5, false, true, hp, at) == "move 0.0 0.0 0.0" && at == 4); // sent again once after 5 s
    CHECK(r.retreatOn);
    CHECK(r.note.find("retreat: 2 dead, the living pull back to the last stand at 0.0 0.0 0.0") == 0);
    // Arrived at the stand: over, and not again in this fight.
    Facts there = LosingFight(hp);
    there.retreatAt2Dead = true;
    there.leadX = 0;
    Tick(r, there);
    CHECK(!r.retreatOn && r.retreatDone);
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, there).find("move") != 0);
    // The fight over: the next fight may retreat again.
    Facts calm = Party(0);
    calm.seen = true;
    Tick(r, calm);
    CHECK(!r.retreatDone);
    // Off (the default): nothing.
    Run off = fighting();
    hp = 900;
    CHECK(FightFor(off, 30, false, false, hp, at).empty() && !off.retreatOn);
    // A fight still being won (hp falling): no retreat.
    Run winning = fighting();
    hp = 1000;
    CHECK(FightFor(winning, 30, true, true, hp, at).empty());
    // One dead: no retreat.
    Run one = fighting();
    bool moved = false;
    for (int i = 0; i < 30; ++i)
    {
        Facts f = LosingFight(900);
        f.retreatAt2Dead = true;
        f.dead = 1;
        f.alive = 4;
        moved = moved || Tick(one, f).compare(0, 5, "move ") == 0;
    }
    CHECK(!moved);
    // An event boss (an encounter): never.
    Run boss = fighting(ai::kPlanFlagEncounter | ai::kPlanFlagDoneData, PlanStepKind::Boss);
    hp = 900;
    CHECK(FightFor(boss, 30, false, true, hp, at).empty());
    // A plain boss: the retreat as for a pack.
    Run plain = fighting(0, PlanStepKind::Boss);
    hp = 900;
    CHECK(FightFor(plain, 30, false, true, hp, at) == "move 0.0 0.0 0.0");
    // A stand farther than kRetreatMaxYd: none.
    Run far = fighting();
    far.anchorX = -60;
    hp = 900;
    CHECK(FightFor(far, 30, false, true, hp, at).empty());
    // Bounded: never on the stand after kRetreatCapMs, the order sent twice at most.
    Run stuck = fighting();
    hp = 900;
    int moves = 0;
    for (int i = 0; i < 60; ++i)
    {
        Facts f = LosingFight(hp);
        f.retreatAt2Dead = true;
        if (Tick(stuck, f) == "move 0.0 0.0 0.0")
            ++moves;
    }
    CHECK(moves == 2 && !stuck.retreatOn && stuck.retreatDone);
    // A refused retreat ends it without a pause.
    Run refused = fighting();
    hp = 900;
    CHECK(FightFor(refused, 20, false, true, hp, at) == "move 0.0 0.0 0.0");
    OrderRefused(refused, "move 0.0 0.0 0.0", "invalid: in combat");
    CHECK(refused.state == State::Running && !refused.retreatOn && refused.retreatDone);
}

// (B) Rest to what the next step needs (WASTED-TIME sink 3). A fight is won on pull step 0; what the rest gate does
// then depends on the next fight: a small pull or nothing (the light lines), a boss, an event or a big pull (full).
static PlanStep Boss(float x, std::uint32_t entry)
{
    PlanStep s;
    s.kind = PlanStepKind::Boss;
    s.x = x;
    s.entry = entry;
    s.radius = 15.0f;
    ai::NormalizePlanStep(s);
    return s;
}

// The fight on step 0 is over (its pack gone); the ticks after it with `party`'s health and mana. The first order.
static std::string AfterFight(Run& r, Facts party, int ticks = 1)
{
    Facts f = Party(0);
    f.seen = true;
    f.units = {};
    f.anyInCombat = true;
    UnitFact u;
    u.guid = 5;
    u.entry = 1;
    f.units = {u};
    Tick(r, f); // the fight
    party.seen = true;
    std::string got;
    for (int i = 0; i < ticks && got.empty(); ++i)
        got = Tick(r, party);
    return got;
}

static Facts Health(std::uint32_t health, std::uint32_t healerMana = 100, std::uint32_t mana = 100)
{
    Facts f = Party(0);
    f.minHealthPct = health;
    f.minHealerManaPct = healerMana;
    f.minManaPct = mana;
    return f;
}

static void TestRestLinesFollowTheNextFight()
{
    DungeonPlan small;
    small.steps = {Pull(0, 0, 1), Move(30, 0), Pull(60, 0, 2)};
    DungeonPlan boss;
    boss.steps = {Pull(0, 0, 1), Move(30, 0), Boss(60, 3)};
    // 70 % health: within the budget of a small pull, no rest (the old lines rested under 80 %).
    Run a = Start(&small);
    a.fighting = true;
    CHECK(AfterFight(a, Health(70)) != "rest" && a.phase != Phase::Rest);
    // ... before a boss: the full lines, the rest as before.
    Run b = Start(&boss);
    b.fighting = true;
    CHECK(AfterFight(b, Health(70)) == "rest" && b.phase == Phase::Rest && !b.restLight);
    Tick(b, Health(85));
    CHECK(b.phase == Phase::Rest); // under the full end line (90)
    Tick(b, Health(90));
    CHECK(b.phase != Phase::Rest);
    // Under the light start line before a small pull: a rest, ended at the light end line (75), not 90.
    Run c = Start(&small);
    c.fighting = true;
    CHECK(AfterFight(c, Health(50)) == "rest" && c.restLight);
    Tick(c, Health(70));
    CHECK(c.phase == Phase::Rest);
    Tick(c, Health(76));
    CHECK(c.phase != Phase::Rest && !c.restLight);
    // Healer mana: 40 % is within a small pull's budget (35), 30 % rests to 60, never only the healer while the others
    // walk on: the order is the party's `rest`.
    Run d = Start(&small);
    d.fighting = true;
    CHECK(AfterFight(d, Health(100, 40)) != "rest");
    Run e = Start(&small);
    e.fighting = true;
    CHECK(AfterFight(e, Health(100, 30)) == "rest");
    Tick(e, Health(100, 55));
    CHECK(e.phase == Phase::Rest);
    Tick(e, Health(100, 60));
    CHECK(e.phase != Phase::Rest);
    // Nothing but moves ahead: the light lines.
    DungeonPlan walk;
    walk.steps = {Pull(0, 0, 1), Move(30, 0), Move(60, 0)};
    Run w = Start(&walk);
    w.fighting = true;
    CHECK(AfterFight(w, Health(70)) != "rest");
    // A boss beyond the lookahead (kRestLookaheadSteps) does not count yet.
    DungeonPlan far;
    far.steps = {Pull(0, 0, 1)};
    for (std::size_t i = 0; i < kRestLookaheadSteps; ++i)
        far.steps.push_back(Move(10.0f * (i + 1), 0));
    far.steps.push_back(Boss(200, 3));
    Run fr = Start(&far);
    fr.fighting = true;
    CHECK(AfterFight(fr, Health(70)) != "rest");
    // An event ahead (a hold) and a big pull (the aggro estimate's kBigPackJoiners): the full lines.
    DungeonPlan event;
    event.steps = small.steps;
    PlanStep hold;
    hold.kind = PlanStepKind::Hold;
    hold.x = 90;
    hold.radius = 20;
    hold.timeoutS = 60;
    hold.entries = {9};
    ai::NormalizePlanStep(hold);
    event.steps.push_back(hold);
    Run h = Start(&event);
    CHECK(LightRestAhead(h, 1));  // the next fight is the small pull at 60 ...
    CHECK(!LightRestAhead(h, 3)); // ... and after it the hold: the full lines before it
    Run on = Start(&small);
    on.eventOn = true; // inside an event already: the full lines
    CHECK(!LightRestAhead(on, 1));
    Run big = Start(&small);
    big.step = 2;
    big.joinerSeen[44] = kBigPackJoiners;
    CHECK(!LightRestAhead(big, 2));
    big.joinerSeen[44] = kBigPackJoiners - 1;
    CHECK(LightRestAhead(big, 2));
    // A plan `rest` step rests to the full lines whatever comes after it.
    DungeonPlan planned;
    PlanStep rest;
    rest.kind = PlanStepKind::Rest;
    ai::NormalizePlanStep(rest);
    planned.steps = {rest, Pull(60, 0, 2)};
    Run pr = Start(&planned);
    CHECK(Tick(pr, Health(85)) == "rest" && !pr.restLight);
    Tick(pr, Health(85));
    CHECK(pr.phase == Phase::Rest);
}

// The meal wait under the light lines: a bot other than the lead bot still drinking above them does not hold a small
// pull; the lead bot seated or eating does (the pullback would find the tank seated, Uldaman pulls 17 and 61), and so
// does a party under them.
static void TestMealWaitUnderTheLightLines()
{
    DungeonPlan plan;
    plan.steps = {Pull(40, 0, 4852)};
    UnitFact u;
    u.guid = 31;
    u.entry = 4852;
    u.distLead = 30;
    Facts f = Health(82, 70);
    f.seen = true;
    f.units = {u};
    f.eating = 1;
    Run r = Start(&plan);
    CHECK(Tick(r, f) == "pullback 31"); // the healer sips at 70 %: the pull goes out
    Run t = Start(&plan);
    f.leadResting = true;
    CHECK(Tick(t, f).empty() && t.phase == Phase::Rest && t.restForEating);
    // The tank stands up: the wait is over at the light lines, the pull goes out with the healer still drinking.
    f.leadResting = false;
    std::string order;
    for (int i = 0; i < 3 && order.empty(); ++i)
        order = Tick(t, f);
    CHECK(order == "pullback 31");
    // Under the light end lines a drinking bot holds the pull as before.
    Run low = Start(&plan);
    Facts g = Health(82, 50);
    g.seen = true;
    g.units = {u};
    g.eating = 1;
    CHECK(Tick(low, g).empty() && low.phase == Phase::Rest);
    // Before a boss: every drinking bot holds, as before.
    DungeonPlan boss;
    boss.steps = {Boss(40, 4852)};
    Run b = Start(&boss);
    Facts bf = f;
    bf.units.clear();
    CHECK(Tick(b, bf).empty() && b.phase == Phase::Rest && b.restForEating);
}

// (C) Fight forward (WASTED-TIME sink 2). Step 0's pack is cleared with the tank at 5 0; the plan's next rows walk
// back to the stand at -20 0 and pull the next pack at 20 0. The pull goes out from where the tank stands.
struct ForwardCase
{
    DungeonPlan plan;
    float leadX = 5.0f, leadZ = 0.0f;
    bool walkBackKey = false;
    std::uint32_t pulls = 1;
    // The glue's read of the way (Facts::forward*), and whether it answers at all.
    bool answers = true, pathOk = true, los = true, hostileNear = false, hopAlive = false, key = true;
};

// The first order after the cleared step, and the step the run stands on then.
static std::string AfterClear(ForwardCase& c, Run& r)
{
    r = Start(&c.plan);
    r.pulls = c.pulls;
    Facts f = Party(c.leadX, 0.0f, c.leadZ);
    f.seen = true;
    f.walkBackAfterWonFight = c.walkBackKey;
    f.fightForward = c.key;
    Tick(r, f); // "pack cleared"
    CHECK(r.step == 1 && r.note.find("pack cleared") == 0);
    std::string order;
    for (int i = 0; i < 4 && order.empty(); ++i)
    {
        f.forwardRead = c.answers && r.fightForwardWanted != static_cast<std::size_t>(-1);
        f.forwardFor = r.fightForwardWanted;
        f.forwardPathOk = c.pathOk;
        f.forwardLos = c.los;
        f.forwardHostileNear = c.hostileNear;
        f.forwardHopAlive = c.hopAlive;
        const PlanStep* s = Current(r);
        if (s && s->kind == PlanStepKind::Pull)
        {
            UnitFact u;
            u.guid = 61;
            u.entry = s->entries.front();
            u.distLead = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s->x, s->y, s->z);
            f.units = {u};
        }
        order = Tick(r, f);
    }
    return order;
}

static ForwardCase Forward()
{
    ForwardCase c;
    c.plan.steps = {Pull(0, 0, 1), Move(-20, 0), Pull(20, 0, 2, 10.0f), Move(60, 0)};
    return c;
}

static void TestFightForwardPullsFromWhereTheTankStands()
{
    ForwardCase c = Forward();
    Run r;
    CHECK(AfterClear(c, r) == "pullback 61");
    CHECK(r.step == 2 && r.anchorKnown && r.anchorX == 5.0f);
    // The old way: the key at 1 walks back to the stand first.
    ForwardCase old = Forward();
    old.walkBackKey = true;
    Run o;
    CHECK(AfterClear(old, o) == "move -20.0 0.0 0.0");
    // A pack farther than kFightForwardYd: the plan's rows.
    ForwardCase far = Forward();
    far.plan.steps[2] = Pull(40, 0, 2, 10.0f);
    Run fr;
    CHECK(AfterClear(far, fr) == "move -20.0 0.0 0.0");
    // On another floor (more than kStragglerDzYd up or down): the plan's rows.
    ForwardCase up = Forward();
    up.leadZ = 9.0f;
    Run u;
    AfterClear(up, u);
    CHECK(u.step == 1);
    // Another live pack on the straight line: the plan's rows.
    ForwardCase cross = Forward();
    cross.plan.steps.push_back(Pull(12, 3, 3, 5.0f));
    Run x;
    AfterClear(cross, x);
    CHECK(x.step == 1);
    // An exact point (a stand requirement) between: the plan's rows.
    ForwardCase exact = Forward();
    exact.plan.steps[1].flags = ai::kPlanFlagExact;
    Run e;
    AfterClear(exact, e);
    CHECK(e.step == 1);
    // An ambush pack, or one the party never fought for (no pull went out on the cleared step): the plan's rows.
    ForwardCase ambush = Forward();
    ambush.plan.steps[2].flags = ai::kPlanFlagAmbush;
    Run a;
    AfterClear(ambush, a);
    CHECK(a.step == 1);
    ForwardCase quiet = Forward();
    quiet.pulls = 0;
    Run q;
    CHECK(AfterClear(quiet, q) == "move -20.0 0.0 0.0");
    // More than kFightForwardMoves moves to the pull: the plan's rows.
    ForwardCase many = Forward();
    many.plan.steps = {Pull(0, 0, 1)};
    for (std::size_t i = 0; i <= kFightForwardMoves; ++i)
        many.plan.steps.push_back(Move(-2.0f * (i + 1), 0));
    many.plan.steps.push_back(Pull(20, 0, 2, 10.0f));
    Run m;
    AfterClear(many, m);
    CHECK(m.step == 1);
    // Two moves, within the bound: both left out.
    ForwardCase two = Forward();
    two.plan.steps = {Pull(0, 0, 1), Move(-20, 0), Move(-10, 5), Pull(20, 0, 2, 10.0f)};
    Run t;
    CHECK(AfterClear(two, t) == "pullback 61" && t.step == 3);
}

// (D) Gnomeregan r27 slot 4 steps 583 and 610: the stuck pause while the lead bot walked up a long spiral ramp (its
// straight distance to the point grew from 82.8 to 104.9 yd). A walk whose lead bot moves 2 yd or more from its spot
// at the last progress is making progress, for kWalkMovingCapMs of such movement in one walk.
static void TestSpiralRampWalkIsNotStuck()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0, 40)};
    Run r = Start(&plan);
    Facts f = Party(80, 0, 0);
    f.seen = true; // the glue's facts for a plain move: its walk, not approach hops
    CHECK(Tick(r, f) == "move 0.0 0.0 40.0");
    // Up the spiral: 80 s around a ring whose radius grows from 80 to 104 yd (the straight distance grows), then in.
    bool paused = false;
    int resends = 0;
    for (int t = 0; t < 80; ++t)
    {
        float const a = 0.09f * t;
        float const radius = 80.0f + 0.3f * t;
        f.leadX = radius * std::cos(a);
        f.leadY = radius * std::sin(a);
        f.leadZ = 0.5f * t;
        std::string const o = Tick(r, f);
        resends += o == "move 0.0 0.0 40.0" ? 1 : 0;
        paused = paused || r.state != State::Running;
    }
    CHECK(!paused && resends == 0 && r.walkMovingMs > 60000);
    f.leadX = 1;
    f.leadY = 0;
    f.leadZ = 40;
    Tick(r, f);
    CHECK(r.state == State::Done);
    // The cap is one walk's: the next walk starts with its own.
    DungeonPlan two;
    two.steps = {Move(0, 0, 40), Move(0, 0, 80)};
    Run w = Start(&two);
    Facts k = Party(80, 0, 0);
    k.seen = true;
    for (int leg = 0; leg < 2; ++leg)
    {
        float const top = 40.0f * (leg + 1);
        Tick(w, k);
        for (int t = 0; t < 90 && w.state == State::Running; ++t)
        {
            float const radius = 80.0f + 0.3f * t;
            k.leadX = radius * std::cos(0.09f * t);
            k.leadY = radius * std::sin(0.09f * t);
            k.leadZ = top - 40.0f + 0.4f * t;
            Tick(w, k);
        }
        CHECK(w.state == State::Running && w.step == static_cast<std::size_t>(leg));
        k.leadX = 0;
        k.leadY = 0;
        k.leadZ = top;
        Tick(w, k);
        k.leadX = 80;
        k.leadY = 0;
    }
    CHECK(w.state == State::Done);
    // Movement alone is bounded: a lead bot walking back and forth between two spots 30 yd apart, never nearer, is
    // stuck after kWalkMovingCapMs as before (then the walk's tries).
    Run o = Start(&plan);
    Facts g = Party(80, 0, 0);
    g.seen = true;
    Tick(o, g);
    int ticks = 0;
    for (; ticks < 400 && o.state == State::Running; ++ticks)
    {
        g.leadX = (ticks / 4) % 2 ? 110.0f : 80.0f;
        Tick(o, g);
    }
    CHECK(o.state == State::Paused && o.note.find("the lead bot is stuck") == 0);
    CHECK(ticks * kTickMs >= kWalkMovingCapMs);
    // Standing still: the old stuck pause after the walk's tries (kWalkTries x kWalkRetryMs).
    Run st = Start(&plan);
    Facts h = Party(80, 0, 0);
    h.seen = true;
    int still = 0;
    for (; still < 200 && st.state == State::Running; ++still)
        Tick(st, h);
    CHECK(st.state == State::Paused && still * kTickMs <= kWalkTries * kWalkRetryMs + 2 * kTickMs);
}

// Round 1 of 0d9957be (Opus must 1): a bare PATHFIND_INCOMPLETE is set for a point just off the mesh (the end a few
// yards from it) and at the path finder's 256-polygon cap (the end anywhere along a long way); only an end far from the
// point (kPathShortYd) or on another floor (kPathShortDzYd) is short, and a walk takes a capped path that still gets on
// (round 3, the Opus re-review of a4a16e14: CappedPathGetsOn, PathWalkable).
static void TestPathEndsShortOnlyFarFromThePoint()
{
    CHECK(PathEndsShort(true, false, 0, 0, 0, 100, 0, 0));       // no path
    CHECK(!PathEndsShort(false, false, 0, 0, 0, 100, 0, 0));     // complete
    CHECK(!PathEndsShort(false, true, 95, 0, 0, 100, 0, 0));     // ending 5 yd short on the same floor
    CHECK(!PathEndsShort(false, true, 99, 0, 1, 100, 0, 0));     // a point just off the mesh
    CHECK(PathEndsShort(false, true, 88, 0, 0, 100, 0, 0));      // 12 yd short
    CHECK(PathEndsShort(false, true, 100, 2, 7, 100, 0, 0));     // 7 yd above it: another floor
    CHECK(PathFitsStraight(125, 100) && !PathFitsStraight(135, 100) && PathFitsStraight(3, 0.5f));
    // Round 2 (the Codex review of a4a16e14): a fallback result proves no navmesh route, for fight forward and the
    // replay shortcut alike (both read it through DirectNavPath -> NavPathReaches), complete or not.
    CHECK(NavPathReaches(false, false, false, false, 100, 0, 0, 100, 0, 0));
    CHECK(NavPathReaches(false, true, false, false, 95, 0, 0, 100, 0, 0));   // 5 yd short, as above
    CHECK(!NavPathReaches(false, false, true, false, 100, 0, 0, 100, 0, 0)); // PATHFIND_SHORTCUT
    CHECK(!NavPathReaches(false, false, false, true, 100, 0, 0, 100, 0, 0)); // PATHFIND_NOT_USING_PATH
    CHECK(!NavPathReaches(false, true, true, false, 95, 0, 0, 100, 0, 0));
    CHECK(!NavPathReaches(false, true, false, true, 95, 0, 0, 100, 0, 0));
    CHECK(!NavPathReaches(true, false, false, false, 100, 0, 0, 100, 0, 0)); // no path
    CHECK(!NavPathReaches(false, true, false, false, 88, 0, 0, 100, 0, 0));  // short
    CHECK(NavPathIsFallback(true, false) && NavPathIsFallback(false, true) && !NavPathIsFallback(false, false));
    // The capped long path: from -300 0 toward 400 0 (700 yd) it ends at 150 0 (250 yd short, more than half the way
    // made): a walk takes it; one ending at 100 0 (300 yd short of a 700 yd way, 57 %: also more than half) too; one at
    // -100 0 (500 yd short) does not; nor one that went up a floor (z 20, both floors at 0) or a skip's or shortcut's
    // read (NavPathReaches: the end must reach the point).
    CHECK(PathEndsShort(false, true, 150, 0, 0, 400, 0, 0));
    CHECK(CappedPathGetsOn(false, true, -300, 0, 0, 150, 0, 0, 400, 0, 0));
    CHECK(PathWalkable(false, true, -300, 0, 0, 150, 0, 0, 400, 0, 0));
    CHECK(PathWalkable(false, true, -300, 0, 0, 100, 0, 0, 400, 0, 0));
    CHECK(!PathWalkable(false, true, -300, 0, 0, -100, 0, 0, 400, 0, 0));
    CHECK(!PathWalkable(false, true, -300, 0, 0, 150, 0, 20, 400, 0, 0));
    CHECK(PathWalkable(false, true, -300, 0, 0, 150, 0, 3, 400, 0, 5)); // on the point's floor
    CHECK(PathWalkable(false, true, -300, 0, 0, 150, 0, 15, 400, 0, 18)); // up a ramp onto the point's floor
    CHECK(!PathWalkable(true, false, -300, 0, 0, 150, 0, 0, 400, 0, 0)); // no path
    CHECK(!CappedPathGetsOn(false, false, -300, 0, 0, 150, 0, 0, 400, 0, 0)); // a complete path is not "capped"
    CHECK(!NavPathReaches(false, true, false, false, 150, 0, 0, 400, 0, 0));
    // A long straight walk back whose path stops at the cap halfway stays straight (the glue's read, PathWalkable).
    DungeonPlan plan;
    plan.steps = {Move(0, 0), Move(400, 0), Pull(600, 200, 9)};
    Run r = Start(&plan, 2);
    Facts f = Party(-300, 0);
    StartWalkBack(r, f);
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 1);
    f.seen = false;
    f.approachRead = true;
    f.approachKnown = true;
    f.approachX = -260;
    f.approachIncomplete = !PathWalkable(false, true, -300, 0, 0, 150, 0, 0, 400, 0, 0);
    CHECK(Tick(r, f) == "move -260.0 0.0 0.0" && r.walkBackDirect);
}

// Round 1 (Opus must 2): the stuck walk back's teleport goes to a safe point: when the last reached point is the stand
// beside the pack that wiped the party, the latest point before it out of every live pack's reach; off, the pause.
static void TestStuckRescueGoesToASafePoint()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0), Move(40, 0), Move(80, 0), Move(110, 0), Pull(125, 0, 4519)};
    Run r = Start(&plan, 4);
    Facts f = Party(10, 40, 30); // stuck on a ledge
    f.seen = true;
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 3;
    std::string order;
    for (int i = 0; i < 60 && r.state == State::Running && order.compare(0, 7, "rescue ") != 0; ++i)
    {
        AnswerRescueScan(r, f);
        order = Tick(r, f);
    }
    CHECK(order == "rescue from 10.0 40.0 30.0 to 80.0 0.0 0.0"); // not 110 0, 15 yd from the pack
    CHECK(r.walkBackIdx == 2);
    // Round 3 (the Opus re-review of a4a16e14): with the route checkpoint known on the stand beside the pack (MoveStep's
    // NoteReached after the walk there, the normal case), the safe point is still searched before it.
    Run known = Start(&plan, 4);
    known.routeKnown = true;
    known.checkpoint = 3;
    CHECK(ReplayFloor(known) == 3 && LastReachedBefore(known, 4) == 3);
    CHECK(StraightWalkBackTarget(known, 0, 3) == 2 && SafePointAtOrBefore(known, 0, 3) == 2);
    known.walkBack = true;
    known.walkBackIdx = known.walkBackStart = 3;
    std::string ko;
    for (int i = 0; i < 60 && known.state == State::Running && ko.compare(0, 7, "rescue ") != 0; ++i)
    {
        AnswerRescueScan(known, f);
        ko = Tick(known, f);
    }
    CHECK(ko == "rescue from 10.0 40.0 30.0 to 80.0 0.0 0.0");
    // Not past a direct step (a hole): the search ends there.
    DungeonPlan hole = plan;
    hole.steps[2] = Direct(80, 0);
    Run hr = Start(&hole, 4);
    CHECK(SafePointAtOrBefore(hr, 0, 3) == static_cast<std::size_t>(-1));
    // No safe point at all: the pause with that reason, never a teleport beside the pack.
    DungeonPlan tight;
    tight.steps = {Move(100, 0), Move(110, 0), Pull(125, 0, 4519)};
    Run t = Start(&tight, 2);
    t.routeKnown = true;
    t.checkpoint = 1;
    t.walkBack = true;
    t.walkBackIdx = t.walkBackStart = 1;
    std::string to;
    for (int i = 0; i < 60 && t.state == State::Running; ++i)
    {
        std::string const o = Tick(t, f);
        if (!o.empty())
            to = o;
    }
    CHECK(t.state == State::Paused && to.compare(0, 7, "rescue ") != 0);
    CHECK(t.note.find("no point on the way it came is out of every live pack's reach") != std::string::npos);
    Run off = Start(&plan, 4);
    Facts g = f;
    g.walkBackRescue = false;
    off.walkBack = true;
    off.walkBackIdx = off.walkBackStart = 3;
    for (int i = 0; i < 60 && off.state == State::Running; ++i)
        Tick(off, g);
    CHECK(off.state == State::Paused && off.note.find("the lead bot is stuck") == 0);
    // In a fight: no teleport (the walk waits for the fight as before; the rescue is judged out of combat).
    Run fight = Start(&plan, 4);
    fight.walkBack = true;
    fight.walkBackIdx = fight.walkBackStart = 3;
    Facts h = f;
    h.anyInCombat = true;
    CHECK(!WalkBackStuckRescue(fight, h, 50, 110, 0, 0));
}

// Round 1 (Codex high, Opus must 3): fight forward skips only with the glue's read of the way passed: a direct
// navmesh path (not through a wall, not to the floor above, not partial), sight of the pack, no live hostile by the
// way (a patrol), no live hostile at a move point left out; never past a leg end or with a deferred pull pending;
// never to a pack left uncleared or past one on the line; the key off keeps the rows.
static void TestFightForwardNeedsTheGluesRead()
{
    auto skipped = [](ForwardCase c) {
        Run r;
        AfterClear(c, r);
        return r.step == 2; // else the walk back to the stand at -20 0 (step 2 of the plan) is under way
    };
    CHECK(skipped(Forward()));
    ForwardCase wall = Forward();
    wall.pathOk = false; // a wall between: the path goes round, longer than 1.3 x the line (or the floor above)
    CHECK(!skipped(wall));
    ForwardCase sight = Forward();
    sight.los = false;
    CHECK(!skipped(sight));
    ForwardCase patrol = Forward();
    patrol.hostileNear = true;
    CHECK(!skipped(patrol));
    ForwardCase hop = Forward();
    hop.hopAlive = true;
    CHECK(!skipped(hop));
    ForwardCase silent = Forward();
    silent.answers = false; // a glue that does not read it: the rows
    CHECK(!skipped(silent));
    ForwardCase off = Forward();
    off.key = false;
    CHECK(!skipped(off));
    // Round 3: a rest after the ask drops it (no navmesh read on a rest tick); the ask goes out again after the rest.
    {
        ForwardCase c = Forward();
        Run r = Start(&c.plan);
        r.pulls = 1;
        Facts f = Party(5, 0);
        f.seen = true;
        Tick(r, f); // pack cleared
        Tick(r, f); // the ask
        CHECK(r.fightForwardWanted == 2);
        r.phase = Phase::Rest;
        f.minHealthPct = 30;
        Tick(r, f);
        CHECK(r.phase == Phase::Rest && r.fightForwardWanted == static_cast<std::size_t>(-1) && r.fightForwardDue);
        Tick(r, f);
        CHECK(r.fightForwardWanted == static_cast<std::size_t>(-1));
        f.minHealthPct = 100;
        for (int i = 0; i < 3 && r.fightForwardWanted == static_cast<std::size_t>(-1); ++i)
            Tick(r, f);
        CHECK(r.fightForwardWanted == 2); // asked again once the rest is over
    }
    // A leg end between the cleared pack and the next one.
    ForwardCase leg = Forward();
    leg.plan.steps[1].leg = 1;
    leg.plan.steps[2].leg = 1;
    leg.plan.steps[3].leg = 1;
    CHECK(!skipped(leg));
    // A deferred pull pending (StepAfter may turn back to it).
    {
        ForwardCase d = Forward();
        Run r = Start(&d.plan);
        r.pulls = 1;
        r.deferredPulls = {0};
        Facts f = Party(5, 0);
        f.seen = true;
        CHECK(r.step == 0);
        r.step = 1;
        CHECK(FightForwardTarget(r, f) == static_cast<std::size_t>(-1));
        r.deferredPulls.clear();
        CHECK(FightForwardTarget(r, f) == 2);
        // The pack itself left uncleared, or an uncleared earlier pack on the line: no.
        r.notCleared.insert(2);
        CHECK(FightForwardTarget(r, f) == static_cast<std::size_t>(-1));
        r.notCleared.clear();
        DungeonPlan line = d.plan;
        line.steps.insert(line.steps.begin(), Pull(12, 2, 8, 4.0f)); // a pack behind the run, on the line
        Run q = Start(&line);
        q.step = 2;
        CHECK(FightForwardTarget(q, f) == 3); // cleared earlier: not live
        q.notCleared.insert(0);
        CHECK(FightForwardTarget(q, f) == static_cast<std::size_t>(-1));
    }
}

// Round 1 (Opus notes): AiPlayerbot.DungeonRunLightRest 0 keeps the full lines; with the aggro estimate off a pack of
// kBigPackUnits or more in sight is a big pull (full lines, a rest to them before its pull once a step); a meal wait
// under the light lines with the tank seated stays in its rest (no Rest/Begin flip-flop); a replay shortcut whose
// path the glue finds no direct way is not taken.
static void TestRoundOneRestAndShortcutNotes()
{
    DungeonPlan small;
    small.steps = {Pull(0, 0, 1), Move(30, 0), Pull(60, 0, 2)};
    Run off = Start(&small);
    off.fighting = true;
    Facts full = Health(70);
    full.lightRest = false;
    CHECK(AfterFight(off, full) == "rest" && !off.restLight);
    // A big pack in sight, the estimate off.
    DungeonPlan plan;
    plan.steps = {Pull(40, 0, 4852)};
    Facts f = Health(75);
    f.seen = true;
    for (std::uint64_t g = 1; g <= kBigPackUnits; ++g)
    {
        UnitFact u;
        u.guid = g;
        u.entry = 4852;
        u.distLead = 30;
        f.units.push_back(u);
    }
    Run big = Start(&plan);
    CHECK(Tick(big, f) == "rest" && !big.restLight && !big.restForJoiners); // the step's own gate: the full lines
    Run pre = Start(&plan);
    pre.restDue = false; // no rest gate due: the big pack's own rest before its pull
    CHECK(Tick(pre, f) == "rest" && pre.restForJoiners && pre.note.find("a pack of 5 units") != std::string::npos);
    Facts up = f;
    up.minHealthPct = 100;
    std::string po;
    for (int i = 0; i < 4 && po.compare(0, 9, "pullback ") != 0; ++i)
        po = Tick(pre, up);
    CHECK(po.compare(0, 9, "pullback ") == 0);
    pre.phase = Phase::Begin; // the same step again (a pull that left units): its big rest was spent
    CHECK(Tick(pre, f) != "rest");
    f.minHealthPct = 100;
    std::string o;
    for (int i = 0; i < 4 && o.compare(0, 9, "pullback ") != 0; ++i)
        o = Tick(big, f);
    CHECK(o.compare(0, 9, "pullback ") == 0);
    f.units.pop_back(); // four: a small pack, no rest at 75 %
    f.minHealthPct = 75;
    Run four = Start(&plan);
    four.restDue = false;
    CHECK(Tick(four, f).compare(0, 9, "pullback ") == 0);
    // The meal wait with the tank seated: Rest on every tick while it sits.
    Facts meal = Health(82, 70);
    meal.seen = true;
    meal.units = {f.units.front()};
    meal.eating = 1;
    meal.leadResting = true;
    Run m = Start(&plan);
    for (int i = 0; i < 10; ++i)
    {
        Tick(m, meal);
        CHECK(m.phase == Phase::Rest && m.restForEating);
    }
    CHECK(m.phaseMs >= 9 * kTickMs); // one rest, not a new one every tick
    // The replay shortcut with no direct way there: the plan's next point.
    std::size_t wipe = 0, side = 0;
    DungeonPlan shape = GnomereganShape(wipe, side);
    shape.steps.insert(shape.steps.begin() + 15, Pull(150.0f, 5.0f, 7300));
    ++wipe;
    Run r = Start(&shape, wipe);
    r.notCleared.insert(15);
    Facts e = Party(0.0f, 0.0f);
    e.walkBackRescue = false;
    StartWalkBack(r, e);
    WalkBackSim const walls = SimulateWalkBack(r, e, 8000, false);
    CHECK(r.step == wipe && NoteSeen(walls, "no direct way from step"));
    CHECK(walls.walked > 1150.0f); // every point, the side leg too
}

// The placed run's teleport target follows the safe-point rule too: the first point from the start step on that no
// live pack is near (a pack before the start step is live: the run never fought it).
static void TestPlacedRescueTargetIsSafe()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0), Move(100, 0), Pull(505, 0, 77), Move(500, 0), Move(540, 0), Move(580, 0), Pull(620, 60, 78)};
    Run r = Start(&plan, 6);
    r.startStep = 3;
    Facts f = Party(0, 0);
    StartWalkBack(r, f);
    CHECK(r.strandDue && r.strandIdx == 5); // 580 0: the last point reached, out of every live pack's reach
    // With a real player in the party: the first safe point from the start step, as before (he walks back with the
    // party from there): 540 0, as 500 0 lies 5 yd from the pre-start pack at 505 0.
    Run q = Start(&plan, 6);
    q.startStep = 3;
    Facts g = Party(0, 0);
    PlayerFact pl;
    pl.name = "Hypeer";
    g.players.push_back(pl);
    StartWalkBack(q, g);
    CHECK(q.strandDue && q.strandIdx == 4);
    CHECK(q.note == "walk back: the lead bot is up behind the run's start step (step 4; the plan point nearest it is "
        "step 1): it is teleported to step 5 out of combat");
}
// BFD r29 slot 6: the first wipe's rescue succeeded on this step, then a second wipe came on the same step. The
// regroup the run starts by itself re-arms the rescue's once-a-step latch (strandStep), so the walk back after it
// may teleport again; the failed tries of this step stay spent.
static void TestSecondWipeOnTheStepIsRescuedToo()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    std::size_t const start = side + 1;
    Run r = Start(&plan, wipe);
    r.startStep = start;
    r.strandStep = wipe;            // the first wipe's rescue succeeded on this step
    Facts f = Party(0.0f, 0.0f);    // up at the entrance
    f.dead = 3; f.alive = 2;        // the second wipe, fight over (anyInCombat false by default)
    Tick(r, f);
    CHECK(r.regroup && r.autoRegroups == 1 && r.note.find("regrouping by itself") != std::string::npos);
    CHECK(r.strandStep == static_cast<std::size_t>(-1));     // re-armed by the regroup
    Facts up = Party(0.0f, 0.0f);
    StartWalkBack(r, up);
    CHECK(r.strandDue && r.walkBackIdx == IndexOf(plan, 590.0f, 0.0f) &&
        r.note.find("is up behind the run's start step") != std::string::npos);
    // The failed tries stay counted.
    Run t = Start(&plan, wipe);
    t.startStep = start;
    t.strandStep = wipe;
    t.strandCountStep = wipe;
    t.strandTries = kStrandTries;
    Facts g = Party(0.0f, 0.0f); g.dead = 3; g.alive = 2;
    Tick(t, g);
    CHECK(t.strandStep == static_cast<std::size_t>(-1) && t.strandTries == kStrandTries && t.strandCountStep == wipe);
    StartWalkBack(t, Party(0.0f, 0.0f));
    CHECK(!t.strandDue);   // the step's tries are spent: the walk, as before
}

// The lead bot died and came back to life away from the party (LeadRevivedAway): the same re-arm, so its walk back
// may teleport again on the same step.
static void TestLeadRevivedAwayRearmsTheRescue()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    std::size_t const start = side + 1;
    Run r = Start(&plan, wipe);
    r.startStep = start;
    r.strandStep = wipe;
    r.leadDied = true;
    r.leadDeathX = plan.steps[wipe].x; r.leadDeathY = plan.steps[wipe].y; r.leadDeathZ = 0.0f;
    Facts f = Party(0.0f, 0.0f);   // alive, at the entrance, far from where it fell
    Tick(r, f);
    CHECK(r.regroup && r.note.find("came back to life away from the party") != std::string::npos);
    CHECK(r.strandStep == static_cast<std::size_t>(-1));
}

// Hateforge Quarry r32 slot E (placed at step 83, six wipes at step 224) and Windhorn Canyon r34 slot B (placed at 727,
// wipes at 1025): after each wipe the placed run's rescue teleported the lead bot to the FIRST safe point after the
// start (steps 84 and 734), and the walk back replayed the whole placed route to the wipe (270-525 s a wipe). It goes
// to the safe point on the way it came instead (SafePointAtOrBefore of LastReachedBefore, as every other rescue).
static void TestPlacedRescueGoesToTheSafePointByTheWipe()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    std::size_t const start = side + 1; // placed at the main route's first point, 310 0
    std::size_t const safe = IndexOf(plan, 590.0f, 0.0f);
    Run r = Start(&plan, wipe);
    r.startStep = start;
    Facts f = Party(0.0f, 0.0f); // revived at the entrance, behind the start step
    StartWalkBack(r, f);
    CHECK(r.strandDue && r.strandIdx == safe && r.walkBackIdx == safe);
    CHECK(r.note == "walk back: the lead bot is up behind the run's start step (step " + std::to_string(start + 1) +
        "; the plan point nearest it is step 1): it is teleported to the safe point on the way it came (step " +
        std::to_string(safe + 1) + ") out of combat");
    WalkBackSim const sim = SimulateWalkBack(r, f);
    CHECK(r.state == State::Running && !r.walkBack && r.step == wipe);
    CHECK(sim.teleports == 1 && sim.lowest >= safe);
    CHECK(sim.walked < 60.0f); // 590 0 to the stand at 608 0, not the route from 310 0
    std::cout << "placed rescue by the wipe: walked " << sim.walked << " yd after the teleport (old: from step "
              << start + 1 << ", " << RouteYards(plan, start, wipe, 310.0f, 0.0f) << " yd)\n";
    // The route checkpoint known and before the step: the safe point at or before it (the stand 608 0 is the
    // checkpoint: back to 590 0, never the stand beside the pack that wiped the party).
    Run c = Start(&plan, wipe);
    c.startStep = start;
    c.routeKnown = true;
    c.checkpoint = wipe - 1;
    StartWalkBack(c, f);
    CHECK(c.strandDue && c.strandIdx == safe);
    // A checkpoint before the start step (never a point of this run): the first safe point from the start, as before.
    Run b = Start(&plan, wipe);
    b.startStep = start;
    b.routeKnown = true;
    b.checkpoint = 0;
    StartWalkBack(b, f);
    CHECK(b.strandDue && b.strandIdx == start);
}

// The fall-back to the first safe point from the start step: a direct step (a navmesh hole) between the start and the
// last point reached, and every point after it by a live pack: no safe point on the way it came (ClearPointBefore
// never goes back over a direct step), so the first safe point from the start, as before.
static void TestPlacedRescueFallsBackToTheFirstSafePoint()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0), Move(100, 0), Move(300, 0), Direct(340, 0), Move(600, 0), Pull(610, 0, 9)};
    Run r = Start(&plan, 5);
    r.startStep = 2;
    Facts f = Party(0, 0);
    StartWalkBack(r, f);
    CHECK(r.strandDue && r.strandIdx == 2);
    CHECK(r.note == "walk back: the lead bot is up behind the run's start step (step 3; the plan point nearest it is "
        "step 1): it is teleported to step 3 out of combat");
    // With the pack away from the last point: the last point itself, past the direct step.
    DungeonPlan far = plan;
    far.steps[5] = Pull(610, 100, 9);
    Run g = Start(&far, 5);
    g.startStep = 2;
    StartWalkBack(g, f);
    CHECK(g.strandDue && g.strandIdx == 4);
}

// Windhorn Canyon r34 slot B wipe 3 and Hateforge Quarry r32 slot E wipe 5: the wipe past kAutoRegroupsPerStep paused
// ('regroups by itself at this step did not hold'); the resume found the rescue's latch used by the wipe before
// (Run::strandStep), so no rescue fired and the walk back replayed the placed route from its start (Windhorn: '8 hops
// did not reach the point of step 828: replaying from the first point (step 727)', fights with respawned packs, 9
// min; Hateforge: 84-223, 525 s). Each death pause arms the rescue again; the resume itself still does not.
static void TestDeathPauseRearmsTheRescue()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    std::size_t const start = side + 1;
    std::size_t const safe = IndexOf(plan, 590.0f, 0.0f);
    // The wipe's pause.
    Run r = Start(&plan, wipe);
    r.startStep = start;
    r.strandStep = wipe;                      // the wipe before was rescued on this step
    r.autoRegroups = kAutoRegroupsPerStep;    // both regroups by itself used
    Facts f = Party(0.0f, 0.0f);
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("3 dead: regroup (cmd revive party), then .bot run resume") == 0);
    CHECK(r.strandStep == static_cast<std::size_t>(-1));
    CHECK(Resume(r));
    StartWalkBack(r, Party(0.0f, 0.0f));
    CHECK(r.strandDue && r.strandIdx == safe);
    // LeadRevivedAway's pause.
    Run a = Start(&plan, wipe);
    a.startStep = start;
    a.strandStep = wipe;
    a.autoRegroups = kAutoRegroupsPerStep;
    a.leadDied = true;
    a.leadDeathX = plan.steps[wipe].x;
    a.leadDeathY = plan.steps[wipe].y;
    a.leadDeathZ = 0.0f;
    Tick(a, Party(0.0f, 0.0f));
    CHECK(a.state == State::Paused && a.note.find("the lead bot died and came back") == 0);
    CHECK(a.strandStep == static_cast<std::size_t>(-1));
    // WaitForFallen's pause (the lead bot dead).
    Run w = Start(&plan, wipe);
    w.startStep = start;
    w.strandStep = wipe;
    w.autoRegroups = kAutoRegroupsPerStep;
    Facts d = Party(0.0f, 0.0f);
    d.leadAlive = false;
    d.dead = 1;
    d.alive = 4;
    Tick(w, d);
    CHECK(w.state == State::Paused && w.note.find("the lead bot is dead: revive it") == 0);
    CHECK(w.strandStep == static_cast<std::size_t>(-1));
    // The failed tries of the step stay counted: spent, no teleport after the resume.
    Run t = Start(&plan, wipe);
    t.startStep = start;
    t.strandStep = wipe;
    t.strandCountStep = wipe;
    t.strandTries = kStrandTries;
    t.autoRegroups = kAutoRegroupsPerStep;
    Tick(t, f);
    CHECK(t.state == State::Paused && t.strandStep == static_cast<std::size_t>(-1) && t.strandTries == kStrandTries);
    CHECK(Resume(t));
    StartWalkBack(t, Party(0.0f, 0.0f));
    CHECK(!t.strandDue);
}

// The kept design: a resume (ResetStep) does NOT re-arm the latch.
static void TestResumeKeepsTheRescueLatch()
{
    std::size_t wipe = 0, side = 0;
    DungeonPlan plan = GnomereganShape(wipe, side);
    std::size_t const start = side + 1;
    Run r = Start(&plan, wipe);
    r.startStep = start;
    r.strandStep = wipe;
    Pause(r, "test");
    CHECK(Resume(r));
    CHECK(r.strandStep == wipe);
    StartWalkBack(r, Party(0.0f, 0.0f));
    CHECK(!r.strandDue);
}

// ta268 Part A: a fight-ceiling pause follows the lead bot while it fights.
static DungeonPlan CeilingPausePlan()
{
    DungeonPlan plan;
    plan.steps = {Move(0, 0), Move(20, 0), Direct(40, 0), Move(60, 0), Move(80, 0), Pull(100, 0, 7206)};
    return plan;
}

static void TestCeilingPauseFollowsTheFightingLeadBot()
{
    DungeonPlan plan = CeilingPausePlan();
    Run r = Start(&plan, 5);
    NoteCheckpoint(r, 4);
    r.leadSeen = true;
    r.leadLastX = 95;
    r.leadLastY = 0;
    r.leadLastZ = 0;
    Pause(r, "a fight lasted more than 180 s");
    r.ceilingPaused = true;
    TrackRouteWhilePaused(r, true, 98, 0, 0);
    TrackRouteWhilePaused(r, true, 101, 3, 0);
    TrackRouteWhilePaused(r, true, 104, 8, 0);
    TrackRouteWhilePaused(r, true, 106, 12, 0);
    CHECK(CeilingFightOverWhilePaused(r, false, kCeilingQuietMs));
    Facts f = Party(106, 12, 0);
    f.total = 5;
    f.alive = 5;
    Tick(r, f);
    CHECK(CheckpointKnown(r));
    CHECK(FirstUncrossedDirect(r) == static_cast<std::size_t>(-1));
    // A player's pause never follows, even when armed as a ceiling one: only the guard is tested.
    Run p = Start(&plan, 5);
    NoteCheckpoint(p, 4);
    p.leadSeen = true;
    p.leadLastX = 95;
    p.leadLastY = 0;
    p.leadLastZ = 0;
    PausePlayer(p, "player order (cmd move): .bot run resume to go on");
    p.ceilingPaused = true;
    TrackRouteWhilePaused(p, true, 98, 0, 0);
    TrackRouteWhilePaused(p, true, 101, 3, 0);
    TrackRouteWhilePaused(p, true, 104, 8, 0);
    TrackRouteWhilePaused(p, true, 106, 12, 0);
    CHECK(p.routeSpotX == 95.0f);
    // No ceiling flag (an ordinary pause, not a fight ceiling's): no follow either, even step by step. This guards
    // the `!r.ceilingPaused` check in TrackRouteWhilePaused: dropping it tracks here and must fail the check below.
    Run q = Start(&plan, 5);
    NoteCheckpoint(q, 4);
    q.leadSeen = true;
    q.leadLastX = 95;
    q.leadLastY = 0;
    q.leadLastZ = 0;
    Pause(q, "a fight lasted more than 180 s");
    TrackRouteWhilePaused(q, true, 98, 0, 0);
    TrackRouteWhilePaused(q, true, 101, 3, 0);
    TrackRouteWhilePaused(q, true, 104, 8, 0);
    TrackRouteWhilePaused(q, true, 106, 12, 0);
    CHECK(q.routeSpotX == 95.0f);
}

static void TestCeilingPauseDropsTheRouteOnAJump()
{
    DungeonPlan plan = CeilingPausePlan();
    Run r = Start(&plan, 5);
    NoteCheckpoint(r, 4);
    r.leadSeen = true;
    r.leadLastX = 95;
    r.leadLastY = 0;
    r.leadLastZ = 0;
    Pause(r, "a fight lasted more than 180 s");
    r.ceilingPaused = true;
    TrackRouteWhilePaused(r, true, 98, 0, 0);
    TrackRouteWhilePaused(r, true, 140, 0, 0); // a 42 yd jump: a teleport, a GM move, a fall
    CHECK(CeilingFightOverWhilePaused(r, false, kCeilingQuietMs));
    Tick(r, Party(140, 0, 0));
    CHECK(!CheckpointKnown(r));
}

static void TestCeilingPauseKeepsTheDeadLeadBotsSpot()
{
    DungeonPlan plan = CeilingPausePlan();
    Run r = Start(&plan, 5);
    NoteCheckpoint(r, 4);
    r.leadSeen = true;
    r.leadLastX = 95;
    r.leadLastY = 0;
    r.leadLastZ = 0;
    Pause(r, "a fight lasted more than 180 s");
    r.ceilingPaused = true;
    TrackRouteWhilePaused(r, false, 80, 0, 0); // dead: no follow, although only 15 yd away
    TrackRouteWhilePaused(r, true, 55, 0, 0);  // alive again 40 yd away: a jump, no follow
    CHECK(CeilingFightOverWhilePaused(r, false, kCeilingQuietMs));
    Tick(r, Party(55, 0, 0));
    CHECK(!CheckpointKnown(r));
}

// ta268 Part B: a long hole-branch replay is a rescue (bot-only party).
static DungeonPlan HoleShape(std::size_t& wipe, std::size_t& hole)
{
    DungeonPlan plan;
    plan.name = "hole-shape";
    for (int i = 0; i <= 5; ++i)
        plan.steps.push_back(Move(20.0f * i, 0.0f)); // 0 .. 100
    plan.steps.push_back(Direct(110.0f, 0.0f));
    hole = plan.steps.size() - 1;
    for (int i = 0; i <= 20; ++i)
        plan.steps.push_back(Move(120.0f + 20.0f * i, 0.0f)); // 120 .. 520
    plan.steps.push_back(Pull(540.0f, 0.0f, 7300));
    wipe = plan.steps.size() - 1;
    return plan;
}

static void TestLongHoleReplayIsRescued()
{
    std::size_t wipe = 0, hole = 0;
    DungeonPlan plan = HoleShape(wipe, hole);
    (void)hole;
    Run r = Start(&plan, wipe);
    Facts f = Party(0, 0); // revived at the entrance, route unknown
    StartWalkBack(r, f);
    CHECK(r.strandDue && r.strandIdx == IndexOf(plan, 500.0f, 0.0f));
    CHECK(r.note.find("a direct step is on the way (step 7) and the plan's points from step 6 would walk") !=
        std::string::npos);
    // The arrival itself records the teleport point as the checkpoint, past the hole: fire the send, move the lead
    // bot to the target as the glue would, then let the settle elapse on the point, before the walk re-checkpoints
    // the later points on its own.
    Facts g = Party(0, 0);
    std::string order;
    AnswerRescueScan(r, g);
    CHECK(WalkBack(r, g, kTickMs, order)); // the send: strandOn, settling (order "rescue from ... to ...")
    CHECK(!r.strandDue && r.strandOn);
    g.leadX = plan.steps[r.strandIdx].x;
    g.leadY = plan.steps[r.strandIdx].y;
    g.leadZ = plan.steps[r.strandIdx].z;
    CHECK(WalkBack(r, g, kStrandSettleMs, order)); // settled on the point: the checkpoint
    CHECK(CheckpointKnown(r) && r.checkpoint == r.strandIdx);
    CHECK(FirstUncrossedDirect(r) == static_cast<std::size_t>(-1));
    WalkBackSim const sim = SimulateWalkBack(r, g);
    CHECK(sim.walked < 60.0f && !r.walkBack && r.step == wipe);
}

static void TestShortHoleReplayAndPlayersKeepTheReplay()
{
    std::size_t wipe = 0, hole = 0;
    DungeonPlan longPlan = HoleShape(wipe, hole);
    (void)hole;
    // (a) A short replay: walked, not rescued.
    DungeonPlan plan;
    for (int i = 0; i <= 5; ++i)
        plan.steps.push_back(Move(20.0f * i, 0.0f)); // 0 .. 100
    plan.steps.push_back(Direct(110.0f, 0.0f));
    for (int i = 0; i <= 3; ++i)
        plan.steps.push_back(Move(120.0f + 20.0f * i, 0.0f)); // 120 .. 180
    plan.steps.push_back(Pull(200.0f, 0.0f, 7300));
    Run a = Start(&plan, plan.steps.size() - 1);
    StartWalkBack(a, Party(90, 0));
    CHECK(!a.strandDue && a.walkBack && a.walkBackDirect && a.walkBackStart == 5);
    // (b) A real player in the party walks back with it.
    Facts withPlayer = Party(0, 0);
    PlayerFact p;
    p.name = "Hypeer";
    p.yd = 5.0f;
    withPlayer.players.push_back(p);
    Run b = Start(&longPlan, wipe);
    StartWalkBack(b, withPlayer);
    CHECK(!b.strandDue && b.walkBack && b.walkBackStart == 5);
    // (c) The rescue off.
    Facts off = Party(0, 0);
    off.walkBackRescue = false;
    Run c = Start(&longPlan, wipe);
    StartWalkBack(c, off);
    CHECK(!c.strandDue);
    // (d) The only safe point is the direct step itself: before the hole, so the replay as before.
    DungeonPlan tight;
    for (int i = 0; i <= 20; ++i)
        tight.steps.push_back(Move(20.0f * i, 0.0f)); // 0 .. 400
    tight.steps.push_back(Direct(410.0f, 0.0f));
    tight.steps.push_back(Pull(440.0f, 0.0f, 7301, 5.0f));
    Run d = Start(&tight, tight.steps.size() - 1);
    StartWalkBack(d, Party(0, 0));
    CHECK(!d.strandDue && d.walkBack && d.walkBackStart == 20);
}

// ta268 Part C: one "walk back: " prefix.
static void TestWalkBackNotePrefixOnce()
{
    CHECK(WalkBackNote("gathering before a pull") == "walk back: gathering before a pull");
    CHECK(WalkBackNote(WalkBackNote("x")) == "walk back: x");
}
int main()
{
    TestWipeWalkBackOnAGnomereganShapedPlan();
    TestReplayLeavesOutTheFoldedSideLeg();
    TestPlacedRunNeverWalksBackBeforeItsStartStep();
    TestStraightTargetBacksOffBeforeTheReplay();
    TestRouteShortcutRules();
    TestIncompletePathToTheTargetReplaysThePlansPoints();
    TestStuckWalkBackIsRescuedNotPaused();
    TestLongReplayIsRescuedInAFullRun();
    TestShortReplayAndPlayersKeepTheReplay();
    TestRefusedReplayRescueReplaysThePlansPoints();
    TestJoinedPlayerCancelsTheReplayRescue();
    TestReplayRescueNeedsDistanceToTheTarget();
    TestReplayRescueAfterEveryStraightFailure();
    TestBackoffToTheEntranceIsRescued();
    TestRetreatAtTwoDead();
    TestRestLinesFollowTheNextFight();
    TestMealWaitUnderTheLightLines();
    TestFightForwardPullsFromWhereTheTankStands();
    TestSpiralRampWalkIsNotStuck();
    TestPathEndsShortOnlyFarFromThePoint();
    TestStuckRescueGoesToASafePoint();
    TestFightForwardNeedsTheGluesRead();
    TestRoundOneRestAndShortcutNotes();
    TestPlacedRescueTargetIsSafe();
    TestSecondWipeOnTheStepIsRescuedToo();
    TestLeadRevivedAwayRearmsTheRescue();
    TestResumeKeepsTheRescueLatch();
    TestPlacedRescueGoesToTheSafePointByTheWipe();
    TestPlacedRescueFallsBackToTheFirstSafePoint();
    TestDeathPauseRearmsTheRescue();
    TestCeilingPauseFollowsTheFightingLeadBot();
    TestCeilingPauseDropsTheRouteOnAJump();
    TestCeilingPauseKeepsTheDeadLeadBotsSpot();
    TestLongHoleReplayIsRescued();
    TestShortHoleReplayAndPlayersKeepTheReplay();
    TestWalkBackNotePrefixOnce();
    std::cout << "test_run_time_sinks_policy: ok\n";
    return 0;
}
