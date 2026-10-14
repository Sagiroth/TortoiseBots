#pragma once

#include <cstdint>

// Warlock off-hand stones (WAR-4): which conjured stone the bot may put in
// its off-hand. Vanilla's Spellstone (5522/13602/13603) and Firestone
// (1254/13699-13701) are held off-hand items (inventory_type 23). Turtle
// 1.18.1 also has a "Spellstone" (51933) and a "Firestone" (51932) that are
// plain bag consumables (inventory_type 0, used from the bag). Equipping
// one of those fails in the core every time, so the old name-only check
// picked the equip action again on every tick. Pure decision logic,
// unit-tested in tools/test_warlock_stone_policy.cpp; the triggers and the
// equip actions in strategy/warlock wire it to the live items.

namespace TortoiseBots
{

// INVTYPE_HOLDABLE in the core's ItemPrototype.h.
constexpr uint32_t kInventoryTypeHoldable = 23;
// INVTYPE_2HWEAPON in the core's ItemPrototype.h.
constexpr uint32_t kInventoryType2HWeapon = 17;

struct WarlockStoneSlotFacts
{
    bool offHandEmpty = false;
    bool hasMainHand = false;
    uint32_t mainHandInventoryType = 0;
};

// The stone itself must be a holdable off-hand item.
inline bool IsEquippableWarlockStone(uint32_t stoneInventoryType)
{
    return stoneInventoryType == kInventoryTypeHoldable;
}

// Never displace worn gear, and a two-handed main hand (or none) leaves
// nowhere to hold the stone.
inline bool CanHoldWarlockStone(uint32_t stoneInventoryType, WarlockStoneSlotFacts const& slots)
{
    if (!IsEquippableWarlockStone(stoneInventoryType))
        return false;
    if (!slots.offHandEmpty || !slots.hasMainHand)
        return false;
    return slots.mainHandInventoryType != kInventoryType2HWeapon;
}

} // namespace TortoiseBots
