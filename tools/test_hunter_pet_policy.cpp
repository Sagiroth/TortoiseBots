// Standalone regression test for the below-threshold hunter pet rule: a hunter
// cannot own a pet below level 10 (tw_world.spell_template: Tame Beast 1515 and
// the Call/Revive/Feed pet kit all carry baseLevel = spellLevel = 10), and the
// login cleanup may only ever drop the pet of an unclaimed pool bot. Guards the
// rules that keep player characters, hired companions and adopted party bots
// safe.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_hunter_pet_policy.cpp -o /tmp/test_hunter_pet
//   /tmp/test_hunter_pet

#include "../runtime/HunterPetPolicy.h"

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

static HunterPetLoginInputs PoolHunter(uint32_t level, bool hasPet)
{
    HunterPetLoginInputs inputs;
    inputs.isPoolBot = true;
    inputs.isHunter = true;
    inputs.level = level;
    inputs.hasPet = hasPet;
    return inputs;
}

// The one case the cleanup exists for: an unclaimed pool hunter below the
// threshold that still carries a seeded pet row.
static void TestUnclaimedPoolHunterBelowThreshold()
{
    CHECK(DecideHunterPetOnLogin(PoolHunter(1, true)) == HunterPetLoginDecision::DropPetBelowThreshold);
    CHECK(DecideHunterPetOnLogin(PoolHunter(HUNTER_PET_MIN_LEVEL - 1, true)) ==
        HunterPetLoginDecision::DropPetBelowThreshold);
}

// At the threshold the pet is legal: never touched, every level above included.
static void TestThresholdAndAboveAreLeftAlone()
{
    CHECK(DecideHunterPetOnLogin(PoolHunter(HUNTER_PET_MIN_LEVEL, true)) == HunterPetLoginDecision::LeaveAlone);
    CHECK(DecideHunterPetOnLogin(PoolHunter(60, true)) == HunterPetLoginDecision::LeaveAlone);
}

// A petless pool hunter has nothing to drop.
static void TestPetlessPoolHunter()
{
    CHECK(DecideHunterPetOnLogin(PoolHunter(1, false)) == HunterPetLoginDecision::LeaveAlone);
}

// A hired companion is not an unclaimed pool bot (record.random is true for
// hires too); its pet is never dropped, even while its master is offline.
static void TestHiredCompanion()
{
    HunterPetLoginInputs inputs = PoolHunter(5, true);
    inputs.isHiredCompanion = true;
    CHECK(DecideHunterPetOnLogin(inputs) == HunterPetLoginDecision::LeaveAlone);
}

// Owner-account characters and adopted party bots are not pool bots: the
// classification already keeps them out, and the policy must too.
static void TestNotPool()
{
    HunterPetLoginInputs inputs = PoolHunter(1, true);
    inputs.isPoolBot = false;
    CHECK(DecideHunterPetOnLogin(inputs) == HunterPetLoginDecision::LeaveAlone);
}

// Other classes are never touched.
static void TestOtherClasses()
{
    HunterPetLoginInputs inputs = PoolHunter(1, true);
    inputs.isHunter = false;
    CHECK(DecideHunterPetOnLogin(inputs) == HunterPetLoginDecision::LeaveAlone);
}

// The threshold itself is server data, not a tunable: pin it so a stray edit
// has to face this test.
static void TestThresholdValue()
{
    CHECK(HUNTER_PET_MIN_LEVEL == 10);
}

int main()
{
    TestUnclaimedPoolHunterBelowThreshold();
    TestThresholdAndAboveAreLeftAlone();
    TestPetlessPoolHunter();
    TestHiredCompanion();
    TestNotPool();
    TestOtherClasses();
    TestThresholdValue();
    std::printf("hunter pet policy: %d checks passed\n", checks);
    return 0;
}
