"""The sub-order drain places a found candidate before it retries the scan.

PlaceSubOrders scans guild members under a per-tick planner budget. The
decision rules live in runtime/GuildOrderSubOrderPolicy.h; this pins the
store to them: a take found inside the budget is placed even when more
members were never looked at (the live livelock: every request was re-queued
after eight evaluations, so a guild of 28 bots never got a child order), the
scan resumes at the next member on a retry instead of re-scanning the head,
and a fresh request is due whatever the world uptime.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()


def place_slice():
    start = STORE.index("bool PlaceSubOrders(uint32_t nowMs)")
    end = STORE.index("\n}\n", start)
    return STORE[start:end]


class GuildOrderSubOrderWiringTest(unittest.TestCase):
    def test_take_resolved_before_retry_decision(self):
        body = place_slice()
        self.assertIn("SubOrderRetriesAfterScan(", body)
        self.assertLess(body.index("PickSubOrderCandidate("),
                        body.index("SubOrderRetriesAfterScan("))
        # No request is re-queued before the candidates are ranked: that is
        # what discarded a take found inside the budget.
        self.assertLess(body.index("PickSubOrderCandidate("),
                        body.index("RetryQueuedSubOrder("))

    def test_scan_resumes_between_attempts(self):
        body = place_slice()
        self.assertIn("NextSubOrderScanStart(", body)
        self.assertIn("request.nextMember", body)

    def test_retry_keeps_resume_position(self):
        self.assertRegex(STORE, r"RetryQueuedSubOrder\(uint32_t parentBotGuidLow, uint32_t itemId, "
            r"uint32_t count, uint32_t nextTryMs,\s*uint32_t nextMember\)")

    def test_fresh_request_due_at_any_uptime(self):
        self.assertIn("SubOrderRequestDue(", STORE)
        self.assertIn("uint32_t nextMember = 0;", STORE)


if __name__ == "__main__":
    unittest.main()
