#include "../ai/playerbot/GroupHygienePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::GroupFarAwayLeave;

int main()
{
    std::cout << "Starting TortoiseBots group-hygiene gate tests...\n";

    // SOC-G2: cross-map groups always leave (no contribution possible).
    // The caller evaluates this above the member-safety veto loop, so a
    // cross-map master cannot veto its own leave.
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

    std::cout << "All TortoiseBots group-hygiene gate tests passed.\n";
    return 0;
}
