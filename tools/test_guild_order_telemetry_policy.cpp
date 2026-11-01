// Standalone test for runtime/GuildOrderTelemetryPolicy.h: sub-order leg telemetry.
#include "../runtime/GuildOrderTelemetryPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Leg token reuses the supply-step token plus the supply item.
    CHECK(OrderLegToken(SupplyStep::Bank, 2319) == "bank:2319");
    CHECK(OrderLegToken(SupplyStep::None, 0) == "-:0");
    CHECK(OrderLegToken(SupplyStep::AuctionHouse, 4306) == "ah:4306");
    // Same leg re-logs at most once a minute; a changed leg logs at once.
    CHECK(OrderRequestLogDue("bank:2319", "bank:2319", 100, 160) == false);
    CHECK(OrderRequestLogDue("bank:2319", "bank:2319", 160, 160) == true);
    CHECK(OrderRequestLogDue("bank:2319", "bags:2319", 100, 160) == true);
    CHECK(OrderRequestLogDue("", "bank:2319", 100, 0) == true);
    // Top-level orders carry no parent; unknown names fall back to "?".
    CHECK(OrderParentToken(0, "Goldie") == "-");
    CHECK(OrderParentToken(42, "Goldie") == "Goldie");
    CHECK(OrderParentToken(42, "") == "?");

    if (failures == 0)
        std::printf("guild order telemetry policy: all tests passed\n");
    return failures != 0;
}
