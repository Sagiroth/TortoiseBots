// Standalone checks for the fixes from the 2026-10-16 Uldaman run-leader live test (server build 19z10):
//   1. `cmd move` takes the floor at or just below the point before a surface above it (a low ceiling),
//      and never lifts a point in water out of the water (Karazhan Crypt kc316, 2026-10-28);
//   2. a direct step: a follower off the lead bot's line walks its own straight line, and one the step
//      still refuses is summoned before the step goes out again;
//   3. after a regroup the party walks the plan's way back (not straight at the pack), replaying the
//      plan's points at once when a direct step is on the way;
//   4. a door whose lock is of an allowlisted type (open, quick open, slow open) opens on a plain use;
//   5. a hold counts the event's own units, not every dormant statue of its entries.
//
//   g++ -std=c++17 -Wall -Wextra tools/test_run_leader_live_policy.cpp -o /tmp/t && /tmp/t
#include "../runtime/DungeonRunPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"
#include "../ai/playerbot/DoorLockPolicy.h"
#include "../ai/playerbot/SwimPathPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;
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

static DungeonRun::Facts Party(float x = 0, float y = 0)
{
    DungeonRun::Facts f;
    f.total = 5;
    f.alive = 5;
    f.leadX = x;
    f.leadY = y;
    f.carrier = "Nobble";
    f.seen = true;
    f.leadAlive = true;
    return f;
}

static void TestFloorSnap()
{
    using namespace RemoteCommander;
    // The tunnel north of the Sentinel's hall: point -43.0, floor -43.5, ceiling 5.3 yd up (-38.2).
    CHECK(ChooseFloorZ(-43.0f, -43.5f, true, -38.2f, true) == -43.5f);
    // The old search alone took the ceiling: within kFloorSnapRange of the point.
    CHECK(SnapToFloorZ(-43.0f, -38.2f, true) == -38.2f);
    // Nothing at or below the point (it lies under the floor): the search from above counts.
    CHECK(ChooseFloorZ(-45.0f, 0.0f, false, -43.5f, true) == -43.5f);
    // A floor far below (the core's unbounded fallback) is not "just below": the search from above.
    CHECK(!FloorBelowUsable(-43.0f, -60.0f, true));
    CHECK(ChooseFloorZ(-43.0f, -60.0f, true, -43.2f, true) == -43.2f);
    // The band edges: kFloorBelowBand below, kFloorBelowHeadroom above (the core's ray starts there).
    CHECK(FloorBelowUsable(-43.0f, -43.0f - kFloorBelowBand, true));
    CHECK(FloorBelowUsable(-43.0f, -43.0f + kFloorBelowHeadroom, true));
    CHECK(!FloorBelowUsable(-43.0f, -43.0f + kFloorBelowHeadroom + 0.1f, true));
    // Neither found: the point keeps its own z.
    CHECK(ChooseFloorZ(-43.0f, 0.0f, false, 0.0f, false) == -43.0f);
}

