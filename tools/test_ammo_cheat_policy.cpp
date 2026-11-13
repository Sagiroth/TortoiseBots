#include "../runtime/policy/provision/AmmoCheatPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::SuppressAmmoBuy;
using ai::VendorAmmoBuyAllowed;
using ai::CHEAT_AMMO_BUY_COOLDOWN_SECONDS;

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

    // Vendor ammo is capped by what the bot carries: an EQUIP answer is met
    // from the bag copy, the restock answer already counts carried stacks.
    CHECK(VendorAmmoBuyAllowed(false, 0.0f, false, 1000, 0) == true);   // empty slot, none carried
    CHECK(VendorAmmoBuyAllowed(false, 1.0f, false, 1000, 0) == false);  // carried: equip it instead
    CHECK(VendorAmmoBuyAllowed(false, 0.2f, true, 1000, 0) == false);
    CHECK(VendorAmmoBuyAllowed(true, 3.0f, false, 1000, 0) == true);    // restock below 8/2 need
    std::cout << "  [PASS] ammo buy capped by carried stacks\n";

    // Item-cheat bots: one ammo stack per per-bot cooldown, not per visit.
    int64_t const bought = 1000;
    int64_t const readyAt = bought + CHEAT_AMMO_BUY_COOLDOWN_SECONDS;
    CHECK(VendorAmmoBuyAllowed(false, 0.0f, true, bought + 5, readyAt) == false);  // next rpg buy tick
    CHECK(VendorAmmoBuyAllowed(false, 0.0f, true, readyAt - 1, readyAt) == false);
    CHECK(VendorAmmoBuyAllowed(false, 0.0f, true, readyAt, readyAt) == true);
    CHECK(VendorAmmoBuyAllowed(false, 0.0f, true, bought, 0) == true);             // never bought
    CHECK(VendorAmmoBuyAllowed(true, 0.0f, false, bought + 5, readyAt) == true);   // no cheat: no cooldown
    CHECK(CHEAT_AMMO_BUY_COOLDOWN_SECONDS == 300);
    std::cout << "  [PASS] item-cheat ammo buys wait out a per-bot cooldown\n";

    std::cout << "All ammo cheat vendor policy checks PASSED!\n";
    return 0;
}
