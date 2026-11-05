#pragma once

// Read-only catalogue of permanent item enchants (spell effect "enchant
// item permanent"). The core-data builder in ai/playerbot/GuildOrderEnchants.cpp
// loads it once at startup from the server's own spell and skill-line data;
// search and the later trade task only read it. Header-only and free of core
// types so the rules stay unit tested on their own
// (tools/test_guild_order_enchant_policy.cpp). All inputs are plain
// caller-resolved facts; core enum values are mirrored as documented
// literals so this header never includes a core header.

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// Slot token for the TBM:ORDER_WHO and TBM:ORDER_ENCHANT lines. Deriving only
// the eight slots enchants really land on keeps the guild-wide search to one
// short axis; head/shoulder/leg slots have no permanent enchants.
enum class EnchantSlot : uint8_t
{
    Unknown = 0,
    Bracer,
    Chest,
    Boots,
    Cloak,
    Gloves,
    Shield,
    Weapon,
    TwoHandWeapon,
};

inline char const* EnchantSlotToken(EnchantSlot slot)
{
    switch (slot)
    {
        case EnchantSlot::Bracer:       return "bracer";
        case EnchantSlot::Chest:        return "chest";
        case EnchantSlot::Boots:        return "boots";
        case EnchantSlot::Cloak:        return "cloak";
        case EnchantSlot::Gloves:       return "gloves";
        case EnchantSlot::Shield:       return "shield";
        case EnchantSlot::Weapon:        return "weapon";
        case EnchantSlot::TwoHandWeapon: return "2hweapon";
        case EnchantSlot::Unknown:      break;
    }
    return "-";
}

// The slot an enchant lands on, from the spell's own equipped-item masks
// (literals mirror SharedDefines.h: ITEM_CLASS_WEAPON = 2, subclass bit 6 =
// shield, INVTYPE bits WRISTS = 9, FEET = 8, CLOAK = 16, HANDS = 10,
// SHIELD = 14, CHEST = 5, ROBE = 20). Weapon enchants carry no inventory
// mask, so the subclass decides between one-hand and two-hand; shield
// enchants carry either the subclass bit or the inventory bit; chest
// enchants name both chest and robe.
inline EnchantSlot EnchantSlotForMasks(int32_t itemClass, int32_t subMask, int32_t invMask)
{
    if (itemClass == 2) // ITEM_CLASS_WEAPON: 1378 = two-hand subclasses only
        return subMask == 1378 ? EnchantSlot::TwoHandWeapon : EnchantSlot::Weapon;
    if (subMask == 64 || (invMask & (1 << 14))) // shield, either axis
        return EnchantSlot::Shield;
    if (invMask & ((1 << 5) | (1 << 20))) // INVTYPE_CHEST, INVTYPE_ROBE
        return EnchantSlot::Chest;
    if (invMask & (1 << 9)) // INVTYPE_WRISTS
        return EnchantSlot::Bracer;
    if (invMask & (1 << 8)) // INVTYPE_FEET
        return EnchantSlot::Boots;
    if (invMask & (1 << 16)) // INVTYPE_CLOAK
        return EnchantSlot::Cloak;
    if (invMask & (1 << 10)) // INVTYPE_HANDS
        return EnchantSlot::Gloves;
    return EnchantSlot::Unknown;
}

inline bool ParseEnchantSlot(std::string const& token, EnchantSlot& out)
{
    if (token == "bracer")   { out = EnchantSlot::Bracer; return true; }
    if (token == "chest")    { out = EnchantSlot::Chest; return true; }
    if (token == "boots")    { out = EnchantSlot::Boots; return true; }
    if (token == "cloak")    { out = EnchantSlot::Cloak; return true; }
    if (token == "gloves")   { out = EnchantSlot::Gloves; return true; }
    if (token == "shield")   { out = EnchantSlot::Shield; return true; }
    if (token == "weapon")   { out = EnchantSlot::Weapon; return true; }
    if (token == "2hweapon") { out = EnchantSlot::TwoHandWeapon; return true; }
    return false;
}
// Search-text aliases for the slot ("wrist->bracer", "feet->boots",
// "back->cloak", "hands->gloves", "2h->2hweapon"). Exact words only: the
// caller lowercases and splits on spaces first, so "boots" never matches a
// name word like "bootstraps".
inline std::string EnchantSlotAlias(std::string const& word)
{
    if (word == "wrist") return "bracer";
    if (word == "feet")  return "boots";
    if (word == "back")  return "cloak";
    if (word == "hands") return "gloves";
    if (word == "2h")    return "2hweapon";
    return word;
}
// One permanent enchant: the spell, where it lands, what skill it needs and
// what one cast consumes. reagents parallels the spell's own reagent list
// (itemId per non-empty reagent slot, kept in spell order); tools lists the
// recipe tools the bot always carries (runed rods), one item id each.
struct EnchantEntry
{
    uint32_t spellId = 0;
    std::string name;
    std::string lowerName;
    EnchantSlot slot = EnchantSlot::Unknown;
    uint32_t reqSkill = 0;
    std::vector<std::pair<uint32_t, uint32_t>> reagents;
    std::vector<uint32_t> tools;
};

