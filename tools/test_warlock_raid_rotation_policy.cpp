// Standalone regression test for the warlock raid rotation
// (runtime/policy/class/WarlockRaidRotationPolicy.h, key
// AiPlayerbot.WarlockRaidRotation):
//   - no new DoT/curse on a target under 35% health, no spread to an attacker
//     at or below 50%, and at most two extra dotted targets in a raid;
//   - Drain Soul never on a boss and only below two shards.
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

// Raids keep at most two extra dotted targets; a party and the key off never cap.
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

int main()
{
    TestDotLifeGateOnCurrentTarget();
    TestSpreadLifeGate();
    TestRaidSpreadCap();
    TestDrainSoulBossGate();

    std::printf("warlock raid rotation policy: %d checks passed\n", checks);
    return 0;
}
