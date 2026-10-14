// Standalone regression test for the warlock off-hand stone gate
// (runtime/WarlockStonePolicy.h, WAR-4): only a holdable stone goes into an
// empty off-hand beside a one-handed weapon. Turtle's bag-consumable
// Spellstone (51933) / Firestone (51932) must never be picked for equipping.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_warlock_stone_policy.cpp -o /tmp/test_warlock_stone
//   /tmp/test_warlock_stone

#include "../runtime/WarlockStonePolicy.h"

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

static WarlockStoneSlotFacts OneHanderEmptyOffHand()
{
    WarlockStoneSlotFacts slots;
    slots.offHandEmpty = true;
    slots.hasMainHand = true;
    slots.mainHandInventoryType = 13; // INVTYPE_WEAPON
    return slots;
}

int main()
{
    // Vanilla holdable stone (inventory_type 23) beside a one-hander.
    CHECK(IsEquippableWarlockStone(23));
    CHECK(CanHoldWarlockStone(23, OneHanderEmptyOffHand()));

    // Turtle 1.18.1 bag consumable (inventory_type 0): never equipped, so the
    // equip action is not picked every tick (the live 41x FAILED loop).
    CHECK(!IsEquippableWarlockStone(0));
    CHECK(!CanHoldWarlockStone(0, OneHanderEmptyOffHand()));

    // Main-hand-only weapon (21) is still a one-hander.
    WarlockStoneSlotFacts mainHandOnly = OneHanderEmptyOffHand();
    mainHandOnly.mainHandInventoryType = 21;
    CHECK(CanHoldWarlockStone(23, mainHandOnly));

    // Worn off-hand: never displaced.
    WarlockStoneSlotFacts wornOffHand = OneHanderEmptyOffHand();
    wornOffHand.offHandEmpty = false;
    CHECK(!CanHoldWarlockStone(23, wornOffHand));

    // Staff (two-handed) or empty main hand: nowhere to hold it.
    WarlockStoneSlotFacts staff = OneHanderEmptyOffHand();
    staff.mainHandInventoryType = 17;
    CHECK(!CanHoldWarlockStone(23, staff));
    WarlockStoneSlotFacts noMainHand = OneHanderEmptyOffHand();
    noMainHand.hasMainHand = false;
    CHECK(!CanHoldWarlockStone(23, noMainHand));

    std::printf("test_warlock_stone_policy: %d checks passed\n", checks);
    return 0;
}
