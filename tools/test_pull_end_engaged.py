"""A pull ends as soon as the pulled mob is on the puller in melee.

Corner pulls: the tank walks to a mob it cannot see, the mob meets it on the
way, and the pull action never completes. While the pull keeps its target,
PullMultiplier mutes every action below ACTION_INTERRUPT, so the tank stood
with its back to the mob (no swing, no ability, no turn) until the 15 s pull
cap (Uldaman hire run, 2026-10-14: 8 s facing away, trace-score facing FAIL).
Pins the "pull end" clause that ends such a pull, and its order: after the
pullback return (a landed pullback still walks back) and the dead-target and
time-cap exits.

Run: python3 -m unittest tools/test_pull_end_engaged.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/PullTriggers.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PullEndEngagedTest(unittest.TestCase):
    def setUp(self):
        self.fn = body(TRIGGERS, "bool PullEndTrigger::IsActive()", "\n}\n")

    def test_engaged_in_melee_ends_pull(self):
        self.assertIn("!strategy->HasPullActionCompleted() && target->GetVictim() == bot &&", self.fn)
        self.assertIn("bot->CanReachWithMeleeAutoAttack(target)", self.fn)

    def test_clause_order(self):
        pullback = self.fn.index("if (pullback && strategy->HasPullActionCompleted())")
        cap = self.fn.index("secondsSincePullStarted >= strategy->GetMaxPullTime()")
        engaged = self.fn.index("target->GetVictim() == bot")
        final = self.fn.index("return strategy->HasPullActionCompleted();")
        self.assertLess(pullback, engaged)
        self.assertLess(cap, engaged)
        self.assertLess(engaged, final)


if __name__ == "__main__":
    unittest.main()
