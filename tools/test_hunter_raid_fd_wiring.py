"""Raid Feign Death gate wiring (warrior-hunter-raid fix 3).

In a raid group Feign Death fired on medium threat (60%) even with nothing
attacking the hunter, dropping combat while the raid fought. In a raid it
needs a mob on the hunter or near-max threat; dungeon behaviour is unchanged.

Run: python3 -m unittest tools/test_hunter_raid_fd_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTION = ROOT / "ai/playerbot/strategy/hunter/HunterActions.cpp"
POLICY = ROOT / "runtime/policy/class/HunterFeignDeathPolicy.h"


class HunterRaidFdWiringTest(unittest.TestCase):
    def test_policy_rule_exists(self):
        self.assertIn("HunterFeignDeathRaidAllowed", POLICY.read_text())
    def test_action_gates_raids(self):
        code = ACTION.read_text()
        start = code.index("bool CastFeignDeathAction::isUseful")
        fn = code[start:code.index("\n}\n", start)]
        self.assertIn("HunterFeignDeathRaidAllowed", fn)
        self.assertIn("isRaidGroup", fn)
        self.assertIn('"threat"', fn)

    def test_dungeon_path_unchanged(self):
        # The old rule still decides outside raid groups (called by the
        # raid gate for non-raid groups).
        policy = POLICY.read_text()
        start = policy.index("inline bool HunterFeignDeathRaidAllowed")
        fn = policy[start:policy.index("\n}\n", start)]
        self.assertIn("HunterFeignDeathAllowed(othersPerAttacker)", fn)
if __name__ == "__main__":
    unittest.main()
