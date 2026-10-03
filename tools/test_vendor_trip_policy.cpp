#include "../ai/playerbot/strategy/values/VendorTripPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::VENDOR_TRIP_REPICK_WINDOW;
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
    CHECK(!VendorTripSuppressedByRecentTrip(1, now));

    std::cout << "vendor trip policy: OK\n";
    return 0;
}
