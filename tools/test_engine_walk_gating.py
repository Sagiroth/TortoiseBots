#!/usr/bin/env python3
"""Regression test for issue #84 review findings (P1/P2).

Pins structural properties of Engine::DoNextAction that unit tests over
ActionFailureBackoff cannot see (it is pure policy math):

P1: the backoff gate must run BEFORE prerequisite expansion and the
    possibility check, so a backing-off action (including one whose
    isPossible() is false) costs no work and extends no deadline.
P2: tick start must consult the module-owned transition tracker and drain
    the queue on arrival/map-change/jump; the walk must stop mid Stride on
    teleport.

Run: python3 tools/test_engine_walk_gating.py
"""

import re
import sys
import unittest
from pathlib import Path

ENGINE = Path(__file__).resolve().parent.parent / "ai/playerbot/strategy/Engine.cpp"


def walk_body():
    src = ENGINE.read_text()
    start = src.index("bool Engine::DoNextAction(")
    nxt = re.compile(r"^(?:ActionResult|ActionNode\*|bool|void|std::string) Engine::", re.M)
    m = nxt.search(src, start + 1)
    return src[start:m.start() if m else len(src)]


class WalkGatingTest(unittest.TestCase):
    def test_backoff_gate_before_prerequisites(self):
        body = walk_body()
        gate = body.index("IsFailureBackedOff(action, event)")
        prereq = body.index("getPrerequisites()")
        self.assertLess(gate, prereq,
                        "backoff gate must precede prerequisite expansion (P1)")

    def test_backoff_gate_before_possibility_check(self):
        body = walk_body()
        gate = body.index("IsFailureBackedOff(action, event)")
        possible = body.index("action->isPossible()")
        self.assertLess(gate, possible,
                        "backoff gate must precede isPossible() (P1)")

    def test_backed_off_action_drops_without_alternatives(self):
        body = walk_body()
        gate = body.index("IsFailureBackedOff(action, event)")
        window = body[gate:gate + 800]
        self.assertIn("delete actionNode", window)
        self.assertNotIn("getAlternatives()", window)

    def test_tick_start_tracks_transitions_and_drains(self):
        body = walk_body()
        head = body[: body.index("ProcessTriggers")]
        self.assertIn("transitions.Update(", head)
        self.assertIn("TransitionTracker::AWAY", head)
        self.assertIn("DrainQueue()", head)

    def test_mid_walk_transition_stop(self):
        body = walk_body()
        self.assertIn("IsBeingTeleported()", body)
        self.assertIn("transition mid-walk", body)

    def test_explicit_commands_clear_backoff(self):
        src = ENGINE.read_text()
        start = src.index("Engine::ExecuteAction(")
        nxt = re.compile(r"^(?:ActionResult|ActionNode\*|bool|void|std::string) Engine::", re.M)
        m = nxt.search(src, start + 1)
        fn = src[start:m.start() if m else len(src)]
        self.assertIn("ClearActionFailures(action, event)", fn)
        self.assertNotIn("IsFailureBackedOff", fn)

    def test_teleport_ack_bumps_generation(self):
        ai_src = (ENGINE.parent.parent / "PlayerbotAI.cpp").read_text()
        start = ai_src.index("void PlayerbotAI::HandleTeleportAck()")
        fn = ai_src[start:start + 800]
        self.assertIn("++transitionGeneration", fn)

    def test_tick_consumes_generation_before_triggers(self):
        body = walk_body()
        gen = body.index("GetTransitionGeneration()")
        trig = body.index("ProcessTriggers")
        self.assertLess(gen, trig,
                        "transition signal must be consumed before triggers run")

    def test_mid_walk_guard_marks_tracker_away(self):
        body = walk_body()
        guard = body.index("transition mid-walk")
        window = body[max(0, guard - 600):guard]
        self.assertIn("transitions.NoteAway()", window)
if __name__ == "__main__":
    suite = unittest.TestLoader().loadTestsFromTestCase(WalkGatingTest)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    if result.wasSuccessful():
        print(f"PASS tools/test_engine_walk_gating ({result.testsRun} checks)")
    sys.exit(0 if result.wasSuccessful() else 1)
