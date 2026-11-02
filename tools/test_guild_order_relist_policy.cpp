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

// The table: one row per seq, writes that can be made to fail.
struct FakeLedger
{
    std::vector<RelistLedgerEntry> rows;
    bool failWrites = false;
    int writes = 0;

    bool Save(RelistLedgerEntry const& row)
    {
        if (failWrites)
            return false;
        ++writes;
        for (RelistLedgerEntry& existing : rows)
        {
            if (existing.seq == row.seq)
            {
                existing = row;
                return true;
            }
        }
        rows.push_back(row);
        return true;
    }

    bool Delete(RelistLedgerEntry const& row)
    {
        if (failWrites)
            return false;
        ++writes;
        for (auto it = rows.begin(); it != rows.end(); ++it)
        {
            if (it->seq == row.seq)
            {
                rows.erase(it);
                break;
            }
        }
        return true;
    }
};

// The guild bank: takes at most the given limits, and can break the table
// right after the money moved (the crash-after-deposit case).
struct FakeBank
{
    uint32_t goldLimit = 0xFFFFFFFF;
    uint32_t itemLimit = 0xFFFFFFFF;
    FakeLedger* breakLedgerAfterDeposit = nullptr;
    int deposits = 0;
    uint32_t lastGold = 0;
    uint32_t lastItems = 0;

    RelistMoved Deposit(uint32_t gold, uint32_t items)
    {
        ++deposits;
        lastGold = gold;
        lastItems = items;
        if (breakLedgerAfterDeposit)
            breakLedgerAfterDeposit->failWrites = true;
        RelistMoved moved;
        moved.gold = gold < goldLimit ? gold : goldLimit;
        moved.items = items < itemLimit ? items : itemLimit;
        return moved;
    }
};

