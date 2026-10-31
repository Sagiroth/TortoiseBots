#pragma once

// Consumable guard for guild orders: a bot must never eat, drink or otherwise
// consume the item its active order needs. Live case: Axebane caught 4x Raw
// Brilliant Smallfish 6291 (an on-use food item) for a farm order and the eat
// action consumed them all while resting, so the quota never filled.
// Header-only and free of core/database types on purpose: exercised
// standalone in tools/test_guild_order_consumable_policy.cpp. Callers feed it
// the stored order fields plus, for craft orders, whether the candidate item
// is one of the order's reagents.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// True when the candidate item is reserved for the bot's unfinished order:
// the order item itself, or (craft orders) one of its reagents. Finished or
// absent orders (itemId 0) reserve nothing.
inline bool ConsumableReservedForOrder(uint32_t orderItemId, bool orderFinished,
    bool orderIsCraft, uint32_t candidateItemId, bool candidateIsOrderReagent)
{
    if (orderFinished || orderItemId == 0 || candidateItemId == 0)
        return false;
    if (candidateItemId == orderItemId)
        return true;
    return orderIsCraft && candidateIsOrderReagent;
}

} // namespace GuildOrders
} // namespace TortoiseBots
