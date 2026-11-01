"""A craft order keeps sourcing while one item waits on the player.

runtime/GuildOrderStore.cpp no longer parks the whole order when a single
component or sub-order fails: the item goes on the need list, the row stays
working, the ladder re-plans, and the need lanes and the whisper run on
working rows too. ai/playerbot/strategy/values/GuildValues.cpp folds the
recipe materials through the policy header instead of stopping at the first
missing reagent. This pins the call sites the policy header cannot reach.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
VALUES = (ROOT / "ai/playerbot/strategy/values/GuildValues.cpp").read_text()


def between(source, start_marker, end_marker):
    start = source.index(start_marker)
    return source[start:source.index(end_marker, start)]


class GuildOrderPartialNeedWiringTest(unittest.TestCase):
    def test_set_supply_keeps_need_list(self):
        block = between(STORE, "void SetSupply(", "void SetNeedItems(")
        self.assertNotIn("needItems.clear()", block)

    def test_whisper_uses_policy(self):
        self.assertIn("NeedWhisperDue(record.state, record.reason", STORE)

    def test_whisper_sites_use_policy(self):
        # Every list mutation decides whether the player already heard about
        # it through the policy predicate, never token equality.
        for block in (
            between(STORE, "void SetNeedItems(", "void AddNeedItem("),
            between(STORE, "void AddNeedItem(", "void ReplaceNeedItems("),
            between(STORE, "void ReplaceNeedItems(", "void RecordGuildGoldWithdrawal("),
            between(STORE, "void NoteSubOrderFailed(", "bool ReassignSubOrder("),
        ):
            self.assertIn("NeedListNeedsWhisper(", block)
        # A sent whisper stores the union of what the player was told.
        self.assertIn("NeedWhisperTokenUnion(", STORE)

    def test_dead_end_reason_reset_on_working(self):
        for block in (
            between(STORE, "void AddNeedItem(", "void ReplaceNeedItems("),
            between(STORE, "void NoteSubOrderFailed(", "bool ReassignSubOrder("),
        ):
            self.assertIn("ResetDeadEndReasonOnWorking(", block)

    def test_add_need_item_records_bank_denied(self):
        self.assertIn("AddNeedItem(uint32_t botGuidLow, uint32_t itemId, uint32_t count, bool bankDenied)",
            STORE)
        self.assertIn("record.needBankDenied = bankDenied;", STORE)
        block = between(VALUES, "void AddCraftNeedItem(", "void SetSupplyStage(")
        self.assertIn("AddNeedItem(order.botGuidLow, candidate.itemId, count,", block)
        self.assertIn("order.reason == OrderReason::BankDenied", block)

    def test_craft_need_uses_whole_tree_count(self):
        block = between(VALUES, "void AddCraftNeedItem(", "void SetSupplyStage(")
        self.assertIn("CollectMissingReagents(", block)
        self.assertIn("need.count > count", block)

    def test_working_bank_lane_gated_on_withdraw(self):
        tail = STORE[STORE.index("void Update(uint32_t diff)"):]
        self.assertIn("guildOrderBankWithdraw", tail)
        self.assertIn("needBankDenied", tail)

    def test_no_progress_clock_pauses_for_sub_orders(self):
        # The clock gate reads the covered sub-order items, not only the Sub step.
        self.assertIn("CoveredSubOrderItems(guidLow)", STORE)
        self.assertIn("!in.subOrdersRunning", STORE)
        self.assertIn("subOrdersRunning =", STORE)

    def test_non_craft_sub_failure_parks(self):
        block = between(STORE, "void NoteSubOrderFailed(", "bool ReassignSubOrder(")
        self.assertIn("MergeNeedItem(", block)
        self.assertIn("record.type == OrderType::Craft", block)
        self.assertIn("record.state = OrderState::Waiting;", block)
        self.assertIn("record.reason = OrderReason::NeedItems;", block)
        self.assertIn("record.supplyStep = SupplyStep::Need;", block)

    def test_need_clear_keeps_unrelated_leg(self):
        self.assertIn("KeepLegOnNeedClear(", STORE)
        self.assertGreaterEqual(STORE.count("KeepLegOnNeedClear(record.supplyItemId, record.needItems)"), 2)

    def test_replace_need_items_compare_and_set(self):
        self.assertIn("void ReplaceNeedItems(uint32_t botGuidLow, std::vector<NeedItem> const& expected",
            STORE)
        self.assertIn("NeedItemsEqual(record.needItems, expected)", STORE)
        self.assertIn("ReplaceNeedItems(order.botGuidLow, current.needItems, pruned)", VALUES)

    def test_lanes_use_policy(self):
        self.assertIn("NeedBankLaneEligible(", STORE)
        self.assertIn("NeedLadderLaneEligible(", STORE)

    def test_ladder_skips_need_items(self):
        self.assertIn("NeedListHas(order->needItems", VALUES)
        self.assertIn("FoldMaterials(", VALUES)
        self.assertIn("PartialNeedVerdictFor(", VALUES)


if __name__ == "__main__":
    unittest.main()
