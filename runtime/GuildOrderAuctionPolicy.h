#pragma once

// Auction-leg rules for guild orders: one search then hand off, the
// reachability deadline, and the buy/relist rule for oversized stacks.
// Header-only and free of core/database types on purpose: exercised
// standalone in tools/test_guild_order_auction_policy.cpp.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// The auction leg gets one search. A leg whose auctioneer search comes back
// empty moves straight down the ladder instead of parking for three tries.
inline constexpr uint32_t kAuctionSearchMovesLadderAfter = 1;

inline bool AuctionSearchMovesLadder(uint32_t failedSearches)
{
    return failedSearches >= kAuctionSearchMovesLadderAfter;
}

// A leg that has not reached interact range of an auctioneer by this point
// is unreachable: the bot hands off instead of walking at a city forever.
inline constexpr uint32_t kAuctionUnreachableMs = 120 * 1000;

inline bool AuctionUnreachableElapsed(uint32_t startedMs, uint32_t nowMs)
{
    return startedMs != 0 && nowMs - startedMs >= kAuctionUnreachableMs;
}

// Cheapest per item: a listing beats another when its unit price is lower.
// Stack sizes are >= 1, so the divisors are never zero.
inline bool CheaperPerUnit(uint32_t buyout, uint32_t count, uint32_t bestBuyout, uint32_t bestCount)
{
    return uint64_t(buyout) * bestCount < uint64_t(bestBuyout) * count;
}

// Which listing the order buys. A stack that covers the whole miss beats a
// partial one, because one trip should finish the leg; among covering stacks
// the smallest wins (least surplus), and the unit price breaks ties. With no
// covering stack, the cheapest per unit among the partial stacks wins.
inline bool AuctionPickBeats(uint32_t count, uint32_t buyout, uint32_t bestCount, uint32_t bestBuyout,
    uint32_t missing)
{
    bool const covers = count >= missing;
    bool const bestCovers = bestCount >= missing;
    if (covers != bestCovers)
        return covers;
    if (covers && count != bestCount)
        return count < bestCount;
    return CheaperPerUnit(buyout, count, bestBuyout, bestCount);
}

// What an oversized purchase leaves for the auction house.
inline uint32_t BuySurplus(uint32_t bought, uint32_t missing)
{
    return bought > missing ? bought - missing : 0;
}

// Per-unit price of a listing, rounded up: a 301-copper stack of 20 is 16 a
// unit, so relisting never loses copper to truncation.
inline uint32_t AuctionUnitPrice(uint32_t buyout, uint32_t count)
{
    if (!count)
        return 0;
    return uint32_t((uint64_t(buyout) + count - 1) / count);
}

inline uint32_t RelistUnitPriceFromUnit(uint32_t boughtUnitPrice, uint32_t lowestUnitPrice)
{
    return boughtUnitPrice > lowestUnitPrice ? boughtUnitPrice : lowestUnitPrice;
}

// Relist at no less than the bought unit price and no less than the current
// lowest listing for the item.
inline uint32_t RelistUnitPrice(uint32_t boughtBuyout, uint32_t boughtCount, uint32_t lowestUnitPrice)
{
    return RelistUnitPriceFromUnit(AuctionUnitPrice(boughtBuyout, boughtCount), lowestUnitPrice);
}

// Buyout for the surplus, clamped to the core's 2,000,000,000 copper limit.
inline uint32_t RelistTotal(uint32_t unitPrice, uint32_t count)
{
    uint64_t const total = uint64_t(unitPrice) * count;
    uint64_t const cap = 2000000000u;
    return uint32_t(total > cap ? cap : total);
}

// Normal auction duration: the core accepts 2, 8 or 24 hours in minutes.
inline constexpr uint32_t kRelistDurationMinutes = 8 * 60;

// Where an unpostable surplus goes: nowhere when the relist landed, else the
// order's normal leftover path, which deposits it into the guild bank at
// delivery (the craft leftover pass, or the delivery bank pass).
enum class SurplusDestination : uint8_t
{
    None,
    DeliveryLeftover,
};

inline SurplusDestination SurplusDestinationAfterRelist(bool posted, uint32_t surplus)
{
    if (posted || !surplus)
        return SurplusDestination::None;
    return SurplusDestination::DeliveryLeftover;
}

} // namespace GuildOrders
} // namespace TortoiseBots
