#pragma once

// Pool residency for bots working a guild order: an order bot stays online
// until its order finishes, and offline order bots log in first when the
// pool refills. Header-only and free of core/database types on purpose:
// exercised standalone in tools/test_guild_order_pool_policy.cpp.
// RandomBotService feeds these functions plain state it already keeps.
#include "GuildOrderContract.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// An unfinished order keeps its bot online: no timed logout, no trim, no
// rotation. A finished order (or none) logs out as normal. Same check the
// stall watchdog uses to tell busy from stuck.
inline bool KeepOrderBotOnline(std::optional<OrderRecord> const& order)
{
    return order && !IsFinished(order->state);
}

struct OrderLoginPick
{
    std::vector<size_t> indexes; // candidate indexes to log in, scan order
    uint32_t online = 0;         // online count after these picks
};

// Offline bots with an unfinished order come first, before the normal order,
// bounded by the free pool slots and the queue budget. Band shares do not
// apply here: an order bot may exceed its band, but the total never passes
// the configured max. hasUnfinished(i) is false for online candidates.
template <typename HasUnfinished>
inline OrderLoginPick PickOrderBotsFirst(size_t count, HasUnfinished hasUnfinished,
    uint32_t online, uint32_t targetMax, uint32_t queueLeft)
{
    OrderLoginPick out;
    out.online = online;
    for (size_t i = 0; i < count; ++i)
    {
        if (out.indexes.size() >= queueLeft || out.online >= targetMax)
            break;
        if (!hasUnfinished(i))
            continue;
        out.indexes.push_back(i);
        ++out.online;
    }
    return out;
}

} // namespace GuildOrders
} // namespace TortoiseBots
