#pragma once
#include <cstdint>

// Pure policy for pool-bot vendor weapon and shield upgrades: a bot standing
// at a vendor during an existing errand buys a weapon or shield that is a
// real upgrade (spec-allowed, usable now, better by the module's own scoring)
// when it can afford it out of its own gold while keeping the next trainer
// ranks funded. No free gear, no gold injection, no extra shopping trips.
//
// The upgrade check itself reuses ItemUsageValue::QueryItemUsageForEquip (the
// same EQUIP answer the equip audit uses); the functions below are the pure
// parts (tool/ammo filter, trainer-first affordability) so they can be tested
// standalone without core objects.

namespace ai
{
    // ITEM_CLASS_WEAPON in ItemPrototype.h. Kept literal so this header stays
    // core-free for the standalone g++ policy test.
    constexpr uint32_t VENDOR_WEAPON_ITEM_CLASS = 2;
    // ITEM_CLASS_ARMOR in ItemPrototype.h: shields ride the armor class.
    constexpr uint32_t VENDOR_SHIELD_ITEM_CLASS = 4;
    // ITEM_SUBCLASS_ARMOR_SHIELD in ItemPrototype.h.
    constexpr uint32_t VENDOR_SHIELD_ITEM_SUBCLASS = 6;
    // Weapon subclasses that are tools or ammo, never gear upgrades:
    // MISC (14, profession tools), THROWN (16, rogue/warrior ammo),
    // FISHING_POLE (20, fishing tool). See ItemSubclassWeapon in ItemPrototype.h.
    constexpr uint32_t VENDOR_WEAPON_SUBCLASS_MISC = 14;
    constexpr uint32_t VENDOR_WEAPON_SUBCLASS_THROWN = 16;
    constexpr uint32_t VENDOR_WEAPON_SUBCLASS_FISHING_POLE = 20;

    // True when the prototype could be a gear weapon or a shield (not a tool
    // or ammo). Spec allowlist, CanUseItem and the better-by-scoring compare
    // stay in QueryItemUsageForEquip (usage EQUIP), which the caller checks.
    inline bool IsVendorWeaponUpgradeCandidate(uint32_t itemClass, uint32_t itemSubClass)
    {
        if (itemClass == VENDOR_SHIELD_ITEM_CLASS)
            return itemSubClass == VENDOR_SHIELD_ITEM_SUBCLASS;
        if (itemClass != VENDOR_WEAPON_ITEM_CLASS)
            return false;
        if (itemSubClass == VENDOR_WEAPON_SUBCLASS_MISC)
            return false;
        if (itemSubClass == VENDOR_WEAPON_SUBCLASS_THROWN)
            return false;
        if (itemSubClass == VENDOR_WEAPON_SUBCLASS_FISHING_POLE)
            return false;
        return true;
    }

    // True when the bot can pay `price` and still keep `spellReserve` (total
    // money needed for spells, which already includes the higher-priority
    // repair/ammo/ah/guild reserves but not travel/gear). Uses live money, so
    // callers must re-read bot money after each purchase.
    inline bool VendorWeaponUpgradeAffordable(uint32_t price, uint32_t money, uint32_t spellReserve)
    {
        if (money < spellReserve)
            return false;
        return money - spellReserve >= price;
    }
}
