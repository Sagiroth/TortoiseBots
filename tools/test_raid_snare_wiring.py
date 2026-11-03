"""Raid snare gate wiring (warrior-hunter-raid fix 2).

In a raid group Piercing Howl fires at ACTION_HIGH on every mob the snare
value returns; raid trash on the tank qualified, so the warrior spent GCDs
snaring mobs that are already controlled. In a raid only a mob attacking a
non-tank is a snare target; fleeing mobs are still finished per the shared
runner rules and 5-man behaviour is unchanged.

The behavioural rule is SnareAllowedInRaid (RaidSnarePolicy.h, pinned by
tools/test_raid_snare_policy.cpp); this pins that the value calls it at
the CHASE decision point (the path that selected tank-chasing mobs).

Run: python3 -m unittest tools/test_raid_snare_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VALUE = ROOT / "ai/playerbot/strategy/values/SnareTargetValue.cpp"


class RaidSnareWiringTest(unittest.TestCase):
    def test_chase_branch_calls_snare_policy(self):
        code = VALUE.read_text()
        chase = code[code.index("case CHASE_MOTION_TYPE"):]
        self.assertIn("SnareAllowedInRaid", chase)
        self.assertIn("inRaidGroup", chase)
        self.assertIn("victimOnTank", chase)
        self.assertIn("GetMainTankGuid", code)


class DungeonChaseVetoTest(unittest.TestCase):
    """Review round 3: outside a raid the chase branch keeps the old veto (no snare on a mob chasing a tank)."""

    def test_chase_branch_keeps_the_dungeon_tank_veto(self):
        src = (ROOT / "ai/playerbot/strategy/values/SnareTargetValue.cpp").read_text()
        self.assertIn("(inRaidGroup || !ai->IsTank(chaseTargetPlayer))", src)


if __name__ == "__main__":
    unittest.main()
