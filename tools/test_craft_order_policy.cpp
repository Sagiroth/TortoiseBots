// Standalone test for the craft-order arithmetic
// (runtime/policy/provision/CraftOrderPolicy.h): shortfall, guaranteed yield,
// cast counts and the bag-room gate.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_craft_order_policy.cpp -o /tmp/test_craft_order_policy
//   /tmp/test_craft_order_policy

#include "../runtime/policy/provision/CraftOrderPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace TortoiseBots::CraftOrderPolicy;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestShortfall()
{
    CHECK(ReagentShortfall(2, 5, 7) == 3);
    CHECK(ReagentShortfall(2, 5, 10) == 0);
    CHECK(ReagentShortfall(2, 5, 11) == 0);
    CHECK(ReagentShortfall(0, 9, 0) == 0);
    CHECK(ReagentShortfall(3, 0, 1) == 0);
}

static void TestMinYield()
{
    // A plain single-item craft: 0 base + 1..1 roll.
    CHECK(MinYieldPerCast(0, 1, 1) == 1);
    // Bandages: 1 base + 1..1 roll.
    CHECK(MinYieldPerCast(1, 1, 1) == 2);
    // A 1..3 roll guarantees 1.
    CHECK(MinYieldPerCast(0, 1, 3) == 1);
    // The lower end wins whichever side it is on.
    CHECK(MinYieldPerCast(0, 3, 1) == 1);
    // Never below one item per cast.
    CHECK(MinYieldPerCast(-5, 1, 1) == 1);
}

static void TestCastsForRemaining()
{
    CHECK(CastsForRemaining(10, 1) == 10);
    CHECK(CastsForRemaining(10, 2) == 5);
    CHECK(CastsForRemaining(11, 2) == 6);
    CHECK(CastsForRemaining(0, 2) == 0);
    CHECK(CastsForRemaining(7, 0) == 7);
}

static void TestFitsInFreeSlots()
{
    // 5 casts of a 1-per-cast item at stack 20: one slot.
    CHECK(FitsInFreeSlots(5, 1, 20, 1));
    CHECK(!FitsInFreeSlots(5, 1, 20, 0));
    // 21 items at stack 20 need two slots.
    CHECK(!FitsInFreeSlots(21, 1, 20, 1));
    CHECK(FitsInFreeSlots(21, 1, 20, 2));
    // Multi-yield worst case: 3 casts x up to 3 = 9 items, stack 5 -> 2 slots.
    CHECK(FitsInFreeSlots(3, 3, 5, 2));
    CHECK(!FitsInFreeSlots(3, 3, 5, 1));
    // Zero casts always fits.
    CHECK(FitsInFreeSlots(0, 3, 5, 0));
}

static void TestBatchForFreeSlots()
{
    // Whole order fits: full batch.
    CHECK(BatchForFreeSlots(5, 1, 20, 1) == 5);
    // One slot of 20 holds 6 casts at worst-case 3 each (18 items).
    CHECK(BatchForFreeSlots(10, 3, 20, 1) == 6);
    // Non-stackable: one cast per slot.
    CHECK(BatchForFreeSlots(5, 1, 1, 3) == 3);
    // No room: no batch (the caller delivers first).
    CHECK(BatchForFreeSlots(5, 1, 20, 0) == 0);
    CHECK(BatchForFreeSlots(0, 1, 20, 4) == 0);
}

static void TestFreeBagSlots()
{
    // Backpack 3/16 used plus bags with 0 and 4 free: 13 + 4.
    CHECK(FreeBagSlots(3, 16, std::vector<uint32_t>{0, 4},
        [](uint32_t free) { return free; }) == 17);
    // Full everywhere.
    CHECK(FreeBagSlots(16, 16, std::vector<uint32_t>{0, 0},
        [](uint32_t free) { return free; }) == 0);
    // No bags.
    CHECK(FreeBagSlots(10, 16, std::vector<uint32_t>{},
        [](uint32_t free) { return free; }) == 6);
}

static void TestCastsAffordable()
{
    CHECK(CastsAffordable(10, 2) == 5);
    CHECK(CastsAffordable(9, 2) == 4);
    CHECK(CastsAffordable(1, 2) == 0);
    CHECK(CastsAffordable(0, 0) == UINT32_MAX);
    CHECK(CastsAffordable(7, 0) == UINT32_MAX);
}

static void TestBundlesForShortfall()
{
    CHECK(BundlesForShortfall(0, 5) == 0);
    CHECK(BundlesForShortfall(5, 5) == 1);
    CHECK(BundlesForShortfall(6, 5) == 2);
    CHECK(BundlesForShortfall(7, 0) == 7);
}

int main()
{
    TestShortfall();
    TestMinYield();
    TestCastsForRemaining();
    TestFitsInFreeSlots();
    TestBatchForFreeSlots();
    TestFreeBagSlots();
    TestCastsAffordable();
    TestBundlesForShortfall();
    std::printf("craft order policy: %d checks passed\n", checks);
    return 0;
}
