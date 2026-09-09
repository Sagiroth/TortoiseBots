#include "../ai/playerbot/TravelRoutePolicy.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x << "\n"; \
        std::exit(1); \
    } \
} while (0)

int main()
{
    std::cout << "Starting TortoiseBots TravelRoutePolicy regression tests...\n";

    // -------------------------------------------------------------
    // Test 1: Taxi Route Cost Policy
    // -------------------------------------------------------------
    {
        float const testDistance = 2139.057f; // Approx Southshore to Aerie Peak
        float const taxiCost = ai::GetTaxiRouteCost(testDistance);
        CHECK(std::fabs(taxiCost - 0.594183f) < 0.001f);

        // Zero and negative distance edge cases
        CHECK(ai::GetTaxiRouteCost(0.0f) == 0.0f);
        CHECK(ai::GetTaxiRouteCost(-100.0f) == 0.0f);

        // Comparing taxi cost against walking cost for standard speeds (run=8.0, swim=4.0)
        float const walkCost = ai::GetWalkTravelTime(testDistance, 0.0f, 8.0f, 4.0f);
        CHECK(taxiCost < walkCost);
        // Taxi cost should be vastly lower (~0.59 vs ~267.38)
        CHECK(walkCost / taxiCost > 400.0f);
    }
    std::cout << "  [PASS] Test 1: Taxi route cost weighting passed\n";

    // -------------------------------------------------------------
    // Test 2: Walk and Swim Travel Time Policy (Safe Grace & Long Swim Penalty)
    // -------------------------------------------------------------
    {
        float const runSpeed = 8.0f;
        float const swimSpeed = 4.0f;

        // Pure running
        CHECK(std::fabs(ai::GetWalkTravelTime(800.0f, 0.0f, runSpeed, swimSpeed) - 100.0f) < 0.001f);

        // Safe short swim (80 yd <= 120 yd safe grace)
        // 800yd run (100s) + 80yd swim (80/4 = 20s) = 120s
        CHECK(std::fabs(ai::GetWalkTravelTime(800.0f, 80.0f, runSpeed, swimSpeed) - 120.0f) < 0.001f);

        // Long swim (400 yd > 120 yd safe grace: 120 safe + 280 long * 4x penalty)
        // 800yd run (100s) + 120/4 (30s) + (280/4 * 4) (280s) = 100 + 30 + 280 = 410s
        CHECK(std::fabs(ai::GetWalkTravelTime(800.0f, 400.0f, runSpeed, swimSpeed) - 410.0f) < 0.001f);

        // Zero distance or speeds
        CHECK(ai::GetWalkTravelTime(0.0f, 0.0f, runSpeed, swimSpeed) == 0.0f);
        CHECK(ai::GetWalkTravelTime(100.0f, 50.0f, 0.0f, 0.0f) == 0.0f);
    }
    std::cout << "  [PASS] Test 2: Walk/swim travel time and long swim penalty passed\n";

    // -------------------------------------------------------------
    // Test 3: Stable Travel Selection Seed
    // -------------------------------------------------------------
    {
        std::uint32_t const party1 = 100;
        std::uint32_t const party2 = 101;
        std::uint32_t const purposeQuest = 4;
        std::uint32_t const purposeGrind = 8;
        std::uint32_t const mapId = 0;
        float const posX = -715.146f;
        float const posY = -512.134f;

        std::uint32_t const seedA = ai::GetStableTravelSelectionSeed(party1, purposeQuest, mapId, posX, posY);
        std::uint32_t const seedA_repeat = ai::GetStableTravelSelectionSeed(party1, purposeQuest, mapId, posX, posY);
        CHECK(seedA == seedA_repeat); // Deterministic

        // Different party produces different seed
        std::uint32_t const seedB = ai::GetStableTravelSelectionSeed(party2, purposeQuest, mapId, posX, posY);
        CHECK(seedA != seedB);

        // Different purpose produces different seed
        std::uint32_t const seedC = ai::GetStableTravelSelectionSeed(party1, purposeGrind, mapId, posX, posY);
        CHECK(seedA != seedC);

        // Nearby point within 50yd quantize bucket produces same seed
        std::uint32_t const seedNearby = ai::GetStableTravelSelectionSeed(party1, purposeQuest, mapId, posX + 5.0f, posY - 5.0f);
        CHECK(seedA == seedNearby);

        // Far point outside quantize bucket produces different seed
        std::uint32_t const seedFar = ai::GetStableTravelSelectionSeed(party1, purposeQuest, mapId, posX + 150.0f, posY);
        CHECK(seedA != seedFar);
    }
    std::cout << "  [PASS] Test 3: Stable travel selection seed determinism passed\n";

    // -------------------------------------------------------------
    // Test 4: Stable Party Route Variation Multiplier
    // -------------------------------------------------------------
    {
        float const fromX = -715.146f, fromY = -512.134f;
        float const toX = 282.096f, toY = -2001.28f;
        std::uint32_t const mapId = 0;

        float const mult1 = ai::GetStableRouteCostMultiplier(100, mapId, fromX, fromY, mapId, toX, toY);
        float const mult1_repeat = ai::GetStableRouteCostMultiplier(100, mapId, fromX, fromY, mapId, toX, toY);
        CHECK(mult1 == mult1_repeat); // Deterministic
        CHECK(mult1 >= 1.0f && mult1 <= 1.25f); // 25% ceiling

        // Variety test: across 128 parties, verify variation distribution
        std::set<int> buckets;
        for (std::uint32_t party = 1; party <= 128; ++party)
        {
            float const m = ai::GetStableRouteCostMultiplier(party, mapId, fromX, fromY, mapId, toX, toY);
            CHECK(m >= 1.0f && m <= 1.25f);
            buckets.insert(static_cast<int>((m - 1.0f) * 10000.0f));
        }
        CHECK(buckets.size() > 80); // High diversity across independent parties
    }
    std::cout << "  [PASS] Test 4: Stable party route variation multiplier passed\n";

    // -------------------------------------------------------------
    // Test 5: Route Choice Simulation (Sustained Swimming vs Road)
    // -------------------------------------------------------------
    {
        // Route 1 (Road around lake): 1000 yd run, 0 yd swim
        float const roadTime = ai::GetWalkTravelTime(1000.0f, 0.0f, 8.0f, 4.0f); // 125s

        // Route 2 (Straight swim across wide bay): 300 yd run, 600 yd sustained swim
        // 300/8 (37.5s) + 120/4 (30s) + 480/4 * 4 (480s) = 547.5s
        float const swimTime = ai::GetWalkTravelTime(300.0f, 600.0f, 8.0f, 4.0f);

        // Verification: The policy successfully steers bots to road over hazardous swim
        CHECK(roadTime < swimTime);
        CHECK(swimTime > roadTime * 4.0f);
    }
    std::cout << "  [PASS] Test 5: Route choice simulation (avoid long swims) passed\n";

    // -------------------------------------------------------------
    // Test 6: Route Choice Simulation (Flight Point vs Overland Walk)
    // -------------------------------------------------------------
    {
        float const overlandDistance = 4000.0f; // Cross-zone travel
        float const overlandWalkTime = ai::GetWalkTravelTime(overlandDistance, 0.0f, 8.0f, 4.0f); // 500s
        float const flightPathCost = ai::GetTaxiRouteCost(overlandDistance); // ~1.11

        // Flight paths must be overwhelmingly favored by route search
        CHECK(flightPathCost < overlandWalkTime);
        CHECK(flightPathCost < 5.0f);
    }
    std::cout << "  [PASS] Test 6: Flight path preference over cross-zone walk passed\n";

    std::cout << "\nAll 6 TortoiseBots Travel Route Policy tests PASSED!\n";
    return 0;
}
