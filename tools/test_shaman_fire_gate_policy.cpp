#include "../ai/playerbot/strategy/shaman/ShamanFireGatePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ChainHealWiredToMediumAoeHeal;
using ai::FireNovaShouldFire;
using ai::FireNovaTotemPulseRange;

int main()
{
    std::cout << "Starting shaman fire-gate policy tests...\n";

    // Pulse radius is the donor's 8 yards.
    CHECK(FireNovaTotemPulseRange() == 8.0f);
    std::cout << "  [PASS] pulse radius is 8 yards\n";

    // Nova fires only with a fire totem down and the target in pulse range.
    CHECK(FireNovaShouldFire(true, 5.0f) == true);
    CHECK(FireNovaShouldFire(true, 8.0f) == true);
    CHECK(FireNovaShouldFire(true, 8.1f) == false);
    CHECK(FireNovaShouldFire(true, 30.0f) == false);
    CHECK(FireNovaShouldFire(false, 0.0f) == false);
    CHECK(FireNovaShouldFire(false, 5.0f) == false);
    std::cout << "  [PASS] nova needs a fire totem with the target in range\n";

    // Chain heal stays wired to medium aoe heal (no new trigger).
    CHECK(ChainHealWiredToMediumAoeHeal() == true);
    std::cout << "  [PASS] chain heal wiring documented\n";

    std::cout << "All shaman fire-gate policy checks PASSED!\n";
    return 0;
}
