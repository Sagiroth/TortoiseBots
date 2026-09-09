#include "../runtime/GearSeedingGuard.h"

#include <cstdlib>
#include <iostream>

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

    std::cout << "All progression seeding checks PASSED!\n";
    return 0;
}
