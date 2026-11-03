// Standalone test for runtime/GuildOrderTelemetryPolicy.h: sub-order leg telemetry.
#include "../runtime/GuildOrderTelemetryPolicy.h"
#include <string>

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
    // A replacement order logs its first row at once even when the leg matches.
    CHECK(OrderIdentityToken(0, 2319, OrderType::Farm, 1700000000) == "0:2319:farm:1700000000");
    CHECK(OrderIdentityToken(42, 2319, OrderType::Farm, 1700000000) == "42:2319:farm:1700000000");
    CHECK(OrderRequestKey(SupplyStep::Bank, 2319, 0, 2319, OrderType::Farm, 1700000000) ==
        "bank:2319|0:2319:farm:1700000000");
    std::string firstRequest = OrderRequestKey(SupplyStep::Bank, 2319, 0, 2319, OrderType::Farm, 1700000000);
    // Same order, same leg inside the minute stays suppressed.
    CHECK(OrderRequestLogDue(firstRequest, firstRequest, 100, 160) == false);
    // Same leg, new parent logs at once.
    CHECK(OrderRequestLogDue(firstRequest,
        OrderRequestKey(SupplyStep::Bank, 2319, 42, 2319, OrderType::Farm, 1700000000), 100, 160) == true);
    // Same leg, new order item logs at once.
    CHECK(OrderRequestLogDue(firstRequest,
        OrderRequestKey(SupplyStep::Bank, 2319, 0, 4306, OrderType::Farm, 1700000000), 100, 160) == true);
    // Same leg, new order type logs at once.
    CHECK(OrderRequestLogDue(firstRequest,
        OrderRequestKey(SupplyStep::Bank, 2319, 0, 2319, OrderType::Buy, 1700000000), 100, 160) == true);
    // Identical replacement (same parent/item/type/leg, fresh createdAt) logs at once.
    CHECK(OrderRequestLogDue(firstRequest,
        OrderRequestKey(SupplyStep::Bank, 2319, 0, 2319, OrderType::Farm, 1700000060), 100, 160) == true);
    // Same order (same createdAt) on the same leg inside the minute stays suppressed.
    CHECK(OrderRequestLogDue(firstRequest,
        OrderRequestKey(SupplyStep::Bank, 2319, 0, 2319, OrderType::Farm, 1700000000), 100, 160) == false);
    // Top-level orders carry no parent; unknown names fall back to "?".
    CHECK(OrderParentToken(0, "Goldie") == "-");
    CHECK(OrderParentToken(42, "Goldie") == "Goldie");
    CHECK(OrderParentToken(42, "") == "?");

    if (failures == 0)
        std::printf("guild order telemetry policy: all tests passed\n");
    return failures != 0;
}
