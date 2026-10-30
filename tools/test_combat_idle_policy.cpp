#include "../ai/playerbot/policy/combat/CombatIdlePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::COMBAT_IDLE_EXIT_SECONDS;
using ai::CombatIdleExitDue;

int main()
{
    std::cout << "Starting TortoiseBots combat-idle policy tests...\n";

    CHECK(COMBAT_IDLE_EXIT_SECONDS == 30u);
    std::cout << "  [PASS] idle window is 30 seconds\n";

    // A real fight never exits: core combat, attackers, or a live hostile
    // order each hold the engine in combat whatever the clock says.
    CHECK(!CombatIdleExitDue(true, false, false, 3600));
    CHECK(!CombatIdleExitDue(false, true, false, 3600));
    CHECK(!CombatIdleExitDue(false, false, true, 3600));
    CHECK(!CombatIdleExitDue(true, true, true, 3600));
    std::cout << "  [PASS] any fight sign holds combat\n";

    // The latch itself: core quiet, no attackers, no live hostile order.
    // Short of the window the engine holds (pull flight, CC landing,
    // target switches); at the window it leaves.
    CHECK(!CombatIdleExitDue(false, false, false, 0));
    CHECK(!CombatIdleExitDue(false, false, false, 29));
    CHECK(CombatIdleExitDue(false, false, false, 30));
    CHECK(CombatIdleExitDue(false, false, false, 3600));
    std::cout << "  [PASS] quiet combat exits at the window\n";

    std::cout << "combat-idle policy: OK\n";
    return 0;
}
