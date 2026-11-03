#pragma once

// Final-deposit rule for cancelled guild sub-orders: when a branch cancel (or
// a parent ending) removes a child row whose bot already holds the child's
// finished order items, the child deposits those items into the guild bank
// through the normal delivery path instead of keeping them in its bags.
// Header-only and free of core/database types on purpose: exercised
// standalone in tools/test_guild_order_cancel_deposit_policy.cpp. The store
// feeds this header the child's row facts it already holds under its lock.
#include <cstdint>

#include "GuildOrderContract.h"

namespace TortoiseBots
{
namespace GuildOrders
{

// A cancelled child that already crossed its quota heads to the bank instead
// of ending with the stack in its bags. The delivery drain re-checks the
// live bot, guild and banker before every pass, so the stranded-deposit edge
// below (and not the predicate) covers an unreachable banker: the state
inline bool CancelledChildDeposits(OrderState state, OrderReason reason, uint32_t delivered,
    uint32_t bagCount, uint32_t /*mailboxCount*/, uint32_t amount)
{
    if (state == OrderState::Delivering)
        return true;
    if (state != OrderState::Working || amount == 0)
        return false;
    if (reason == OrderReason::Grouped)
        return false;
    if (delivered >= amount)
        return false;
    // Mailbox items still need a pickup leg the cancelled row no longer
    // runs, so only bags count: the bank deposit cannot take them by mail.
    uint32_t const held = bagCount;
    if (held == 0)
        return false;
    return delivered + held >= amount;
}

} // namespace GuildOrders
} // namespace TortoiseBots
