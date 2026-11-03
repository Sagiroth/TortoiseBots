// Standalone regression test for the alive gates of "party member to heal"
// (ai/playerbot/HealTargetPolicy.h).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_heal_target_policy.cpp -o /tmp/test_heal_target
//   /tmp/test_heal_target

#include "../ai/playerbot/HealTargetPolicy.h"

#include <cstdio>
#include <cstdlib>

using ai::HealablePet;
using ai::HealableTarget;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestDeadPetIsNotHealable()
{
    CHECK(!HealablePet(false, false));
}

static void TestAlivePetIsHealable()
{
    CHECK(HealablePet(false, true));
}

static void TestMiniPetIsNeverHealable()
{
    CHECK(!HealablePet(true, true));
    CHECK(!HealablePet(true, false));
}

// A corpse at 0% health passes the branches' below-full-health test, so the
// alive check is the only thing keeping the healer off the body.
static void TestDeadTargetIsNotHealable()
{
    CHECK(!HealableTarget(false));
    CHECK(HealableTarget(true));
}

int main()
{
    std::printf("Starting TortoiseBots heal target policy tests...\n");
    TestDeadPetIsNotHealable();
    TestAlivePetIsHealable();
    TestMiniPetIsNeverHealable();
    TestDeadTargetIsNotHealable();
    std::printf("  [PASS] %d checks\n", checks);
    return 0;
}
