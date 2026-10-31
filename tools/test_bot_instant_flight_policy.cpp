#include "../ai/playerbot/BotInstantFlightPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

int main()
{
    std::cout << "Starting TortoiseBots bot instant flight policy tests...\n";

    // Switch off: never an instant arrival, even for a started flight.
    CHECK(ai::BotInstantFlightDestNode(false, true, {1, 2}) == 0);
    CHECK(ai::BotInstantFlightDestNode(false, false, {1, 2}) == 0);
    std::cout << "  [PASS] disabled switch finishes nothing\n";

    // Flight failed to start: nothing to finish.
    CHECK(ai::BotInstantFlightDestNode(true, false, {1, 2}) == 0);
    std::cout << "  [PASS] unstarted flight finishes nothing\n";

    // Empty route: no destination to resolve.
    CHECK(ai::BotInstantFlightDestNode(true, true, {}) == 0);
    std::cout << "  [PASS] empty route finishes nothing\n";

    // Started flight: arrive at the last node, single or multi-leg.
    CHECK(ai::BotInstantFlightDestNode(true, true, {3, 7}) == 7);
    CHECK(ai::BotInstantFlightDestNode(true, true, {3, 5, 7}) == 7);
    std::cout << "  [PASS] started flight arrives at last node\n";

    std::cout << "bot instant flight policy: OK\n";
    return 0;
}
