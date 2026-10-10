#include "../ai/playerbot/strategy/shaman/ShamanEarthShockPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::EarthShockExecuteHealthPercent;
using ai::EarthShockExecuteShouldFire;

int main()
{
    std::cout << "Starting shaman earth-shock execute policy tests...\n";

    // Execute window: below 25% AND below 1500 hp.
    CHECK(EarthShockExecuteShouldFire(100.0f, 1000.0f) == true);
    CHECK(EarthShockExecuteShouldFire(374.0f, 1500.0f) == true);
    std::cout << "  [PASS] wounded low-hp targets take the execute\n";

    // Healthy percent vetoes, even on tiny targets (boss/add discipline).
    CHECK(EarthShockExecuteShouldFire(500.0f, 1000.0f) == false);
    CHECK(EarthShockExecuteShouldFire(250.0f, 1000.0f) == false);
    CHECK(EarthShockExecuteShouldFire(25.0f, 100.0f) == false);
    std::cout << "  [PASS] healthy targets never take the shock\n";

    // Absolute-hp vetoes: a 24% boss with 100k hp is not an execute.
    CHECK(EarthShockExecuteShouldFire(24000.0f, 100000.0f) == false);
    CHECK(EarthShockExecuteShouldFire(1500.0f, 10000.0f) == false);
    CHECK(EarthShockExecuteShouldFire(1499.0f, 10000.0f) == true);
    std::cout << "  [PASS] high-hp targets vetoed even below 25 percent\n";

    // Boundaries and degenerate input.
    CHECK(EarthShockExecuteShouldFire(1.0f, 1000.0f) == true);
    CHECK(EarthShockExecuteHealthPercent(0.0f, 0.0f) == 100.0f);
    CHECK(EarthShockExecuteShouldFire(0.0f, 0.0f) == false);
    // Dead targets never take the execute (matches the trigger IsAlive gate).
    CHECK(EarthShockExecuteShouldFire(100.0f, 1000.0f, false) == false);
    CHECK(EarthShockExecuteShouldFire(100.0f, 1000.0f, true) == true);
    std::cout << "  [PASS] boundaries, zero-max-health, and dead targets safe\n";

    std::cout << "All shaman earth-shock execute policy checks PASSED!\n";
    return 0;
}
