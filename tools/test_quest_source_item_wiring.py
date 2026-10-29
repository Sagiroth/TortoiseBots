"""The bot treats a quest's source item as needed (TB #1307).

The rule is pure (ai/playerbot/policy/loot/QuestSourceItemPolicy.h, tested in
tools/test_quest_source_item_policy.cpp). This pins the glue, which only builds
against the core:
- ItemUsageValue::IsNeededForQuest also matches quest->ReqSourceId and asks
  the policy, with the bag count from player->GetItemCount(itemId, true) like
  the core's Player::HasQuestForItem ReqSource branch, honouring
  ignoreInventory (needed regardless of count);
- LootObject::Refresh and OpenLootAction::DoLoot both decide "corpse still has
  body loot for this bot" through ai::CorpseHasBodyLootFor
  (ai/playerbot/policy/loot/CorpseLootPolicy.h: raw lootable flag OR
  bot->IsAllowedToLoot): a corpse whose only loot left is the bot's own quest
  item can lack the flag (the core masks it per viewer), and MayLootCorpse
  stays the gate inside Refresh. DoLoot must not test the raw flag on its own,
  or an allowed corpse without it falls through to the skinning/gather spell.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ITEM_USAGE = (ROOT / "ai/playerbot/strategy/values/ItemUsageValue.cpp").read_text()
STACK = (ROOT / "ai/playerbot/LootObjectStack.cpp").read_text()
LOOT_ACTION = (ROOT / "ai/playerbot/strategy/actions/LootAction.cpp").read_text()
HELPER = (ROOT / "ai/playerbot/policy/loot/CorpseLootPolicy.h").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class QuestSourceItemWiringTest(unittest.TestCase):
    def test_the_quest_check_asks_the_source_policy(self):
        fn = body(ITEM_USAGE, "bool ItemUsageValue::IsNeededForQuest(", "\n}\n")
        self.assertIn("ReqSourceId", fn)
        self.assertIn("ai::QuestSourceItemStillNeeded(", fn)
        self.assertIn("GetItemCount(itemId, true)", fn)

    def test_the_helper_covers_flag_and_viewer_right(self):
        self.assertIn("CorpseHasBodyLootFor(Player* bot, Creature* creature)", HELPER)
        self.assertIn("UNIT_DYNFLAG_LOOTABLE", HELPER)
        self.assertIn("IsAllowedToLoot(creature)", HELPER)

    def test_the_corpse_branch_trusts_the_viewer_flag(self):
        fn = body(STACK, "void LootObject::Refresh(", "\n}\n")
        self.assertIn("ai::CorpseHasBodyLootFor(bot, creature)", fn)
        self.assertIn("MayLootCorpse(bot, creature)", fn)

    def test_doloot_sends_cmsg_loot_through_the_helper(self):
        fn = body(LOOT_ACTION, "bool OpenLootAction::DoLoot(", "\n}\n")
        self.assertIn("ai::CorpseHasBodyLootFor(bot, creature)", fn)
        self.assertIn("CMSG_LOOT", fn)
        self.assertNotIn("HasFlag(UNIT_DYNAMIC_FLAGS", fn)


if __name__ == "__main__":
    unittest.main()
