#pragma once

// Read-only catalogue of permanent item enchants (spell effect "enchant
// item permanent"). The core-data builder in ai/playerbot/GuildOrderEnchants.cpp
// loads it once at startup from the server's own spell and skill-line data;
// search and the later trade task only read it. Header-only and free of core
// types so the rules stay unit tested on their own
// (tools/test_guild_order_enchant_policy.cpp). All inputs are plain
// caller-resolved facts; core enum values are mirrored as documented
// literals so this header never includes a core header.
#include "GuildOrderContract.h"

#include <stdexcept>

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

// Required skill to learn the spell, mirroring the Professions missing-recipe
// line (commands/Professions.cpp): the lowest trainer requirement teaching
// it, and the recipe item's RequiredSkillRank; the lower wins when both
// exist, 0 when neither names one. The core-data builder in
// ai/playerbot/GuildOrderEnchants.cpp resolves fromTrainer/trainerSkill off
// the trainer tables and itemSkill off the recipe item's prototype, then
// calls this; the unit test covers the combine directly.
inline uint32_t EnchantReqSkillCombine(bool fromTrainer, uint32_t trainerSkill,
    bool hasRecipeItem, uint32_t itemSkill)
{
    if (fromTrainer && hasRecipeItem)
        return trainerSkill < itemSkill ? trainerSkill : itemSkill;
    if (fromTrainer)
        return trainerSkill;
    if (hasRecipeItem)
        return itemSkill;
    return 0;
}

// Per-bot enchant search match: catalogue name match plus slot-token and
// slot-alias words. Every query word must match either the name or the slot;
// a single-letter word never counts as a slot hit, so "a" does not match
// every weapon enchant through the "a" in "weapon".
inline bool EnchantSearchMatches(EnchantEntry const& entry, std::vector<std::string> const& words)
{
    std::string const slot = EnchantSlotToken(entry.slot);
    for (std::string const& word : words)
    {
        bool nameHit = entry.lowerName.find(word) != std::string::npos;
        bool slotHit = word.size() > 1 && (word == slot || EnchantSlotAlias(word) == slot);
        if (!nameHit && !slotHit)
            return false;
    }
    return true;
}

// Per-bot search verdict: every listed enchant is known (unknown ones never
// reach the list), so the only question is whether the bot's Enchanting
// skill covers the requirement. A zero requirement is always usable.
inline OrderReason EnchantSearchVerdict(bool known, uint32_t skillValue, uint32_t reqSkill)
{
    if (!known)
        return OrderReason::NoRecipe;
    if (reqSkill > 0 && skillValue < reqSkill)
        return OrderReason::NoSkill;
    return OrderReason::None;
}

// Wire verdict token for TBM:ORDER_ENCHANT: "ok" or the reason token.
inline char const* EnchantVerdictToken(OrderReason verdict)
{
    return verdict == OrderReason::None ? "ok" : ReasonToken(verdict);
}

// One TBM:ORDER_ENCHANT per-match row: spell, name, slot token, required
// skill, verdict and reagents (itemId:need:have per reagent, ";" between,
// "-" when the cast needs none). The name crosses already pipe-sanitized.
inline std::string FormatEnchantRow(uint32_t spellId, std::string const& name, EnchantSlot slot,
    uint32_t reqSkill, OrderReason verdict, std::string const& reagents)
{
    return "TBM:ORDER_ENCHANT|" + std::to_string(spellId) + "|" + name + "|" +
        EnchantSlotToken(slot) + "|" + std::to_string(reqSkill) + "|" +
        EnchantVerdictToken(verdict) + "|" + (reagents.empty() ? "-" : reagents);
}

inline std::string FormatEnchantsBegin(uint32_t count)
{
    return "TBM:ORDER_ENCHANTS_BEGIN|" + std::to_string(count);
}

inline std::string FormatEnchantsEnd()
{
    return "TBM:ORDER_ENCHANTS_END";
}

// One per-bot reagent token: itemId:need:have (have = bags + mail), ";"
// between reagents, "-" when the cast needs nothing.
struct EnchantSearchReagent
{
    uint32_t itemId = 0;
    uint32_t need = 0;
    uint32_t have = 0;
};

