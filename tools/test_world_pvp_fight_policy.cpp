// Standalone test for the open-world PvP fight timers: a seeker chases a
// fleeing target for up to 60 seconds, may camp a player it killed for 5
// minutes, then moves on and ignores that player for 10 minutes.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_world_pvp_fight_policy.cpp -o /tmp/test_world_pvp_fight_policy
//   /tmp/test_world_pvp_fight_policy

#include "../runtime/policy/pvp/WorldPvpFightPolicy.h"

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
    CHECK(WorldPvpPursuitSeconds() == 60);
    CHECK(WorldPvpKillIgnoreSeconds() == 10 * 60);

    // Pursuit gives up after 60 s.
    CHECK(!WorldPvpPursuitExpired(1000, 1000));
    CHECK(!WorldPvpPursuitExpired(1000, 1000 + 59999));
    CHECK(WorldPvpPursuitExpired(1000, 1000 + 60000));
    CHECK(WorldPvpPursuitExpired(1000, 1000 + 600000));
    CHECK(!WorldPvpPursuitExpired(2000, 1000));

    // An established pursuit holds on the 60 s limit plus sight range only.
    CHECK(WorldPvpPursuitHolds(1000, 1000, true));
    CHECK(WorldPvpPursuitHolds(1000, 1000 + 59999, true));
    CHECK(!WorldPvpPursuitHolds(1000, 1000 + 60000, true));
    CHECK(!WorldPvpPursuitHolds(1000, 1000, false));
    CHECK(!WorldPvpPursuitHolds(0, 1000, true));
    CHECK(!WorldPvpPursuitHolds(2000, 1000, true));

    // Camp ends after 5 min.

    // The ignore after a camp lasts 10 min.
    CHECK(WorldPvpIgnoreFresh(1000, 1000));
    CHECK(WorldPvpIgnoreFresh(1000, 1000 + 10 * 60 * 1000 - 1));
    CHECK(!WorldPvpIgnoreFresh(1000, 1000 + 10 * 60 * 1000));
    CHECK(!WorldPvpIgnoreFresh(1000, 1000 + 20 * 60 * 1000));
    CHECK(WorldPvpIgnoreFresh(2000, 1000));

    std::printf("test_world_pvp_fight_policy: %d checks passed\n", checks);
    return 0;
}
