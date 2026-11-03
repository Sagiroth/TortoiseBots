// Standalone test for runtime/GuildOrderSearchPolicy.h: smart guild-order
// search matching and ranking.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_search_policy.cpp -o /tmp/test_guild_order_search_policy
//   /tmp/test_guild_order_search_policy
#include "../runtime/GuildOrderSearchPolicy.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Matches(char const* name, char const* query)
{
    return SearchNameMatches(name, SplitQueryWords(query));
}

int main()
{
    CHECK(SplitQueryWords("").empty());
    CHECK(SplitQueryWords("   ").empty());
    std::vector<std::string> words = SplitQueryWords("  heavy   leather ");
    CHECK(words.size() == 2 && words[0] == "heavy" && words[1] == "leather");
    CHECK(NormalizeQueryWords(words) == "heavy leather");
    CHECK(NormalizeQueryWords(SplitQueryWords("")) == "");
    CHECK(Matches("heavy leather", "leather heavy"));
    CHECK(Matches("heavy leather", "heavy"));
    CHECK(Matches("heavy leather", "leather"));
    CHECK(!Matches("heavy leather", "leather light"));
    CHECK(!Matches("heavy leather", "heavy hide"));
    CHECK(Matches("rough heavy leather scraps", "heavy leather"));
    CHECK(Matches("iron ore", "iron"));
    CHECK(!Matches("iron ore", "iron copper"));

    CHECK(SearchNameRank("heavy leather", "heavy leather") == 0);
    CHECK(SearchNameRank("heavy", "heavy leather") != 0);
    // Name start beats word start beats contains.
    CHECK(SearchNameRank("heavy leather scraps", "heavy") == 1);
    CHECK(SearchNameRank("rough heavy leather", "heavy") == 2);
    CHECK(SearchNameRank("leather", "eather") == 3);
    CHECK(SearchNameRank("rough heavy leather", "heavy l") == 2);
    CHECK(SearchNameRank("heavy leather", "leather heavy") == 3);
    // Extra spaces rank exact: the needle is the normalized words.
    CHECK(SearchNameRank("heavy leather", NormalizeQueryWords(SplitQueryWords("heavy   leather"))) == 0);
    // Per-type filters: Rocket Boots (crafted gear) are Buy/Craft only,
    // random-drop gear and recipes show nowhere.
    OrderItemFacts const ore{ true, false, SourceOre };
    OrderItemFacts const droppedMaterial{ true, false, SourceDrop };
    OrderItemFacts const craftedGear{ false, true, SourceDrop };
    OrderItemFacts const craftedMaterial{ true, true, 0 };
    OrderItemFacts const randomDropGear{ false, false, SourceDrop };
    OrderItemFacts const fish{ false, false, SourceFish };
    CHECK(OrderSearchShows(OrderType::Farm, ore));
    CHECK(OrderSearchShows(OrderType::Farm, droppedMaterial));
    CHECK(OrderSearchShows(OrderType::Farm, fish));
    CHECK(!OrderSearchShows(OrderType::Farm, craftedGear));
    CHECK(!OrderSearchShows(OrderType::Farm, craftedMaterial));
    CHECK(!OrderSearchShows(OrderType::Farm, randomDropGear));
    CHECK(OrderSearchShows(OrderType::Craft, craftedGear));
    CHECK(OrderSearchShows(OrderType::Craft, craftedMaterial));
    CHECK(!OrderSearchShows(OrderType::Craft, ore));
    CHECK(OrderSearchShows(OrderType::Buy, craftedGear));
    CHECK(OrderSearchShows(OrderType::Buy, ore));
    CHECK(OrderSearchShows(OrderType::Buy, droppedMaterial));
    CHECK(OrderSearchShows(OrderType::Buy, fish));
    CHECK(!OrderSearchShows(OrderType::Buy, randomDropGear));
    CHECK(!OrderSearchShows(OrderType::Buy, OrderItemFacts{}));
    if (failures == 0)
        std::printf("test_guild_order_search_policy ok\n");
    return failures ? 1 : 0;
}
