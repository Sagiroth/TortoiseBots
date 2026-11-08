"""Carve never walks (hunter dead zone fix).

The Survival Carve row used a range-blind trigger, so its `reach melee`
prerequisite walked hunters onto a tank-held boss and the dead-zone step-out
carried them back, every 10 s. CarveTrigger now gates on melee reach behind
AiPlayerbot.HunterCarveInReachOnly (default 0).

Run: python3 -m unittest tools/test_hunter_carve_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRIGGERS = ROOT / "ai/playerbot/strategy/hunter/HunterTriggers.h"
CONFIG = ROOT / "ai/playerbot/PlayerbotAIConfig.cpp"
CONF = ROOT / "ai/playerbot/aiplayerbot.conf.dist.in"
STRATEGY = ROOT / "ai/playerbot/strategy/hunter/SurvivalHunterStrategy.cpp"
ACTIONS = ROOT / "ai/playerbot/strategy/hunter/HunterActions.h"
DOCS = ROOT / "docs/server/settings/class-behaviour.md"


class HunterCarveWiringTest(unittest.TestCase):
    def test_trigger_gates_range(self):
        code = TRIGGERS.read_text()
        start = code.index("class CarveTrigger")
        body = code[start:code.index("\n    };", start)]
        for needle in ("bool IsActive", "HunterCarveAllowed",
                       "hunterCarveInReachOnly", "SizeFactor::CombatReach"):
            self.assertIn(needle, body)

    def test_config_key(self):
        self.assertIn('"AiPlayerbot.HunterCarveInReachOnly", false', CONFIG.read_text())
        self.assertIn("AiPlayerbot.HunterCarveInReachOnly = 0", CONF.read_text())

    def test_row_unchanged(self):
        self.assertIn('NextAction("carve", ACTION_HIGH - 1)', STRATEGY.read_text())

    def test_manual_action_still_melee(self):
        self.assertIn('MELEE_ACTION(CastCarveAction, "carve")', ACTIONS.read_text())

    def test_docs(self):
        self.assertIn("AiPlayerbot.HunterCarveInReachOnly", DOCS.read_text())


if __name__ == "__main__":
    unittest.main()
