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
using ai::BagPressureSaleRelievesBags;
using ai::ORDER_BAG_PRESSURE_MIN_SELL_SLOTS;

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

    // Order-stock bag pressure: the walk is worth it only when the sale frees
    // a bag row or gets the bags under the pressure line.
    CHECK(ORDER_BAG_PRESSURE_MIN_SELL_SLOTS == 4);
    CHECK(!BagPressureSaleRelievesBags(70, 72, 1, 85));
    CHECK(!BagPressureSaleRelievesBags(70, 72, 3, 85));
    CHECK(BagPressureSaleRelievesBags(70, 72, 4, 85));
    CHECK(BagPressureSaleRelievesBags(62, 72, 1, 85));
    CHECK(!BagPressureSaleRelievesBags(72, 72, 0, 85));
    // Completely full: no more loot can arrive, so 1-3 sellable slots still go (Codex review).
    CHECK(BagPressureSaleRelievesBags(72, 72, 1, 85));
    CHECK(BagPressureSaleRelievesBags(72, 72, 3, 85));
    CHECK(!BagPressureSaleRelievesBags(18, 20, 1, 85));
    CHECK(BagPressureSaleRelievesBags(0, 0, 0, 85));
    CHECK(BagPressureSaleRelievesBags(2, 16, 3, 85));

    std::cout << "vendor trip policy: OK\n";
    return 0;
}
