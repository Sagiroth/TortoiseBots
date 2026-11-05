// Standalone check for runtime/policy/class/RogueSapApproachPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_rogue_sap_approach_policy.cpp -o /tmp/test_rogue_sap && /tmp/test_rogue_sap
#include "../runtime/policy/class/RogueSapApproachPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;

namespace
{

// Open floor: the navmesh path is the straight line.
bool StraightPath(SapPoint, SapPoint to, SapPath& out)
{
    out.push_back(to);
    return true;
}

bool NoPath(SapPoint, SapPoint, SapPath&)
{
    return false;
}

bool Near(float a, float b, float eps = 0.05f)
{
    return std::fabs(a - b) <= eps;
}

// One pack-mate west of the target, the rogue comes from the north: the stand
// point is east of the target, away from the mate, not on the rogue's side.
void TestStandPointOnFarSideOfPack()
{
    SapPoint const target{0.0f, 0.0f};
    SapPoint const rogue{0.0f, 30.0f};
    std::vector<SapPackMate> const mates{{-5.0f, 0.0f, 7.0f}};

    SapApproachPlan const plan = PlanSapApproach(rogue, target, 3.0f, 15.0f, mates, StraightPath);
    CHECK(plan.found);
    CHECK(!plan.viaWaypoint);
    CHECK(Near(plan.stand.x, 3.0f));
    CHECK(Near(plan.stand.y, 0.0f));
    CHECK(SapPointMargin(plan.stand, mates) >= 0.0f);
    // The old stand point (the rogue's side, 3 yd north) sits inside the mate's clearance.
    CHECK(SapPointMargin(SapPoint{0.0f, 3.0f}, mates) < 0.0f);
}

// The navmesh path to the best stand point (and to every point north of the
// target) runs through the pack: that point is rejected and the next one on the
// arc, reached by a clear path, is used instead.
void TestPathThroughClearanceUsesArcPoint()
{
    SapPoint const target{0.0f, 0.0f};
    SapPoint const rogue{0.0f, -30.0f};
    std::vector<SapPackMate> const mates{{-5.0f, 0.0f, 6.0f}};

    auto corridor = [](SapPoint, SapPoint to, SapPath& out)
    {
        if (to.y >= -0.01f)
            out.push_back(SapPoint{-4.0f, 1.0f}); // squeezes past the mate
        out.push_back(to);
        return true;
    };

    SapApproachPlan const plan = PlanSapApproach(rogue, target, 3.0f, 15.0f, mates, corridor);
    CHECK(plan.found);
    CHECK(!plan.viaWaypoint);
    CHECK(plan.stand.y < 0.0f);
    CHECK(plan.stand.x > 0.0f);
    CHECK(plan.pathQueries >= 2);
    // The best stand point by distance from the pack was east, and its path was refused.
    SapPath through{SapPoint{-4.0f, 1.0f}, SapPoint{3.0f, 0.0f}};
    CHECK(!SapPathClear(rogue, through, mates));
}

// Two mates between the rogue and the target: every straight walk to a stand
// point passes them, so the rogue goes round by a detour point on the outer ring.
void TestDetourWhenEveryDirectPathCrossesThePack()
{
    SapPoint const target{0.0f, 0.0f};
    SapPoint const rogue{-30.0f, 0.0f};
    std::vector<SapPackMate> const mates{{-6.0f, 2.0f, 7.0f}, {-6.0f, -2.0f, 7.0f}};

    SapApproachPlan const plan = PlanSapApproach(rogue, target, 3.0f, 15.0f, mates, StraightPath);
    CHECK(plan.found);
    CHECK(plan.viaWaypoint);
    CHECK(SapPointMargin(plan.waypoint, mates) >= 0.0f);
    CHECK(SapPointMargin(plan.stand, mates) >= 0.0f);
    CHECK(SapPathClear(rogue, SapPath{plan.waypoint}, mates));
    CHECK(SapPathClear(plan.waypoint, SapPath{plan.stand}, mates));
    CHECK(Near(SapDist(plan.stand, target), 3.0f));
}

// Mates on every side of the target: no stand point is outside the pack's
// clearance, so the Sap is skipped (and no path is asked for).
void TestNoClearStandPointSkipsSap()
{
    SapPoint const target{0.0f, 0.0f};
    SapPoint const rogue{0.0f, 30.0f};
    std::vector<SapPackMate> const mates{
        {4.0f, 0.0f, 7.0f}, {-4.0f, 0.0f, 7.0f}, {0.0f, 4.0f, 7.0f}, {0.0f, -4.0f, 7.0f}};

    SapApproachPlan const plan = PlanSapApproach(rogue, target, 3.0f, 15.0f, mates, StraightPath);
    CHECK(!plan.found);
    CHECK(plan.pathQueries == 0);
}

// Clear stand points but none reachable on the navmesh: skipped, within the query budget.
void TestNoReachablePointSkipsSap()
{
    SapPoint const target{0.0f, 0.0f};
    SapPoint const rogue{0.0f, 30.0f};
    std::vector<SapPackMate> const mates{{-5.0f, 0.0f, 7.0f}};

    SapApproachPlan const plan = PlanSapApproach(rogue, target, 3.0f, 15.0f, mates, NoPath);
    CHECK(!plan.found);
    CHECK(plan.pathQueries <= kSapMaxPathQueries);
}

// No pack: the old walk to the rogue's side of the mob, with no navmesh query.
void TestNoPackKeepsRogueSideStandPoint()
{
    SapPoint const target{0.0f, 0.0f};
    SapPoint const rogue{0.0f, 30.0f};
    SapApproachPlan const plan = PlanSapApproach(rogue, target, 3.0f, 15.0f, {}, NoPath);
    CHECK(plan.found);
    CHECK(!plan.viaWaypoint);
    CHECK(plan.pathQueries == 0);
    CHECK(Near(plan.stand.x, 0.0f));
    CHECK(Near(plan.stand.y, 3.0f));
}

// A rogue already inside a mate's clearance (undetected) may walk away from it,
// never towards it.
void TestStartInsideClearanceMayOnlyLeave()
{
    std::vector<SapPackMate> const mates{{0.0f, 0.0f, 8.0f}};
    SapPoint const start{5.0f, 0.0f};
    CHECK(SapPathClear(start, SapPath{SapPoint{12.0f, 0.0f}}, mates));
    CHECK(!SapPathClear(start, SapPath{SapPoint{-12.0f, 0.5f}}, mates));
}

// Core formula (Unit::CanDetectStealthOf, creature detector, front arc).
void TestStealthDetectRange()
{
    // Equal level, Stealth skill = 5 x level: 5/6 yd.
    CHECK(Near(SapStealthDetectRange(40, 0, 200, 0), 5.0f / 6.0f, 0.001f));
    // Three levels up: +15 detection, 1/6 yd each.
    CHECK(Near(SapStealthDetectRange(43, 0, 200, 3), 5.0f / 6.0f + 2.5f, 0.001f));
    // Five levels up: the per-level rate doubles.
    CHECK(Near(SapStealthDetectRange(45, 0, 200, 5), 5.0f / 6.0f + 25.0f / 3.0f, 0.001f));
    // Clamped to 0..30.
    CHECK(SapStealthDetectRange(10, 0, 300, -20) == 0.0f);
    CHECK(SapStealthDetectRange(60, 500, 0, 30) == 30.0f);
    CHECK(Near(SapStealthClearance(1.0f, 1.5f), 1.0f + kSapStealthAlertYd + kSapClearanceMargin + 1.5f, 0.001f));
}

// A skipped Sap holds on that mob only, until its time is up (also across the ms clock wrap).
void TestSapSkipWindow()
{
    CHECK(SapSkipActive(42, 42, 31000, 1000));
    CHECK(!SapSkipActive(42, 43, 31000, 1000));
    CHECK(!SapSkipActive(42, 42, 31000, 31000));
    CHECK(!SapSkipActive(0, 0, 31000, 1000));
    CHECK(SapSkipActive(7, 7, 20000u, 0xFFFFF000u));
}

} // namespace

int main()
{
    TestStandPointOnFarSideOfPack();
    TestPathThroughClearanceUsesArcPoint();
    TestDetourWhenEveryDirectPathCrossesThePack();
    TestNoClearStandPointSkipsSap();
    TestNoReachablePointSkipsSap();
    TestNoPackKeepsRogueSideStandPoint();
    TestStartInsideClearanceMayOnlyLeave();
    TestStealthDetectRange();
    TestSapSkipWindow();
    std::cout << "rogue sap approach policy tests passed\n";
    return 0;
}
