#pragma once

// Pure decision rules for the eat/drink gate. The bot's own group test misses
// a straggler more than sight distance from every fighting member, and it
// flaps on the ticks between kills when no member has a victim; the run
// leader already knows the whole truth ("any bound bot or its pet is in
// combat") and stamps it here once a second. No core includes: the callers
// translate the world into numbers, so tools/test_meal_gate_policy.cpp can
// test the rules.

#include <cstdint>

namespace ai
{
    // The run leader's 1 Hz tick marks each bound bot "the run is fighting
    // until now + hold". A meal waits while that mark is live (0 = never set).
    constexpr uint32_t RUN_FIGHT_HOLD_MS = 2500; // one 1 Hz tick missed, plus margin

    // A live mark lies at most one hold ahead of now. The subtraction is
    // unsigned, so a timer wrapping past 2^32 still reads inside the window,
    // while a stale or corrupted value far ahead cannot keep the meal held.
    inline bool RunFightHolds(uint32_t untilMs, uint32_t nowMs)
    {
        uint32_t const d = untilMs - nowMs;
        return untilMs != 0 && d != 0 && d <= RUN_FIGHT_HOLD_MS;
    }

    // A meal waits while something hits the bot, a member within sight is in a
    // real fight, or the run's mark says the run is fighting.
    inline bool MealBlocked(bool hasAttackers, bool memberNearFighting,
        uint32_t runFightUntilMs, uint32_t nowMs)
    {
        return hasAttackers || memberNearFighting || RunFightHolds(runFightUntilMs, nowMs);
    }
}
