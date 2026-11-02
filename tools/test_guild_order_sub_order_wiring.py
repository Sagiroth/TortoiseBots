"""The sub-order drain decides a roster scan when the walk completes.

PlaceSubOrders scans guild members under a per-tick planner budget. The
decision rules live in runtime/GuildOrderSubOrderPolicy.h; this pins the
store to them: the best free candidate and the busy verdict found in earlier
slices travel with the request, so an attempt that stopped early retries
instead of placing a candidate a later slice outranks, and a free candidate
seen in an early slice is not forgotten when the last slice is useless. The
scan also resumes after the last member it looked at instead of re-scanning
the head (in guid order, so a roster that shifts between retries cannot skip
a member), and a fresh request is due whatever the world uptime. The carried
candidate is re-checked just before Give, an unfinished slice retries on the
short timer instead of the busy one, a keep holder does not keep a request
alive, ten busy-only scans fail it, and the drain rotates its starting
request past the one that used the last planner budget.
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
            r"best\.canMake,\s*best\.sawBusyUsable,\s*request\.scan\.busyScans\s*\}")

    def test_scan_resumes_between_attempts(self):
        body = place_slice()
        self.assertIn("PlanSubOrderScan(", body)
        self.assertIn("AdvanceSubOrderScan(", body)
        self.assertIn("request.scan.lastMember", body)
        self.assertIn("request.scan.scanStart", body)
        self.assertIn("ORDER BY `guid`", body)

    def test_retry_keeps_resume_position(self):
        self.assertRegex(STORE, r"RetryQueuedSubOrder\(uint32_t parentBotGuidLow, uint32_t itemId, "
            r"uint32_t count, uint32_t nextTryMs,\s*uint32_t queuedAtMs, "
            r"QueuedSubOrderScan const& scan\)")

    def test_fresh_request_due_at_any_uptime(self):
        # A fresh request carries no retry time, and the drain asks the policy
        # about the stored due time instead of comparing the stamp itself.
        self.assertIn("uint32_t nextTryMs = 0;", STORE)
        self.assertIn("SubOrderRequestDue(queued.nextTryMs, nowMs)", STORE)

    def test_recheck_before_give(self):
        body = place_slice()
        recheck = body[body.index("ClassifySubOrderScan("):body.index("Give(record)")]
        # A candidate carried over several ticks is re-checked with the scan's
        # own member checks and busy rule: Give REPLACEs the bot's order.
        self.assertIn("SubOrderRecheckPasses(recheck)", recheck)
        self.assertIn("sObjectAccessor.FindPlayer(bestGuid)", recheck)
        self.assertIn("bestBot->IsInWorld()", recheck)
        self.assertIn("bestBot->GetGuildId() == parent->guildId", recheck)
        self.assertIn("IsDurableOrderBot(best.guid)", recheck)
        self.assertIn("busyOrder(Find(best.guid))", recheck)
        # A stale candidate restarts the walk on the short slice timer.
        self.assertIn("kSubOrderSliceRetryMs", recheck)
        self.assertIn("QueuedSubOrderScan{}", recheck)

    def test_unfinished_scan_retries_next_tick(self):
        body = place_slice()
        incomplete = body[body.index("if (!scan.complete)"):body.index(
            "SubOrderPick const pick")]
        self.assertIn("kSubOrderSliceRetryMs", incomplete)
        self.assertNotIn("kSubOrderRetryMs", incomplete)

    def test_busy_retry_keeps_the_limit_and_timer(self):
        body = place_slice()
        self.assertIn("ClassifySubOrderScan(best, request.scan.busyScans + 1)", body)
        queued = body[body.index("if (pick == SubOrderPick::Queued)"):body.index(
            "if (pick != SubOrderPick::Take)")]
        self.assertIn("kSubOrderRetryMs", queued)
        self.assertIn("request.scan.busyScans + 1", queued)
        give_failure = body[body.index("if (!Give(record))"):]
        self.assertIn("kSubOrderRetryMs", give_failure)

    def test_keep_holder_does_not_keep_request_alive(self):
        body = place_slice()
        self.assertIn("candidate.keepHolder = candidate.hasOrder &&", body)

    def test_drain_rotates_between_requests(self):
        # Due requests are drained in key order, rotated past the one that
        # spent the last planner budget.
        self.assertIn("SubOrderDrainOffset(", STORE)
        self.assertIn("std::rotate(out.begin(), out.begin() + begin, out.end())", STORE)
        self.assertIn("NoteSubOrderDrained(", STORE)
        self.assertIn("if (plannerEvals > evalsBefore)", STORE)


if __name__ == "__main__":
    unittest.main()
