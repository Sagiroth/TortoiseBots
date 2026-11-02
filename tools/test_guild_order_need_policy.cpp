// Standalone test for runtime/GuildOrderNeedPolicy.h: need-list recovery.
#include "../runtime/GuildOrderNeedPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // The slow lane waits a full interval between checks.
    CHECK(NeedLadderCheckDue(600000, 0));
    CHECK(NeedLadderCheckDue(kNeedLadderCheckMs, 0));
    CHECK(!NeedLadderCheckDue(kNeedLadderCheckMs - 1, 0));
    CHECK(!NeedLadderCheckDue(1000, 900));
    // Vendor first (the deployed-later rule), then bank, then AH.
    CHECK(NeedResumeLegFor(true, true, true) == NeedResumeLeg::Vendor);
    CHECK(NeedResumeLegFor(false, true, true) == NeedResumeLeg::Bank);
    CHECK(NeedResumeLegFor(false, false, true) == NeedResumeLeg::AuctionHouse);
    CHECK(NeedResumeLegFor(false, false, false) == NeedResumeLeg::None);
    // A long park re-runs the whole ladder, after the cheaper legs.
    CHECK(NeedResumeLegFor(false, false, false, true) == NeedResumeLeg::Ladder);
    CHECK(NeedResumeLegFor(false, true, false, true) == NeedResumeLeg::Bank);
    CHECK(!NeedFullRetryDue(0));
    CHECK(!NeedFullRetryDue(1));
    CHECK(!NeedFullRetryDue(2));
    CHECK(NeedFullRetryDue(3));
    CHECK(NeedFullRetryDue(6));

    // Partial need: a craft order keeps sourcing while one item waits on the player.
    std::vector<NeedItem> listed{ { 2319, 4 } };
    CHECK(NeedListHas(listed, 2319));
    CHECK(!NeedListHas(listed, 2320));
    CHECK(!NeedListHas(listed, 0));
    std::vector<NeedItem> zeroListed{ { 2320, 0 } };
    CHECK(!NeedListHas(zeroListed, 2320));

    // A known id keeps the larger count; a new one goes to the back.
    std::vector<NeedItem> merged;
    MergeNeedItem(merged, 2319, 4);
    CHECK(merged.size() == 1 && merged[0].itemId == 2319 && merged[0].count == 4);
    MergeNeedItem(merged, 2319, 2);
    CHECK(merged.size() == 1 && merged[0].count == 4);
    MergeNeedItem(merged, 2319, 6);
    CHECK(merged.size() == 1 && merged[0].count == 6);
    MergeNeedItem(merged, 2320, 1);
    CHECK(merged.size() == 2 && merged[0].itemId == 2319 && merged[1].itemId == 2320);
    MergeNeedItem(merged, 0, 5);
    MergeNeedItem(merged, 2321, 0);
    CHECK(merged.size() == 2);

    // The whole-tree count wins; the list keeps its order.
    std::vector<NeedItem> pruned{ { 2319, 4 }, { 2320, 2 } };
    std::vector<NeedItem> missingNow{ { 2320, 3 } };
    PruneNeedItems(pruned, missingNow);
    CHECK(pruned.size() == 1 && pruned[0].itemId == 2320 && pruned[0].count == 3);
    pruned = { { 2319, 4 }, { 2320, 2 } };
    PruneNeedItems(pruned, std::vector<NeedItem>{});
    CHECK(pruned.empty());
    pruned = { { 2319, 4 }, { 2320, 2 } };
    missingNow = { { 2320, 1 }, { 2320, 5 }, { 2319, 4 } };
    PruneNeedItems(pruned, missingNow);
    CHECK(pruned.size() == 2 && pruned[0].itemId == 2319 && pruned[0].count == 4 &&
        pruned[1].itemId == 2320 && pruned[1].count == 5);

    // Supply wins while anything is still supplyable; a running sub-order is work, not a park.
    CHECK(PartialNeedVerdictFor(true, true, true) == PartialNeedVerdict::Supply);
    CHECK(PartialNeedVerdictFor(true, false, true) == PartialNeedVerdict::Supply);
    CHECK(PartialNeedVerdictFor(true, true, false) == PartialNeedVerdict::Supply);
    CHECK(PartialNeedVerdictFor(true, false, false) == PartialNeedVerdict::Supply);
    CHECK(PartialNeedVerdictFor(false, true, true) == PartialNeedVerdict::WaitOnSubOrders);
    CHECK(PartialNeedVerdictFor(false, true, false) == PartialNeedVerdict::WaitOnSubOrders);
    CHECK(PartialNeedVerdictFor(false, false, true) == PartialNeedVerdict::Park);
    CHECK(PartialNeedVerdictFor(false, false, false) == PartialNeedVerdict::Supply);

    // The clock pauses only for a parent with no leg of its own waiting on an
    // unfinished child: a done child (nothing left to wait for) does not, and
    // an own leg keeps every clock (the farm give-up and the zone move too).
    CHECK(PauseOrderClocks(SupplyStep::None, 1));
    CHECK(PauseOrderClocks(SupplyStep::None, 3));
    CHECK(!PauseOrderClocks(SupplyStep::None, 0));
    CHECK(!PauseOrderClocks(SupplyStep::None, -1));
    CHECK(!PauseOrderClocks(SupplyStep::Farm, 1));
    CHECK(!PauseOrderClocks(SupplyStep::Bank, 2));
    CHECK(!PauseOrderClocks(SupplyStep::AuctionHouse, 1));
    CHECK(!PauseOrderClocks(SupplyStep::Craft, 1));
    CHECK(PauseOrderClocks(SupplyStep::Sub, 1));
    CHECK(!PauseOrderClocks(SupplyStep::Sub, 0));
    CHECK(!PauseOrderClocks(SupplyStep::Need, 1));

    // The whisper fires as soon as a list exists, once per list.
    CHECK(NeedWhisperDue(OrderState::Working, OrderReason::None, true, false));
    CHECK(NeedWhisperDue(OrderState::Waiting, OrderReason::NeedItems, true, false));
    CHECK(!NeedWhisperDue(OrderState::Waiting, OrderReason::Grouped, true, false));
    CHECK(!NeedWhisperDue(OrderState::Working, OrderReason::None, true, true));
    CHECK(!NeedWhisperDue(OrderState::Working, OrderReason::None, false, false));
    CHECK(!NeedWhisperDue(OrderState::Delivering, OrderReason::None, true, false));

    // The need lanes run for parked rows and for working rows with a list (or a Sub leg).
    CHECK(NeedBankLaneEligible(OrderState::Waiting, OrderReason::NeedItems, SupplyStep::Need, true));
    CHECK(NeedLadderLaneEligible(OrderState::Waiting, OrderReason::NeedItems, true));
    CHECK(NeedBankLaneEligible(OrderState::Working, OrderReason::None, SupplyStep::Sub, false));
    CHECK(!NeedLadderLaneEligible(OrderState::Working, OrderReason::None, false));
    CHECK(NeedBankLaneEligible(OrderState::Working, OrderReason::None, SupplyStep::Farm, true));
    CHECK(NeedLadderLaneEligible(OrderState::Working, OrderReason::None, true));
    CHECK(!NeedBankLaneEligible(OrderState::Working, OrderReason::None, SupplyStep::Farm, false));
    CHECK(!NeedLadderLaneEligible(OrderState::Working, OrderReason::None, false));
    CHECK(!NeedBankLaneEligible(OrderState::Waiting, OrderReason::Grouped, SupplyStep::None, true));
    CHECK(!NeedLadderLaneEligible(OrderState::Waiting, OrderReason::Grouped, true));

    // Materials fold: a missing reagent first, then a craftable one, then a blocked one.
    MaterialOutcome mixed[] = { MaterialOutcome::Owned, MaterialOutcome::Craftable, MaterialOutcome::Missing };
    MaterialFold fold = FoldMaterials(mixed, 3);
    CHECK(fold.outcome == MaterialOutcome::Missing && fold.index == 2);
    MaterialOutcome craftBlocked[] = { MaterialOutcome::Craftable, MaterialOutcome::Blocked };
    fold = FoldMaterials(craftBlocked, 2);
    CHECK(fold.outcome == MaterialOutcome::Craftable && fold.index == 0);
    MaterialOutcome blockOwned[] = { MaterialOutcome::Blocked, MaterialOutcome::Owned };
    fold = FoldMaterials(blockOwned, 2);
    CHECK(fold.outcome == MaterialOutcome::Blocked && fold.index == 2);
    MaterialOutcome ownOwn[] = { MaterialOutcome::Owned, MaterialOutcome::Owned };
    fold = FoldMaterials(ownOwn, 2);
    CHECK(fold.outcome == MaterialOutcome::Owned && fold.index == 2);
    fold = FoldMaterials(nullptr, 0);
    CHECK(fold.outcome == MaterialOutcome::Owned && fold.index == 0);
    MaterialOutcome blockMissing[] = { MaterialOutcome::Blocked, MaterialOutcome::Missing };
    fold = FoldMaterials(blockMissing, 2);
    CHECK(fold.outcome == MaterialOutcome::Missing && fold.index == 1);

    // The whisper covers a list once: a new item id or a higher count than
    // the last whispered token is news, everything else stays silent.
    CHECK(NeedListNeedsWhisper("-", std::vector<NeedItem>{ { 2319, 4 } }));
    CHECK(NeedListNeedsWhisper("", std::vector<NeedItem>{ { 2319, 4 } }));
    CHECK(!NeedListNeedsWhisper("2319:4", std::vector<NeedItem>{ { 2319, 4 } }));
    CHECK(NeedListNeedsWhisper("2319:4", std::vector<NeedItem>{ { 2319, 4 }, { 2320, 2 } }));
    CHECK(NeedListNeedsWhisper("2319:4;2320:2", std::vector<NeedItem>{ { 2319, 6 } }));
    CHECK(!NeedListNeedsWhisper("2319:4;2320:2", std::vector<NeedItem>{ { 2319, 4 } }));
    CHECK(!NeedListNeedsWhisper("2319:4;2320:2", std::vector<NeedItem>{ { 2320, 1 }, { 2319, 2 } }));
    CHECK(NeedListNeedsWhisper("2319:4", std::vector<NeedItem>{ { 2320, 2 }, { 2319, 2 } }));
    CHECK(!NeedListNeedsWhisper("2319:4", std::vector<NeedItem>{}));
    // A list rebuilt one item at a time grows the token the same way the
    // sends did, so the rebuild never whispers a second time.
    std::string whispered = NeedWhisperTokenUnion("-", std::vector<NeedItem>{ { 2319, 4 } });
    CHECK(whispered == "2319:4");
    whispered = NeedWhisperTokenUnion(whispered, std::vector<NeedItem>{ { 2319, 4 }, { 2320, 2 } });
    CHECK(whispered == "2319:4;2320:2");
    CHECK(!NeedListNeedsWhisper(whispered, std::vector<NeedItem>{ { 2319, 4 } }));
    CHECK(!NeedListNeedsWhisper(whispered, std::vector<NeedItem>{ { 2319, 4 }, { 2320, 2 } }));
    CHECK(NeedListNeedsWhisper(whispered, std::vector<NeedItem>{ { 2320, 3 } }));
    whispered = NeedWhisperTokenUnion(whispered, std::vector<NeedItem>{ { 2320, 3 } });
    CHECK(whispered == "2319:4;2320:3");

    // A dead-end verdict does not survive the leg that produced it.
    CHECK(ResetDeadEndReasonOnWorking(OrderReason::NoSource) == OrderReason::None);
    CHECK(ResetDeadEndReasonOnWorking(OrderReason::BankDenied) == OrderReason::None);
    CHECK(ResetDeadEndReasonOnWorking(OrderReason::NoBanker) == OrderReason::None);
    CHECK(ResetDeadEndReasonOnWorking(OrderReason::NoRecipe) == OrderReason::NoRecipe);
    CHECK(ResetDeadEndReasonOnWorking(OrderReason::None) == OrderReason::None);

    // A cleared need list keeps the leg on some other item, resets it when
    // the leg sources a listed item (or there is no leg at all).
    CHECK(KeepLegOnNeedClear(2319, std::vector<NeedItem>{ { 2320, 1 } }));
    CHECK(!KeepLegOnNeedClear(2319, std::vector<NeedItem>{ { 2319, 1 } }));
    CHECK(!KeepLegOnNeedClear(0, std::vector<NeedItem>{ { 2319, 1 } }));
    CHECK(KeepLegOnNeedClear(2319, std::vector<NeedItem>{}));

    // Compare-and-set needs the exact list, entries and counts.
    CHECK(NeedItemsEqual(std::vector<NeedItem>{ { 2319, 4 }, { 2320, 2 } },
        std::vector<NeedItem>{ { 2319, 4 }, { 2320, 2 } }));
    CHECK(NeedItemsEqual(std::vector<NeedItem>{}, std::vector<NeedItem>{}));
    CHECK(!NeedItemsEqual(std::vector<NeedItem>{ { 2319, 4 } },
        std::vector<NeedItem>{ { 2319, 4 }, { 2320, 2 } }));
    CHECK(!NeedItemsEqual(std::vector<NeedItem>{ { 2319, 4 } }, std::vector<NeedItem>{ { 2319, 5 } }));
    CHECK(!NeedItemsEqual(std::vector<NeedItem>{ { 2319, 4 } }, std::vector<NeedItem>{ { 2320, 4 } }));

    if (failures == 0)
        std::printf("guild order need policy: all tests passed\n");
    return failures != 0;
}
