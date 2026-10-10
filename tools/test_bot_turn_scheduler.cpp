// Standalone regression test for the random-pool turn scheduler
// (runtime/BotTurnScheduler.h). Exercises the rules that must not regress:
// a turn is due at the AI's own delay but never later than the situation
// allows, the due check survives the world clock wrapping, due bots are
// served most overdue relative to their tolerance, and in a simulated
// overloaded pool no bot starves while fights stay ahead of idle walking.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_bot_turn_scheduler.cpp -o /tmp/test_bot_turn_scheduler
//   /tmp/test_bot_turn_scheduler

#include "../runtime/BotTurnScheduler.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestNextDueCapsTheAiDelay()
{
    // The AI's own delay wins while it is shorter than the situation's cap.
    CHECK(TurnNextDueMs(1000, 100, TurnSituation::Combat) == 1100);
    CHECK(TurnNextDueMs(1000, 1000, TurnSituation::Normal) == 2000);
    CHECK(TurnNextDueMs(1000, 0, TurnSituation::Normal) == 1000);
    // A long wait (GCD, cast, passive delay) is cut to the cap, so reactions
    // still run in a fight and an idle bot still looks around.
    CHECK(TurnNextDueMs(1000, 1500, TurnSituation::Combat) == 1400);
    CHECK(TurnNextDueMs(1000, 10000, TurnSituation::Normal) == 4000);
    CHECK(TurnNextDueMs(1000, 5000, TurnSituation::Dead) == 2000);
    CHECK(TurnNextDueMs(1000, 5000, TurnSituation::Battleground) == 2000);
    CHECK(TurnNextDueMs(1000, 2000, TurnSituation::Teleport) == 1100);
    // More urgent situations never tolerate a longer wait.
    CHECK(TurnMaxWaitMs(TurnSituation::Teleport) <= TurnMaxWaitMs(TurnSituation::Combat));
    CHECK(TurnMaxWaitMs(TurnSituation::Combat) <= TurnMaxWaitMs(TurnSituation::Dead));
    CHECK(TurnMaxWaitMs(TurnSituation::Dead) <= TurnMaxWaitMs(TurnSituation::Normal));
    CHECK(TurnMaxWaitMs(TurnSituation::Battleground) <= TurnMaxWaitMs(TurnSituation::Normal));
}

static void TestDueSurvivesClockWrap()
{
    CHECK(TurnIsDue(500, 500));
    CHECK(TurnIsDue(501, 500));
    CHECK(!TurnIsDue(499, 500));
    // Due just before the wrap, checked just after it.
    uint32_t const dueMs = 0xFFFFFF00u;
    CHECK(TurnIsDue(0x00000010u, dueMs));
    // Due just after the wrap, checked just before it.
    CHECK(!TurnIsDue(0xFFFFFF00u, TurnNextDueMs(0xFFFFFF00u, 1000, TurnSituation::Normal)));
}

static void TestOrderIsRelativeToTolerance()
{
    uint32_t const now = 10000;
    std::vector<DueTurn> due = {
        {1, now - 1000, TurnSituation::Normal}, // 1000 of 3000 = 0.33
        {2, now - 200, TurnSituation::Combat},  // 200 of 400 = 0.50
        {3, now - 2000, TurnSituation::Normal}, // 2000 of 3000 = 0.67
        {4, now - 100, TurnSituation::Combat},  // 100 of 400 = 0.25
        {5, now, TurnSituation::Normal},        // just due
        {6, now, TurnSituation::Combat},        // just due, more urgent
    };
    OrderDueTurns(due, now);
    std::vector<uint32_t> order;
    for (DueTurn const& t : due)
        order.push_back(t.guidLow);
    CHECK((order == std::vector<uint32_t>{3, 2, 1, 4, 6, 5}));

    // Ordering also holds across the clock wrap.
    uint32_t const wrapNow = 0x00000100u;
    std::vector<DueTurn> wrapped = {
        {7, wrapNow - 50, TurnSituation::Normal},
        {8, wrapNow - 0x200, TurnSituation::Normal}, // due before the wrap
    };
    OrderDueTurns(wrapped, wrapNow);
    CHECK(wrapped[0].guidLow == 8);
}

// A pool the way BotManager drives it: each tick collect the due bots, order
// them, serve as many as the budget allows, and set each served bot's next due
// from its AI delay. Every turn costs one budget unit.
struct SimBot
{
    uint32_t guidLow;
    TurnSituation situation;
    uint32_t aiDelayMs;
    uint32_t dueMs = 0;
    uint32_t lastTurnMs = 0;
    uint32_t turns = 0;
    uint32_t maxGapMs = 0;
    uint64_t gapSumMs = 0;
};

