// Standalone test for hunter ammo bag equip/upgrade decisions.
//
//   g++ -std=c++17 -Wall -Wextra tools/test_ammo_bag_policy.cpp -o /tmp/test_ammo_bag_policy

#include "../ai/playerbot/AmmoBagPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    AmmoBagSpec const smallAmmoPouch{ AmmoBagKind::Pouch, 6, 10, 1 };
    AmmoBagSpec const smallShotPouch{ AmmoBagKind::Pouch, 8, 10, 1 };
    AmmoBagSpec const smallShotPouchLowerIlvl{ AmmoBagKind::Pouch, 8, 5, 1 };
    AmmoBagSpec const lightQuiver{ AmmoBagKind::Quiver, 8, 10, 1 };
    AmmoBagSpec const heavyQuiver{ AmmoBagKind::Quiver, 14, 30, 1 };
    AmmoBagSpec const noneBag{};

    // Non-hunters never equip or keep ammo bags.
    CHECK(!ShouldEquipAmmoBag(false, AmmoBagKind::Quiver, lightQuiver, false, noneBag));
    CHECK(!ShouldEquipAmmoBag(false, AmmoBagKind::Pouch, smallShotPouch, false, noneBag));

    // Hunter without matching ranged weapon never equips ammo bags.
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::None, lightQuiver, false, noneBag));
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::None, smallShotPouch, false, noneBag));

    // Unknown candidate kind is rejected.
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::Quiver, noneBag, false, noneBag));

    // First matching ammo bag when none is equipped.
    CHECK(ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, smallAmmoPouch, false, noneBag));
    CHECK(ShouldEquipAmmoBag(true, AmmoBagKind::Quiver, lightQuiver, false, noneBag));

    // Weapon/bag type mismatch is rejected.
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, lightQuiver, false, noneBag));
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::Quiver, smallShotPouch, false, noneBag));

    // Larger matching bag upgrades even at equal or lower item level.
    CHECK(ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, smallShotPouch, true, smallAmmoPouch));
    CHECK(ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, smallShotPouchLowerIlvl, true, smallAmmoPouch));

    // Duplicate or smaller matching bag is not an upgrade (becomes junk).
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, smallShotPouch, true, smallShotPouch));
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, smallAmmoPouch, true, smallShotPouch));

    // Smaller slot count never replaces a larger bag even with higher ilvl/quality.
    AmmoBagSpec const smallHighIlvlPouch{ AmmoBagKind::Pouch, 6, 40, 2 };
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, smallHighIlvlPouch, true, smallShotPouch));

    // Equal slot count breaks ties by itemLevel, then quality.
    AmmoBagSpec const pouch8Ilvl15{ AmmoBagKind::Pouch, 8, 15, 1 };
    AmmoBagSpec const pouch8Ilvl15Green{ AmmoBagKind::Pouch, 8, 15, 2 };
    CHECK(ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, pouch8Ilvl15, true, smallShotPouch));
    CHECK(ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, pouch8Ilvl15Green, true, pouch8Ilvl15));
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, pouch8Ilvl15, true, pouch8Ilvl15Green));

    // Weapon type change: swap to matching bag even if the wrong-type bag is larger.
    CHECK(ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, smallShotPouch, true, heavyQuiver));
    CHECK(ShouldEquipAmmoBag(true, AmmoBagKind::Quiver, lightQuiver, true, smallShotPouch));
    CHECK(!ShouldEquipAmmoBag(true, AmmoBagKind::Pouch, heavyQuiver, true, smallShotPouch));

    std::printf("ammo bag policy: %d checks passed\n", checks);
    return 0;
}
