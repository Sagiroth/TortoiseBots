#include "../ai/playerbot/HeiganDancePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::HeiganNextEruption;
using ai::HeiganSafeArea;
using ai::HeiganSafeAreaNow;
using ai::IsHeiganDanceUp;

int main()
{
    std::cout << "Starting TortoiseBots Heigan-dance policy tests...\n";

    // Safe walk: 0,1,2,3,2,1 repeating.
    CHECK(HeiganSafeArea(0) == 0);
    CHECK(HeiganSafeArea(1) == 1);
    CHECK(HeiganSafeArea(2) == 2);
    CHECK(HeiganSafeArea(3) == 3);
    CHECK(HeiganSafeArea(4) == 2);
    CHECK(HeiganSafeArea(5) == 1);
    CHECK(HeiganSafeArea(6) == 0);
    CHECK(HeiganSafeArea(7) == 1);
    std::cout << "  [PASS] safe-area walk matches core section cycle\n";

    // Eruptions at 4s, 7s, 10s...; 1s move lead picks the next one.
    CHECK(HeiganNextEruption(0) == 0);
    CHECK(HeiganNextEruption(3000) == 1);
    CHECK(HeiganNextEruption(4000) == 1);
    CHECK(HeiganNextEruption(6500) == 2);
    CHECK(HeiganNextEruption(43000) == 14);
    std::cout << "  [PASS] eruption clock uses 4s-first / 3s-cadence\n";

    // Spot choice folds clock + walk together.
    CHECK(HeiganSafeAreaNow(0) == 0);
    CHECK(HeiganSafeAreaNow(3000) == 1);
    CHECK(HeiganSafeAreaNow(6500) == 2);
    CHECK(HeiganSafeAreaNow(9500) == 3);
    CHECK(HeiganSafeAreaNow(12500) == 2);
    std::cout << "  [PASS] spot choice follows the dance\n";

    CHECK(IsHeiganDanceUp(true));
    CHECK(!IsHeiganDanceUp(false));
    std::cout << "  [PASS] dance detection is the Plague Cloud aura\n";

    std::cout << "All Heigan-dance policy tests passed.\n";
    return 0;
}
