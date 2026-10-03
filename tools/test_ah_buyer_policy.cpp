// Standalone policy test for issue #405 rework: organic AH buyer.
// No buyer teleport - the market buyer bids only with a bot already at a
// matching-house auctioneer; demand comes from the organic AH travel purpose
// (spare gold above the trainer reserve, RPG-phase rate bound).
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
using ai::kBuyerExamineCap;
using ai::kBuyerProbeCap;
using ai::kBuyerTripMinSpareCopper;
using ai::kBuyerTripPhaseBelow;
using ai::kBuyerTripPhaseMax;

int main()
{
    // Scan stays bounded for a 500-bot pool: 24 cheap examinations max,
    // 8 of them at most touching an AI context ("nearest npcs" probe).
    // Past the cap the candidate is skipped - there is no teleport fallback.
    CHECK(kBuyerExamineCap == 24);
    CHECK(kBuyerProbeCap == 8);
    CHECK(kBuyerProbeCap <= kBuyerExamineCap);

    // Purse rule: spare gold above the trainer reserve must cover at least
    // the cheapest realistic opening bid (1 silver floor).
    CHECK(kBuyerTripMinSpareCopper == 100);
    CHECK(!BuyerTripAffordable(0, 0));       // broke bot stays home
    CHECK(!BuyerTripAffordable(500, 500));   // reserve exactly covered: no trip
    CHECK(!BuyerTripAffordable(500, 450));   // 50c spare: below granularity
    CHECK(BuyerTripAffordable(600, 500));    // 100c spare: trip
    CHECK(BuyerTripAffordable(100000, 0));   // rich bot with nothing to train

    // Rate bound: only the first 15 minutes of the hourly RPG phase walk
    // to shop (same 0..60 clock as the GenericRpg/Grind stagger).
    CHECK(kBuyerTripPhaseMax == 60);
    CHECK(kBuyerTripPhaseBelow == 15);
    CHECK(BuyerTripPhaseOpen(0));
    CHECK(BuyerTripPhaseOpen(14));
    CHECK(!BuyerTripPhaseOpen(15));
    CHECK(!BuyerTripPhaseOpen(59));

    if (failures)
    {
        std::cerr << "ah-buyer policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "ah-buyer policy: OK\n";
    return 0;
}
