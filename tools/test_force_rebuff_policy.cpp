// Standalone regression test for the force-rebuff top-off pass
// (mod-playerbots parity, BUFF-1): 2-min pending window, margin rule that
// grows with window age, and the buff-first heal veto.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_force_rebuff_policy.cpp -o /tmp/test_force_rebuff
//   /tmp/test_force_rebuff

#include "../ai/playerbot/ForceRebuffPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestWindow()
{
    CHECK(ForceRebuffPending(1000, 1000));
    CHECK(ForceRebuffPending(1000, 1000 + 119999));
    CHECK(!ForceRebuffPending(1000, 1000 + 120000));
    CHECK(!ForceRebuffPending(1000, 1000 + 600000));
    std::printf("  [PASS] pending window\n");
}

static void TestMargin()
{
    // Floor 60 s dominates early; age + 5 s dominates late.
    CHECK(ForceRebuffMarginMs(1000, 1000) == 60000u);
    CHECK(ForceRebuffMarginMs(1000, 30000) == 60000u);
    CHECK(ForceRebuffMarginMs(1000, 1000 + 120000) == 125000u);
    std::printf("  [PASS] margin growth\n");
}

static void TestBelowTarget()
{
    // Missing always rebuffs; permanent auras never count.
    CHECK(ForceRebuffBelowTarget(false, 0, 0, 1000, 2000));
    CHECK(!ForceRebuffBelowTarget(true, 0, 1800000, 1000, 2000));
    CHECK(!ForceRebuffBelowTarget(true, 1000, 0, 1000, 2000));
    // 30-min buff at full at window start: 1800 s + 60 s margin > 1800 s max.
    CHECK(!ForceRebuffBelowTarget(true, 1800000, 1800000, 1000, 2000));
    // Same buff 20 min in (remaining 600 s): 600 + 65 < 1800 -> rebuff.
    CHECK(ForceRebuffBelowTarget(true, 600000, 1800000, 1000, 1000 + 60000));
    std::printf("  [PASS] top-off verdict\n");
}

static void TestHealVeto()
{
    CHECK(ForceRebuffSuppressHeal(true, false, true, false));
    CHECK(ForceRebuffSuppressHeal(true, false, false, true));
    CHECK(!ForceRebuffSuppressHeal(true, false, false, false));
    CHECK(!ForceRebuffSuppressHeal(false, false, true, true));
    CHECK(!ForceRebuffSuppressHeal(true, true, true, true));
    std::printf("  [PASS] heal veto\n");
}

int main()
{
    std::printf("Starting TortoiseBots force-rebuff policy tests...\n");
    TestWindow();
    TestMargin();
    TestBelowTarget();
    TestHealVeto();
    std::printf("All force-rebuff policy tests passed (%d checks).\n", checks);
    return 0;
}
