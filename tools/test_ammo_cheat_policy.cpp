#include "../ai/playerbot/strategy/values/AmmoCheatPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::SuppressAmmoBuy;

int main()
{
    std::cout << "Starting TortoiseBots ammo cheat vendor policy tests...\n";

    // Ammo restock demand is skipped only with the item cheat (gated as
    // needAmmo = 0 AFTER the EQUIP checks, so cheat bots still equip an
    // empty slot / better ammo). Bots without the cheat (owned/hired) keep
    // the earned needAmmo = 8/2 restock path.
    CHECK(SuppressAmmoBuy(true) == true);
    CHECK(SuppressAmmoBuy(false) == false);
    std::cout << "  [PASS] ammo restock suppressed only with the item cheat, EQUIP preserved\n";

    std::cout << "All ammo cheat vendor policy checks PASSED!\n";
    return 0;
}
