// Standalone policy test for issue #405 (review 2): organic AH buyer.
// No buyer teleport - the market buyer bids only with a bot already at a
// matching-house auctioneer; demand comes from the organic AH travel purpose
// (level 10+, same-continent house, spendable purse, narrow RPG-phase slice).
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

using ai::BuyerTripAffordable;
using ai::BuyerTripPhaseOpen;
using ai::BuyerTripSameContinent;
using ai::kBuyerExamineCap;
using ai::kBuyerProbeCap;
using ai::kBuyerTripMinLevel;
using ai::kBuyerTripMinSpareCopper;
using ai::kBuyerTripPhaseBelow;
using ai::kBuyerTripPhaseMax;

int main()
{
    // Review finding 6: examine == probe, one pass, no blind spot. Every
    // pool entry the scan touches is fully probed; the rotating start cycles
    // the whole pool over passes instead of skipping two-thirds of it.
    CHECK(kBuyerExamineCap == 8);
    CHECK(kBuyerProbeCap == 8);
    CHECK(kBuyerProbeCap <= kBuyerExamineCap);

    // Review finding 3: shopping trips are for established bots (level 10+,
    // past the beginner death belt), never low-level capital marches.
    CHECK(kBuyerTripMinLevel == 10);

    // Review finding 4 (one money rule): the trip needs several gold of
    // spendable purse - the same "free money for anything" the arrival bid
    // reads - never a 1-silver walk to fail every gate on arrival.
    CHECK(kBuyerTripMinSpareCopper == 50000);
    CHECK(!BuyerTripAffordable(0, 0));          // broke bot stays home
    CHECK(!BuyerTripAffordable(500, 500));      // reserve exactly covered
    CHECK(!BuyerTripAffordable(150, 100));      // 50c spare: below floor
    CHECK(!BuyerTripAffordable(60000, 59000));  // 10s spare: still below floor
    CHECK(BuyerTripAffordable(60000, 5000));    // 5.5g spare: trip
    CHECK(BuyerTripAffordable(10000000, 0));    // rich bot, nothing to train

    // Review finding 3/10: narrow phase slice (first 3 of 61 slots, ~5% of
    // the pool per hour) on the shared 0..60 RPG clock (modulo 61 noted).
    CHECK(kBuyerTripPhaseMax == 60);
    CHECK(kBuyerTripPhaseBelow == 3);
    CHECK(BuyerTripPhaseOpen(0));
    CHECK(BuyerTripPhaseOpen(2));
    CHECK(!BuyerTripPhaseOpen(3));
    CHECK(!BuyerTripPhaseOpen(60));

    // Review finding 3: same-continent only. A cross-map house is unroutable
    // (FLT_MAX in the destination search) so the trip never opens for it.
    CHECK(BuyerTripSameContinent(0, true));
    CHECK(BuyerTripSameContinent(1, true));
    CHECK(!BuyerTripSameContinent(0, false));
    CHECK(!BuyerTripSameContinent(1, false));

    if (failures)
    {
        std::cerr << "ah-buyer policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "ah-buyer policy: OK\n";
    return 0;
}
