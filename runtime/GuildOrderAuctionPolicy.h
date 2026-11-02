#pragma once

// Auction-leg rules for guild orders: one search then hand off, the
// reachability deadline, and the buy/relist rule for oversized stacks.
// Header-only and free of core/database types on purpose: exercised
// standalone in tools/test_guild_order_auction_policy.cpp.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// An empty auctioneer search is often transient (the house just visited is
// cooling down after a mailbox trip), so the leg gets three tries like the
// bank. "Nothing on the AH" still moves on at once: the buy action hands the
// leg down the ladder when no listing fits.
inline constexpr uint32_t kAuctionSearchMovesLadderAfter = 3;

inline bool AuctionSearchMovesLadder(uint32_t failedSearches)
{
    return failedSearches >= kAuctionSearchMovesLadderAfter;
}

// Once the bot has arrived in the auctioneer's city it gets this long to
// reach a counter before the leg hands off.
inline constexpr uint32_t kAuctionUnreachableMs = 120 * 1000;

inline bool AuctionUnreachableElapsed(uint32_t startedMs, uint32_t nowMs)
{
    return startedMs != 0 && nowMs - startedMs >= kAuctionUnreachableMs;
}

// The walk itself is bounded too: a leg entered far from the city hands off
// after this much travel instead of crossing the world forever.
inline constexpr uint32_t kAuctionLegTravelCapMs = 20 * 60 * 1000;

inline bool AuctionLegTravelCapped(uint32_t legStartedMs, uint32_t nowMs)
{
    return legStartedMs != 0 && nowMs - legStartedMs >= kAuctionLegTravelCapMs;
}

// Cheapest per item: a listing beats another when its unit price is lower.
// Stack sizes are >= 1, so the divisors are never zero.
inline bool CheaperPerUnit(uint32_t buyout, uint32_t count, uint32_t bestBuyout, uint32_t bestCount)
{
    return uint64_t(buyout) * bestCount < uint64_t(bestBuyout) * count;
}

// Which listing the order buys within one class of listing. Among covering
// stacks the smallest wins (least surplus), and the unit price breaks ties;
// among partial stacks the cheapest per unit wins. Whether a covering stack
// is worth its surplus at all is AuctionCoveringBeatsPartials' call.
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

// Per-unit price of a listing, rounded up: a 301-copper stack of 20 is 16 a
// unit, so relisting never loses copper to truncation.
inline uint32_t AuctionUnitPrice(uint32_t buyout, uint32_t count)
{
    if (!count)
        return 0;
    return uint32_t((uint64_t(buyout) + count - 1) / count);
}

// One buyable listing: its whole-stack buyout and item count.
struct AuctionQuote
{
    uint32_t count = 0;
    uint32_t buyout = 0;
};

// What filling the miss from partial stacks costs, buying the cheapest per
// unit first. Zero when the partial stacks seen cannot fill it. Reports the
// cheapest per-unit price among them (zero when none qualifies).
inline uint32_t PartialFillCost(std::vector<AuctionQuote> const& partials, uint32_t missing,
    uint32_t& cheapestUnit)
{
    cheapestUnit = 0;
    if (!missing)
        return 0;
    std::vector<AuctionQuote> sorted;
    sorted.reserve(partials.size());
    for (AuctionQuote const& quote : partials)
        if (quote.count && quote.buyout)
            sorted.push_back(quote);
    std::sort(sorted.begin(), sorted.end(), [](AuctionQuote const& a, AuctionQuote const& b)
    {
        return CheaperPerUnit(a.buyout, a.count, b.buyout, b.count);
    });
    uint64_t cost = 0;
    uint64_t count = 0;
    for (AuctionQuote const& quote : sorted)
    {
        uint32_t const unit = AuctionUnitPrice(quote.buyout, quote.count);
        if (!cheapestUnit || unit < cheapestUnit)
            cheapestUnit = unit;
        if (count >= missing)
            continue;
        cost += quote.buyout;
        count += quote.count;
    }
    if (count < missing)
        return 0;
    return uint32_t(std::min<uint64_t>(cost, UINT32_MAX));
}

// Whether a covering stack is worth buying for its surplus. It wins when it
// costs no more than filling the miss from the cheap partial stacks, or, while
// those cannot fill the miss, when its per-unit price stays within twice the
// cheapest partial's. With no partial listing at all there is nothing cheaper
// to plan around, so the covering stack wins.
inline bool AuctionCoveringBeatsPartials(uint32_t coverBuyout, uint32_t coverCount,
    uint32_t partialFillCost, uint32_t cheapestPartialUnit)
{
    if (!coverCount || !coverBuyout)
        return false;
    if (partialFillCost)
        return coverBuyout <= partialFillCost;
    if (!cheapestPartialUnit)
        return true;
    return uint64_t(AuctionUnitPrice(coverBuyout, coverCount)) <= uint64_t(cheapestPartialUnit) * 2;
}

// A hand-off during a relist trip must not source the leg's item/count as
// missing supply: that item is the surplus the order already bought and was
// about to post. The hand-off drops the relist instead and leaves the stack in
// the bags for the bank drain.
inline bool AuctionHandoffLeavesSurplus(bool pendingRelist, uint32_t relistItemId, uint32_t legItemId)
{
    return pendingRelist && relistItemId && relistItemId == legItemId;
}

// What an oversized purchase leaves for the auction house.
inline uint32_t BuySurplus(uint32_t bought, uint32_t missing)
{
    return bought > missing ? bought - missing : 0;
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
