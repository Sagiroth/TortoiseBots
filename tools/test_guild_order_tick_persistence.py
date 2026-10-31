"""Guild order Tick persists every state change it makes.

Live bug: Esveit's hours=1 order timed out in memory (cant/timeout) but the
DB row kept saying working — the Tick timeout branch wrote the state without
marking the row dirty, so FlushLocked skipped it and a restart would have
brought the order back as working. Every Tick branch that assigns
record.state must mark the row dirty on the same path.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()


def tick_body():
    start = STORE.index("TickOut Tick(uint32_t botGuidLow, TickIn const& in)")
    end = STORE.index("private:\n    struct Stored", start)
    return STORE[start:end]


class GuildOrderTickPersistenceTest(unittest.TestCase):
    def test_timeout_marks_dirty(self):
        body = tick_body()
        idx = body.index("record.reason = OrderReason::Timeout;")
        window = body[idx:idx + 1200]
        self.assertIn("MarkDirtyLocked(stored);", window)

    def test_noprogress_marks_dirty(self):
        body = tick_body()
        idx = body.index("record.reason = OrderReason::NoProgress;")
        window = body[idx:idx + 1200]
        self.assertIn("MarkDirtyLocked(stored);", window)

    def test_quota_delivery_marks_dirty(self):
        body = tick_body()
        idx = body.index("record.reason = OrderReason::None;\n            record.supplyStep = SupplyStep::Deliver;")
        window = body[idx:idx + 400]
        self.assertIn("MarkDirtyLocked(stored);", window)

    def test_group_gate_marks_dirty(self):
        body = tick_body()
        for token in ("OrderReason::Grouped;", 'record.reason = OrderReason::None;\n                MarkDirtyLocked(stored);'):
            self.assertIn(token, body)


if __name__ == "__main__":
    unittest.main()
