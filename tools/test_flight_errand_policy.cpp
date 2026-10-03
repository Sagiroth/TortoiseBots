#include "../ai/playerbot/FlightErrandPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FLIGHT_ERRAND_LEVEL_CEILING_OVER_BOT;
using ai::FLIGHT_ERRAND_OUTGROWN_MARGIN;
using ai::FlightErrandDestinationUsable;

int main()
{
    std::cout << "Starting TortoiseBots flight errand policy tests...\n";

    // 1. Same-level destination is usable.
    CHECK(FlightErrandDestinationUsable(11, false, 11, false));
    std::cout << "  [PASS] same-level destination usable\n";

    // 2. Ceiling is bot level +5 (mirrors the RPG walk gate): +5 flies, +6
    //    does not. The Teldrassil exit case: a level-11 bot may fly to an
    //    area rated 16, but not 17.
    CHECK(FLIGHT_ERRAND_LEVEL_CEILING_OVER_BOT == 5);
    CHECK(FlightErrandDestinationUsable(11, false, 16, false));
    CHECK(!FlightErrandDestinationUsable(11, false, 17, false));
    std::cout << "  [PASS] over-level destinations past +5 are refused\n";

    // 3. Outgrown floor: a destination 10+ below the bot is refused, unless
    //    it is a capital (trainers/AH/bank live there).
    CHECK(FLIGHT_ERRAND_OUTGROWN_MARGIN == 10);
    CHECK(!FlightErrandDestinationUsable(40, false, 29, false));
    CHECK(FlightErrandDestinationUsable(40, false, 30, false));
    CHECK(FlightErrandDestinationUsable(40, false, 10, true));
    std::cout << "  [PASS] outgrown destinations refused except capitals\n";

    // 4. A bot sitting in a capital never flies to another capital (donor
    //    GetOptimalFlightDestinations guard: that just shuffles cities).
    CHECK(!FlightErrandDestinationUsable(40, true, 30, true));
    // ... but may still fly out to a level-fitting field.
    CHECK(FlightErrandDestinationUsable(40, true, 40, false));
    // ... and may fly into a capital from the field when it fits the band.
    CHECK(FlightErrandDestinationUsable(25, false, 30, true));
    std::cout << "  [PASS] capital-to-capital shuffle refused\n";

    // 5. Unknown area level (level <= 0: unloaded vmap at cross-map legs)
    //    fails open - the walk gate vets the bot on arrival. Failing closed
    //    would ground exactly the long flights this errand exists for.
    CHECK(FlightErrandDestinationUsable(11, false, 0, false));
    CHECK(FlightErrandDestinationUsable(60, false, -2, false));
    std::cout << "  [PASS] unknown area levels fail open\n";

    // 6. Full errand shape: a levelling bot's hop is inside the whole band.
    CHECK(FlightErrandDestinationUsable(11, false, 12, false));
    CHECK(!FlightErrandDestinationUsable(11, true, 60, true));
    std::cout << "  [PASS] levelling errand shape holds end to end\n";

    std::cout << "TortoiseBots flight errand policy tests passed.\n";
    return 0;
}
