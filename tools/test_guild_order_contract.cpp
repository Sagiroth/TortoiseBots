// Standalone test for runtime/GuildOrderContract.h: the TBM order tokens.
#include "../runtime/GuildOrderContract.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    for (uint8_t i = 0; i <= uint8_t(OrderState::Cant); ++i)
    {
        OrderState back = OrderState::Working;
        CHECK(ParseState(StateToken(OrderState(i)), back) && back == OrderState(i));
    }
    for (uint8_t i = 0; i <= uint8_t(OrderReason::NoBank); ++i)
    {
        OrderReason back = OrderReason::None;
        CHECK(ParseReason(ReasonToken(OrderReason(i)), back) && back == OrderReason(i));
    }
    OrderType type = OrderType::Farm;
    CHECK(ParseType("craft", type) && type == OrderType::Craft);
    CHECK(!ParseType("kill", type));
    CHECK(std::string(ReasonToken(OrderReason::None)) == "-");

    CHECK(StateForReason(OrderReason::Grouped) == OrderState::Waiting);
    CHECK(StateForReason(OrderReason::LowSkill) == OrderState::NoSkill);
    CHECK(StateForReason(OrderReason::Stuck) == OrderState::Cant);
    CHECK(StateForReason(OrderReason::BagsFull) == OrderState::Delivering);
    CHECK(StateForReason(OrderReason::NoBank) == OrderState::Mailed);
    CHECK(IsFinished(OrderState::Mailed) && !IsFinished(OrderState::Delivering));

    CHECK(SourcesToken(0) == "-");
    CHECK(SourcesToken(SourceSkin | SourceAh) == "skin,ah");

    if (failures == 0)
        std::printf("test_guild_order_contract ok\n");
    return failures ? 1 : 0;
}
