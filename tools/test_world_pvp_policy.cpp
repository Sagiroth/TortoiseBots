// Standalone test for the open-world PvP level-gap odds: a seeker never
// starts up (+3 or more: 0 %), 75 % at 2 apart either way, 50 % at 3 below,
// 25 % at 4+ below, always closer, with the per-pair decision stable for
// about two minutes.

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
    // Never starts up: +3 or more above the bot is 0.
    CHECK(WorldPvpLevelGapOdds(3) == 0);
    CHECK(WorldPvpLevelGapOdds(4) == 0);
    CHECK(WorldPvpLevelGapOdds(5) == 0);
    CHECK(WorldPvpLevelGapOdds(6) == 0);
    CHECK(WorldPvpLevelGapOdds(60) == 0);

    // 2 apart either way: 75 %.
    CHECK(WorldPvpLevelGapOdds(2) == 75);
    CHECK(WorldPvpLevelGapOdds(-2) == 75);

    // 3 below: 50 %; 4 or more below: 25 %.
    CHECK(WorldPvpLevelGapOdds(-3) == 50);
    CHECK(WorldPvpLevelGapOdds(-4) == 25);
    CHECK(WorldPvpLevelGapOdds(-5) == 25);
    CHECK(WorldPvpLevelGapOdds(-10) == 25);
    CHECK(WorldPvpLevelGapOdds(-59) == 25);

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
