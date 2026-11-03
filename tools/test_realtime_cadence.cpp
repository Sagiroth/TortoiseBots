// The live position cadence (task S3/R3m): the two periods clamp to 250..2000 ms, an idle period of 2000 ms means no
// out-of-combat positions at all (the 2 s COMMANDERS report is the idle source), and one world tick advances both
// accumulators independently, so neither period can hold the other back. Plain data and the standard library only.
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

static void TestCombatAccumulator()
{
    Clock clock;
    clock.Set(Cadence{500, 2000});
    for (int i = 1; i <= 9; ++i)
        CHECK(!clock.Advance(50).combat); // 450 ms: not yet
    CHECK(clock.Advance(50).combat);      // 500 ms: due
    for (int i = 1; i <= 9; ++i)
        CHECK(!clock.Advance(50).combat);
    CHECK(clock.Advance(50).combat);      // again 500 ms later
}

static void TestIdleAccumulator()
{
    Clock clock;
    clock.Set(Cadence{2000, 1000});
    for (int i = 1; i <= 19; ++i)
        CHECK(!clock.Advance(50).idle); // 950 ms: not yet
    CHECK(clock.Advance(50).idle);      // 1000 ms: due
}

static void TestIdleAtTwoThousandNeverFires()
{
    Clock clock;
    clock.Set(Cadence{500, 2000});
    int idle = 0;
    for (int i = 0; i < 200; ++i)
        idle += clock.Advance(50).idle;
    CHECK(idle == 0); // at 2000 the 2 s report is the idle source
}

// The emitter's tick loop (ObservabilityEmitter::Update): one Advance per world tick, and the provider runs once when
// either period is due. These are the send counts over 10 s of realistic 50 ms ticks, so a period that only advances
// inside the other period's gate fails here: at combat 2000 / idle 500 the idle period would never fire in 10 s, and at
// combat 500 / idle 1000 its first send would come at 10 s instead of 1 s.
static void TestFeedTickLoop()
{
    struct Expected
    {
        Cadence cadence;
        int combatSends;
        int idleSends;
        int totalSends;  // one provider call per tick when either is due
        int firstIdleTick; // -1: the idle period never fires
    };
    Expected const cases[] = {
        {Cadence{500, 2000}, 20, 0, 20, -1},
        {Cadence{2000, 500}, 5, 20, 20, 9},
        {Cadence{500, 1000}, 20, 10, 20, 19},
    };
    for (Expected const& expected : cases)
    {
        Clock clock;
        clock.Set(expected.cadence);
        int combat = 0;
        int idle = 0;
        int sends = 0;
        int firstIdle = -1;
        for (int tick = 0; tick < 200; ++tick)
        {
            Due const due = clock.Advance(50);
            combat += due.combat ? 1 : 0;
            idle += due.idle ? 1 : 0;
            if (due.idle && firstIdle < 0)
                firstIdle = tick;
            if (due.Any())
                ++sends;
        }
        CHECK(combat == expected.combatSends);
        CHECK(idle == expected.idleSends);
        CHECK(sends == expected.totalSends);
        CHECK(firstIdle == expected.firstIdleTick);
    }
}

int main()
{
    TestClamp();
    TestIdleSends();
    TestCombatAccumulator();
    TestIdleAccumulator();
    TestIdleAtTwoThousandNeverFires();
    TestFeedTickLoop();
    std::printf("test_realtime_cadence: %d checks passed\n", checks);
    return EXIT_SUCCESS;
}
