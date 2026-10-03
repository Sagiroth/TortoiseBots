// Standalone regression test for the autonomous pet-autocast upkeep policy
// (E01, runtime/PetUpkeepPolicy.h): the denylist must match the 1.12-existant
// subset of the donor's disabledPetSpells (tw_world.spell_template verified)
// and the toggle decision must be a pure function of current vs desired state.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pet_upkeep_policy.cpp -o /tmp/test_pet_upkeep
//   /tmp/test_pet_upkeep

#include "../runtime/PetUpkeepPolicy.h"

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

// Donor denylist entries that exist in 1.12 must stay disabled.
static void TestDonorDenylistKept()
{
    CHECK(IsDisabledPetAutocast(24450)); // Prowl 1
    CHECK(IsDisabledPetAutocast(24452)); // Prowl 2
    CHECK(IsDisabledPetAutocast(24453)); // Prowl 3
    CHECK(IsDisabledPetAutocast(1742));  // Cower 1
    CHECK(IsDisabledPetAutocast(19244)); // Spell Lock 1
    CHECK(IsDisabledPetAutocast(19647)); // Spell Lock 2
    CHECK(IsDisabledPetAutocast(19505)); // Devour Magic 1
    CHECK(IsDisabledPetAutocast(19731)); // Devour Magic 2
    CHECK(IsDisabledPetAutocast(19734)); // Devour Magic 3
    CHECK(IsDisabledPetAutocast(19736)); // Devour Magic 4
}

// Cower ranks are off by default in the factory (cowerSpellIds), so the
// sweep must agree with it on every rank, not just rank 1.
static void TestAllCowerRanksDisabled()
{
    CHECK(IsDisabledPetAutocast(1742));
    CHECK(IsDisabledPetAutocast(1753));
    CHECK(IsDisabledPetAutocast(1754));
    CHECK(IsDisabledPetAutocast(1755));
    CHECK(IsDisabledPetAutocast(1756));
    CHECK(IsDisabledPetAutocast(16697));
}

// Donor-only ranks with no tw_world.spell_template row must NOT be listed:
// Devour Magic 27276/27277, master's-call Leap 47482, Spirit Wolf Leap
// 58867, and the 48011 visual.
static void TestDonorOnlyRanksExcluded()
{
    CHECK(!IsDisabledPetAutocast(27276));
    CHECK(!IsDisabledPetAutocast(27277));
    CHECK(!IsDisabledPetAutocast(47482));
    CHECK(!IsDisabledPetAutocast(58867));
    CHECK(!IsDisabledPetAutocast(48011));
}

// Working combat spells must stay autocast-on.
static void TestWorkingSpellsEnabled()
{
    CHECK(!IsDisabledPetAutocast(2649));  // Growl
    CHECK(!IsDisabledPetAutocast(17253)); // Bite
    CHECK(!IsDisabledPetAutocast(16827)); // Claw
    CHECK(!IsDisabledPetAutocast(3110));  // Firebolt (imp)
    CHECK(!IsDisabledPetAutocast(3716));  // Torment (voidwalker)
    CHECK(!IsDisabledPetAutocast(17735)); // Suffering (voidwalker)
}

// Toggle rule: desired state is on unless denylisted; change only on mismatch.
static void TestToggleDecisionMatrix()
{
    CHECK(DecidePetAutocast(true, false) == PetAutocastDecision::LeaveAlone);
    CHECK(DecidePetAutocast(false, false) == PetAutocastDecision::Enable);
    CHECK(DecidePetAutocast(true, true) == PetAutocastDecision::Disable);
    CHECK(DecidePetAutocast(false, true) == PetAutocastDecision::LeaveAlone);
}

int main()
{
    std::printf("Starting pet upkeep policy tests...\n");
    TestDonorDenylistKept();
    std::printf("  [PASS] donor denylist kept\n");
    TestAllCowerRanksDisabled();
    std::printf("  [PASS] all Cower ranks disabled\n");
    TestDonorOnlyRanksExcluded();
    std::printf("  [PASS] donor-only ranks excluded\n");
    TestWorkingSpellsEnabled();
    std::printf("  [PASS] working spells stay enabled\n");
    TestToggleDecisionMatrix();
    std::printf("  [PASS] toggle decision matrix\n");
    std::printf("All pet upkeep policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
