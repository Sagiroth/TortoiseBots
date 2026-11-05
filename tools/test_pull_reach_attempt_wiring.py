"""An airborne pull target is accepted as started only after "reach pull" ran.

Codex review t1105c round 2: airborne geometry alone made PullWalkStarted true and
suppressed the direct retry, even when the queued decision never ran the reach. The
reach action records its attempt on the PullStrategy; the order's start check reads it.

Run: python3 -m unittest tools/test_pull_reach_attempt_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTION = (ROOT / "commands/Action.cpp").read_text()
REACH = (ROOT / "ai/playerbot/strategy/actions/ReachTargetActions.h").read_text()
STRAT_H = (ROOT / "ai/playerbot/strategy/generic/PullStrategy.h").read_text()
STRAT_CPP = (ROOT / "ai/playerbot/strategy/generic/PullStrategy.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/PullStartPolicy.h").read_text()


class PullReachAttemptWiringTest(unittest.TestCase):
    def test_policy_needs_the_attempt_for_an_air_target(self):
        self.assertIn("(facts.airborne && facts.reachAttempted)", POLICY)

    def test_reach_pull_records_its_attempt(self):
        i = REACH.index("class ReachPullAction")
        execute = REACH[i:REACH.index("kMaxFiringPositionDrop", i)]
        self.assertIn("NoteReachPullAttempted();", execute)
        self.assertLess(execute.index("NoteReachPullAttempted();"), execute.index("return ReachTargetAction::Execute(event);"))

    def test_the_flag_lives_and_dies_with_the_command(self):
        self.assertIn("bool HasReachPullAttempted() const", STRAT_H)
        begin = STRAT_CPP[STRAT_CPP.index("void PullStrategy::BeginCommand"):]
        begin = begin[:begin.index("\n}\n")]
        self.assertIn("reachPullAttempted = false;", begin)
        ended = STRAT_CPP[STRAT_CPP.index("void PullStrategy::OnPullEnded"):]
        ended = ended[:ended.index("\n}\n")]
        self.assertIn("reachPullAttempted = false;", ended)

    def test_the_order_reads_the_attempt_for_every_judgement(self):
        self.assertIn("facts.reachAttempted = reachAttempted();", ACTION)
        self.assertIn("strategy->HasReachPullAttempted()", ACTION)


if __name__ == "__main__":
    unittest.main()