// Karazhan Crypt kc316 (2026-10-28, step 292, leg 6 corpsemuncher): the plan's water row S2
// (-11205 -1514.5 -0.5) lies in open water 3.5 yd under the shaft's WMO water surface (3.0), the shaft
// floor 46 yd below it (nothing in the below band) and a model surface at 5.2 within kFloorSnapRange above
// it. `cmd move` lifted the point onto that surface ("Move point snapped to the floor: -11205.0 -1514.5
// 5.2", 4 times). From the lead floating at -11209 -1522.5 -26.5 the core then answered 0x11 for the dry
// end, the swim hand-off's 0x11 line needs the end in water, so the lead's own move failed and the run
// paused ("stuck 27.5 yd ... cmd move path type 0x11"); from S1 the same point was refused off-mesh
// ("the path ends more than 5 yd from it"). Reachability decides (Codex review of 3ad0950d: a pier over
// the water looks the same by height and liquid): the water point keeps its z only when the raised point
// has no usable path and the water point is swimmable.
static void TestFloorSnapKeepsAWaterPointInTheWater()
{
    using namespace RemoteCommander;
    float const level = 3.0f;
    float const s2x = -11205.0f, s2y = -1514.5f, s2z = -0.5f;
    // The rare case is asked only for a water point the search from above would lift out of the water.
    CHECK(WaterSnapAsksPath(s2z, -45.9f, true, 5.2f, true, true, level));
    CHECK(WaterSnapAsksPath(s2z, 0.0f, false, 5.2f, true, true, level));
    CHECK(!WaterSnapAsksPath(s2z, -45.9f, true, 5.2f, true, false, level));   // not in water
    CHECK(!WaterSnapAsksPath(s2z, -3.0f, true, 5.2f, true, true, level));     // a floor in the below band
    CHECK(!WaterSnapAsksPath(s2z, 0.0f, false, 1.0f, true, true, level));     // a ledge under the surface
    CHECK(!WaterSnapAsksPath(s2z, 0.0f, false, level, true, true, level));    // exactly at the surface
    CHECK(!WaterSnapAsksPath(s2z, 0.0f, false, 6.0f, true, true, level));     // beyond kFloorSnapRange
    CHECK(!WaterSnapAsksPath(s2z, 0.0f, false, 0.0f, false, true, level));    // nothing found above
    // The reachability fact: the live answers for the raised S2 (0x11 from mid-water; a path ending
    // more than 5 yd off it from S1) are unusable; a complete path onto it, an incomplete (length-capped)
    // one, or the lead bot already on it are usable; NOPATH is not.
    CHECK(!RaisedFloorPathUsable(0x11, 0.0f, false));
    CHECK(!RaisedFloorPathUsable(0x01, 6.6f, false));
    CHECK(!RaisedFloorPathUsable(kPathNoPath, 0.0f, false));
    CHECK(RaisedFloorPathUsable(0x01, 0.4f, false));
    CHECK(RaisedFloorPathUsable(0x01, kMaxMoveEndOffset, false));
    CHECK(RaisedFloorPathUsable(kPathIncomplete, 30.0f, false));
    CHECK(RaisedFloorPathUsable(kPathNoPath, 0.0f, true));

    // S2: the raised point unreachable, the water point swimmable -> the water point.
    CHECK(ChooseFloorZ(s2z, -45.9f, true, 5.2f, true, true, level, true, false) == s2z);
    CHECK(ChooseFloorZ(s2z, 0.0f, false, 5.2f, true, true, level, true, false) == s2z);
    // A pier: a point 1.5 yd under a deck at 4.0 over water at 3.0, the lead bot has a path onto the deck
    // -> the deck, as before.
    CHECK(ChooseFloorZ(2.5f, 0.0f, false, 4.0f, true, true, level, true, true) == 4.0f);
    // Neither usable (the raised point unreachable, the water point too shallow to swim) -> the old rule.
    CHECK(ChooseFloorZ(2.5f, 0.0f, false, 4.0f, true, true, level, false, false) == 4.0f);
    // Not in water, or no water facts (every other caller): the old snap onto the surface above.
    CHECK(ChooseFloorZ(s2z, -45.9f, true, 5.2f, true, false, level, true, false) == 5.2f);
    CHECK(ChooseFloorZ(s2z, -45.9f, true, 5.2f, true) == 5.2f);
    CHECK(ChooseFloorZ(s2z, -45.9f, true, 5.2f, true, true, level) == 5.2f); // reachability not asked: old rule
    // A ledge above the point but still under the surface: snapped as before (it stays in the water).
    CHECK(ChooseFloorZ(s2z, 0.0f, false, 1.0f, true, true, level, true, false) == 1.0f);
    CHECK(ChooseFloorZ(s2z, 0.0f, false, level, true, true, level, true, false) == level);
    // A floor in the below band (a lake bed under the point) wins first, in water or not.
    CHECK(ChooseFloorZ(s2z, -3.0f, true, 5.2f, true, true, level, true, false) == -3.0f);
    // Nothing found: the point keeps its z.
    CHECK(ChooseFloorZ(s2z, 0.0f, false, 0.0f, false, true, level, true, false) == s2z);

    // The move the lead can then execute: from its logged spots in the shaft (kc316 12:29:04 "stranded at
    // -11209.0 -1522.5 -26.5", the shaft floor F1, S1, and deep in the shaft's south half) the end is in water
    // (the glue samples it 0.5 yd up), so the core's 0x11 (no polygon in reach) or 0x81 (both ends swimmable)
    // answer is handed to the core as a straight swim (SwimHandoff CoreLine).
    struct Spot { float x, y, z; };
    Spot const spots[] = { { -11209.0f, -1522.5f, -26.5f }, { -11211.0f, -1522.0f, -45.9f },
        { -11211.0f, -1516.0f, -3.0f }, { -11215.0f, -1560.0f, -30.0f } };
    std::uint32_t const types[] = { ai::kPathTypeNormal | ai::kPathTypeNotUsingPath,
        ai::kPathTypeNormal | ai::kPathTypeUnderwater };
    for (Spot const& p : spots)
        for (std::uint32_t const type : types)
        {
            float const endZ = ChooseFloorZ(s2z, -45.9f, true, 5.2f, true, true, level, true,
                RaisedFloorPathUsable(0x11, 0.0f, false));
            ai::SwimHandoffFacts f;
            f.inWater = true;
            f.routeEndFromBot = 0.0f; // the own route refused: no progress
            f.endFromBot = std::sqrt((s2x - p.x) * (s2x - p.x) + (s2y - p.y) * (s2y - p.y) + (endZ - p.z) * (endZ - p.z));
            f.coreType = type;
            f.swimmableHere = true;
            f.endInWater = endZ + 0.5f < level;
            f.endSwimmable = f.endInWater;
            CHECK(ai::ChooseSwimHandoff(f) == ai::SwimHandoff::CoreLine);
            // The lifted point (5.2) was out of the water: never a swim line to it.
            ai::SwimHandoffFacts lifted = f;
            lifted.endInWater = 5.2f + 0.5f < level;
            lifted.endSwimmable = false;
            lifted.coreType = ai::kPathTypeNormal | ai::kPathTypeNotUsingPath;
            CHECK(ai::ChooseSwimHandoff(lifted) != ai::SwimHandoff::CoreLine);
        }
}

