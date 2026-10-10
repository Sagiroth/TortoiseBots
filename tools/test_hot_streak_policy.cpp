// Standalone regression test for the mage Hot Streak proc port (MAG-1):
// hurry a Pyroblast only when the proc aura (51930/51931) sits at full
// stacks — never for the passive talent auras, never for partial stacks.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_hot_streak_policy.cpp -o /tmp/test_hot_streak
//   /tmp/test_hot_streak

#include "../ai/playerbot/HotStreakPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace ai;

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
    // Proc ID guard: only 51930/51931 count, never the talent auras
    // 51927/51928 (which share the "Hot Streak" name) or anything else.
    CHECK(IsHotStreakProcId(51930));
    CHECK(IsHotStreakProcId(51931));
    CHECK(!IsHotStreakProcId(51927));
    CHECK(!IsHotStreakProcId(51928));
    CHECK(!IsHotStreakProcId(0));

    // No proc at all: quiet.
    CHECK(!ShouldCastHotStreakPyroblast(HotStreakState{false, 0, false, 0}));

    // Full stacks on either rank: fire.
    CHECK(ShouldCastHotStreakPyroblast(HotStreakState{true, 5, false, 0}));
    CHECK(ShouldCastHotStreakPyroblast(HotStreakState{false, 0, true, 5}));

    // Partial stacks (1-4): keep stacking, do not spend the charge.
    for (std::uint32_t stacks = 1; stacks < 5; ++stacks)
    {
        CHECK(!ShouldCastHotStreakPyroblast(HotStreakState{true, stacks, false, 0}));
        CHECK(!ShouldCastHotStreakPyroblast(HotStreakState{false, 0, true, stacks}));
    }

    // Aura flag set but zero stacks (stale read): quiet.
    CHECK(!ShouldCastHotStreakPyroblast(HotStreakState{true, 0, false, 0}));

    std::printf("hot streak policy: %d checks passed\n", checks);
    return 0;
}
