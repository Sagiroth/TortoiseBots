"""The sub-order drain decides a roster scan when the walk completes.

PlaceSubOrders scans guild members under a per-tick planner budget. The
decision rules live in runtime/GuildOrderSubOrderPolicy.h; this pins the
store to them: the best free candidate and the busy verdict found in earlier
slices travel with the request, so an attempt that stopped early retries
instead of placing a candidate a later slice outranks, and a free candidate
seen in an early slice is not forgotten when the last slice is useless. The
scan also resumes after the last member it looked at instead of re-scanning
the head (in guid order, so a roster that shifts between retries cannot skip
a member), and a fresh request is due whatever the world uptime.
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
    def test_scan_verdict_waits_for_completion(self):
        body = place_slice()
        self.assertIn("ClassifySubOrderScan(", body)
        self.assertIn("NoteSubOrderCandidate(", body)
        # Candidates are folded as the scan walks; the pick is only made once
        # the walk has covered the roster, so placement cannot land on the
        # first free candidate of a slice.
        self.assertLess(body.index("AdvanceSubOrderScan("),
                        body.index("ClassifySubOrderScan("))
        self.assertLess(body.index("if (!scan.complete)"),
                        body.index("ClassifySubOrderScan("))
        self.assertLess(body.index("ClassifySubOrderScan("), body.index("Give("))
        self.assertNotIn("PickSubOrderCandidate(", body)

    def test_busy_verdict_carried_between_slices(self):
        body = place_slice()
        # The state found in earlier slices is read back on the next attempt
        # and handed to the retry: dropping the busy flag fails a request
        # whose only usable member was busy in an early slice.
        self.assertRegex(body, r"best\.guid\s*=\s*request\.scan\.bestGuid;")
        self.assertRegex(body, r"best\.rank\s*=\s*request\.scan\.bestRank;")
        self.assertRegex(body, r"best\.sawBusyUsable\s*=\s*request\.scan\.sawBusyUsable;")
        self.assertRegex(body, r"QueuedSubOrderScan\{[^}]*best\.guid,\s*best\.rank,\s*"
            r"best\.canMake,\s*best\.sawBusyUsable\s*\}")

    def test_scan_resumes_between_attempts(self):
        body = place_slice()
        self.assertIn("PlanSubOrderScan(", body)
        self.assertIn("AdvanceSubOrderScan(", body)
        self.assertIn("request.scan.lastMember", body)
        self.assertIn("request.scan.scanStart", body)
        self.assertIn("ORDER BY `guid`", body)

    def test_retry_keeps_resume_position(self):
        self.assertRegex(STORE, r"RetryQueuedSubOrder\(uint32_t parentBotGuidLow, uint32_t itemId, "
            r"uint32_t count, uint32_t nextTryMs,\s*QueuedSubOrderScan const& scan\)")

    def test_fresh_request_due_at_any_uptime(self):
        self.assertIn("SubOrderRequestDue(", STORE)
        self.assertIn("uint32_t lastMember = 0;", STORE)


if __name__ == "__main__":
    unittest.main()