// Sort for both searches: required skill ascending, then name. Craft keeps
// today's order; enchants sort.
inline bool EnchantSortLess(EnchantEntry const& a, EnchantEntry const& b)
{
    if (a.reqSkill != b.reqSkill)
        return a.reqSkill < b.reqSkill;
    return a.name < b.name;
}

// Item classes and inventory types below mirror SharedDefines.h values
// (ITEM_CLASS_WEAPON = 2, ITEM_CLASS_ARMOR = 4; INVTYPE_* in declaration
// order starting at NON_EQUIP = 0). The catalogue stores the resolved slot,
// so this check only needs the raw spell masks and the item's three facts.
struct EnchantTargetFacts
{
    int32_t equippedItemClass = -1;
    int32_t subClassMask = 0;
    int32_t invTypeMask = 0;
    // spell->Targets bit for "item in trade window can be enchanted".
    bool itemTarget = false;
    // Exact-spell exception for Enchant Cloak - Minor Agility (13419): the
    // spell data carries the weapon class, but the core exempts cloaks.
    uint32_t spellId = 0;
};
struct EnchantTargetItem
{
    uint32_t itemClass = 0;
    uint32_t subClass = 0;
    uint32_t inventoryType = 0;
};
// Whether the spell's equipped-item masks admit the item, mirroring the
// core's own fit check (Item::IsFitToSpellRequirements): -1 class means any
// class with no subclass check, a zero mask means any of that axis, and the
// inventory-type mask only applies to item-targeted spells. 2H-only enchants
// (Fiery Weapon on a 2H sword) carry the 2H bit in the mask, so the same
// check that admits the slot also gates the weapon kind.
inline bool EnchantFitsItem(EnchantTargetFacts const& spell, EnchantTargetItem const& item)
{
    if (spell.spellId == 13419 && item.inventoryType == 16) // INVTYPE_CLOAK
        return true;
    if (spell.equippedItemClass != -1)
    {
        if (spell.equippedItemClass != int32_t(item.itemClass) && spell.spellId != 13419)
            return false;
        if (spell.subClassMask != 0 && (spell.subClassMask & (1 << item.subClass)) == 0)
            return false;
    }
    if (spell.invTypeMask != 0 && spell.itemTarget)
    {
        if (item.inventoryType >= 32 || (spell.invTypeMask & (1 << item.inventoryType)) == 0)
            return false;
    }
    return true;
}

// Whether a skill-line row is an Enchanting ability that can contribute its
// min_value to the catalogue: skill 333 (SKILL_ENCHANTING), and the spell
// really is a permanent enchant (caller resolves hasEnchantEffect off the
// SpellEntry: any effect slot with Effect == 53 and no Effect == 24, which
inline bool IsCatalogueEnchantRow(uint32_t skillId, bool hasPermanentEnchantEffect,
    bool createsItem, bool isSkillStep, uint32_t effect0)
{
    // 333 = SKILL_ENCHANTING; 44 = SPELL_EFFECT_SKILL_STEP,
    // 36 = SPELL_EFFECT_LEARN_SPELL (mirrors ProfessionSnapshotPolicy).
    if (skillId != 333)
        return false;
    if (isSkillStep || effect0 == 36 || effect0 == 44)
        return false;
    return hasPermanentEnchantEffect && !createsItem;
}

// Required skill for one catalogue row: the ability's min_value (the yellow
// threshold, the skill the formula is aimed at). Vanilla enchant rows carry
// req_skill_value 1 (trainers gate by rank instead); sorting or gating on it
// would put every vanilla enchant at 1, so the catalogue uses min_value.
// Take the lowest min over the rows teaching the spell.
inline bool EnchantReqSkillFor(uint32_t rowSkillId, uint32_t rowMinValue, uint32_t& best)
{
    if (rowSkillId != 333)
        return false;
    if (best == 0 || rowMinValue < best)
        best = rowMinValue;
    return true;
}

} // namespace GuildOrders
} // namespace TortoiseBots
