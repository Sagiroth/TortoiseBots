// Standalone check for ai/playerbot/HealReservePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_heal_reserve_policy.cpp -o /tmp/test_heal_reserve_policy && /tmp/test_heal_reserve_policy
#include "../ai/playerbot/HealReservePolicy.h"

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static HealReserveRead Live(float reservePct, float windowSec, std::uint32_t dueInMs)
{
    return HealReservePick(std::vector<std::pair<float, float>>{std::make_pair(reservePct, windowSec)}, true, dueInMs);
}

// The named failing case: outside the release window the top-up needs the row's reserve (85 %), so 84 % of mana is
// refused and 85 % allowed; inside the window (due in 60 s or less) today's MediumMana rule (40) decides.
static void TestTopUpHeldUnderReserveOutsideWindow()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(outside.live);
    CHECK(!outside.released);
    CHECK(!HealTopUpAllowed(true, outside, 84, 40, false));
    CHECK(HealTopUpAllowed(true, outside, 85, 40, false));

    const HealReserveRead released = Live(85.0f, 60.0f, 60000);
    CHECK(released.released);
    CHECK(HealTopUpAllowed(true, released, 41, 40, false));
    CHECK(!HealTopUpAllowed(true, released, 39, 40, false));
}

// A tank or the boss's current victim is on today's rule whatever the reserve.
static void TestTopUpExemptTarget()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(HealTopUpAllowed(true, outside, 41, 40, true));
    CHECK(!HealTopUpAllowed(true, outside, 39, 40, true));
}

// The key off is exactly today's rule, whatever the row says.
static void TestTopUpFlagOffIsToday()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(HealTopUpAllowed(false, outside, 40, 40, false));
    CHECK(!HealTopUpAllowed(false, outside, 39, 40, false));
    const HealReserveRead released = Live(85.0f, 60.0f, 1000);
    CHECK(!HealTopUpAllowed(false, released, 39, 40, false));
    const HealReserveRead none;
    CHECK(HealTopUpAllowed(true, none, 40, 40, false));
    CHECK(!HealTopUpAllowed(true, none, 39, 40, false));
}

// No clock, or a lead of 0, never releases the reserve.
static void TestReserveNoClock()
{
    CHECK(!HealReserveReleased(false, 0, 60.0f));
    CHECK(!HealReserveReleased(false, 60000, 60.0f));
    CHECK(!HealReserveReleased(true, 0, 0.0f));
    const HealReserveRead none = HealReservePick(std::vector<std::pair<float, float>>{{85.0f, 60.0f}}, false, 0);
    CHECK(none.live);
    CHECK(!none.released);
    CHECK(!HealTopUpAllowed(true, none, 84, 40, false));
    const HealReserveRead leadZero = Live(85.0f, 0.0f, 0);
    CHECK(leadZero.live);
    CHECK(!leadZero.released);
}

// Several rows keep the highest reserve, and its own window goes with it.
static void TestReservePickHighest()
{
    const std::vector<std::pair<float, float>> rows{{70.0f, 30.0f}, {85.0f, 60.0f}};
    const HealReserveRead pick = HealReservePick(rows, true, 45000);
    CHECK(pick.live);
    CHECK(pick.reservePct == 85.0f);
    CHECK(pick.released);               // the 85 % row's 60 s window, not the losing row's 30 s
    CHECK(!HealReservePick(rows, true, 70000).released);
    CHECK(HealReservePick(rows, true, 60000).released);
    CHECK(!HealReservePick(rows, true, 60001).released);
    CHECK(!HealReservePick(std::vector<std::pair<float, float>>{}, true, 0).live);
}

// The reserve never pulls the line under today's MediumMana rule.
static void TestReserveNeverBelowMedium()
{
    const HealReserveRead low = Live(20.0f, 60.0f, 70000);
    CHECK(!HealTopUpAllowed(true, low, 39, 40, false));
    CHECK(HealTopUpAllowed(true, low, 40, 40, false));
}

int main()
{
    TestTopUpHeldUnderReserveOutsideWindow();
    TestTopUpExemptTarget();
    TestTopUpFlagOffIsToday();
    TestReserveNoClock();
    TestReservePickHighest();
    TestReserveNeverBelowMedium();

    std::cout << "All heal reserve policy tests passed.\n";
    return 0;
}
