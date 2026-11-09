"""A released pool bot resets like an alt-bot release (issue: idle after uninvite).

On the freeBot leave branch LeaveGroupAction::Leave must expire the travel
plan, clear follow/stay/guard anchors and stop movement (PlayerbotAI::Reset(true)
plus StopMoving plus the anchor clears), mirroring AltBotService's release, so
the next tick rebuilds the pool leisure kit from a clean slate. The guild-claim
flag and hired-companion handling stay untouched (hires leave via HireLifecycle).
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LEAVE = (ROOT / "ai/playerbot/strategy/actions/LeaveGroupAction.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PoolLeaveResetWiringTest(unittest.TestCase):
    def test_freebot_branch_runs_full_reset_and_stops(self):
        fn = body(LEAVE, "if (freeBot)", "if(!aiMaster)")
        self.assertIn("ai->Reset(true);", fn)
        self.assertIn("bot->StopMoving();", fn)

    def test_freebot_branch_clears_follow_stay_guard_anchors(self):
        fn = body(LEAVE, "if (freeBot)", "if(!aiMaster)")
        self.assertIn('"manual follow target"', fn)
        self.assertIn('posMap["stay"]', fn)
        self.assertIn('posMap["guard"]', fn)
        self.assertIn('"stay time"', fn)

    def test_freebot_branch_keeps_claim_and_hire_out(self):
        fn = body(LEAVE, "if (freeBot)", "if(!aiMaster)")
        self.assertIn("ClearBotMaster", fn)
        self.assertIn("SetForcedRole(0)", fn)
        self.assertNotIn("Claim", fn)
        self.assertNotIn("HireLifecycle", fn)
        self.assertNotIn("Teleport", fn)


if __name__ == "__main__":
    unittest.main()
