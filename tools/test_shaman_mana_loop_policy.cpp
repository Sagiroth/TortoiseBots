#include "../ai/playerbot/strategy/shaman/ShamanManaLoopPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ChainLightningFillerOffset;
using ai::ElementalWantsWaterShield;

int main()
{
    std::cout << "Starting shaman mana-loop policy tests...\n";

    // Shield pick: water whenever trained, lightning only before training.
    CHECK(ElementalWantsWaterShield(true) == true);
    CHECK(ElementalWantsWaterShield(false) == false);
    std::cout << "  [PASS] water shield wins once trained\n";

    // Filler offset: strictly negative (below shocks and bolt default).
    CHECK(ChainLightningFillerOffset() < 0.0f);
    std::cout << "  [PASS] chain-lightning filler sits below the shock lines\n";

    std::cout << "All shaman mana-loop policy checks PASSED!\n";
    return 0;
}
