"""Wiring of the raid config keys AiPlayerbot.DungeonRunRaid and AiPlayerbot.DungeonRunRaidBossRules."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
HEADER = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RaidConfigWiringTest(unittest.TestCase):
    def test_keys_default_off(self):
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn('dungeonRunRaid = config.GetBoolDefault("AiPlayerbot.DungeonRunRaid", false);', keys)
        self.assertIn("bool dungeonRunRaid", HEADER)
        self.assertIn("AiPlayerbot.DungeonRunRaid = 0", TEMPLATE)
        self.assertIn("AiPlayerbot.DungeonRunRaid", SETTINGS)

    def test_boss_rules_key_default_off_and_fed_to_the_raid_facts(self):
        # AiPlayerbot.DungeonRunRaidBossRules: a raid's boss fight ceiling of 1200 s, deaths that do not pause it while
        # it loses hp, and the off-tanks' pick-ups (DungeonRun::RaidBossRulesFight). Off by default: dev's raid boss
        # fight is unchanged unless it is set.
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn(
            'dungeonRunRaidBossRules = config.GetBoolDefault("AiPlayerbot.DungeonRunRaidBossRules", false);', keys)
        self.assertIn("bool dungeonRunRaidBossRules = false;", HEADER)
        self.assertIn("AiPlayerbot.DungeonRunRaidBossRules = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunRaidBossRules`", SETTINGS)
        self.assertIn("[`AiPlayerbot.DungeonRunRaidBossRules`](#aiplayerbotdungeonrunraidbossrules) | `0`", SETTINGS)
        code = strip_comments(COMMANDER)
        raid = body(code, "    if (raid && sPlayerbotAIConfig.dungeonRunRaid)", "\n    }\n")
        self.assertIn("f.raid.on = true;", raid)
        self.assertIn("f.raid.bossRules = sPlayerbotAIConfig.dungeonRunRaidBossRules;", raid)


if __name__ == "__main__":
    unittest.main()
