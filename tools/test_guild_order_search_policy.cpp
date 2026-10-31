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
    if (failures == 0)
        std::printf("test_guild_order_search_policy ok\n");
    return failures ? 1 : 0;
}
