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

    // Rage tank above critical: shield refused (eats rage).
    // Rage tank above critical: shield refused, in or out of combat (the rule has no combat input; the wiring test pins that the guard reads none).
    CHECK(!ShouldShieldTarget(true, true, 50, 20));
    // Rage tank at/below critical: the save wins.
    CHECK(ShouldShieldTarget(true, true, 20, 20));
    // Mana tank (paladin) at 50 %: shields as usual.
    CHECK(ShouldShieldTarget(true, false, 50, 20));
    // Rage non-tank (DPS warrior) at 50 %: shields as usual.
    CHECK(ShouldShieldTarget(false, true, 50, 20));
    // Stance/form counts as tank even when the talent role does not.
    CHECK(!TankForShield(false, false, false, false));
    CHECK(TankForShield(true, false, false, false));
    CHECK(TankForShield(false, false, true, false));
    CHECK(TankForShield(false, false, false, true));
    // An explicitly assigned non-tank role wins over stance and form.
    CHECK(!TankForShield(false, true, true, false));
    CHECK(!TankForShield(false, true, false, true));
    CHECK(TankForShield(true, true, true, false));
    // Defensive-Stance rage tank above critical: shield refused.
    CHECK(!ShouldShieldTarget(TankForShield(false, false, true, false), true, 50, 20));
    // Explicit DPS in Defensive Stance: shields as usual.
    CHECK(ShouldShieldTarget(TankForShield(false, true, true, false), true, 50, 20));
    // A bear without Feral talents above critical: refused.
    CHECK(!ShouldShieldTarget(TankForShield(false, false, false, true), true, 66, 20));
    // A stance tank below critical: the save lands.
    CHECK(ShouldShieldTarget(TankForShield(false, false, true, false), true, 19, 20));

    std::cout << "All shield-target policy tests passed.\n";
    return 0;
}
