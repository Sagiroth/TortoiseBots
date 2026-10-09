#include "../ai/playerbot/GroupHygienePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DungeonLeadershipYield;
using ai::GroupFarAwayLeave;

int main()
{
    std::cout << "Starting TortoiseBots group-hygiene gate tests...\n";

    // SOC-G2: cross-map groups always leave (no contribution possible).
    CHECK(GroupFarAwayLeave(false, 10.0f, 200.0f));
    CHECK(GroupFarAwayLeave(false, 0.0f, 200.0f));
    std::cout << "  [PASS] cross-map group leaves\n";

    // SOC-G2: same map, beyond twice the roam distance, leaves.
    CHECK(GroupFarAwayLeave(true, 400.0f, 200.0f));
    CHECK(GroupFarAwayLeave(true, 1000.0f, 200.0f));
    std::cout << "  [PASS] far same-map group leaves\n";

    // SOC-G2: same map, within reach, stays.
    CHECK(!GroupFarAwayLeave(true, 0.0f, 200.0f));
    CHECK(!GroupFarAwayLeave(true, 399.9f, 200.0f));
    std::cout << "  [PASS] nearby group stays\n";

    // SOC-G4: leader bot + live real master + same-map dungeon yields.
    CHECK(DungeonLeadershipYield(true, true, true, true, true));
    std::cout << "  [PASS] dungeon leadership yields\n";

    // SOC-G4: any missing clause keeps leadership.
    CHECK(!DungeonLeadershipYield(false, true, true, true, true));
    CHECK(!DungeonLeadershipYield(true, false, true, true, true));
    CHECK(!DungeonLeadershipYield(true, true, false, true, true));
    CHECK(!DungeonLeadershipYield(true, true, true, false, true));
    CHECK(!DungeonLeadershipYield(true, true, true, true, false));
    CHECK(!DungeonLeadershipYield(false, false, false, false, false));
    std::cout << "  [PASS] every missing clause keeps leadership\n";

    std::cout << "All TortoiseBots group-hygiene gate tests passed.\n";
    return 0;
}
