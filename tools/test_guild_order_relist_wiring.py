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
POLICY = (ROOT / "runtime/GuildOrderRelistPolicy.h").read_text()
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
        self.assertIn("RelistSettleIndex(entries, view, uint64_t(curTime), matched)", take)
        self.assertIn("RelistMailMatch(entry, view, uint64_t(curTime))", take)
        self.assertIn("relist mail ambiguous", take)
        self.assertIn("match across guilds", take)
        self.assertIn("RelistNoteMailReceived(entry, kind, gold, items)", take)
        self.assertIn("RelistNoteMailClaimed(claim, mail->messageID, view, kind)", take)
        self.assertIn("NoteRelistClaimed(bot->GetGUIDLow(), entry.seq,", take)
        self.assertIn("NoteRelistReceived(bot->GetGUIDLow(), entry.seq, gold, items)", take)
        helper = body(MAIL, "bool TakeRelistValue(", "void DeleteEmptyRelistMail(")
        self.assertIn("HandleMailTakeMoney(", helper)
        self.assertIn("HandleMailTakeItem(", helper)
        self.assertIn("TakeRelistValue(ai, bot, mailbox, mail, entry.itemId, kind, gold, items)", take)
        # The claim write lands before the take, and the received write after
        # it: a failed claim leaves the mail untouched, a failed received
        # write leaves the claim to settle the row.
        loop = take[take.index("RelistLedgerEntry& entry = entries[matched];"):]
        claim = loop.index("NoteRelistClaimed(")
        taken = loop.index("TakeRelistValue(ai, bot, mailbox, mail, entry.itemId, kind, gold, items)")
        received = loop.index("NoteRelistReceived(", taken)
        self.assertLess(claim, taken)
        self.assertLess(taken, received)
        # A claimed row is retried by the mail it names, and a mail a claim
        # already owns never matches a second row.
        self.assertIn("RelistClaimedIndex(entries, mail->messageID, claimed)", take)
        self.assertIn("RelistNoteClaimSettled(entry)", take)
        self.assertIn("owner->GetMail(entry.claimMailId)", take)
        # A claim settles once its mail no longer carries the row's value (the
        # core keeps has_items set after the stack is taken) and is retried
        # only while it still does.
        self.assertIn("RelistClaimMailHoldsValue(entry, view)", take)
        retry = take[take.index("RelistState::Claimed"):]
        self.assertIn("view = MakeRelistMailView(mail, owner)", retry)
        self.assertLess(retry.index("view = MakeRelistMailView(mail, owner)"),
            retry.index("RelistClaimMailHoldsValue(entry, view)"))
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
        self.assertIn("`state`, `claim_mail`, `held_gold`,", STORE)
        migration = (ROOT / "data/sql/character/20261102180000_char.sql").read_text()
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_relist`", migration)
        for column in ("bot_guid", "guild_id", "item_id", "count", "unit_price", "posted_at"):
            self.assertIn("`%s`" % column, migration)
        identity = (ROOT / "data/sql/character/20261102190000_char.sql").read_text()
        for column in ("bid", "buyout", "deposit", "expires_at"):
            self.assertIn("ADD COLUMN `%s`" % column, identity)
        claim = (ROOT / "data/sql/character/20261102200000_char.sql").read_text()
        self.assertIn("ADD COLUMN `state`", claim)
        self.assertIn("ADD COLUMN `claim_mail`", claim)
        self.assertIn("SET `state` = 2 WHERE `received` = 1", claim)
        self.assertIn("DROP COLUMN `received`", claim)
        self.assertIn("`bid`, `buyout`, `deposit`, `posted_at`, `expires_at`", STORE)
        self.assertIn("`count`, `unit_price`, `bid`, `buyout`, `deposit`, `posted_at`, `expires_at`,", STORE)

    def test_stale_rows_drop_with_a_log_line(self):
        expire = body(STORE, "void ExpireStaleRelists(", "\n    void NoteBagsFull(")
        self.assertIn("RelistEntryExpiry(*row, seen->second, nowSec)", expire)
        self.assertIn("RelistAgedBots(nowSec)", expire)
        self.assertIn("ReadRelistPresence(aged)", expire)
        self.assertIn("RelistExpiry::MailExpired", expire)
        self.assertIn("`logout_time`", body(STORE, "ReadRelistPresence(", "\n    void NoteBagsFull("))
        self.assertNotIn("RelistEntryStale", STORE)
        self.assertIn("TB_LOG_BASIC(", expire)
        self.assertIn("DELETE FROM `tortoise_bots_guild_relist`", expire)
        self.assertIn("RelistExpireDue(nowSec)", body(STORE, "void Update(uint32_t diff)", "PlaceSubOrders(now)"))

    def test_bank_visit_pays_the_ledger(self):
        self.assertIn("HasPayableRelist(bot)", BANK)
        self.assertIn("RelistDepositableNow(row, cash,", BANK)
        self.assertIn("RelistDeposits(bot->GetGUIDLow())", BANK)
        self.assertIn("RelistQueue::Instance().Enqueue(bot->GetGUIDLow())", BANK)
        drain = body(DELIVERY, "class RelistDrain", "\n};")
        self.assertIn("RelistDeposits(request.botGuidLow)", drain)
        self.assertIn("RelistBankTrip(entry, view, ledger, bank)", drain)
        self.assertIn("RelistTripBot const view{ guildId, atBank,", drain)
        # The bank adapter is the one place the bank handler and the bag pass run.
        writes = body(DELIVERY, "class RelistBankWrites", "\n};")
        self.assertIn('"DepositMoney:"', writes)
        self.assertIn("DepositPass(m_bot, m_guild, m_banker, m_itemId, 0, items)", writes)
        ledger = body(DELIVERY, "struct RelistLedgerWrites", "\n};")
        self.assertIn("SaveRelistRow(row)", ledger)
        self.assertIn("DeleteRelistRow(row.botGuidLow, row.seq)", ledger)
        # A visit with no banker in reach never reserves value: the bank is
        # looked up before the trip runs.
        self.assertLess(drain.index("FindNearbyGuildBanker("), drain.index("RelistBankTrip("))
        self.assertIn("UpdateRelistDeposits();", STORE)
        # The trip writes the ledger before the bank is asked, and restores
        # what the bank refused after it: the at-most-once order.
        trip = body(POLICY, "RelistTripOutcome RelistBankTrip(", "\n}\n")
        self.assertLess(trip.index("ledger.Save(reserved)"), trip.index("bank.Deposit("))
        self.assertLess(trip.index("ledger.Delete(entry)"), trip.index("bank.Deposit("))
        self.assertLess(trip.index("bank.Deposit("), trip.index("ledger.Save(owed)"))

    def test_store_row_writes_are_checked(self):
        save = body(STORE, "bool SaveRelist(", "\n    bool DeleteRelist(")
        self.assertIn("if (!CharacterDatabase.DirectPExecute(", save)
        self.assertIn("REPLACE INTO `tortoise_bots_guild_relist`", save)
        delete = body(STORE, "bool DeleteRelist(", "\n    // The received rows")
        self.assertIn("if (!CharacterDatabase.DirectPExecute(", delete)
        self.assertIn("DELETE FROM `tortoise_bots_guild_relist`", delete)
        # Every ledger write is checked and synchronous, posting, claiming and
        # receiving included.
        posted = body(STORE, "void NoteRelistPosted(", "\n    // The bot's ledger rows")
        self.assertIn("if (!CharacterDatabase.DirectPExecute(", posted)
        self.assertNotIn("CharacterDatabase.PExecute(", posted)
        state = body(STORE, "bool WriteRelistState(", "\n    // A matched mail is about to be taken")
        self.assertIn("if (!CharacterDatabase.DirectPExecute(", state)
        self.assertIn("UPDATE `tortoise_bots_guild_relist` SET `state`", state)
        self.assertNotIn("CharacterDatabase.PExecute(", state)
        claimed = body(STORE, "bool NoteRelistClaimed(", "\n    // A claimed mail was collected")
        self.assertIn("if (!mailId)", claimed)
        self.assertIn("WriteRelistState(botGuidLow, seq, RelistState::Claimed, mailId, gold, items)",
            claimed)
        received = body(STORE, "bool NoteRelistReceived(", "\n    // Writes a ledger row")
        self.assertIn("WriteRelistState(botGuidLow, seq, RelistState::Received, 0, gold, items)",
            received)
        self.assertNotIn("NoteRelistDeposit", STORE)
        self.assertNotIn("NoteRelistDeposit", DELIVERY)

    def test_relist_mail_wakes_the_mail_read(self):
        self.assertIn("RelistEntries(bot->GetGUIDLow())", MAINT)
        self.assertIn("hasRelistLedger && (*itr)->stationery == MAIL_STATIONERY_AUCTION", MAINT)
        # A claimed row wakes the read even when its mail is already gone:
        # the read is what settles it.
        self.assertIn("RelistClaimPending(relistRows)", MAINT)

    def test_character_deletion_drops_the_ledger(self):
        cleanup = (ROOT / "runtime/CharacterCleanup.cpp").read_text()
        self.assertIn("DELETE FROM `tortoise_bots_guild_relist` WHERE `bot_guid` = '%u'", cleanup)

    def test_docs_describe_the_ledger(self):
        self.assertIn("relist ledger", DOC.lower())
        self.assertIn("tortoise_bots_guild_relist", DOC)
        self.assertIn("three days", DOC)
        self.assertIn("30-day", DOC)
        self.assertIn("at most once", DOC.lower())
        self.assertIn("has since left", DOC.lower())


if __name__ == "__main__":
    unittest.main()
