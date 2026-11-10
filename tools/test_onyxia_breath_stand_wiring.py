"""Wiring pins for the Onyxia breath seat yield (AiPlayerbot.BossStandOutranksBreathDodge).

The rule is pure (ai/playerbot/policy/raid/OnyxiaBreathPolicy.h, BreathDodgeYieldsToStand) and tested in
tools/test_onyxia_breath_policy.cpp. These pins only prove the live code calls it: the breath dodge action's
isUseful sits beside MovementAction's and refuses a seated bot with the key on, and the key is read, templated
and documented.

Run: python3 -m unittest tools/test_onyxia_breath_stand_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/policy/raid/OnyxiaBreathPolicy.h").read_text()
ACTION_H = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.h").read_text()
ACTION_CPP = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
TACTICS = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class OnyxiaBreathStandWiring(unittest.TestCase):
    def test_the_rule_is_flag_on_and_seated(self):
        rule = body(POLICY, "inline bool BreathDodgeYieldsToStand(")
        self.assertIn("return flagOn && seated;", rule)

    def test_the_dodge_yields_to_a_seat(self):
        self.assertIn("bool isUseful() override;", ACTION_H)
        useful = body(ACTION_CPP, "bool OnyxiaBreathSafeZoneAction::isUseful()")
        self.assertIn("if (!MovementAction::isUseful())\n        return false;", useful)
        self.assertIn("!BreathDodgeYieldsToStand(sPlayerbotAIConfig.bossStandOutranksBreathDodge, "
                      "FindBossStand(ai, bot).valid)", useful)
        self.assertIn('#include "playerbot/BossTacticsStand.h"', ACTION_CPP)

    def test_the_key_ships_off(self):
        self.assertIn("bool bossStandOutranksBreathDodge = false;", CONFIG_H)
        keys = body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()")
        self.assertIn('bossStandOutranksBreathDodge = config.GetBoolDefault('
                      '"AiPlayerbot.BossStandOutranksBreathDodge", false);', keys)
        self.assertIn("\nAiPlayerbot.BossStandOutranksBreathDodge = 0\n", CONF)
        self.assertIn("Recommended on", CONF)

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.BossStandOutranksBreathDodge`", SETTINGS)
        self.assertIn("recommended on", SETTINGS)
        self.assertIn("BossStandOutranksBreathDodge", TACTICS)
        self.assertIn("BossStandOutranksBreathDodge", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
