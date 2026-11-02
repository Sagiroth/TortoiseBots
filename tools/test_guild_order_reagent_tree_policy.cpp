// Standalone test for runtime/GuildOrderReagentTree.h: the whole-tree need
// list a craft order hands to the ladder.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_reagent_tree_policy.cpp -o /tmp/t && /tmp/t
#include "../runtime/GuildOrderReagentTree.h"

#include <cstdio>
#include <unordered_map>
#include <vector>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

namespace
{

// The Goblin Rocket Boots shape, abstract ids: a base reagent at depth 2
// under one intermediate and at depth 3 under another, with a second base
// reagent shared by two intermediates.
//
//   1000 <- 1100 x1, 1200 x2, 1400 x1, 1500 x4
//   1100 <- 1110 x3 (yield 1);  1110 <- 900 x4      (900 at depth 3)
//   1200 <- 1300 x3 (yield 1);  1300 <- 800 x1      (800 at depth 3)
//   1400 <- 1300 x1, 900 x1                          (800 and 900 at depth 2)
//   1500 is a leaf
struct FakeRecipe
{
    uint32_t yieldPerCast = 1;
    std::vector<RecipeReagent> reagents;
    std::vector<uint32_t> tools;
};

using Table = std::unordered_map<uint32_t, FakeRecipe>;

struct Bag
{
    Table const* table;
    std::unordered_map<uint32_t, uint32_t> stock;

    bool operator()(uint32_t itemId, Recipe& out) const
    {
        auto found = table->find(itemId);
        if (found == table->end())
            return false;
        out.yieldPerCast = found->second.yieldPerCast;
        out.reagents = found->second.reagents;
        out.tools = found->second.tools;
        return true;
    }

    uint32_t operator()(uint32_t itemId) const
    {
        auto found = stock.find(itemId);
        return found == stock.end() ? 0 : found->second;
    }
};

uint32_t CountOf(std::vector<ReagentNeed> const& needs, uint32_t itemId)
{
    for (ReagentNeed const& need : needs)
        if (need.itemId == itemId)
            return need.count;
    return 0;
}

std::vector<ReagentNeed> Walk(Table const& table, std::unordered_map<uint32_t, uint32_t> stock,
    uint32_t itemId, uint32_t count)
{
    Bag bag{ &table, std::move(stock) };
    return CollectBaseReagents(itemId, count, bag, bag);
}

Table SharedTree()
{
    Table table;
    table[1000] = { 1, { { 1100, 1 }, { 1200, 2 }, { 1400, 1 }, { 1500, 4 } }, {} };
    table[1100] = { 1, { { 1110, 3 } }, {} };
    table[1110] = { 1, { { 900, 4 } }, {} };
    table[1200] = { 1, { { 1300, 3 } }, {} };
    table[1300] = { 1, { { 800, 1 } }, {} };
    table[1400] = { 1, { { 1300, 1 }, { 900, 1 } }, {} };
    return table;
}

} // namespace

int main()
{
    Table const tree = SharedTree();

    // Both shared bases come out once, at the sum over every branch: 3 bolts
    // x4 cloth under the boots plus 1 cloth under the trigger; 6 bars under
    // the tubes plus 1 under the trigger.
    std::vector<ReagentNeed> needs = Walk(tree, {}, 1000, 1);
    CHECK(needs.size() == 3);
    CHECK(CountOf(needs, 900) == 13);
    CHECK(CountOf(needs, 800) == 7);
    CHECK(CountOf(needs, 1500) == 4);
    CHECK(needs.size() > 0 && needs[0].itemId == 900);

    // Stock is allocated once across the whole walk: 8 cloth leaves 5.
    needs = Walk(tree, { { 900, 8 } }, 1000, 1);
    CHECK(CountOf(needs, 900) == 5);
    CHECK(CountOf(needs, 800) == 7);

    // An intermediate in hand shrinks its subtree: two tubes cover both.
    needs = Walk(tree, { { 1200, 2 } }, 1000, 1);
    CHECK(CountOf(needs, 800) == 1);
    CHECK(CountOf(needs, 900) == 13);

    // Bars held are spent before more ore is asked: 6 - 2 + 1 = 5.
    needs = Walk(tree, { { 1300, 2 } }, 1000, 1);
    CHECK(CountOf(needs, 800) == 5);

    // A yielded recipe never over-casts: 3 plates at yield 2 take 2 casts.
    Table yielded;
    yielded[3000] = { 1, { { 3100, 3 } }, {} };
    yielded[3100] = { 2, { { 3200, 3 } }, {} };
    needs = Walk(yielded, {}, 3000, 1);
    CHECK(CountOf(needs, 3200) == 6);
    needs = Walk(yielded, {}, 3000, 0);
    CHECK(needs.empty());

    // A recipe cycle terminates and names the item it closed on.
    Table cycle;
    cycle[4000] = { 1, { { 4100, 1 } }, {} };
    cycle[4100] = { 1, { { 4000, 1 } }, {} };
    needs = Walk(cycle, {}, 4000, 1);
    CHECK(CountOf(needs, 4000) == 1);

    // Depth cap: the item at depth 6 is a leaf, nothing below it is walked.
    Table deep;
    for (uint32_t itemId = 5000; itemId < 5008; ++itemId)
        deep[itemId] = { 1, { { itemId + 1, 1 } }, {} };
    needs = Walk(deep, {}, 5000, 1);
    CHECK(needs.size() == 1);
    CHECK(CountOf(needs, 5006) == 1);

    // Depth 1 keeps its old rows: a leaf asks for what is missing, an
    // intermediate already in hand contributes nothing.
    Table flat;
    flat[6000] = { 1, { { 6100, 3 }, { 6200, 1 } }, {} };
    flat[6200] = { 1, { { 6300, 2 } }, {} };
    needs = Walk(flat, { { 6100, 1 }, { 6200, 1 } }, 6000, 1);
    CHECK(needs.size() == 1);
    CHECK(CountOf(needs, 6100) == 2);
    needs = Walk(flat, { { 6100, 3 } }, 6000, 1);
    CHECK(CountOf(needs, 6100) == 0);
    CHECK(needs.size() == 1);
    CHECK(CountOf(needs, 6300) == 2);

    // A recipe tool listed at two levels is one row, and only while unheld.
    Table tooled;
    tooled[7000] = { 1, { { 7100, 1 }, { 7200, 1 } }, { 7300 } };
    tooled[7100] = { 1, { { 7400, 1 } }, { 7300 } };
    tooled[7200] = { 1, { { 7400, 2 } }, {} };
    needs = Walk(tooled, {}, 7000, 1);
    CHECK(CountOf(needs, 7300) == 1);
    CHECK(CountOf(needs, 7400) == 3);
    needs = Walk(tooled, { { 7300, 1 } }, 7000, 1);
    CHECK(CountOf(needs, 7300) == 0);

    if (failures == 0)
        std::printf("test_guild_order_reagent_tree_policy ok\n");
    return failures ? 1 : 0;
}
