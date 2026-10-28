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

// A save running on the bank refuses everything for a moment. The bank
// action retries that many times over later ticks before treating the
// tab as refused and moving on to mail.
inline constexpr uint32_t kDeliveryBankRetries = 3;

// Tab indices the deposit loop tries, in order.
inline uint8_t DeliveryTabAt(size_t index) { return uint8_t(kDeliveryFirstTab + index); }

// State the order lands in after a delivery attempt. The bank may take a
// few more than ordered (a partial stack fills up in the bank), and a
// partial (bags-full) delivery sends the bot back to its farm zone.
enum class DeliveryOutcome : uint8_t
{
    Done,        // amount reached, everything in the bank
    Mailed,      // amount reached, at least one stack mailed
    BackToWork,  // partial delivery: keep working
};

inline DeliveryOutcome ClassifyDelivery(uint32_t delivered, uint32_t amount, bool mailedAny)
{
    if (delivered >= amount)
        return mailedAny ? DeliveryOutcome::Mailed : DeliveryOutcome::Done;
    return DeliveryOutcome::BackToWork;
}

// One stack per mail; the server takes a single item per mail.
inline uint32_t DeliveryMailCount(size_t stacksLeft) { return uint32_t(stacksLeft); }

} // namespace GuildOrders
} // namespace TortoiseBots
