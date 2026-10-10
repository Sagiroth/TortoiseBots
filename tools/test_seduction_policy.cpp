// Standalone regression test for the Succubus Seduction gate
// (runtime/SeductionPolicy.h, PET-2): Seduction fires only with a Succubus
// out on a non-player humanoid mark.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_seduction_policy.cpp -o /tmp/test_seduction
//   /tmp/test_seduction

#include "../runtime/SeductionPolicy.h"

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

static SeductionGateInputs Mark(uint32_t petEntry, uint32_t creatureType)
{
    SeductionGateInputs inputs;
    inputs.hasPet = true;
    inputs.currentPetEntry = petEntry;
    inputs.targetCreatureType = creatureType;
    return inputs;
}

static void TestSuccubusOnHumanoidFires()
{
    CHECK(CanCastSeduction(Mark(WARLOCK_SUCCUBUS_PET_ENTRY, CREATURE_TYPE_HUMANOID_VALUE)));
}

static void TestOtherDemonsStayQuiet()
{
    CHECK(!CanCastSeduction(Mark(416, CREATURE_TYPE_HUMANOID_VALUE)));
    CHECK(!CanCastSeduction(Mark(1860, CREATURE_TYPE_HUMANOID_VALUE)));
    CHECK(!CanCastSeduction(Mark(417, CREATURE_TYPE_HUMANOID_VALUE)));
}

static void TestPetlessStaysQuiet()
{
    SeductionGateInputs inputs;
    inputs.targetCreatureType = CREATURE_TYPE_HUMANOID_VALUE;
    CHECK(!CanCastSeduction(inputs));
}

static void TestNonHumanoidsStayQuiet()
{
    // Demon, elemental, undead, mechanical: banish/fear/en slave territory.
    CHECK(!CanCastSeduction(Mark(WARLOCK_SUCCUBUS_PET_ENTRY, 3)));
    CHECK(!CanCastSeduction(Mark(WARLOCK_SUCCUBUS_PET_ENTRY, 4)));
    CHECK(!CanCastSeduction(Mark(WARLOCK_SUCCUBUS_PET_ENTRY, 6)));
    CHECK(!CanCastSeduction(Mark(WARLOCK_SUCCUBUS_PET_ENTRY, 9)));
}

static void TestPlayersStayQuiet()
{
    SeductionGateInputs inputs = Mark(WARLOCK_SUCCUBUS_PET_ENTRY, CREATURE_TYPE_HUMANOID_VALUE);
    inputs.targetIsPlayer = true;
    CHECK(!CanCastSeduction(inputs));
}

int main()
{
    TestSuccubusOnHumanoidFires();
    TestOtherDemonsStayQuiet();
    TestPetlessStaysQuiet();
    TestNonHumanoidsStayQuiet();
    TestPlayersStayQuiet();
    std::printf("test_seduction_policy: %d checks passed\n", checks);
    return 0;
}
