#pragma once

// Delivery rule for guild orders: what happens after the bot worked the
// amount (or its bags filled up). Header-only and free of core/database
// types on purpose: exercised standalone in tools/test_guild_order_delivery_policy.cpp.
// The bank/mail code feeds this header plain counters it already keeps.
#include <cstddef>
#include <cstdint>

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

inline DeliveryOutcome ClassifyDelivery(uint32_t delivered, uint32_t amount, bool mailedAny, bool movedAny)
{
    if (delivered >= amount)
        return mailedAny ? DeliveryOutcome::Mailed : DeliveryOutcome::Done;
    if (!movedAny)
        return DeliveryOutcome::Stuck;
    return DeliveryOutcome::BackToWork;
}

// One stack per mail; the server takes a single item per mail.
inline uint32_t DeliveryMailCount(size_t stacksLeft) { return uint32_t(stacksLeft); }

} // namespace GuildOrders
} // namespace TortoiseBots
