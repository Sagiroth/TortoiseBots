#include "../ai/playerbot/AnubrekhanSwarmPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsAnubrekhanSwarmUp;

int main()
{
    std::cout << "Starting TortoiseBots Anub'Rekhan-swarm policy tests...\n";

    CHECK(IsAnubrekhanSwarmUp(true));
    CHECK(!IsAnubrekhanSwarmUp(false));
    std::cout << "  [PASS] swarm detection is the boss self-buff\n";

    std::cout << "All Anub'Rekhan-swarm policy tests passed.\n";
    return 0;
}
