// Standalone regression test for the professions-at-5 rule: every pool bot
// gets its PRIMARY pair at level 5, secondaries at any level, and a bot that
// already holds a primary (or is not a pool bot) is never re-rolled. Also
// pins the Tailoring + Enchanting share: only cloth classes (mage, priest,
// warlock) may roll it, about 1 in 3 of them. Guards the rules that keep
// player characters and hired/owned bots' existing professions safe. This
// pins the exact helper production calls:
// PlayerbotFactory::EnsurePrimaryProfessions (GrantAll gate for the pair
// roll, RollsTailorEnchantPair for the cloth share),
// AutoLearnSpellAction::LearnSpells (GrantAll gate on ding) and
// BotManager::OnPlayerLogin (any non-LeaveAlone enters for secondaries).
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

// Tailoring + Enchanting share (owner decision): only cloth classes (mage 8,
// priest 5, warlock 9) may roll it, and only roll 0 of 3 takes it - about 1
// in 3 eligible bots. Class ids mirror SharedDefines.h (same precedent as
// StarterKitPolicy.h). Pins the exact rule production calls:
// PlayerbotFactory::EnsurePrimaryProfessions (urand(0, DENOMINATOR - 1)).
static void TestTailorEnchantEligibility()
{
    CHECK(IsTailorEnchantClass(5));  // priest
    CHECK(IsTailorEnchantClass(8));  // mage
    CHECK(IsTailorEnchantClass(9));  // warlock
    CHECK(!IsTailorEnchantClass(1)); // warrior
    CHECK(!IsTailorEnchantClass(2)); // paladin
    CHECK(!IsTailorEnchantClass(3)); // hunter
    CHECK(!IsTailorEnchantClass(4)); // rogue
    CHECK(!IsTailorEnchantClass(7)); // shaman
    CHECK(!IsTailorEnchantClass(11)); // druid
}

static void TestTailorEnchantShare()
{
    CHECK(TAILOR_ENCHANT_SHARE_DENOMINATOR == 3);
    // Roll 0 takes it, rolls 1-2 keep the gathering pairs.
    CHECK(RollsTailorEnchantPair(8, 0));
    CHECK(RollsTailorEnchantPair(5, 0));
    CHECK(RollsTailorEnchantPair(9, 0));
    CHECK(!RollsTailorEnchantPair(8, 1));
    CHECK(!RollsTailorEnchantPair(8, 2));
    CHECK(!RollsTailorEnchantPair(5, 1));
    CHECK(!RollsTailorEnchantPair(9, 2));
    // Non-cloth classes never take it on any roll.
    CHECK(!RollsTailorEnchantPair(1, 0));
    CHECK(!RollsTailorEnchantPair(2, 0));
    CHECK(!RollsTailorEnchantPair(3, 0));
    CHECK(!RollsTailorEnchantPair(4, 0));
    CHECK(!RollsTailorEnchantPair(7, 0));
    CHECK(!RollsTailorEnchantPair(11, 0));
}

int main()
{
    TestBelowGateSecondariesOnly();
    TestAtGateGrantAll();
    TestExistingPrimariesNeverTouched();
    TestNotPool();
    TestTrainerParity();
    TestThresholdValue();
    TestTailorEnchantEligibility();
    TestTailorEnchantShare();
    std::printf("profession grant policy: %d checks passed\n", checks);
    return 0;
}
