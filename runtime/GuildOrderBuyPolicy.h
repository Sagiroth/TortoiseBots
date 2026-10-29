#pragma once

// Purchase rule for guild buy orders: what a bot still needs to buy at the
// auction house, and when the auction attempt is over and the rest becomes a
// farm. Header-only and free of core/database types on purpose: exercised
// standalone in tools/test_guild_order_buy_policy.cpp. The buy action feeds
// this function plain counters it already keeps per order.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// Auction mail arrives about an hour after the sale, so bought items are
// still in the mailbox for a while: owned counts bags plus mailbox plus what
// was already delivered, and the order only switches to farming the rest
// once nothing affordable is listed.
inline uint32_t BuyOwnedCount(uint32_t delivered, uint32_t bagCount, uint32_t mailboxCount)
{
    return delivered + bagCount + mailboxCount;
}

inline uint32_t BuyStillMissing(uint32_t want, uint32_t owned)
{
    return owned >= want ? 0 : want - owned;
}

// One auction beats another when its per-item price is lower. Auction
// counts are stack sizes (>= 1), so the divisor is never zero.
inline bool BuyoutBeatsPerItem(uint32_t buyout, uint32_t count, uint32_t bestBuyout, uint32_t bestCount)
{
    return uint64_t(buyout) * bestCount < uint64_t(bestBuyout) * count;
}

} // namespace GuildOrders
} // namespace TortoiseBots
