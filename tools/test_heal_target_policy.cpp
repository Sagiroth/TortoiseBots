// Standalone regression test for the alive gates of "party member to heal"
// (ai/playerbot/HealTargetPolicy.h).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_heal_target_policy.cpp -o /tmp/test_heal_target
//   /tmp/test_heal_target

#include "../ai/playerbot/HealTargetPolicy.h"

#include <cstdio>
#include <cstdlib>

using ai::FocusListIsLayoutAssignment;
using ai::HealablePet;
using ai::HealableTarget;
using ai::RaidAssistCandidate;
using ai::TankSeatQuiet;

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

// Two healers on the main tank, both quiet: the tank's first seat never
// leaves, its second does, so exactly one of them helps the raid member.
static void TestOnlyASecondTankSeatHelps()
{
    CHECK(!TankSeatQuiet(true, 0, true, true, false, 100.0f));
    CHECK(TankSeatQuiet(true, 1, true, true, false, 100.0f));
}

// The quiet test: the tank found, alive, not on a boss, and at or above 90%
// health after the damage it is taking. A missing seat or focus target is a
// missing answer at the call site (tankFound false).
static void TestQuietTankSeat()
{
    CHECK(TankSeatQuiet(true, 1, true, true, false, 95.0f));
    CHECK(TankSeatQuiet(true, 1, true, true, false, 90.0f));
    CHECK(!TankSeatQuiet(true, 1, true, true, false, 89.0f));
    CHECK(!TankSeatQuiet(true, 1, true, true, true, 100.0f));
    CHECK(!TankSeatQuiet(true, 1, false, true, false, 100.0f));
    CHECK(!TankSeatQuiet(true, 1, true, false, false, 100.0f));
    CHECK(!TankSeatQuiet(false, 1, true, true, false, 100.0f));
}

// The raid member the quiet seat may heal: in sight and inside heal range,
// below 70%.
static void TestAssistNeedsSightAndRange()
{
    CHECK(RaidAssistCandidate(true, true, true, 50.0f));
    CHECK(!RaidAssistCandidate(true, true, true, 70.0f));
    CHECK(!RaidAssistCandidate(false, true, true, 10.0f));
    CHECK(!RaidAssistCandidate(true, false, true, 10.0f));
    CHECK(!RaidAssistCandidate(true, true, false, 10.0f));
}

// The layout owns a seat's focus list only while it is exactly the assigned
// set: a player who extends it (or drops a member) keeps the tank healer but
// the seat no longer helps the raid.
static void TestFocusListMustBeTheLayoutAssignment()
{
    std::vector<std::uint64_t> const tankHealer = {11, 22}; // the tank, the healer itself
    CHECK(FocusListIsLayoutAssignment(tankHealer, tankHealer));
    CHECK(FocusListIsLayoutAssignment({22, 11}, tankHealer));
    CHECK(!FocusListIsLayoutAssignment({11, 22, 33}, tankHealer)); // a member added
    CHECK(!FocusListIsLayoutAssignment({11}, tankHealer));         // the healer itself dropped
    CHECK(!FocusListIsLayoutAssignment({33, 22}, tankHealer));     // tank replaced
    CHECK(!FocusListIsLayoutAssignment({11, 11}, tankHealer));     // the healer's guid stolen for a duplicate
    CHECK(!FocusListIsLayoutAssignment({}, tankHealer));
    CHECK(!FocusListIsLayoutAssignment({}, {}));                   // no layout seat, nothing owned
}

int main()
{
    std::printf("Starting TortoiseBots heal target policy tests...\n");
    TestDeadPetIsNotHealable();
    TestAlivePetIsHealable();
    TestMiniPetIsNeverHealable();
    TestDeadTargetIsNotHealable();
    TestOnlyASecondTankSeatHelps();
    TestQuietTankSeat();
    TestAssistNeedsSightAndRange();
    TestFocusListMustBeTheLayoutAssignment();
    std::printf("  [PASS] %d checks\n", checks);
    return 0;
}
