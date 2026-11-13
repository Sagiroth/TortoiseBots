// Standalone policy test for issue #1606: a summoned bot's live pet goes along.
// The summon commands hand the pet to the core's temporary unsummon cache
// (UnsummonPetTemporaryIfAny / ResummonPetTemporaryUnSummonedIfAny), which
// resummons it beside its owner when the teleport lands. Only a live pet of a
// bot with nothing already pending, and only when the core would resummon, is
// handed over; everything else keeps today's behaviour (the pet stays put).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_summon_pet_follow_policy.cpp -o /tmp/test_summon_pet_follow_policy && /tmp/test_summon_pet_follow_policy

#include "../ai/playerbot/SummonPetFollowPolicy.h"

#include <iostream>

namespace
{
int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        ++failures; \
    } \
} while (0)
} // namespace

using ai::SummonTeleportMovesPet;

int main()
{
    // The reported case: a live hunter pet on a summoned bot crosses with it.
    CHECK(SummonTeleportMovesPet(true, true, true, true));
    // A warlock demon is a controlled pet too: same facts, same answer.
    CHECK(SummonTeleportMovesPet(true, true, true, true));

    // No pet to bring.
    CHECK(!SummonTeleportMovesPet(false, true, true, true));
    CHECK(!SummonTeleportMovesPet(false, false, true, true));

    // A dead pet stays where it is: it cannot walk or pull, and a resummon would
    // bring it back at the owner's side instead of leaving the corpse behind.
    CHECK(!SummonTeleportMovesPet(true, false, true, true));

    // An earlier temporary unsummon is still pending: the core would resummon
    // that older pet and drop this one.
    CHECK(!SummonTeleportMovesPet(true, true, false, true));

    // The core would not resummon at the landing (mounted, taxi, dead, out of
    // the world): the pet would be gone until the next teleport.
    CHECK(!SummonTeleportMovesPet(true, true, true, false));

    // Each gate alone refuses; all four together are the only yes.
    CHECK(!SummonTeleportMovesPet(false, false, false, false));

    if (failures)
    {
        std::cerr << "summon-pet-follow policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "summon-pet-follow policy: OK\n";
    return 0;
}
