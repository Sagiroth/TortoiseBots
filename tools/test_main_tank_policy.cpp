#include "../ai/playerbot/MainTankPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::LowTankThreatFires;
using ai::MainTankSticksToCurrent;

int main()
{
    std::cout << "Starting TortoiseBots main-tank rule tests...\n";

    // -------------------------------------------------------------
    // (1) LowTankThreat: fires past half the main tank's threat.
    // -------------------------------------------------------------
    {
        // Below half: quiet.
        CHECK(!LowTankThreatFires(false, 49.0f, 100.0f));
        // Exactly half: quiet (donor uses strict >).
        CHECK(!LowTankThreatFires(false, 50.0f, 100.0f));
        // Past half: the pull is slipping.
        CHECK(LowTankThreatFires(false, 51.0f, 100.0f));
        // Tank holds nothing: uncontrolled pull, always fires.
        CHECK(LowTankThreatFires(false, 0.0f, 0.0f));
        CHECK(LowTankThreatFires(false, 10.0f, 0.0f));
        // Player targets have no threat table: never fires.
        CHECK(!LowTankThreatFires(true, 1000.0f, 0.0f));
        std::cout << "  [PASS] threat past half the tank fires, players never\n";
    }

    // -------------------------------------------------------------
    // (2) MT stickiness: explicit main tank only, multi-tank only.
    // -------------------------------------------------------------
    {
        // Lone tank (or solo): normal tournament, no stick.
        CHECK(!MainTankSticksToCurrent(true, 1));
        CHECK(!MainTankSticksToCurrent(true, 0));
        // Off-tank in a 2-tank group: picks up loose adds, no stick.
        CHECK(!MainTankSticksToCurrent(false, 2));
        // Explicit main tank with an off-tank beside it: holds its mob.
        CHECK(MainTankSticksToCurrent(true, 2));
        CHECK(MainTankSticksToCurrent(true, 3));
        std::cout << "  [PASS] only the explicit MT sticks, only with 2+ tanks\n";
    }

    std::cout << "All main-tank rule checks PASSED!\n";
    return 0;
}
