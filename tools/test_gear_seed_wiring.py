"""Wiring pins for the random-bot gear seed fixes (issue #1485, F2-F5).

F2: the fresh-seed band in InitEquipment takes its floor from SeedQualityFloor
    (white below level 20, never grey); grey stays in the empty-slot backstop.
F3: the pool start level minimum defaults to 5 (gear seeding starts at 5).
F4: the RandomGearUpgradeEnabled dist comment says it is a one-time seed.
F5: the unused isRandomBot variable is gone from InitEquipment.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FACTORY = (ROOT / "ai/playerbot/PlayerbotFactory.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class GearSeedWiringTest(unittest.TestCase):
    def test_seed_band_uses_the_floor_policy(self):
        self.assertIn("qBegin = TortoiseBots::SeedQualityFloor(level);", FACTORY)
        self.assertIn("q = TortoiseBots::SeedQualityFloor(level);", FACTORY)
        self.assertIn('#include "../../runtime/GearSeedQualityPolicy.h"', FACTORY)

    def test_seed_band_has_no_grey_floor(self):
        seed = body(FACTORY, "uint32 qBegin = quality;", "for (uint32 q = qBegin;")
        self.assertNotIn("ITEM_QUALITY_POOR", seed)
        fallback = body(FACTORY, "seedEpic = false;\n                    for", "QuerySeedCandidates")
        self.assertNotIn("ITEM_QUALITY_POOR", fallback)

    def test_grey_stays_in_the_empty_slot_backstop(self):
        gates = body(FACTORY, "gates.minQuality =", "gates.maxQuality")
        self.assertIn("ITEM_QUALITY_POOR", gates)

    def test_start_level_minimum_defaults_to_five(self):
        self.assertRegex(CONFIG, r'"AiPlayerbot\.RandomBotStartLevelMin",\s*5\)')
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.RandomBotStartLevelMin = 5$")

    def test_gear_upgrade_comment_says_one_time_seed(self):
        i = TEMPLATE.index("#AiPlayerbot.RandomGearUpgradeEnabled")
        comment = TEMPLATE[:i].rsplit("\n\n", 1)[-1]
        self.assertIn("One-time", comment)
        self.assertIn("does NOT upgrade", comment)
        self.assertNotIn("Upgrade pooled bots' gear as they level", TEMPLATE)

    def test_unused_is_random_bot_is_gone(self):
        init = body(FACTORY, "void PlayerbotFactory::InitEquipment(", "\nvoid PlayerbotFactory::")
        self.assertNotIn("isRandomBot", init)


if __name__ == "__main__":
    unittest.main()
