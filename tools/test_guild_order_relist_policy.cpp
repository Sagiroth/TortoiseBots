// Standalone test for runtime/GuildOrderRelistPolicy.h: the relist ledger a
// guild order keeps after it posts its bought surplus on the auction house.
// Fake mail, no core and no database.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_relist_policy.cpp -o /tmp/test_guild_order_relist_policy
//   /tmp/test_guild_order_relist_policy
#include "../runtime/GuildOrderAuctionPolicy.h"
#include "../runtime/GuildOrderRelistPolicy.h"

#include <cstdio>
#include <vector>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

namespace
{
constexpr uint32_t kBot = 42;
constexpr uint32_t kGuild = 7;
constexpr uint32_t kOtherGuild = 9;
constexpr uint32_t kItem = 3857; // mageweave cloth, a stack that splits
constexpr uint64_t kNow = 2000000000;
constexpr uint64_t kDurationSec = kRelistDurationMinutes * 60;
constexpr uint32_t kBuyout = 5200; // 13 x 400
constexpr uint32_t kBid = 4940;    // 95% of the buyout
constexpr uint32_t kDeposit = 130;

RelistLedgerEntry Posted(uint32_t count = 13, uint32_t unitPrice = 400, uint64_t postedAtSec = kNow,
    uint32_t guildId = kGuild)
{
    RelistLedgerEntry entry;
    entry.seq = 1;
    entry.botGuidLow = kBot;
    entry.guildId = guildId;
    entry.itemId = kItem;
    entry.count = count;
    entry.unitPrice = unitPrice;
    entry.bid = kBid;
    entry.buyout = kBuyout;
    entry.deposit = kDeposit;
    entry.postedAtSec = postedAtSec;
    entry.expiresAtSec = postedAtSec + kDurationSec;
    return entry;
}

// The core writes the item, the random property and the notification state
// into the subject, and the listing terms into the body of a sale.
RelistMail SoldMail(uint32_t itemId = kItem, uint32_t money = 5100, uint32_t bid = kBuyout,
    uint32_t buyout = kBuyout, uint32_t deposit = kDeposit)
{
    RelistMail mail;
    mail.auction = true;
    mail.subjectItemId = itemId;
    mail.subjectState = kRelistMailSuccessful;
    mail.money = money;
    mail.bodyParsed = true;
    mail.bodyBid = bid;
    mail.bodyBuyout = buyout;
    mail.bodyDeposit = deposit;
    return mail;
}

RelistMail ExpiredMail(uint32_t itemId = kItem, uint32_t count = 13)
{
    RelistMail mail;
    mail.auction = true;
    mail.subjectItemId = itemId;
    mail.subjectState = kRelistMailExpired;
    mail.hasItems = true;
    mail.itemId = itemId;
    mail.count = count;
    return mail;
}

// A won-auction mail: same item, items, no money, but the core marks it with
// the won state instead of the expired one.
RelistMail WonMail(uint32_t itemId = kItem, uint32_t count = 13)
{
    RelistMail mail = ExpiredMail(itemId, count);
    mail.subjectState = 1;
    return mail;
}

// Mirrors the store: a matched mail marks the row received.
void ReadMail(RelistLedgerEntry& entry, RelistMail const& mail, uint64_t nowSec = kNow)
{
    RelistMailKind const kind = RelistMailMatch(entry, mail, nowSec);
    if (kind == RelistMailKind::Sold)
        RelistNoteMailReceived(entry, kind, mail.money, 0);
    else if (kind == RelistMailKind::Expired)
        RelistNoteMailReceived(entry, kind, 0, mail.count);
}
} // namespace

