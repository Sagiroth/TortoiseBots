// Standalone test for the guild-order supply ladder and guild gold budget.
#include "../runtime/GuildOrderSupplyPolicy.h"
// (SupplyMailItemId and NextSupplySearchStreak need the give-up limit.)
#include "../runtime/GuildOrderGiveUpPolicy.h"

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
    // The mailbox leg walks for the supply item, never the order item.
    CHECK(SupplyMailItemId(SupplyStep::Bags, 123, 456) == 123);
    CHECK(SupplyMailItemId(SupplyStep::Bank, 123, 456) == 456);
    CHECK(SupplyMailItemId(SupplyStep::Bags, 0, 456) == 456);
    CHECK(SupplyMailItemId(SupplyStep::None, 123, 456) == 456);

    // The streak counts one leg: a repeat fails up, anything else restarts.
    SupplySearchStreak streak;
    streak = NextSupplySearchStreak(streak, SupplyStep::Bags, 7);
    CHECK(streak.fails == 1);
    streak = NextSupplySearchStreak(streak, SupplyStep::Bags, 7);
    CHECK(streak.fails == 2);
    streak = NextSupplySearchStreak(streak, SupplyStep::Bank, 7);
    CHECK(streak.fails == 1 && streak.step == SupplyStep::Bank);
    streak = NextSupplySearchStreak(streak, SupplyStep::Bank, 9);
    CHECK(streak.fails == 1 && streak.itemId == 9);
    CHECK(SearchFailsMoveZone(streak.fails + 2));

    options = SupplyOptions{};
    options.bankEnabled = false;
    CHECK(NextSupplyStep(options) == SupplyStep::AuctionHouse);

    CHECK(GuildGoldCapRemaining(1000, true, 400) == 600);
    CHECK(GuildGoldCapRemaining(1000, true, 1000) == 0);
    CHECK(GuildGoldCapRemaining(1000, false, 400) == UINT32_MAX);
    CHECK(GuildGoldShortfall(500, 300, 100, 200) == 100);
    CHECK(GuildGoldShortfall(300, 300, 100, 200) == 0);
    CHECK(GuildGoldShortfall(500, 300, 100, 50) == 50);

    // A full bag yields the mail leg to the vendor trip: the take gate
    // refuses above 95, so at 95 the leg already detours instead of walking
    // to a mailbox it cannot use. No mail waiting, no yield.
    CHECK(OrderMailYieldsToVendor(95, true));
    CHECK(OrderMailYieldsToVendor(100, true));
    CHECK(!OrderMailYieldsToVendor(94, true));
    CHECK(!OrderMailYieldsToVendor(100, false));
    CHECK(!OrderMailYieldsToVendor(0, false));

    if (failures == 0)
        std::printf("guild order supply policy: all tests passed\n");
    return failures != 0;
}
