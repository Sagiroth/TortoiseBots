#include "../ai/playerbot/GeddonInfernoPolicy.h"

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

int main()
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
