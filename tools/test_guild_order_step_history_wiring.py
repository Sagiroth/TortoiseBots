"""Guild order step history wiring: the Tick hook, the Stored member, the command, the capability.

The timeline has one recording point (the end of Tick, memory only), one
accessor (FindStepHistory) and one command (`.bot order <bot> steps`). This
pins the shapes those must keep: the hook sits between the reported-have
write and the need token, it never dirties the row, the Stored struct keeps
the history, TBM:ORDER (FormatOrderLine) stays byte-for-byte, and the command
is read-only (no CheckGiver) and advertises the capability token.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
ORDER = (ROOT / "commands/Order.cpp").read_text()
INFO = (ROOT / "commands/Info.cpp").read_text()


def tick_body():
    start = STORE.index("TickOut Tick(uint32_t botGuidLow, TickIn const& in)")
    end = STORE.index("private:\n    struct Stored", start)
    return STORE[start:end]


def stored_struct():
    start = STORE.index("    struct Stored\n")
    end = STORE.index("    };", start)
    return STORE[start:end]


def format_order_body():
    start = STORE.index("std::string FormatOrderLine(")
    end = STORE.index("\n}\n", start)
    return STORE[start:end]


class GuildOrderStepHistoryWiringTest(unittest.TestCase):
    def test_tick_hook_between_reported_have_and_need_token(self):
        body = tick_body()
        hook = body.index("NoteOrderStep(stored.steps")
        self.assertGreater(hook, body.index("out.reportedHave = in.scanned"))
        self.assertLess(hook, body.index("std::string needToken"))

    def test_tick_hook_is_memory_only(self):
        body = tick_body()
        idx = body.index("NoteOrderStep(stored.steps")
        self.assertNotIn("MarkDirtyLocked", body[idx:idx + 200])

    def test_stored_keeps_the_history(self):
        self.assertIn("OrderStepHistory steps;", stored_struct())

    def test_format_order_line_unchanged(self):
        body = format_order_body()
        self.assertTrue(body.rstrip().endswith("NeedItemsToken(order.needItems);"))
        self.assertNotIn("OrderStep", body)

    def test_command_records_steps_read_only(self):
        self.assertIn('lowerSecond == "steps"', ORDER)
        self.assertIn("HandleOrderSteps(", ORDER)
        start = ORDER.index("HandleOrderSteps(")
        end = ORDER.index("\n}\n", start)
        body = ORDER[start:end]
        self.assertIn("FormatOrderStepLines(", body)
        self.assertIn('"not-guild"', body)
        self.assertNotIn("CheckGiver(", body)

    def test_capability_token_present(self):
        self.assertIn(",guild-orders,guild-order-steps", INFO)


if __name__ == "__main__":
    unittest.main()
