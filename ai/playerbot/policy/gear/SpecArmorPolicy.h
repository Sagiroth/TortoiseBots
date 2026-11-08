#pragma once
#include <cstdint>
#include <string_view>

// Pure policy for spec-based armor gates:
// - Non-body-armor material bypass: rings, necks, trinkets, cloaks, tabards,
//   and shirts have no material subclass restrictions (stat weight / upgrade
//   check decides).
// - Held-in-hand off-hands (INVTYPE_HOLDABLE): allowed for caster specs
//   (Priest, Mage, Warlock, Druid resto/balance, Shaman resto/elem, Paladin holy);
//   refused for melee classes/specs (Warrior, Hunter, Rogue, Druid feral,
//   Shaman enhance/tank, Paladin non-holy).
// - Shields: allowed for specs whose weapon policy uses a shield off-hand
//   (Protection Warrior, Protection/Holy Paladin, every Shaman spec).
// - Relics: libram (Paladin), idol (Druid), totem (Shaman).
// - Body armor material: spec-appropriate cloth/leather/mail/plate rules.
//
// Kept literal and core-free so standalone g++ policy tests can include it.

// Classes (SharedDefines.h).
constexpr uint32_t SPEC_ARMOR_CLASS_WARRIOR = 1;
constexpr uint32_t SPEC_ARMOR_CLASS_PALADIN = 2;
constexpr uint32_t SPEC_ARMOR_CLASS_HUNTER = 3;
constexpr uint32_t SPEC_ARMOR_CLASS_ROGUE = 4;
constexpr uint32_t SPEC_ARMOR_CLASS_PRIEST = 5;
constexpr uint32_t SPEC_ARMOR_CLASS_SHAMAN = 7;
constexpr uint32_t SPEC_ARMOR_CLASS_MAGE = 8;
constexpr uint32_t SPEC_ARMOR_CLASS_WARLOCK = 9;
constexpr uint32_t SPEC_ARMOR_CLASS_DRUID = 11;

// Inventory types (ItemPrototype.h).
constexpr uint32_t SPEC_ARMOR_INVTYPE_HEAD = 1;
constexpr uint32_t SPEC_ARMOR_INVTYPE_NECK = 2;
constexpr uint32_t SPEC_ARMOR_INVTYPE_SHOULDERS = 3;
constexpr uint32_t SPEC_ARMOR_INVTYPE_BODY = 4;
constexpr uint32_t SPEC_ARMOR_INVTYPE_CHEST = 5;
constexpr uint32_t SPEC_ARMOR_INVTYPE_WAIST = 6;
constexpr uint32_t SPEC_ARMOR_INVTYPE_LEGS = 7;
constexpr uint32_t SPEC_ARMOR_INVTYPE_FEET = 8;
constexpr uint32_t SPEC_ARMOR_INVTYPE_WRISTS = 9;
constexpr uint32_t SPEC_ARMOR_INVTYPE_HANDS = 10;
constexpr uint32_t SPEC_ARMOR_INVTYPE_FINGER = 11;
constexpr uint32_t SPEC_ARMOR_INVTYPE_TRINKET = 12;
constexpr uint32_t SPEC_ARMOR_INVTYPE_SHIELD = 14;
constexpr uint32_t SPEC_ARMOR_INVTYPE_CLOAK = 16;
constexpr uint32_t SPEC_ARMOR_INVTYPE_TABARD = 19;
constexpr uint32_t SPEC_ARMOR_INVTYPE_ROBE = 20;
constexpr uint32_t SPEC_ARMOR_INVTYPE_HOLDABLE = 23;
constexpr uint32_t SPEC_ARMOR_INVTYPE_RELIC = 28;

// Armor subclasses (ItemSubclassArmor).
constexpr uint32_t SPEC_ARMOR_AS_MISC = 0;
constexpr uint32_t SPEC_ARMOR_AS_CLOTH = 1;
constexpr uint32_t SPEC_ARMOR_AS_LEATHER = 2;
constexpr uint32_t SPEC_ARMOR_AS_MAIL = 3;
constexpr uint32_t SPEC_ARMOR_AS_PLATE = 4;
constexpr uint32_t SPEC_ARMOR_AS_SHIELD = 6;
constexpr uint32_t SPEC_ARMOR_AS_LIBRAM = 7;
constexpr uint32_t SPEC_ARMOR_AS_IDOL = 8;
constexpr uint32_t SPEC_ARMOR_AS_TOTEM = 9;

namespace ai
{
    // True when the inventory type is exempt from the material subclass gate:
    // necks, rings, trinkets, cloaks, tabards, and shirts.
    inline bool IsArmorExemptFromMaterialGate(uint32_t inventoryType)
    {
        switch (inventoryType)
        {
            case SPEC_ARMOR_INVTYPE_NECK:
            case SPEC_ARMOR_INVTYPE_FINGER:
            case SPEC_ARMOR_INVTYPE_TRINKET:
            case SPEC_ARMOR_INVTYPE_CLOAK:
            case SPEC_ARMOR_INVTYPE_TABARD:
            case SPEC_ARMOR_INVTYPE_BODY:
                return true;
            default:
                return false;
        }
    }

