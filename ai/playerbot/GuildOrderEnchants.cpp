// pi-lens-ignore-file: all
#include "playerbot/GuildOrderEnchants.h"

#include "runtime/GuildOrderEnchantPolicy.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DBCStores.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace ai
{
namespace
{
std::vector<EnchantEntry>& EnchantIndex()
{
    static std::vector<EnchantEntry> index;
    return index;
}

bool& EnchantIndexBuilt()
{
    static bool built = false;
    return built;
}

std::string ToLower(std::string value)
{
    for (char& c : value)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

} // namespace

// One scan over the Enchanting skill line: rows whose spell is a permanent
// enchant (any effect SPELL_EFFECT_ENCHANT_ITEM, nothing created) join the
// catalogue; oils, wands and rods fall out. The lowest min_value over the
// rows teaching a spell is its required skill; reagents are read off the
// spell in slot order and the rods off its totem list.
void BuildGuildOrderEnchantIndex()
{
    if (EnchantIndexBuilt())
        return;
    EnchantIndexBuilt() = true;

    std::unordered_map<uint32_t, std::vector<SkillLineAbilityEntry const*>> bySpell;
    for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
    {
        SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(i);
        if (!ability || !ability->spellId || ability->skillId != 333) // SKILL_ENCHANTING
            continue;
        bySpell[ability->spellId].push_back(ability);
    }
    std::vector<EnchantEntry>& index = EnchantIndex();
    for (auto const& [spellId, rows] : bySpell)
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell)
            continue;
        bool hasEnchant = false;
        bool createsItem = false;
        for (uint32 e = 0; e < MAX_EFFECT_INDEX; ++e)
        {
            if (spell->Effect[e] == SPELL_EFFECT_ENCHANT_ITEM)
                hasEnchant = true;
            if (spell->Effect[e] == SPELL_EFFECT_CREATE_ITEM)
                createsItem = true;
        }
        uint32_t effect0 = spell->Effect[EFFECT_INDEX_0];
        bool isSkillStep = spell->Effect[EFFECT_INDEX_1] == SPELL_EFFECT_SKILL_STEP;
        bool rowOk = false;
        for (SkillLineAbilityEntry const* ability : rows)
        {
            if (IsCatalogueEnchantRow(ability->skillId, hasEnchant, createsItem, isSkillStep, effect0))
            {
                rowOk = true;
                break;
            }
        }
        if (!rowOk)
            continue;
        EnchantSlot slot = EnchantSlotForMasks(spell->EquippedItemClass,
            spell->EquippedItemSubClassMask, spell->EquippedItemInventoryTypeMask);
        if (slot == EnchantSlot::Unknown)
            continue;

        EnchantEntry entry;
        entry.spellId = spellId;
        entry.name = spell->SpellName[LOCALE_enUS];
        if (entry.name.empty())
            entry.name = spell->SpellName[0];
        entry.lowerName = ToLower(entry.name);
        entry.slot = slot;
        for (SkillLineAbilityEntry const* ability : rows)
            EnchantReqSkillFor(ability->skillId, ability->min_value, entry.reqSkill);
        for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
            if (spell->ReagentCount[i] && spell->Reagent[i] > 0)
                entry.reagents.emplace_back(uint32_t(spell->Reagent[i]), uint32_t(spell->ReagentCount[i]));
        for (uint32_t totem : spell->Totem)
            if (totem)
                entry.tools.push_back(totem);
        index.push_back(std::move(entry));
    }

    std::sort(index.begin(), index.end(), EnchantSortLess);
    sLog.outString("Loading guild order enchants: %u permanent enchants.", uint32_t(index.size()));
}

bool GuildOrderEnchantsReady()
{
    return EnchantIndexBuilt();
}

std::vector<EnchantEntry> GuildOrderEnchants()
{
    return EnchantIndex();
}

std::vector<GuildOrderEnchantMaterial> GuildOrderEnchantReagents(uint32_t spellId)
{
    for (EnchantEntry const& entry : EnchantIndex())
    {
        if (entry.spellId != spellId)
            continue;
        std::vector<GuildOrderEnchantMaterial> out;
        for (auto const& [itemId, count] : entry.reagents)
            out.push_back(GuildOrderEnchantMaterial{ itemId, count });
        return out;
    }
    return {};
}

std::vector<uint32_t> GuildOrderEnchantTools(uint32_t spellId)
{
    for (EnchantEntry const& entry : EnchantIndex())
        if (entry.spellId == spellId)
            return entry.tools;
    return {};
}

} // namespace ai
