// Standalone policy test for the mount/dismount-loop fix (#1097,
// ai/playerbot/MountDismountPolicy.h). Runtime proof (headless GM observer,
// Oct 2026): each mount cast lands (spell_go + aura_applied) and the aura is
// removed 1-4 s later with the bot unmoved - by buff upkeep (Blessing of
// Wisdom re-cast on the dismount tick) or by the combat dismount rule on a
// dps pick the travelling bot never engages - then the next tick casts
// again. The motion gate (ca0af148, #553) stays: the fix is on the dismount
// side, in two rules plus a stable mount choice.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_mount_dismount_policy.cpp -o /tmp/test_mount_dismount_policy
//   /tmp/test_mount_dismount_policy

#include "../ai/playerbot/MountDismountPolicy.h"

#include <cstdio>
#include <cstdlib>

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// (1) The loop shape: travelling bot, not in combat, dps pick nearby that
// fights nobody and no order on it. Must NOT dismount.
static void TestUnengagedPickKeepsMount()
{
    CHECK(!ai::CombatDismountAllowed(false, false, false));
}

// A real fight dismounts: bot in combat, or the target fights the bot, or
// an explicit attack/pull order names it.
static void TestRealFightDismounts()
{
    CHECK(ai::CombatDismountAllowed(true, false, false));
    CHECK(ai::CombatDismountAllowed(false, true, false));
    CHECK(ai::CombatDismountAllowed(false, false, true));
}

// (2) Out-of-combat casts wait while mounted and travelling; combat,
// and non-travelling bots, keep today's behavior. The gate lives in two
// places with the same condition: CheckCanCastSpell (Unit spell-id
// overload), which every bot cast funnels through on the resolved spell id,
// and the buff trigger gate ai::BuffTriggerMountGated, which keeps the
// trigger from queueing a cast the cast gate would refuse with
// SPELL_FAILED_NOT_MOUNTED (~78k refusals / 474 bots on 2026-11-01).
// Blessing actions override isUseful/isPossible without calling the base
// and resolve their spell dynamically, which is why the first gate had to
// move to cast time; the trigger gate covers the queueing side for the
// whole BuffTrigger family.
static void TestMountedTravelDefersCasts()
{
    CHECK(!ai::MountedTravelCastAllowed(false, true));
    CHECK(ai::MountedTravelCastAllowed(true, true));
    CHECK(ai::MountedTravelCastAllowed(false, false));
}

// (3) Stable per-bot choice: equal-speed ties break by bot guid + spell id,
// so one bot always rides the same mount while bots differ from each other.
static void TestStableMountChoice()
{
    // Same bot, same candidates: same answer every time, both orders.
    CHECK(ai::StableMountTieBreak(7u, 13819u) == ai::StableMountTieBreak(7u, 13819u));
    // Different bots can differ on the same pair (variety preserved).
    bool anyDiffer = false;
    for (unsigned guid = 1u; guid < 32u; ++guid)
        if (ai::StableMountTieBreak(guid, 13819u) != ai::StableMountTieBreak(guid, 46449u))
            anyDiffer = true;
    CHECK(anyDiffer);
    // Ordering is total: for any bot, one of the pair wins deterministically.
    for (unsigned guid = 1u; guid < 32u; ++guid)
    {
        bool aFirst = ai::StableMountTieBreak(guid, 13819u) < ai::StableMountTieBreak(guid, 46449u);
        bool bFirst = ai::StableMountTieBreak(guid, 46449u) < ai::StableMountTieBreak(guid, 13819u);
        CHECK(aFirst != bFirst);
    }
}
int main()
{
    std::printf("Starting mount-dismount policy tests...\n");
    TestUnengagedPickKeepsMount();
    std::printf("  [PASS] unengaged pick keeps mount\n");
    TestRealFightDismounts();
    std::printf("  [PASS] real fight dismounts\n");
    TestMountedTravelDefersCasts();
    std::printf("  [PASS] mounted travel defers casts\n");
    TestStableMountChoice();
    std::printf("  [PASS] stable per-bot mount choice\n");
    std::printf("All mount-dismount policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
