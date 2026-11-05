// Standalone check for ai/playerbot/policy/raid/RangedOnlyPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_ranged_only_policy.cpp -o /tmp/test_ranged_only_policy && /tmp/test_ranged_only_policy
#include "../ai/playerbot/policy/raid/RangedOnlyPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " << #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    // Ayamiss climbs 20 yd in the air: a melee bot under her is held while she stands 8 yd or more above it.
    CHECK(RangedOnlyHolds(true, 105.0f, 85.0f, 8.0f, false)); // MeleeHeldWhileSubjectHigh
    // A ranged bot is never held: the row exists because ranged attacks still reach.
    CHECK(!RangedOnlyHolds(false, 105.0f, 85.0f, 8.0f, false)); // RangedNeverHeld
    // Once she lands the gap closes and the hold ends by itself.
    CHECK(!RangedOnlyHolds(true, 87.0f, 85.0f, 8.0f, false)); // MeleeFreeAfterLanding
    // The threshold is inclusive: exactly 8 yd above still holds.
    CHECK(RangedOnlyHolds(true, 93.0f, 85.0f, 8.0f, false)); // ExactThresholdHolds
    // An explicit attack order outranks the tactic: the bot keeps the ordered subject until the order is cleared.
    CHECK(!RangedOnlyHolds(true, 105.0f, 85.0f, 8.0f, true)); // ExplicitOrderBeatsTheHold

    std::cout << "All ranged only policy tests passed.\n";
    return 0;
}
