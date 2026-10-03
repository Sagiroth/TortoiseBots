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

    // Reachability trace: the reviewer showed the old fallback (inside the
    // n<10 usage loop, after `if (!usageAllowed) break;`) was dead for
    // NONE-classified stock. The new pass sits per vendor item BEFORE that
    // loop, so each gate below is evaluated for NONE stock too:
    //   candidate(2,7)=true -> master/hire/random gates -> not-owned ->
    //   QueryItemUsageForEquip==EQUIP -> affordable(price,money,reserve) ->
    //   BuyItem -> equip upgrades. At most one weapon per Execute
    //   (boughtWeapon), so the trace ends after the first upgrade.
    CHECK(IsVendorWeaponUpgradeCandidate(2, 7) == true);      // reached: gear weapon
    CHECK(VendorWeaponUpgradeAffordable(54, 500, 100) == true); // reached: purse side ok
    CHECK(VendorWeaponUpgradeAffordable(54, 58, 100) == false); // blocked: reserve first
    std::cout << "  [PASS] weapon-pass gates evaluate before the usage loop\n";

    std::cout << "All vendor weapon upgrade policy checks PASSED!\n";
    return 0;
}
