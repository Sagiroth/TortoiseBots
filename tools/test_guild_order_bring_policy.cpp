// Standalone test for runtime/GuildOrderBringPolicy.h: the "materials I
// bring" tick set. No core, no DB: feeds the header plain ids.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_guild_order_bring_policy.cpp -o /tmp/test_guild_order_bring_policy
//   /tmp/test_guild_order_bring_policy
#include "../runtime/GuildOrderBringPolicy.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Comma list parses, dedupes, refuses garbage like a bad gold cap.
    std::vector<uint32_t> bring;
    CHECK(ParseBringList("123,456", bring) && bring.size() == 2 && bring[0] == 123 && bring[1] == 456);
    CHECK(ParseBringList("123,123,456", bring) && bring.size() == 2);
    CHECK(ParseBringList("", bring) == false);
    CHECK(ParseBringList("abc", bring) == false);
    CHECK(ParseBringList("0", bring) == false);
    CHECK(ParseBringList("12a", bring) == false);

    CHECK(ParseBringList("123,456", bring) && bring.size() == 2);
    CHECK(BringListHas(bring, 123));
    CHECK(!BringListHas(bring, 999));
    CHECK(!BringListHas(bring, 0));

    // Wire token round-trips, dash when empty.
    std::vector<uint32_t> empty;
    CHECK(BringItemsToken(empty) == "-");
    std::vector<uint32_t> two{ 123, 456 };
    CHECK(BringItemsToken(two) == "123,456");
    CHECK(ParseBringItemsToken("-").empty());
    CHECK(ParseBringItemsToken("123,456") == two);

    // A ticked reagent blocks every sourcing leg.
    CHECK(BringTickedBlocksLeg(two, 123));
    CHECK(!BringTickedBlocksLeg(two, 999));
    CHECK(!BringTickedBlocksLeg(empty, 123));

    // Only base reagents of the craft survive; the rest is dropped.
    uint32_t base[] = { 123, 789 };
    std::vector<uint32_t> mixed{ 123, 999 };
    std::vector<uint32_t> kept = FilterBringToBaseReagents(mixed, base, 2);
    CHECK(kept.size() == 1 && kept[0] == 123);
    CHECK(FilterBringToBaseReagents(mixed, nullptr, 0).empty());

    if (failures == 0)
        std::printf("guild order bring policy: all tests passed\n");
    return failures != 0;
}