int main()
{
    // A sale mail carrying the listing's terms settles as gold; a sale of
    // another item (the bot's own loot) never touches the ledger.
    CHECK(RelistMailMatch(Posted(), SoldMail(), kNow) == RelistMailKind::Sold);
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem + 1), kNow) == RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem, 0), kNow) == RelistMailKind::None);
    RelistMail notAuction = SoldMail();
    notAuction.auction = false;
    CHECK(RelistMailMatch(Posted(), notAuction, kNow) == RelistMailKind::None);
    RelistMail noBody = SoldMail();
    noBody.bodyParsed = false;
    CHECK(RelistMailMatch(Posted(), noBody, kNow) == RelistMailKind::None);

    // An unrelated auction of the same item never settles the row: a
    // different buyout, a different deposit, or a bid outside the posting's
    // start bid ... buyout range is another listing.
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem, 6100, 5700, 6000, kDeposit), kNow) ==
        RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem, 5400, 5130, kBuyout, 260), kNow) ==
        RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem, 5100, kBid - 1), kNow) == RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem, 5400, kBuyout + 1), kNow) == RelistMailKind::None);
    // A bid at the posted start bid and a buyout purchase both settle.
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem, 5100, kBid), kNow) == RelistMailKind::Sold);
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem, 5100, kBuyout), kNow) == RelistMailKind::Sold);

    // An expired listing returns the posted stack, but only once its listing
    // time is over; a won-auction mail of the same item is the bot's own
    // purchase, not a return.
    CHECK(RelistMailMatch(Posted(), ExpiredMail(), kNow) == RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(), ExpiredMail(), kNow + kDurationSec) == RelistMailKind::Expired);
    CHECK(RelistMailMatch(Posted(), WonMail(), kNow + kDurationSec) == RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(), ExpiredMail(kItem + 1), kNow + kDurationSec) ==
        RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(13), ExpiredMail(kItem, 12), kNow + kDurationSec) ==
        RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(13), ExpiredMail(kItem, 20), kNow + kDurationSec) ==
        RelistMailKind::None);
    {
        // A newer row of the same item cannot be settled by the older
        // listing's expiry.
        RelistLedgerEntry newer = Posted(13, 400, kNow + kDurationSec);
        CHECK(RelistMailMatch(newer, ExpiredMail(), kNow + kDurationSec) == RelistMailKind::None);
    }

    // A mail settles exactly one row. Two guilds' rows of the same item stay
    // apart because each sale carries its own listing's terms, and a mail
    // that fits both is ambiguous: the ledger consumes neither.
    {
        std::vector<RelistLedgerEntry> rows{Posted(), Posted(13, 400, kNow, kOtherGuild)};
        rows[0].buyout = 5200;
        rows[0].bid = 4940;
        rows[1].buyout = 6000;
        rows[1].bid = 5700;
        size_t index = 99;
        CHECK(RelistMatchCount(rows, SoldMail(kItem, 5100, 4940, 5200, kDeposit), kNow, index) == 1);
        CHECK(index == 0);
        ReadMail(rows[index], SoldMail(kItem, 5100, 4940, 5200, kDeposit));
        CHECK(rows[0].received && rows[0].heldGold == 5100);
        CHECK(rows[0].guildId == kGuild && !rows[1].received);
        CHECK(RelistMatchCount(rows, SoldMail(kItem, 5800, 5700, 6000, kDeposit), kNow, index) == 1);
        CHECK(index == 1);
        ReadMail(rows[index], SoldMail(kItem, 5800, 5700, 6000, kDeposit));
        CHECK(rows[1].received && rows[1].guildId == kOtherGuild);

        std::vector<RelistLedgerEntry> twins{Posted(), Posted(13, 400, kNow, kOtherGuild)};
        CHECK(RelistMatchCount(twins, SoldMail(), kNow, index) == 2);
        // None of the twins is read: an ambiguous mail is left to normal
        // mail handling and no guild is credited.
        CHECK(!twins[0].received && !twins[1].received);
        twins[0].received = true;
        CHECK(RelistMatchCount(twins, SoldMail(), kNow, index) == 1);
        CHECK(index == 1);
        CHECK(RelistMatchCount(twins, SoldMail(kItem + 1), kNow, index) == 0);
    }

    // Sold -> the gold is in the bot's hands, the row waits for the bank, and
    // a funded bank trip drops it.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        CHECK(entry.received);
        CHECK(entry.heldGold == 5100);
        CHECK(entry.heldCount == 0);
        CHECK(RelistNeedsDeposit(entry));
        CHECK(RelistNoteDeposit(entry, 5100, 0));
        CHECK(!RelistNeedsDeposit(entry));
    }

    // Expired -> the posted stack is back in the bags and the bank trip
    // drops the row.
    {
        RelistLedgerEntry entry = Posted(13);
        ReadMail(entry, ExpiredMail(kItem, 13), kNow + kDurationSec);
        CHECK(entry.received);
        CHECK(entry.heldCount == 13);
        CHECK(entry.heldGold == 0);
        CHECK(RelistNoteDeposit(entry, 0, 13));
    }

    // The ledger takes no order input at all, so a row posted by an order that
    // has since finished (or was never persisted) settles the same way.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail());
        CHECK(entry.received);
        CHECK(RelistNoteDeposit(entry, 5100, 0));
    }

    // Bank unreachable: the pass moved nothing, the row keeps the whole debt
    // and stays due, and the next trip still pays it.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        CHECK(!RelistNoteDeposit(entry, 0, 0));
        CHECK(entry.heldGold == 5100);
        CHECK(RelistNeedsDeposit(entry));
        CHECK(!RelistNoteDeposit(entry, 2500, 0)); // partial pass
        CHECK(entry.heldGold == 2600);
        CHECK(RelistNoteDeposit(entry, 2600, 0));
    }

    // Three days with nothing back drops the row; a row already collected
    // waits for the bank however long that takes.
    CHECK(!RelistEntryStale(Posted(), kNow));
    CHECK(!RelistEntryStale(Posted(13, 400, kNow - kRelistStaleSec + 1), kNow));
    CHECK(RelistEntryStale(Posted(13, 400, kNow - kRelistStaleSec), kNow));
    CHECK(RelistEntryStale(Posted(13, 400, kNow - 10 * kRelistStaleSec), kNow));
    {
        RelistLedgerEntry held = Posted(13, 400, kNow - 30 * 24 * 60 * 60);
        held.received = true;
        held.heldGold = 1;
        CHECK(!RelistEntryStale(held, kNow));
    }

    if (failures == 0)
        std::printf("test_guild_order_relist_policy ok\n");
    return failures ? 1 : 0;
}
