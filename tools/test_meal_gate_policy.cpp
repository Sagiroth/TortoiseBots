#include "../ai/playerbot/policy/combat/MealGatePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots meal gate policy tests...\n";

    // The named failing case: the run is fighting (its tick marked this bot),
    // the bot stands 80 yd from the fight with no attackers and no member with
    // a victim inside 60 yd. Today the meal starts and puts the AI to sleep.
    CHECK(MealBlocked(false, false, 12000, 10000));
    std::cout << "  [PASS] run fight holds: meal blocked far from the fight\n";

    // A mark never set means no run fight.
    CHECK(!MealBlocked(false, false, 0, 10000));
    std::cout << "  [PASS] never set: meal allowed\n";

    // Expiry at the boundary: the hold is over the moment now reaches it.
    CHECK(!MealBlocked(false, false, 10000, 10000));
    std::cout << "  [PASS] expired at the boundary: meal allowed\n";

    CHECK(!MealBlocked(false, false, 9000, 10000));
    std::cout << "  [PASS] expired before: meal allowed\n";

    // The timer wraps; the subtraction stays wrap-safe.
    CHECK(RunFightHolds(5, 0xFFFFFFF0u));
    CHECK(!RunFightHolds(0xFFFFFFF0u, 5));
    std::cout << "  [PASS] hold is wrap-safe across the timer wrap\n";

    // The hold is at most RUN_FIGHT_HOLD_MS: a stale or corrupted mark far
    // ahead, even half a timer away, cannot keep the meal held.
    CHECK(!RunFightHolds(20000, 10000));
    CHECK(!RunFightHolds(10000, 10000 + 0x80000000u + 5000));
    CHECK(RunFightHolds(10000 + RUN_FIGHT_HOLD_MS, 10000));
    CHECK(!RunFightHolds(10000 + RUN_FIGHT_HOLD_MS + 1, 10000));
    std::cout << "  [PASS] hold is bounded to one run tick\n";

    // The pre-existing gates keep refusing on their own.
    CHECK(MealBlocked(true, false, 0, 10000));
    CHECK(MealBlocked(false, true, 0, 10000));
    std::cout << "  [PASS] attackers alone and a near fighting member alone still block\n";

    // A live mark holds even when both old gates are clear.
    CHECK(MealBlocked(false, false, 12001, 10001));
    std::cout << "  [PASS] live mark holds until its expiry\n";

    std::cout << "All meal gate policy tests passed.\n";
    return 0;
}
