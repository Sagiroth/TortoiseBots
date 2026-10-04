// Standalone policy test for city-service errands (AH, vendor, repair,
// mail, trainer, city): arrival radii, stable approach offsets, the service
// predicate and the rescue gate. Mirrors ai/playerbot/ServiceTripPolicy.h;
// no server headers needed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_service_trip_policy.cpp -o /tmp/test_service_trip_policy && /tmp/test_service_trip_policy

#include "../ai/playerbot/ServiceTripPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::SERVICE_TRIP_COUNTER_ARRIVAL_YD;
using ai::SERVICE_TRIP_HALL_ARRIVAL_YD;
using ai::SERVICE_TRIP_PURPOSE_BITS;
using ai::SERVICE_TRIP_RESCUE_COOLDOWN_SEC;
using ai::ServiceTripArrivalRadius;
using ai::ServiceTripIsServicePurpose;
using ai::ServiceTripRescueEligible;
using ai::ServiceTripStableOffset;

int main()
{
    std::cout << "Starting TortoiseBots service trip policy tests...\n";

    // Service purposes: numeric AH/vendor/repair/mail/trainer ids, plus the
    // named city/trainer/mount/reagent-vendor requests. Quest/grind (empty,
    // "quest") and every other named leisure trip stay out.
    CHECK(ServiceTripIsServicePurpose("1024"));   // AH
    CHECK(ServiceTripIsServicePurpose("512"));    // Vendor
    CHECK(ServiceTripIsServicePurpose("256"));    // Repair
    CHECK(ServiceTripIsServicePurpose("128"));    // Trainer
    CHECK(ServiceTripIsServicePurpose("2048"));   // Mail
    CHECK(ServiceTripIsServicePurpose("city"));
    CHECK(ServiceTripIsServicePurpose("trainer class"));
    CHECK(ServiceTripIsServicePurpose("trainer trade"));
    CHECK(ServiceTripIsServicePurpose("mount"));
    CHECK(ServiceTripIsServicePurpose("reagent vendor"));
    CHECK(!ServiceTripIsServicePurpose(""));
    CHECK(!ServiceTripIsServicePurpose("quest"));
    CHECK(!ServiceTripIsServicePurpose("4096"));  // Grind
    CHECK(!ServiceTripIsServicePurpose("64"));    // GenericRpg
    CHECK(!ServiceTripIsServicePurpose("pvp"));
    CHECK(!ServiceTripIsServicePurpose("guild order"));
    CHECK(!ServiceTripIsServicePurpose("tabard"));
    CHECK(!ServiceTripIsServicePurpose("petition"));
    CHECK(SERVICE_TRIP_PURPOSE_BITS == ((1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11)));
    std::cout << "  [PASS] service predicate covers service errands only\n";

    // Arrival: counter NPCs from twice interaction range (the stall band),
    // banker/battlemaster halls from the room. Unknown flags fail to the
    // counter so arrival never widens by accident.
    CHECK(SERVICE_TRIP_COUNTER_ARRIVAL_YD == 10.0f);
    CHECK(SERVICE_TRIP_HALL_ARRIVAL_YD == 60.0f);
    CHECK(ServiceTripArrivalRadius(0x00001000) == SERVICE_TRIP_COUNTER_ARRIVAL_YD);  // auctioneer
    CHECK(ServiceTripArrivalRadius(0x00000004) == SERVICE_TRIP_COUNTER_ARRIVAL_YD);  // vendor
    CHECK(ServiceTripArrivalRadius(0) == SERVICE_TRIP_COUNTER_ARRIVAL_YD);
    CHECK(ServiceTripArrivalRadius(0x00000100) == SERVICE_TRIP_HALL_ARRIVAL_YD);     // banker
    CHECK(ServiceTripArrivalRadius(0x00000800) == SERVICE_TRIP_HALL_ARRIVAL_YD);     // battlemaster
    std::cout << "  [PASS] arrival radii: counter vs hall\n";

    // Stable offset: same inputs give the same approach (the old per-tick
    // urand never converged), two bots differ, every approach stays inside
    // the radius and off the exact spawn.
    {
        float ax, ay, bx, by, cx, cy;
        ServiceTripStableOffset(1234, 555, 100.0f, 200.0f, 5.0f, ax, ay);
        ServiceTripStableOffset(1234, 555, 100.0f, 200.0f, 5.0f, bx, by);
        ServiceTripStableOffset(9999, 555, 100.0f, 200.0f, 5.0f, cx, cy);
        CHECK(ax == bx && ay == by);
        CHECK(ax != cx || ay != cy);
        CHECK(std::sqrt(ax * ax + ay * ay) <= 5.0f);
        CHECK(std::sqrt(ax * ax + ay * ay) > 0.0f);
    }
    std::cout << "  [PASS] stable approach offset per (bot, destination)\n";

    // Rescue gate: every clause must hold. Forced (guild meeting) and
    // watched trips never rescue; cooldown holds one rescue per 30 min.
    CHECK(SERVICE_TRIP_RESCUE_COOLDOWN_SEC == 30 * 60);
    CHECK(ServiceTripRescueEligible(true, true, true, false, true, false, false, false));
    CHECK(!ServiceTripRescueEligible(false, true, true, false, true, false, false, false));  // below threshold
    CHECK(!ServiceTripRescueEligible(true, false, true, false, true, false, false, false));  // owned/hired
    CHECK(!ServiceTripRescueEligible(true, true, false, false, true, false, false, false));  // dead
    CHECK(!ServiceTripRescueEligible(true, true, true, true, true, false, false, false));    // busy
    CHECK(!ServiceTripRescueEligible(true, true, true, false, false, false, false, false));  // cross-map
    CHECK(!ServiceTripRescueEligible(true, true, true, false, true, true, false, false));    // watched
    CHECK(!ServiceTripRescueEligible(true, true, true, false, true, false, true, false));    // cooling down
    CHECK(!ServiceTripRescueEligible(true, true, true, false, true, false, false, true));    // forced
    std::cout << "  [PASS] rescue gate holds every conservative clause\n";

    std::cout << "service trip policy: OK\n";
    return 0;
}
