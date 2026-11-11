// Standalone check for ai/playerbot/HealReservePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_heal_reserve_policy.cpp -o /tmp/test_heal_reserve_policy && /tmp/test_heal_reserve_policy
#include "../ai/playerbot/HealReservePolicy.h"
#include "../ai/playerbot/HealingTriagePolicy.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
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
    return HealReservePick(std::vector<HealReserveRow>{{reservePct, windowSec, 14834}}, true, dueInMs);
}

// The named failing case: outside the release window the top-up needs the row's reserve (85 %), so 84 % of mana is
// refused and 85 % allowed; inside the window (due in 60 s or less) today's MediumMana rule (40) decides.
static void TestTopUpHeldUnderReserveOutsideWindow()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(outside.live);
    CHECK(!outside.released);
    CHECK(!HealTopUpAllowedAtHealth(true, outside, 84, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(HealTopUpAllowedAtHealth(true, outside, 85, 40, 75.0f, 70.0f, 90.0f, false));

    const HealReserveRead released = Live(85.0f, 60.0f, 60000);
    CHECK(released.released);
    CHECK(HealTopUpAllowedAtHealth(true, released, 41, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(!HealTopUpAllowedAtHealth(true, released, 39, 40, 75.0f, 70.0f, 90.0f, false));
}

// A tank or the boss's current victim is on today's rule whatever the reserve.
static void TestTopUpExemptTarget()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(HealTopUpAllowedAtHealth(true, outside, 41, 40, 75.0f, 70.0f, 90.0f, true));
    CHECK(!HealTopUpAllowedAtHealth(true, outside, 39, 40, 75.0f, 70.0f, 90.0f, true));
}

// The key off is exactly today's rule, whatever the row says.
static void TestTopUpFlagOffIsToday()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(HealTopUpAllowedAtHealth(false, outside, 40, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(!HealTopUpAllowedAtHealth(false, outside, 39, 40, 75.0f, 70.0f, 90.0f, false));
    const HealReserveRead released = Live(85.0f, 60.0f, 1000);
    CHECK(!HealTopUpAllowedAtHealth(false, released, 39, 40, 75.0f, 70.0f, 90.0f, false));
    const HealReserveRead none;
    CHECK(HealTopUpAllowedAtHealth(true, none, 40, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(!HealTopUpAllowedAtHealth(true, none, 39, 40, 75.0f, 70.0f, 90.0f, false));
}

// No clock, or a lead of 0, never releases the reserve.
static void TestReserveNoClock()
{
    CHECK(!HealReserveReleased(false, 0, 60.0f));
    CHECK(!HealReserveReleased(false, 60000, 60.0f));
    CHECK(!HealReserveReleased(true, 0, 0.0f));
    const HealReserveRead none = HealReservePick(std::vector<HealReserveRow>{{85.0f, 60.0f, 14834}}, false, 0);
    CHECK(none.live);
    CHECK(!none.released);
    CHECK(!none.clockKnown);
    CHECK(!HealTopUpAllowedAtHealth(true, none, 84, 40, 75.0f, 70.0f, 90.0f, false));
    const HealReserveRead leadZero = Live(85.0f, 0.0f, 0);
    CHECK(leadZero.live);
    CHECK(!leadZero.released);
}

// Several rows keep the highest reserve, and its own window and boss go with it.
static void TestReservePickHighest()
{
    const std::vector<HealReserveRow> rows{{70.0f, 30.0f, 111}, {85.0f, 60.0f, 222}};
    const HealReserveRead pick = HealReservePick(rows, true, 45000);
    CHECK(pick.live);
    CHECK(pick.reservePct == 85.0f);
    CHECK(pick.bossEntry == 222);
    CHECK(pick.clockKnown);
    CHECK(pick.dueInMs == 45000);
    CHECK(pick.released);               // the 85 % row's 60 s window, not the losing row's 30 s
    CHECK(!HealReservePick(rows, true, 70000).released);
    CHECK(HealReservePick(rows, true, 60000).released);
    CHECK(!HealReservePick(rows, true, 60001).released);
    CHECK(!HealReservePick(std::vector<HealReserveRow>{}, true, 0).live);
    // A tie keeps the first row, and its boss with it.
    CHECK(HealReservePick({{85.0f, 60.0f, 111}, {85.0f, 30.0f, 222}}, true, 70000).bossEntry == 111);
}

// The reserve governs only the almost-full top-up tier; a 60 % member is another tier's decision.
static void TestTopUpTier()
{
    CHECK(!HealReserveTopUpTier(60.0f, 70.0f, 90.0f));
    CHECK(HealReserveTopUpTier(70.0f, 70.0f, 90.0f));
    CHECK(HealReserveTopUpTier(89.9f, 70.0f, 90.0f));
    CHECK(!HealReserveTopUpTier(90.0f, 70.0f, 90.0f));
}

// The named D2 case: six non-tanks at 71-77 % are refused while the healer (60 % mana) sits under the live 85 % line;
// the tank at 88 % is exempt, so the pick's skip leaves it as the only candidate and the pick.
static void TestPickFallsToTheExemptTank()
{
    HealingBands const bands{20.0f, 50.0f, 70.0f, 90.0f};
    const HealReserveRead read = Live(85.0f, 60.0f, 70000);
    const std::uint8_t mana = 60;
    const std::uint8_t mediumMana = 70;
    std::vector<HealingCandidate> candidates;
    for (std::uint64_t i = 0; i < 6; ++i)
        candidates.push_back(HealingCandidate{i + 1, static_cast<std::uint32_t>(71000 + i * 1000), 100000, 0, false,
            false, false});
    candidates.push_back(HealingCandidate{7, 88000, 100000, 0, true, false, false});
    // The skip is the pick's: an almost-full candidate the reserve refuses (a tank is exempt) is not a candidate.
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
        [&](HealingCandidate const& candidate)
        {
            return HealReserveSkipsTopUp(true, read, mana, mediumMana, PredictedHealHealth(candidate), bands.medium,
                bands.almostFull, candidate.tank);
        }), candidates.end());
    CHECK(candidates.size() == 1);
    std::sort(candidates.begin(), candidates.end(), [&](HealingCandidate const& left, HealingCandidate const& right)
        { return HealingPriority(left, right, bands); });
    CHECK(candidates[HealingAssignment(candidates, bands, 0)].guid == 7);
    // Key off: nothing is refused, so the pick is today's (the lowest health non-tank, the six all equal on the ledge).
    CHECK(!HealReserveRefuses(false, read, mana, mediumMana, false));
    // A member below the tier is never skipped, whatever the reserve says.
    CHECK(!HealReserveTopUpTier(60.0f, bands.medium, bands.almostFull));
}

// The named regression: the reserve used raw health while triage used the effective health, so a member at 75 % raw
// whose predicted incoming damage takes it to 68 % was refused as a top-up although triage calls it urgent. The
// effective value is the triage one (HealEffectiveHealthPct, the same PredictedHealHealth the pick passes), and the
// reserve must not skip it even with the healer under the live 85 % line.
static void TestReserveTierReadsEffectiveHealthNotRaw()
{
    const HealReserveRead read = Live(85.0f, 60.0f, 70000);
    const std::uint8_t mana = 60;
    const std::uint8_t mediumMana = 40;
    const float mediumHealth = 70.0f;
    const float almostFull = 90.0f;
    float const damaged = HealEffectiveHealthPct(75000, 100000, 0, 7000, 20);
    CHECK(damaged < mediumHealth);
    CHECK(!HealReserveSkipsTopUp(true, read, mana, mediumMana, damaged, mediumHealth, almostFull, false));
    CHECK(HealTopUpAllowedAtHealth(true, read, mana, mediumMana, damaged, mediumHealth, almostFull, false));
    // The raw health would have called it a top-up; that is the classification this case replaces.
    CHECK(HealReserveTopUpTier(75.0f, mediumHealth, almostFull));
    // The player's own mana rule still stands: a healer under MediumMana is refused at that tier.
    CHECK(!HealTopUpAllowedAtHealth(true, read, 39, mediumMana, damaged, mediumHealth, almostFull, false));
}

// The reverse: raw 65 % with credited incoming heals reaching 75 % is a top-up, refused like any other.
static void TestReserveTierReadsCreditedHealsIntoTheBand()
{
    const HealReserveRead read = Live(85.0f, 60.0f, 70000);
    const std::uint8_t mana = 60;
    const std::uint8_t mediumMana = 40;
    const float mediumHealth = 70.0f;
    const float almostFull = 90.0f;
    float const healed = HealEffectiveHealthPct(65000, 100000, 10000, 0, 20);
    CHECK(healed >= mediumHealth && healed < almostFull);
    CHECK(HealReserveSkipsTopUp(true, read, mana, mediumMana, healed, mediumHealth, almostFull, false));
    CHECK(!HealTopUpAllowedAtHealth(true, read, mana, mediumMana, healed, mediumHealth, almostFull, false));
    // Key off: today's rule, so 60 % mana allows the top-up and 39 % does not.
    CHECK(!HealReserveSkipsTopUp(false, read, mana, mediumMana, healed, mediumHealth, almostFull, false));
    CHECK(HealTopUpAllowedAtHealth(false, read, mana, mediumMana, healed, mediumHealth, almostFull, false));
    CHECK(!HealTopUpAllowedAtHealth(false, read, 39, mediumMana, healed, mediumHealth, almostFull, false));
}

// The reserve never pulls the line under today's MediumMana rule.
static void TestReserveNeverBelowMedium()
{
    const HealReserveRead low = Live(20.0f, 60.0f, 70000);
    CHECK(!HealTopUpAllowedAtHealth(true, low, 39, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(HealTopUpAllowedAtHealth(true, low, 40, 40, 75.0f, 70.0f, 90.0f, false));
}

int main()
{
    TestReserveTierReadsEffectiveHealthNotRaw();
    TestReserveTierReadsCreditedHealsIntoTheBand();
    TestTopUpHeldUnderReserveOutsideWindow();
    TestTopUpExemptTarget();
    TestTopUpFlagOffIsToday();
    TestReserveNoClock();
    TestReservePickHighest();
    TestReserveNeverBelowMedium();
    TestTopUpTier();
    TestPickFallsToTheExemptTank();

    std::cout << "All heal reserve policy tests passed.\n";
    return 0;
}
