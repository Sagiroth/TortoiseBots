// Standalone test for runtime/GuildOrderSubOrderPolicy.h (guild order
// sub-orders: depth limit, branch loop guard, candidate ranking).
#include "../runtime/GuildOrderSubOrderPolicy.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// One PlaceSubOrders attempt over an ordered roster: look at up to `budget`
// members from where the cursor left off and report who was looked at.
struct ScanAttempt
{
    std::vector<uint32_t> looked;
    SubOrderScanCursor cursor;
};

static ScanAttempt RunScanAttempt(std::vector<uint32_t> const& roster, SubOrderScanCursor cursor,
    size_t budget)
{
    SubOrderScanPlan const plan = PlanSubOrderScan(roster.data(), roster.size(), cursor.lastGuid,
        cursor.startGuid);
    ScanAttempt attempt;
    while (attempt.looked.size() < plan.remaining && attempt.looked.size() < budget)
        attempt.looked.push_back(roster[(plan.begin + attempt.looked.size()) % roster.size()]);
    attempt.cursor = AdvanceSubOrderScan(roster.data(), roster.size(), plan, cursor.lastGuid,
        attempt.looked.size());
    return attempt;
}

static bool WasLookedAt(std::vector<uint32_t> const& looked, uint32_t guid)
{
    return std::find(looked.begin(), looked.end(), guid) != looked.end();
}

