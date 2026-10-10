// Standalone regression test for the combat revive gate
// (runtime/PetRevivePolicy.h, PET-5): the 10s Revive Pet channel only starts
// with a dead pet, zero attackers on the bot, and the bot unmounted.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pet_revive_policy.cpp -o /tmp/test_pet_revive
//   /tmp/test_pet_revive

#include "../runtime/PetRevivePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static PetReviveGateInputs Inputs(bool dead, uint8_t attackers, bool mounted)
{
    PetReviveGateInputs inputs;
    inputs.petDead = dead;
    inputs.attackerCount = attackers;
    inputs.mounted = mounted;
    return inputs;
}

// The one case the combat node exists for: dead pet, nobody hitting the bot.
static void TestSafeReviveFires()
{
    CHECK(CanRevivePetNow(Inputs(true, 0, false)));
}

static void TestLivePetStaysQuiet()
{
    CHECK(!CanRevivePetNow(Inputs(false, 0, false)));
}

// Any attacker on the bot vetoes the 10s channel.
static void TestAttackersVeto()
{
    CHECK(!CanRevivePetNow(Inputs(true, 1, false)));
    CHECK(!CanRevivePetNow(Inputs(true, 3, false)));
}

static void TestMountedStaysQuiet()
{
    CHECK(!CanRevivePetNow(Inputs(true, 0, true)));
}

int main()
{
    TestSafeReviveFires();
    TestLivePetStaysQuiet();
    TestAttackersVeto();
    TestMountedStaysQuiet();
    std::printf("test_pet_revive_policy: %d checks passed\n", checks);
    return 0;
}
