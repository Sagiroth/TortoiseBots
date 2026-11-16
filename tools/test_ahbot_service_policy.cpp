// Standalone policy test for AhBotService pure decision rules.
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_ahbot_service_policy.cpp -o /tmp/test_ahbot_service_policy && /tmp/test_ahbot_service_policy

#include "../runtime/policy/ah/AhBotServicePolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace
{
int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)
} // namespace

int main()
{
    // Range parsing: single value, min:max, clamping, inverted, malformed.
    {
        auto r1 = ai::ParseAhBotRange("1", 1);
        CHECK(r1.min == 1 && r1.max == 1);

        auto r2 = ai::ParseAhBotRange("3:15", 1);
        CHECK(r2.min == 3 && r2.max == 15);

        auto r3 = ai::ParseAhBotRange(" 2 : 5 ", 1);
        CHECK(r3.min == 2 && r3.max == 5);

        auto r4 = ai::ParseAhBotRange("0:10", 1);
        CHECK(r4.min == 1 && r4.max == 10);

        auto r5 = ai::ParseAhBotRange("10:4", 1);
        CHECK(r5.min == 10 && r5.max == 10);

        auto r6 = ai::ParseAhBotRange("bogus", 5);
        CHECK(r6.min == 5 && r6.max == 5);

        auto r7 = ai::ParseAhBotRange("1:bad", 3);
        CHECK(r7.min == 3 && r7.max == 3);
    }

    // House sell target calculation (plus AddNewAuctions semantics).
    {
        CHECK(ai::ComputeAhBotSellCount(0, 3000, 3000, 150) == 150);
        CHECK(ai::ComputeAhBotSellCount(2950, 3000, 3000, 150) == 50);
        CHECK(ai::ComputeAhBotSellCount(3000, 3000, 3000, 150) == 0);
        CHECK(ai::ComputeAhBotSellCount(3100, 3000, 3000, 150) == 0);
        CHECK(ai::ComputeAhBotSellCount(100, 3000, 0, 150) == 0);
        CHECK(ai::ComputeAhBotSellCount(2500, 2000, 3000, 150) == 0); // already >= minItems
        CHECK(ai::ComputeAhBotSellCount(1900, 2000, 3000, 150) == 150); // capped at maxItems - current = 1100
        CHECK(ai::ComputeAhBotSellCount(1900, 3000, 2000, 150) == 100); // capped at maxItems - current
    }

    // Early expiry of own unbid listings before core expiry.
    {
        CHECK(!ai::ShouldRemoveOwnAhBotListing(false, 0, 100, 100, 60));
        CHECK(!ai::ShouldRemoveOwnAhBotListing(true, 42, 100, 100, 60)); // has active bid
        CHECK(!ai::ShouldRemoveOwnAhBotListing(true, 0, 161, 100, 60));  // > 60s left
        CHECK(ai::ShouldRemoveOwnAhBotListing(true, 0, 160, 100, 60));   // within 60s
        CHECK(ai::ShouldRemoveOwnAhBotListing(true, 0, 90, 100, 60));    // already due
    }

    // Legacy synthetic owner-0 check.
    {
        CHECK(ai::IsLegacySyntheticOwner(0));
        CHECK(!ai::IsLegacySyntheticOwner(123));
    }

    // Buyer evaluation (plus AddNewAuctionBuyerBotBid 1:1).
    {
        // Buyout strictly below willingness -> Buyout
        auto e1 = ai::EvaluateAhBotBuyerCandidate(900, 0, 700, 35, 100, 10, 0, false, true);
        CHECK(e1.action == ai::AhBotBuyDecision::Buyout && e1.amount == 900);

        // Buyout equals willingness (not strictly <) -> falls through to bid
        auto e2 = ai::EvaluateAhBotBuyerCandidate(1000, 0, 700, 35, 100, 10, 0, false, true);
        CHECK(e2.action == ai::AhBotBuyDecision::Bid && e2.amount == 700);

        // AlwaysBidMax when willingness == buyout converts cleanly to Buyout
        auto e3 = ai::EvaluateAhBotBuyerCandidate(1000, 0, 700, 35, 100, 10, 0, true, true);
        CHECK(e3.action == ai::AhBotBuyDecision::Buyout && e3.amount == 1000);

        // Existing bid: outbids by currentBid + outbidIncrement when < willingness
        auto e4 = ai::EvaluateAhBotBuyerCandidate(2000, 800, 700, 40, 100, 10, 0, false, true);
        CHECK(e4.action == ai::AhBotBuyDecision::Bid && e4.amount == 840);

        // Existing bid: AlwaysBidMax bids willingTotal (when < buyout)
        auto e5 = ai::EvaluateAhBotBuyerCandidate(2000, 800, 700, 40, 100, 10, 0, true, true);
        CHECK(e5.action == ai::AhBotBuyDecision::Bid && e5.amount == 1000);

        // Existing bid + outbid >= willingness -> Skip
        auto e6 = ai::EvaluateAhBotBuyerCandidate(2000, 960, 700, 40, 100, 10, 0, false, true);
        CHECK(e6.action == ai::AhBotBuyDecision::Skip);

        // PreventOverpayingForVendorItems blocks buyout/bid above vendorSellPrice
        auto e7 = ai::EvaluateAhBotBuyerCandidate(900, 0, 700, 35, 100, 10, 500, false, true);
        CHECK(e7.action == ai::AhBotBuyDecision::Skip);

        // PreventOverpayingForVendorItems disabled allows buyout
        auto e8 = ai::EvaluateAhBotBuyerCandidate(900, 0, 700, 35, 100, 10, 500, false, false);
        CHECK(e8.action == ai::AhBotBuyDecision::Buyout && e8.amount == 900);

        // Zero fair value -> Skip
        auto e9 = ai::EvaluateAhBotBuyerCandidate(900, 0, 700, 35, 0, 10, 0, false, true);
        CHECK(e9.action == ai::AhBotBuyDecision::Skip);
    }

    std::cout << "OK " << checks << " checks\n";
    return 0;
}
