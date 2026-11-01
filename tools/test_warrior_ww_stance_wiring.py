"""Whirlwind/Pummel stance-dance prerequisite wiring (warrior-hunter-raid fix 1).

The engine runs the ACTION_NODE_P prerequisite BEFORE isPossible, so a plain
"berserker stance" prereq stance-swaps even when Whirlwind/Pummel is on
cooldown or unaffordable (rage truncated, cast fails, pin swaps back). The
dedicated prereq actions only dance when the main spell can actually fire,
gated on the rage the warrior holds AFTER the swap (Tactical Mastery
retention via RageAfterStanceSwap).

Run: python3 -m unittest tools/test_warrior_ww_stance_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WARRIOR = ROOT / "ai/playerbot/strategy/warrior"
POLICY = ROOT / "ai/playerbot/WarriorWwStancePolicy.h"


def body(path, start, end="\n    };\n"):
    text = path.read_text()
    i = text.index(start)
    return text[text.index("\n", i):text.index(end, i + len(start))]


class WwStanceWiringTest(unittest.TestCase):
    def test_policy_rule_exists(self):
        policy = POLICY.read_text()
        self.assertIn("ShouldWhirlwindStanceDance", policy)
        self.assertIn("ShouldPummelStanceDance", policy)
        self.assertIn("RageAfterStanceSwap", policy)

    def test_whirlwind_prereq_is_gated_action(self):
        # The whirlwind node no longer points at the always-useful plain stance.
        text = (WARRIOR / "ArmsWarriorStrategy.cpp").read_text()
        self.assertIn('ACTION_NODE_P(whirlwind, "whirlwind", "berserker stance for whirlwind");', text)
        self.assertNotIn('ACTION_NODE_P(whirlwind, "whirlwind", "berserker stance");', text)

    def test_gated_stance_actions_gate_on_post_swap_rage(self):
        actions = (WARRIOR / "WarriorActions.h").read_text()
        for name in ("CastBerserkerStanceForWhirlwindAction", "CastBerserkerStanceForPummelAction"):
            self.assertIn(name, actions)
        ww = body(WARRIOR / "WarriorActions.h", "class CastBerserkerStanceForWhirlwindAction")
        self.assertIn("ShouldWhirlwindStanceDance", ww)
        self.assertIn('"whirlwind"', ww)
        self.assertIn("TacticalMasteryRank", ww)
        self.assertIn('HasAura("berserker stance"', ww)
        pummel = body(WARRIOR / "WarriorActions.h", "class CastBerserkerStanceForPummelAction")
        self.assertIn("ShouldPummelStanceDance", pummel)
        self.assertIn('"pummel"', pummel)
        self.assertIn("TacticalMasteryRank", pummel)
        self.assertIn('HasAura("berserker stance"', pummel)

    def test_gated_stance_creators_registered(self):
        ctx = (WARRIOR / "WarriorAiObjectContext.cpp").read_text()
        self.assertIn('creators["berserker stance for whirlwind"]', ctx)
        self.assertIn('creators["berserker stance for pummel"]', ctx)

    def test_pummel_nodes_use_gated_prereq(self):
        # Base strategy: gated stance prerequisite. Fury: the intercept
        # alternative becomes the gated dance (intercept is itself
        # berserker-locked, so it danced anyway).
        strategy = re.sub(r"/\*.*?\*/", "", (WARRIOR / "WarriorStrategy.cpp").read_text(), flags=re.S)
        self.assertNotRegex(strategy, r'ACTION_NODE_P\(pummel, "pummel", "berserker stance"\)')
        self.assertIn('ACTION_NODE_P(pummel, "pummel", "berserker stance for pummel");', strategy)
        fury = (WARRIOR / "FuryWarriorStrategy.cpp").read_text()
        self.assertIn('ACTION_NODE_A(pummel, "pummel", "berserker stance for pummel");', fury)


if __name__ == "__main__":
    unittest.main()
