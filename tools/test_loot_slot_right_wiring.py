"""The bot's loot loop takes its own quest and party-loot copies (TB #1307).

The rule is pure (ai/playerbot/policy/loot/LootSlotRightPolicy.h, tested in
tools/test_loot_slot_right_policy.cpp). This pins the glue, which only builds
against the core:
- StoreLootAction::Execute asks Loot::LootItemInSlot for the quest and the
  free-for-all copy of the slot, so the policy can tell a personal copy from a
  shared one; the core sets is_blocked on every quest item once it enters a
  player's quest list, and GetSlotTypeForSharedLoot answers MAX for every
  free-for-all item, so the old "is_blocked ||" line skipped both;
- the policy decides, and the skip debug line names the flags;
- LootObject::Refresh says why a creature guid was rejected ("not found" or
  "not a corpse", with the death state) and says "Go has bad lock." only for a
  game object.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTION = (ROOT / "ai/playerbot/strategy/actions/LootAction.cpp").read_text()
STACK = (ROOT / "ai/playerbot/LootObjectStack.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class LootSlotRightWiringTest(unittest.TestCase):
    def test_the_slot_asks_for_its_quest_and_ffa_copies(self):
        self.assertIn("LootItemInSlot(itemindex, bot->GetGUIDLow(), &qitem, &ffaitem)", ACTION)

    def test_the_policy_decides(self):
        fn = body(ACTION, "bool StoreLootAction::Execute(", "\n}\n")
        self.assertIn("ai::BotMayTakeLootSlot(", fn)
        self.assertNotIn("lootItem->is_blocked ||", fn)

    def test_the_corpse_rejection_is_named(self):
        fn = body(STACK, "void LootObject::Refresh(", "\n}\n")
        self.assertIn("Creature is not a corpse", fn)
        self.assertIn("Creature not found on the bot's map.", fn)

    def test_bad_lock_is_said_only_for_a_game_object(self):
        fn = body(STACK, "void LootObject::Refresh(", "\n}\n")
        self.assertIn("if (debug && guid && !this->guid && guid.IsGameObject())", fn)
        self.assertIn('"Go has bad lock."', fn)


if __name__ == "__main__":
    unittest.main()
