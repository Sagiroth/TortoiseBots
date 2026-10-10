// Standalone regression test for the pet taunt situation toggle
// (runtime/PetTauntPolicy.h, PET-3 + PET-8b): Growl/Torment autocast stays on
// solo or in a tankless group, and turns off when grouped with a real tank;
// the Cower rank set is the deliberate threat-drop ordered in that case.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pet_taunt_policy.cpp -o /tmp/test_pet_taunt
//   /tmp/test_pet_taunt

#include "../runtime/PetTauntPolicy.h"

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

static PetTauntInputs Inputs(bool grouped, bool tank)
{
    PetTauntInputs inputs;
    inputs.grouped = grouped;
    inputs.tankInGroup = tank;
    return inputs;
}

// Solo: the pet is the tank, taunts stay on.
static void TestSoloTaunts()
{
    CHECK(ShouldPetTaunt(Inputs(false, false)));
}

// Grouped with a real tank: pet taunts off.
static void TestGroupedWithTankStandsDown()
{
    CHECK(!ShouldPetTaunt(Inputs(true, true)));
}

// Grouped with no tank: the pet is the fallback tank, taunts stay on.
static void TestTanklessGroupTaunts()
{
    CHECK(ShouldPetTaunt(Inputs(true, false)));
}

// Taunt spell set: every Growl/Torment rank, nothing else.
static void TestTauntSpellSet()
{
    CHECK(IsPetTauntSpell(2649));
    CHECK(IsPetTauntSpell(14916));
    CHECK(IsPetTauntSpell(14921));
    CHECK(IsPetTauntSpell(3716));
    CHECK(IsPetTauntSpell(7811));
    CHECK(IsPetTauntSpell(11775));
    CHECK(!IsPetTauntSpell(19505)); // Devour Magic is deliberate-cast, not a taunt
    CHECK(!IsPetTauntSpell(19244)); // Spell Lock is an interrupt, not a taunt
    CHECK(!IsPetTauntSpell(1742));  // Cower is a threat drop, not a taunt
}

// Cower set: all six ranks, and taunt spells are not cower.
static void TestCowerSpellSet()
{
    CHECK(IsPetCowerSpell(1742));
    CHECK(IsPetCowerSpell(1756));
    CHECK(IsPetCowerSpell(16697));
    CHECK(!IsPetCowerSpell(2649));
    CHECK(!IsPetCowerSpell(3716));
}

int main()
{
    TestSoloTaunts();
    TestGroupedWithTankStandsDown();
    TestTanklessGroupTaunts();
    TestTauntSpellSet();
    TestCowerSpellSet();
    std::printf("test_pet_taunt_policy: %d checks passed\n", checks);
    return 0;
}
