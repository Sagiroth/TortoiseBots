// Follow-first unit test (owner report 2026-11-01): when the master moves
// far enough that the bot would start following, the move overrides every
// non-combat row - buffing, drinking, eating, other downtime.
//
// Covers ai::FollowOverridesDowntime / ai::BuffCastHoldsFollow
// (ai/playerbot/FollowUrgencyPolicy.h): the pure urgency predicate the
// follow, drink/eat and buff gates share.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_follow_urgency_policy.cpp -o /tmp/test_follow_urgency
//   /tmp/test_follow_urgency

#include "../ai/playerbot/FollowUrgencyPolicy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// At or inside the follow leash the bot may buff and drink: follow's own
// "out of free move range" row does not fire there either.
static void TestInsideLeashNoOverride()
{
    CHECK(!ai::FollowOverridesDowntime(0.5f, 1.5f));
    CHECK(!ai::FollowOverridesDowntime(1.5f, 1.5f));
}

// Past the follow leash the move wins, even for a tight (default 1.5 yd)
// leash: the station dead zone (5 yd) is the floor, so the catch-up starts
// past it - the same line the "update follow" trigger fires on.
static void TestPastLeashOverrides()
{
    CHECK(!ai::FollowOverridesDowntime(1.6f, 1.5f));
    CHECK(ai::FollowOverridesDowntime(5.1f, 1.5f));
    CHECK(ai::FollowOverridesDowntime(30.0f, 1.5f));
}

static void TestWideLeashHoldsUntilExceeded()
{
    CHECK(!ai::FollowOverridesDowntime(20.0f, 25.0f));
    CHECK(ai::FollowOverridesDowntime(25.1f, 25.0f));
}

// Degenerate inputs never force a move.
static void TestDegenerateInputsSafe()
{
    CHECK(!ai::FollowOverridesDowntime(0.0f, 1.5f));
    CHECK(!ai::FollowOverridesDowntime(-3.0f, 1.5f));
}

// Cast-time buffs hold the bot (they stop movement), instant buffs ride
// along: the predicate only names the stand-still half, callers pass the
// cast-time shape from CastKeepsRunningPolicy.
static void TestBuffCastGate()
{
    CHECK(ai::BuffCastHoldsFollow(true));
    CHECK(!ai::BuffCastHoldsFollow(false));
}

int main()
{
    TestInsideLeashNoOverride();
    TestPastLeashOverrides();
    TestWideLeashHoldsUntilExceeded();
    TestDegenerateInputsSafe();
    TestBuffCastGate();
    std::printf("ok %d checks\n", checks);
    return 0;
}
