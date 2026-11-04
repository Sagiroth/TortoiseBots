// Standalone check for runtime/policy/movement/TeleportLandingPolicy.h (the ground a bot lands on after a near
// teleport):
//   g++ -std=c++17 -Wall -Wextra tools/test_teleport_landing_policy.cpp -o /tmp/test_teleport_landing_policy && /tmp/test_teleport_landing_policy
#include "../runtime/policy/movement/TeleportLandingPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::TeleportLandingPolicy::SimulatedFallMs;
using ai::TeleportLandingPolicy::TeleportLandingZ;

// Jin'do's Banish 24466 lands a player at (-11582.9, -1251.15, 90.0), 12 yd over the ZG pit floor at 77.63. With
// no client nothing makes a bot fall, so it hovers there and never paths back. The feet probe misses a floor that
// deep: the caller's UpdateAllowedPositionZ reading is the fallback (floorFound false), and within the jump limit
// it is the landing.
static void TestBanishLandsOnThePitFloor()
{
    CHECK(TeleportLandingZ(90.0f, true, 77.63f, 20.0f) == 77.63f);
    CHECK(TeleportLandingZ(90.0f, false, 77.63f, 20.0f) == 77.63f);
    CHECK(TeleportLandingZ(90.0f, false, 77.63f, 60.0f) == 77.63f);
}

// Nothing to fall to: the terrain under a WMO deck reads far deeper than the jump limit, and a bot is never pulled
// down through a floor (the jump landing's own deck guard).
static void TestNoFloorStays()
{
    CHECK(TeleportLandingZ(90.0f, false, 90.0f, 20.0f) == 90.0f);   // the map answers with the feet
    CHECK(TeleportLandingZ(90.0f, false, 60.0f, 20.0f) == 90.0f);   // 30 yd: terrain only, over the limit
    // A navmesh floor is authoritative however deep it is: it is the surface the bot stands on.
    CHECK(TeleportLandingZ(180.0f, true, 0.0f, 60.0f) == 0.0f);
}

// A destination at or above the floor it would land on does not fall: same height, a step, or a deck overhead.
static void TestNoFallWithinAStep()
{
    CHECK(TeleportLandingZ(90.0f, true, 90.0f, 20.0f) == 90.0f);
    CHECK(TeleportLandingZ(90.0f, true, 89.7f, 20.0f) == 90.0f);
    CHECK(TeleportLandingZ(90.0f, false, 89.7f, 20.0f) == 90.0f);
    CHECK(TeleportLandingZ(90.0f, true, 92.0f, 20.0f) == 90.0f);   // a bridge over the head is no landing ground
}

// The fall time is the jump landing's formula: the same fall lasts as long whichever caller starts it.
static void TestFallTimeMatchesTheJumpFormula()
{
    // 12.37 yd from rest: sqrt(2 * 12.37 / 19.2911) = 1.132 s.
    CHECK(SimulatedFallMs(12.37f) == 1132);
    CHECK(SimulatedFallMs(-12.37f) == 1132);
    CHECK(SimulatedFallMs(0.0f) == 0);
    // Past the terminal length the last stretch runs at terminal velocity.
    CHECK(SimulatedFallMs(1000.0f) > SimulatedFallMs(500.0f));
}

int main()
{
    TestBanishLandsOnThePitFloor();
    TestNoFloorStays();
    TestNoFallWithinAStep();
    TestFallTimeMatchesTheJumpFormula();
    std::cout << "test_teleport_landing_policy: all checks passed\n";
    return 0;
}
