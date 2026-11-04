// Standalone check for ai/playerbot/policy/raid/BurnManaPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_burn_mana_policy.cpp -o /tmp/test_burn_mana_policy && /tmp/test_burn_mana_policy
#include "../ai/playerbot/policy/raid/BurnManaPolicy.h"

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
    // Moam's mana is full while drain mana keeps refilling it: a burner keeps burning above the row's start %.
    CHECK(BurnManaWanted(80.0f, 10.0f, true, 0, 4)); // BurnsWhileAboveStart
    // Below the start % the boss is filling up on his own drain casts and the burners go back to the raid.
    CHECK(!BurnManaWanted(9.0f, 10.0f, true, 0, 4)); // IdleBelowStart
    // `priority` is the most burners; the ranks are 0-based, so rank 4 is the fifth bot and sits out a cap of 4.
    CHECK(!BurnManaWanted(80.0f, 10.0f, true, 4, 4)); // CapsAtMaxBurners
    CHECK(BurnManaWanted(80.0f, 10.0f, true, 3, 4));
    // A cap of 0 means every eligible bot burns.
    CHECK(BurnManaWanted(80.0f, 10.0f, true, 9, 0)); // ZeroCapMeansAll
    // A bot with none of the three spells never burns, whatever the mana.
    CHECK(!BurnManaWanted(80.0f, 10.0f, false, 0, 0));

    std::cout << "All burn mana policy tests passed.\n";
    return 0;
}
