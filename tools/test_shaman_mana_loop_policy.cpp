#include "../ai/playerbot/strategy/shaman/ShamanManaLoopPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ChainLightningPackFillerOnly;
using ai::ElementalWantsWaterShield;

int main()
{
    std::cout << "Starting shaman mana-loop policy tests...\n";

    // Shield pick: water once trained, lightning before that. Both rows
    // stay queued; the water trigger gates on HasSpell.
    CHECK(ElementalWantsWaterShield(true) == true);
    CHECK(ElementalWantsWaterShield(false) == false);
    std::cout << "  [PASS] water shield wins once trained, lightning before\n";

    // Filler is pack-only, never single-target.
    CHECK(ChainLightningPackFillerOnly() == true);
    std::cout << "  [PASS] chain-lightning filler is pack-only\n";

    std::cout << "All shaman mana-loop policy checks PASSED!\n";
    return 0;
}
