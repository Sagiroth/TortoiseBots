"""Wiring of the config key AiPlayerbot.CombatMonitor."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
HEADER = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()


class CombatMonitorConfigWiringTest(unittest.TestCase):
    def test_key_default_off(self):
        self.assertIn('combatMonitor = config.GetBoolDefault("AiPlayerbot.CombatMonitor", false);', CONFIG)
        self.assertIn("bool combatMonitor", HEADER)
        self.assertIn("AiPlayerbot.CombatMonitor = 0", TEMPLATE)
        self.assertIn("AiPlayerbot.CombatMonitor", SETTINGS)


if __name__ == "__main__":
    unittest.main()
