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

    def test_purge_destroys_only_what_the_call_created(self):
        # r2: pre-existing bag items survive; only a created-then-unused
        # item may be destroyed.
        self.assertIn("CollectCreatedItemGuids(", SERVICE)
        self.assertIn("BotGear::CreatedItemUnused(", SERVICE)
        self.assertIn("CreatedItemMayBeDestroyed", POLICY)
        purge = strip_comments(body(SERVICE, "auto purgeSlot = [&](uint8 bag, uint8 slot, Item* item)", "};"))
        self.assertIn("BotGear::PurgeBagItem(", purge)
        self.assertIn("BotGear::CreatedItemUnused(", purge)
        self.assertIn("bot->DestroyItem(bag, slot, true);", purge)
        self.assertIn("++kept;", purge)

    def test_before_snapshot_includes_worn_equipment(self):
        # r3 (Codex): an item the gear pass swaps off the body lands in a
        # bag; it must already be in `before`, or the purge would treat it
        # as created and could destroy it.
        owned = strip_comments(body(SERVICE, "void CollectOwnedItemGuids(Player* bot, std::set<uint32_t>& out)", "\n}"))
        self.assertIn("CollectBagItemGuids(bot, out);", owned)
        self.assertIn("EQUIPMENT_SLOT_START", owned)
        self.assertIn("EQUIPMENT_SLOT_END", owned)
        code = strip_comments(SERVICE)
        self.assertIn("CollectOwnedItemGuids(bot, before);", code)
        self.assertNotIn("CollectBagItemGuids(bot, before);", code)

    def test_worn_above_cap_is_unequipped_not_destroyed(self):
        self.assertIn("UnequipAboveCapWorn(", SERVICE)
        self.assertIn("BotGear::WornAboveCap(", SERVICE)
        wear = strip_comments(body(SERVICE, "uint32_t UnequipAboveCapWorn(Player* bot, uint32_t cap)", "\n}\n"))
        self.assertIn("bot->RemoveItem(", wear)
        self.assertIn("bot->StoreItem(", wear)
        self.assertNotIn("DestroyItem", wear)

    def test_pin_holds_the_cap_in_the_equip_path(self):
        equip = (ROOT / "ai/playerbot/strategy/actions/EquipAction.cpp").read_text()
        self.assertIn("BotGearService::Instance().PinnedCap(", equip)
        self.assertIn("ITEM_CLASS_WEAPON", equip)
        self.assertIn("ITEM_CLASS_ARMOR", equip)

    def test_skip_reasons_and_null_ai(self):
        svc = strip_comments(SERVICE)
        self.assertIn('outcome.skip = "offline";', svc)
        self.assertIn('outcome.skip = "not-bot";', svc)
        self.assertIn('outcome.skip = "not-in-world";', svc)
        self.assertIn('outcome.skip = "no-ai";', svc)

    def test_logout_drops_the_pin(self):
        mgr = (ROOT / "runtime/BotManager.cpp").read_text()
        self.assertIn("BotGearService::Instance().OnBotLogout(", mgr)

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
