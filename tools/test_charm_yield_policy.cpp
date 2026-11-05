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

    // A charmed raid member is never a dps or tank pick: the attackers list, the possible attack targets and the
    // tank's victim all ask this rule before a tournament sees the member. Today (key on, a creature charm) the mage
    // is picked and killed by its own raid 3-7 s later, so the first case fails before the fix.
    auto picked = [](bool keyOn, bool sameGroup, bool hasCharmer, bool charmerIsPlayer)
    {
        return !CharmedAllyIsOffLimits(keyOn, sameGroup, hasCharmer, charmerIsPlayer);
    };
    CHECK(!picked(true, true, true, false));   // Hakkar's Cause Insanity 24327 on a raid mage
    CHECK(picked(false, true, true, false));   // key off: today's pick
    CHECK(picked(true, false, true, false));   // an enemy player outside the group is an ordinary target
    CHECK(picked(true, true, true, true));     // a player's mind control is the core's business
    CHECK(picked(true, true, false, false));   // not charmed at all
    // Healing is not a pick: the rule never gates a heal action, and a charmed member cannot be targeted by one
    // through the hostile cast paths anyway.

    // A pet stands down for the whole charm, not only for the order that CanPetAttack already refuses: the core
    // PetAI attacks back after the charmed member hits the owner or pet, and an attack already under way survives a
    // refused order. Before this rule the pet kept swinging on the member (a swing is not lost to AttackStop).
    CHECK(CharmedAllyParksPet(true, true));
    CHECK(!CharmedAllyParksPet(false, true));
    CHECK(!CharmedAllyParksPet(true, false));
    CHECK(!CharmedAllyParksPet(false, false));

    // The hold is raised by any live same-map member a creature charms, the bot itself included (its own pet and
    // guardians would otherwise keep fighting for the charmed owner). A dead, absent, off-map, uncharmed or
    // player-charmed member does not raise it.
    CHECK(MemberHoldsPetHold(true, true, true, true, false));
    CHECK(!MemberHoldsPetHold(false, true, true, true, false));
    CHECK(!MemberHoldsPetHold(true, false, true, true, false));
    CHECK(!MemberHoldsPetHold(true, true, false, true, false));
    CHECK(!MemberHoldsPetHold(true, true, true, false, false));
    CHECK(!MemberHoldsPetHold(true, true, true, true, true));

    std::cout << "All charm-yield policy tests passed.\n";
    return 0;
}
