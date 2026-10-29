#pragma once

// Mailbox collection for guild orders: bought AH items arrive by mail, and
// delivery needs them in the bags, so order items waiting in the mailbox are
// collected before the bot farms, buys or delivers. Header-only and free of
// core/database types on purpose: exercised standalone in
// tools/test_guild_order_mail_policy.cpp. Callers feed it plain counters.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// The bot walks to a mailbox for its order items when mail waits and the
// bags still fall short. A buy whose auction visit is still open keeps
// buying first while an auctioneer is in reach, one listing per tick.
inline bool CollectOrderMailFirst(uint32_t bagCount, uint32_t delivered, uint32_t mailboxCount,
    uint32_t amount, bool auctionVisitOpen, bool auctioneerNearby)
{
    if (mailboxCount == 0 || amount == 0)
        return false;
    if (delivered + bagCount >= amount)
        return false;
    if (auctionVisitOpen && auctioneerNearby)
        return false;
    return true;
}

} // namespace GuildOrders
} // namespace TortoiseBots
