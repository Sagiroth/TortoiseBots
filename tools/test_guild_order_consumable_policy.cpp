// Standalone test for runtime/GuildOrderConsumablePolicy.h: consumable guard
// for guild orders (never eat/drink the order item or its reagents).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_consumable_policy.cpp -o /tmp/test_guild_order_consumable_policy
//   /tmp/test_guild_order_consumable_policy
#include "../runtime/GuildOrderConsumablePolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // The order item itself is always reserved while the order runs.
    CHECK(ConsumableReservedForOrder(6291, false, false, 6291, false));
    CHECK(ConsumableReservedForOrder(2592, false, false, 2592, false));
    // Anything else is free to consume.
    CHECK(!ConsumableReservedForOrder(6291, false, false, 4542, false));
    CHECK(!ConsumableReservedForOrder(6291, false, false, 6289, false));

    // Finished orders and absent orders reserve nothing.
    CHECK(!ConsumableReservedForOrder(6291, true, false, 6291, false));
    CHECK(!ConsumableReservedForOrder(0, false, false, 6291, false));
    CHECK(!ConsumableReservedForOrder(6291, false, false, 0, false));

    // Craft-order reagents are reserved; other items are not.
    CHECK(ConsumableReservedForOrder(2840, false, true, 2770, true));
    CHECK(!ConsumableReservedForOrder(2840, false, true, 2770, false));
    CHECK(!ConsumableReservedForOrder(2840, false, true, 4542, false));
    // Farm/buy orders never reserve reagents.
    CHECK(!ConsumableReservedForOrder(2592, false, false, 2770, true));

    if (failures == 0)
        std::printf("test_guild_order_consumable_policy ok\n");
    return failures ? 1 : 0;
}
