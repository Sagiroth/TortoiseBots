// Standalone test for runtime/GuildOrderGiveUpPolicy.h (guild order zone moves,
// the player-chosen farm time and the progress watchdog). No core, no DB:
// feeds the policy plain counters.
#include "../runtime/GuildOrderGiveUpPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    CHECK(kZoneMoveWorkedSeconds == 3600);
    CHECK(kZoneMoveDeaths == 3);
    CHECK(kMinOrderHours == 1);
    CHECK(kMaxOrderHours == 24);
    CHECK(kDefaultOrderHours == 0);
    CHECK(kUnlimitedOrderHours == 0);

    // Below both thresholds: keep working in this zone.
    CHECK(ClassifyZoneMove(0, 0) == ZoneMoveTrigger::None);
    CHECK(ClassifyZoneMove(3599, 2) == ZoneMoveTrigger::None);

    // Three deaths in the zone move, even with fresh progress.
    CHECK(ClassifyZoneMove(0, 3) == ZoneMoveTrigger::Deaths);
    CHECK(ClassifyZoneMove(3599, 3) == ZoneMoveTrigger::Deaths);
    // The pick wraps around the plan: 0 -> 1 -> 2 -> 0.
    CHECK(NextZonePick(false, 0, 0, 3) == 0);
    CHECK(NextZonePick(false, 0, 1, 3) == 1);
    CHECK(NextZonePick(false, 0, 2, 3) == 2);
    CHECK(NextZonePick(false, 0, 3, 3) == 0);
    CHECK(NextZonePick(false, 0, 4, 3) == 1);
    // A listed zone moves to the one after it; a single-zone plan stays.
    CHECK(NextZonePick(true, 0, 0, 3) == 1);
    CHECK(NextZonePick(true, 2, 0, 3) == 0);
    CHECK(NextZonePick(false, 0, 7, 1) == 0);
    // An empty plan picks nothing: the caller ends the order as no source.
    CHECK(NextZonePick(false, 0, 0, 0) == 0);
    CHECK(ClassifyZoneMove(3600, 0) == ZoneMoveTrigger::NoProgress);
    CHECK(ClassifyZoneMove(7200, 2) == ZoneMoveTrigger::NoProgress);

    // Deaths win when both thresholds hit together.
    // Failed order-leg searches move the zone after three in a row: a bad
    // zone costs short parks, not the hour the death rules would take.
    CHECK(kSearchFailZoneMove == 3);
    CHECK(kOrderLegParkSeconds == 15);
    CHECK(!SearchFailsMoveZone(0));
    CHECK(!SearchFailsMoveZone(1));
    CHECK(!SearchFailsMoveZone(2));
    CHECK(SearchFailsMoveZone(3));
    CHECK(SearchFailsMoveZone(4));

    // A craft reagent farm hands off after 20 min; the full hour only when
    // the hand-off is disabled. The ladder below always offers a sub-order
    // or the need list.
    CHECK(kSupplyFarmNoGainSeconds == 3600);
    CHECK(kSupplyFarmNoGainWithFallbackSeconds == 20 * 60);
    CHECK(!SupplyFarmGivesUp(true, 20 * 60 - 1));
    CHECK(SupplyFarmGivesUp(true, 20 * 60));
    CHECK(!SupplyFarmGivesUp(false, 7200));
    CHECK(!SupplyFarmGivesUp(true, 3599, false));
    CHECK(SupplyFarmGivesUp(true, 3600, false));
    CHECK(!SupplyFarmGivesUp(false, 7200, false));

    // Search-forced zone moves give up after a bounded number of zones, not
    // the full plan length: a 30-zone plan costs 8 moves, not ~1.5 h.
    CHECK(kSearchZoneMovesExhaustPlan == 8);
    CHECK(!SearchZoneMovesExhaustPlan(0, 1));
    CHECK(!SearchZoneMovesExhaustPlan(7, 30));
    CHECK(SearchZoneMovesExhaustPlan(8, 30));
    CHECK(SearchZoneMovesExhaustPlan(8, 1));
    CHECK(SearchZoneMovesExhaustPlan(1, 1));
    CHECK(SearchZoneMovesExhaustPlan(3, 3));
    CHECK(!SearchZoneMovesExhaustPlan(2, 3));
    CHECK(SearchZoneMovesExhaustPlan(1, 0));
    CHECK(SearchZoneMovesExhaustPlan(9, 30));

    // Arrived auction visits without an auctioneer in reach move the leg
    // down at the configured limit: the search layer keeps offering the
    // next city, so the count lives on the leg, not the pick streak.
    // Zero disables the exit.
    CHECK(kAuctionVisitsWithoutAuctioneer == 3);
    CHECK(!AuctionVisitsMoveLadder(0, 3));
    CHECK(!AuctionVisitsMoveLadder(2, 3));
    CHECK(AuctionVisitsMoveLadder(3, 3));
    CHECK(AuctionVisitsMoveLadder(9, 3));
    CHECK(!AuctionVisitsMoveLadder(99, 0));
    CHECK(ClassifyAuctionArrival(true, 0, 3) == AuctionArrival::Reset);
    CHECK(ClassifyAuctionArrival(true, 9, 3) == AuctionArrival::Reset);
    CHECK(ClassifyAuctionArrival(true, 0, 0) == AuctionArrival::Reset);
    // Any other arrival counts, exiting once the count would hit the limit.
    CHECK(ClassifyAuctionArrival(false, 0, 3) == AuctionArrival::Count);
    CHECK(ClassifyAuctionArrival(false, 1, 3) == AuctionArrival::Count);
    CHECK(ClassifyAuctionArrival(false, 2, 3) == AuctionArrival::CountAndExit);
    CHECK(ClassifyAuctionArrival(false, 9, 3) == AuctionArrival::CountAndExit);
    // Zero disables the exit: arrivals keep counting, never exiting.
    CHECK(ClassifyAuctionArrival(false, 0, 0) == AuctionArrival::Count);
    CHECK(ClassifyAuctionArrival(false, 99, 0) == AuctionArrival::Count);
    CHECK(!IsOrderTimeout(3599, 1));
    CHECK(IsOrderTimeout(3600, 1));
    CHECK(!IsOrderTimeout(3600, 2));
    CHECK(IsOrderTimeout(2 * 3600, 2));
    CHECK(IsOrderTimeout(24 * 3600, 24));
    CHECK(!IsOrderTimeout(23 * 3600 + 3599, 24));
    // Out-of-range hours fall back to the default (no limit); unlimited never times out.
    CHECK(!IsOrderTimeout(3600, 25));
    CHECK(!IsOrderTimeout(99 * 3600, 99));
    CHECK(!IsOrderTimeout(0, kUnlimitedOrderHours));
    CHECK(!IsOrderTimeout(24 * 3600, kUnlimitedOrderHours));
    CHECK(!IsOrderTimeout(0xFFFFFFFFu, kUnlimitedOrderHours));

    // The progress watchdog ends orders that gain nothing, capped or not.
    CHECK(kNoGainWorkedSeconds == 3 * 3600);
    CHECK(!IsOrderNoProgress(0));
    CHECK(!IsOrderNoProgress(3 * 3600 - 1));
    CHECK(IsOrderNoProgress(3 * 3600));
    CHECK(IsOrderNoProgress(0xFFFFFFFFu));

    // The row's have count is delivered only and never drops; the bags and
    // mailbox travel in a separate carry count that may fall on deposit.
    CHECK(ReportedHave(0, 0) == 0);
    CHECK(ReportedHave(0, 5) == 5);
    CHECK(ReportedHave(4, 0) == 4);
    CHECK(ReportedHave(4, 7) == 7);
    CHECK(ReportedHave(9, 7) == 9);
    CHECK(ReportedCarry(true, true, 4, 2, 0) == 6);
    CHECK(ReportedCarry(true, true, 0, 0, 9) == 0);
    CHECK(ReportedCarry(true, false, 4, 2, 6) == 6);
    CHECK(ReportedCarry(false, true, 4, 2, 6) == 6);
    CHECK(ReportedCarry(false, false, 0, 0, 6) == 6);

    // Only a real online rise resets the zone-move clock.
    CHECK(IsNewItemRise(true, true, 6, 5));
    CHECK(!IsNewItemRise(true, true, 5, 5));
    CHECK(!IsNewItemRise(true, true, 4, 5));
    CHECK(!IsNewItemRise(true, false, 6, 0));
    CHECK(!IsNewItemRise(false, true, 6, 5));
    if (failures == 0)
        std::printf("test_guild_order_giveup_policy ok\n");
    return failures ? 1 : 0;
}
