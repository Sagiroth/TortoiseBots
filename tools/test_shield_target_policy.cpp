// Shield target gate: a shield eats the hits a rage tank lives on, so it is
// kept for the save. Decision lives in ai/playerbot/policy/heal/ShieldTargetPolicy.h.

#include "../ai/playerbot/policy/heal/ShieldTargetPolicy.h"

#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ShouldShieldTarget;
using ai::TankForShield;

int main()
{
    std::cout << "Starting TortoiseBots shield-target policy tests...\n";

    // Rage tank in combat above critical: shield refused (eats rage).
    CHECK(!ShouldShieldTarget(true, true, true, 50, 20));
    // Rage tank in combat at/below critical: the save wins.
    CHECK(ShouldShieldTarget(true, true, true, 20, 20));
    // Rage tank out of combat: no rage to lose.
    CHECK(ShouldShieldTarget(true, true, false, 50, 20));
    // Mana tank (paladin) at 50 %: shields as usual.
    CHECK(ShouldShieldTarget(true, false, true, 50, 20));
    // Rage non-tank (DPS warrior) at 50 %: shields as usual.
    CHECK(ShouldShieldTarget(false, true, true, 50, 20));
    // Stance/form counts as tank even when the talent role does not.
    CHECK(!TankForShield(false, false, false));
    CHECK(TankForShield(true, false, false));
    CHECK(TankForShield(false, true, false));
    CHECK(TankForShield(false, false, true));
    // Defensive-Stance rage tank in combat above critical: shield refused.
    CHECK(!ShouldShieldTarget(TankForShield(false, true, false), true, true, 50, 20));

    std::cout << "All shield-target policy tests passed.\n";
    return 0;
}
