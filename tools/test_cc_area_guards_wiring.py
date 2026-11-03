"""Wiring pins for the crowd-control area guards (ZG walk 32).

The pure rule lives in ai/playerbot/strategy/values/CcMarkHoldPolicy.h
(CcDotTargetAllowed; behaviour in tools/test_cc_mark_hold_policy.cpp) and the
game-state reads in ai/playerbot/strategy/values/TargetValue.cpp
(CcGuard::IsCcMarkedUnit/IsCrowdControlledUnit). These pins only prove the live
code calls them:

 - the DoT rows ("attacker without aura") skip a CC mark or a crowd-controlled
   unit while AiPlayerbot.DungeonRunAutoCc is on, and keep the held-mark skip
   without it.

Run: python3 -m unittest tools/test_cc_area_guards_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
VALUES = STRATEGY / "values"
TARGET_VALUE = (VALUES / "TargetValue.cpp").read_text()
TARGET_VALUE_H = (VALUES / "TargetValue.h").read_text()
DOT_TARGET = (VALUES / "AttackerWithoutAuraTargetValue.cpp").read_text()
POLICY = (VALUES / "CcMarkHoldPolicy.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DotTargetWiring(unittest.TestCase):
    """No DoT on a CC mark or a crowd-controlled unit (auto crowd control on)."""

    def test_the_dot_rows_call_the_policy(self):
        fn = body(DOT_TARGET, "Unit* AttackerWithoutAuraTargetValue::Calculate()")
        self.assertIn("sPlayerbotAIConfig.dungeonRunAutoCc", fn)
        self.assertIn("ccGuard.IsCcMarkedUnit(unit)", fn)
        self.assertIn("ccGuard.IsCrowdControlledUnit(unit)", fn)
        self.assertIn(
            "CcDotTargetAllowed(sPlayerbotAIConfig.dungeonRunAutoCc, ccGuard.IsHeldCcMark(unit), ccMarkOrControlled)",
            fn)
        # The two reads behind the policy are the guard's own mark and CC tests.
        marked = body(TARGET_VALUE, "bool CcGuard::IsCcMarkedUnit(")
        self.assertIn("IsCcMark(unit)", marked)
        controlled = body(TARGET_VALUE, "bool CcGuard::IsCrowdControlledUnit(")
        self.assertIn("IsCrowdControlled(ai, unit)", controlled)
        self.assertIn("bool CcDotTargetAllowed(bool runAutoCc, bool heldMark, bool ccMarkOrControlled)", POLICY)


if __name__ == "__main__":
    unittest.main()
