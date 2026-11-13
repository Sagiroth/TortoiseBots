// Standalone regression test for the "lose aggro" trigger's tank branch
// (ai/playerbot/policy/combat/LoseAggroPolicy.h): a victimless mob is not
// "held" by anyone. A taunt queued on a fresh refill mob (no victim yet)
// used to survive its reach prerequisite and fire after the mob turned onto
// a tank player (Tortoise Trials tank-*-no-taunt-tank-player, 2026-11-10).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_lose_aggro_policy.cpp -o /tmp/test_lose_aggro_policy
//   /tmp/test_lose_aggro_policy

#include "../ai/playerbot/policy/combat/LoseAggroPolicy.h"

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

static LoseAggroSeen Seen(bool exists, bool player, bool tank)
{
    LoseAggroSeen in;
    in.victimExists = exists;
    in.victimIsPlayer = player;
    in.victimIsTank = tank;
    return in;
}

int main()
{
    // No victim yet (the trial's failing case): stay quiet, not held.
    CHECK(!LoseAggroTankBranch(Seen(false, false, false)));
    // A tank holds it (tank player or another bot tank): stay quiet.
    CHECK(!LoseAggroTankBranch(Seen(true, true, true)));
    // A healer holds it (taunt-off-healer must still fire): go.
    CHECK(LoseAggroTankBranch(Seen(true, true, false)));
    // A DPS player holds it: go.
    CHECK(LoseAggroTankBranch(Seen(true, true, false)));
    // On a pet / NPC (not a player): go.
    CHECK(LoseAggroTankBranch(Seen(true, false, false)));
    // Tank flag without a player victim is still a pet / NPC: go.
    CHECK(LoseAggroTankBranch(Seen(true, false, true)));

    std::printf("OK test_lose_aggro_policy (%d checks)\n", checks);
    return 0;
}
