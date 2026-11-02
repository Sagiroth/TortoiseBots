// Standalone test for runtime/GuildOrderAuctionPolicy.h (guild auction leg).
// No core, no DB: feeds the rules plain counters.
#include "../runtime/GuildOrderAuctionPolicy.h"

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

    // A covering stack beats a partial one; the smallest covering stack
    // wins, then the cheaper unit price.
    CHECK(AuctionPickBeats(10, 100, 5, 40, 7));    // 10 covers the miss of 7
    CHECK(!AuctionPickBeats(5, 40, 10, 100, 7));   // a partial stack loses
    CHECK(AuctionPickBeats(10, 100, 20, 100, 7));  // 10 has less surplus than 20
    CHECK(!AuctionPickBeats(20, 100, 10, 100, 7));
    CHECK(AuctionPickBeats(10, 90, 10, 100, 7));   // same stack, cheaper per unit
    CHECK(AuctionPickBeats(5, 40, 3, 30, 7));      // no cover: 8/unit beats 10/unit
    CHECK(!AuctionPickBeats(3, 30, 5, 40, 7));

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

    if (failures == 0)
        std::printf("guild order auction policy: all tests passed\n");
    return failures != 0;
}
