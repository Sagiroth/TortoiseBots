#pragma once
#include <cstdint>
#include <string>

// Slot-aware spec weapon sets for warriors and paladins (owner weapon
// matrix; donor mod-playerbots `src/Mgr/Item/StatsWeightCalculator.cpp`
// CalculateItemTypePenalty + `src/Mgr/Item/RandomItemMgr.cpp`
// ShouldEquipWeaponForSpec @b6696bdb: protection wants 1H+shield, arms/ret
// 2H, fury 1H+1H with a 2H stand-in only before Dual Wield).
//
// Why this exists: RandomItemMgr::ShouldEquipWeaponForSpec answers "legal
// in ANY slot" - it checks the main-hand set first, so every 1H weapon
// reads as spec-legal for protection. The equip audit then equips bag 1H
// weapons over the shield (higher weight), the shield-transition puts the
// shield back, and the bot ping-pongs every audit cycle. Every function
// below is core-free so the standalone g++ policy test can include it;
// the literal constants mirror ItemPrototype.h / Player.h / SharedDefines.h.

// Classes (SharedDefines.h).
constexpr uint32_t SPEC_POLICY_CLASS_WARRIOR = 1;
constexpr uint32_t SPEC_POLICY_CLASS_PALADIN = 2;

// Equipment slots (Player.h).
constexpr uint32_t SPEC_POLICY_SLOT_MAINHAND = 15;
constexpr uint32_t SPEC_POLICY_SLOT_OFFHAND = 16;
constexpr uint32_t SPEC_POLICY_SLOT_RANGED = 17;

// Item classes (ItemPrototype.h).
constexpr uint32_t SPEC_POLICY_ITEM_WEAPON = 2;
constexpr uint32_t SPEC_POLICY_ITEM_ARMOR = 4;

// Weapon subclasses (ItemSubclassWeapon).
constexpr uint32_t SPEC_POLICY_WS_AXE = 0;
constexpr uint32_t SPEC_POLICY_WS_AXE2 = 1;
constexpr uint32_t SPEC_POLICY_WS_BOW = 2;
constexpr uint32_t SPEC_POLICY_WS_GUN = 3;
constexpr uint32_t SPEC_POLICY_WS_MACE = 4;
constexpr uint32_t SPEC_POLICY_WS_MACE2 = 5;
constexpr uint32_t SPEC_POLICY_WS_POLEARM = 6;
constexpr uint32_t SPEC_POLICY_WS_SWORD = 7;
constexpr uint32_t SPEC_POLICY_WS_SWORD2 = 8;
constexpr uint32_t SPEC_POLICY_WS_FIST = 13;
constexpr uint32_t SPEC_POLICY_WS_DAGGER = 15;
constexpr uint32_t SPEC_POLICY_WS_CROSSBOW = 18;

// Armor subclasses (ItemSubclassArmor).
constexpr uint32_t SPEC_POLICY_AS_MISC = 0;
constexpr uint32_t SPEC_POLICY_AS_SHIELD = 6;
constexpr uint32_t SPEC_POLICY_AS_LIBRAM = 7;

namespace ai
{
    // True when the spec's off hand is a shield (protection warrior/paladin,
    // holy paladin). Other classes never take this path.
    inline bool SpecUsesShieldOffHand(uint32_t playerClass, std::string const& specName)
    {
        if (playerClass == SPEC_POLICY_CLASS_WARRIOR)
            return specName == "prot";
        if (playerClass == SPEC_POLICY_CLASS_PALADIN)
            return specName == "prot" || specName == "holy";
        return false;
    }

