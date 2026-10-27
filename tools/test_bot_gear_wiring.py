"""The glue of `.bot gear` (raid task A7): the GM-gated handler parses with
BotGear::Parse, the kit pass honours the item-level cap and the raid gear
service purges bagged upgrades and pins what it set.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOTCOMMANDS = (ROOT / "commands" / "BotCommands.cpp").read_text()
PROVISION_CPP = (ROOT / "runtime" / "HireProvisionService.cpp").read_text()
PROVISION_H = (ROOT / "runtime" / "HireProvisionService.h").read_text()
SERVICE = (ROOT / "runtime" / "BotGearService.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
POLICY = (ROOT / "runtime" / "policy" / "hire" / "BotGearPolicy.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class BotGearWiringTest(unittest.TestCase):
    def test_handler_is_gm_gated_before_parse(self):
        handle = strip_comments(body(BOTCOMMANDS, "bool HandleBotGear(", "\n}\n"))
        self.assertIn("IsBotAdministrator(requester)", handle)
        self.assertIn("IsGmCliHandler(handler)", handle)
        self.assertIn("BotGear::Parse(", handle)
        self.assertLess(handle.index("IsBotAdministrator(requester)"), handle.index("BotGear::Parse("))
        self.assertLess(handle.index("IsGmCliHandler(handler)"), handle.index("BotGear::Parse("))

    def test_command_row(self):
        self.assertIn('{"gear", HandleBotGear,', strip_comments(BOTCOMMANDS))

    def test_kit_honours_the_cap(self):
        self.assertIn("BotGear::RowAllowed(", PROVISION_CPP)
        self.assertIn("cappedSlots->push_back(", PROVISION_CPP)

    def test_service_gears_then_purges(self):
        auto = SERVICE.index("AutogearOwned(ITEM_QUALITY_EPIC")
        kit = SERVICE.index("EquipHireGearSet(")
        purge = SERVICE.index("BotGear::PurgeBagItem(")
        save = SERVICE.index("SaveToDB()")
        self.assertLess(auto, kit)
        self.assertLess(kit, purge)
        self.assertLess(purge, save)
        self.assertIn("kGearBotsPerTick", SERVICE)
        self.assertIn("IsPinned(", SERVICE)

    def test_config_default_matches_header(self):
        self.assertIn('AiPlayerbot.BotGearPresets', CONFIG)
        self.assertIn("BotGear::kPresetsDefault", CONFIG)
        match = re.search(r'kPresetsDefault\s*=\s*((?:"[^"]*"\s*)+);', POLICY)
        self.assertIsNotNone(match)
        literal = "".join(re.findall(r'"([^"]*)"', match.group(1)))
        for line in TEMPLATE.splitlines():
            if line.startswith("AiPlayerbot.BotGearPresets ="):
                self.assertEqual(line.split("=", 1)[1].strip(), literal)
                break
        else:
            self.fail("template has no active AiPlayerbot.BotGearPresets line")


if __name__ == "__main__":
    unittest.main()
