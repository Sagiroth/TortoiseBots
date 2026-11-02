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
        # The reach deadline starts in the auctioneer's city, not at leg entry.
        city = refresh.index("GuildOrderAuctioneerWithin(ai, bot, ai::kGuildOrderAuctioneerCityYd)")
        self.assertLess(city, check)
        self.assertIn("NoteAuctionZoneArrival(order.botGuidLow, nowMs)", refresh)
        self.assertIn("AdvanceGuildOrderAfterAuction(ai, bot, order, OrderReason::NoAuctioneer)",
            refresh)

    def test_relist_leg_waits_until_the_bought_stack_is_in_the_bags(self):
        refresh = body(VALUES, "void RefreshGuildOrderSupply(", "\n    if (order.type != OrderType::Craft)")
        self.assertIn("PendingRelist(order.botGuidLow, relistItemId, relistCount,", refresh)
        self.assertIn("SnapshotBags(ai, snapshot, relistItemId) >= relistCount", refresh)
        self.assertIn("SetSupply(order.botGuidLow, SupplyStep::AuctionHouse,", refresh)
        self.assertIn("relistItemId, relistCount, SupplyStep::None, order.reason", refresh)

    def test_store_clock_starts_on_arrival_and_the_walk_is_capped(self):
        set_supply = body(STORE, "void SetSupply(uint32_t botGuidLow", "\n    void SetNeedItems(")
        self.assertIn("enteringAuction", set_supply)
        # Entering the leg starts the travel cap only; the reach deadline
        # starts on arrival (NoteAuctionZoneArrival / NoteArrivedAuctionVisit).
        self.assertIn("auctionLegStartedMs = WorldTimer::getMSTime()", set_supply)
        self.assertIn("auctionReachStartedMs = 0", set_supply)
        unreachable = body(STORE, "bool AuctionLegUnreachable(", "\n    void NoteAuctioneerInReach(")
        self.assertIn("AuctionUnreachableElapsed(it->second.auctionReachStartedMs, nowMs)",
            unreachable)
        self.assertIn("AuctionLegTravelCapped(it->second.auctionLegStartedMs, nowMs)", unreachable)
        self.assertNotIn("if (!started)", unreachable)
        zone = body(STORE, "void NoteAuctionZoneArrival(uint32_t botGuidLow, uint32_t nowMs)",
            "\n    // Either auction clock")
        self.assertIn("if (!it->second.auctionReachStartedMs)", zone)
        arrival = body(STORE, "uint32_t NoteArrivedAuctionVisit(", "\n    // The bot came within sight")
        self.assertIn("auctionReachStartedMs = WorldTimer::getMSTime()", arrival)
        self.assertIn("void NoteAuctioneerInReach(", STORE)
        self.assertIn("void NoteAuctionZoneArrival(", STORE)

    def test_buy_picks_the_listing_through_the_policy(self):
        execute = body(BUY, "bool GuildOrderBuyAction::Execute(", "\nbool GuildOrderTakeMailAction")
        # The listing choice is one policy call (covered by the policy test:
        # smallest covering stack, then cheaper unit, else cheapest partial).
        self.assertIn("std::vector<AuctionListing> candidates;", execute)
        self.assertIn("PickAuctionListing(candidates, missing)", execute)
        self.assertNotIn("count > missing", execute)
        self.assertIn("uint32_t const surplus = BuySurplus(best.count, missing);", execute)
        self.assertIn("NoteRelistSurplus(botGuidLow, itemId, surplus, AuctionUnitPrice(best.buyout, best.count));",
            execute)

    def test_relist_handoff_drops_the_surplus_instead_of_sourcing_it(self):
        advance = body(BUY, "void ai::AdvanceGuildOrderAfterAuction(", "\nbool GuildOrderBuyAction::isUseful(")
        self.assertIn("PendingRelist(order.botGuidLow, relistItemId, relistCount, relistUnitPrice)",
            advance)
        self.assertIn("AuctionHandoffLeavesSurplus(true, relistItemId, order.supplyItemId)", advance)
        relist = advance[advance.index("if (relistLeg)"):]
        relist = relist[:relist.index("return;")]
        self.assertIn("SetSupply(order.botGuidLow, SupplyStep::None, 0, 0)", relist)
        # The surplus must not reach the source planner at all.
        self.assertLess(advance.index("ClearRelistSurplus(order.botGuidLow)"),
            advance.index("if (relistLeg)"))
        self.assertLess(advance.index("if (relistLeg)"),
            advance.index("PlanGuildOrderSources("))

    def test_no_fitting_listing_hands_off_in_the_same_tick(self):
        execute = body(BUY, "bool GuildOrderBuyAction::Execute(", "\nbool GuildOrderTakeMailAction")
        self.assertIn("PickAuctionListing(candidates, missing)", execute)
        miss = execute[execute.index("if (pick == kNoAuctionListing)"):]
        miss = miss[:miss.index("return false;")]
        self.assertIn("AdvanceGuildOrderAfterAuction(ai, bot, *stored, OrderReason::NoListing)",
            miss)
        self.assertLess(miss.index("AdvanceGuildOrderAfterAuction"), miss.index("RESET_AI_VALUE"))

    def test_buy_handoffs_keep_their_reason(self):
        execute = body(BUY, "bool GuildOrderBuyAction::Execute(", "\nbool GuildOrderTakeMailAction")
        # A landed search with nothing fitting, or a listing over the price
        # cap, is the leg's own dead end; a lost bid is not a search result.
        self.assertEqual(execute.count("AdvanceGuildOrderAfterAuction(ai, bot, *stored, OrderReason::NoListing)"),
            2)
        self.assertEqual(execute.count("AdvanceGuildOrderAfterAuction(ai, bot, *stored, stored->reason)"),
            1)
        self.assertNotIn("AdvanceGuildOrderAfterAuction(ai, bot, *stored);", execute)

    def test_handoff_records_the_leg_reason_on_the_timeline(self):
        advance = body(BUY, "void ai::AdvanceGuildOrderAfterAuction(", "\nbool GuildOrderBuyAction::isUseful(")
        self.assertIn("NoteAuctionLegEnded(order.botGuidLow, reason)", advance)
        self.assertLess(advance.index("NoteAuctionLegEnded("), advance.index("ClearRelistSurplus("))

    def test_ladder_consults_the_reentry_cycle(self):
        choose = body(VALUES, "SupplyStep ChooseSourceStep(", "\nSupplyStep MailReturnStep(")
        self.assertEqual(choose.count("AuctionReentryAllowed("), 2)
        # The forced bank -> auction branch honours the same cycle.
        forced = VALUES[VALUES.index("if (step == SupplyStep::Bank)"):]
        forced = forced[:forced.index("SetSupplyStage(")]
        self.assertIn("AuctionReentryAllowed(order.botGuidLow, candidate.itemId)", forced)

    def test_store_keeps_the_cycle_and_marks_the_ladder_end(self):
        self.assertIn("bool AuctionReentryAllowed(uint32_t botGuidLow, uint32_t itemId)", STORE)
        self.assertIn("void NoteAuctionLegEnded(uint32_t botGuidLow, OrderReason reason)", STORE)
        set_supply = body(STORE, "void SetSupply(uint32_t botGuidLow", "\n    void SetNeedItems(")
        self.assertIn("NoteAuctionLegEntered(", set_supply)
        need = body(STORE, "void SetNeedItems(uint32_t botGuidLow", "\n    void AddNeedItem(")
        self.assertIn("MarkAuctionLadderEndLocked(", need)
        add = body(STORE, "void AddNeedItem(uint32_t botGuidLow", "\n    bool ReplaceNeedItems(")
        self.assertIn("MarkAuctionLadderEndLocked(", add)
        helper = body(STORE, "static void MarkAuctionLadderEndLocked(Stored& stored", "\n    // A zone move advances")
        self.assertIn("NoteAuctionLadderEnd(", helper)

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

    def test_contract_carries_both_auction_reasons(self):
        self.assertIn('case OrderReason::NoAuctioneer: return "noauctioneer";', CONTRACT)
        self.assertIn("case OrderReason::NoAuctioneer:return OrderState::Working;", CONTRACT)
        self.assertIn('case OrderReason::NoListing: return "nolisting";', CONTRACT)
        self.assertIn("case OrderReason::NoListing:return OrderState::Working;", CONTRACT)


if __name__ == "__main__":
    unittest.main()
