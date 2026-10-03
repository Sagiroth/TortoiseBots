#include "../ai/playerbot/FlightErrandPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FLIGHT_TRANSPORT_LEVEL_CEILING_OVER_BOT;
using ai::FLIGHT_TRANSPORT_MIN_SAVED_YD;
using ai::FLIGHT_TRANSPORT_MIN_TRIP_YD;
using ai::FLIGHT_TRANSPORT_OUTGROWN_MARGIN;
using ai::FlightTransportAffordable;
using ai::FlightTransportDestinationUsable;
using ai::FlightTransportLegWorthwhile;

int main()
{
    std::cout << "Starting TortoiseBots flight transport policy tests...\n";

    // 1. Same-level destination is usable.
    CHECK(FlightTransportDestinationUsable(11, false, 11, false));
    std::cout << "  [PASS] same-level destination usable\n";

    // 2. Ceiling is bot level +5 (mirrors the RPG walk gate): +5 flies, +6
    //    does not. The Teldrassil exit case: a level-11 bot may fly to an
    //    area rated 16, but not 17.
    CHECK(FLIGHT_TRANSPORT_LEVEL_CEILING_OVER_BOT == 5);
    CHECK(FlightTransportDestinationUsable(11, false, 16, false));
    CHECK(!FlightTransportDestinationUsable(11, false, 17, false));
    std::cout << "  [PASS] over-level destinations past +5 are refused\n";

    // 3. Outgrown floor: a destination 10+ below the bot is refused, unless
    //    it is a capital (trainers/AH/bank live there).
    CHECK(FLIGHT_TRANSPORT_OUTGROWN_MARGIN == 10);
    CHECK(!FlightTransportDestinationUsable(40, false, 29, false));
    CHECK(FlightTransportDestinationUsable(40, false, 30, false));
    CHECK(FlightTransportDestinationUsable(40, false, 10, true));
    std::cout << "  [PASS] outgrown destinations refused except capitals\n";

    // 4. A bot sitting in a capital never flies to another capital (donor
    //    GetOptimalFlightDestinations guard: that just shuffles cities).
    CHECK(!FlightTransportDestinationUsable(40, true, 30, true));
    // ... but may still fly out to a level-fitting field.
    CHECK(FlightTransportDestinationUsable(40, true, 40, false));
    // ... and may fly into a capital from the field when it fits the band.
    CHECK(FlightTransportDestinationUsable(25, false, 30, true));
    std::cout << "  [PASS] capital-to-capital shuffle refused\n";

    // 5. Unknown area levels FAIL CLOSED: an unresolvable destination (level
    //    <= 0: unloaded tile, cross-map node) never boards. Failing open is
    //    how a low-level bot strands itself in a high-level zone with every
    //    local gate (#418/#428/#434) rejecting all content.
    CHECK(!FlightTransportDestinationUsable(11, false, 0, false));
    CHECK(!FlightTransportDestinationUsable(60, false, -2, false));
    std::cout << "  [PASS] unknown area levels fail closed\n";

    // 6. The leg must be worth the fare: long trip, landing meaningfully
    //    closer than the takeoff.
    CHECK(FLIGHT_TRANSPORT_MIN_TRIP_YD == 1500.0f);
    CHECK(FLIGHT_TRANSPORT_MIN_SAVED_YD == 500.0f);
    CHECK(FlightTransportLegWorthwhile(5000.0f, 5000.0f, 500.0f));
    CHECK(!FlightTransportLegWorthwhile(1000.0f, 1000.0f, 100.0f));
    CHECK(!FlightTransportLegWorthwhile(5000.0f, 5000.0f, 4800.0f));
    std::cout << "  [PASS] short or pointless legs refused\n";

    // 7. The bot pays its own fare above the class-trainer reserve.
    CHECK(FlightTransportAffordable(1000, 100, 500));
    CHECK(!FlightTransportAffordable(1000, 600, 500));
    CHECK(!FlightTransportAffordable(400, 100, 500));
    CHECK(FlightTransportAffordable(1000, 0, 500));
    CHECK(!FlightTransportAffordable(500, 0, 500));
    std::cout << "  [PASS] fare gated on trainer reserve\n";

    // 8. Full shape: a levelling bot's hop is inside the whole band.
    CHECK(FlightTransportDestinationUsable(11, false, 12, false));
    CHECK(!FlightTransportDestinationUsable(11, true, 60, true));
    std::cout << "  [PASS] levelling hop shape holds end to end\n";

    std::cout << "TortoiseBots flight transport policy tests passed.\n";
    return 0;
}
