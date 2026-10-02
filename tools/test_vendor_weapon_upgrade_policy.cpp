#include "../ai/playerbot/strategy/values/VendorWeaponUpgradePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsVendorWeaponUpgradeCandidate;
using ai::VendorWeaponUpgradeAffordable;

int main()
{
    std::cout << "Starting TortoiseBots vendor weapon upgrade policy tests...\n";

    // Candidate gate: gear weapons pass, tools and ammo never do. A fishing
    // pole or thrown stack must not read as a weapon upgrade (the fish action
    // and the ammo paths own those subclasses).
    CHECK(IsVendorWeaponUpgradeCandidate(2, 7) == true);    // sword
    CHECK(IsVendorWeaponUpgradeCandidate(2, 4) == true);    // mace
    CHECK(IsVendorWeaponUpgradeCandidate(2, 15) == true);   // dagger
    CHECK(IsVendorWeaponUpgradeCandidate(2, 13) == true);   // fist
    CHECK(IsVendorWeaponUpgradeCandidate(4, 1) == false);   // armor, not a weapon
    CHECK(IsVendorWeaponUpgradeCandidate(2, 14) == false);  // MISC profession tool
    CHECK(IsVendorWeaponUpgradeCandidate(2, 16) == false);  // THROWN ammo
    CHECK(IsVendorWeaponUpgradeCandidate(2, 20) == false);  // fishing pole
    std::cout << "  [PASS] candidate gate admits gear weapons, rejects tools/ammo\n";

    // Affordability: price must fit live money minus the spell reserve. Money
    // for the next trainer ranks comes first, so a broke bot buys nothing.
    CHECK(VendorWeaponUpgradeAffordable(54, 500, 100) == true);    // Shortsword fits
    CHECK(VendorWeaponUpgradeAffordable(54, 100, 100) == false);   // exact reserve: no spend
    CHECK(VendorWeaponUpgradeAffordable(54, 58, 100) == false);    // Fyrmoryr: 58c, reserve first
    CHECK(VendorWeaponUpgradeAffordable(0, 0, 0) == true);         // free item never blocked
    CHECK(VendorWeaponUpgradeAffordable(54, 0, 0) == false);       // broke bot buys nothing
    std::cout << "  [PASS] trainer reserve is kept before any weapon buy\n";

    std::cout << "All vendor weapon upgrade policy checks PASSED!\n";
    return 0;
}
