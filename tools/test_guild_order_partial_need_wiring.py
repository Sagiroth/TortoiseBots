"""A craft order keeps sourcing while one item waits on the player.

runtime/GuildOrderStore.cpp no longer parks the whole order when a single
component or sub-order fails: the item goes on the need list, the row stays
working, the ladder re-plans, and the need lanes and the whisper run on
working rows too. ai/playerbot/strategy/values/GuildValues.cpp folds the
recipe materials through the policy header instead of stopping at the first
missing reagent.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
VALUES = (ROOT / "ai/playerbot/strategy/values/GuildValues.cpp").read_text()


class GuildOrderPartialNeedWiringTest(unittest.TestCase):
    def test_set_supply_keeps_need_list(self):
        start = STORE.index("void SetSupply(")
        block = STORE[start:STORE.index("void SetNeedItems(", start)]
        self.assertNotIn("needItems.clear()", block)

    def test_whisper_uses_policy(self):
        self.assertIn("NeedWhisperDue(record.state, record.reason", STORE)

    def test_lanes_use_policy(self):
        self.assertIn("NeedBankLaneEligible(", STORE)
        self.assertIn("NeedLadderLaneEligible(", STORE)

    def test_sub_failure_keeps_parent_working(self):
        start = STORE.index("void NoteSubOrderFailed(")
        block = STORE[start:STORE.index("bool ReassignSubOrder(", start)]
        self.assertIn("MergeNeedItem(", block)
        self.assertNotIn("record.state = OrderState::Waiting;", block)

    def test_ladder_skips_need_items(self):
        self.assertIn("NeedListHas(order->needItems", VALUES)
        self.assertIn("FoldMaterials(", VALUES)
        self.assertIn("PartialNeedVerdictFor(", VALUES)

    def test_craft_need_merges(self):
        self.assertIn("AddNeedItem(order.botGuidLow, candidate.itemId, candidate.count)", VALUES)


if __name__ == "__main__":
    unittest.main()
