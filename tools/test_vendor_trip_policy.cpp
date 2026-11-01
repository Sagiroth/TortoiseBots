#include "../ai/playerbot/policy/gear/VendorTripPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::VENDOR_TRIP_PURPOSE_ID;
using ai::VENDOR_TRIP_REPICK_WINDOW;
using ai::VendorTripAlreadyUnderway;
using ai::VendorTripSuppressedByRecentTrip;

int main()
{
    // The window matches the trainer window and the fruitless-errand parks.
    CHECK(VENDOR_TRIP_REPICK_WINDOW == 600);

    std::time_t const now = 1'000'000;

    // No trip picked yet: never suppressed.
    CHECK(!VendorTripSuppressedByRecentTrip(0, now));

    // A just-picked trip suppresses the next request.
    CHECK(VendorTripSuppressedByRecentTrip(now, now));
    CHECK(VendorTripSuppressedByRecentTrip(now - 1, now));
    CHECK(VendorTripSuppressedByRecentTrip(now - (VENDOR_TRIP_REPICK_WINDOW - 1), now));

    // The window ends exactly at its edge: the bot may walk again.
    CHECK(!VendorTripSuppressedByRecentTrip(now - VENDOR_TRIP_REPICK_WINDOW, now));
    CHECK(!VendorTripSuppressedByRecentTrip(now - VENDOR_TRIP_REPICK_WINDOW - 1, now));

    // An ancient trip never suppresses.
    // An already-picked vendor trip is its own answer: the valve must not
    // re-request while it is held, whatever its status. Any other held
    // purpose (or no destination) keeps the old behaviour.
    CHECK(VENDOR_TRIP_PURPOSE_ID == 512);
    CHECK(VendorTripAlreadyUnderway(true, 512));
    CHECK(!VendorTripAlreadyUnderway(true, 4096));
    CHECK(!VendorTripAlreadyUnderway(true, 131072));
    CHECK(!VendorTripAlreadyUnderway(false, 512));
    CHECK(!VendorTripAlreadyUnderway(false, 0));

    std::cout << "vendor trip policy: OK\n";
    return 0;
}
