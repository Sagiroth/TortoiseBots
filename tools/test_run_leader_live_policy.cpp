// Standalone checks for the fixes from the 2026-10-16 Uldaman run-leader live test (server build 19z10):
//   1. `cmd move` takes the floor at or just below the point before a surface above it (a low ceiling);
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
    // Refused for the same bot again: once per bot and step, so it pauses.
    OrderRefused(r, "move 20.0 0.0 0.0 direct", answer);
    CHECK(r.state == State::Paused);
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
    TestDirectOwnLine();
    TestDirectSummon();
    TestWalkBackAfterRegroup();
    TestDoorLock();
    TestEventCount();
    std::cout << "test_run_leader_live_policy: all checks passed\n";
    return 0;
}
