// Standalone test for runtime/GuildOrderWhoSearchPolicy.h: the guild-wide
// "who can make this" argument, sort, count and line-format rules.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_guild_order_who_search_policy.cpp -o /tmp/test_guild_order_who_search_policy
//   /tmp/test_guild_order_who_search_policy

#include "../runtime/GuildOrderWhoSearchPolicy.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // A type keyword as the first argument means "no bot".
    CHECK(IsNoBotFirstArg("craft"));
    CHECK(IsNoBotFirstArg("CRAFT"));
    CHECK(IsNoBotFirstArg("enchant"));
    CHECK(IsNoBotFirstArg("farm"));
    CHECK(IsNoBotFirstArg("buy"));
    CHECK(!IsNoBotFirstArg("Bob"));
    CHECK(!IsNoBotFirstArg("crafty"));
    CHECK(!IsNoBotFirstArg(""));

    // Bots sort by skill high to low, name breaking ties.
    std::vector<WhoBot> bots = {{"Bob", 150, 2, 3, false}, {"Al", 300, 3, 3, true}, {"Zed", 150, 0, 3, false}};
    SortWhoBots(bots);
    CHECK(bots.size() == 3);
    CHECK(bots[0].name == "Al" && bots[0].busy);
    CHECK(bots[1].name == "Bob");
    CHECK(bots[2].name == "Zed");

    // have counts distinct reagents the bot holds in full.
    std::vector<WhoReagent> reagents = {{2, 5}, {3, 2}, {1, 1}};
    uint32_t have = 0, need = 0;
    CountWhoReagents(reagents, have, need);
    CHECK(have == 2 && need == 3);
    std::vector<WhoReagent> none;
    CountWhoReagents(none, have, need);
    CHECK(have == 0 && need == 0);
    std::vector<WhoReagent> full = {{1, 1}};
    CountWhoReagents(full, have, need);
    CHECK(have == 1 && need == 1);
    // Recipes with more than three reagents count them all: spell reagents
    // run up to eight rows, not three effects.
    std::vector<WhoReagent> five = {{1, 1}, {2, 0}, {3, 3}, {4, 5}, {1, 0}};
    CountWhoReagents(five, have, need);
    CHECK(have == 3 && need == 5);

    // Reply framing matches the addon contract exactly.
    CHECK(FormatWhoBegin("craft", 2) == "TBM:ORDER_WHO_BEGIN|craft|2");
    CHECK(FormatWhoEnd() == "TBM:ORDER_WHO_END");
    std::vector<WhoBot> empty;
    CHECK(FormatWhoBots(empty) == "-");
    std::vector<WhoBot> one = {{"Al", 300, 2, 3, true}};
    CHECK(FormatWhoBots(one) == "Al:300:2:3:1");
    std::vector<WhoBot> two = {{"Al", 300, 3, 3, true}, {"Bob", 150, 0, 3, false}};
    CHECK(FormatWhoBots(two) == "Al:300:3:3:1;Bob:150:0:3:0");
    CHECK(FormatWhoRow("craft", 1234, "Copper Mace", "2", "-", 75, "Al:300:3:3:1") ==
        "TBM:ORDER_WHO|craft|1234|Copper Mace|2|-|75|Al:300:3:3:1");
    CHECK(FormatWhoRow("craft", 1234, "Copper Mace", "2", "-", 75, "-") ==
        "TBM:ORDER_WHO|craft|1234|Copper Mace|2|-|75|-");

    if (failures == 0)
        std::printf("test_guild_order_who_search_policy ok\n");
    return failures != 0;
}