static void TestDirectOwnLine()
{
    using namespace RemoteCommander;
    // Step 53: a follower on the line of step 52, over the navmesh hole, 6 yd from the lead, no path to it.
    CHECK(OwnDirectLineFits(14.0f, -0.4f, true));
    CHECK(ChooseDirectJoin(6.0f, -0.4f, true, false, -1.0f, true) == DirectJoin::Own);
    // The lead bot's line still comes first.
    CHECK(ChooseDirectJoin(6.0f, -0.4f, true, true, 8.0f, true) == DirectJoin::Path);
    CHECK(ChooseDirectJoin(3.0f, 0.2f, true, false, -1.0f, true) == DirectJoin::Straight);
    // On the pit floor 2.5 yd below the ramp (the reason for the lead bot's line): never its own line.
    CHECK(!OwnDirectLineFits(10.0f, -2.5f, true));
    CHECK(ChooseDirectJoin(6.0f, -2.5f, true, false, -1.0f, false) == DirectJoin::None);
    // Out of sight of the point, or out of a direct step's reach: none.
    CHECK(!OwnDirectLineFits(10.0f, 0.0f, false));
    CHECK(!OwnDirectLineFits(kDirectMaxDistance + 0.1f, 0.0f, true));
    CHECK(OwnDirectLineFits(kDirectMaxDistance, kDirectOwnLineVertical, true));
}

