// Standalone test for runtime/GuildOrderProtectedItems.h: the protected set
// a bot keeps while its guild order runs (order item, supply item, whole
// known-recipe reagent tree for craft orders).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_protected_items.cpp -o /tmp/test_guild_order_protected_items
//   /tmp/test_guild_order_protected_items
#include "../runtime/GuildOrderProtectedItems.h"

#include <cstdio>
#include <unordered_map>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

namespace
{

// Goblin Rocket Boots 7189 <- Black Mageweave Boots 10026, Mithril Tube
// 10559 x2, Heavy Leather 4234 x4; tube <- Mithril Bar 3860 <- ore 3858;
// unknown branch (no recipe known) stops the tree.
std::unordered_map<uint32_t, std::vector<uint32_t>> const kRecipes = {
    {7189, {10026, 10559, 4234}},
    {10559, {3860}},
    {3860, {3858}},
};

std::vector<uint32_t> KnownReagents(uint32_t itemId)
{
    auto it = kRecipes.find(itemId);
    return it == kRecipes.end() ? std::vector<uint32_t>{} : it->second;
}

}

int main()
{
    std::unordered_set<uint32_t> kept;

    // Craft order: order item, direct reagents, and the nested ore->bar->tube
    // chain are all protected.
    CHECK(CollectOrderProtectedItems(7189, false, true, 0, KnownReagents, kept));
    CHECK(kept.count(7189));
    CHECK(kept.count(10026));
    CHECK(kept.count(10559));
    CHECK(kept.count(4234));
    CHECK(kept.count(3860));
    CHECK(kept.count(3858));
    CHECK(kept.size() == 6);

    // A supply leg on an intermediate (crafting the tube) keeps the tube's own
    // reagents: the supply item joined the set first, and the tree walk once
    // skipped expanding an item it had already seen (live: Unstable Trigger,
    // its Mageweave Cloth and Solid Blasting Powder were sold).
    kept.clear();
    CHECK(CollectOrderProtectedItems(7189, false, true, 10559, KnownReagents, kept));
    CHECK(kept.count(10559));
    CHECK(kept.count(3860));
    CHECK(kept.count(3858));

    // The current supply item joins the set (here: the nested ore leg).
    kept.clear();
    CHECK(CollectOrderProtectedItems(7189, false, true, 3858, KnownReagents, kept));
    CHECK(kept.count(3858));

    // An unknown intermediate stops the tree: without a tube recipe the ore
    // below it is not reachable.
    kept.clear();
    auto noTube = [](uint32_t itemId) -> std::vector<uint32_t>
    {
        if (itemId == 10559)
            return {};
        return KnownReagents(itemId);
    };
    CHECK(CollectOrderProtectedItems(7189, false, true, 0, noTube, kept));
    CHECK(kept.count(10559));
    CHECK(!kept.count(3860));
    CHECK(!kept.count(3858));

    // A vendor-unlimited reagent is protected but not expanded: the bot
    // buys the tube instead of making it, so the mithril chain below it
    // stays out of the bags while the tube itself stays in.
    kept.clear();
    auto vendorTube = [](uint32_t itemId) { return itemId == 10559; };
    CHECK(CollectOrderProtectedItems(7189, false, true, 0, KnownReagents, vendorTube, kept));
    CHECK(kept.count(7189));
    CHECK(kept.count(10559));
    CHECK(kept.count(4234));
    CHECK(!kept.count(3860));
    CHECK(!kept.count(3858));

    // The worked supply item expands even when a vendor sells it: the leg
    // crafts the tube, so the mithril chain below it is live stock.
    kept.clear();
    CHECK(CollectOrderProtectedItems(7189, false, true, 10559, KnownReagents, vendorTube, kept));
    CHECK(kept.count(10559));
    CHECK(kept.count(3860));
    CHECK(kept.count(3858));

    // Farm/buy orders protect the order and supply items only.
    kept.clear();
    CHECK(CollectOrderProtectedItems(2592, false, false, 2592, KnownReagents, kept));
    CHECK(kept.size() == 1);
    kept.clear();
    CHECK(CollectOrderProtectedItems(2592, false, false, 3858, KnownReagents, kept));
    CHECK(kept.count(2592));
    CHECK(kept.count(3858));
    CHECK(!kept.count(3860));

    // Finished or absent orders reserve nothing.
    kept.clear();
    CHECK(!CollectOrderProtectedItems(7189, true, true, 0, KnownReagents, kept));
    CHECK(kept.empty());
    CHECK(!CollectOrderProtectedItems(0, false, true, 0, KnownReagents, kept));

    if (failures == 0)
        std::printf("test_guild_order_protected_items ok\n");
    return failures ? 1 : 0;
}
