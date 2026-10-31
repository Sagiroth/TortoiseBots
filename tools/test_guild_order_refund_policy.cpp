// Standalone test for runtime/GuildOrderRefundPolicy.h (gold refund, AH retry,
// empty bank leg, leftover deposit cap).
#include "../runtime/GuildOrderRefundPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Refund repays held copper, capped by what the bot carries.
    CHECK(GuildGoldRefundAmount(500, 800) == 500);
    CHECK(GuildGoldRefundAmount(800, 500) == 500);
    CHECK(GuildGoldRefundAmount(0, 500) == 0);

    // Remainder saturates after a full deposit.
    CHECK(GuildGoldRefundRemaining(300, 100) == 200);
    CHECK(GuildGoldRefundRemaining(300, 300) == 0);
    CHECK(GuildGoldRefundRemaining(300, 500) == 0);

    // A denied gold top-up never retries for the same listing.
    CHECK(!GoldTopUpAllowed(7, 7, 500, true));
    CHECK(GoldTopUpAllowed(8, 7, 500, true));
    CHECK(!GoldTopUpAllowed(8, 7, 0, true));
    CHECK(!GoldTopUpAllowed(8, 7, 500, false));
    CHECK(!GoldTopUpAllowed(0, 7, 500, true));

    // An empty bank leg waits for the cooldown per item.
    CHECK(BankLegSkipped(5, 5, 1000, 900));
    CHECK(!BankLegSkipped(5, 5, kBankEmptyCooldownMs + 1, 0));
    CHECK(!BankLegSkipped(5, 6, 1000, 900));
    CHECK(!BankLegSkipped(0, 0, 1000, 900));

    // Leftover delivery caps at order-caused gains.
    CHECK(SupplyLeftoverAllowed(10, 4) == 4);
    CHECK(SupplyLeftoverAllowed(3, 4) == 3);
    CHECK(SupplyLeftoverAllowed(0, 4) == 0);

    if (failures == 0)
        std::printf("guild order refund policy: all tests passed\n");
    return failures != 0;
}