static void TestDirectSummon()
{
    using namespace DungeonRun;
    CHECK(DirectStragglerName("too-far: Elia is not with Nobble (no navmesh path of at most 40 yd)") == "Elia");
    CHECK(DirectStragglerName("too-far: The point is more than 25 yd").empty());
    CHECK(DirectStragglerName("no-los: Nobble has no line of sight").empty());

    DungeonPlan plan;
    plan.name = "t";
    plan.steps = {Step(PlanStepKind::MoveDirect, 10, 0, 0), Step(PlanStepKind::MoveDirect, 20, 0, 0)};
    Run r = Start(&plan, 1);
    Facts f = Party(10);
    std::string order = Tick(r, f);
    CHECK(order == "move 20.0 0.0 0.0 direct");
    std::string const answer = "too-far: Elia is not with Nobble (no navmesh path of at most 40 yd to its spot)";
    OrderRefused(r, order, answer);
    CHECK(r.state == State::Running);
    MemberFact elia;
    elia.name = "Elia";
    elia.yd = 12.0f;
    f.members = {elia};
    CHECK(Tick(r, f) == "summon Elia");
    // The summon is the 3 s `.bot summon` flow: on the tick after it Elia is still away, so the step waits.
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running);
    // `busy` (her summon pending) keeps the wait.
    OrderRefused(r, "summon Elia", "busy: That bot already has a summon pending.");
    CHECK(r.state == State::Running);
    CHECK(Tick(r, f).empty());
    // She stands at the lead bot: the step goes out again.
    f.members[0].yd = 1.5f;
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0 direct");
    // Refused for the same bot again at once: no second summon before kDirectResummonMs and no pause before two
    // summons and kDirectPauseMs (r12-19z29, the summon-once rule paused here): the run waits for her.
    OrderRefused(r, "move 20.0 0.0 0.0 direct", answer);
    CHECK(r.state == State::Running && r.directWaitFor == "Elia");
    CHECK(Tick(r, f).empty());
    // More than kDirectResummonMs later: summoned again.
    r.runMs += kDirectResummonMs;
    r.directWaitFor.clear();
    OrderRefused(r, "move 20.0 0.0 0.0 direct", answer);
    CHECK(r.state == State::Running && r.summonDue == "Elia");
    // The wait is capped: a summoned bot that never arrives lets the step go out after kSummonWaitMs.
    Run c = Start(&plan, 1);
    Facts far = Party(10);
    far.members = {elia};
    CHECK(Tick(c, far) == "move 20.0 0.0 0.0 direct");
    OrderRefused(c, "move 20.0 0.0 0.0 direct", answer);
    CHECK(Tick(c, far) == "summon Elia");
    std::uint32_t waited = 0;
    std::string next;
    while (next.empty() && waited <= kSummonWaitMs + kTickMs)
    {
        next = Tick(c, far);
        waited += kTickMs;
    }
    CHECK(next == "move 20.0 0.0 0.0 direct");
    CHECK(waited >= kSummonWaitMs);
    // Any other refusal of the summon ends the wait at once.
    Run u = Start(&plan, 1);
    CHECK(Tick(u, far) == "move 20.0 0.0 0.0 direct");
    OrderRefused(u, "move 20.0 0.0 0.0 direct", answer);
    CHECK(Tick(u, far) == "summon Elia");
    OrderRefused(u, "summon Elia", "unavailable: That bot is dead, in combat, on a taxi or teleporting.");
    CHECK(Tick(u, far) == "move 20.0 0.0 0.0 direct");
    // A plain move is never answered with a summon.
    Run m = Start(&plan, 1);
    OrderRefused(m, "move 20.0 0.0 0.0", answer);
    CHECK(m.state == State::Paused);
}

