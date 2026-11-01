"""Guild order Tick scans bags and mailbox on the progress cadence only.

The world thread ticks worked clocks every pass, but the bag and mailbox
walks run at most every 5 s per order. Off-cadence passes carry no counts,
so the rise trackers, the delivery gate and the reported-have line must all
read through the scanned flag instead of the raw counts.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()


def tick_body():
    start = STORE.index("TickOut Tick(uint32_t botGuidLow, TickIn const& in)")
    end = STORE.index("private:\n    struct Stored", start)
    return STORE[start:end]


class GuildOrderScanThrottleTest(unittest.TestCase):
    def test_rise_tracking_needs_scan(self):
        self.assertIn("in.botOnline && in.scanned", tick_body())

    def test_delivery_gate_needs_scan(self):
        body = tick_body()
        idx = body.index("record.delivered + in.bagCount >= record.amount")
        self.assertIn("in.scanned", body[max(0, idx - 200):idx])

    def test_reported_have_holds_off_scan(self):
        body = tick_body()
        idx = body.index("out.reportedHave = in.scanned ?")
        self.assertGreater(idx, 0)

    def test_update_throttles_scans(self):
        update = STORE[STORE.index("void Update(uint32_t diff)"):]
        self.assertIn("ShouldScanProgress", update)
        self.assertIn("MarkProgressScanned", update)

    def test_give_up_forces_fresh_scan(self):
        update = STORE[STORE.index("void Update(uint32_t diff)"):]
        self.assertIn("IsOrderTimeout(snapshot.workedTotalSec", update)
        self.assertIn("IsOrderNoProgress(snapshot.workedNoGainSec)", update)


if __name__ == "__main__":
    unittest.main()
