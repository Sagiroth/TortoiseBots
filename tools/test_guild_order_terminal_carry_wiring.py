"""A finished guild order row reports its deposit and no carry.

Live bug: the delivery drain moves a row to done, mailed or cant outside
Tick's scan cadence, and a finished row never scans again. The row kept the
have count of the last scan (often 0) and the carry of the last
pre-delivery scan (the whole stack), so the final line read done with
have=0 and carry=<stack> and the row write persisted it.

The reporting seam now reads the row's delivered count on every pass and
reports zero carry on the transition to a terminal state, before the last
line is sent and the row is written. This test pins the wiring: Tick calls
the seam, the zero reaches the stored row although no scan runs for it, and
every terminal transition (SetState, cancel stub) zeroes the stored carry.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
POLICY = (ROOT / "runtime/GuildOrderGiveUpPolicy.h").read_text()
ORDER = (ROOT / "commands/Order.cpp").read_text()
ADDON = (ROOT / "docs/play/commands/addon.md").read_text()


def tick_body():
    start = STORE.index("TickOut Tick(uint32_t botGuidLow, TickIn const& in)")
    end = STORE.index("private:\n    struct Stored", start)
    return STORE[start:end]


class GuildOrderTerminalCarryWiringTest(unittest.TestCase):
    def test_tick_reports_through_the_seam(self):
        body = tick_body()
        self.assertIn("out.reportedHave = ReportedHaveThisPass(stocked, in.bankChecked, in.bankCount,",
                      body)
        self.assertIn("out.reportedCarry = ReportedCarryThisPass(terminal, in.scanned,",
                      body)
        # Terminal means finished but not stocked: a stocked row keeps its
        # quota for the next round and is not over.
        self.assertIn("bool terminal = IsFinished(record.state) && !stocked;", body)

    def test_carry_zero_is_decided_before_the_line_and_the_row_write(self):
        body = tick_body()
        zero = body.index("out.reportedCarry = ReportedCarryThisPass(terminal,")
        send = body.index("if (out.sendLine)")
        persist = body.index("out.persist = TakeDirtyLocked(stored, out.persistRecord);", send)
        self.assertLess(zero, send)
        self.assertLess(send, persist)

    def test_terminal_zero_reaches_the_stored_row(self):
        body = tick_body()
        scan = body.index("if ((in.scanned || terminal) && carryChanged)")
        send = body.index("if (out.sendLine)")
        stored = body[scan:send]
        self.assertIn("record.reportedCarry = out.reportedCarry;", stored)
        self.assertIn("MarkDirtyLocked(stored);", stored)

    def test_set_state_zeroes_the_stored_carry(self):
        branch = STORE[STORE.index("else if (IsFinished(state))"):]
        branch = branch[:branch.index("MarkDirtyLocked(it->second);")]
        self.assertIn("it->second.record.reportedCarry = 0;", branch)

    def test_cancel_stub_zeroes_the_stored_carry(self):
        cancel = STORE[STORE.index("bool Cancel(uint32_t botGuidLow)"):]
        cancel = cancel[:cancel.index("if (park)")]
        self.assertIn("it->second.record.reportedCarry = 0;", cancel)

    def test_the_seam_zeroes_only_a_terminal_row(self):
        seam = POLICY[POLICY.index("inline uint32_t ReportedCarryThisPass("):]
        seam = seam[:seam.index("\n}\n")]
        self.assertIn("if (terminal)\n        return 0;", seam)
        self.assertIn("return ReportedCarry(scanned, botOnline, bagCount, mailboxCount, reportedCarry);",
                      seam)

    def test_the_offline_list_reads_the_stored_carry(self):
        self.assertIn("GuildOrders::Carry(bot, order)", ORDER)
        carry = STORE[STORE.index("uint32_t Carry(Player* bot"):]
        carry = carry[:carry.index("\n}\n")]
        self.assertIn("ListedCarry(botOnline, liveCarry, order.reportedCarry)", carry)

    def test_addon_documents_the_finished_row(self):
        self.assertIn("`0` on a finished row (nothing is pending for it)", ADDON)


if __name__ == "__main__":
    unittest.main()
