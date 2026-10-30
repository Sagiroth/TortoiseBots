"""The summon point is checked by behavior/SummonPointPolicy.h in the module call site.

PlayerConvenience::RequestSummon used to call WorldObject::UpdateGroundPositionZ,
which leaves z unchanged when Map::GetHeight finds no floor within the default
10 yd search. A point 5 yd out over a drop therefore kept the summoner's z, the
drop and line-of-sight checks passed, and the bot was teleported into mid-air
(Wailing Caverns, the Serpentis foot, 47 yd). The call site now asks the map
for the floor itself and only uses the point through SummonPointIsSafe
(tools/test_summon_point_policy.cpp covers the rule).

The module cannot be compiled outside the core tree, so this test pins the
source shape instead.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONVENIENCE = (ROOT / "behavior/PlayerConvenience.cpp").read_text()


class SummonPointWiringTest(unittest.TestCase):
    def test_summon_point_uses_the_policy_and_the_map_height(self):
        self.assertIn('#include "SummonPointPolicy.h"', CONVENIENCE)
        self.assertIn("SummonPointIsSafe(", CONVENIENCE)
        self.assertIn("GetHeight(destX, destY", CONVENIENCE)

    def test_ground_position_z_helper_is_gone(self):
        self.assertNotIn("UpdateGroundPositionZ(destX", CONVENIENCE)


if __name__ == "__main__":
    unittest.main()
