"""Guild AH relist ledger wiring: the ledger row on every posted relist, the mail read that collects the sale gold or the returned stack at any order state, and the banker visit that pays it back to the guild.

The rules live in runtime/GuildOrderRelistPolicy.h (test_guild_order_relist_policy.cpp); this pins where they are wired: the buy action's relist, the mail take path, the store's persisted ledger, the bank action and the world-thread relist drain.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUY = (ROOT / "ai/playerbot/strategy/actions/GuildOrderBuyAction.cpp").read_text()
MAIL = (ROOT / "ai/playerbot/strategy/actions/MailAction.cpp").read_text()
BANK = (ROOT / "ai/playerbot/strategy/actions/GuildBankAction.cpp").read_text()
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
DELIVERY = (ROOT / "runtime/GuildOrderDelivery.cpp").read_text()
MAINT = (ROOT / "ai/playerbot/strategy/values/MaintenanceValues.cpp").read_text()
DOC = (ROOT / "docs/developers/guild-supply.md").read_text()


def body(text, start_marker, end_marker):
    start = text.index(start_marker)
    return text[start:text.index(end_marker, start)]


class GuildOrderRelistWiringTest(unittest.TestCase):
    def test_relist_records_the_ledger_row(self):
        relist = body(BUY, "uint32_t RelistSurplus(", "\n} // namespace")
        self.assertIn("NoteRelistPosted(bot->GetGUIDLow(), bot->GetGuildId(), itemId, postCount, unitPrice, bid, buyout,", relist)
        self.assertIn("deposit, kRelistDurationMinutes * 60);", relist)
        sell = relist.index("HandleAuctionSellItem(")
        self.assertGreater(relist.index("NoteRelistPosted("), sell)

    def test_mail_take_settles_the_ledger_at_any_order_state(self):
        take = body(MAIL, "uint32_t ai::TakeRelistMail(", "\nclass DeleteMailProcessor")
        self.assertNotIn("GuildOrders::Find(", take)
        self.assertIn("RelistEntries(bot->GetGUIDLow())", take)
        self.assertIn("RelistMatchCount(entries, view, uint64_t(curTime), matched)", take)
        self.assertIn("RelistMailMatch(*entry, view, uint64_t(curTime))", take)
        self.assertIn("relist mail ambiguous", take)
        self.assertIn("RelistNoteMailReceived(*entry, kind, gold, items)", take)
        self.assertIn("NoteRelistReceived(bot->GetGUIDLow(), entry->seq,", take)
        self.assertIn("HandleMailTakeMoney(", take)
        self.assertIn("HandleMailTakeItem(", take)
        self.assertIn("MAIL_STATIONERY_AUCTION", MAIL)
        self.assertIn("ParseAuctionSubject(mail->subject", MAIL)
        self.assertIn("ParseAuctionSaleBody(sObjectMgr.GetItemText(mail->itemTextId), view)", MAIL)

    def test_order_mail_leg_settles_before_taking_order_stock(self):
        execute = BUY[BUY.index("bool GuildOrderTakeMailAction::Execute("):]
        self.assertIn("ai::TakeRelistMail(ai);", execute)
        self.assertLess(execute.index("TakeRelistMail(ai)"), execute.index("TakeOrderMail(ai, itemId)"))

    def test_mail_take_command_runs_the_settlement(self):
        execute = body(MAIL, "bool MailAction::Execute(", "void MailProcessor::RemoveMail")
        self.assertIn('if (action == "take")', execute)
        self.assertIn("ai::TakeRelistMail(ai);", execute)

    def test_ledger_is_persisted_with_the_order_store(self):
        self.assertIn("tortoise_bots_guild_relist", STORE)
        self.assertIn("FROM `tortoise_bots_guild_relist`", STORE)
        self.assertIn("`received`, `held_gold`, `held_count`", STORE)
        migration = (ROOT / "data/sql/character/20261102180000_char.sql").read_text()
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_relist`", migration)
        for column in ("bot_guid", "guild_id", "item_id", "count", "unit_price", "posted_at"):
            self.assertIn("`%s`" % column, migration)
        identity = (ROOT / "data/sql/character/20261102190000_char.sql").read_text()
        for column in ("bid", "buyout", "deposit", "expires_at"):
            self.assertIn("ADD COLUMN `%s`" % column, identity)
        self.assertIn("`bid`, `buyout`, `deposit`, `posted_at`, `expires_at`", STORE)
        self.assertIn("`count`, `unit_price`, `bid`, `buyout`, `deposit`, `posted_at`, `expires_at`,", STORE)

    def test_stale_rows_drop_with_a_log_line(self):
        expire = body(STORE, "void ExpireStaleRelists(", "\n    void NoteBagsFull(")
        self.assertIn("RelistEntryStale(*row, nowSec)", expire)
        self.assertIn("TB_LOG_BASIC(", expire)
        self.assertIn("DELETE FROM `tortoise_bots_guild_relist`", expire)
        self.assertIn("RelistExpireDue(nowSec)", body(STORE, "void Update(uint32_t diff)", "PlaceSubOrders(now)"))

    def test_bank_visit_pays_the_ledger(self):
        self.assertIn("RelistDepositPending(bot->GetGUIDLow())", BANK)
        self.assertIn("RelistQueue::Instance().Enqueue(bot->GetGUIDLow())", BANK)
        drain = body(DELIVERY, "class RelistDrain", "\n};")
        self.assertIn("RelistDeposits(request.botGuidLow)", drain)
        self.assertIn('"DepositMoney:"', drain)
        self.assertIn("DepositPass(bot, guild, banker, entry.itemId, 0, quota)", drain)
        self.assertIn("NoteRelistDeposit(request.botGuidLow, entry.seq, goldMoved, itemsMoved)", drain)
        # No banker in reach leaves every row for the next trip.
        no_banker = drain.index("if (!banker)")
        self.assertLess(no_banker, drain.index("NoteRelistDeposit("))
        self.assertIn("UpdateRelistDeposits();", STORE)

    def test_relist_mail_wakes_the_mail_read(self):
        self.assertIn("RelistEntries(bot->GetGUIDLow())", MAINT)
        self.assertIn("hasRelistLedger && (*itr)->stationery == MAIL_STATIONERY_AUCTION", MAINT)

    def test_docs_describe_the_ledger(self):
        self.assertIn("relist ledger", DOC.lower())
        self.assertIn("tortoise_bots_guild_relist", DOC)
        self.assertIn("three days", DOC)


if __name__ == "__main__":
    unittest.main()
