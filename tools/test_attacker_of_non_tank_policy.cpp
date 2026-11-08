// Standalone regression test for the off-target taunt policy: which engaged
// hostile the tank taunts when a mob sits on a non-tank group member —
// healer victims first, closest first, tanks' own mobs and player attackers
// skipped, held CC marks refused unless the owner's taunt exception applies.
//
// Range is not a policy decision: the value caps candidates at a fixed scan
// radius and each taunt cast reads its real range from the spell, walking
// the tank in with its reach prerequisite — game state this std-only test
// cannot see, so only the ranking and hold rules are checked here.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_attacker_of_non_tank_policy.cpp -o /tmp/test_attacker_of_non_tank_policy
//   /tmp/test_attacker_of_non_tank_policy

#include "../ai/playerbot/policy/combat/AttackerOfNonTankPolicy.h"

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

static AttackerOfNonTankCandidate Mob(float distance, bool healerVictim)
{
    AttackerOfNonTankCandidate c;
    c.victimIsGroupPlayer = true;
    c.victimIsHealer = healerVictim;
    c.distance = distance;
    return c;
}

int main()
{
    // The core case from the issue: a mob that has arrived and swings at the
    // healer while the tank fights its own target.
    {
        std::vector<AttackerOfNonTankCandidate> candidates = { Mob(12.0f, true) };
        CHECK(AttackerOfNonTankPick(candidates) == 0);
    }

    // The healer's attacker outranks a mob on a DPS, regardless of distance.
    {
        std::vector<AttackerOfNonTankCandidate> candidates = { Mob(5.0f, false), Mob(25.0f, true) };
        CHECK(AttackerOfNonTankPick(candidates) == 1);
    }
    {
        std::vector<AttackerOfNonTankCandidate> candidates = { Mob(5.0f, true), Mob(1.0f, false) };
        CHECK(AttackerOfNonTankPick(candidates) == 0);
    }

    // Two mobs on the healer: the closer one.
    {
        std::vector<AttackerOfNonTankCandidate> candidates = { Mob(20.0f, true), Mob(8.0f, true) };
        CHECK(AttackerOfNonTankPick(candidates) == 1);
    }

    // No non-tank victim needs help: the tank holding its mob, nothing else.
    {
        AttackerOfNonTankCandidate tankHeld = Mob(3.0f, false);
        tankHeld.victimIsTank = true;
        std::vector<AttackerOfNonTankCandidate> candidates = { tankHeld };
        CHECK(AttackerOfNonTankPick(candidates) == 1);
    }

    // A distant mob inside the scan radius is still picked: the taunt's own
    // reach prerequisite walks the tank to it.
    {
        std::vector<AttackerOfNonTankCandidate> candidates = { Mob(25.0f, true) };
        CHECK(AttackerOfNonTankPick(candidates) == 0);
    }

    // A held CC mark is refused like every other row: the CC bot's job.
    {
        AttackerOfNonTankCandidate mark = Mob(10.0f, true);
        mark.heldCcMark = true;
        std::vector<AttackerOfNonTankCandidate> candidates = { mark };
        CHECK(AttackerOfNonTankPick(candidates) == 1);
    }

    // Owner exception: a held mark that is not crowd-controlled yet and is
    // hitting the healer may be taunted (CcHeldMarkTauntAllowed). The caller
    // folds that into heldMarkTauntAllowed; a DPS victim never qualifies.
    {
        AttackerOfNonTankCandidate mark = Mob(10.0f, true);
        mark.heldCcMark = true;
        mark.heldMarkTauntAllowed = true;
        std::vector<AttackerOfNonTankCandidate> candidates = { mark };
        CHECK(AttackerOfNonTankPick(candidates) == 0);
    }

    // A held mark on a DPS victim stays refused even with the taunt flag set
    // (the caller can only set it for healer victims; the policy double-checks
    // the combination the caller may not produce).
    {
        AttackerOfNonTankCandidate mark = Mob(10.0f, false);
        mark.heldCcMark = true;
        mark.heldMarkTauntAllowed = true;
        std::vector<AttackerOfNonTankCandidate> candidates = { mark };
        CHECK(AttackerOfNonTankPick(candidates) == 1);
    }

    // A subject the taunt gate refuses (a Shade of Jin'do a cursed tank could damage, a taunt-immune boss) is no pick
    // while another attacker still is; with the gate off the same list picks the Shade (today's rule).
    // TestTauntRefusedCandidateSkipped.
    {
        AttackerOfNonTankCandidate shade = Mob(5.0f, true);
        shade.tauntRefused = true;
        AttackerOfNonTankCandidate slave = Mob(20.0f, false);
        std::vector<AttackerOfNonTankCandidate> candidates = { shade, slave };
        CHECK(AttackerOfNonTankPick(candidates) == 1);
    }
    {
        AttackerOfNonTankCandidate shade = Mob(5.0f, true);
        AttackerOfNonTankCandidate slave = Mob(20.0f, false);
        std::vector<AttackerOfNonTankCandidate> candidates = { shade, slave };
        CHECK(AttackerOfNonTankPick(candidates) == 0);
    }

    // Empty scan / nothing fighting: no pick.
    {
        std::vector<AttackerOfNonTankCandidate> candidates;
        CHECK(AttackerOfNonTankPick(candidates) == candidates.size());
        CHECK(AttackerOfNonTankPick(candidates) == 0);
    }

    std::printf("ok: %d checks\n", checks);
    return 0;
}
