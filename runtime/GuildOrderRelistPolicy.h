#pragma once

// Guild AH relist ledger rules. An order that buys an oversized stack relists
// the surplus; the sale gold or the returned item arrives later by mail, often
// after the order is gone. The ledger keeps that guild-funded value attached
// to the guild until the bank gets it. Header-only and free of core/database
// types on purpose: exercised standalone in
// tools/test_guild_order_relist_policy.cpp.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// One posted relist: what the auction house holds for the guild. `seq` is the
// per-bot row key of the store (the second part of the table's primary key).
// The listing terms are the identity the core gives back: the sale mail body
// carries the bid, buyout and deposit of the auction that settled, and an
// expired mail returns the posted stack. `expires_at` is the posting plus its
// duration, so an expired notification can only settle a listing that is over.
struct RelistLedgerEntry
{
    uint32_t seq = 0;
    uint32_t botGuidLow = 0;
    uint32_t guildId = 0;
    uint32_t itemId = 0;
    uint32_t count = 0;
    uint32_t unitPrice = 0;
    uint32_t bid = 0;        // the start bid the stack was posted with
    uint32_t buyout = 0;     // the listing's buyout for the whole stack
    uint32_t deposit = 0;    // the deposit the auction house charged
    uint64_t postedAtSec = 0;
    uint64_t expiresAtSec = 0;
    bool received = false;   // the mail was read: the value is in hand
    uint32_t heldGold = 0;   // sale copper waiting for the bank trip
    uint32_t heldCount = 0;  // returned items waiting for the bank trip
};

// The auction mail fields the matching rules read. The core writes
// "<item>:<random>:<state>" as the subject of an auction notification
// (Mail.h AuctionMailState: 2 successful, 3 expired) and, for a sale, the
// listing terms into the body: "<bidder>:<bid>:<buyout>:<deposit>:<cut>"
// (AuctionHouseMgr::SendAuctionSuccessfulMail). The item id in the subject is
// the same template entry the ledger row was posted with.
struct RelistMail
{
    bool auction = false;       // stationery is the auction house
    uint32_t subjectItemId = 0; // item id from the subject, 0 when unparsed
    uint32_t subjectState = 0;  // auction mail state; 0 = not an auction notification
    bool hasItems = false;
    uint32_t money = 0;
    uint32_t itemId = 0;        // first returned item's entry (expired mail)
    uint32_t count = 0;         // its count
    bool bodyParsed = false;    // a sale notification body was read
    uint32_t bodyBid = 0;       // what the buyer paid
    uint32_t bodyBuyout = 0;    // the listing's buyout
    uint32_t bodyDeposit = 0;   // the listing's deposit
};

inline constexpr uint32_t kRelistMailSuccessful = 2;
inline constexpr uint32_t kRelistMailExpired = 3;

enum class RelistMailKind : uint8_t
{
    None,
    Sold,
    Expired,
};

// An expired notification can only settle a listing whose time is over, so a
// return of an older listing never pays a newer row of the same item.
inline bool RelistListingPassed(RelistLedgerEntry const& entry, uint64_t nowSec)
{
    return entry.expiresAtSec && nowSec >= entry.expiresAtSec;
}

// What one mail settles for the entry. A sale must carry the listing's own
// buyout and deposit, and a bid between the posted start bid and the buyout: a
// different auction of the same item (or the bot's own sale) never matches.
// An expired notification must return the posted count after the listing ran
// out; a won-auction mail (state 1) never counts as a return.
inline RelistMailKind RelistMailMatch(RelistLedgerEntry const& entry, RelistMail const& mail,
    uint64_t nowSec)
{
    if (!entry.itemId || !entry.count || !mail.auction || mail.subjectItemId != entry.itemId)
        return RelistMailKind::None;
    if (mail.subjectState == kRelistMailSuccessful && mail.money && !mail.hasItems &&
        mail.bodyParsed && mail.bodyBuyout == entry.buyout && mail.bodyDeposit == entry.deposit &&
        mail.bodyBid >= entry.bid && mail.bodyBid <= entry.buyout)
        return RelistMailKind::Sold;
    if (mail.subjectState == kRelistMailExpired && !mail.money && mail.hasItems &&
        mail.itemId == entry.itemId && mail.count == entry.count &&
        RelistListingPassed(entry, nowSec))
        return RelistMailKind::Expired;
    return RelistMailKind::None;
}

// How many unread rows the mail matches, and the one row's index when it is
// exactly one. The mail read only settles a single match: with none or more
// than one the mail is left to normal mail handling, because consuming the
// wrong auction's value would charge one guild for another's sale.
inline size_t RelistMatchCount(std::vector<RelistLedgerEntry> const& rows, RelistMail const& mail,
    uint64_t nowSec, size_t& index)
{
    size_t matches = 0;
    index = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].received || RelistMailMatch(rows[i], mail, nowSec) == RelistMailKind::None)
            continue;
        if (!matches)
            index = i;
        ++matches;
    }
    return matches;
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
