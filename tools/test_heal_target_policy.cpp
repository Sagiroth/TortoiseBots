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
using ai::MemberNeedsHeal;
using ai::PetNeedsHeal;

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

// Bug 1: a pet is a candidate only when hurt AND not already being healed.
// The old "||" admitted a full-health pet and a pet someone was healing.
static void TestPetNeedsHealAndNotHealed()
{
    auto healed = []() { return true; };
    auto unhealed = []() { return false; };
    CHECK(!PetNeedsHeal(100.0f, 90.0f, unhealed));  // full health, nobody casting: excluded
    CHECK(!PetNeedsHeal(95.0f, 90.0f, unhealed));
    CHECK(!PetNeedsHeal(50.0f, 90.0f, healed));     // hurt but already being healed: excluded
    CHECK(!PetNeedsHeal(100.0f, 90.0f, healed));
    CHECK(PetNeedsHeal(50.0f, 90.0f, unhealed));    // hurt, nobody healing: candidate
}

// Bug 2: the "already being healed" filter applies only above the critical band.
static void TestMemberNeedsHealCriticalStaysCandidate()
{
    auto healed = []() { return true; };
    auto unhealed = []() { return false; };
    // 15 % with one incoming heal is still a candidate for a second healer.
    CHECK(MemberNeedsHeal(15.0f, 90.0f, 20.0f, healed));
    CHECK(MemberNeedsHeal(20.0f, 90.0f, 20.0f, healed));  // the line itself is critical
    // 80 % with an incoming heal is not.
    CHECK(!MemberNeedsHeal(80.0f, 90.0f, 20.0f, healed));
    CHECK(!MemberNeedsHeal(21.0f, 90.0f, 20.0f, healed));
    // Nobody healing: hurt members are candidates, healthy ones are not.
    CHECK(MemberNeedsHeal(80.0f, 90.0f, 20.0f, unhealed));
    CHECK(MemberNeedsHeal(15.0f, 90.0f, 20.0f, unhealed));
    CHECK(!MemberNeedsHeal(90.0f, 90.0f, 20.0f, unhealed));
    CHECK(!MemberNeedsHeal(100.0f, 90.0f, 20.0f, healed));
}

// The "being healed" lookup is a scan: it must not run when the answer is fixed.
static void TestBeingHealedLookupIsLazy()
{
    int calls = 0;
    auto counted = [&calls]() { ++calls; return true; };
    MemberNeedsHeal(15.0f, 90.0f, 20.0f, counted);   // critical: no lookup
    MemberNeedsHeal(95.0f, 90.0f, 20.0f, counted);   // healthy: no lookup
    PetNeedsHeal(95.0f, 90.0f, counted);              // healthy pet: no lookup
    CHECK(calls == 0);
    MemberNeedsHeal(80.0f, 90.0f, 20.0f, counted);
    CHECK(calls == 1);
}

int main()
{
    std::printf("Starting TortoiseBots heal target policy tests...\n");
    TestDeadPetIsNotHealable();
    TestAlivePetIsHealable();
    TestMiniPetIsNeverHealable();
    TestDeadTargetIsNotHealable();
    TestPetNeedsHealAndNotHealed();
    TestMemberNeedsHealCriticalStaysCandidate();
    TestBeingHealedLookupIsLazy();
    std::printf("  [PASS] %d checks\n", checks);
    return 0;
}
