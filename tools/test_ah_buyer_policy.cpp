// Standalone policy test for issue #405: the AH buyer teleport-or-skip uses
// a bounded scan plus one-interval trip/global teleport cooldowns.
// Mirrors runtime/AhBuyerPolicy.h; no server headers needed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_ah_buyer_policy.cpp -o /tmp/test_ah_buyer_policy && /tmp/test_ah_buyer_policy

#include "../runtime/AhBuyerPolicy.h"

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

using TortoiseBots::BuyerTeleportAllowed;
using TortoiseBots::BuyerTripCooldownSec;
using TortoiseBots::kBuyerCooldownMaxSec;
using TortoiseBots::kBuyerCooldownMinSec;
using TortoiseBots::kBuyerExamineCap;
using TortoiseBots::kBuyerProbeCap;

int main()
{
    // Scan stays bounded for a 500-bot pool: 24 cheap examinations max,
    // 8 of them at most touching an AI context ("nearest npcs" probe).
    CHECK(kBuyerExamineCap == 24);
    CHECK(kBuyerProbeCap == 8);
    CHECK(kBuyerProbeCap <= kBuyerExamineCap);

    // The seller path clamps its attempt cooldown to 5..3600 s; the buyer
    // trip cooldown uses the same floor and cap.
    CHECK(kBuyerCooldownMinSec == 5);
    CHECK(kBuyerCooldownMaxSec == 3600);
    CHECK(BuyerTripCooldownSec(120) == 120);
    CHECK(BuyerTripCooldownSec(0) == kBuyerCooldownMinSec);
    CHECK(BuyerTripCooldownSec(5000) == kBuyerCooldownMaxSec);

    // First teleport after boot is always allowed (last = 0 sentinel).
    CHECK(BuyerTeleportAllowed(1000000, 0, 120));
    // A second teleport inside the same 120 s interval is denied.
    CHECK(!BuyerTeleportAllowed(110, 100, 120));
    // Exactly one interval later it fires again.
    CHECK(BuyerTeleportAllowed(220, 100, 120));
    // Short intervals still pay the 5 s floor, not the raw tick.
    CHECK(!BuyerTeleportAllowed(104, 100, 0));
    CHECK(BuyerTeleportAllowed(105, 100, 0));

    if (failures)
    {
        std::cerr << "ah-buyer policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "ah-buyer policy: OK\n";
    return 0;
}
