#include "../ai/playerbot/LongStuckRescuePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::LongStuckFallbackTarget;
using ai::PickLongStuckFallbackTarget;

int main()
{
    std::cout << "Starting TortoiseBots long-stuck rescue policy tests...\n";

    // Crurcar (issue #400): scattered ~25k yd from its homebind, wedged where
    // it cannot walk out - the nearest graveyard is a genuine relocation.
    CHECK(PickLongStuckFallbackTarget(true, true, 25368.0f, 25368.0f) == LongStuckFallbackTarget::Graveyard);
    std::cout << "  [PASS] far graveyard wins over a far homebind\n";

    // A bot wedged next to its graveyard gains nothing from teleporting there:
    // homebind (its racial start inn for a pool bot) moves it instead.
    CHECK(PickLongStuckFallbackTarget(true, true, 50.0f, 5000.0f) == LongStuckFallbackTarget::Homebind);
    std::cout << "  [PASS] near graveyard falls through to homebind\n";

    // Nowhere far to go (stuck inside the home inn's graveyard): fail closed
    // rather than looping a no-op teleport every trip.
    CHECK(PickLongStuckFallbackTarget(true, true, 30.0f, 30.0f) == LongStuckFallbackTarget::None);
    std::cout << "  [PASS] near graveyard and near homebind refuses\n";

    // No graveyard found: homebind is the only relocation left.
    CHECK(PickLongStuckFallbackTarget(true, false, 0.0f, 5000.0f) == LongStuckFallbackTarget::Homebind);
    CHECK(PickLongStuckFallbackTarget(true, false, 0.0f, 30.0f) == LongStuckFallbackTarget::None);
    CHECK(PickLongStuckFallbackTarget(true, false, 0.0f, 0.0f) == LongStuckFallbackTarget::None);
    std::cout << "  [PASS] missing graveyard degrades to homebind or refusal\n";

    // Battleground bots and bots with an actively played master are never
    // yanked, however far everything is.
    CHECK(PickLongStuckFallbackTarget(false, true, 25368.0f, 25368.0f) == LongStuckFallbackTarget::None);
    std::cout << "  [PASS] disallowed rescue refuses outright\n";

    // Boundary: the relocation must EXCEED 100 yd. Exactly 100 yd still counts
    // as "standing on the same spot" and falls through.
    CHECK(PickLongStuckFallbackTarget(true, true, 100.0f, 5000.0f) == LongStuckFallbackTarget::Homebind);
    CHECK(PickLongStuckFallbackTarget(true, true, 100.01f, 5000.0f) == LongStuckFallbackTarget::Graveyard);
    CHECK(PickLongStuckFallbackTarget(true, false, 0.0f, 100.0f) == LongStuckFallbackTarget::None);
    std::cout << "  [PASS] 100-yd boundary is strict\n";

    std::cout << "long-stuck rescue policy: OK\n";
    return 0;
}
