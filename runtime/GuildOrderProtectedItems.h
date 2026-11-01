// Protected set for guild orders: every item id a bot must not sell,
// auction, destroy, disenchant, trade or use while its order runs.
// The order item itself, the current supply-leg item, and (craft orders)
// the whole reagent tree through recipes the bot knows.
//
// Header-only and free of core/database types on purpose: exercised
// standalone in tools/test_guild_order_protected_items.cpp. Callers feed it
// the stored order fields plus one callback resolving an item's direct
// reagents; the AI side feeds bot-known recipes (spell map create effects),
// never the global static map.
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
template <typename ReagentsFor>
inline bool CollectOrderProtectedItems(uint32_t orderItemId, bool orderFinished,
    bool orderIsCraft, uint32_t supplyItemId, ReagentsFor reagentsFor,
    std::unordered_set<uint32_t>& out)
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
    // chains all stay in the bags. Unknown branches stop: a reagent the bot
    // cannot make is a farm/buy leg item, protected only as supply item.
    std::vector<uint32_t> frontier;
    frontier.push_back(orderItemId);
    std::unordered_set<uint32_t> expanded;
    expanded.insert(orderItemId);
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
                if (expanded.insert(reagent).second)
                    next.push_back(reagent);
            }
        }
        frontier.swap(next);
    }
    return true;
}

// True when the candidate item is reserved for the bot's unfinished order.
template <typename ReagentsFor>
inline bool OrderProtectsItem(uint32_t orderItemId, bool orderFinished,
    bool orderIsCraft, uint32_t supplyItemId, uint32_t candidateItemId,
    ReagentsFor reagentsFor)
{
    if (orderFinished || orderItemId == 0 || candidateItemId == 0)
        return false;
    if (candidateItemId == orderItemId || candidateItemId == supplyItemId)
        return true;
    if (!orderIsCraft)
        return false;
    std::unordered_set<uint32_t> kept;
    CollectOrderProtectedItems(orderItemId, orderFinished, orderIsCraft,
        supplyItemId, reagentsFor, kept);
    return kept.count(candidateItemId) != 0;
}

} // namespace GuildOrders
} // namespace TortoiseBots
