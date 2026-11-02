// Standalone test for runtime/GuildOrderRelistPolicy.h: the relist ledger a
// guild order keeps after it posts its bought surplus on the auction house.
// Fake mail, no core and no database.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_relist_policy.cpp -o /tmp/test_guild_order_relist_policy
//   /tmp/test_guild_order_relist_policy
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
constexpr uint32_t kItem = 3857; // mageweave cloth, a stack that splits
constexpr uint64_t kNow = 2000000000;

RelistLedgerEntry Posted(uint32_t count = 13, uint32_t unitPrice = 400, uint64_t postedAtSec = kNow)
{
    RelistLedgerEntry entry;
    entry.seq = 1;
    entry.botGuidLow = kBot;
    entry.guildId = kGuild;
    entry.itemId = kItem;
    entry.count = count;
    entry.unitPrice = unitPrice;
    entry.postedAtSec = postedAtSec;
    return entry;
}

// The core writes the item, the random property and the notification state
// into the subject; the driver below only fills the fields the rules read.
RelistMail SoldMail(uint32_t itemId = kItem, uint32_t money = 5100)
{
    RelistMail mail;
    mail.auction = true;
    mail.subjectItemId = itemId;
    mail.subjectState = kRelistMailSuccessful;
    mail.money = money;
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
void ReadMail(RelistLedgerEntry& entry, RelistMail const& mail)
{
    RelistMailKind const kind = RelistMailMatch(entry, mail);
    if (kind == RelistMailKind::Sold)
        RelistNoteMailReceived(entry, kind, mail.money, 0);
    else if (kind == RelistMailKind::Expired)
        RelistNoteMailReceived(entry, kind, 0, mail.count);
}
} // namespace

int main()
{
    // A sale mail for the posted item settles as gold; a sale of another item
    // (the bot's own loot) never touches the ledger.
    CHECK(RelistMailMatch(Posted(), SoldMail()) == RelistMailKind::Sold);
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem + 1)) == RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(13, 400), SoldMail()) == RelistMailKind::Sold);
    CHECK(RelistMailMatch(Posted(), SoldMail(kItem, 0)) == RelistMailKind::None);
    RelistMail notAuction = SoldMail();
    notAuction.auction = false;
    CHECK(RelistMailMatch(Posted(), notAuction) == RelistMailKind::None);

    // An expired listing returns the posted stack; a won-auction mail of the
    // same item is the bot's own purchase, not a return.
    CHECK(RelistMailMatch(Posted(), ExpiredMail()) == RelistMailKind::Expired);
    CHECK(RelistMailMatch(Posted(), WonMail()) == RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(), ExpiredMail(kItem + 1)) == RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(13), ExpiredMail(kItem, 12)) == RelistMailKind::None);
    CHECK(RelistMailMatch(Posted(13), ExpiredMail(kItem, 20)) == RelistMailKind::Expired);

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

    // Expired -> the returned stack is in the bags, capped at the posted
    // amount when a stack merged, and the bank trip drops the row.
    {
        RelistLedgerEntry entry = Posted(13);
        ReadMail(entry, ExpiredMail(kItem, 20));
        CHECK(entry.received);
        CHECK(entry.heldCount == 13);
        CHECK(entry.heldGold == 0);
        CHECK(RelistNoteDeposit(entry, 0, 13));
    }

    // The ledger takes no order input at all, so a row posted by an order that
    // has since finished (or was never persisted) settles the same way.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, ExpiredMail());
        CHECK(entry.received);
        CHECK(RelistNoteDeposit(entry, 0, 13));
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
