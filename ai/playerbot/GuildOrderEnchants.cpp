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

using namespace TortoiseBots::GuildOrders;

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

// Spell teaching a recipe item teaches: first lowest-entry item whose use
// spell learns that spell. Trained-from-trainer rows have no item and stay 0.
// Shared with the Professions missing-recipe line (commands/Professions.cpp).
uint32_t GuildOrderRecipeItemFor(uint32_t spellId)
{
    uint32_t found = 0;
    for (uint32 id = 0; id < sItemStorage.GetMaxEntry(); ++id)
    {
        ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(id);
        if (!proto || proto->Class != ITEM_CLASS_RECIPE)
            continue;
        for (uint32 e = 0; e < MAX_ITEM_PROTO_SPELLS; ++e)
        {
            if (!proto->Spells[e].SpellId)
                continue;
            SpellEntry const* use = sSpellMgr.GetSpellEntry(proto->Spells[e].SpellId);
            if (!use)
                continue;
            for (uint32 f = 0; f < MAX_EFFECT_INDEX; ++f)
            {
                if (use->Effect[f] != SPELL_EFFECT_LEARN_SPELL || !use->EffectTriggerSpell[f])
                    continue;
                if (use->EffectTriggerSpell[f] == spellId && (!found || id < found))
                    found = id;
            }
        }
    }
    return found;
}

// Lowest required skill among this trainer's rows that teach the spell.
bool GuildOrderTaughtByTrainer(uint32_t spellId, uint32_t trainerType,
    TrainerSpellData const* spells, uint32_t& reqSkill)
{
    if (!spells || trainerType != TRAINER_TYPE_TRADESKILLS)
        return false;
    bool found = false;
    for (auto const& entry : spells->spellList)
    {
        TrainerSpell const* tSpell = &entry.second;
        if (!tSpell || !tSpell->spell)
            continue;
        bool teaches = tSpell->spell == spellId;
        if (SpellEntry const* teaching = sSpellMgr.GetSpellEntry(tSpell->spell))
            for (uint32 e = 0; e < MAX_EFFECT_INDEX && !teaches; ++e)
                teaches = teaching->Effect[e] == SPELL_EFFECT_LEARN_SPELL &&
                    teaching->EffectTriggerSpell[e] == spellId;
        if (!teaches)
            continue;
        if (!found || tSpell->reqSkillValue < reqSkill)
            reqSkill = tSpell->reqSkillValue;
        found = true;
    }
    return found;
}

// Any trade-skill trainer anywhere teaching this spell (the bot visits the
// trainer to learn it; reachability is the travel layer's job, not this
// listing's). Cached per run like the factory's trainer sweeps.
bool GuildOrderTaughtByAnyTrainer(uint32_t spellId, uint32_t& reqSkill)
{
    struct TrainerCache
    {
        uint32_t trainerType = 0;
        TrainerSpellData const* spells = nullptr;
    };
    static std::vector<TrainerCache> tradeskillTrainers;
    static bool cached = false;
    if (!cached)
    {
        for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
        {
            CreatureInfo const* co = sCreatureStorage.LookupEntry<CreatureInfo>(id);
            if (!co || co->trainer_type != TRAINER_TYPE_TRADESKILLS)
                continue;
            uint32 trainerId = co->trainer_id ? co->trainer_id : co->entry;
            TrainerSpellData const* spells = sObjectMgr.GetNpcTrainerTemplateSpells(trainerId);
            if (!spells)
                spells = sObjectMgr.GetNpcTrainerSpells(trainerId);
            if (!spells)
                continue;
            bool known = false;
            for (TrainerCache const& cachedTrainer : tradeskillTrainers)
                if (cachedTrainer.spells == spells)
                    known = true;
            if (!known)
                tradeskillTrainers.push_back({ co->trainer_type, spells });
        }
        cached = true;
    }
    bool found = false;
    for (TrainerCache const& entry : tradeskillTrainers)
    {
        uint32_t need = 0;
        if (!GuildOrderTaughtByTrainer(spellId, entry.trainerType, entry.spells, need))
            continue;
        if (!found || need < reqSkill)
            reqSkill = need;
        found = true;
    }
    return found;
}

// Required skill to learn the spell: the same rule as the Professions
// missing-recipe line — the lowest trainer requirement teaching it, and the
// recipe item's RequiredSkillRank; the lower wins when both exist.
uint32_t GuildOrderEnchantReqSkill(uint32_t spellId, uint32_t recipeItem)
{
    uint32_t trainerSkill = 0;
    bool const fromTrainer = GuildOrderTaughtByAnyTrainer(spellId, trainerSkill);
    ItemPrototype const* recipeProto = recipeItem ? sObjectMgr.GetItemPrototype(recipeItem) : nullptr;
    uint32_t const itemSkill = recipeProto ? recipeProto->RequiredSkillRank : 0;
    if (fromTrainer && recipeProto)
        return std::min(trainerSkill, itemSkill);
    if (fromTrainer)
        return trainerSkill;
    if (recipeProto)
        return itemSkill;
    return 0;
}

// One scan over the Enchanting skill line: rows whose spell is a permanent
// enchant (any effect SPELL_EFFECT_ENCHANT_ITEM, nothing created) join the
// catalogue; oils, wands and rods fall out. The required skill comes from
// the same sources as the Professions missing-recipe line (lowest trainer
// requirement, recipe item's RequiredSkillRank); reagents are read off the
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
        entry.reqSkill = GuildOrderEnchantReqSkill(spellId, GuildOrderRecipeItemFor(spellId));
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
