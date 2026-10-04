// Standalone policy test for task A: the organic AH seller lift.
// Nearest same-map house only, and only for a bot already on an AH trip or
// already standing near that house (mirrors runtime/AhBuyerPolicy.h: a
// cross-map house is unroutable for the walk back). Critharadro (Dun Morogh
// -> Darnassus AH) is the cross-map case; Tharobraeth (Dun Morogh ->
// Stormwind AH) the same-map hijack case; the idle Dun Morogh gnome lifted
// to Booty Bay is the random-pick case the nearest rule kills.
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
using TortoiseBots::kAhSellerLiftNearbyYd;

int main()
{
    CHECK(kAhSellerLiftNearbyYd == 300.0f);

    // Cross-map is never a lift (Critharadro Dun Morogh map 0 -> Darnassus
    // AH map 1), whatever the bot is doing or however close the raw numbers
    // look (same x,y on the other map must not read as "near").
    CHECK(!AhSellerTeleportAllowed(0, 1, true, 10.0f));
    CHECK(!AhSellerTeleportAllowed(0, 1, false, 10.0f));
    CHECK(!AhSellerTeleportAllowed(1, 0, true, 0.0f));

    // Same-map hijack refused (Tharobraeth Dun Morogh -> Stormwind AH, mid
    // quest trip): not on an AH trip and not near the house, so it walks.
    CHECK(!AhSellerTeleportAllowed(0, 0, false, 5000.0f));
    CHECK(!AhSellerTeleportAllowed(0, 0, false, kAhSellerLiftNearbyYd + 1.0f));

    // Idle Dun Morogh gnome vs Booty Bay: nearest-house pick means this case
    // never arises, but a far house is refused even with no trip at all.
    CHECK(!AhSellerTeleportAllowed(0, 0, false, 9000.0f));

    // On an AH trip: the lift fires at any distance (the walk there is the
    // bot's own errand; the lift just finishes it).
    CHECK(AhSellerTeleportAllowed(0, 0, true, 5000.0f));
    CHECK(AhSellerTeleportAllowed(1, 1, true, 100000.0f));

    // Already at the house: short hop on, no trip needed. Boundary inclusive.
    CHECK(AhSellerTeleportAllowed(0, 0, false, 10.0f));
    CHECK(AhSellerTeleportAllowed(0, 0, false, kAhSellerLiftNearbyYd));
    CHECK(AhSellerTeleportAllowed(1, 1, false, 0.0f));

    if (failures)
    {
        std::cerr << "ah-seller-teleport policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "ah-seller-teleport policy: OK\n";
    return 0;
}