    // True when the spec allows held-in-hand off-hands (INVTYPE_HOLDABLE).
    // Allowed for caster specs: Priest, Mage, Warlock, Druid (resto/balance),
    // Shaman (resto/elem), Paladin (holy).
    // Refused for melee classes and specs: Warrior, Hunter, Rogue, Druid (feral),
    // Shaman (enhance, shamantank), Paladin (non-holy).
    inline bool SpecAllowsHoldable(uint32_t playerClass, std::string_view specName)
    {
        switch (playerClass)
        {
            case SPEC_ARMOR_CLASS_PRIEST:
            case SPEC_ARMOR_CLASS_MAGE:
            case SPEC_ARMOR_CLASS_WARLOCK:
                return true;
            case SPEC_ARMOR_CLASS_DRUID:
                return specName != "feraltank" && specName != "feraldps";
            case SPEC_ARMOR_CLASS_SHAMAN:
                return specName == "resto" || specName == "elem";
            case SPEC_ARMOR_CLASS_PALADIN:
                return specName == "holy";
            default:
                return false;
        }
    }

    // True when the spec allows shields in armor checks.
    // Allowed for specs whose weapon policy uses a shield off-hand:
    // Protection Warrior, Protection/Holy Paladin, every Shaman spec.
    inline bool SpecAllowsShield(uint32_t playerClass, std::string_view specName)
    {
        switch (playerClass)
        {
            case SPEC_ARMOR_CLASS_WARRIOR:
                return specName == "prot";
            case SPEC_ARMOR_CLASS_PALADIN:
                return specName == "prot" || specName == "holy";
            case SPEC_ARMOR_CLASS_SHAMAN:
                return specName == "shamantank" || specName == "resto" || specName == "elem" ||
                       specName == "enhance";
            default:
                return false;
        }
    }

    // True when the spec allows the given armor subclass for the item's slot.
    inline bool SpecArmorSubClassAllowed(uint32_t playerClass, std::string_view specName,
                                        uint32_t inventoryType, uint32_t subClass)
    {
        if (IsArmorExemptFromMaterialGate(inventoryType))
            return true;

        if (inventoryType == SPEC_ARMOR_INVTYPE_HOLDABLE)
            return SpecAllowsHoldable(playerClass, specName);

        if (subClass == SPEC_ARMOR_AS_SHIELD)
            return SpecAllowsShield(playerClass, specName);

        switch (playerClass)
        {
            case SPEC_ARMOR_CLASS_WARRIOR:
            {
                if (specName == "arms" || specName == "fury")
                    return subClass == SPEC_ARMOR_AS_LEATHER || subClass == SPEC_ARMOR_AS_MAIL ||
                           subClass == SPEC_ARMOR_AS_PLATE;
                return subClass == SPEC_ARMOR_AS_MAIL || subClass == SPEC_ARMOR_AS_PLATE;
            }
            case SPEC_ARMOR_CLASS_PALADIN:
            {
                if (specName == "holy")
                    return subClass == SPEC_ARMOR_AS_CLOTH || subClass == SPEC_ARMOR_AS_LEATHER ||
                           subClass == SPEC_ARMOR_AS_MAIL || subClass == SPEC_ARMOR_AS_PLATE ||
                           subClass == SPEC_ARMOR_AS_LIBRAM;
                return subClass == SPEC_ARMOR_AS_MAIL || subClass == SPEC_ARMOR_AS_PLATE ||
                       subClass == SPEC_ARMOR_AS_LIBRAM;
            }
            case SPEC_ARMOR_CLASS_HUNTER:
                return subClass == SPEC_ARMOR_AS_CLOTH || subClass == SPEC_ARMOR_AS_LEATHER ||
                       subClass == SPEC_ARMOR_AS_MAIL;
            case SPEC_ARMOR_CLASS_ROGUE:
                return subClass == SPEC_ARMOR_AS_CLOTH || subClass == SPEC_ARMOR_AS_LEATHER;
            case SPEC_ARMOR_CLASS_PRIEST:
                return subClass == SPEC_ARMOR_AS_CLOTH;
            case SPEC_ARMOR_CLASS_SHAMAN:
            {
                if (specName == "shamantank" || specName == "enhance")
                    return subClass == SPEC_ARMOR_AS_TOTEM || subClass == SPEC_ARMOR_AS_LEATHER ||
                           subClass == SPEC_ARMOR_AS_MAIL;
                return subClass == SPEC_ARMOR_AS_TOTEM || subClass == SPEC_ARMOR_AS_CLOTH ||
                       subClass == SPEC_ARMOR_AS_LEATHER || subClass == SPEC_ARMOR_AS_MAIL;
            }
            case SPEC_ARMOR_CLASS_MAGE:
            case SPEC_ARMOR_CLASS_WARLOCK:
                return subClass == SPEC_ARMOR_AS_CLOTH;
            case SPEC_ARMOR_CLASS_DRUID:
            {
                if (specName == "feraltank" || specName == "feraldps")
                    return subClass == SPEC_ARMOR_AS_IDOL || subClass == SPEC_ARMOR_AS_LEATHER;
                return subClass == SPEC_ARMOR_AS_IDOL || subClass == SPEC_ARMOR_AS_CLOTH ||
                       subClass == SPEC_ARMOR_AS_LEATHER;
            }
            default:
                return subClass == SPEC_ARMOR_AS_CLOTH;
        }
    }
}
