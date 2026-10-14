// Standalone regression test for the free-hire policy (runtime/HireFeePolicy.h).
// Guards
//  - AiPlayerbot.HireFreeMinSecurity = 0 is off for every security level,
//  - a level at or above the key hires free, a level below it pays the quote,
//  - the key is clamped to 0..6 (negative = off),
//  - a free hire charges exactly 0 and never turns a fee into a credit.
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_fee_policy.cpp -o /tmp/test_hire_fee_policy
//   /tmp/test_hire_fee_policy

#include "../runtime/HireFeePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestOffByDefault()
{
    for (int sec = 0; sec <= 6; ++sec)
    {
        CHECK(!HireFeeWaived(sec, 0));
        CHECK(HireFeeCharged(15000, sec, 0) == 15000);
    }
}

static void TestThreshold()
{
    CHECK(!HireFeeWaived(0, 3));
    CHECK(!HireFeeWaived(2, 3));
    CHECK(HireFeeWaived(3, 3));
    CHECK(HireFeeWaived(4, 3));
    CHECK(HireFeeWaived(6, 3));
    CHECK(HireFeeCharged(15000, 2, 3) == 15000);
    CHECK(HireFeeCharged(15000, 3, 3) == 0);
    CHECK(HireFeeCharged(15000, 6, 3) == 0);
    // Key 1: every account above plain player hires free; level 0 still pays.
    CHECK(!HireFeeWaived(0, 1));
    CHECK(HireFeeWaived(1, 1));
}

static void TestZeroQuoteStaysZero()
{
    CHECK(HireFeeCharged(0, 0, 0) == 0);
    CHECK(HireFeeCharged(0, 3, 3) == 0);
}

static void TestClamp()
{
    CHECK(ClampHireFreeMinSecurity(-5) == 0);
    CHECK(ClampHireFreeMinSecurity(0) == 0);
    CHECK(ClampHireFreeMinSecurity(3) == 3);
    CHECK(ClampHireFreeMinSecurity(6) == 6);
    CHECK(ClampHireFreeMinSecurity(7) == 6);
    CHECK(ClampHireFreeMinSecurity(1000000) == 6);
}

int main()
{
    TestOffByDefault();
    TestThreshold();
    TestZeroQuoteStaysZero();
    TestClamp();
    std::printf("test_hire_fee_policy: %d checks passed\n", checks);
    return 0;
}
