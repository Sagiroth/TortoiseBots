"""Wiring of the raid config key AiPlayerbot.DungeonRunRaid."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
HEADER = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()


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


if __name__ == "__main__":
    unittest.main()
