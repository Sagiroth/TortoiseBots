#pragma once

// Guild AH relist ledger rules. An order that buys an oversized stack relists
// the surplus; the sale gold or the returned item arrives later by mail, often
// after the order is gone. The ledger keeps that guild-funded value attached
// to the guild until the bank gets it. Header-only and free of core/database
// types on purpose: exercised standalone in
// tools/test_guild_order_relist_policy.cpp.

#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// One posted relist: what the auction house holds for the guild. `seq` is the
// per-bot row key of the store (the second part of the table's primary key).
struct RelistLedgerEntry
{
    uint32_t seq = 0;
    uint32_t botGuidLow = 0;
    uint32_t guildId = 0;
    uint32_t itemId = 0;
    uint32_t count = 0;
    uint32_t unitPrice = 0;
    uint64_t postedAtSec = 0;
    bool received = false;   // the mail was read: the value is in hand
    uint32_t heldGold = 0;   // sale copper waiting for the bank trip
    uint32_t heldCount = 0;  // returned items waiting for the bank trip
};

// The auction mail fields the matching rules read. The core writes
// "<item>:<random>:<state>" as the subject of an auction notification
// (Mail.h AuctionMailState: 2 successful, 3 expired) and, for a sale, the
// listing terms as the body.
struct RelistMail
{
    bool auction = false;       // stationery is the auction house
    uint32_t subjectItemId = 0; // item id from the subject, 0 when unparsed
    uint32_t subjectState = 0;  // auction mail state; 0 = not an auction notification
    bool hasItems = false;
    uint32_t money = 0;
    uint32_t itemId = 0;        // first returned item's entry (expired mail)
    uint32_t count = 0;         // its count
};

inline constexpr uint32_t kRelistMailSuccessful = 2;
inline constexpr uint32_t kRelistMailExpired = 3;

enum class RelistMailKind : uint8_t
{
    None,
    Sold,
    Expired,
};

// What one mail settles for the entry. A sale pays copper only; an expired
// listing returns the posted stack with no copper and no message body. The
// subject names the item, so a sale of the bot's own loot never pays a relist
// ledger entry, and a won-auction mail (state 1) never counts as a return.
inline RelistMailKind RelistMailMatch(RelistLedgerEntry const& entry, RelistMail const& mail)
{
    if (!entry.itemId || !entry.count || !mail.auction || mail.subjectItemId != entry.itemId)
        return RelistMailKind::None;
    if (mail.subjectState == kRelistMailSuccessful && mail.money && !mail.hasItems)
        return RelistMailKind::Sold;
    if (mail.subjectState == kRelistMailExpired && !mail.money && mail.hasItems &&
        mail.itemId == entry.itemId && mail.count >= entry.count)
        return RelistMailKind::Expired;
    return RelistMailKind::None;
}

// Nothing returned after three days: the posting can no longer report on the
// auction house, so the entry stops being a payable debt (the store drops it
// with a log line).
inline constexpr uint64_t kRelistStaleSec = 3 * 24 * 60 * 60;

inline bool RelistEntryStale(RelistLedgerEntry const& entry, uint64_t nowSec)
{
    return !entry.received && nowSec > entry.postedAtSec &&
        nowSec - entry.postedAtSec >= kRelistStaleSec;
}

// A mail the store matched: the value measured into the bot's hands is now
// owed to the guild, and the entry waits for the bank trip that pays it. The
// returned item count never counts past the posted stack.
inline void RelistNoteMailReceived(RelistLedgerEntry& entry, RelistMailKind kind, uint32_t gold,
    uint32_t items)
{
    entry.received = true;
    if (kind == RelistMailKind::Sold)
        entry.heldGold = gold;
    else if (kind == RelistMailKind::Expired)
        entry.heldCount = items < entry.count ? items : entry.count;
}

// What a ledger entry still owes the guild after a bank pass moved its
// value. A pass that moved nothing (no banker, a refused message) leaves the
// whole amount for the next trip.
inline uint32_t RelistDepositRemaining(uint32_t held, uint32_t moved)
{
    return moved >= held ? 0 : held - moved;
}

inline bool RelistNeedsDeposit(RelistLedgerEntry const& entry)
{
    return entry.received && (entry.heldGold > 0 || entry.heldCount > 0);
}

// Apply one bank pass. Returns true only when nothing is left to deposit, so
// the caller drops the entry; an unreachable or refusing bank keeps it.
inline bool RelistNoteDeposit(RelistLedgerEntry& entry, uint32_t goldMoved, uint32_t itemMoved)
{
    entry.heldGold = RelistDepositRemaining(entry.heldGold, goldMoved);
    entry.heldCount = RelistDepositRemaining(entry.heldCount, itemMoved);
    return entry.received && !RelistNeedsDeposit(entry);
}

} // namespace GuildOrders
} // namespace TortoiseBots
