// Standalone test for runtime/GuildOrderPreviewPolicy.h: the read-only
// order preview shape. No core, no DB: feeds the header plain counts.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_guild_order_preview_policy.cpp -o /tmp/test_guild_order_preview_policy
//   /tmp/test_guild_order_preview_policy
#include "../runtime/GuildOrderPreviewPolicy.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Only the uncovered rest lands on the need list.
    CHECK(PreviewNeedCount(10, 3, 1, 2, 2, 1, 0, 1) == 0);
    CHECK(PreviewNeedCount(10, 3, 1, 0, 0, 0, 0, 0) == 6);
    CHECK(PreviewNeedCount(10, 0, 0, 0, 0, 0, 0, 0) == 10);
    CHECK(PreviewNeedCount(5, 9, 9, 9, 9, 9, 9, 9) == 0);

    // Rough minutes: a bench job without a farm leg, else kills over five
    // plus a base, floored at ten.
    CHECK(PreviewMinutesForKills(0.0, false) == 10);
    CHECK(PreviewMinutesForKills(200.0, false) == 10);
    CHECK(PreviewMinutesForKills(25.0, true) == 15);
    CHECK(PreviewMinutesForKills(0.0, true) == 10);

    // One row token carries every preview count in order.
    PreviewReagent row;
    row.itemId = 2319;
    row.required = 10;
    row.inBags = 3;
    row.inMail = 1;
    row.inBank = 2;
    row.farmCount = 2;
    row.farmZone = "Ashenvale";
    row.ahCount = 1;
    CHECK(PreviewReagentToken(row) == "2319:10:3:1:2:2:Ashenvale:1:0:0:1");
    CHECK(PreviewReagentsToken({}) == "-");
    CHECK(PreviewReagentsToken({ row }) == "2319:10:3:1:2:2:Ashenvale:1:0:0:1");

    // Missing zone renders as a dash, never empty (the wire field stays).
    PreviewReagent zoneless = row;
    zoneless.farmZone.clear();
    CHECK(PreviewReagentToken(zoneless) == "2319:10:3:1:2:2:-:1:0:0:1");

    if (failures == 0)
        std::printf("guild order preview policy: all tests passed\n");
    return failures != 0;
}
