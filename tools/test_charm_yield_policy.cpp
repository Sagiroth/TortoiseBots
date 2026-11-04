// Standalone check for ai/playerbot/CharmYieldPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_charm_yield_policy.cpp -o /tmp/test_charm_yield_policy
//   /tmp/test_charm_yield_policy
#include "../ai/playerbot/CharmYieldPolicy.h"

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
    std::cout << "Starting TortoiseBots charm-yield policy tests...\n";

    // A creature's charm hands the bot to the core (Hakkar's Cause Insanity 24327: SetControlledBy of a creature, the
    // bot's own AI still installed), so its AI yields; a player charmer keeps the old behaviour. Key off: unchanged.
    CHECK(CreatureCharmYields(true, true, false));
    CHECK(!CreatureCharmYields(true, true, true));
    CHECK(!CreatureCharmYields(false, true, false));
    CHECK(!CreatureCharmYields(true, false, false));
    CHECK(!CreatureCharmYields(true, false, true));

    // An area action within 10 yd of a charmed group member stands down; a self-centred blast (Thunder Clap,
    // Consecration, Swipe, the cleave toggles) is judged from the bot, never from a point it has no target for.
    CHECK(CharmedAllyInArea(9.9f, 50.0f, false));
    CHECK(!CharmedAllyInArea(10.1f, 50.0f, false));
    CHECK(CharmedAllyInArea(10.0f, 50.0f, false));   // the edge is inclusive
    CHECK(CharmedAllyInArea(50.0f, 7.0f, true));
    CHECK(!CharmedAllyInArea(9.0f, 50.0f, true));
    CHECK(CharmedAllyInArea(9.0f, 10.0f, true));

    std::cout << "All charm-yield policy tests passed.\n";
    return 0;
}
