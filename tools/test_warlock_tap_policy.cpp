// Standalone regression test for the warlock Life Tap top-up rule
// (runtime/WarlockTapPolicy.h): donor-parity two-band tapping — urgent at
// mana<=mediumMana, low-priority top-up below 85%, both gated on health
// above lowHealth. Affliction Dark Pact / the health floor are untouched.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_warlock_tap_policy.cpp -o /tmp/test_warlock_tap
//   /tmp/test_warlock_tap

#include "../runtime/WarlockTapPolicy.h"

#include <cstdint>
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

static WarlockTapInputs Tap(bool knows, uint8_t mana, uint8_t health)
{
    WarlockTapInputs inputs;
    inputs.knowsLifeTap = knows;
    inputs.manaPct = mana;
    inputs.healthPct = health;
    return inputs;
}

// Near-empty mana with safe health: the urgent band (live NORMAL+2 row).
static void TestUrgentAtMediumMana()
{
    CHECK(DecideWarlockTap(Tap(true, 40, 80)) == WarlockTapDecision::TapUrgent);
    CHECK(DecideWarlockTap(Tap(true, 15, 80)) == WarlockTapDecision::TapUrgent);
}

// Mid mana (41-84) with safe health: the new filler / pre-tap band.
static void TestTopUpBelow85()
{
    CHECK(DecideWarlockTap(Tap(true, 84, 80)) == WarlockTapDecision::TapTopUp);
    CHECK(DecideWarlockTap(Tap(true, 60, 80)) == WarlockTapDecision::TapTopUp);
    CHECK(DecideWarlockTap(Tap(true, 41, 80)) == WarlockTapDecision::TapTopUp);
}

// Near-full mana: no tap — enter the pull casting, not tapping.
static void TestFullManaLeftAlone()
{
    CHECK(DecideWarlockTap(Tap(true, 85, 80)) == WarlockTapDecision::LeaveAlone);
    CHECK(DecideWarlockTap(Tap(true, 100, 100)) == WarlockTapDecision::LeaveAlone);
}

// Health at/below the floor: never tap, in either band.
static void TestLowHealthNeverTaps()
{
    CHECK(DecideWarlockTap(Tap(true, 20, 50)) == WarlockTapDecision::LeaveAlone);
    CHECK(DecideWarlockTap(Tap(true, 20, 30)) == WarlockTapDecision::LeaveAlone);
    CHECK(DecideWarlockTap(Tap(true, 60, 50)) == WarlockTapDecision::LeaveAlone);
    CHECK(DecideWarlockTap(Tap(true, 60, 30)) == WarlockTapDecision::LeaveAlone);
}

// Untrained Life Tap (pre-6): the triggers stay quiet.
static void TestUntrainedLeftAlone()
{
    CHECK(DecideWarlockTap(Tap(false, 20, 80)) == WarlockTapDecision::LeaveAlone);
    CHECK(DecideWarlockTap(Tap(false, 60, 80)) == WarlockTapDecision::LeaveAlone);
}

// Pin the donor top-up line so a stray edit faces this test.
static void TestConstants()
{
    CHECK(WARLOCK_TAP_TOPUP_MANA == 85);
}

int main()
{
    std::printf("Starting warlock tap policy tests...\n");
    TestUrgentAtMediumMana();
    std::printf("  [PASS] urgent at medium mana\n");
    TestTopUpBelow85();
    std::printf("  [PASS] top-up below 85\n");
    TestFullManaLeftAlone();
    std::printf("  [PASS] full mana left alone\n");
    TestLowHealthNeverTaps();
    std::printf("  [PASS] low health never taps\n");
    TestUntrainedLeftAlone();
    std::printf("  [PASS] untrained left alone\n");
    TestConstants();
    std::printf("  [PASS] constants\n");
    std::printf("All warlock tap policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