RelistTripBot AtBank(uint32_t cash = 0, uint32_t bagCount = 0, uint32_t guildId = kGuild)
{
    RelistTripBot bot;
    bot.guildId = guildId;
    bot.atBank = true;
    bot.cash = cash;
    bot.bagCount = bagCount;
    return bot;
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
        CHECK(rows[0].state == RelistState::Received && rows[0].heldGold == 5100);
        CHECK(rows[0].guildId == kGuild && rows[1].state == RelistState::Posted);
        CHECK(RelistMatchCount(rows, SoldMail(kItem, 5800, 5700, 6000, kDeposit), kNow, index) == 1);
        CHECK(index == 1);
        ReadMail(rows[index], SoldMail(kItem, 5800, 5700, 6000, kDeposit));
        CHECK(rows[1].state == RelistState::Received && rows[1].guildId == kOtherGuild);

        std::vector<RelistLedgerEntry> twins{Posted(), Posted(13, 400, kNow, kOtherGuild)};
        CHECK(RelistMatchCount(twins, SoldMail(), kNow, index) == 2);
        // None of the twins is read: an ambiguous mail is left to normal
        // mail handling and no guild is credited.
        CHECK(twins[0].state == RelistState::Posted && twins[1].state == RelistState::Posted);
        twins[0].state = RelistState::Received;
        CHECK(RelistMatchCount(twins, SoldMail(), kNow, index) == 1);
        CHECK(index == 1);
        CHECK(RelistMatchCount(twins, SoldMail(kItem + 1), kNow, index) == 0);
    }

    // Look-alike rows of one guild settle as the oldest (that guild is
    // credited either way); the next mail settles the other row.
    {
        std::vector<RelistLedgerEntry> rows{Posted(), Posted(13, 400, kNow, kGuild)};
        rows[1].seq = 2;
        size_t index = 99;
        CHECK(RelistMatchCount(rows, SoldMail(), kNow, index) == 2);
        CHECK(RelistSettleIndex(rows, SoldMail(), kNow, index));
        CHECK(index == 0);
        ReadMail(rows[index], SoldMail());
        CHECK(rows[0].state == RelistState::Received && rows[0].heldGold == 5100);
        CHECK(RelistSettleIndex(rows, SoldMail(), kNow, index));
        CHECK(index == 1);
        CHECK(!RelistSettleIndex(rows, SoldMail(kItem + 1), kNow, index));
    }
    {
        // Oldest means oldest row, not first in the vector.
        std::vector<RelistLedgerEntry> rows{Posted(), Posted(13, 400, kNow, kGuild)};
        rows[0].seq = 5;
        rows[1].seq = 3;
        size_t index = 99;
        CHECK(RelistSettleIndex(rows, SoldMail(), kNow, index));
        CHECK(index == 1);
    }
    {
        // Matches spanning guilds stay ambiguous: neither row is consumed.
        std::vector<RelistLedgerEntry> rows{Posted(), Posted(13, 400, kNow, kOtherGuild)};
        rows[1].seq = 2;
        size_t index = 99;
        CHECK(RelistMatchCount(rows, SoldMail(), kNow, index) == 2);
        CHECK(!RelistSettleIndex(rows, SoldMail(), kNow, index));
        CHECK(rows[0].state == RelistState::Posted && rows[1].state == RelistState::Posted);
    }

    // A claim names the mail and the value it promises before the take, so a
    // take whose received write fails is still known from the row.
    {
        RelistLedgerEntry sold = Posted();
        RelistNoteMailClaimed(sold, 77, SoldMail(), RelistMailKind::Sold);
        CHECK(sold.state == RelistState::Claimed);
        CHECK(sold.claimMailId == 77);
        CHECK(sold.heldGold == 5100 && sold.heldCount == 0);
        CHECK(!RelistNeedsDeposit(sold)); // not in hand yet: no bank trip
        CHECK(!RelistDepositableNow(sold, 5100, 0));
        size_t index = 99;
        CHECK(RelistClaimedIndex({sold}, 77, index) && index == 0);
        CHECK(!RelistClaimedIndex({sold}, 78, index));
        CHECK(RelistClaimPending({sold}));
        // The claimed row is not matched by its terms again: a look-alike
        // mail of the same guild cannot claim it a second time.
        CHECK(RelistMatchCount({sold}, SoldMail(), kNow, index) == 0);
        CHECK(!RelistSettleIndex({sold}, SoldMail(), kNow, index));

        RelistLedgerEntry expired = Posted(13);
        RelistNoteMailClaimed(expired, 78, ExpiredMail(kItem, 13), RelistMailKind::Expired);
        CHECK(expired.heldCount == 13 && expired.heldGold == 0);
    }

    // A claim write that fails leaves the row posted: the mail is not taken
    // and the next read tries the same mail again.
    {
        RelistLedgerEntry entry = Posted();
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        ledger.failWrites = true;
        RelistLedgerEntry claim = entry;
        RelistNoteMailClaimed(claim, 77, SoldMail(), RelistMailKind::Sold);
        CHECK(!ledger.Save(claim));
        CHECK(ledger.rows[0].state == RelistState::Posted && ledger.rows[0].claimMailId == 0);
        CHECK(!RelistClaimPending(ledger.rows));
        size_t index = 99;
        CHECK(RelistMatchCount(ledger.rows, SoldMail(), kNow, index) == 1);
        CHECK(index == 0);
    }

    // The take lands but the received write fails: the row stays claimed with
    // the promised value. The mail is gone, so the next read (or a reload)
    // settles the row from the claim and the bank trip pays it.
    {
        RelistLedgerEntry entry = Posted();
        RelistNoteMailClaimed(entry, 77, SoldMail(), RelistMailKind::Sold);
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        ledger.failWrites = true;
        CHECK(!ledger.Save(entry)); // the received write failed
        CHECK(ledger.rows[0].state == RelistState::Claimed);
        ledger.failWrites = false;
        RelistLedgerEntry loaded = ledger.rows[0];
        RelistNoteClaimSettled(loaded);
        CHECK(loaded.state == RelistState::Received && loaded.claimMailId == 0);
        CHECK(loaded.heldGold == 5100 && RelistNeedsDeposit(loaded));
        FakeLedger after;
        after.rows.push_back(loaded);
        FakeBank bank;
        CHECK(RelistBankTrip(loaded, AtBank(5100), after, bank) == RelistTripOutcome::Deposited);
        CHECK(bank.lastGold == 5100 && after.rows.empty());
    }

    // A claimed row whose mail is still there is retried, not doubled: the
    // mail never matches a second row, and the retry keeps one value.
    {
        RelistLedgerEntry claimed = Posted();
        RelistNoteMailClaimed(claimed, 77, SoldMail(), RelistMailKind::Sold);
        std::vector<RelistLedgerEntry> rows{claimed, Posted()};
        rows[1].seq = 2;
        size_t index = 99;
        CHECK(RelistMatchCount(rows, SoldMail(), kNow, index) == 1);
        CHECK(index == 1); // only the posted row matches; the claim waits by id
        CHECK(RelistClaimedIndex(rows, 77, index) && index == 0);
        RelistNoteMailReceived(claimed, RelistMailKind::Sold, 5100, 0);
        CHECK(claimed.state == RelistState::Received && claimed.heldGold == 5100);
    }

    // A banker visit only earns its trip when the bot can hand value over
    // now: sale gold in the purse, or the returned stack in the bags.
    {
        RelistLedgerEntry sold = Posted();
        ReadMail(sold, SoldMail(kItem, 5100));
        CHECK(RelistDepositableNow(sold, 5100, 0));
        CHECK(!RelistDepositableNow(sold, 0, 13));
        RelistLedgerEntry expired = Posted(13);
        ReadMail(expired, ExpiredMail(kItem, 13), kNow + kDurationSec);
        CHECK(RelistDepositableNow(expired, 0, 13));
        CHECK(!RelistDepositableNow(expired, 5000, 0));
        CHECK(!RelistDepositableNow(Posted(), 5000, 13)); // unread: nothing in hand
    }

    // Sold -> the gold is in the bot's hands, the row waits for the bank, and
    // a funded bank trip drops it.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        CHECK(entry.state == RelistState::Received);
        CHECK(entry.heldGold == 5100);
        CHECK(entry.heldCount == 0);
        CHECK(RelistNeedsDeposit(entry));
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        CHECK(RelistBankTrip(entry, AtBank(5100), ledger, bank) == RelistTripOutcome::Deposited);
        CHECK(bank.deposits == 1 && bank.lastGold == 5100 && bank.lastItems == 0);
        CHECK(ledger.rows.empty());
    }

    // Expired -> the posted stack is back in the bags and the bank trip
    // drops the row.
    {
        RelistLedgerEntry entry = Posted(13);
        ReadMail(entry, ExpiredMail(kItem, 13), kNow + kDurationSec);
        CHECK(entry.state == RelistState::Received);
        CHECK(entry.heldCount == 13);
        CHECK(entry.heldGold == 0);
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        CHECK(RelistBankTrip(entry, AtBank(0, 13), ledger, bank) == RelistTripOutcome::Deposited);
        CHECK(bank.lastGold == 0 && bank.lastItems == 13);
        CHECK(ledger.rows.empty());
    }

    // Ledger write fails -> no deposit at all: the planned value is saved off
    // the row before the bank is asked, and the row keeps its debt.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        ledger.failWrites = true;
        FakeBank bank;
        CHECK(RelistBankTrip(entry, AtBank(5100), ledger, bank) == RelistTripOutcome::Refused);
        CHECK(bank.deposits == 0);
        CHECK(ledger.rows.size() == 1 && ledger.rows[0].heldGold == 5100);
    }

    // Deposit refused -> the row is restored with the whole debt. The planned
    // value settled the row (it was removed before the bank), so the restore
    // writes it back in full.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        bank.goldLimit = 0;
        CHECK(RelistBankTrip(entry, AtBank(5100), ledger, bank) == RelistTripOutcome::Returned);
        CHECK(bank.deposits == 1);
        CHECK(ledger.rows.size() == 1 && ledger.rows[0].state == RelistState::Received);
        CHECK(ledger.rows[0].heldGold == 5100 && ledger.rows[0].seq == entry.seq);
    }

    // Partial pass -> the refused rest goes back on the row, nothing more.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        bank.goldLimit = 2500;
        CHECK(RelistBankTrip(entry, AtBank(5100), ledger, bank) == RelistTripOutcome::Returned);
        CHECK(ledger.rows.size() == 1 && ledger.rows[0].heldGold == 2600);
    }

    // Deposit refused and the restore fails too -> the value is lost, and the
    // row is not left to be paid a second time.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        bank.goldLimit = 0;
        bank.breakLedgerAfterDeposit = &ledger;
        CHECK(RelistBankTrip(entry, AtBank(5100), ledger, bank) == RelistTripOutcome::Lost);
        CHECK(ledger.rows.empty());
    }

    // Bag short -> only the items the bags hold are planned; the rest of the
    // row stays owed and is written back with the planned part taken off.
    {
        RelistLedgerEntry entry = Posted(13);
        ReadMail(entry, ExpiredMail(kItem, 13), kNow + kDurationSec);
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        CHECK(RelistBankTrip(entry, AtBank(0, 4), ledger, bank) == RelistTripOutcome::Deposited);
        CHECK(bank.lastItems == 4);
        CHECK(ledger.rows.size() == 1 && ledger.rows[0].heldCount == 9);
    }

    // Partial item pass -> the refused stack remainder goes back on the row.
    {
        RelistLedgerEntry entry = Posted(13);
        ReadMail(entry, ExpiredMail(kItem, 13), kNow + kDurationSec);
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        bank.itemLimit = 5;
        CHECK(RelistBankTrip(entry, AtBank(0, 13), ledger, bank) == RelistTripOutcome::Returned);
        CHECK(ledger.rows.size() == 1 && ledger.rows[0].heldCount == 8);
    }

    // Bot left the row's guild -> the row is dropped without a bank trip, even
    // with no banker in reach. A drop whose write fails keeps the row.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        RelistTripBot away = AtBank(5100, 0, kOtherGuild);
        away.atBank = false;
        CHECK(RelistBankTrip(entry, away, ledger, bank) == RelistTripOutcome::Dropped);
        CHECK(bank.deposits == 0 && ledger.rows.empty());

        FakeLedger broken;
        broken.rows.push_back(entry);
        broken.failWrites = true;
        CHECK(RelistBankTrip(entry, AtBank(5100, 0, 0), broken, bank) == RelistTripOutcome::Refused);
        CHECK(broken.rows.size() == 1);
        CHECK(bank.deposits == 0);
    }

    // No banker in reach, or nothing carried: the row waits, nothing is written.
    {
        RelistLedgerEntry entry = Posted();
        ReadMail(entry, SoldMail(kItem, 5100));
        FakeLedger ledger;
        ledger.rows.push_back(entry);
        FakeBank bank;
        RelistTripBot away = AtBank(5100);
        away.atBank = false;
        CHECK(RelistBankTrip(entry, away, ledger, bank) == RelistTripOutcome::Idle);
        CHECK(RelistBankTrip(entry, AtBank(0), ledger, bank) == RelistTripOutcome::Idle);
        CHECK(ledger.writes == 0 && bank.deposits == 0);
        CHECK(ledger.rows.size() == 1 && RelistNeedsDeposit(ledger.rows[0]));
    }

    // Three days with nothing back drops the row; a row already collected
    // waits for the bank however long that takes.
    CHECK(!RelistEntryStale(Posted(), kNow));
    CHECK(!RelistEntryStale(Posted(13, 400, kNow - kRelistStaleSec + 1), kNow));
    CHECK(RelistEntryStale(Posted(13, 400, kNow - kRelistStaleSec), kNow));
    CHECK(RelistEntryStale(Posted(13, 400, kNow - 10 * kRelistStaleSec), kNow));
    {
        RelistLedgerEntry held = Posted(13, 400, kNow - 30 * 24 * 60 * 60);
        held.state = RelistState::Received;
        held.heldGold = 1;
        CHECK(!RelistEntryStale(held, kNow));
        RelistLedgerEntry claimed = Posted(13, 400, kNow - 30 * 24 * 60 * 60);
        RelistNoteMailClaimed(claimed, 77, SoldMail(), RelistMailKind::Sold);
        CHECK(!RelistEntryStale(claimed, kNow));
    }

    if (failures == 0)
        std::printf("test_guild_order_relist_policy ok\n");
    return failures ? 1 : 0;
}
