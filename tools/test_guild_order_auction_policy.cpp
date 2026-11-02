// Standalone test for runtime/GuildOrderAuctionPolicy.h (guild auction leg).
// No core, no DB: feeds the rules plain counters.
#include "../runtime/GuildOrderAuctionPolicy.h"
#include "../runtime/GuildOrderRefundPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // One search per auction leg: the first empty search moves the ladder.
    CHECK(kAuctionSearchMovesLadderAfter == 1);
    CHECK(!AuctionSearchMovesLadder(0));
    CHECK(AuctionSearchMovesLadder(1));
    CHECK(AuctionSearchMovesLadder(3));

    // Two minutes without an auctioneer in interact range ends the leg.
    CHECK(kAuctionUnreachableMs == 120000);
    CHECK(!AuctionUnreachableElapsed(0, 0xFFFFFFFFu)); // never started
    CHECK(!AuctionUnreachableElapsed(1000, 1000 + kAuctionUnreachableMs - 1));
    CHECK(AuctionUnreachableElapsed(1000, 1000 + kAuctionUnreachableMs));
    CHECK(AuctionUnreachableElapsed(0xFFFFFF00u, 0x00020000u)); // uptime wrap

    // The walk is capped on its own: 20 minutes from entering the leg, so a
    // bot in the field is never handed off by the two-minute city deadline.
    CHECK(kAuctionLegTravelCapMs == 20 * 60 * 1000);
    CHECK(!AuctionLegTravelCapped(0, 0xFFFFFFFFu)); // never entered
    CHECK(!AuctionLegTravelCapped(1000, 1000 + kAuctionLegTravelCapMs - 1));
    CHECK(AuctionLegTravelCapped(1000, 1000 + kAuctionLegTravelCapMs));
    CHECK(AuctionLegTravelCapped(0xFFFF0000u, 0x0012FFFFu)); // uptime wrap

    // A covering stack beats a partial one; the smallest covering stack
    // wins, then the cheaper unit price.
    CHECK(AuctionPickBeats(10, 100, 5, 40, 7));    // 10 covers the miss of 7
    CHECK(!AuctionPickBeats(5, 40, 10, 100, 7));   // a partial stack loses
    CHECK(AuctionPickBeats(10, 100, 20, 100, 7));  // 10 has less surplus than 20
    CHECK(!AuctionPickBeats(20, 100, 10, 100, 7));
    CHECK(AuctionPickBeats(10, 90, 10, 100, 7));   // same stack, cheaper per unit
    CHECK(AuctionPickBeats(5, 40, 3, 30, 7));      // no cover: 8/unit beats 10/unit
    CHECK(!AuctionPickBeats(3, 30, 5, 40, 7));

    // A covering stack only wins when it costs no more than filling the miss
    // from the cheap partial stacks. Two partials fill 7 units for 1900
    // copper, so a 100 g stack loses and a 1900-copper one ties for the win.
    {
        std::vector<AuctionQuote> partials{ { 4, 1000 }, { 3, 900 } };
        uint32_t cheapestUnit = 0;
        uint32_t const fill = PartialFillCost(partials, 7, cheapestUnit);
        CHECK(fill == 1900);
        CHECK(cheapestUnit == 250);
        CHECK(!AuctionCoveringBeatsPartials(100000, 20, fill, cheapestUnit));
        CHECK(AuctionCoveringBeatsPartials(1900, 7, fill, cheapestUnit));
        CHECK(!AuctionCoveringBeatsPartials(1901, 7, fill, cheapestUnit));
    }
    {
        // The partials cannot fill the miss: only a covering stack within
        // twice their per-unit price is worth the surplus.
        std::vector<AuctionQuote> partials{ { 2, 1000 } };
        uint32_t cheapestUnit = 0;
        CHECK(PartialFillCost(partials, 7, cheapestUnit) == 0);
        CHECK(cheapestUnit == 500);
        CHECK(AuctionCoveringBeatsPartials(20 * 999, 20, 0, cheapestUnit));
        CHECK(!AuctionCoveringBeatsPartials(20 * 1001, 20, 0, cheapestUnit));
    }
    {
        // Nothing else is listed: the covering stack is the only way to
        // finish the leg, so it wins whatever it costs.
        uint32_t cheapestUnit = 0;
        CHECK(PartialFillCost({}, 7, cheapestUnit) == 0);
        CHECK(cheapestUnit == 0);
        CHECK(AuctionCoveringBeatsPartials(100000, 20, 0, 0));
    }

    // A fitting listing is still bought: the leg only hands off when the
    // pick is empty. A partial stack is bought too, and a miss of zero is
    // never a purchase.
    std::vector<AuctionListing> listings{
        { 11, 101, 100, 10 }, // 10 for 100 covers the miss of 7
        { 12, 102, 200, 20 },
    };
    CHECK(PickAuctionListing(listings, 7) == 0);
    std::vector<AuctionListing> partialsOnly{ { 21, 201, 300, 3 } };
    CHECK(PickAuctionListing(partialsOnly, 7) == 0);
    std::vector<AuctionListing> none;
    CHECK(PickAuctionListing(none, 7) == kNoAuctionListing);
    CHECK(PickAuctionListing(listings, 0) == kNoAuctionListing);

    // The leg is not re-entered for the same reagent until the rest of the
    // ladder was tried: the first visit blocks a second one, the need stage
    // earns one retry, and the retry is the last.
    CHECK(kAuctionVisitsPerReagent == 2);
    AuctionLegCycle cycle;
    CHECK(AuctionReentryAllowed(cycle, 2589)); // never tried
    NoteAuctionLegEntered(cycle, 2589);
    CHECK(!AuctionReentryAllowed(cycle, 2589)); // no other source yet
    NoteAuctionLadderEnd(cycle, 2590); // another reagent's cycle does not unlock it
    CHECK(!AuctionReentryAllowed(cycle, 2589));
    NoteAuctionLadderEnd(cycle, 2589);
    CHECK(AuctionReentryAllowed(cycle, 2589)); // full cycle: one retry
    NoteAuctionLegEntered(cycle, 2589);
    CHECK(!AuctionReentryAllowed(cycle, 2589));
    NoteAuctionLadderEnd(cycle, 2589);
    CHECK(!AuctionReentryAllowed(cycle, 2589)); // retry spent
    CHECK(AuctionReentryAllowed(cycle, 2591)); // a fresh reagent starts clean

    // A hand-off during a relist trip drops the surplus instead of sourcing
    // it: the leg's item is stock the order already bought.
    CHECK(AuctionHandoffLeavesSurplus(true, 2589, 2589));
    CHECK(!AuctionHandoffLeavesSurplus(true, 2589, 2590));
    CHECK(!AuctionHandoffLeavesSurplus(false, 2589, 2589));
    CHECK(!AuctionHandoffLeavesSurplus(true, 0, 0));

    // A 20-stack bought for a miss of 7: 7 used, 13 surplus.
    CHECK(BuySurplus(20, 7) == 13);
    CHECK(BuySurplus(7, 7) == 0);
    CHECK(BuySurplus(5, 7) == 0);

    // Relist at max(bought unit, current lowest listing unit), rounded up.
    CHECK(AuctionUnitPrice(300, 20) == 15);
    CHECK(AuctionUnitPrice(301, 20) == 16);
    CHECK(AuctionUnitPrice(0, 0) == 0);
    CHECK(RelistUnitPrice(300, 20, 10) == 15);
    CHECK(RelistUnitPrice(100, 20, 10) == 10);
    CHECK(RelistUnitPriceFromUnit(15, 0) == 15);
    CHECK(RelistUnitPriceFromUnit(0, 10) == 10);
    CHECK(RelistTotal(15, 13) == 195);
    CHECK(RelistTotal(2000000000u, 20) == 2000000000u);
    CHECK(kRelistDurationMinutes == 480);

    // A relist that could not be posted leaves the surplus to the order's
    // delivery leftover path (the guild-bank deposit).
    CHECK(SurplusDestinationAfterRelist(false, 13) == SurplusDestination::DeliveryLeftover);
    CHECK(SurplusDestinationAfterRelist(true, 13) == SurplusDestination::None);
    CHECK(SurplusDestinationAfterRelist(false, 0) == SurplusDestination::None);

    // A blocked top-up hands off with the bank's reason; a budget-cut top-up
    // or a leg with no recorded reason is the leg's own nolisting.
    CHECK(AuctionTopUpBlockedReason(false, OrderReason::BankDenied) == OrderReason::BankDenied);
    CHECK(AuctionTopUpBlockedReason(true, OrderReason::BankDenied) == OrderReason::NoListing);
    CHECK(AuctionTopUpBlockedReason(false, OrderReason::None) == OrderReason::NoListing);

    // Auction -> failed gold withdrawal -> auction hand-off: the first pass
    // queues the top-up for listing 42, the bank denies it, and the next pass
    // on the same listing is blocked. The final reason is bankdenied.
    {
        uint32_t const listing = 42;
        CHECK(GoldTopUpAllowed(listing, 0, 500, true));
        // The bank leg recorded the denial; the retry on the same listing is blocked.
        CHECK(!GoldTopUpAllowed(listing, listing, 500, true));
        CHECK(AuctionTopUpBlockedReason(false, OrderReason::BankDenied) == OrderReason::BankDenied);
    }

    if (failures == 0)
        std::printf("guild order auction policy: all tests passed\n");
    return failures != 0;
}
