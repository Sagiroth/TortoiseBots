// Standalone regression test for the Felhunter Devour Magic gate
// (runtime/DevourMagicPolicy.h): purge/cleanse may only fire while the
// active demon is a Felhunter (entry 417); Imp/Voidwalker/Succubus and the
// petless case stay quiet.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_devour_magic_policy.cpp -o /tmp/test_devour_magic
//   /tmp/test_devour_magic

#include "../runtime/DevourMagicPolicy.h"

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

static DevourMagicGateInputs Pet(uint32_t entry)
{
    DevourMagicGateInputs inputs;
    inputs.hasPet = true;
    inputs.currentPetEntry = entry;
    return inputs;
}

static void TestFelhunterCasts()
{
    CHECK(CanCastDevourMagic(Pet(WARLOCK_FELHUNTER_PET_ENTRY)));
}

static void TestOtherDemonsStayQuiet()
{
    CHECK(!CanCastDevourMagic(Pet(416)));   // Imp
    CHECK(!CanCastDevourMagic(Pet(1860)));  // Voidwalker
    CHECK(!CanCastDevourMagic(Pet(1863)));  // Succubus
}

static void TestPetlessStaysQuiet()
{
    DevourMagicGateInputs inputs;
    CHECK(!CanCastDevourMagic(inputs));
}

int main()
{
    TestFelhunterCasts();
    TestOtherDemonsStayQuiet();
    TestPetlessStaysQuiet();
    std::printf("test_devour_magic_policy: %d checks passed\n", checks);
    return 0;
}
