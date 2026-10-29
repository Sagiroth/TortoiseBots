#include "../ai/playerbot/policy/travel/TravelRouteDangerPolicy.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static std::vector<float> SampleOffsets(float length)
{
    std::uint32_t const count = TravelRouteSampleCount(length);
    std::vector<float> offsets;
    for (std::uint32_t i = 0; i < count; ++i)
        offsets.push_back(length * TravelRouteSampleFraction(i, count));
    return offsets;
}

int main()
{
    std::cout << "Starting TortoiseBots TravelRouteDangerPolicy regression tests...\n";

    // -------------------------------------------------------------
    // Test 1: area rule boundary (same +5 margin as every other gate)
    // -------------------------------------------------------------
    {
        CHECK(!TravelRouteAreaTooHigh(0, 19));    // unresolved area: fail open
        CHECK(!TravelRouteAreaTooHigh(-1, 19));
        CHECK(!TravelRouteAreaTooHigh(-2, 19));
        CHECK(!TravelRouteAreaTooHigh(24, 19));   // +5 is ground the bot may cross
        CHECK(TravelRouteAreaTooHigh(25, 19));    // +6 is not
        CHECK(!TravelRouteAreaTooHigh(20, 60));   // outgrown ground is safe to cross
        CHECK(!TravelRouteAreaTooHigh(55, 55));
        CHECK(!TravelRouteAreaTooHigh(11, 6));
        CHECK(TravelRouteAreaTooHigh(12, 6));
    }
    std::cout << "  [PASS] Test 1: route area margin matches the destination gates\n";

    // -------------------------------------------------------------
    // Test 2: live-pool verdicts
    // -------------------------------------------------------------
    {
        // Gallows' Corner (Alterac Mountains) rates 36 in the shipped area
        // table; the level-17-19 bots that walked there died to level-35
        // Crushridge Ogres (Shak 19, Brightthorn 17, Bahn 16).
        CHECK(TravelRouteAreaTooHigh(36, 19));
        CHECK(TravelRouteAreaTooHigh(36, 17));
        // Dragonmaw Gates (Wetlands) rates 25; the Wetlands grind trips of
        // level-16-20 bots walked into level-26-28 orcs.
        CHECK(TravelRouteAreaTooHigh(25, 16));
        CHECK(TravelRouteAreaTooHigh(28, 18));   // Angerfang Encampment
        // Winterspring rates 56: the level-50 bots sent there died to level-56
        // Winterfall Den Watchers.
        CHECK(TravelRouteAreaTooHigh(56, 50));
        // Arathi Highlands itself rates 34 and is a level-30-40 zone: a
        // level-30 bot may still travel it (the waypoint rule admits it today),
        // which is why the route rule must not fire on the zone's own rating.
        CHECK(!TravelRouteAreaTooHigh(34, 30));
        // ... while the camps inside it that killed level-29-32 bots do fire.
        CHECK(TravelRouteAreaTooHigh(37, 31));
    }
    std::cout << "  [PASS] Test 2: live-pool area verdicts\n";

    // -------------------------------------------------------------
    // Test 3: sample count covers a leg, cap bounds a cross-continent one
    // -------------------------------------------------------------
    {
        CHECK(TravelRouteSampleCount(0.0f) == 1);   // degenerate leg: one sample
        CHECK(TravelRouteSampleCount(-5.0f) == 1);
        CHECK(TravelRouteSampleCount(1.0f) == 2);   // both ends always checked
        CHECK(TravelRouteSampleCount(TRAVEL_ROUTE_SAMPLE_STEP_YD) == 2);
        CHECK(TravelRouteSampleCount(TRAVEL_ROUTE_SAMPLE_STEP_YD + 1.0f) == 3);
        CHECK(TravelRouteSampleCount(505.0f) == 9);          // the Refuge Pointe leg
        CHECK(TravelRouteSampleCount(20000.0f) == TRAVEL_ROUTE_SAMPLE_MAX);
    }
    std::cout << "  [PASS] Test 3: sample count and cap\n";

    // -------------------------------------------------------------
    // Test 4: the sampled corridor has no gap the danger can hide in
    // -------------------------------------------------------------
    {
        // Samples sit on the straight line, so two neighbouring samples cover
        // anything up to TRAVEL_ROUTE_SAMPLE_RADIUS_YD from each of them. Every
        // offset gap must stay under twice that radius, or the gate would walk
        // a bot through a town sitting between two samples.
        CHECK(TRAVEL_ROUTE_SAMPLE_STEP_YD < 2.0f * TRAVEL_ROUTE_SAMPLE_RADIUS_YD);

        for (float length : {1.0f, 63.0f, 64.0f, 100.0f, 505.0f, 999.0f, 1000.0f, 3000.0f, 7175.0f})
        {
            std::vector<float> offsets = SampleOffsets(length);
            CHECK(offsets.size() >= 2);
            CHECK(offsets.front() == 0.0f);
            CHECK(offsets.back() == length);
            for (size_t i = 1; i < offsets.size(); ++i)
            {
                CHECK(offsets[i] > offsets[i - 1]);
                CHECK(offsets[i] - offsets[i - 1] < 2.0f * TRAVEL_ROUTE_SAMPLE_RADIUS_YD);
            }
        }
    }
    std::cout << "  [PASS] Test 4: even sampling with no gap beyond the scan radius\n";

    // -------------------------------------------------------------
    // Test 5: sample fractions span exactly 0..1, monotonic
    // -------------------------------------------------------------
    {
        CHECK(TravelRouteSampleFraction(0, 1) == 0.0f); // single-sample leg
        CHECK(TravelRouteSampleFraction(0, 5) == 0.0f);
        CHECK(TravelRouteSampleFraction(4, 5) == 1.0f);
        CHECK(TravelRouteSampleFraction(2, 5) > TravelRouteSampleFraction(1, 5));
        CHECK(TravelRouteSampleFraction(3, 13) > 0.24f && TravelRouteSampleFraction(3, 13) < 0.26f);
    }
    std::cout << "  [PASS] Test 5: sample fractions cover 0..1\n";

    // -------------------------------------------------------------
    // Test 6: already-there waypoint exemptions (stranded escape)
    // -------------------------------------------------------------
    {
        // Same zone as start: exempt (a bot in Moonwhisper can route out
        // through Moonwhisper waypoints); leaving the zone re-arms.
        CHECK(TravelRouteWaypointSharesStartZone(5642, 5642));
        CHECK(!TravelRouteWaypointSharesStartZone(148, 5642));
        // Unresolvable areas never exempt.
        CHECK(!TravelRouteWaypointSharesStartZone(0, 0));
        CHECK(!TravelRouteWaypointSharesStartZone(5642, 0));
        CHECK(!TravelRouteWaypointSharesStartZone(0, 5642));
        // Walk-out radius: inside it is ground underfoot, outside is a town
        // the journey crosses.
        CHECK(TravelRouteWaypointInWalkout(0.0f));
        CHECK(TravelRouteWaypointInWalkout(TRAVEL_ROUTE_TOWN_WALKOUT_YD));
        CHECK(!TravelRouteWaypointInWalkout(TRAVEL_ROUTE_TOWN_WALKOUT_YD + 1.0f));
        CHECK(!TravelRouteWaypointInWalkout(-1.0f));
    }
    std::cout << "  [PASS] Test 6: stranded-bot waypoint exemptions\n";

    std::cout << "\nAll 6 TortoiseBots Travel Route Danger Policy tests PASSED!\n";
    return 0;
}
