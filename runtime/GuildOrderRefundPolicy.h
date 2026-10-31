#pragma once

// Guild gold refund and bank-leg retry rules. Header-only and free of
// core/database types on purpose: exercised standalone in
// tools/test_guild_order_refund_policy.cpp. The drains feed this header
// plain counters they already keep per order.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// One banker visit repays what the bot still holds, capped by what it
// carries: repairs may have eaten copper after the withdraw, and the rest
// stays owed until the bot holds it again.
inline uint32_t GuildGoldRefundAmount(uint32_t held, uint32_t cash)
{
    return held < cash ? held : cash;
}

// After the drain deposits `deposited` of `held` copper, what is still
// owed. Saturates: a miscount never wraps the remainder up.
inline uint32_t GuildGoldRefundRemaining(uint32_t held, uint32_t deposited)
{
    return deposited >= held ? 0 : held - deposited;
}

// A failed gold top-up is tried once per listing: the buy action records
// the auction id it queued the top-up for and never re-queues for the same
// id, so the ladder moves down instead of walking bank to AH forever. A new
// listing (new id) may try again; the bank may have funds by then.
inline bool GoldTopUpAllowed(uint32_t auctionId, uint32_t triedAuctionId, uint32_t shortfall,
    bool bankGoldEnabled)
{
    return bankGoldEnabled && shortfall > 0 && auctionId != 0 && auctionId != triedAuctionId;
}

// An empty bank leg is not retried for the same item until the bank changes
// or a long cooldown passes: the ladder moves down instead of walking back
// and forth. The drain clears the marker when the bank shows more than the
// count seen, or when the cooldown elapses.
inline constexpr uint32_t kBankEmptyCooldownMs = 10 * 60 * 1000;

inline bool BankLegSkipped(uint32_t itemId, uint32_t emptyItemId, uint32_t nowMs, uint32_t emptyMs)
{
    return itemId != 0 && itemId == emptyItemId && nowMs - emptyMs < kBankEmptyCooldownMs;
}

// Leftover delivery deposits only order-caused stock: what the order gained
// (bank, AH, mail, vendor, farm) caps what may leave the bags, so the bot's
// own pre-existing materials stay put.
inline uint32_t SupplyLeftoverAllowed(uint32_t bagCount, uint32_t gained)
{
    return bagCount < gained ? bagCount : gained;
}

} // namespace GuildOrders
} // namespace TortoiseBots
