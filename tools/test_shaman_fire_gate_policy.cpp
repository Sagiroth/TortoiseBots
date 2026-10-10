#include "../ai/playerbot/strategy/shaman/ShamanFireGatePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FireNovaDropRange;
using ai::FireNovaDropShouldFire;

int main()
{
    std::cout << "Starting shaman fire-gate policy tests...\n";

    // Drop range matches the totem-placement idiom (10y, like CastTotemAction).
    CHECK(FireNovaDropRange() == 10.0f);
    std::cout << "  [PASS] drop range is 10 yards\n";

    // The totem lands at our feet: in-range targets detonate, far ones waste
    // the drop (and destroy the current fire totem for nothing).
    CHECK(FireNovaDropShouldFire(5.0f) == true);
    CHECK(FireNovaDropShouldFire(10.0f) == true);
    CHECK(FireNovaDropShouldFire(10.1f) == false);
    CHECK(FireNovaDropShouldFire(30.0f) == false);
    std::cout << "  [PASS] out-of-range targets veto the drop\n";

    std::cout << "All shaman fire-gate policy checks PASSED!\n";
    return 0;
}
