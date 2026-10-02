// Standalone regression test for the professions-at-5 rule: every pool bot
// gets its PRIMARY pair at level 5, secondaries at any level, and a bot that
// already holds a primary (or is not a pool bot) is never re-rolled. Guards
// the rules that keep player characters and hired/owned bots' existing
// professions safe.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_profession_grant_policy.cpp -o /tmp/test_profession_grant
//   /tmp/test_profession_grant

#include "../runtime/ProfessionGrantPolicy.h"

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

static ProfessionGrantInputs PoolBot(uint32_t level, bool hasPrimary)
{
    ProfessionGrantInputs inputs;
    inputs.level = level;
    inputs.isPoolBot = true;
    inputs.hasPrimaryProfession = hasPrimary;
    return inputs;
}

// Below the gate a pool bot without primaries gets secondaries only.
static void TestBelowGateSecondariesOnly()
{
    CHECK(DecideProfessionGrant(PoolBot(1, false)) == ProfessionGrantDecision::SecondariesOnly);
    CHECK(DecideProfessionGrant(PoolBot(4, false)) == ProfessionGrantDecision::SecondariesOnly);
}

// At the gate the pair is granted.
static void TestAtGateGrantAll()
{
    CHECK(DecideProfessionGrant(PoolBot(5, false)) == ProfessionGrantDecision::GrantAll);
    CHECK(DecideProfessionGrant(PoolBot(6, false)) == ProfessionGrantDecision::GrantAll);
    CHECK(DecideProfessionGrant(PoolBot(60, false)) == ProfessionGrantDecision::GrantAll);
}

// A bot that already holds a primary is never re-rolled, at any level.
static void TestExistingPrimariesNeverTouched()
{
    CHECK(DecideProfessionGrant(PoolBot(1, true)) == ProfessionGrantDecision::LeaveAlone);
    CHECK(DecideProfessionGrant(PoolBot(4, true)) == ProfessionGrantDecision::LeaveAlone);
    CHECK(DecideProfessionGrant(PoolBot(5, true)) == ProfessionGrantDecision::LeaveAlone);
    CHECK(DecideProfessionGrant(PoolBot(60, true)) == ProfessionGrantDecision::LeaveAlone);
}

// Non-pool bots (player characters, owned alts) are never touched.
static void TestNotPool()
{
    ProfessionGrantInputs owned;
    owned.level = 60;
    owned.isPoolBot = false;
    owned.hasPrimaryProfession = false;
    CHECK(DecideProfessionGrant(owned) == ProfessionGrantDecision::LeaveAlone);
    owned.level = 1;
    owned.hasPrimaryProfession = true;
    CHECK(DecideProfessionGrant(owned) == ProfessionGrantDecision::LeaveAlone);
}

// Trainer parity: rank-1 professions become trainable from the same gate.
static void TestTrainerParity()
{
    CHECK(!IsRankOneProfessionTrainable(1));
    CHECK(!IsRankOneProfessionTrainable(4));
    CHECK(IsRankOneProfessionTrainable(5));
    CHECK(IsRankOneProfessionTrainable(10));
    CHECK(IsRankOneProfessionTrainable(60));
}

// The gate itself is an owner decision, not a tunable: pin it.
static void TestThresholdValue()
{
    CHECK(PRIMARY_PROFESSION_MIN_LEVEL == 5);
}

int main()
{
    TestBelowGateSecondariesOnly();
    TestAtGateGrantAll();
    TestExistingPrimariesNeverTouched();
    TestNotPool();
    TestTrainerParity();
    TestThresholdValue();
    std::printf("profession grant policy: %d checks passed\n", checks);
    return 0;
}
