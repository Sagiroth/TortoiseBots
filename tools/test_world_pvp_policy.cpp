// Standalone test for the open-world PvP level-gap odds: never 5+ levels
// above the bot, 25 % at 4 apart or 5+ below, 50 % at 3, 75 % at 2,
// always closer, with the per-pair decision stable for about two minutes.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_world_pvp_policy.cpp -o /tmp/test_world_pvp_policy
//   /tmp/test_world_pvp_policy

#include "../runtime/WorldPvpPolicy.h"

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

int main()
{
    // Never at 5 or more levels above the bot.
    CHECK(WorldPvpLevelGapOdds(5) == 0);
    CHECK(WorldPvpLevelGapOdds(6) == 0);
    CHECK(WorldPvpLevelGapOdds(60) == 0);

    // 4 apart (either way) or 5+ below: 25 %.
    CHECK(WorldPvpLevelGapOdds(4) == 25);
    CHECK(WorldPvpLevelGapOdds(-4) == 25);
    CHECK(WorldPvpLevelGapOdds(-5) == 25);
    CHECK(WorldPvpLevelGapOdds(-10) == 25);
    CHECK(WorldPvpLevelGapOdds(-59) == 25);

    // 3 apart: 50 %.
    CHECK(WorldPvpLevelGapOdds(3) == 50);
    CHECK(WorldPvpLevelGapOdds(-3) == 50);

    // 2 apart: 75 %.
    CHECK(WorldPvpLevelGapOdds(2) == 75);
    CHECK(WorldPvpLevelGapOdds(-2) == 75);

    // Closer: always.
    CHECK(WorldPvpLevelGapOdds(1) == 100);
    CHECK(WorldPvpLevelGapOdds(0) == 100);
    CHECK(WorldPvpLevelGapOdds(-1) == 100);

    // The decision for one pair stays stable for the window.
    CHECK(WorldPvpLevelGapStabilitySeconds() == 120);
    CHECK(WorldPvpLevelGapDecisionFresh(1000, 1000));
    CHECK(WorldPvpLevelGapDecisionFresh(1000, 1119));
    CHECK(!WorldPvpLevelGapDecisionFresh(1000, 1120));
    CHECK(!WorldPvpLevelGapDecisionFresh(1000, 5000));
    CHECK(WorldPvpLevelGapDecisionFresh(2000, 1000));

    std::printf("test_world_pvp_policy: %d checks passed\n", checks);
    return 0;
}
