#include "../ai/playerbot/strategy/values/RogueWeaponPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::RogueSpecWantsDaggers;

int main()
{
    std::cout << "Starting TortoiseBots rogue weapon policy tests...\n";

    // Assassination and Subtlety need a dagger main hand (core refuses
    // Backstab/Ambush without one), so both specs allow daggers only.
    CHECK(RogueSpecWantsDaggers("assas") == true);
    CHECK(RogueSpecWantsDaggers("subtle") == true);
    std::cout << "  [PASS] assas/subtle want daggers\n";

    // Combat prefers swords, maces and fist weapons; other classes and the
    // pre-talent fallback are never dagger-only.
    CHECK(RogueSpecWantsDaggers("combat") == false);
    CHECK(RogueSpecWantsDaggers("") == false);
    CHECK(RogueSpecWantsDaggers("fury") == false);
    CHECK(RogueSpecWantsDaggers("prot") == false);
    std::cout << "  [PASS] combat and non-rogue specs are not dagger-only\n";

    std::cout << "All rogue weapon policy checks PASSED!\n";
    return 0;
}
