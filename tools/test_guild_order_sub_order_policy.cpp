// Standalone test for runtime/GuildOrderSubOrderPolicy.h (guild order
// sub-orders: depth limit, branch loop guard, candidate ranking).
#include "../runtime/GuildOrderSubOrderPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    CHECK(kSubOrderDefaultDepth == 3);
    CHECK(kSubOrderRetryMs == 45000);
    // Branch gold caps nest: the spendable budget is the smallest remaining
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

    // Makers beat farmers; first of equal kind wins.
    {
        SubOrderCandidate both[] = { farmer, maker };
        bool queuedOut = false;
        CHECK(PickSubOrderCandidate(both, 2, queuedOut) == 1);
        CHECK(!queuedOut);
    }
    {
        SubOrderCandidate secondMaker = maker;
        secondMaker.botGuidLow = 7;
        SubOrderCandidate both[] = { maker, secondMaker };
        bool queuedOut = false;
        CHECK(PickSubOrderCandidate(both, 2, queuedOut) == 0);
        CHECK(!queuedOut);
    }
    // Offline and unable candidates are invisible: no candidate at all.
    {
        SubOrderCandidate none[] = { offline, useless };
        bool queuedOut = false;
        CHECK(PickSubOrderCandidate(none, 2, queuedOut) == 2);
        CHECK(!queuedOut);
        CHECK(ClassifySubOrderPick(2, 2, queuedOut) == SubOrderPick::None);
    }
    // Only busy candidates: keep the request queued for a retry.
    {
        SubOrderCandidate onlyBusy[] = { busy, queued };
        bool queuedOut = false;
        size_t picked = PickSubOrderCandidate(onlyBusy, 2, queuedOut);
        CHECK(picked == 2);
        CHECK(queuedOut);
        CHECK(ClassifySubOrderPick(picked, 2, queuedOut) == SubOrderPick::Queued);
    }
    // A free candidate wins over busy ones without queueing.
    {
        SubOrderCandidate mixed[] = { busy, farmer };
        bool queuedOut = false;
        size_t picked = PickSubOrderCandidate(mixed, 2, queuedOut);
        CHECK(picked == 1);
        CHECK(!queuedOut);
        CHECK(ClassifySubOrderPick(picked, 2, queuedOut) == SubOrderPick::Take);
    }
    {
        bool queuedOut = false;
        CHECK(PickSubOrderCandidate(nullptr, 0, queuedOut) == 0);
        CHECK(!queuedOut);
        CHECK(ClassifySubOrderPick(0, 0, false) == SubOrderPick::None);
    }

    if (failures == 0)
        std::printf("guild order sub-order policy: all tests passed\n");
    return failures != 0;
}
