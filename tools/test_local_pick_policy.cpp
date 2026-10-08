#include "../ai/playerbot/LocalPickPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::CampLocalRange;
using ai::CampRequestMaxDistance;
using ai::ClassTrainerRequestMaxDistance;
using ai::GrindLocalFarRange;
using ai::GrindLocalNearRange;
using ai::GrindRequestMaxDistance;
using ai::LocalPickAppliesToBot;

namespace
{
    bool Feq(float a, float b) { return std::fabs(a - b) < 0.001f; }
}

int main()
{
    std::cout << "Starting TortoiseBots local grind/camp pick tests...\n";

    // -------------------------------------------------------------
    // Test 1: donor grind window (hi 500 / lo 2500, both /3 below 5)
    // -------------------------------------------------------------
    {
        CHECK(Feq(GrindLocalNearRange(1), 500.0f / 3.0f));
        CHECK(Feq(GrindLocalFarRange(1), 2500.0f / 3.0f));
        CHECK(Feq(GrindLocalNearRange(4), 500.0f / 3.0f));
        CHECK(Feq(GrindLocalFarRange(4), 2500.0f / 3.0f));
        CHECK(Feq(GrindLocalNearRange(5), 500.0f));
        CHECK(Feq(GrindLocalFarRange(5), 2500.0f));
        CHECK(Feq(GrindLocalNearRange(60), 500.0f));
        CHECK(Feq(GrindLocalFarRange(60), 2500.0f));
        std::cout << "  [PASS] grind window matches donor (500/2500, /3 below 5)\n";
    }

    // -------------------------------------------------------------
    // Test 2: donor camp window (500 at <=5, 2500 above)
    // -------------------------------------------------------------
    {
        CHECK(Feq(CampLocalRange(1), 500.0f));
        CHECK(Feq(CampLocalRange(5), 500.0f));
        CHECK(Feq(CampLocalRange(6), 2500.0f));
        CHECK(Feq(CampLocalRange(60), 2500.0f));
        std::cout << "  [PASS] camp window matches donor (500 <=5, 2500 above)\n";
    }

    // -------------------------------------------------------------
    // Test 3: pool bots only - owned/hired keep the full radius
    // -------------------------------------------------------------
    {
        CHECK(LocalPickAppliesToBot(true));
        CHECK(!LocalPickAppliesToBot(false));
        CHECK(Feq(GrindRequestMaxDistance(false, false, 10, 10000.0f), 10000.0f));
        CHECK(Feq(CampRequestMaxDistance(false, 10, 10000.0f), 10000.0f));
        std::cout << "  [PASS] owned/hired bots keep the full search radius\n";
    }

    // -------------------------------------------------------------
    // Test 4: ordinary pool grind is capped, zone exits are not
    // -------------------------------------------------------------
    {
        CHECK(Feq(GrindRequestMaxDistance(false, true, 10, 10000.0f), 2500.0f));
        CHECK(Feq(GrindRequestMaxDistance(false, true, 1, 10000.0f), 2500.0f / 3.0f));
        CHECK(Feq(GrindRequestMaxDistance(true, true, 10, 10000.0f), 10000.0f));
        CHECK(Feq(GrindRequestMaxDistance(true, true, 1, 10000.0f), 10000.0f));
        std::cout << "  [PASS] pool grind capped locally, leave-outgrown uncapped\n";
    }

    // -------------------------------------------------------------
    // Test 5: pool camp errands are capped by level
    // -------------------------------------------------------------
    {
        CHECK(Feq(CampRequestMaxDistance(true, 3, 10000.0f), 500.0f));
        CHECK(Feq(CampRequestMaxDistance(true, 20, 10000.0f), 2500.0f));
        std::cout << "  [PASS] pool camp errands capped by level\n";
    }

    // -------------------------------------------------------------
    // Test 6: class trainer goes far only when several spells behind
    // -------------------------------------------------------------
    {
        CHECK(Feq(ClassTrainerRequestMaxDistance(true, 20, 0, 10000.0f), 2500.0f));
        CHECK(Feq(ClassTrainerRequestMaxDistance(true, 20, 2, 10000.0f), 2500.0f));
        CHECK(Feq(ClassTrainerRequestMaxDistance(true, 20, 3, 10000.0f), 10000.0f));
        CHECK(Feq(ClassTrainerRequestMaxDistance(true, 9, 5, 10000.0f), 2500.0f));
        CHECK(Feq(ClassTrainerRequestMaxDistance(true, 3, 5, 10000.0f), 500.0f));
        CHECK(Feq(ClassTrainerRequestMaxDistance(false, 20, 0, 10000.0f), 10000.0f));
        std::cout << "  [PASS] class trainer far trip only when 3+ spells behind at 10+\n";
    }

    std::cout << "All local grind/camp pick tests passed.\n";
    return 0;
}
