"""A stay spot set by an order moves the idle "return" anchor with it.

`.bot cmd move` and `.bot action come/hold/comestay` put a staying bot on a
new spot. They used to set only the "stay" position; the "return" anchor of
an earlier stay order stayed behind, so the return strategy walked the bot to
the old spot and "return to stay position" walked it back, over and over
(seen live on a tank after `cmd move`). The stay shortcut has always set both.
Also pins the stay action: standing still is a success, and it only runs while
there is idle bookkeeping to do (it used to fail on every idle tick).
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/BotCommands.cpp").read_text()
STAY = (ROOT / "ai/playerbot/strategy/actions/StayActions.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class StayAnchorWiringTest(unittest.TestCase):
    def test_helper_sets_stay_and_return(self):
        fn = body(COMMANDS, "static void SetStayAnchor(", "\n}\n")
        self.assertIn('posMap["stay"] = stayPos;', fn)
        self.assertIn('posMap["return"] = returnPos;', fn)
        self.assertIn('posMap["random"].Reset();', fn)

    def test_commander_move_uses_helper(self):
        fn = body(COMMANDS, "static bool CommanderMove(", "\n}\n")
        self.assertIn("SetStayAnchor(leadAi, point.x, point.y, point.z, lead->GetMapId());", fn)
        self.assertNotIn('posMap["stay"]', fn)

    def test_direct_move_walks_straight_and_anchors_the_lead(self):
        fn = body(COMMANDS, "static bool CommanderMoveDirect(ChatHandler* handler, CommanderOrder const& order,\n    RemoteCommander::MovePoint const& point, std::string const& intent)\n{",
                  "\n}\n")
        self.assertIn("SetStayAnchor(ai, point.x, point.y, ground, lead->GetMapId());", fn)
        self.assertIn("MovePoint(0, point.x, point.y, ground, MOVE_RUN_MODE);", fn)
        self.assertNotIn("MOVE_PATHFINDING", fn)
        self.assertIn("CheckDirectStep(", fn)
        self.assertIn("CheckDirectGround(", fn)
        self.assertIn("isInLineOfSight(", fn)
        self.assertIn("IsInCombat()", fn)
        # The followers take the lead bot's line: its spot first (straight or on a complete navmesh path), then the point.
        self.assertIn("ChooseDirectJoin(", fn)
        self.assertIn("for (Vector3 const& next : { leadSpot, target })", fn)
        self.assertIn("init.MovebyPath(walker.route);", fn)
        self.assertNotIn('posMap["stay"]', fn)

    def test_come_hold_comestay_use_helper(self):
        branch = body(COMMANDS, 'else if (intent == "come" || intent == "hold" || intent == "comestay")',
                      'else if (intent == "ready")')
        self.assertIn("SetStayAnchor(ai, requester->GetPositionX()", branch)
        self.assertNotIn('posMap["stay"]', branch)

    def test_stay_action_does_not_fail_when_still(self):
        stay = body(STAY, "bool StayActionBase::Stay(", "\n}\n")
        self.assertIn("if (!sServerFacade.isMoving(bot))\n        return true;", stay)
        useful = body(STAY, "bool StayAction::isUseful()", "\n}\n")
        self.assertIn('!AI_VALUE(time_t, "stay time")', useful)
        self.assertIn("lastMove.lastFollow", useful)


if __name__ == "__main__":
    unittest.main()
