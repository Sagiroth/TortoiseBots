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
            between(STORE, "void AddNeedItem(", "bool ReplaceNeedItems("),
            between(STORE, "bool ReplaceNeedItems(", "void RecordGuildGoldWithdrawal("),
            between(STORE, "void NoteSubOrderFailed(", "bool ReassignSubOrder("),
        ):
            self.assertIn("NeedListNeedsWhisper(", block)
        # A sent whisper stores the union of what the player was told.
        self.assertIn("NeedWhisperTokenUnion(", STORE)

    def test_dead_end_reason_reset_on_working(self):
        for block in (
            between(STORE, "void AddNeedItem(", "bool ReplaceNeedItems("),
            between(STORE, "void NoteSubOrderFailed(", "bool ReassignSubOrder("),
        ):
            self.assertIn("ResetDeadEndReasonOnWorking(", block)

    def test_add_need_item_records_bank_denied(self):
        self.assertIn("AddNeedItem(uint32_t botGuidLow, uint32_t itemId, uint32_t count, bool bankDenied)",
            STORE)
        block = between(STORE, "void AddNeedItem(", "bool ReplaceNeedItems(")
        # OR, not assignment: a denial one item recorded survives a later
        # candidate, or the ten-second bank probe would re-arm on every item.
        self.assertIn("record.needBankDenied = record.needBankDenied || bankDenied;", block)
        craft = between(VALUES, "void AddCraftNeedItem(", "void SetSupplyStage(")
        self.assertIn("AddNeedItem(order.botGuidLow, candidate.itemId, count,", craft)
        self.assertIn("order.reason == OrderReason::BankDenied", craft)

    def test_bank_denial_cleared_on_clear_and_resume(self):
        # With OR, every path that clears the need list or resumes it must
        # reset the flag, or the bank lane stays off after one denial.
        for block in (
            between(STORE, "bool Cancel(uint32_t botGuidLow)", "// Map-thread requests"),
            between(STORE, "else if (IsFinished(state))", "// A round that ends cant"),
            between(STORE, "OrderRecord stub = sub->second.record;",
                "m_orders[subOrderBotGuidLow] = Stored{stub};"),
            between(STORE, "if (record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems &&\n            (in.needItemAvailable",
                "if (record.state == OrderState::Working && !record.cancelled && !record.needItems.empty()"),
            between(STORE, "if (wasStocked && record.keep && in.bankChecked)",
                "// A deaths-driven zone move"),
            between(STORE, "if (record.state == OrderState::Working && !record.cancelled && !record.needItems.empty()",
                "// Keep watch:"),
        ):
            self.assertIn("needBankDenied = false;", block)

    def test_keep_rearm_clears_whisper_state(self):
        # A keep round's fresh plan is a new round: the finished round's list,
        # whisper flag and token must not silence its first need list.
        block = between(STORE, "if (wasStocked && record.keep && in.bankChecked)",
            "// A deaths-driven zone move")
        self.assertIn("record.needItems.clear();", block)
        self.assertIn("record.needWhispered = false;", block)
        self.assertIn("record.lastNeedWhisper.clear();", block)

    def test_clock_gate_pauses_only_without_own_leg(self):
        # The gate pauses only when the parent has no leg of its own (the
        # ladder's WaitOnSubOrders return is step None) and an unfinished
        # child runs; the covered-items list (which counts delivery-waiting
        # finished rows) no longer drives it.
        self.assertIn("PauseOrderClocks(record.supplyStep", STORE)
        self.assertIn("UnfinishedSubOrderCount(guidLow)", STORE)
        self.assertNotIn("subOrdersRunning", STORE)

    def test_unfinished_count_filters_and_expires(self):
        block = between(STORE, "uint32_t UnfinishedSubOrderCount(",
            "std::vector<uint32_t> CollectableSubOrderItems(")
        # Only a child that is not finished and not cancelled counts.
        self.assertIn("!IsFinished(row.state)", block)
        self.assertIn("row.cancelled", block)
        # A queued request stops counting once its staleness window elapsed.
        self.assertIn("kQueuedSubOrderStaleMs", block)
        self.assertIn("queuedAtMs", block)

    def test_unfinished_count_does_not_use_covered_items(self):
        # The covered-items list still counts a finished child's pending
        # delivery: the clock's own count must not be that list.
        block = between(STORE, "uint32_t UnfinishedSubOrderCount(",
            "std::vector<uint32_t> CollectableSubOrderItems(")
        self.assertNotIn("CoveredSubOrderItems", block)

    def test_queued_stamp_survives_retry(self):
        # A retry keeps the original queue time, so the pause a queued request
        # causes expires like the request, not with each 45-second retry.
        self.assertIn("request.queuedAtMs = WorldTimer::getMSTime();", STORE)
        self.assertIn("nowMs + kSubOrderRetryMs, request.queuedAtMs)", STORE)

    def test_sub_failure_reason_reset_with_leg(self):
        # The dead-end reset sits inside the leg-reset block: a leg that
        # survives keeps the reason its own search produced.
        block = between(STORE, "void NoteSubOrderFailed(", "bool ReassignSubOrder(")
        leg = block[block.index("if (!KeepLegOnNeedClear(record.supplyItemId, record.needItems))"):]
        self.assertIn("ResetDeadEndReasonOnWorking(", leg)

    def test_replace_need_items_compare_and_set(self):
        self.assertIn("bool ReplaceNeedItems(uint32_t botGuidLow, std::vector<NeedItem> const& expected",
            STORE)
        self.assertIn("NeedItemsEqual(record.needItems, expected)", STORE)
        # The caller keeps the prune only when the write landed; otherwise it
        # re-reads the live row.
        self.assertIn("ReplaceNeedItems(order.botGuidLow, current.needItems, pruned)", VALUES)
        self.assertIn("GuildOrders::Find(order.botGuidLow)", VALUES)

    def test_craft_need_uses_output_shortage(self):
        # The walk counts the output the bot still owes (delivered + bags +
        # mail), the same shortage the prune uses, not amount - delivered.
        block = between(VALUES, "void AddCraftNeedItem(", "void SetSupplyStage(")
        self.assertIn("outputMissing", block)
        self.assertIn("CollectMissingReagents(ai, bot, order.itemId, outputMissing", block)
        self.assertNotIn("order.amount - order.delivered", block)

    def test_working_bank_lane_gated_on_withdraw(self):
        tail = STORE[STORE.index("void Update(uint32_t diff)"):]
        self.assertIn("guildOrderBankWithdraw", tail)
        self.assertIn("needBankDenied", tail)

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

    def test_lanes_use_policy(self):
        self.assertIn("NeedBankLaneEligible(", STORE)
        self.assertIn("NeedLadderLaneEligible(", STORE)

    def test_ladder_skips_need_items(self):
        self.assertIn("NeedListHas(order->needItems", VALUES)
        self.assertIn("FoldMaterials(", VALUES)
        self.assertIn("PartialNeedVerdictFor(", VALUES)


if __name__ == "__main__":
    unittest.main()
