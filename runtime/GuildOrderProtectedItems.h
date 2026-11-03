// Protected set for guild orders: every item id a bot must not sell,
// auction, destroy, disenchant, trade or use while its order runs.
// The order item itself, the current supply-leg item, and (craft orders)
// the whole reagent tree through recipes the bot knows.
//
// Header-only and free of core/database types on purpose: exercised
// standalone in tools/test_guild_order_protected_items.cpp. Callers feed it
// the stored order fields, one callback resolving an item's direct
// reagents, and one reporting vendor-unlimited items (bought, never
// expanded further); the AI side feeds bot-known recipes (spell map create
// effects) and its vendor cache, never the global static map.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

inline constexpr std::size_t kOrderProtectedTreeDepth = 8;

// Collect the protected ids for an unfinished order: the order item, the
// current supply item, and every reagent reachable through known recipes
// (craft orders). Farm/buy orders protect the order and supply items only.
// Returns false when the order reserves nothing (finished or no item).
template <typename ReagentsFor, typename VendorUnlimited>
inline bool CollectOrderProtectedItems(uint32_t orderItemId, bool orderFinished,
    bool orderIsCraft, uint32_t supplyItemId, ReagentsFor reagentsFor,
    VendorUnlimited vendorUnlimited, std::unordered_set<uint32_t>& out)
{
    out.clear();
    if (orderFinished || orderItemId == 0)
        return false;
    out.insert(orderItemId);
    if (supplyItemId != 0 && supplyItemId != orderItemId)
        out.insert(supplyItemId);
    if (!orderIsCraft)
        return true;
    // Breadth-first through the bot's known recipes, so ore -> bar -> tube
    // chains all stay in the bags. Bought reagents still expand: the walk
    // only follows recipes the bot knows, and a vendor-sold reagent the bot
    // also crafts from (or its siblings) must stay covered the same way.
    // Unknown branches stop: a reagent the bot cannot make is a farm/buy
    // leg item, protected only as supply item.
    std::vector<uint32_t> frontier;
    frontier.push_back(orderItemId);
    std::unordered_set<uint32_t> expanded;
    expanded.insert(orderItemId);
    // The supply item joined the set above, so the walk below would skip it
    // as already seen and never reach its reagents. Seed it as a second root
    // and always expand it: while the leg crafts an intermediate, its
    // reagents are live stock (even when a vendor also sells it).
    if (supplyItemId != 0 && supplyItemId != orderItemId)
    {
        expanded.insert(supplyItemId);
        frontier.push_back(supplyItemId);
    }
    for (std::size_t step = 0; step < kOrderProtectedTreeDepth && !frontier.empty(); ++step)
    {
        std::vector<uint32_t> next;
        for (uint32_t parent : frontier)
        {
            std::vector<uint32_t> reagents = reagentsFor(parent);
            for (uint32_t reagent : reagents)
            {
                if (reagent == 0 || reagent == parent)
                    continue;
                if (!out.insert(reagent).second)
                    continue;
                if (vendorUnlimited(reagent))
                    continue;
                if (expanded.insert(reagent).second)
                    next.push_back(reagent);
            }
        }
        frontier.swap(next);
    }
    return true;
}

// Same walk without a vendor callback: vendor-unlimited reagents are plain
// tree members (the store's version; the AI side passes its vendor cache).
template <typename ReagentsFor>
inline bool CollectOrderProtectedItems(uint32_t orderItemId, bool orderFinished,
    bool orderIsCraft, uint32_t supplyItemId, ReagentsFor reagentsFor,
    std::unordered_set<uint32_t>& out)
{
    return CollectOrderProtectedItems(orderItemId, orderFinished, orderIsCraft,
        supplyItemId, reagentsFor, [](uint32_t) { return false; }, out);
}

} // namespace GuildOrders
} // namespace TortoiseBots