// uldaman-events-4 (2026-10-17, 14:03 and 14:39): the lead bot died and came back at the entrance; the two
// still alive where it fell were held there (Run::hold) while it walked back. At the scorpid pit's direct step the
// module named a held bot (its check counts every bot), the run summoned it, but the wait read the bot from the
// facts without the held bots, saw nobody to wait for and sent the step again 2 s later, before the 3 s summon
// landed: refused again, then the second held bot the same, and the walk back paused with the whole party
// standing on the lip a second later. The bot being waited for is shown to the wait; it stays held until it
// arrives, so a refused, timed-out or vanished summon leaves it held.
namespace HeldSummon
{
using namespace DungeonRun;
static DungeonPlan const& Plan()
{
    static DungeonPlan plan;
    if (plan.steps.empty())
    {
        plan.name = "t";
        plan.steps = {Step(PlanStepKind::Move, 10, 0, 0), Step(PlanStepKind::MoveDirect, 14, 0, 0),
            Step(PlanStepKind::Move, 200, 0, 0)};
    }
    return plan;
}
static std::string const kStep = "move 14.0 0.0 0.0 direct";
static std::string Named(std::string const& bot)
{
    return "too-far: " + bot + " is not with Nobble (no navmesh path of at most 40 yd to its spot)";
}
static Run Walking()
{
    Run r = Start(&Plan(), 2);
    r.walkBack = true;
    r.walkBackIdx = 1;
    r.hold = {"Elia", "Quick"};
    return r;
}
static Facts Scene()
{
    Facts f = Party(10);
    MemberFact lead;
    lead.name = "Nobble";
    MemberFact elia;
    elia.name = "Elia";
    elia.yd = 320.0f;
    MemberFact quick = elia;
    quick.name = "Quick";
    f.members = {lead, elia, quick};
    f.maxMemberYd = 320.0f;
    return f;
}
static bool Held(Run const& r, std::string const& bot)
{
    return std::find(r.hold.begin(), r.hold.end(), bot) != r.hold.end();
}
// Up to the summon of Elia, the first one named.
static void ToSummon(Run& r, Facts const& f)
{
    CHECK(Tick(r, f) == kStep); // the held bots do not hold up the walk back: no gather
    OrderRefused(r, kStep, Named("Elia"));
    CHECK(r.state == State::Running);
    CHECK(Tick(r, f) == "summon Elia");
    CHECK(Held(r, "Elia")); // held until she arrives
}
}

static void TestDirectSummonHeldBot()
{
    using namespace DungeonRun;
    using namespace HeldSummon;
    // The 14:39 case: both held bots named in turn, each summoned and waited for.
    {
        Run r = Walking();
        Facts f = Scene();
        ToSummon(r, f);
        CHECK(Tick(r, f).empty());
        CHECK(Tick(r, f).empty());
        CHECK(r.note.find("waiting for Elia") != std::string::npos);
        f.members[1].yd = 0.5f; // she stands at the lead bot: out of the hold, the step goes out again
        CHECK(Tick(r, f) == kStep);
        CHECK(!Held(r, "Elia") && Held(r, "Quick"));
        OrderRefused(r, kStep, Named("Quick"));
        CHECK(Tick(r, f) == "summon Quick");
        CHECK(Tick(r, f).empty());
        f.members[2].yd = 0.5f;
        f.maxMemberYd = 0.5f;
        CHECK(Tick(r, f) == kStep);
        CHECK(r.hold.empty() && r.state == State::Running && r.walkBackRefusals == 0);
    }
    // `busy` (her summon pending) keeps the wait; she stays held.
    {
        Run r = Walking();
        Facts f = Scene();
        ToSummon(r, f);
        OrderRefused(r, "summon Elia", "busy: That bot already has a summon pending.");
        CHECK(Tick(r, f).empty());
        CHECK(r.summonWait == "Elia" && Held(r, "Elia"));
    }
    // Any other refusal ends the wait: she stays held where she is, and the step goes out without a gather of her.
    {
        Run r = Walking();
        Facts f = Scene();
        ToSummon(r, f);
        OrderRefused(r, "summon Elia", "unavailable: That bot is dead, in combat, on a taxi or teleporting.");
        CHECK(Held(r, "Elia") && r.summonWait.empty());
        CHECK(Tick(r, f) == kStep);
        CHECK(Held(r, "Elia"));
    }
    // The summon never lands: after kSummonWaitMs she stays held, and the step goes out again without a gather.
    {
        Run r = Walking();
        Facts f = Scene();
        ToSummon(r, f);
        std::uint32_t waited = 0;
        std::string next;
        while (next.empty() && waited <= kSummonWaitMs + 3 * kTickMs)
        {
            next = Tick(r, f);
            waited += kTickMs;
        }
        CHECK(next == kStep);
        CHECK(waited >= kSummonWaitMs);
        CHECK(Held(r, "Elia") && r.summonWait.empty());
    }
    // 10 yd off (inside kGatherYd, outside kArriveYd): still held and still waited for through the whole wait. Once
    // the wait times out she is under the hold's own rule: a held bot within kGatherYd of the lead bot is with it.
    {
        Run r = Walking();
        Facts f = Scene();
        ToSummon(r, f);
        f.members[1].yd = 10.0f;
        std::uint32_t waited = 0;
        std::string next;
        while (next.empty() && waited <= kSummonWaitMs + 3 * kTickMs)
        {
            if (!r.summonWait.empty())
                CHECK(Held(r, "Elia")); // never released while the summon is waited for
            next = Tick(r, f);
            waited += kTickMs;
        }
        CHECK(next == kStep && waited >= kSummonWaitMs);
        CHECK(!Held(r, "Elia") && r.summonWait.empty());
    }
    // Arrival from 10 yd: within kArriveYd she leaves the hold and the step goes out.
    {
        Run r = Walking();
        Facts f = Scene();
        ToSummon(r, f);
        f.members[1].yd = 10.0f;
        CHECK(Tick(r, f).empty() && Held(r, "Elia"));
        f.members[1].yd = 1.0f;
        CHECK(Tick(r, f) == kStep);
        CHECK(!Held(r, "Elia"));
    }
    // She vanishes from the facts (logged out, off the map): the wait ends; the hold drops her as gone.
    {
        Run r = Walking();
        Facts f = Scene();
        ToSummon(r, f);
        f.members.erase(f.members.begin() + 1);
        CHECK(Tick(r, f) == kStep);
        CHECK(!Held(r, "Elia") && Held(r, "Quick") && r.summonWait.empty());
    }
}

