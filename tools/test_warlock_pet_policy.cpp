// Standalone regression test for the solo warlock default-pet rule
// (runtime/WarlockPetPolicy.h): once a pool bot knows Summon Voidwalker
// (697) AND holds a Soul Shard (6265, the summon reagent) the upkeep summons
// the Voidwalker; below that it keeps the Imp, and a shardless bot stays
// quiet instead of queueing a summon that fails isPossible. Owned/hired
// bots (live real-player master) are never touched.
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

static WarlockSoloPetInputs Solo(bool knowsVW, bool hasShard, bool hasPet, uint32_t entry)
{
    WarlockSoloPetInputs inputs;
    inputs.knowsVoidwalker = knowsVW;
    inputs.hasSoulShard = hasShard;
    inputs.hasPet = hasPet;
    inputs.currentPetEntry = entry;
    return inputs;
}

// Petless lvl10+ pool bot that knows the summon and holds a shard: summon.
static void TestPetlessSummonsVoidwalker()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, false, 0)) == WarlockSoloPetDecision::SummonVoidwalker);
}

// Imp running past the tier with a shard: upgrade to the Voidwalker.
static void TestImpUpgrades()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, true, WARLOCK_IMP_PET_ENTRY)) ==
        WarlockSoloPetDecision::SummonVoidwalker);
}

// Voidwalker already out: leave it alone (no resummon loop).
static void TestVoidwalkerLeftAlone()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, true, WARLOCK_VOIDWALKER_PET_ENTRY)) ==
        WarlockSoloPetDecision::LeaveAlone);
}

// Below the tier (no 697): the Imp stays, petless stays petless.
static void TestBelowTierKeepsImp()
{
    CHECK(DecideWarlockSoloPet(Solo(false, true, true, WARLOCK_IMP_PET_ENTRY)) ==
        WarlockSoloPetDecision::LeaveAlone);
    CHECK(DecideWarlockSoloPet(Solo(false, true, false, 0)) == WarlockSoloPetDecision::LeaveAlone);
}

// Shardless at/above the tier: stay quiet so no failed summon is queued
// (petless falls back to the "no pet" summon-imp node instead).
static void TestShardlessStaysQuiet()
{
    CHECK(DecideWarlockSoloPet(Solo(true, false, true, WARLOCK_IMP_PET_ENTRY)) ==
        WarlockSoloPetDecision::LeaveAlone);
    CHECK(DecideWarlockSoloPet(Solo(true, false, false, 0)) == WarlockSoloPetDecision::LeaveAlone);
}

// Exotic current pet (Succubus/Felhunter from a manual order): never override.
static void TestExoticPetLeftAlone()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, true, 1863)) == WarlockSoloPetDecision::LeaveAlone);
    CHECK(DecideWarlockSoloPet(Solo(true, true, true, 417)) == WarlockSoloPetDecision::LeaveAlone);
}

// Owned/hired bot with a live master: the player decides, always.
static void TestOwnedBotLeftAlone()
{
    WarlockSoloPetInputs inputs = Solo(true, true, false, 0);
    inputs.hasRealPlayerMaster = true;
    CHECK(DecideWarlockSoloPet(inputs) == WarlockSoloPetDecision::LeaveAlone);
    inputs = Solo(true, true, true, WARLOCK_IMP_PET_ENTRY);
    inputs.hasRealPlayerMaster = true;
    CHECK(DecideWarlockSoloPet(inputs) == WarlockSoloPetDecision::LeaveAlone);
}

// Pin the server-data constants so a stray edit faces this test.
static void TestConstants()
{
    CHECK(WARLOCK_VOIDWALKER_SUMMON_SPELL == 697);
    CHECK(WARLOCK_VOIDWALKER_PET_ENTRY == 1860);
    CHECK(WARLOCK_IMP_PET_ENTRY == 416);
    CHECK(WARLOCK_SOUL_SHARD_ITEM == 6265);
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
    TestShardlessStaysQuiet();
    std::printf("  [PASS] shardless stays quiet\n");
    TestExoticPetLeftAlone();
    std::printf("  [PASS] exotic pet left alone\n");
    TestOwnedBotLeftAlone();
    std::printf("  [PASS] owned bot left alone\n");
    TestConstants();
    std::printf("  [PASS] constants\n");
    std::printf("All warlock pet policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
