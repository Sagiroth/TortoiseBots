#pragma once

// Keep-in-stock rules for guild orders: after a keep round lands in the bank
// the order watches it, and a round re-arms while the bank holds less than
// the stock target. Header-only and free of core/database types on purpose:
// exercised standalone in tools/test_guild_order_keep_policy.cpp. The store
// feeds this header the bank count it read for the round.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// One cheap bank look per keep order this often: a single summed row read,
// never per bot tick.
inline constexpr uint32_t kKeepBankCheckMs = 10 * 60 * 1000;

// Whether the keep watch is due for another bank look.
inline bool KeepCheckDue(uint32_t nowMs, uint32_t lastCheckMs)
{
    return nowMs - lastCheckMs >= kKeepBankCheckMs;
}

// Missing stock below the target: the next round quota. Zero at or above
// the target, and without a target there is nothing to keep.
inline uint32_t KeepMissing(uint32_t stockTarget, uint32_t bankCount)
{
    if (stockTarget == 0 || bankCount >= stockTarget)
        return 0;
    return stockTarget - bankCount;
}

// Whether a delivered keep round starts watching the bank instead of ending.
inline bool KeepWatchesBank(bool keep, uint32_t stockTarget)
{
    return keep && stockTarget != 0;
}

} // namespace GuildOrders
} // namespace TortoiseBots
