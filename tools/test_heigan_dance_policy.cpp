#include "../ai/playerbot/HeiganDancePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::HeiganDanceElapsed;
using ai::HeiganSafeArea;
using ai::HeiganSafeAreaNow;
using ai::HeiganSafeIndex;

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

    // Windows: eruption k at 4+3k s; stand in seq[k] for [t_k, t_{k+1})
    // with a 300ms early move. At 3.9s eruption 0 has NOT fired (fires
    // at 4s) — stand in seq[0], not seq[1].
    CHECK(HeiganSafeIndex(0) == 0);
    CHECK(HeiganSafeIndex(3699) == 0);
    CHECK(HeiganSafeIndex(3700) == 0);
    CHECK(HeiganSafeIndex(3900) == 0);
    CHECK(HeiganSafeIndex(4100) == 0);
    CHECK(HeiganSafeIndex(6800) == 1);
    CHECK(HeiganSafeIndex(7000) == 1);
    CHECK(HeiganSafeIndex(9800) == 2);
    CHECK(HeiganSafeIndex(12800) == 3);
    CHECK(HeiganSafeIndex(15800) == 4);
    // Past the last eruption (43s): clamp to the final window.
    CHECK(HeiganSafeIndex(43000) == 13);
    CHECK(HeiganSafeIndex(44000) == 13);
    CHECK(HeiganSafeIndex(60000) == 13);
    std::cout << "  [PASS] window index stands in the current section\n";

    CHECK(HeiganSafeAreaNow(0) == 0);
    CHECK(HeiganSafeAreaNow(3900) == 0);
    CHECK(HeiganSafeAreaNow(4100) == 0);
    CHECK(HeiganSafeAreaNow(6800) == 1);
    CHECK(HeiganSafeAreaNow(9800) == 2);
    CHECK(HeiganSafeAreaNow(12800) == 3);
    CHECK(HeiganSafeAreaNow(15800) == 2);
    std::cout << "  [PASS] spot choice follows the dance\n";

    // Clock from aura max - remaining; fallback total when max unknown.
    CHECK(HeiganDanceElapsed(45000, 45000) == 0);
    CHECK(HeiganDanceElapsed(45000, 41000) == 4000);
    CHECK(HeiganDanceElapsed(45000, 2000) == 43000);
    CHECK(HeiganDanceElapsed(0, 41000) == 4000);
    CHECK(HeiganDanceElapsed(-1, 45000) == 0);
    std::cout << "  [PASS] dance clock reads max-minus-remaining\n";

    std::cout << "All Heigan-dance policy tests passed.\n";
    return 0;
}
