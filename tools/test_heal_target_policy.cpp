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
using ai::FocusedHealerMayHelpRaid;

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

// A focused tank healer whose own targets are all above "almost full" may heal
// a raid member below 70%; with the seat key off it never leaves its seat.
static void TestTankHealerHelpsRaid()
{
    CHECK(FocusedHealerMayHelpRaid(true, 95.0f, 50.0f, 90.0f));
    CHECK(FocusedHealerMayHelpRaid(true, 90.0f, 50.0f, 90.0f));
    // Its own tank is not full: the seat still comes first.
    CHECK(!FocusedHealerMayHelpRaid(true, 89.0f, 20.0f, 90.0f));
    // A raid member at or above 70% is not an emergency.
    CHECK(!FocusedHealerMayHelpRaid(true, 95.0f, 70.0f, 90.0f));
    CHECK(FocusedHealerMayHelpRaid(true, 95.0f, 69.0f, 90.0f));
    // Key off: today's behaviour, the focus targets only.
    CHECK(!FocusedHealerMayHelpRaid(false, 100.0f, 10.0f, 90.0f));
}

int main()
{
    std::printf("Starting TortoiseBots heal target policy tests...\n");
    TestDeadPetIsNotHealable();
    TestAlivePetIsHealable();
    TestMiniPetIsNeverHealable();
    TestDeadTargetIsNotHealable();
    TestTankHealerHelpsRaid();
    std::printf("  [PASS] %d checks\n", checks);
    return 0;
}
