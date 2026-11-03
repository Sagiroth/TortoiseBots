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
HEADER = (ROOT / "ai/playerbot/PlayerbotFactory.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def strip(text):
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class GearSeedWiringTest(unittest.TestCase):
    def test_seed_band_uses_the_floor_policy(self):
        self.assertIn("qBegin = TortoiseBots::GearQualityFloor(level, freshPoolSeed);", FACTORY)
        self.assertIn("q = TortoiseBots::GearQualityFloor(level, freshPoolSeed);", FACTORY)
        self.assertIn('#include "../../runtime/GearSeedQualityPolicy.h"', FACTORY)

    def test_seed_band_has_no_grey_floor(self):
        seed = body(FACTORY, "uint32 qBegin = quality;", "for (uint32 q = qBegin;")
        self.assertNotIn("ITEM_QUALITY_POOR", seed)
        fallback = body(FACTORY, "seedEpic = false;\n                    for", "QuerySeedCandidates")
        self.assertNotIn("ITEM_QUALITY_POOR", fallback)

    def test_backstop_floor_follows_the_seed_path(self):
        gates = body(FACTORY, "gates.minQuality =", "gates.maxQuality")
        self.assertIn("TortoiseBots::GearQualityFloor(level, freshPoolSeed)", gates)
        self.assertNotIn("ITEM_QUALITY_POOR", gates)

    def test_start_level_minimum_defaults_to_five(self):
        self.assertRegex(CONFIG, r'"AiPlayerbot\.RandomBotStartLevelMin",\s*5\)')
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.RandomBotStartLevelMin = 5$")

    def test_gear_upgrade_comment_says_one_time_seed(self):
        i = TEMPLATE.index("#AiPlayerbot.RandomGearUpgradeEnabled")
        comment = TEMPLATE[:i].rsplit("\n\n", 1)[-1]
        self.assertIn("One-time", comment)
        self.assertIn("does NOT upgrade", comment)
        self.assertNotIn("Upgrade pooled bots' gear as they level", TEMPLATE)

    def test_fresh_pool_seed_flag_is_set_only_by_the_seed_path(self):
        # The flag is a private member set in exactly one place: MakeComplete.
        make = body(FACTORY, "void PlayerbotFactory::MakeComplete(bool freshSeed)", "\n}\n")
        make_start = FACTORY.index(make)
        for pos in [m.start() for m in re.finditer(r"\bfreshPoolSeed\s*=\s*[^=]", FACTORY)]:
            self.assertTrue(make_start <= pos < make_start + len(make), "freshPoolSeed assigned outside MakeComplete")
        self.assertIn("freshPoolSeed = freshSeed;", make)
        self.assertLess(make.index("freshPoolSeed = freshSeed;"), make.index("InitEquipment(true, false);"))
        self.assertLess(make.index("InitEquipment(true, false);"), make.index("freshPoolSeed = false;"))
        self.assertIn("bool freshPoolSeed = false;", HEADER)
        self.assertIn("void MakeComplete(bool freshPoolSeed);", HEADER)

    def test_only_the_seed_facade_calls_make_complete_true(self):
        calls = {}
        for path in ROOT.rglob("*.cpp"):
            if "tools" in path.parts or ".git" in path.parts:
                continue
            for m in re.finditer(r"MakeComplete\(([^)]*)\)", strip(path.read_text())):
                if path.name == "PlayerbotFactory.cpp" and "bool freshSeed" in m.group(1):
                    continue
                calls.setdefault(path.name, []).append(m.group(1))
        self.assertEqual(calls, {"PlayerbotRuntimeFacade.cpp": ["true"]})

    def test_facade_is_reached_only_from_fresh_guarded_seed_sites(self):
        for name in ("runtime/BotManager.cpp", "runtime/RandomBotService.cpp"):
            text = (ROOT / name).read_text()
            i = text.index("UpdateGearSpells(")
            self.assertIn("NeedsInitialGearSeeding", text[max(0, i - 2500):i], name)
        callers = [p.name for p in ROOT.rglob("*.cpp")
                   if "tools" not in p.parts and ".git" not in p.parts and "UpdateGearSpells(" in p.read_text()]
        self.assertEqual(sorted(callers), ["BotManager.cpp", "PlayerbotRuntimeFacade.cpp", "RandomBotService.cpp"])

    def test_other_gear_paths_never_set_the_flag(self):
        for fn, end in (("void PlayerbotFactory::AutogearPlayer(", "\n}\n"), ("void PlayerbotFactory::AutogearOwned(", "\n}\n")):
            self.assertNotIn("freshPoolSeed", body(FACTORY, fn, end))
        provision = body(FACTORY, "UseStandardHireGearCap();", "InitEquipment(true, false);")
        self.assertNotIn("freshPoolSeed", provision)

    def test_unused_is_random_bot_is_gone(self):
        init = body(FACTORY, "void PlayerbotFactory::InitEquipment(", "\nvoid PlayerbotFactory::")
        self.assertNotIn("isRandomBot", init)


if __name__ == "__main__":
    unittest.main()