static void Simulate(std::vector<SimBot>& bots, uint32_t turnsPerTick, uint32_t tickMs, uint32_t ticks)
{
    uint32_t now = 1000;
    for (SimBot& b : bots)
        b.dueMs = b.lastTurnMs = now;
    std::map<uint32_t, SimBot*> byGuid;
    for (SimBot& b : bots)
        byGuid[b.guidLow] = &b;

    for (uint32_t t = 0; t < ticks; ++t, now += tickMs)
    {
        std::vector<DueTurn> due;
        for (SimBot const& b : bots)
            if (TurnIsDue(now, b.dueMs))
                due.push_back({b.guidLow, b.dueMs, b.situation});
        OrderDueTurns(due, now);
        uint32_t served = 0;
        for (DueTurn const& d : due)
        {
            if (served++ == turnsPerTick)
                break;
            SimBot& b = *byGuid[d.guidLow];
            uint32_t const gap = now - b.lastTurnMs;
            if (b.turns)
            {
                b.maxGapMs = std::max(b.maxGapMs, gap);
                b.gapSumMs += gap;
            }
            ++b.turns;
            b.lastTurnMs = now;
            b.dueMs = TurnNextDueMs(now, b.aiDelayMs, b.situation);
        }
    }
}

static void TestUnderloadedPoolRunsOnTime()
{
    // 10 fighting bots (AI asks every 100 ms) and 40 walking ones (1 s):
    // 100 + 40 = 140 turns/s against 20 per 50 ms tick = 400/s of room.
    std::vector<SimBot> bots;
    for (uint32_t i = 0; i < 10; ++i)
        bots.push_back({i + 1, TurnSituation::Combat, 100});
    for (uint32_t i = 0; i < 40; ++i)
        bots.push_back({i + 100, TurnSituation::Normal, 1000});
    Simulate(bots, 20, 50, 400);
    for (SimBot const& b : bots)
    {
        // Served at the AI's own delay, rounded up to the next tick.
        CHECK(b.maxGapMs <= b.aiDelayMs + 50);
        CHECK(b.turns > 0);
    }
}

static void TestOverloadedPoolStarvesNobody()
{
    // 20 fighting bots (100 ms), 10 dead (1 s) and 170 walking (1 s):
    // 200 + 10 + 170 = 380 turns/s wanted, room for 10 per 50 ms tick = 200/s.
    std::vector<SimBot> bots;
    for (uint32_t i = 0; i < 20; ++i)
        bots.push_back({i + 1, TurnSituation::Combat, 100});
    for (uint32_t i = 0; i < 10; ++i)
        bots.push_back({i + 100, TurnSituation::Dead, 1000});
    for (uint32_t i = 0; i < 170; ++i)
        bots.push_back({i + 1000, TurnSituation::Normal, 1000});
    Simulate(bots, 10, 50, 2000);

    uint32_t combatMax = 0, deadMax = 0, normalMax = 0;
    uint64_t combatSum = 0, combatTurns = 0, normalSum = 0, normalTurns = 0;
    for (SimBot const& b : bots)
    {
        CHECK(b.turns > 10); // nobody starves
        if (b.situation == TurnSituation::Combat)
        {
            combatMax = std::max(combatMax, b.maxGapMs);
            combatSum += b.gapSumMs;
            combatTurns += b.turns - 1;
        }
        else if (b.situation == TurnSituation::Dead)
            deadMax = std::max(deadMax, b.maxGapMs);
        else
        {
            normalMax = std::max(normalMax, b.maxGapMs);
            normalSum += b.gapSumMs;
            normalTurns += b.turns - 1;
        }
    }
    uint64_t const combatMean = combatSum / combatTurns;
    uint64_t const normalMean = normalSum / normalTurns;
    std::printf("  overload: combat mean %llu ms max %u, dead max %u, normal mean %llu ms max %u\n",
        static_cast<unsigned long long>(combatMean), combatMax, deadMax,
        static_cast<unsigned long long>(normalMean), normalMax);
    // Fights stay far ahead of walking, and every wait stays bounded.
    CHECK(combatMean * 3 < normalMean);
    CHECK(combatMax < 1000);
    CHECK(deadMax < normalMax);
    CHECK(normalMax < 5000);
}

int main()
{
    TestNextDueCapsTheAiDelay();
    TestDueSurvivesClockWrap();
    TestOrderIsRelativeToTolerance();
    TestUnderloadedPoolRunsOnTime();
    TestOverloadedPoolStarvesNobody();
    std::printf("test_bot_turn_scheduler: %d checks passed\n", checks);
    return 0;
}
