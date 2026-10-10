// Standalone regression test for the mage flamestrike -> blizzard
// sequencing port (MAG-2): after the bot's own flamestrike lands, the
// follow-up blizzard stacks on the burning ground while the window holds.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_flamestrike_window_policy.cpp -o /tmp/test_flamestrike_window
//   /tmp/test_flamestrike_window

#include "../ai/playerbot/FlamestrikeWindowPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static FlamestrikeWindowState Base()
{
    // Flamestrike rank 1 cast 2s ago, pack still grouped.
    return FlamestrikeWindowState{2124, 1000, 1002, true};
}

int main()
{
    // Base case: blizzard follow-up fires.
    CHECK(ShouldBlizzardAfterFlamestrike(Base()));

    // Every flamestrike rank opens the window.
    for (std::uint32_t id : std::vector<std::uint32_t>{2125u, 8425u, 8426u, 10217u, 10218u})
        CHECK(ShouldBlizzardAfterFlamestrike(FlamestrikeWindowState{id, 1000, 1002, true}));

    // Non-flamestrike last cast (fireball 133, frostbolt 116, blizzard
    // 1196, effect id 2120): no follow-up.
    for (std::uint32_t id : std::vector<std::uint32_t>{133u, 116u, 1196u, 2120u, 0u})
        CHECK(!ShouldBlizzardAfterFlamestrike(FlamestrikeWindowState{id, 1000, 1002, true}));

    // Window edge: exactly 6s still counts, 7s does not.
    CHECK(ShouldBlizzardAfterFlamestrike(FlamestrikeWindowState{2124, 1000, 1006, true}));
    CHECK(!ShouldBlizzardAfterFlamestrike(FlamestrikeWindowState{2124, 1000, 1007, true}));

    // Pack scattered since the cast: single-target logic owns the fight.
    CHECK(!ShouldBlizzardAfterFlamestrike(FlamestrikeWindowState{2124, 1000, 1002, false}));

    // Clock skew (cast timestamp in the future): quiet, never fires.
    CHECK(!ShouldBlizzardAfterFlamestrike(FlamestrikeWindowState{2124, 1005, 1000, true}));

    std::printf("flamestrike window policy: %d checks passed\n", checks);
    return 0;
}
