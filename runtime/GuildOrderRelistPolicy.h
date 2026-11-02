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

// The row a mail may settle. Look-alike rows of one guild (the same terms
// posted twice, for instance two visits that both posted) are safe to settle
// as the oldest: that guild is credited either way. Matches spanning guilds
// stay ambiguous and consume nothing, because one guild would be paid with
// another auction's value.
inline bool RelistSettleIndex(std::vector<RelistLedgerEntry> const& rows, RelistMail const& mail,
    uint64_t nowSec, size_t& index)
{
    size_t matches = 0;
    size_t oldest = 0;
    uint32_t guildId = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].received || RelistMailMatch(rows[i], mail, nowSec) == RelistMailKind::None)
            continue;
        if (!matches)
        {
            oldest = i;
            guildId = rows[i].guildId;
        }
        else
        {
            if (rows[i].guildId != guildId)
                return false;
            if (rows[i].seq < rows[oldest].seq)
                oldest = i;
        }
        ++matches;
    }
    if (!matches)
        return false;
    index = oldest;
    return true;
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

inline bool RelistNeedsDeposit(RelistLedgerEntry const& entry)
{
    return entry.received && (entry.heldGold > 0 || entry.heldCount > 0);
}

// A banker visit only earns its trip when the bot can hand the value over
// now: sale gold in its purse, or the returned stack still in its bags.
inline bool RelistDepositableNow(RelistLedgerEntry const& entry, uint32_t cash, uint32_t bagCount)
{
    return RelistNeedsDeposit(entry) && ((entry.heldGold && cash) || (entry.heldCount && bagCount));
}

// What one bank visit knows about the bot: its guild now, whether a banker is
// in reach (alive, out of combat, the guild has a bank), and what it carries.
struct RelistTripBot
{
    uint32_t guildId = 0;
    bool atBank = false;
    uint32_t cash = 0;
    uint32_t bagCount = 0; // the row's item in the bags
};

// What the bank took in one pass.
struct RelistMoved
{
    uint32_t gold = 0;
    uint32_t items = 0;
};

enum class RelistTripOutcome : uint8_t
{
    Idle,      // nothing owed here, or no banker in reach: the row waits
    Dropped,   // the bot left the row's guild: the row is gone
    Refused,   // a ledger write failed first: nothing was deposited
    Deposited, // the bank took the planned value
    Returned,  // the bank took less: the rest is written back to the row
    Lost,      // the bank took less and the rest could not be written back
};

// One bank trip for one ledger row. Order matters: the planned value is saved
// off the row BEFORE the bank is asked, so a failed ledger write means no
// deposit and a crash in between loses the value rather than paying it twice.
// What the bank refused is put back on the row. Ledger needs Save(row) and
// Delete(row), each false on a failed write; Bank needs
// Deposit(gold, items) -> RelistMoved.
template <typename Ledger, typename Bank>
RelistTripOutcome RelistBankTrip(RelistLedgerEntry const& entry, RelistTripBot const& bot,
    Ledger& ledger, Bank& bank)
{
    if (!RelistNeedsDeposit(entry))
        return RelistTripOutcome::Idle;
    if (entry.guildId != bot.guildId)
        return ledger.Delete(entry) ? RelistTripOutcome::Dropped : RelistTripOutcome::Refused;
    if (!bot.atBank)
        return RelistTripOutcome::Idle;
    uint32_t const gold = entry.heldGold < bot.cash ? entry.heldGold : bot.cash;
    uint32_t const items = entry.heldCount < bot.bagCount ? entry.heldCount : bot.bagCount;
    if (!gold && !items)
        return RelistTripOutcome::Idle;

    RelistLedgerEntry reserved = entry;
    reserved.heldGold -= gold;
    reserved.heldCount -= items;
    bool const saved = RelistNeedsDeposit(reserved) ? ledger.Save(reserved) : ledger.Delete(entry);
    if (!saved)
        return RelistTripOutcome::Refused;

    RelistMoved const moved = bank.Deposit(gold, items);
    uint32_t const goldTook = moved.gold < gold ? moved.gold : gold;
    uint32_t const itemsTook = moved.items < items ? moved.items : items;
    if (goldTook == gold && itemsTook == items)
        return RelistTripOutcome::Deposited;
    RelistLedgerEntry owed = entry;
    owed.heldGold -= goldTook;
    owed.heldCount -= itemsTook;
    return ledger.Save(owed) ? RelistTripOutcome::Returned : RelistTripOutcome::Lost;
}

} // namespace GuildOrders
} // namespace TortoiseBots
