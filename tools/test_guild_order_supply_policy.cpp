// Standalone test for the guild-order supply ladder and guild gold budget.
#include "../runtime/GuildOrderSupplyPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    SupplyOptions options;
    CHECK(NextSupplyStep(options) == SupplyStep::Bank);
    options.bankAttempted = true;
    CHECK(NextSupplyStep(options) == SupplyStep::AuctionHouse);
    options.auctionHouseAttempted = true;
    options.selfCanFarm = true;
    CHECK(NextSupplyStep(options) == SupplyStep::Farm);
    options.selfCanCraft = true;
    CHECK(NextSupplyStep(options) == SupplyStep::Craft);
    options.selfCanCraft = false;
    options.selfCanFarm = false;
    CHECK(NextSupplyStep(options) == SupplyStep::Sub);
    options.subAttempted = true;
    CHECK(NextSupplyStep(options) == SupplyStep::Need);

    options = SupplyOptions{};
    options.bankEnabled = false;
    CHECK(NextSupplyStep(options) == SupplyStep::AuctionHouse);

    CHECK(GuildGoldCapRemaining(1000, true, 400) == 600);
    CHECK(GuildGoldCapRemaining(1000, true, 1000) == 0);
    CHECK(GuildGoldCapRemaining(1000, false, 400) == UINT32_MAX);
    CHECK(GuildGoldShortfall(500, 300, 100, 200) == 100);
    CHECK(GuildGoldShortfall(300, 300, 100, 200) == 0);
    CHECK(GuildGoldShortfall(500, 300, 100, 50) == 50);

    if (failures == 0)
        std::printf("guild order supply policy: all tests passed\n");
    return failures != 0;
}
