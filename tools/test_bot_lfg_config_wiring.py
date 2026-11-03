"""Bot LFG group config keys (part 2, Task 5).

Pins that PlayerbotAIConfig reads the four BotLfg* keys, the shipped template
shows the values that actually run, and the stale SystemRunPolicy comment is gone.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class BotLfgConfigWiringTest(unittest.TestCase):
    def test_load_dungeon_run_keys(self):
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn(
            'botLfgFillAfterMinutes = static_cast<uint32>(std::min(60, std::max(1, '
            'config.GetIntDefault("AiPlayerbot.BotLfgFillAfterMinutes", 10))));',
            keys)
        self.assertIn(
            'botLfgGroupsPer100Bots = static_cast<uint32>(std::min(20, std::max(0, '
            'config.GetIntDefault("AiPlayerbot.BotLfgGroupsPer100Bots", 1))));',
            keys)
        self.assertIn(
            'botLfgMaxGroups = static_cast<uint32>(std::min(20, std::max(0, '
            'config.GetIntDefault("AiPlayerbot.BotLfgMaxGroups", 5))));',
            keys)
        self.assertIn(
            'botLfgOnlyWithPlayersOnline = config.GetBoolDefault("AiPlayerbot.BotLfgOnlyWithPlayersOnline", true);',
            keys)

    def test_template_defaults_after_groups(self):
        anchor = TEMPLATE.index("AiPlayerbot.BotLfgGroups = 0")
        for line in ("AiPlayerbot.BotLfgFillAfterMinutes = 10",
                     "AiPlayerbot.BotLfgGroupsPer100Bots = 1",
                     "AiPlayerbot.BotLfgMaxGroups = 5",
                     "AiPlayerbot.BotLfgOnlyWithPlayersOnline = 1"):
            self.assertIn(line, TEMPLATE)
            self.assertGreater(TEMPLATE.index(line), anchor, line)

    def test_header_comment_fixed(self):
        self.assertNotIn("(SystemRunPolicy.h; default on)", HEADER)

    def test_bot_lfg_chat(self):
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        anchor = keys.index("botLfgOnlyWithPlayersOnline =")
        line = 'botLfgChat = config.GetBoolDefault("AiPlayerbot.BotLfgChat", true);'
        self.assertIn(line, keys)
        self.assertGreater(keys.index(line), anchor)
        tanchor = TEMPLATE.index("AiPlayerbot.BotLfgOnlyWithPlayersOnline = 1")
        tline = TEMPLATE.index("AiPlayerbot.BotLfgChat = 1")
        self.assertGreater(tline, tanchor)
        block = TEMPLATE[tanchor:tline]
        for word in ("LookingForGroup", "Only read while",
                     "AiPlayerbot.BotLfgGroups = 1"):
            self.assertIn(word, block)


if __name__ == "__main__":
    unittest.main()