    // True when the prototype may occupy `slot` for the spec. Covers only
    // warriors/paladins; every other class returns true (fail-open - the old
    // RandomItemMgr logic still owns those). `slot` is the concrete slot the
    // item is evaluated for, not "any slot it could fit": a 1H weapon is
    // main-hand-legal but off-hand-forbidden for shield specs, which is the
    // reverse guard the ping-pong fix needs.
    inline bool SpecWeaponAllowed(uint32_t playerClass, std::string const& specName,
        uint32_t itemClass, uint32_t itemSubClass, uint32_t slot, bool canDualWield)
    {
        if (playerClass == SPEC_POLICY_CLASS_WARRIOR)
        {
            if (specName == "prot")
            {
                if (slot == SPEC_POLICY_SLOT_MAINHAND)
                    return itemClass == SPEC_POLICY_ITEM_WEAPON &&
                        (itemSubClass == SPEC_POLICY_WS_SWORD || itemSubClass == SPEC_POLICY_WS_AXE ||
                         itemSubClass == SPEC_POLICY_WS_MACE || itemSubClass == SPEC_POLICY_WS_DAGGER ||
                         itemSubClass == SPEC_POLICY_WS_FIST);
                if (slot == SPEC_POLICY_SLOT_OFFHAND)
                    return itemClass == SPEC_POLICY_ITEM_ARMOR && itemSubClass == SPEC_POLICY_AS_SHIELD;
                if (slot == SPEC_POLICY_SLOT_RANGED)
                    return itemClass == SPEC_POLICY_ITEM_WEAPON &&
                        (itemSubClass == SPEC_POLICY_WS_BOW || itemSubClass == SPEC_POLICY_WS_CROSSBOW ||
                         itemSubClass == SPEC_POLICY_WS_GUN);
                return false;
            }
            if (specName == "arms")
            {
                if (slot == SPEC_POLICY_SLOT_MAINHAND)
                    return itemClass == SPEC_POLICY_ITEM_WEAPON &&
                        (itemSubClass == SPEC_POLICY_WS_SWORD2 || itemSubClass == SPEC_POLICY_WS_AXE2 ||
                         itemSubClass == SPEC_POLICY_WS_MACE2 || itemSubClass == SPEC_POLICY_WS_POLEARM);
                if (slot == SPEC_POLICY_SLOT_RANGED)
                    return itemClass == SPEC_POLICY_ITEM_WEAPON &&
                        (itemSubClass == SPEC_POLICY_WS_BOW || itemSubClass == SPEC_POLICY_WS_CROSSBOW ||
                         itemSubClass == SPEC_POLICY_WS_GUN);
                return false;
            }
            // Fury: dual-wield 1H pair once Dual Wield is learned; before that
            // the same 2H arms swings (stand-in, never downgraded on sight).
            if (slot == SPEC_POLICY_SLOT_MAINHAND)
            {
                if (itemClass != SPEC_POLICY_ITEM_WEAPON)
                    return false;
                if (itemSubClass == SPEC_POLICY_WS_SWORD || itemSubClass == SPEC_POLICY_WS_AXE ||
                    itemSubClass == SPEC_POLICY_WS_MACE || itemSubClass == SPEC_POLICY_WS_DAGGER ||
                    itemSubClass == SPEC_POLICY_WS_FIST)
                    return true;
                if (!canDualWield &&
                    (itemSubClass == SPEC_POLICY_WS_SWORD2 || itemSubClass == SPEC_POLICY_WS_AXE2 ||
                     itemSubClass == SPEC_POLICY_WS_MACE2 || itemSubClass == SPEC_POLICY_WS_POLEARM))
                    return true;
                return false;
            }
            if (slot == SPEC_POLICY_SLOT_OFFHAND)
                return itemClass == SPEC_POLICY_ITEM_WEAPON &&
                    (itemSubClass == SPEC_POLICY_WS_SWORD || itemSubClass == SPEC_POLICY_WS_AXE ||
                     itemSubClass == SPEC_POLICY_WS_MACE || itemSubClass == SPEC_POLICY_WS_DAGGER ||
                     itemSubClass == SPEC_POLICY_WS_FIST);
            if (slot == SPEC_POLICY_SLOT_RANGED)
                return itemClass == SPEC_POLICY_ITEM_WEAPON &&
                    (itemSubClass == SPEC_POLICY_WS_BOW || itemSubClass == SPEC_POLICY_WS_CROSSBOW ||
                     itemSubClass == SPEC_POLICY_WS_GUN);
            return false;
        }
        if (playerClass == SPEC_POLICY_CLASS_PALADIN)
        {
            if (specName == "prot" || specName == "holy")
            {
                if (slot == SPEC_POLICY_SLOT_MAINHAND)
                    return itemClass == SPEC_POLICY_ITEM_WEAPON &&
                        (itemSubClass == SPEC_POLICY_WS_SWORD || itemSubClass == SPEC_POLICY_WS_AXE ||
                         itemSubClass == SPEC_POLICY_WS_MACE);
                if (slot == SPEC_POLICY_SLOT_OFFHAND)
                {
                    if (itemClass == SPEC_POLICY_ITEM_ARMOR && itemSubClass == SPEC_POLICY_AS_SHIELD)
                        return true;
                    return specName == "holy" && itemClass == SPEC_POLICY_ITEM_ARMOR &&
                        itemSubClass == SPEC_POLICY_AS_MISC;
                }
                if (slot == SPEC_POLICY_SLOT_RANGED)
                    return itemClass == SPEC_POLICY_ITEM_ARMOR && itemSubClass == SPEC_POLICY_AS_LIBRAM;
                return false;
            }
            // Retribution: 2H only, never a shield.
            if (slot == SPEC_POLICY_SLOT_MAINHAND)
                return itemClass == SPEC_POLICY_ITEM_WEAPON &&
                    (itemSubClass == SPEC_POLICY_WS_SWORD2 || itemSubClass == SPEC_POLICY_WS_AXE2 ||
                     itemSubClass == SPEC_POLICY_WS_MACE2 || itemSubClass == SPEC_POLICY_WS_POLEARM);
            if (slot == SPEC_POLICY_SLOT_RANGED)
                return itemClass == SPEC_POLICY_ITEM_ARMOR && itemSubClass == SPEC_POLICY_AS_LIBRAM;
            return false;
        }
        return true;
    }

    // Vendor sort rank: a shieldless shield-bot buys the shield before any
    // other weapon upgrade (owner matrix). Lower ranks first; without a
    // shield need every item ties so the weight sort below is untouched.
    inline uint32_t VendorShieldRank(bool shieldNeed, uint32_t itemClass, uint32_t itemSubClass)
    {
        if (shieldNeed && itemClass == SPEC_POLICY_ITEM_ARMOR && itemSubClass == SPEC_POLICY_AS_SHIELD)
            return 0;
        return 1;
    }
}