int main()
{
    CHECK(kSubOrderDefaultDepth == 3);
    CHECK(kSubOrderRetryMs == 45000);
    // An unfinished scan retries on the next tick, not on the busy timer:
    // waiting 45 s for each slice would stall a branch on a big roster.
    CHECK(kSubOrderSliceRetryMs == 1000);
    CHECK(kSubOrderSliceRetryMs < kSubOrderRetryMs);
    CHECK(kSubOrderBusyScanLimit == 10);
    // The per-tick planner budget is small: sub-order ticks stay bounded
    // while the retry timer drains the rest.
    CHECK(kSubOrderPlannerEvalsPerTick > 0 && kSubOrderPlannerEvalsPerTick <= 16);
    // cap over the row and every ancestor, uncapped only when no row caps.
    CHECK(BranchGoldRemaining(500, true, 200, 500, true, 0) == 300);
    CHECK(BranchGoldRemaining(500, true, 500, 500, true, 0) == 0);
    CHECK(BranchGoldRemaining(0, false, 0, 500, true, 100) == 400);
    CHECK(BranchGoldRemaining(0, false, 0, 0, false, 0) == UINT32_MAX);

    CHECK(SubOrderBranchDepth(1) == 2);
    CHECK(SubOrderBranchDepth(2) == 3);
    CHECK(SubOrderBranchDepth(3) == 4);
    CHECK(SubOrderDepthAllows(1, 3));
    CHECK(SubOrderDepthAllows(2, 3));
    CHECK(!SubOrderDepthAllows(3, 3));
    CHECK(!SubOrderDepthAllows(1, 0));

    uint32_t branch[] = { 100, 200, 300 };
    CHECK(SubOrderLoopFree(400, branch, 3));
    CHECK(!SubOrderLoopFree(100, branch, 3));
    CHECK(!SubOrderLoopFree(200, branch, 3));
    CHECK(!SubOrderLoopFree(300, branch, 3));
    CHECK(SubOrderLoopFree(200, branch, 1));
    CHECK(!SubOrderLoopFree(0, branch, 3));
    CHECK(SubOrderLoopFree(400, nullptr, 0));
    // Parallel siblings only guard their own item: requesting another item
    // under the same parent is free, re-requesting the same one is not.
    CHECK(SubOrderLoopFree(500, branch, 3));
    CHECK(!SubOrderLoopFree(100, branch, 3));

    SubOrderCandidate maker{};
    maker.botGuidLow = 1;
    maker.online = true;
    maker.canMake = true;
    SubOrderCandidate farmer{};
    farmer.botGuidLow = 2;
    farmer.online = true;
    farmer.canFarm = true;
    SubOrderCandidate offline{};
    offline.botGuidLow = 3;
    SubOrderCandidate busy{};
    busy.botGuidLow = 4;
    busy.online = true;
    busy.hasOrder = true;
    busy.canMake = true;
    SubOrderCandidate queued{};
    queued.botGuidLow = 5;
    queued.online = true;
    queued.queuedBusy = true;
    queued.canFarm = true;
    SubOrderCandidate useless{};
    useless.botGuidLow = 6;
    useless.online = true;

    CHECK(SubOrderCandidateRank(maker) == 2);
    CHECK(SubOrderCandidateRank(farmer) == 1);
    CHECK(SubOrderCandidateRank(offline) < 0);
    CHECK(SubOrderCandidateRank(busy) < 0);
    CHECK(SubOrderCandidateRank(queued) < 0);
    CHECK(SubOrderCandidateRank(useless) < 0);

    // Fold the candidates of one slice into the state carried across the
    // scan, the way PlaceSubOrders does between attempts.
    auto foldSlice = [](SubOrderScanBest best, SubOrderCandidate const* candidates, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            NoteSubOrderCandidate(best, candidates[i]);
        return best;
    };

    // Makers beat farmers; first of equal kind wins.
    {
        SubOrderCandidate both[] = { farmer, maker };
        SubOrderScanBest best = foldSlice(SubOrderScanBest{}, both, 2);
        CHECK(best.guid == maker.botGuidLow);
        CHECK(best.canMake);
        CHECK(ClassifySubOrderScan(best) == SubOrderPick::Take);
    }
    {
        SubOrderCandidate secondMaker = maker;
        secondMaker.botGuidLow = 7;
        SubOrderCandidate both[] = { maker, secondMaker };
        SubOrderScanBest best = foldSlice(SubOrderScanBest{}, both, 2);
        CHECK(best.guid == maker.botGuidLow);
    }
    // A gatherer beats a maker for a raw material.
    {
        SubOrderCandidate gatherer = farmer;
        gatherer.botGuidLow = 8;
        gatherer.gathers = true;
        CHECK(SubOrderCandidateRank(gatherer) == 3);
        SubOrderCandidate both[] = { maker, gatherer };
        SubOrderScanBest best = foldSlice(SubOrderScanBest{}, both, 2);
        CHECK(best.guid == gatherer.botGuidLow);
        CHECK(!best.canMake);
    }
    // Offline and unable candidates are invisible: no candidate at all.
    {
        SubOrderCandidate none[] = { offline, useless };
        CHECK(ClassifySubOrderScan(foldSlice(SubOrderScanBest{}, none, 2)) == SubOrderPick::None);
    }
    // Only busy candidates: keep the request queued for a retry.
    {
        SubOrderCandidate onlyBusy[] = { busy, queued };
        CHECK(ClassifySubOrderScan(foldSlice(SubOrderScanBest{}, onlyBusy, 2)) ==
            SubOrderPick::Queued);
    }
    // A keep holder parked at its stock target never frees up: it is busy,
    // but it cannot take the sub-order, so it must not keep the request
    // alive. A normal busy bot next to it still does.
    {
        SubOrderCandidate keepHolder = busy;
        keepHolder.botGuidLow = 9;
        keepHolder.keepHolder = true;
        SubOrderCandidate onlyKeep[] = { keepHolder };
        SubOrderScanBest keepBest = foldSlice(SubOrderScanBest{}, onlyKeep, 1);
        CHECK(!keepBest.sawBusyUsable);
        CHECK(ClassifySubOrderScan(keepBest) == SubOrderPick::None);
        SubOrderCandidate keepAndBusy[] = { keepHolder, busy };
        SubOrderScanBest mixedBest = foldSlice(SubOrderScanBest{}, keepAndBusy, 2);
        CHECK(mixedBest.sawBusyUsable);
        CHECK(ClassifySubOrderScan(mixedBest) == SubOrderPick::Queued);
    }
    // A busy-only roster gives up after the limit instead of rescanning
    // forever: the item lands on the parent's need list.
    {
        SubOrderCandidate onlyBusy[] = { busy };
        SubOrderScanBest best = foldSlice(SubOrderScanBest{}, onlyBusy, 1);
        CHECK(ClassifySubOrderScan(best, kSubOrderBusyScanLimit - 1) == SubOrderPick::Queued);
        CHECK(ClassifySubOrderScan(best, kSubOrderBusyScanLimit) == SubOrderPick::None);
        // A free candidate still wins on the last allowed retry.
        SubOrderCandidate freeOne[] = { farmer };
        SubOrderScanBest freeBest = foldSlice(SubOrderScanBest{}, freeOne, 1);
        CHECK(ClassifySubOrderScan(freeBest, kSubOrderBusyScanLimit) == SubOrderPick::Take);
    }
    // A free candidate wins over busy ones without queueing.
    {
        SubOrderCandidate mixed[] = { busy, farmer };
        SubOrderScanBest best = foldSlice(SubOrderScanBest{}, mixed, 2);
        CHECK(best.guid == farmer.botGuidLow);
        CHECK(ClassifySubOrderScan(best) == SubOrderPick::Take);
    }

    // The carried candidate is re-checked just before placement: a member
    // that went offline, left the guild, lost its durable pool or picked up
    // an order while the scan ran must not have the order placed over it.
    {
        SubOrderRecheck ok;
        ok.found = true;
        ok.inWorld = true;
        ok.sameGuild = true;
        ok.durable = true;
        CHECK(SubOrderRecheckPasses(ok));
        SubOrderRecheck gone;
        gone.found = true;
        gone.sameGuild = true;
        gone.durable = true;
        CHECK(!SubOrderRecheckPasses(gone)); // not in world
        SubOrderRecheck otherGuild = ok;
        otherGuild.sameGuild = false;
        CHECK(!SubOrderRecheckPasses(otherGuild));
        SubOrderRecheck notDurable = ok;
        notDurable.durable = false;
        CHECK(!SubOrderRecheckPasses(notDurable));
        SubOrderRecheck busyNow = ok;
        busyNow.hasOrder = true;
        CHECK(!SubOrderRecheckPasses(busyNow));
        CHECK(!SubOrderRecheckPasses(SubOrderRecheck{}));
    }

    // The roster is looked at in slices: what an early slice learned decides
    // the request when the last slice holds nothing usable. A busy bot that
    // could still make the item keeps the request queued instead of failing
    // it, so its verdict must survive into the slices after it.
    {
        SubOrderCandidate sliceOne[] = { busy };
        SubOrderCandidate lastSlice[] = { offline, useless };
        SubOrderScanBest best = foldSlice(SubOrderScanBest{}, sliceOne, 1);
        best = foldSlice(best, lastSlice, 2);
        CHECK(best.guid == 0);
        CHECK(best.sawBusyUsable);
        CHECK(ClassifySubOrderScan(best) == SubOrderPick::Queued);
    }
    // A farmer in the first slice does not win by arriving first: a maker in
    // a later slice outranks it, and the pick is placed at scan completion.
    {
        SubOrderCandidate sliceOne[] = { farmer };
        SubOrderCandidate lastSlice[] = { offline, maker };
        SubOrderScanBest best = foldSlice(SubOrderScanBest{}, sliceOne, 1);
        CHECK(best.guid == farmer.botGuidLow);
        CHECK(ClassifySubOrderScan(best) == SubOrderPick::Take);
        best = foldSlice(best, lastSlice, 2);
        CHECK(best.guid == maker.botGuidLow);
        CHECK(best.canMake);
        CHECK(ClassifySubOrderScan(best) == SubOrderPick::Take);
    }
    // Nothing usable anywhere in the roster fails the request.
    {
        SubOrderCandidate sliceOne[] = { offline, useless };
        SubOrderCandidate lastSlice[] = { useless };
        SubOrderScanBest best = foldSlice(SubOrderScanBest{}, sliceOne, 2);
        best = foldSlice(best, lastSlice, 1);
        CHECK(ClassifySubOrderScan(best) == SubOrderPick::None);
    }
    // A fresh request is due at once whatever the world uptime: the signed
    // difference wraps past 2^31 ms and would park it for the session.
    CHECK(SubOrderRequestDue(0, 0));
    CHECK(SubOrderRequestDue(0, 1234567));
    CHECK(SubOrderRequestDue(0, 0x80000000u));
    CHECK(SubOrderRequestDue(0, 0xFFFFFFFFu));
    CHECK(SubOrderRequestDue(5000, 5000));
    CHECK(!SubOrderRequestDue(5000, 4999));
    CHECK(!SubOrderRequestDue(5000, 4000));
    CHECK(SubOrderRequestDue(450000, 495000));

    // Two requests due together: the drain starts with the first key, and
    // the next tick starts after the request that spent the budget, so the
    // second one gets its evaluations instead of starving behind the first.
    {
        uint32_t parents[] = { 10, 10 };
        uint32_t items[] = { 100, 200 };
        CHECK(SubOrderDrainOffset(parents, items, 2, 0, 0) == 0);
        CHECK(SubOrderDrainOffset(parents, items, 2, 10, 100) == 1);
        CHECK(SubOrderDrainOffset(parents, items, 2, 10, 200) == 0);
        // A drained key that is no longer due does not park the rotation.
        CHECK(SubOrderDrainOffset(parents, items, 2, 5, 0) == 0);
        CHECK(SubOrderDrainOffset(parents, items, 2, 20, 0) == 0);
        CHECK(SubOrderDrainOffset(nullptr, nullptr, 0, 0, 0) == 0);
    }
    // With a one-evaluation budget, the second request still progresses: the
    // tick after the first request worked starts at the other one.
    {
        uint32_t parents[] = { 10, 20 };
        uint32_t items[] = { 100, 200 };
        bool progressed[2] = { false, false };
        uint32_t lastParent = 0;
        uint32_t lastItem = 0;
        for (int tick = 0; tick < 2; ++tick)
        {
            size_t const begin = SubOrderDrainOffset(parents, items, 2, lastParent, lastItem);
            progressed[begin] = true;
            lastParent = parents[begin];
            lastItem = items[begin];
        }
        CHECK(progressed[0]);
        CHECK(progressed[1]);
    }

    // The member scan is a guid walk over the ordered roster: over retries it
    // looks at each member once, in order, and only finishes back at its
    // start member. An index would land on another member as soon as the
    // roster shifts between attempts.
    {
        std::vector<uint32_t> roster = { 100, 200, 300, 400, 500 };
        SubOrderScanCursor cursor;
        std::vector<uint32_t> seen;
        for (int attempt = 0; attempt < 8 && !cursor.complete; ++attempt)
        {
            ScanAttempt step = RunScanAttempt(roster, cursor, 2);
            seen.insert(seen.end(), step.looked.begin(), step.looked.end());
            cursor = step.cursor;
        }
        CHECK(cursor.complete);
        CHECK(seen.size() == roster.size());
        for (size_t i = 0; i < seen.size(); ++i)
            CHECK(seen[i] == roster[i]);
        CHECK(cursor.startGuid == 100);
    }

    // Members joining and leaving between attempts: a member that joins
    // below the scan start is picked up at the end of the walk, one that
    // leaves is simply never looked at, and the scan cannot report itself
    // finished while a member the roster still holds was never looked at.
    {
        std::vector<uint32_t> roster = { 100, 200, 300, 400, 500 };
        SubOrderScanCursor cursor;
        ScanAttempt first = RunScanAttempt(roster, cursor, 2);
        CHECK(!first.cursor.complete);
        std::vector<uint32_t> seen = first.looked;
        cursor = first.cursor;
        // 300 leaves, 50 joins below the cursor, and the rows come back
        // reordered (the query orders them again).
        roster = { 400, 50, 500, 200, 100 };
        std::sort(roster.begin(), roster.end());
        for (int attempt = 0; attempt < 8 && !cursor.complete; ++attempt)
        {
            ScanAttempt step = RunScanAttempt(roster, cursor, 2);
            seen.insert(seen.end(), step.looked.begin(), step.looked.end());
            cursor = step.cursor;
        }
        CHECK(cursor.complete);
        CHECK(cursor.startGuid == 100);
        for (uint32_t guid : roster)
            CHECK(WasLookedAt(seen, guid));
        CHECK(!WasLookedAt(seen, 300));
    }

    // The member the scan started at leaves mid-scan: the walk is bounded
    // again instead of re-reading the same tail on every attempt.
    {
        std::vector<uint32_t> roster = { 100, 200, 300, 400 };
        SubOrderScanCursor cursor;
        cursor = RunScanAttempt(roster, cursor, 2).cursor;
        CHECK(!cursor.complete);
        roster = { 200, 300, 400 };
        int attempts = 0;
        while (!cursor.complete && attempts < 8)
        {
            cursor = RunScanAttempt(roster, cursor, 1).cursor;
            ++attempts;
        }
        CHECK(cursor.complete);
        CHECK(attempts < 8);
    }

    // Nothing looked at (the planner budget was already gone): the cursor
    // stays put and the scan is not reported finished.
    {
        std::vector<uint32_t> roster = { 10, 20 };
        SubOrderScanPlan plan = PlanSubOrderScan(roster.data(), roster.size(), 0, 0);
        CHECK(!plan.empty);
        CHECK(plan.remaining == 2);
        SubOrderScanCursor cursor = AdvanceSubOrderScan(roster.data(), roster.size(), plan, 0, 0);
        CHECK(!cursor.complete);
        CHECK(cursor.lastGuid == 0);
        CHECK(cursor.startGuid == 10);
    }

    // An empty roster has nothing to look at and nothing to retry.
    {
        SubOrderScanPlan plan = PlanSubOrderScan(nullptr, 0, 0, 0);
        CHECK(plan.empty);
        CHECK(plan.remaining == 0);
        SubOrderScanCursor cursor = AdvanceSubOrderScan(nullptr, 0, plan, 0, 0);
        CHECK(cursor.complete);
    }

    // Parallel pick: first missing item with no running or queued cover.
    {
        uint32_t missing[] = { 100, 200, 300 };
        CHECK(NextUncoveredSubOrderItem(missing, 3, nullptr, 0) == 0);
        uint32_t coveredNone[] = { 400 };
        CHECK(NextUncoveredSubOrderItem(missing, 3, coveredNone, 1) == 0);
        uint32_t coveredFirst[] = { 100 };
        CHECK(NextUncoveredSubOrderItem(missing, 3, coveredFirst, 1) == 1);
        uint32_t coveredAll[] = { 100, 200, 300 };
        CHECK(NextUncoveredSubOrderItem(missing, 3, coveredAll, 3) == 3);
        uint32_t coveredMiddle[] = { 200 };
        CHECK(NextUncoveredSubOrderItem(missing, 3, coveredMiddle, 1) == 0);
        uint32_t missingSkip[] = { 0, 200 };
        CHECK(NextUncoveredSubOrderItem(missingSkip, 2, nullptr, 0) == 1);
        CHECK(NextUncoveredSubOrderItem(nullptr, 0, coveredAll, 3) == 0);
    }


    if (failures == 0)
        std::printf("guild order sub-order policy: all tests passed\n");
    return failures != 0;
}
