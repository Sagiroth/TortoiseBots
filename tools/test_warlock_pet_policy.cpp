// Standalone regression test for the solo warlock default-pet rule
// (runtime/WarlockPetPolicy.h): once a pool bot knows Summon Voidwalker
// (697) the upkeep summons the Voidwalker; below that it keeps the Imp.
// Owned/hired bots (live real-player master) are never touched.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_warlock_pet_policy.cpp -o /tmp/test_warlock_pet
//   /tmp/test_warlock_pet

#include "../runtime/WarlockPetPolicy.h"

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

static WarlockSoloPetInputs Solo(bool knowsVW, bool hasPet, uint32_t entry)
{
    WarlockSoloPetInputs inputs;
    inputs.knowsVoidwalker = knowsVW;
    inputs.hasPet = hasPet;
    inputs.currentPetEntry = entry;
    return inputs;
}

// Petless lvl10+ pool bot that knows the summon: summon the Voidwalker.
static void TestPetlessSummonsVoidwalker()
{
    CHECK(DecideWarlockSoloPet(Solo(true, false, 0)) == WarlockSoloPetDecision::SummonVoidwalker);
}

// Imp running past the tier: upgrade to the Voidwalker.
static void TestImpUpgrades()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, WARLOCK_IMP_PET_ENTRY)) ==
        WarlockSoloPetDecision::SummonVoidwalker);
}

// Voidwalker already out: leave it alone (no resummon loop).
static void TestVoidwalkerLeftAlone()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, WARLOCK_VOIDWALKER_PET_ENTRY)) ==
        WarlockSoloPetDecision::LeaveAlone);
}

// Below the tier (no 697): the Imp stays, petless stays petless.
static void TestBelowTierKeepsImp()
{
    CHECK(DecideWarlockSoloPet(Solo(false, true, WARLOCK_IMP_PET_ENTRY)) ==
        WarlockSoloPetDecision::LeaveAlone);
    CHECK(DecideWarlockSoloPet(Solo(false, false, 0)) == WarlockSoloPetDecision::LeaveAlone);
}

// Exotic current pet (Succubus/Felhunter from a manual order): never override.
static void TestExoticPetLeftAlone()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, 1863)) == WarlockSoloPetDecision::LeaveAlone);
    CHECK(DecideWarlockSoloPet(Solo(true, true, 417)) == WarlockSoloPetDecision::LeaveAlone);
}

// Owned/hired bot with a live master: the player decides, always.
static void TestOwnedBotLeftAlone()
{
    WarlockSoloPetInputs inputs = Solo(true, false, 0);
    inputs.hasRealPlayerMaster = true;
    CHECK(DecideWarlockSoloPet(inputs) == WarlockSoloPetDecision::LeaveAlone);
    inputs = Solo(true, true, WARLOCK_IMP_PET_ENTRY);
    inputs.hasRealPlayerMaster = true;
    CHECK(DecideWarlockSoloPet(inputs) == WarlockSoloPetDecision::LeaveAlone);
}

// Pin the server-data constants so a stray edit faces this test.
static void TestConstants()
{
    CHECK(WARLOCK_VOIDWALKER_SUMMON_SPELL == 697);
    CHECK(WARLOCK_VOIDWALKER_PET_ENTRY == 1860);
    CHECK(WARLOCK_IMP_PET_ENTRY == 416);
}

int main()
{
    std::printf("Starting warlock pet policy tests...\n");
    TestPetlessSummonsVoidwalker();
    std::printf("  [PASS] petless summons voidwalker\n");
    TestImpUpgrades();
    std::printf("  [PASS] imp upgrades\n");
    TestVoidwalkerLeftAlone();
    std::printf("  [PASS] voidwalker left alone\n");
    TestBelowTierKeepsImp();
    std::printf("  [PASS] below tier keeps imp\n");
    TestExoticPetLeftAlone();
    std::printf("  [PASS] exotic pet left alone\n");
    TestOwnedBotLeftAlone();
    std::printf("  [PASS] owned bot left alone\n");
    TestConstants();
    std::printf("  [PASS] constants\n");
    std::printf("All warlock pet policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
