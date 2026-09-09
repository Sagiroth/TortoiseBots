#include "../runtime/GearSeedingGuard.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x << "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::NeedsInitialGearSeeding;

int main()
{
    std::cout << "Starting TortoiseBots progression seeding regression tests...\n";

    // Fresh pool bot: zero played time, never seeded -> seed once.
    CHECK(NeedsInitialGearSeeding(0, 0) == true);
    std::cout << "  [PASS] fresh bot is seeded\n";

    // Same process, second login/timer tick: stamp present -> skip.
    CHECK(NeedsInitialGearSeeding(0, 1) == false);
    std::cout << "  [PASS] seeded stamp suppresses repeat seeding\n";

    // Server restart wipes facade values, but played time persists in the
    // characters table: a veteran with played time and no stamp keeps gear.
    CHECK(NeedsInitialGearSeeding(3600, 0) == false);
    CHECK(NeedsInitialGearSeeding(1, 0) == false);
    std::cout << "  [PASS] veteran bot survives restart without reseed\n";

    // Established bot: both signals set -> skip.
    CHECK(NeedsInitialGearSeeding(86400, 1) == false);
    std::cout << "  [PASS] established bot is never reseeded\n";

    // Boundary: any nonzero played time counts as earned progression.
    CHECK(NeedsInitialGearSeeding(0, 0) == true);
    CHECK(NeedsInitialGearSeeding(UINT32_MAX, 0) == false);
    std::cout << "  [PASS] played-time boundary behaves\n";

    // Partial-purse rule: travel when at least the cheapest trainable spell
    // fits the free-money budget, even if the full batch cannot be afforded.
    auto canAffordTrainerTravel = [](const std::vector<uint32_t>& costs, uint32_t freeMoney) -> bool {
        uint32_t minSpellCost = UINT32_MAX;
        for (uint32_t cost : costs)
            if (cost < minSpellCost)
                minSpellCost = cost;
        if (minSpellCost == UINT32_MAX)
            return false;
        return freeMoney >= minSpellCost;
    };

    // 3 spells costing 20s, 30s, 50s (total batch = 100s = 10000 copper)
    std::vector<uint32_t> batch = {2000, 3000, 5000};
    CHECK(canAffordTrainerTravel(batch, 1000) == false); // 10s: cannot afford cheapest (20s)
    CHECK(canAffordTrainerTravel(batch, 2000) == true);  // 20s: exact cheapest affordable -> travel!
    CHECK(canAffordTrainerTravel(batch, 2500) == true);  // 25s: partial purse, cannot buy all 100s, but can buy 1 spell -> travel!
    CHECK(canAffordTrainerTravel(batch, 10000) == true); // 100s: full batch affordable -> travel!
    CHECK(canAffordTrainerTravel({}, 50000) == false);   // No unlearned spells -> don't travel
    std::cout << "  [PASS] partial-purse trainer gating behaves\n";

    std::cout << "All progression seeding checks PASSED!\n";
    return 0;
}
