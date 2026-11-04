// Standalone regression test for the warlock raid rotation
// (runtime/policy/class/WarlockRaidRotationPolicy.h, key
// AiPlayerbot.WarlockRaidRotation):
//   - no new DoT/curse on a target under 35% health, no spread to an attacker
//     at or below 50%, and at most two extra dotted targets per spread row in
//     a raid;
//   - bosses and the raid-utility curses skip the life gate entirely;
//   - Drain Soul never on a boss and only below two shards;
//   - Life Tap in combat only when two filler casts cannot be paid for.
// Every helper must also answer "today's behaviour" with the key off.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_warlock_raid_rotation_policy.cpp -o /tmp/test_warlock_raid_rotation
//   /tmp/test_warlock_raid_rotation

#include "../runtime/policy/class/WarlockRaidRotationPolicy.h"

#include <cstdint>
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

// The Corruption row: a target at 30% gets no new Corruption with the key on;
// at 60% it does. Key off: the row still fires at 30% (today skips only <=20).
static void TestDotLifeGateOnCurrentTarget()
{
    CHECK(!WarlockDotAllowed(true, 30, false));
    CHECK(WarlockDotAllowed(true, 60, false));
    CHECK(WarlockDotAllowed(true, 35, false));
    CHECK(!WarlockDotAllowed(true, 34, false));
    CHECK(WarlockDotAllowed(false, 30, false));
    CHECK(WarlockDotAllowed(false, 10, false));
}

// The "on attacker" spreads only touch an attacker above 50% with the key on.
static void TestSpreadLifeGate()
{
    CHECK(WarlockDotAllowed(true, 60, true));
    CHECK(!WarlockDotAllowed(true, 50, true));
    CHECK(!WarlockDotAllowed(true, 30, true));
    CHECK(WarlockDotAllowed(false, 30, true));
}

// Any one signal makes a boss: world/raid rank, an instance-binding boss, or
// a boss row of the loaded dungeon plan.
static void TestBossDetection()
{
    CHECK(!WarlockIsBossTarget(false, false, false));
    CHECK(WarlockIsBossTarget(true, false, false));
    CHECK(WarlockIsBossTarget(false, true, false));
    CHECK(WarlockIsBossTarget(false, false, true));
    CHECK(WarlockIsBossTarget(true, true, true));
}

// A boss keeps Corruption/Agony (and the raid-utility curses) on the target
// through its execute phase; trash at the same health gets nothing new. The
// utility exemption is target-independent.
static void TestBossAndUtilityExemption()
{
    CHECK(WarlockDotAllowed(true, 20, false, true));
    CHECK(WarlockDotAllowed(true, 20, true, true));
    CHECK(!WarlockDotAllowed(true, 20, false));
    CHECK(WarlockDotAllowed(true, 20, false, false, true));
    CHECK(WarlockDotAllowed(true, 20, true, false, true));
    CHECK(WarlockDotAllowed(false, 20, false, true));
    CHECK(WarlockDotAllowed(false, 20, false, false, true));
}

// The spread cap only exists for the key in a converted raid group.
static void TestSpreadCapApplies()
{
    CHECK(WarlockSpreadCapApplies(true, true));
    CHECK(!WarlockSpreadCapApplies(true, false));
    CHECK(!WarlockSpreadCapApplies(false, true));
    CHECK(!WarlockSpreadCapApplies(false, false));
}

// The scan walks past a health-gated attacker: a 40% attacker first in scan
// order does not hide the 80% one behind it, and an attacker that already
// carries the aura is never picked.
static void TestSpreadScanSkipsGatedAttacker()
{
    struct Candidate
    {
        std::uint8_t healthPct;
        bool boss;
        bool hasAura;
    };
    Candidate const candidates[] = {{40, false, false}, {80, false, false}};
    auto const picked = WarlockFirstAllowed(candidates, candidates + 2,
        [](Candidate const& c) {
            return WarlockSpreadCandidateAllowed(true, c.healthPct, c.boss, false, c.hasAura);
        });
    CHECK(picked == candidates + 1);
    CHECK(picked->healthPct == 80);
    CHECK(!WarlockSpreadCandidateAllowed(true, 80, false, false, true));
}

// A manual curse order lifts the health line only: the raid cap still blocks
// a third extra target.
static void TestManualCurseLiftsHealthOnly()
{
    CHECK(WarlockSpreadCandidateAllowed(true, 10, false, false, false, true));
    CHECK(!WarlockSpreadCandidateAllowed(true, 10, false, false, false, false));
    // Curse of the Elements is raid utility and already health-exempt.
    CHECK(WarlockSpreadCandidateAllowed(true, 10, false, true, false, false));
    CHECK(!WarlockSpreadAllowed(true, true, WARLOCK_SPREAD_MAX_EXTRA_TARGETS));
    CHECK(WarlockSpreadAllowed(true, true, WARLOCK_SPREAD_MAX_EXTRA_TARGETS - 1));
}

// Raids keep at most two extra dotted targets per spread row; a party and the
// key off never cap.
static void TestRaidSpreadCap()
{
    CHECK(WarlockSpreadAllowed(true, true, 0));
    CHECK(WarlockSpreadAllowed(true, true, 1));
    CHECK(!WarlockSpreadAllowed(true, true, 2));
    CHECK(WarlockSpreadAllowed(true, false, 5));
    CHECK(WarlockSpreadAllowed(false, true, 5));
}

// Drain Soul: shard harvest stays for trash; a boss under 20% is left to
// Shadow Bolt and Shadowburn. Key off keeps today's <5-shard gate everywhere.
static void TestDrainSoulBossGate()
{
    CHECK(!WarlockDrainSoulAllowed(true, true, 3, 15, true));
    CHECK(WarlockDrainSoulAllowed(false, true, 3, 15, true));
    CHECK(WarlockDrainSoulAllowed(true, false, 1, 15, true));
    CHECK(!WarlockDrainSoulAllowed(true, false, 2, 15, true));
    CHECK(WarlockDrainSoulAllowed(true, false, 0, 20, true));
    CHECK(WarlockDrainSoulAllowed(false, false, 4, 20, true));
    CHECK(!WarlockDrainSoulAllowed(true, false, 0, 21, true));
    CHECK(!WarlockDrainSoulAllowed(true, false, 0, 15, false));
}

// Life Tap: with the key on, combat taps only while two Shadow Bolts cannot be
// paid for. An unknown/zero filler cost keeps the mana-line tap.
static void TestCombatTapNeedsTwoFillers()
{
    CHECK(!WarlockCombatTapAllowed(true, 1000, 400));
    CHECK(!WarlockCombatTapAllowed(true, 800, 400));
    CHECK(WarlockCombatTapAllowed(true, 799, 400));
    CHECK(WarlockCombatTapAllowed(true, 1000, 0));
    CHECK(WarlockCombatTapAllowed(false, 1000, 400));
}

int main()
{
    TestDotLifeGateOnCurrentTarget();
    TestSpreadLifeGate();
    TestBossDetection();
    TestBossAndUtilityExemption();
    TestSpreadCapApplies();
    TestSpreadScanSkipsGatedAttacker();
    TestManualCurseLiftsHealthOnly();
    TestRaidSpreadCap();
    TestDrainSoulBossGate();
    TestCombatTapNeedsTwoFillers();

    std::printf("warlock raid rotation policy: %d checks passed\n", checks);
    return 0;
}
