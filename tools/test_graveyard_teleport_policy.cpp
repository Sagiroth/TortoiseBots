#include "../ai/playerbot/GraveyardTeleportPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsUsableGraveyardTarget;

int main()
{
    std::cout << "Starting TortoiseBots graveyard teleport policy tests...\n";

    // Normal death: same-map graveyard a few hundred yards away is usable.
    CHECK(IsUsableGraveyardTarget(true, true, 200.0f));
    std::cout << "  [PASS] nearby same-map graveyard is usable\n";

    // Thorjarhin (night 2026-10-03/04): horde bot stuck in Teldrassil got the
    // Booty Bay graveyard (entryFar, other map). Never follow it across maps.
    CHECK(!IsUsableGraveyardTarget(true, false, 200.0f));
    CHECK(!IsUsableGraveyardTarget(true, false, 0.0f));
    std::cout << "  [PASS] cross-map graveyard refused however near it claims\n";

    // Micra/Nudravyrn/Grarcrarsh: same-map picks 12-17k yd away (Dun Morogh
    // -> invalid map-0 point, Elwynn GY from a transport-offset lookup).
    // Legit landings top out ~1400 yd; these are relocations, not repops.
    CHECK(!IsUsableGraveyardTarget(true, true, 12135.0f));
    CHECK(!IsUsableGraveyardTarget(true, true, 17378.0f));
    std::cout << "  [PASS] absurdly far same-map graveyard refused\n";

    // Boundary: exactly 5000 yd still counts, past it does not.
    CHECK(IsUsableGraveyardTarget(true, true, 5000.0f));
    CHECK(!IsUsableGraveyardTarget(true, true, 5000.01f));
    std::cout << "  [PASS] 5000-yd boundary is inclusive\n";

    // No graveyard found: callers fall back to homebind/spawn.
    CHECK(!IsUsableGraveyardTarget(false, true, 200.0f));
    CHECK(!IsUsableGraveyardTarget(false, false, 0.0f));
    std::cout << "  [PASS] missing graveyard refused\n";

    std::cout << "graveyard teleport policy: OK\n";
    return 0;
}
