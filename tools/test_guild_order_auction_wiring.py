"""Guild auction-leg wiring: one search, the two-minute reach deadline, and
the buy/relist path for oversized stacks.

The policy header owns the rules (test_guild_order_auction_policy.cpp); this
pins where they are wired: the travel pick hand-off, the ladder refresh's
reach check before the normal source choice, the store's leg clock, the buy
action's listing pick and relist, and the fallback that leaves an unpostable
surplus to the delivery leftover path.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRAVEL = (ROOT / "ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp").read_text()
VALUES = (ROOT / "ai/playerbot/strategy/values/GuildValues.cpp").read_text()
BUY = (ROOT / "ai/playerbot/strategy/actions/GuildOrderBuyAction.cpp").read_text()
MOVE = (ROOT / "ai/playerbot/strategy/actions/MoveToTravelTargetAction.cpp").read_text()
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
CONTRACT = (ROOT / "runtime/GuildOrderContract.h").read_text()
DELIVERY = (ROOT / "runtime/GuildOrderDelivery.cpp").read_text()


def body(text, start_marker, end_marker):
    start = text.index(start_marker)
    return text[start:text.index(end_marker, start)]


class GuildOrderAuctionWiringTest(unittest.TestCase):
    def test_travel_search_advances_after_one_auction_miss(self):
        body_text = body(TRAVEL, "static bool OrderLegSearchEmpty(", "\nstatic ")
        auction = body_text[body_text.index("record->supplyStep == SupplyStep::AuctionHouse"):]
        auction = auction[:auction.index("return true;")]
        self.assertIn("NoteSupplySearchFailed(", auction)
        self.assertIn("AuctionSearchMovesLadder(fails)", auction)
        self.assertIn("AdvanceGuildOrderAfterAuction(ai, bot, *record, OrderReason::NoAuctioneer)",
            auction)
        self.assertNotIn("atLimit(SupplyStep::AuctionHouse", auction)

    def test_ladder_refresh_checks_reach_before_source_choice(self):
        refresh = body(VALUES, "void RefreshGuildOrderSupply(", "\n    if (order.type != OrderType::Craft)")
        check = refresh.index("AuctionLegUnreachable(order.botGuidLow, nowMs)")
        self.assertLess(check, refresh.index("SupplyRefreshDue(order.botGuidLow, nowMs)"))
        self.assertIn("GuildOrderAuctioneerInReach(ai, bot)", refresh)
        self.assertIn("NoteAuctioneerInReach(order.botGuidLow)", refresh)
        self.assertIn("AdvanceGuildOrderAfterAuction(ai, bot, order, OrderReason::NoAuctioneer)",
            refresh)

    def test_relist_leg_waits_until_the_bought_stack_is_in_the_bags(self):
        refresh = body(VALUES, "void RefreshGuildOrderSupply(", "\n    if (order.type != OrderType::Craft)")
        self.assertIn("PendingRelist(order.botGuidLow, relistItemId, relistCount,", refresh)
        self.assertIn("SnapshotBags(ai, snapshot, relistItemId) >= relistCount", refresh)
        self.assertIn("SetSupply(order.botGuidLow, SupplyStep::AuctionHouse,", refresh)
        self.assertIn("relistItemId, relistCount, SupplyStep::None, order.reason", refresh)

    def test_store_clock_starts_on_leg_entry_and_clears_on_reach(self):
        set_supply = body(STORE, "void SetSupply(uint32_t botGuidLow", "\n    void SetNeedItems(")
        self.assertIn("enteringAuction", set_supply)
        self.assertIn("auctionReachStartedMs = WorldTimer::getMSTime()", set_supply)
        self.assertIn("auctionReachStartedMs = 0", set_supply)
        self.assertIn("AuctionUnreachableElapsed(started, nowMs)", STORE)
        self.assertIn("void NoteAuctioneerInReach(", STORE)

    def test_buy_picks_the_smallest_covering_stack(self):
        execute = body(BUY, "bool GuildOrderBuyAction::Execute(", "\nbool GuildOrderTakeMailAction")
        self.assertIn("AuctionPickBeats(count, auction->buyout, best.count, best.buyout, missing)",
            execute)
        self.assertNotIn("count > missing", execute)
        self.assertIn("uint32_t const surplus = BuySurplus(best.count, missing);", execute)
        self.assertIn("NoteRelistSurplus(botGuidLow, itemId, surplus, AuctionUnitPrice(best.buyout, best.count));",
            execute)

    def test_relist_posts_the_surplus_at_the_policy_price(self):
        relist = body(BUY, "uint32_t RelistSurplus(", "\n} // namespace")
        self.assertIn("RelistUnitPriceFromUnit(boughtUnitPrice,", relist)
        self.assertIn("LowestListingUnitPrice(auctionHouse, itemId)", relist)
        self.assertIn("RelistTotal(unitPrice, postCount)", relist)
        self.assertIn("kRelistDurationMinutes", relist)
        self.assertIn("HandleSplitItemOpcode(", relist)
        self.assertIn("HandleAuctionSellItem(", relist)
        execute = body(BUY, "bool GuildOrderBuyAction::Execute(", "\nbool GuildOrderTakeMailAction")
        self.assertIn("PendingRelist(botGuidLow, relistItemId, relistCount, relistUnitPrice)",
            execute)

    def test_relist_failure_leaves_the_surplus_for_delivery_deposit(self):
        execute = body(BUY, "bool GuildOrderBuyAction::Execute(", "\nbool GuildOrderTakeMailAction")
        failure = execute[execute.index("if (!posted)"):]
        failure = failure[:failure.index("return false;")]
        self.assertIn("SurplusDestinationAfterRelist(false, relistCount)", failure)
        self.assertIn("SetSupply(botGuidLow, SupplyStep::None, 0, 0)", failure)
        self.assertNotIn("NoteRelistSurplus", failure)
        # That path is the delivery leftover deposit, not a second AH try.
        self.assertIn("DeliverCraftLeftovers(", DELIVERY)
        self.assertIn("DepositPass(", DELIVERY)

    def test_arrival_exit_uses_the_unreachable_reason(self):
        arrival = body(MOVE, "static void CountGuildOrderAuctionArrival(", "\nbool MoveToTravelTargetAction::Execute")
        self.assertIn("AdvanceGuildOrderAfterAuction(ai, bot, *record, OrderReason::NoAuctioneer)",
            arrival)

    def test_contract_carries_noauctioneer(self):
        self.assertIn('case OrderReason::NoAuctioneer: return "noauctioneer";', CONTRACT)
        self.assertIn("case OrderReason::NoAuctioneer:return OrderState::Working;", CONTRACT)


if __name__ == "__main__":
    unittest.main()
