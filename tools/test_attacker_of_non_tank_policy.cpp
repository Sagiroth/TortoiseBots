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
#include "../ai/playerbot/BossTacticsPolicy.h"

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

    // The taunt gate drops a needs_aura unit from the scan (AttackerOfNonTankSkipsUnit) for both users of the value
    // unless the bot carries the aura: a Shade of Jin'do on a healer plus a Voodoo Slave on a DPS picks the Slave for an
    // uncursed tank with the gate on, the Shade for a cursed tank, and the Shade (today's rule) with the gate off.
    // TestShadeSkippedForUncursedTankOnly.
    {
        constexpr std::uint32_t kDelusions = 24306;
        struct Seen { AttackerOfNonTankCandidate candidate; std::uint32_t requiredAura; };
        std::vector<Seen> seen = { { Mob(5.0f, true), kDelusions }, { Mob(20.0f, false), 0 } };
        struct Case { bool gateOn; bool botHasAura; bool shadePicked; };
        for (const Case& c : { Case{ true, false, false }, Case{ true, true, true }, Case{ false, false, true },
                               Case{ false, true, true } })
        {
            std::vector<AttackerOfNonTankCandidate> candidates;
            for (const Seen& s : seen)
                if (!AttackerOfNonTankSkipsUnit(c.gateOn, s.requiredAura, c.botHasAura && s.requiredAura != 0))
                    candidates.push_back(s.candidate);
            std::size_t const pick = AttackerOfNonTankPick(candidates);
            CHECK(pick < candidates.size());
            CHECK(candidates[pick].victimIsHealer == c.shadePicked);
        }
        CHECK(!AttackerOfNonTankSkipsUnit(true, 0, false));  // an ordinary unit stays
        CHECK(AttackerOfNonTankSkipsUnit(true, kDelusions, false));
        CHECK(!AttackerOfNonTankSkipsUnit(true, kDelusions, true));  // a cursed tank keeps the Shade
        CHECK(!AttackerOfNonTankSkipsUnit(false, kDelusions, false));
    }

    // The value and the taunt gate agree (TestShadeValueAndGateAgree): a Shade of Jin'do on the healer plus a tauntable
    // Voodoo Slave on a DPS. An uncursed tank: the value drops the Shade, picks the Slave, and the gate allows the
    // Slave. A cursed tank: the value picks the Shade (it is on the healer) and the gate allows that taunt, so the
    // Shade is never picked and then refused. Key off: the Shade is picked and the gate allows it as before.
    {
        constexpr std::uint32_t kDelusions = 24306;
        struct Seen { AttackerOfNonTankCandidate candidate; std::uint32_t requiredAura; };
        std::vector<Seen> seen = { { Mob(5.0f, true), kDelusions }, { Mob(20.0f, false), 0 } };
        struct Case { bool gateOn; bool botHasAura; std::size_t seenPick; };
        for (const Case& c : { Case{ true, false, 1 }, Case{ true, true, 0 }, Case{ false, false, 0 } })
        {
            std::vector<AttackerOfNonTankCandidate> candidates;
            std::vector<std::size_t> origin;
            for (std::size_t i = 0; i < seen.size(); ++i)
                if (!AttackerOfNonTankSkipsUnit(c.gateOn, seen[i].requiredAura, c.botHasAura && seen[i].requiredAura != 0))
                {
                    candidates.push_back(seen[i].candidate);
                    origin.push_back(i);
                }
            std::size_t const pick = AttackerOfNonTankPick(candidates);
            CHECK(pick < candidates.size());
            CHECK(origin[pick] == c.seenPick);
            // The gate answers the same bot for the unit the value returned: never a refusal.
            std::uint32_t const required = seen[origin[pick]].requiredAura;
            CHECK(TauntGateFor(c.gateOn, required, c.botHasAura && required != 0, false, false) == TauntGate::Allow);
        }
        // The disagreement the pair used to have: an uncursed tank is refused at the Shade, and the value never offers it.
        CHECK(TauntGateFor(true, kDelusions, false, false, false) == TauntGate::NeedsAura);
        CHECK(AttackerOfNonTankSkipsUnit(true, kDelusions, false));
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
