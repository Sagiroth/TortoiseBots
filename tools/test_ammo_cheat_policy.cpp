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
using ai::SuppressNonHunterQuiverBuy;

int main()
{
    std::cout << "Starting TortoiseBots ammo cheat vendor policy tests...\n";

    // Ammo: a bot with the item cheat refills its stack every tick, so a
    // vendor ammo buy is never a restock. Bots without the cheat
    // (owned/hired) keep the earned restock path.
    CHECK(SuppressAmmoBuy(true) == true);
    CHECK(SuppressAmmoBuy(false) == false);
    std::cout << "  [PASS] ammo buys suppressed only with the item cheat\n";

    // Quivers: an ammo container with no ranged kit is a strictly worse bag,
    // so a cheat bot of another class must not read it as a plain-bag
    // upgrade. Hunters keep their dedicated quiver path; no-cheat bots are
    // untouched either way.
    CHECK(SuppressNonHunterQuiverBuy(true, false) == true);   // cheat priest
    CHECK(SuppressNonHunterQuiverBuy(true, true) == false);    // cheat hunter
    CHECK(SuppressNonHunterQuiverBuy(false, false) == false);  // owned priest
    CHECK(SuppressNonHunterQuiverBuy(false, true) == false);   // owned hunter
    std::cout << "  [PASS] quiver gate hits cheat non-hunters only\n";

    // No-cheat regression: the usage classifier must still be able to answer
    // AMMO/EQUIP for these bots (the gates above return false, so the
    // existing branches run unchanged).
    CHECK(SuppressAmmoBuy(false) == false && SuppressNonHunterQuiverBuy(false, false) == false);
    std::cout << "  [PASS] no-cheat bots keep ammo/quiver buys\n";

    std::cout << "All ammo cheat vendor policy checks PASSED!\n";
    return 0;
}
