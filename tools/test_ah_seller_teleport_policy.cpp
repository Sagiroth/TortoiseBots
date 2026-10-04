// Standalone policy test for task A: the organic AH seller lift.
// Same-map only, and never hijacks a bot mid-trip on another errand (mirrors
// runtime/AhBuyerPolicy.h: a cross-map house is unroutable for the walk back).
// Critharadro (Dun Morogh -> Darnassus AH) is the cross-map case;
// Tharobraeth (Dun Morogh -> Stormwind AH) the same-map hijack case.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_ah_seller_teleport_policy.cpp -o /tmp/test_ah_seller_teleport_policy && /tmp/test_ah_seller_teleport_policy

#include "../runtime/AhSellerTeleportPolicy.h"

#include <cstdint>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        ++failures; \
    } \
} while (0)
} // namespace

using TortoiseBots::AhSellerTeleportAllowed;

int main()
{
    // Cross-map is never a lift (Critharadro Dun Morogh map 0 -> Darnassus
    // AH map 1), whatever the bot is doing.
    CHECK(!AhSellerTeleportAllowed(0, 1, false, false));
    CHECK(!AhSellerTeleportAllowed(0, 1, false, true));
    CHECK(!AhSellerTeleportAllowed(0, 1, true, false));
    CHECK(!AhSellerTeleportAllowed(0, 1, true, true));
    CHECK(!AhSellerTeleportAllowed(1, 0, false, false));

    // Same-map hijack refused (Tharobraeth Dun Morogh -> Stormwind AH, both
    // map 0, mid quest trip): a bot working another errand keeps walking.
    CHECK(!AhSellerTeleportAllowed(0, 0, true, false));

    // Idle bot takes the same-map lift; a bot already heading to an AH rides too.
    CHECK(AhSellerTeleportAllowed(0, 0, false, false));
    CHECK(AhSellerTeleportAllowed(0, 0, false, true));
    CHECK(AhSellerTeleportAllowed(0, 0, true, true));
    CHECK(AhSellerTeleportAllowed(1, 1, false, false));

    if (failures)
    {
        std::cerr << "ah-seller-teleport policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "ah-seller-teleport policy: OK\n";
    return 0;
}
