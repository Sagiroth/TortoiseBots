"""Guild order rows report delivered as have and bags+mail as carry.

A working row used to report delivered + bags + mail as <have>, so a 2/2 row
could have nothing deliverable (delivery deposits from the bags only). The
row now keeps <have> for what reached the guild (bank deposit or the mail
hand-over) and appends <carry> after every existing field, so old addons
keep parsing. Progress lines fire on either count.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
ORDER = (ROOT / "commands/Order.cpp").read_text()
ADDON = (ROOT / "docs/play/commands/addon.md").read_text()


def tick_body():
    start = STORE.index("TickOut Tick(uint32_t botGuidLow, TickIn const& in)")
    end = STORE.index("private:\n    struct Stored", start)
    return STORE[start:end]


class GuildOrderStatusHonestyTest(unittest.TestCase):
    def test_have_counts_delivered_only(self):
        body = tick_body()
        idx = body.index("out.reportedHave = in.scanned ?")
        window = body[idx:body.index("out.reportedCarry", idx)]
        self.assertIn("ReportedHave(record.delivered, record.reportedHave)", window)
        for token in ("ownedNow", "in.bagCount", "in.mailboxCount"):
            self.assertNotIn(token, window)

    def test_carry_counts_bags_and_mail(self):
        self.assertIn(
            "ReportedCarry(in.scanned, in.botOnline, in.bagCount, in.mailboxCount,",
            tick_body())

    def test_have_keeps_its_never_drop_floor(self):
        self.assertIn("record.reportedHave", tick_body())

    def test_line_appends_carry_last(self):
        formatter = STORE[STORE.index("std::string FormatOrderLine("):]
        formatter = formatter[:formatter.index("\n}\n")]
        self.assertIn('NeedItemsToken(order.needItems) + "|" +', formatter)
        self.assertTrue(re.search(r'std::to_string\(carry\);\s*$', formatter))
        self.assertNotIn('std::to_string(carry) + "|"', formatter)

    def test_live_line_sends_the_carry(self):
        self.assertIn("SendOrderLine(out.record, out.reportedHave, out.reportedCarry)", STORE)

    def test_list_reply_sends_delivered_and_carry(self):
        listing = ORDER[ORDER.index("bool HandleOrderList("):ORDER.index("} // namespace")]
        self.assertIn("ReportedHave(order.delivered, order.reportedHave)", listing)
        self.assertIn("GuildOrders::Carry(bot, order)", listing)
        self.assertNotIn("GetItemCount(order.itemId)", listing)

    def test_carry_change_sends_a_progress_line(self):
        body = tick_body()
        self.assertIn("bool carryChanged = out.reportedCarry != record.reportedCarry;", body)
        self.assertIn("(haveChanged || carryChanged) &&", body)
        self.assertIn("record.reportedCarry = out.reportedCarry;", body)

    def test_offline_list_reply_reports_the_stored_carry(self):
        carry = STORE[STORE.index("uint32_t Carry(Player* bot"):]
        carry = carry[:carry.index("\n}\n")]
        self.assertIn("ListedCarry(botOnline, liveCarry, order.reportedCarry)", carry)
        self.assertIn("bool botOnline = bot && bot->IsInWorld();", carry)
        self.assertEqual(carry.count("GetItemCount(order.itemId)"), 1)
        self.assertIn("botOnline ? bot->GetItemCount(order.itemId)", carry)

    def test_carry_is_persisted_with_the_row(self):
        self.assertIn("`updated_at`, `carried`", STORE)
        self.assertIn("record.reportedCarry = fields[41].GetUInt32();", STORE)
        self.assertIn("`reported`, `carried`, `farm_rest`", STORE)
        self.assertIn("`reported` = '%u', `carried` = '%u'", STORE)
        self.assertIn("record.reportedHave, record.reportedCarry,", STORE)
        self.assertIn("record.reportedHave,\n                record.reportedCarry, record.farmRest", STORE)

    def test_the_stored_carry_is_its_own_column(self):
        migration = ROOT / "data/sql/character/20261102150000_char.sql"
        self.assertTrue(migration.is_file())
        sql = migration.read_text()
        self.assertIn("ADD COLUMN `carried` int unsigned NOT NULL DEFAULT 0 AFTER `reported`", sql)
        self.assertIn("COLUMN_NAME = 'carried'", sql)

    def test_addon_documents_carry(self):
        self.assertIn("|<step>\\|<need>\\|<carry>`", ADDON)
        self.assertIn("not delivered yet", ADDON)


if __name__ == "__main__":
    unittest.main()
