#include "../ai/playerbot/policy/raid/GeddonInfernoPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsGeddonSurvivalMove;
using ai::kGeddonEntry;
using ai::kGeddonRunoutDistance;
using ai::kInfernoSpellId;
using ai::kLivingBombSpellId;
using ai::ShouldBlockGeddonMove;
using ai::ShouldRunFromGeddonInferno;

int UpstreamGeddonPolicyTest()
{
    std::cout << "Starting TortoiseBots geddon-inferno policy tests...\n";

    // Pinned ids: Baron Geddon 12056, Inferno 19695, Living Bomb 20475.
    CHECK(kGeddonEntry == 12056);
    CHECK(kInfernoSpellId == 19695);
    CHECK(kLivingBombSpellId == 20475);
    CHECK(kGeddonRunoutDistance == 20.0f);
    std::cout << "  [PASS] ids and 20y runout distance pinned\n";

    // Trigger fires only when Geddon is present AND carries Inferno.
    CHECK(ShouldRunFromGeddonInferno(true, true));
    CHECK(!ShouldRunFromGeddonInferno(false, true));
    CHECK(!ShouldRunFromGeddonInferno(true, false));
    CHECK(!ShouldRunFromGeddonInferno(false, false));
    std::cout << "  [PASS] trigger needs boss present with inferno aura\n";

    // Survival moves pass through; everything else is vetoed while active.
    CHECK(IsGeddonSurvivalMove("move away from geddon"));
    CHECK(IsGeddonSurvivalMove("raid bomb runout"));
    CHECK(!IsGeddonSurvivalMove("reach melee"));
    CHECK(!IsGeddonSurvivalMove("charge"));
    CHECK(!IsGeddonSurvivalMove("tank assist"));
    std::cout << "  [PASS] only the two survival moves pass\n";

    // Calm: nothing blocked. Inferno up: the runout actions still run,
    // movement approach ("reach melee", "charge", "follow") is blocked.
    // Bomb on self after Geddon died keeps blocking approach moves too.
    // Non-movement actions (heals, DPS, potions) ALWAYS pass — healers at
    // 30y keep healing through Inferno, ranged keep casting, and the
    // fight's own fire-protection potion is never vetoed.
    CHECK(!ShouldBlockGeddonMove(true, "reach melee", false, false));
    CHECK(!ShouldBlockGeddonMove(true, "move away from geddon", true, false));
    CHECK(!ShouldBlockGeddonMove(true, "raid bomb runout", false, true));
    CHECK(ShouldBlockGeddonMove(true, "reach melee", true, false));
    CHECK(ShouldBlockGeddonMove(true, "charge", true, false));
    CHECK(ShouldBlockGeddonMove(true, "follow", true, false));
    CHECK(ShouldBlockGeddonMove(true, "reach melee", false, true));
    CHECK(ShouldBlockGeddonMove(true, "tank assist", true, true));
    std::cout << "  [PASS] multiplier blocks non-survival movement while active\n";

    // Non-movement gate: the first parameter is false for casts, attacks,
    // heals and consumables, so nothing is ever blocked for them — even
    // with Inferno burning and a bomb on self.
    CHECK(!ShouldBlockGeddonMove(false, "greater heal", true, false));
    CHECK(!ShouldBlockGeddonMove(false, "flash heal", true, true));
    CHECK(!ShouldBlockGeddonMove(false, "renew", false, true));
    CHECK(!ShouldBlockGeddonMove(false, "shoot", true, false));
    CHECK(!ShouldBlockGeddonMove(false, "fire protection potion", true, false));
    CHECK(!ShouldBlockGeddonMove(false, "tank assist", true, true));
    std::cout << "  [PASS] non-movement actions never blocked\n";

    std::cout << "All geddon-inferno policy tests passed.\n";
    return 0;
}

#include "../ai/playerbot/policy/raid/GeddonInfernoPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    if (UpstreamGeddonPolicyTest() != 0) return 1;
    using namespace ai;
    // Both movement filters run in combat: local survival moves must pass both.
    for (std::string const name : {"geddon inferno retreat", "geddon bomb move", "move away from hazard"})
    {
        assert(!ShouldBlockGeddonMove(true, name, true, false));
        assert(!ShouldBlockGeddonMove(true, name, false, true));
        assert(!GeddonInfernoBlocksAction(true, false, true));
    }
    // Opt-in, encounter and explicit-order boundaries. No role/victim gate:
    // the current tank must use exactly the same retreat decision as DPS.
    assert(GeddonInfernoEnabled(true, true, true, true, false));
    assert(!GeddonInfernoEnabled(false, true, true, true, false));
    assert(!GeddonInfernoEnabled(true, false, true, true, false));
    assert(!GeddonInfernoEnabled(true, true, false, true, false));
    assert(!GeddonInfernoEnabled(true, true, true, false, false));
    assert(!GeddonInfernoEnabled(true, true, true, true, true));
    // Full pulse lifecycle, including the root-only ninth scripted tick.
    assert(GeddonInfernoActive(true, true, true, true));
    assert(GeddonInfernoActive(true, true, false, true));
    assert(!GeddonInfernoActive(true, true, false, false));
    assert(!GeddonInfernoActive(false, true, true, true));
    assert(!GeddonInfernoActive(true, false, true, true));
    assert(GeddonInfernoNeedsRetreat(0));
    assert(GeddonInfernoNeedsRetreat(20));
    assert(GeddonInfernoNeedsRetreat(22.99f));
    // Settled bots do not walk another ring-distance on subsequent ticks.
    for (int tick = 0; tick < 10; ++tick)
        assert(!GeddonInfernoNeedsRetreat(23));
    assert(!GeddonInfernoNeedsRetreat(40));
    assert(GeddonInfernoBlocksAction(true, false, false)); // chase/reach
    assert(GeddonInfernoBlocksAction(false, true, false)); // charge
    assert(!GeddonInfernoBlocksAction(false, false, false)); // ranged/heal
    assert(!GeddonInfernoBlocksAction(true, false, true)); // emergency escape
    std::cout << "Geddon Inferno policy: passed\n";
}
