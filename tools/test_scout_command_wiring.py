"""Wiring pins for `.bot scout` (plan scout W2, spec section 4).

The pure part (form parse, wire lines, the move facts) is unit-tested in
tools/test_remote_commander_policy.cpp. These pins prove the world-facing glue
uses it, and that extracting `CheckLeadMove` left `CommanderMove` as it was:

- the command is registered and declared, and the GM gate runs before the parse;
- `CommanderMove` sends the lead bot through `CheckLeadMove(lead, origin, point,
  s_runIssuing)`, keeps both snap messages, and still captures the path type
  only when a path was asked for (not "hold here");
- `CheckLeadMove` measures every distance from `origin` and asks the path finder
  from `origin` (both paths, the main one and the raised-floor one in the snap):
  the scout passes any start, so a distance from the mover instead of the origin
  would silently check the wrong hop (spec mutation M13);
- every scout reply is built by the policy line functions, and `walk` uses the
  bots' own mover call.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
TABLE = (ROOT / "commands/BotCommands.cpp").read_text()
HEADER = (ROOT / "commands/BotCommandHandlers.h").read_text()
DOC = (ROOT / "docs/play/commands/commander.md").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class ScoutCommandWiringTest(unittest.TestCase):
    def test_registered_and_declared(self):
        self.assertIn('{"scout", HandleScout, "scout <cfg|path|walk|where> ...", {}, CommanderVisible},', TABLE)
        self.assertIn("bool HandleScout(ChatHandler* handler, char const* args);", HEADER)
        self.assertIn("bool HandleScout(ChatHandler* handler, char const* args)\n{", COMMANDER)

    def test_gm_gate_before_the_parse(self):
        fn = body(COMMANDER, "bool HandleScout(ChatHandler* handler, char const* args)\n{", "\n}\n")
        self.assertIn("GetSession()->GetSecurity() < SEC_GAMEMASTER", fn)
        self.assertIn("!caller->IsGameMaster()", fn)
        self.assertIn('SendActionError(handler, intent, "gm-only",', fn)
        self.assertLess(fn.index("SEC_GAMEMASTER"), fn.index("ParseScoutArgs("))

    def test_replies_use_the_policy_lines(self):
        fn = body(COMMANDER, "bool HandleScout(ChatHandler* handler, char const* args)\n{", "\n}\n")
        for call in ("RemoteCommander::ScoutCfgLine(", "RemoteCommander::ScoutPathLine(",
                     "RemoteCommander::ScoutWalkLine(", "RemoteCommander::ScoutPosLine("):
            self.assertIn(call, fn)
        self.assertIn("MOVE_RUN_MODE | MOVE_PATHFINDING", fn)

    def test_move_goes_through_the_shared_check(self):
        fn = body(COMMANDER, "static bool CommanderMove(", "\n}\n\n// `.bot scout`")
        self.assertIn("LeadMoveCheck const check = CheckLeadMove(lead, origin, request.point, s_runIssuing);", fn)
        self.assertIn("RemoteCommander::MovePoint const& point = check.point;", fn)
        self.assertIn("Move point kept in the water:", fn)
        self.assertIn("Move point snapped to the floor:", fn)
        # The run order's capture is still only after a path query (not at lead).
        self.assertIn("if (s_runOrderCapture.active && !RemoteCommander::IsAtLead(facts.distanceFromLead))", fn)
        self.assertIn("s_runOrderCapture.movePathType = facts.pathType;", fn)
        self.assertIn("RemoteCommander::MoveVerdict const verdict = check.verdict;", fn)

    def test_check_measures_from_origin(self):
        fn = body(COMMANDER, "static LeadMoveCheck CheckLeadMove(", "\n}\n\nstatic bool CommanderMove(")
        # Every distance and both paths come from origin, never from the mover's
        # spot: the scout asks from a start the mover is not standing on.
        self.assertIn("RemoteCommander::LeadMoveFacts(originPoint, point,", fn)
        self.assertEqual(fn.count(".calculate(origin,"), 2)  # main path and the raised-floor snap path
        self.assertIn("RemoteCommander::LeadMoveFacts(", fn)
        self.assertNotIn("GetPositionX()", fn)
        self.assertIn("check.startSwim = terrain->IsSwimmable(origin.x, origin.y, origin.z);", fn)
        self.assertIn("check.endSwim = terrain->IsSwimmable(point.x, point.y, point.z);", fn)
        # "Hold here" skips the path query: the end is the point itself, never
        # the zero-initialized array (a zero answer reads as the map origin).
        self.assertIn("check.actualEnd[0] = point.x;", fn)
        self.assertLess(fn.index("check.actualEnd[0] = point.x;"),
                        fn.index("if (!RemoteCommander::IsAtLead(check.facts.distanceFromLead))"))

    def test_documented(self):
        self.assertIn("## `.bot scout` (GM, test tooling)", DOC)
        for line in ("TBM:SCOUT_CFG", "TBM:SCOUT_PATH", "TBM:SCOUT_WALK", "TBM:SCOUT_POS", "gm-only"):
            self.assertIn(line, DOC)
        self.assertIn("`.bot scout`", (ROOT / "docs/play/commands/README.md").read_text())


if __name__ == "__main__":
    unittest.main()
