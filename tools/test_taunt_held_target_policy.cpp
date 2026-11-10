// Standalone regression test for the queued taunt's cast-time stand-down: a
// generic taunt stands down when the mob's current victim is a player other
// than the casting bot that PlayerbotAI::IsTank reads as a tank (the rule the
// "lose aggro" and "party member attacked" triggers already apply at decision
// time; a taunt queued while a fresh mob had no victim yet survives its
// reach prerequisite and runs after the mob turned onto a tank player).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_taunt_held_target_policy.cpp -o /tmp/test_taunt_held_target_policy
//   /tmp/test_taunt_held_target_policy

#include "../ai/playerbot/policy/combat/TauntHeldTargetPolicy.h"

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

static TauntHeldInputs Inputs(bool player, bool self, bool tank)
{
    TauntHeldInputs in;
    in.victimIsPlayer = player;
    in.victimIsSelf = self;
    in.victimIsTank = tank;
    return in;
}

int main()
{
    // Another tank holds the mob (the trial's failing case): stand down.
    CHECK(TauntStandsDownForTank(Inputs(true, false, true)));
    // Healer holds it (taunt-off-healer must still taunt): go.
    CHECK(!TauntStandsDownForTank(Inputs(true, false, false)));
    // DPS player holds it: go.
    CHECK(!TauntStandsDownForTank(Inputs(true, false, false)));
    // The bot itself holds it: go.
    CHECK(!TauntStandsDownForTank(Inputs(true, true, true)));
    // On a pet / NPC (not a player): go.
    CHECK(!TauntStandsDownForTank(Inputs(false, false, false)));
    // A non-player reported as a tank is not a tank victim: go.
    CHECK(!TauntStandsDownForTank(Inputs(false, false, true)));
    // No victim yet (fresh mob): go.
    CHECK(!TauntStandsDownForTank(Inputs(false, false, false)));

    std::printf("OK test_taunt_held_target_policy (%d checks)\n", checks);
    return 0;
}
