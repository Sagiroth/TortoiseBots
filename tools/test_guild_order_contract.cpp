// Standalone test for runtime/GuildOrderContract.h: the TBM order tokens.
#include "../runtime/GuildOrderContract.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    for (uint8_t i = 0; i <= uint8_t(OrderState::Stocked); ++i)
    {
        OrderState back = OrderState::Working;
        CHECK(ParseState(StateToken(OrderState(i)), back) && back == OrderState(i));
    }
    for (uint8_t i = 0; i <= uint8_t(OrderReason::Timeout); ++i)
    {
        OrderReason back = OrderReason::None;
        CHECK(ParseReason(ReasonToken(OrderReason(i)), back) && back == OrderReason(i));
    }
    CHECK(std::string(ReasonToken(OrderReason::Timeout)) == "timeout");
    CHECK(StateForReason(OrderReason::Timeout) == OrderState::Cant);
    CHECK(std::string(ReasonToken(OrderReason::Elite)) == "elite");
    CHECK(StateForReason(OrderReason::Elite) == OrderState::Cant);

    CHECK(std::string(DifficultyToken(OrderDifficulty::None)) == "-");
    CHECK(std::string(DifficultyToken(OrderDifficulty::Easy)) == "easy");
    CHECK(std::string(DifficultyToken(OrderDifficulty::Medium)) == "medium");
    CHECK(std::string(DifficultyToken(OrderDifficulty::Hard)) == "hard");

    if (failures == 0)
        std::printf("test_guild_order_contract ok\n");
    return failures ? 1 : 0;
}
