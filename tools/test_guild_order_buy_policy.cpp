// Standalone test for runtime/GuildOrderBuyPolicy.h (guild buy orders).
// No core, no DB: feeds the purchase rule plain counters.
#include "../runtime/GuildOrderBuyPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Owned counts what was delivered, carried and still in the mail.
    CHECK(BuyOwnedCount(0, 0, 0) == 0);
    CHECK(BuyOwnedCount(4, 3, 2) == 9);

    // Missing is want minus owned, never negative.
    CHECK(BuyStillMissing(10, 4) == 6);
    CHECK(BuyStillMissing(10, 10) == 0);
    CHECK(BuyStillMissing(10, 14) == 0);

    // Cheapest per item wins, whatever the stack size.
    CHECK(BuyoutBeatsPerItem(100, 10, 60, 5));    // 10 < 12
    CHECK(!BuyoutBeatsPerItem(60, 5, 100, 10));   // 12 > 10
    CHECK(!BuyoutBeatsPerItem(100, 10, 50, 5));   // tie: keep the first seen
    CHECK(!BuyoutBeatsPerItem(1, 1, 4294967295u, 4294967295u));  // tie, no overflow

    if (failures == 0)
        std::printf("guild order buy policy: all tests passed\n");
    return failures != 0;
}
