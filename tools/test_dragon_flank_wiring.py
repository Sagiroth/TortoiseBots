"""Wiring pins for the dragon flank step (DungeonActions.cpp).

The rules live in ai/playerbot/DragonFlankPolicy.h (pure, tested by
tools/test_dragon_flank_policy.cpp). These pins prove the live action reads
them:

 - the flank step builds its candidates with DragonFlankPoints and picks with
   PickDragonFlankPoint;
 - it never extends the search past the two side points: no 90-degree walk on
   to a third or fourth heading, and no pi/2 added by hand in the action.

Run: python3 -m unittest tools/test_dragon_flank_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
DUNGEON_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/DragonFlankPolicy.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DragonFlankWiring(unittest.TestCase):
    def test_the_flank_step_reads_the_policy(self):
        self.assertIn('#include "playerbot/DragonFlankPolicy.h"', DUNGEON_ACTIONS)
        fn = body(DUNGEON_ACTIONS, "bool DragonFlankAction::Execute(Event& event)")
        self.assertIn("DragonFlankPoint points[kDragonFlankPointCount];", fn)
        self.assertIn("DragonFlankPoints(target->GetOrientation(), bossPos.GetAngleTo(botPos), points);", fn)
        self.assertIn("PickDragonFlankPoint(points,", fn)
        self.assertIn("return chosen >= 0;", fn)

    def test_the_flank_step_never_walks_on_to_the_tail_or_front(self):
        fn = body(DUNGEON_ACTIONS, "bool DragonFlankAction::Execute(Event& event)")
        # The two side points are the whole search: a side that fails line of
        # sight or the path check is not replaced by a point 90 degrees
        # further round (the tail, or her front from the other side).
        self.assertNotIn("M_PI_F / 2.0f", fn)
        self.assertNotIn("side +=", fn)
        self.assertNotIn("i < 4", fn)
        self.assertNotIn("i < 3", fn)
        # The candidate count and the side angle are the policy's.
        self.assertIn("constexpr std::size_t kDragonFlankPointCount = 2;", POLICY)
        self.assertIn("constexpr float kDragonFlankSideAngle = 1.5707963268f;", POLICY)


if __name__ == "__main__":
    unittest.main()
