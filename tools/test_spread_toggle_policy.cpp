#include "../ai/playerbot/CombatSpreadPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kSpreadDistanceMeleeYd;
using ai::kSpreadDistanceRangedYd;
using ai::ShouldOptInSpread;
using ai::SpreadRadius;

int main()
{
    std::cout << "Starting TortoiseBots spread-toggle policy tests...\n";

    // Opt-in gate: combat-only, hold orders always veto — but ownership
    // and ranged-only never gate (the player asked for spacing).
    CHECK(ShouldOptInSpread(true, false, false, false, false));
    CHECK(!ShouldOptInSpread(false, false, false, false, false));
    CHECK(!ShouldOptInSpread(true, true, false, false, false));
    CHECK(!ShouldOptInSpread(true, false, true, false, false));
    CHECK(!ShouldOptInSpread(true, false, false, true, false));
    CHECK(!ShouldOptInSpread(true, false, false, false, true));
    std::cout << "  [PASS] opt-in gate is combat plus no-hold-orders\n";

    // Radius: manual knob wins when set; role defaults otherwise.
    CHECK(SpreadRadius(-1.0f, true) == kSpreadDistanceRangedYd);
    CHECK(SpreadRadius(-1.0f, false) == kSpreadDistanceMeleeYd);
    CHECK(SpreadRadius(0.0f, true) == kSpreadDistanceRangedYd);
    CHECK(SpreadRadius(8.0f, true) == 8.0f);
    CHECK(SpreadRadius(8.0f, false) == 8.0f);
    std::cout << "  [PASS] manual knob overrides role defaults\n";

    // Donor defaults: 5yd ranged, 2yd melee.
    CHECK(kSpreadDistanceRangedYd == 5.0f);
    CHECK(kSpreadDistanceMeleeYd == 2.0f);
    std::cout << "  [PASS] role defaults match the donor disperse distances\n";

    std::cout << "All spread-toggle policy tests passed.\n";
    return 0;
}