inline std::string FormatEnchantReagents(EnchantSearchReagent const* reagents, size_t count)
{
    std::string out;
    if (!reagents)
        return "-";
    for (size_t i = 0; i < count; ++i)
    {
        if (!reagents[i].itemId || !reagents[i].need)
            continue;
        if (!out.empty())
            out += ';';
        out += std::to_string(reagents[i].itemId) + ":" + std::to_string(reagents[i].need) +
            ":" + std::to_string(reagents[i].have);
    }
    return out.empty() ? "-" : out;
}

// Give-path spell argument: an enchant spell id or spell link ("Hspell:").
// At most 10 digits: longer input cannot fit uint32 and must fail, not throw.
inline bool ParseEnchantSpellToken(std::string const& token, uint32_t& spellId)
{
    spellId = 0;
    size_t link = token.find("Hspell:");
    if (link != std::string::npos)
    {
        size_t begin = link + 7;
        size_t end = begin;
        while (end < token.size() && token[end] >= '0' && token[end] <= '9')
            ++end;
        if (begin == end || end - begin > 10)
            return false;
        unsigned long parsed = std::stoul(token.substr(begin, end - begin));
        if (parsed == 0 || parsed > 0xFFFFFFFFul)
            return false;
        spellId = static_cast<uint32_t>(parsed);
        return true;
    }
    if (token.empty() || token.size() > 10)
        return false;
    for (char c : token)
        if (c < '0' || c > '9')
            return false;
    unsigned long parsed = std::stoul(token);
    if (parsed == 0 || parsed > 0xFFFFFFFFul)
        return false;
    spellId = static_cast<uint32_t>(parsed);
    return true;
}

// Give-path argument decisions: the reason the enchant order is refused, or
// None when the order may be stored. Only argument facts cross the boundary
// (the command resolves catalogue membership, recipe knowledge and skill);
// permission, busy, gold cap, hours and bring parsing stay in the command.
struct EnchantGiveArgs
{
    bool knownByBot = false;
    bool inCatalogue = false;
    bool skillEnough = false;
    bool keep = false;
};

inline OrderReason EnchantGiveVerdict(EnchantGiveArgs const& args)
{
    if (!args.knownByBot || !args.inCatalogue)
        return OrderReason::NoRecipe;
    if (!args.skillEnough)
        return OrderReason::NoSkill;
    if (args.keep)
        return OrderReason::Enchant;
    return OrderReason::None;
}

// Plain refusal lines for `.bot order <bot> enchant` give, one per reason.
inline char const* EnchantGiveRefusal(OrderReason verdict)
{
    switch (verdict)
    {
        case OrderReason::NoSkill:  return "The bot's Enchanting skill is too low for that enchant.";
        case OrderReason::Enchant:  return "Keep orders are not available for enchants.";
        default:                    return "The bot does not know that enchant.";
    }
}

// Give-path hours word: "inf" or a whole 1-24 count, sharing the farm/craft
// range. Zero stores as no time limit, like every other order type.
inline bool IsEnchantHoursToken(std::string const& lower)
{
    if (lower.empty() || lower.size() > 2)
        return false;
    for (char c : lower)
        if (c < '0' || c > '9')
            return false;
    return true;
}

inline bool ParseEnchantHours(std::string const& token, uint32_t& hours)
{
    hours = 0;
    std::string lower = token;
    for (char& c : lower)
        c = char(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    if (lower == "inf")
        return true;
    unsigned long parsed = 0;
    try
    {
        size_t pos = 0;
        parsed = std::stoul(token, &pos);
        if (pos != token.size())
            return false;
    }
    catch (...)
    {
        return false;
    }
    if (parsed < 1 || parsed > 24)
        return false;
    hours = static_cast<uint32_t>(parsed);
    return true;
}

// Give-path gold cap: copper 0 to 4294967295, like every other order type.
inline bool ParseEnchantGoldCap(std::string const& token, uint32_t& goldCap)
{
    goldCap = 0;
    if (token.empty() || token.size() > 10)
        return false;
    for (char c : token)
        if (c < '0' || c > '9')
            return false;
    unsigned long parsed = std::stoul(token);
    if (parsed > 0xFFFFFFFFul)
        return false;
    goldCap = static_cast<uint32_t>(parsed);
    return true;
}

} // namespace GuildOrders
} // namespace TortoiseBots
