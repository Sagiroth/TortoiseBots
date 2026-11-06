#pragma once

// Delivery rule for guild orders: what happens after the bot worked the
// amount (or its bags filled up). Header-only and free of core/database
// types on purpose: exercised standalone in tools/test_guild_order_delivery_policy.cpp.
// The bank/mail code feeds this header plain counters it already keeps.
#include <cstddef>
#include <cstdint>
#include "GuildOrderContract.h"

namespace TortoiseBots
{
namespace GuildOrders
{

// The Turtle bank has five tabs, numbered 1-5 in the deposit message.
inline constexpr uint8_t kDeliveryFirstTab = 1;
inline constexpr uint8_t kDeliveryTabCount = 5;

// A save running on the bank refuses everything for a moment. The drain
// runs one pass per world tick and counts consecutive passes that moved
// nothing; only after that many passes is the bank treated as refused
// and the leftovers mailed.
inline constexpr uint32_t kDeliveryBankRetries = 3;

// Tab indices the deposit loop tries, in order.
inline uint8_t DeliveryTabAt(size_t index) { return uint8_t(kDeliveryFirstTab + index); }

// Consecutive no-move passes before the bank counts as refused.
inline bool BankRefused(uint32_t emptyPasses) { return emptyPasses >= kDeliveryBankRetries; }

// What the order still needs. The drain moves whole stacks until delivered
// reaches amount, smallest stacks first, then stops; overshoot is at most
// one stack and surplus stacks past the quota stay in the bags.
inline uint32_t DeliveryNeeded(uint32_t delivered, uint32_t amount)
{
    return delivered >= amount ? 0 : amount - delivered;
}

// Whether another whole stack may move: only while the quota is unmet.
inline bool DeliveryMovesOn(uint32_t delivered, uint32_t amount)
{
    return delivered < amount;
}

// Index of the smallest stack (first wins ties), or count when empty.
inline size_t SmallestStackAt(uint32_t const* counts, size_t stackCount)
{
    size_t best = stackCount;
    for (size_t i = 0; i < stackCount; ++i)
        if (best == stackCount || counts[i] < counts[best])
            best = i;
    return best;
}

// State the order lands in after a delivery attempt. The bank may take a
// few more than ordered (a partial stack fills up in the bank), a partial
// (bags-full) delivery sends the bot back to its farm zone, and an order
// nothing could move at all (no bank, no receiver) ends stuck.
enum class DeliveryOutcome : uint8_t
{
    Done,        // amount reached, everything in the bank
    Mailed,      // amount reached, at least one stack mailed
    BackToWork,  // partial delivery: keep working
    Stuck,       // nothing moved and nothing can: cant / stuck
};

// Whether the delivery ends after this pass: the quota is reached, the
// bank refused after retries, or the bot holds no more of the order item.
// Empty bags end it at once: the bank action only re-queues while the bot
// carries the item, so waiting for refused passes would idle instead.
inline bool DeliveryFinished(uint32_t emptyPasses, uint32_t carried, uint32_t delivered, uint32_t amount)
{
    return delivered >= amount || BankRefused(emptyPasses) || carried == 0;
}

// movedThisDelivery covers the whole delivery, not just this pass: the
// last stack may have moved in an earlier pass.
inline DeliveryOutcome ClassifyDelivery(uint32_t delivered, uint32_t amount, bool mailedAny, bool movedThisDelivery)
{
    if (delivered >= amount)
        return mailedAny ? DeliveryOutcome::Mailed : DeliveryOutcome::Done;
    if (!movedThisDelivery)
        return DeliveryOutcome::Stuck;
    return DeliveryOutcome::BackToWork;
}

// A timed-out (or gain-stalled) delivery never goes back to work: the farm
// time is spent or the watchdog bit, so whatever reached the guild is the
// final count and the order ends with the reason kept.
inline bool DeliveryTimeoutEnds(OrderReason reason, DeliveryOutcome outcome)
{
    return (reason == OrderReason::Timeout || reason == OrderReason::NoProgress) &&
        (outcome == DeliveryOutcome::BackToWork || outcome == DeliveryOutcome::Stuck);
}
// A delivering bot whose banker search keeps coming back empty walks to a
// mailbox and mails the guild master instead: the items still reach the
// guild, and the bot never parks on an unreachable banker.
inline constexpr uint32_t kDeliveryNoBankerMails = 3;

// Whether the empty-banker-search count flips the delivery to the mail
// leg. Counts only consecutive failures: a banker search that offers
// anything resets it.
inline bool DeliveryMailsAfterNoBanker(uint32_t noBankerSearches)
{
    return noBankerSearches >= kDeliveryNoBankerMails;
}

// A guild with no bank tabs at all has nowhere to deposit: the delivery
// skips the banker search and mails the order instead.
inline bool DeliverySkipsBanker(uint32_t bankTabCount)
{
    return bankTabCount == 0;
}

// One stack per mail; the server takes a single item per mail.
inline uint32_t DeliveryMailCount(size_t stacksLeft) { return uint32_t(stacksLeft); }

// One material's share of the giver-surplus split: what still sits in the
// bags up to the giver's unattributed remaining share goes back to the
// giver by mail; everything past that keeps today's path (guild bank, then
// mail to the guild master). The giver's share was spent first, so the row
// only names what the bot did not already use.
struct GiverSurplusSplit
{
    uint32_t itemId = 0;
    uint32_t giverReturn = 0; // by mail to the giver
    uint32_t bankRemainder = 0; // through the existing bank/mail path
};

inline GiverSurplusSplit SplitGiverSurplus(uint32_t itemId, uint32_t inBags, uint32_t giverRemaining)
{
    GiverSurplusSplit split;
    split.itemId = itemId;
    split.giverReturn = inBags < giverRemaining ? inBags : giverRemaining;
    split.bankRemainder = inBags - split.giverReturn;
    return split;
}

// One order's spent reagents, plain counts the callers already keep: the
// giver's share goes first, then the order-caused gain ledger. The row only
// names what the bot did not already use at the end, so consumption narrows
// both legs the same way.
struct GiverSpendShare
{
    uint32_t giverSpent = 0;
    uint32_t gainSpent = 0;
};

inline GiverSpendShare SplitGiverSpend(uint32_t count, uint32_t giverRemaining, uint32_t gainRemaining)
{
    GiverSpendShare share;
    share.giverSpent = count < giverRemaining ? count : giverRemaining;
    uint32_t left = count - share.giverSpent;
    share.gainSpent = left < gainRemaining ? left : gainRemaining;
    return share;
}

} // namespace GuildOrders
} // namespace TortoiseBots
