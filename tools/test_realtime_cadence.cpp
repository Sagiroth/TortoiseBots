// The live position cadence (task S3/R3m): the two periods clamp to 250..2000 ms, an idle period of 2000 ms means no
// out-of-combat positions at all (the 2 s COMMANDERS report is the idle source), and the clock fires combat and idle
// sends on their own accumulators, resetting each one when it fires. Plain data and the standard library only.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_realtime_cadence.cpp -o /tmp/realtime_cadence
//   /tmp/realtime_cadence

#include "../runtime/RealtimeCadence.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots::RealtimeCadence;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestClamp()
{
    CHECK(Clamp(100, 100).combatMs == 250);   // under the floor: 250
    CHECK(Clamp(100, 100).idleMs == 250);
    CHECK(Clamp(5000, 5000).combatMs == 2000); // over the ceiling: 2000
    CHECK(Clamp(5000, 5000).idleMs == 2000);
    CHECK(Clamp(500, 2000).combatMs == 500);   // the shipped defaults stay as they are
    CHECK(Clamp(500, 2000).idleMs == 2000);
    CHECK(Clamp(250, 1500).combatMs == 250 && Clamp(250, 1500).idleMs == 1500);
}

static void TestIdleSends()
{
    CHECK(!IdleSends(Cadence{500, 2000})); // 2000: the 2 s report is the idle source
    CHECK(IdleSends(Cadence{500, 1999}));
    CHECK(IdleSends(Cadence{500, 1000}));
    CHECK(IdleSends(Cadence{500, 250}));
}

static void TestCombatClock()
{
    Clock clock;
    clock.Set(Cadence{500, 2000});
    for (int i = 1; i < 10; ++i)
        CHECK(!clock.CombatDue(50)); // 450 ms: not yet
    CHECK(clock.CombatDue(50));      // 500 ms: due
    for (int i = 1; i < 10; ++i)
        CHECK(!clock.CombatDue(50));
    CHECK(clock.CombatDue(50));      // again 500 ms later
}

static void TestIdleClock()
{
    Clock clock;
    clock.Set(Cadence{500, 1000});
    CHECK(!clock.IdleDue(500));
    CHECK(clock.IdleDue(500)); // 1000 ms: due
    CHECK(!clock.IdleDue(500));
    CHECK(clock.IdleDue(500));
}

int main()
{
    TestClamp();
    TestIdleSends();
    TestCombatClock();
    TestIdleClock();
    std::printf("test_realtime_cadence: %d checks passed\n", checks);
    return EXIT_SUCCESS;
}
