// Standalone test for runtime/GuildOrderPlayerBuyPolicy.h: the player Buy
// order gate. No core, no DB: feeds the gate plain order types.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_guild_order_player_buy_policy.cpp -o /tmp/test_guild_order_player_buy_policy
//   /tmp/test_guild_order_player_buy_policy
#include "../runtime/GuildOrderPlayerBuyPolicy.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // A Buy order from a player is refused while the switch is off.
    CHECK(PlayerBuyOrderRefused(OrderType::Buy, false));
    CHECK(!PlayerBuyOrderRefused(OrderType::Buy, true));

    // Farm and craft orders always pass, switch on or off.
    CHECK(!PlayerBuyOrderRefused(OrderType::Farm, false));
    CHECK(!PlayerBuyOrderRefused(OrderType::Farm, true));
    CHECK(!PlayerBuyOrderRefused(OrderType::Craft, false));
    CHECK(!PlayerBuyOrderRefused(OrderType::Craft, true));

    // The wire code is stable and outside the reason set, so old addons
    // show the message instead of a state.
    OrderReason reason = OrderReason::None;
    CHECK(std::string(PlayerBuyRefusalCode()) == "buy-disabled");
    CHECK(!ParseReason(PlayerBuyRefusalCode(), reason));
    CHECK(std::string(PlayerBuyRefusal()).find('|') == std::string::npos);
    CHECK(!std::string(PlayerBuyRefusal()).empty());

    // The cap token is a plain word with no wire separator.
    CHECK(std::string(PlayerBuyCapToken()) == "guild-order-nobuy");
    CHECK(std::string(PlayerBuyCapToken()).find('|') == std::string::npos);
    CHECK(std::string(PlayerBuyCapToken()).find(',') == std::string::npos);

    if (failures == 0)
        std::printf("guild order player buy policy: all tests passed\n");
    return failures != 0;
}
