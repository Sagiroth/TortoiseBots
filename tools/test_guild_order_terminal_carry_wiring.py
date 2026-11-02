"""A guild order row keeps its carry unless the hand-off succeeded.

Live bug: the reporting seam returned zero carry for every terminal state, so
a delivery whose bank refused and whose guild had no mail receiver ended cant
with the stack still in the bot's bags while the row (and the offline
`.bot order list`) said 0. The online list read the bags and disagreed.

The seam now clears the carry only on a successful hand-off (done after the
bank deposit, mailed). A cant, stuck or cancelled row keeps the last scanned
carry, and the list reads that stored figure for a terminal row whether the
bot is online or not. This test pins the wiring: Tick calls the seam with the
hand-off flag, the zero reaches the stored row although no scan runs for it,
SetState clears only on a hand-off, a cancel stub keeps its carry, and the
docs tell the same story.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
POLICY = (ROOT / "runtime/GuildOrderGiveUpPolicy.h").read_text()
CONTRACT = (ROOT / "runtime/GuildOrderContract.h").read_text()
ORDER = (ROOT / "commands/Order.cpp").read_text()
ADDON = (ROOT / "docs/play/commands/addon.md").read_text()
GUILD_ORDERS = (ROOT / "docs/play/guild-orders.md").read_text()


def tick_body():
    start = STORE.index("TickOut Tick(uint32_t botGuidLow, TickIn const& in)")
    end = STORE.index("private:\n    struct Stored", start)
    return STORE[start:end]


class GuildOrderTerminalCarryWiringTest(unittest.TestCase):
    def test_tick_reports_through_the_seam(self):
        body = tick_body()
        self.assertIn("out.reportedHave = ReportedHaveThisPass(stocked, in.bankChecked, in.bankCount,",
                      body)
        self.assertIn("out.reportedCarry = ReportedCarryThisPass(record.state, in.scanned,",
                      body)
        self.assertIn("bool handOffSucceeded = HandOffSucceeded(record.state);", body)

    def test_the_hand_off_is_only_done_or_mailed(self):
        helper = CONTRACT[CONTRACT.index("inline bool HandOffSucceeded("):]
        helper = helper[:helper.index("\n}\n")]
        self.assertIn("state == OrderState::Done || state == OrderState::Mailed", helper)

    def test_carry_zero_is_decided_before_the_line_and_the_row_write(self):
        body = tick_body()
        zero = body.index("out.reportedCarry = ReportedCarryThisPass(record.state,")
        send = body.index("if (out.sendLine)")
        persist = body.index("out.persist = TakeDirtyLocked(stored, out.persistRecord);", send)
        self.assertLess(zero, send)
        self.assertLess(send, persist)

    def test_hand_off_zero_reaches_the_stored_row(self):
        body = tick_body()
        scan = body.index("if ((in.scanned || handOffSucceeded) && carryChanged)")
        send = body.index("if (out.sendLine)")
        stored = body[scan:send]
        self.assertIn("record.reportedCarry = out.reportedCarry;", stored)
        self.assertIn("MarkDirtyLocked(stored);", stored)

    def test_set_state_clears_only_on_a_hand_off(self):
        branch = STORE[STORE.index("else if (IsFinished(state))"):]
        branch = branch[:branch.index("MarkDirtyLocked(it->second);")]
        self.assertIn("if (HandOffSucceeded(state))\n                it->second.record.reportedCarry = 0;",
                      branch)
        self.assertEqual(branch.count("reportedCarry = 0;"), 1)

    def test_cancel_stub_keeps_the_carry(self):
        start = STORE.index("    bool Cancel(uint32_t botGuidLow)")
        cancel = STORE[start:]
        cancel = cancel[:cancel.index("if (parked")]
        self.assertNotIn("reportedCarry", cancel)

    def test_the_seam_clears_only_a_successful_hand_off(self):
        seam = POLICY[POLICY.index("inline uint32_t ReportedCarryThisPass("):]
        seam = seam[:seam.index("\n}\n")]
        self.assertIn("if (HandOffSucceeded(state))\n        return 0;", seam)
        self.assertIn("return ReportedCarry(scanned, botOnline, bagCount, mailboxCount, reportedCarry);",
                      seam)

    def test_the_list_reads_the_stored_carry_for_a_terminal_row(self):
        self.assertIn("GuildOrders::Carry(bot, order)", ORDER)
        carry = STORE[STORE.index("uint32_t Carry(Player* bot"):]
        carry = carry[:carry.index("\n}\n")]
        self.assertIn("bool terminal = IsTerminal(order.state);", carry)
        self.assertIn("bool botOnline = bot && bot->IsInWorld();", carry)
        self.assertIn("ListedCarry(terminal, botOnline, liveCarry, order.reportedCarry)", carry)
        listed = POLICY[POLICY.index("inline uint32_t ListedCarry("):]
        listed = listed[:listed.index("\n}\n")]
        self.assertIn("terminal || !botOnline ? reportedCarry : liveCarry", listed)

    def test_addon_documents_the_hand_off(self):
        self.assertIn("`0` once the hand-off succeeded", ADDON)
        self.assertIn("a `cant` row keeps the last scanned figure", GUILD_ORDERS)


if __name__ == "__main__":
    unittest.main()
