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

int main()
{
    TestShortfall();
    TestMinYield();
    TestCastsForRemaining();
    TestFitsInFreeSlots();
    std::printf("craft order policy: %d checks passed\n", checks);
    return 0;
}
