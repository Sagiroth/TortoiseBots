#include "../ai/playerbot/policy/combat/HealerItemPolicy.h"

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
    std::cout << "Starting TortoiseBots healer item policy tests...\n";

    // resto druid healer with a grenade: damage item refused
    CHECK(!ai::DamageItemAllowed(true));

    // damage dealers and tanks keep the items
    CHECK(ai::DamageItemAllowed(false));

    std::cout << "All healer item policy checks PASSED!\n";
    return 0;
}