static void TestWalkBackAfterRegroup()
{
    using namespace DungeonRun;
    // The entrance at 0,0. A move to 100,0, the pit's direct steps, the approach stand at 120,60 and the
    // pack at 60,40: from the entrance the pack is nearer in a straight line than its approach stand.
    DungeonPlan plan;
    plan.name = "t";
    PlanStep pull = Step(PlanStepKind::Pull, 60, 40, 0);
    plan.steps = {Step(PlanStepKind::Move, 30, 0, 0), Step(PlanStepKind::Move, 100, 0, 0),
        Step(PlanStepKind::Move, 120, 60, 0), pull};
    Run r = Start(&plan, 3);
    Facts f = Party(0, 0);
    f.seen = false;
    StartWalkBack(r, f);
    CHECK(r.walkBack); // the old rule (pack nearer than the stand) left it to path straight at the pack
    CHECK(r.walkBackIdx == 2);
    CHECK(r.walkBackDirect);

    // A direct step on the way: straight to the last move point before the first direct step (here the only one
    // before it, the nearest), then the plan's own points over the hole. Run 4: not from step 1.
    DungeonPlan pit;
    pit.name = "pit";
    pit.steps = {Step(PlanStepKind::Move, 30, 0, 0), Step(PlanStepKind::MoveDirect, 40, 0, 0),
        Step(PlanStepKind::MoveDirect, 50, 0, 0), Step(PlanStepKind::Move, 120, 60, 0), pull};
    Run p = Start(&pit, 4);
    StartWalkBack(p, f);
    CHECK(p.walkBack);
    CHECK(p.walkBackDirect);
    CHECK(p.walkBackIdx == 0);

    // Standing on the last move point, or near the step itself: no walk back.
    Run q = Start(&plan, 3);
    Facts there = Party(120, 60);
    StartWalkBack(q, there);
    CHECK(!q.walkBack);
    Facts near = Party(70, 40);
    StartWalkBack(q, near);
    CHECK(!q.walkBack);
}

