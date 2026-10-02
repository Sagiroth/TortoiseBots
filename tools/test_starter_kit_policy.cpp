// Standalone regression test for the #401 starter-kit policy: which bags, soul
// bag and ammo container a bot owns, and which container fits a ranged weapon.
// Server data, not guesses: 3914 Journeyman's Backpack (14 slots), 22243 Small
// Soul Pouch (12, warlock-only); subclass numbers mirror ItemPrototype.h,
// class numbers SharedDefines.h (hunter 3, warlock 9).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_starter_kit_policy.cpp -o /tmp/test_starter_kit
//   /tmp/test_starter_kit

#include "../runtime/StarterKitPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// Gun -> pouch, bow/crossbow -> quiver, everything else -> none.
static void TestContainerForWeapon()
{
    CHECK(ContainerSubclassForWeapon(WEAPON_SUBCLASS_GUN) == CONTAINER_SUBCLASS_AMMO_POUCH);
    CHECK(ContainerSubclassForWeapon(WEAPON_SUBCLASS_BOW) == CONTAINER_SUBCLASS_QUIVER);
    CHECK(ContainerSubclassForWeapon(WEAPON_SUBCLASS_CROSSBOW) == CONTAINER_SUBCLASS_QUIVER);
    CHECK(ContainerSubclassForWeapon(WEAPON_SUBCLASS_THROWN) == 0);
    CHECK(ContainerSubclassForWeapon(0) == 0);
    CHECK(ContainerSubclassForWeapon(19) == 0);
}

// Only warlocks get a soul bag, and it is the small pouch.
static void TestSoulBagForClass()
{
    CHECK(SoulBagEntryForClass(CLASS_WARLOCK_ID) == STARTER_SOUL_BAG_ENTRY);
    CHECK(SoulBagEntryForClass(9) == STARTER_SOUL_BAG_ENTRY);
    CHECK(SoulBagEntryForClass(CLASS_HUNTER_ID) == 0);
    CHECK(SoulBagEntryForClass(1) == 0);
    CHECK(SoulBagEntryForClass(11) == 0);
}

// The starter set is three plain bags: fewer means seed, three or more not.
static void TestNeedsStarterBags()
{
    CHECK(NeedsStarterBags(0));
    CHECK(NeedsStarterBags(1));
    CHECK(NeedsStarterBags(2));
    CHECK(!NeedsStarterBags(3));
    CHECK(!NeedsStarterBags(4));
}

// The pinned entries are server data: a stray edit faces this test.
static void TestPinnedEntries()
{
    CHECK(STARTER_PLAIN_BAG_ENTRY == 3914);
    CHECK(STARTER_PLAIN_BAG_COUNT == 3);
    CHECK(STARTER_SOUL_BAG_ENTRY == 22243);
}

// Every class owns three plain bags; the fourth slot holds the hunter quiver
// or the warlock soul pouch (owner spec), so the cap is 3 across the board.
static void TestMaxPlainBagsForClass()
{
    CHECK(MaxPlainBagsForClass(CLASS_HUNTER_ID) == 3);
    CHECK(MaxPlainBagsForClass(CLASS_WARLOCK_ID) == 3);
    CHECK(MaxPlainBagsForClass(1) == 3);
    CHECK(MaxPlainBagsForClass(4) == 3);
    CHECK(MaxPlainBagsForClass(11) == 3);
}

int main()
{
    TestContainerForWeapon();
    TestSoulBagForClass();
    TestNeedsStarterBags();
    TestPinnedEntries();
    TestMaxPlainBagsForClass();
    std::printf("starter kit policy: %d checks passed\n", checks);
    return 0;
}
