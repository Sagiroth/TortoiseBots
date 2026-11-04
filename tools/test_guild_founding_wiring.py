"""Guild-founding travel + strategy wiring stays live while the flag is on.

The petition travel row must exist (TravelStrategy.cpp), the "petition" need
must stay need-gated (TravelValues.cpp), and both still honor the
RandomBotFormGuild flag so the 0 default keeps every leg dark.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRAVEL_STRATEGY = (ROOT / "ai/playerbot/strategy/generic/TravelStrategy.cpp").read_text()
TRAVEL_VALUES = (ROOT / "ai/playerbot/strategy/values/TravelValues.cpp").read_text()
RPG_STRATEGY = (ROOT / "ai/playerbot/strategy/generic/RpgStrategy.cpp").read_text()


class GuildFoundingWiringTest(unittest.TestCase):
    def test_travel_strategy_requests_petitioner(self):
        self.assertIn('"val::should travel named::petition"', TRAVEL_STRATEGY)
        self.assertIn('"request named travel target::petition"', TRAVEL_STRATEGY)

    def test_petition_need_honors_flag(self):
        branch = TRAVEL_VALUES.split('else if (name == "petition")')[1].split("else if")[0]
        self.assertIn("randomBotFormGuild", branch)

    def test_rpg_cascade_adds_guild_only_when_on(self):
        self.assertIn('"+rpg guild"', RPG_STRATEGY)
        self.assertIn("randomBotFormGuild", RPG_STRATEGY)


if __name__ == "__main__":
    unittest.main()
