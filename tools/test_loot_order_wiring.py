"""A loot order reaches a chest that holds only common loot (T24, regression from 046c5002d).

The rule is pure (ai/playerbot/policy/loot/GameObjectPickupPolicy.h, tested in
tools/test_game_object_pickup_policy.cpp). This pins the glue:
- a commander's loot act builds its LootObject with the order flag, and the same
  object goes into "loot target" so the "open loot" it issues keeps the flag;
- a player's explicit "use" on a chest does the same;
- LootObject::Refresh hands the member to the common-world-prop filter, so the
  re-Refresh inside GetWorldObject and the open-loot path keep the object too;
- the copy constructor carries the flag (LootObject travels by value through
  "loot target" and the trigger reads);
- the queue entry carries the order and OrderByDistance rebuilds each LootObject
  with it, so a queued order survives selection; an automatic entry has none.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
STACK = (ROOT / "ai/playerbot/LootObjectStack.cpp").read_text()
STACK_H = (ROOT / "ai/playerbot/LootObjectStack.h").read_text()
ADD_LOOT = (ROOT / "ai/playerbot/strategy/actions/AddLootAction.cpp").read_text()
USE = (ROOT / "ai/playerbot/strategy/actions/UseItemAction.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/policy/loot/GameObjectPickupPolicy.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class LootOrderWiringTest(unittest.TestCase):
    def test_the_commander_loot_act_asks_for_an_ordered_object(self):
        fn = body(COMMANDER, "ActStart BeginLootAct(CommanderTask& task")
        self.assertIn("ai::LootObject lootObject(bot, guid, true);", fn)

    def test_open_loot_receives_that_object(self):
        fn = body(COMMANDER, "ActStart BeginLootAct(CommanderTask& task")
        self.assertIn('GetValue<ai::LootObject>("loot target")->Set(lootObject);', fn)
        self.assertNotIn('Set(ai::LootObject(bot, guid', fn)

    def test_a_players_use_on_a_chest_is_an_order(self):
        fn = body(USE, "bool UseAction::UseGameObject(")
        self.assertIn("LootObject(bot, guid, true)", fn)

    def test_refresh_hands_the_member_to_the_filter(self):
        fn = body(STACK, "void LootObject::Refresh(")
        self.assertIn("IsCommonWorldProp(bot, go, playerOrder)", fn)
        helper = body(STACK, "static bool IsCommonWorldProp(")
        self.assertEqual(helper.splitlines()[0],
                         "static bool IsCommonWorldProp(Player* bot, GameObject* go, bool playerOrder)")
        self.assertIn("facts.playerOrder = playerOrder;", helper)

    def test_the_copy_constructor_carries_the_flag(self):
        fn = body(STACK, "LootObject::LootObject(const LootObject& other)")
        self.assertIn("playerOrder = other.playerOrder;", fn)

    def test_the_member_defaults_off_and_the_constructor_takes_it(self):
        self.assertIn("bool playerOrder = false;", STACK_H)
        self.assertIn("LootObject(Player* bot, ObjectGuid guid, bool playerOrder);", STACK_H)

    def test_selection_keeps_the_queued_order(self):
        fn = body(STACK, "std::vector<LootObject> LootObjectStack::OrderByDistance(")
        # The queued entry's own provenance, not a blanket false: a player-ordered common-only
        # chest would be cleared on Refresh and never reach GetLoot.
        self.assertIn("LootObject lootObject(bot, guid, i->playerOrder);", fn)
        self.assertNotIn("LootObject lootObject(bot, guid, false);", fn)

    def test_the_queue_carries_the_order(self):
        self.assertIn("bool Add(ObjectGuid guid, bool playerOrder = false);", STACK_H)
        self.assertIn("bool playerOrder;", body(STACK_H, "class LootTarget", "class LootTargetList"))
        fn = body(STACK, "bool LootObjectStack::Add(")
        self.assertIn("availableLoot.insert(LootTarget(guid, playerOrder));", fn)

    def test_add_all_loot_builds_its_target_with_the_run_order(self):
        fn = body(ADD_LOOT, "bool AddAllLootAction::AddLoot(")
        self.assertIn("LootObject loot(bot, guid, playerOrder);", fn)
        self.assertIn("lootStack->Add(guid, playerOrder);", fn)

    def test_the_filter_allows_an_order_first(self):
        fn = body(POLICY, "inline bool AllowGameObjectPickup(")
        self.assertTrue(fn.lstrip().startswith("inline bool AllowGameObjectPickup("))
        self.assertIn("if (facts.playerOrder)", fn.split("if (!facts.isChest")[0])


if __name__ == "__main__":
    unittest.main()