static void TestDoorLock()
{
    using namespace DoorLock;
    // Lock 86 (Uldaman's Temple Doors 124368, 141869): slot 1 a skill entry, lock type 10 (quick open), no skill.
    std::uint32_t types[kLockSlots] = {0, 2, 0, 0, 0, 0, 0, 0};
    std::uint32_t index[kLockSlots] = {0, 10, 0, 0, 0, 0, 0, 0};
    CHECK(OpensOnPlainUse(types, index));
    // The allowlist: OPEN (5), QUICK_OPEN (10), SLOW_OPEN (17); every other lock type is refused, BLASTING (16)
    // and GAHZRIDIAN (15) among them, and the profession skills.
    for (std::uint32_t lockType = 0; lockType <= 20; ++lockType)
    {
        std::uint32_t one[kLockSlots] = {0, 2, 0, 0, 0, 0, 0, 0};
        std::uint32_t idx[kLockSlots] = {0, lockType, 0, 0, 0, 0, 0, 0};
        bool const allowed = lockType == 5 || lockType == 10 || lockType == 17;
        CHECK(OpensOnPlainUse(one, idx) == allowed);
    }
    std::uint32_t blasting[kLockSlots] = {0, 16, 0, 0, 0, 0, 0, 0};
    std::uint32_t gahzridian[kLockSlots] = {0, 15, 0, 0, 0, 0, 0, 0};
    std::uint32_t pick[kLockSlots] = {0, 1, 0, 0, 0, 0, 0, 0};
    CHECK(!OpensOnPlainUse(types, blasting));
    CHECK(!OpensOnPlainUse(types, gahzridian));
    CHECK(!OpensOnPlainUse(types, pick));
    // An allowed slot beside a refused one: refused.
    std::uint32_t mixedTypes[kLockSlots] = {2, 2, 0, 0, 0, 0, 0, 0};
    std::uint32_t mixedIndex[kLockSlots] = {10, 16, 0, 0, 0, 0, 0, 0};
    CHECK(!OpensOnPlainUse(mixedTypes, mixedIndex));
    // A key: the key path, not a plain use.
    std::uint32_t keyTypes[kLockSlots] = {1, 0, 0, 0, 0, 0, 0, 0};
    std::uint32_t keyIndex[kLockSlots] = {5396, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!OpensOnPlainUse(keyTypes, keyIndex));
    // An unknown kind of slot.
    std::uint32_t oddTypes[kLockSlots] = {3, 0, 0, 0, 0, 0, 0, 0};
    std::uint32_t none[kLockSlots] = {};
    CHECK(!OpensOnPlainUse(oddTypes, none));
    // An empty lock.
    CHECK(OpensOnPlainUse(none, none));
}

static DungeonRun::UnitFact EventUnit(std::uint64_t guid, std::uint32_t entry, bool hostile)
{
    DungeonRun::UnitFact u;
    u.guid = guid;
    u.entry = entry;
    u.hostile = hostile;
    return u;
}

static void TestEventCount()
{
    using namespace DungeonRun;
    // The Keepers' hold: entry list 4857, four dormant keepers.
    PlanStep keepers = Step(PlanStepKind::Hold, 101.5f, 271.2f, -26.5f);
    keepers.entries = {4857};
    Run r;
    Facts f = Party();
    for (std::uint64_t g = 1; g <= 4; ++g)
        f.units.push_back(EventUnit(g, 4857, false));
    CountEvent(r, f, keepers);
    CHECK(r.eventTotal == 4);
    CHECK(r.eventUp == 0);
    // One awake, one dead.
    f.units.pop_back();
    f.units[0].hostile = true;
    f.eventDown = 1;
    CountEvent(r, f, keepers);
    CHECK(r.eventTotal == 4);
    CHECK(EventProgress(r) == "1 of 4 down, 1 awake");

    // Archaedas's hold: him and some fifty dormant statues of the other entries in the hall.
    PlanStep archaedas = Step(PlanStepKind::Hold, 94, 267.8f, -52);
    archaedas.entries = {2748, 7309, 7077, 7076, 10120};
    Facts a = Party();
    a.units.push_back(EventUnit(1, 2748, true));
    for (std::uint64_t g = 2; g <= 53; ++g)
        a.units.push_back(EventUnit(g, g % 2 ? 7076 : 7309, false));
    CountEvent(r, a, archaedas);
    CHECK(r.eventTotal == 1); // was 53
    // Two statues wake, one of them dies: both count from then on.
    a.units[1].hostile = true;
    a.eventDown = 1;
    CountEvent(r, a, archaedas);
    CHECK(r.eventTotal == 3);
    CHECK(r.eventUp == 2);
}

int main()
{
    TestFloorSnap();
    TestFloorSnapKeepsAWaterPointInTheWater();
    TestDirectOwnLine();
    TestDirectSummon();
    TestDirectSummonHeldBot();
    TestWalkBackAfterRegroup();
    TestDoorLock();
    TestEventCount();
    std::cout << "test_run_leader_live_policy: all checks passed\n";
    return 0;
}
