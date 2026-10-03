#include "../runtime/TradingLeasePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::kTradingTripLeaseMaxMs;
using TortoiseBots::kTradingTripLeaseMinMs;
using TortoiseBots::TradingTripLeaseMs;

int main()
{
    std::cout << "Starting TortoiseBots trading-lease policy tests...\n";

    // 1. Bounds hold: the lease never drops below the floor or above the cap.
    CHECK(kTradingTripLeaseMinMs == 180000);
    CHECK(kTradingTripLeaseMaxMs == 600000);
    CHECK(TradingTripLeaseMs(0) == kTradingTripLeaseMinMs);
    CHECK(TradingTripLeaseMs(300000) == kTradingTripLeaseMaxMs);
    std::cout << "  [PASS] floor and cap hold\n";

    // 2. The #404 case: the old 120 s lease raced the post at the default
    // 120 s tick. Two ticks + margin now clear it (120 s -> 300 s lease).
    CHECK(TradingTripLeaseMs(120000) == 300000);
    std::cout << "  [PASS] default 120 s tick leases 300 s\n";

    // 3. Short ticks still pay the floor (teleport + load margin): a 5 s
    // tick leases the 180 s floor, not 2x5 s.
    CHECK(TradingTripLeaseMs(5000) == kTradingTripLeaseMinMs);
    CHECK(TradingTripLeaseMs(1000) == kTradingTripLeaseMinMs);
    std::cout << "  [PASS] short ticks keep the floor\n";

    // 4. Mid-range ticks scale: a 60 s tick leases two ticks + margin.
    CHECK(TradingTripLeaseMs(60000) == 180000);
    // A 200 s tick leases two ticks + margin, under the cap.
    CHECK(TradingTripLeaseMs(200000) == 460000);
    std::cout << "  [PASS] mid-range ticks scale with the interval\n";

    std::cout << "trading-lease policy: OK\n";
    return 0;
}
