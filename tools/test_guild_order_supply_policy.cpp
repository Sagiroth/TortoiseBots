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
    // The farm item is the supply leg's item on bank/AH/farm/bags legs, 0
    // anywhere else. A craft order short on a vendor-only reagent (bought,
    // never farmed) travels for nothing unless the leg gave up on it.
    CHECK(SupplyFarmItemFor(SupplyStep::Farm, 123, OrderType::Farm, OrderReason::None, false) == 123);
    CHECK(SupplyFarmItemFor(SupplyStep::Bank, 123, OrderType::Farm, OrderReason::None, false) == 123);
    CHECK(SupplyFarmItemFor(SupplyStep::AuctionHouse, 123, OrderType::Craft, OrderReason::None, false) == 123);
    CHECK(SupplyFarmItemFor(SupplyStep::Bags, 123, OrderType::Craft, OrderReason::None, false) == 123);
    CHECK(SupplyFarmItemFor(SupplyStep::None, 123, OrderType::Farm, OrderReason::None, false) == 0);
    CHECK(SupplyFarmItemFor(SupplyStep::Deliver, 123, OrderType::Farm, OrderReason::None, false) == 0);
    CHECK(SupplyFarmItemFor(SupplyStep::Sub, 123, OrderType::Craft, OrderReason::None, false) == 0);
    // A vendor-only reagent (bought, never farmed) travels for nothing
    // unless the leg gave up on it: the vendor leg runs on Farm for any
    // order type, never the wild farm plan.
    CHECK(SupplyFarmItemFor(SupplyStep::Farm, 123, OrderType::Craft, OrderReason::None, true) == 0);
    CHECK(SupplyFarmItemFor(SupplyStep::Farm, 123, OrderType::Craft, OrderReason::NoSource, true) == 123);
    CHECK(SupplyFarmItemFor(SupplyStep::Farm, 0, OrderType::Farm, OrderReason::None, false) == 0);
    CHECK(SupplyFarmItemFor(SupplyStep::Farm, 123, OrderType::Farm, OrderReason::None, true) == 0);
    CHECK(SupplyFarmItemFor(SupplyStep::Farm, 123, OrderType::Farm, OrderReason::NoSource, true) == 123);
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

}
