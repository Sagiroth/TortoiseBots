/*
 * Copyright (C) 2008-2010 Trinity <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2023+ Nathan Handley <https://github.com/NathanHandley>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Ported from mod-ah-bot-plus AuctionHouseBot.cpp@f6858329.
 */

// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "AhBotItemPool.h"

#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/RandomItemMgr.h"
#include "../host/ModuleLog.h"

#include "Database/DatabaseEnv.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "Spells/SpellDefines.h"
#include "Spells/SpellMgr.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>

namespace TortoiseBots
{
namespace
{

bool IsProfessionSkillLine(uint32 skillId)
{
    switch (skillId)
    {
        case 40:  // Poisons
        case 129: // First Aid
        case 142: // Survival
        case 164: // Blacksmithing
        case 165: // Leatherworking
        case 171: // Alchemy
        case 182: // Herbalism
        case 185: // Cooking
        case 186: // Mining
        case 197: // Tailoring
        case 202: // Engineering
        case 333: // Enchanting
        case 356: // Fishing
        case 393: // Skinning
        case 755: // Jewelcrafting
            return true;
        default:
            return false;
    }
}

bool SpellHasProfessionSkill(uint32 spellId)
{
    SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
    for (SkillLineAbilityMap::const_iterator it = bounds.first; it != bounds.second; ++it)
    {
        if (it->second && IsProfessionSkillLine(it->second->skillId))
            return true;
    }
    return false;
}

ItemPrototype const* FindFirstCreatedItem(SpellEntry const* spell)
{
    if (!spell)
        return nullptr;
    for (uint8 eff = 0; eff < MAX_EFFECT_INDEX; ++eff)
    {
        if (spell->Effect[eff] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[eff] > 0)
        {
            if (ItemPrototype const* produced = sObjectMgr.GetItemPrototype(spell->EffectItemType[eff]))
                return produced;
        }
    }
    return nullptr;
}

// Vanilla recipe items either carry the craft spell directly or carry a learn
// spell (SPELL_EFFECT_LEARN_SPELL) whose EffectTriggerSpell points to it.
ItemPrototype const* GetProducedItemFromRecipe(ItemPrototype const* recipeProto)
{
    if (!recipeProto)
        return nullptr;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        uint32 const spellId = recipeProto->Spells[i].SpellId;
        if (!spellId)
            continue;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell)
            continue;
        if (ItemPrototype const* direct = FindFirstCreatedItem(spell))
            return direct;
        for (uint8 eff = 0; eff < MAX_EFFECT_INDEX; ++eff)
        {
            if (spell->Effect[eff] == SPELL_EFFECT_LEARN_SPELL && spell->EffectTriggerSpell[eff] > 0)
            {
                if (SpellEntry const* craft = sSpellMgr.GetSpellEntry(spell->EffectTriggerSpell[eff]))
                {
                    if (ItemPrototype const* produced = FindFirstCreatedItem(craft))
                        return produced;
                }
            }
        }
    }
    return nullptr;
}

void CollectObtainableAndRecipeItems(
    std::unordered_set<uint32_t>& obtainableItems,
    std::unordered_set<uint32_t>& recipeProducedItems,
    std::unordered_set<uint32_t>& unlimitedVendorItems)
{
    obtainableItems.clear();
    recipeProducedItems.clear();
    unlimitedVendorItems.clear();

    // 1. Any *_loot_template (including reference_loot_template), excluding
    // negative mincountOrRef rows where `item` is a reference template ID.
    if (auto result = WorldDatabase.Query(
            "SELECT item FROM creature_loot_template WHERE mincountOrRef > 0 "
            "UNION SELECT item FROM gameobject_loot_template WHERE mincountOrRef > 0 "
            "UNION SELECT item FROM item_loot_template WHERE mincountOrRef > 0 "
            "UNION SELECT item FROM fishing_loot_template WHERE mincountOrRef > 0 "
            "UNION SELECT item FROM skinning_loot_template WHERE mincountOrRef > 0 "
            "UNION SELECT item FROM pickpocketing_loot_template WHERE mincountOrRef > 0 "
            "UNION SELECT item FROM disenchant_loot_template WHERE mincountOrRef > 0 "
            "UNION SELECT item FROM mail_loot_template WHERE mincountOrRef > 0 "
            "UNION SELECT item FROM reference_loot_template WHERE mincountOrRef > 0"))
    {
        do
        {
            uint32 const itemId = result->Fetch()[0].GetUInt32();
            if (itemId > 0)
                obtainableItems.insert(itemId);
        } while (result->NextRow());
    }

    // 2. Items crafted by SPELL_EFFECT_CREATE_ITEM spells.
    uint32 const maxSpellId = sSpellMgr.GetMaxSpellId();
    for (uint32 spellId = 1; spellId < maxSpellId; ++spellId)
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell)
            continue;

        bool const hasCreateEffect =
            spell->Effect[0] == SPELL_EFFECT_CREATE_ITEM ||
            spell->Effect[1] == SPELL_EFFECT_CREATE_ITEM ||
            spell->Effect[2] == SPELL_EFFECT_CREATE_ITEM;
        if (!hasCreateEffect)
            continue;

        bool const isProf = SpellHasProfessionSkill(spellId);
        bool const isCraftAttr = (spell->Attributes & (SPELL_ATTR_TRADESPELL | SPELL_ATTR_NOT_SHAPESHIFT)) != 0;
        if (!isProf && !isCraftAttr)
            continue;

        for (uint8 eff = 0; eff < MAX_EFFECT_INDEX; ++eff)
        {
            if (spell->Effect[eff] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[eff] > 0)
            {
                uint32 const itemId = spell->EffectItemType[eff];
                obtainableItems.insert(itemId);
                if (isProf)
                    recipeProducedItems.insert(itemId);
            }
        }
    }

    // 3. Quest reward and choice reward items.
    for (auto const& pair : sObjectMgr.GetQuestTemplates())
    {
        Quest const* quest = pair.second.get();
        if (!quest)
            continue;
        for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
        {
            if (quest->RewItemId[i] > 0)
                obtainableItems.insert(quest->RewItemId[i]);
        }
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            if (quest->RewChoiceItemId[i] > 0)
                obtainableItems.insert(quest->RewChoiceItemId[i]);
        }
    }

    // 4. Items sold with unlimited stock (maxcount = 0) by any vendor.
    if (auto result = WorldDatabase.Query(
            "SELECT DISTINCT item FROM npc_vendor WHERE maxcount = 0 "
            "UNION SELECT DISTINCT item FROM npc_vendor_template WHERE maxcount = 0"))
    {
        do
        {
            uint32 const itemId = result->Fetch()[0].GetUInt32();
            if (itemId > 0)
                unlimitedVendorItems.insert(itemId);
        } while (result->NextRow());
    }
}

ai::AhBotItemFilterConfig BuildFilterConfigFromPlayerbotConfig()
{
    ai::AhBotItemFilterConfig cfg;

    cfg.itemLevelRestrictEnabled = sPlayerbotAIConfig.ahBotListedItemLevelRestrictEnabled;
    cfg.useCraftedItemForItemLevel = sPlayerbotAIConfig.ahBotListedItemLevelRestrictUseCrafted;
    cfg.minItemLevel = sPlayerbotAIConfig.ahBotListedItemLevelMin;
    cfg.maxItemLevel = sPlayerbotAIConfig.ahBotListedItemLevelMax;
    ai::AhBotParseNumberListToSet(cfg.itemLevelExceptions, sPlayerbotAIConfig.ahBotListedItemLevelExceptionItemIds);

    cfg.itemIdRestrictEnabled = sPlayerbotAIConfig.ahBotListedItemIdRestrictEnabled;
    cfg.minItemId = sPlayerbotAIConfig.ahBotListedItemIdMin;
    cfg.maxItemId = sPlayerbotAIConfig.ahBotListedItemIdMax;
    ai::AhBotParseNumberListToSet(cfg.itemIdExceptions, sPlayerbotAIConfig.ahBotListedItemIdExceptionItemIds);

    cfg.equipLevelRestrictEnabled = sPlayerbotAIConfig.ahBotEquipLevelRestrictEnabled;
    cfg.minRequiredLevel = sPlayerbotAIConfig.ahBotEquipLevelMin;
    cfg.maxRequiredLevel = sPlayerbotAIConfig.ahBotEquipLevelMax;
    ai::AhBotParseNumberListToSet(cfg.equipLevelExceptions, sPlayerbotAIConfig.ahBotEquipLevelExceptionItemIds);

    cfg.disabledItemTextFilter = sPlayerbotAIConfig.ahBotDisabledItemTextFilter;
    cfg.disabledRecipeProducedFilterEnabled = sPlayerbotAIConfig.ahBotDisabledRecipeProducedItemFilterEnabled;
    ai::AhBotParseClassSubClassMap(
        cfg.disabledRecipeProducedClassSubClasses,
        sPlayerbotAIConfig.ahBotDisabledRecipeProducedItemClassSubClasses);
    ai::AhBotParseNumberListToSet(cfg.disabledItems, sPlayerbotAIConfig.ahBotDisabledInvalidItemIds);
    ai::AhBotParseNumberListToSet(cfg.disabledItems, sPlayerbotAIConfig.ahBotDisabledCustomItemIds);

    return cfg;
}

} // namespace

AhBotItemPool& AhBotItemPool::Instance()
{
    static AhBotItemPool instance;
    return instance;
}

void AhBotItemPool::Load()
{
    std::unordered_set<uint32_t> obtainableItems;
    std::unordered_set<uint32_t> recipeProducedItems;
    std::unordered_set<uint32_t> unlimitedVendorItems;
    CollectObtainableAndRecipeItems(obtainableItems, recipeProducedItems, unlimitedVendorItems);

    ai::AhBotItemFilterConfig const filterCfg = BuildFilterConfigFromPlayerbotConfig();

    std::unordered_map<uint32_t, uint32_t> multipliedItemIds;
    ai::AhBotParseItemValuePairs(multipliedItemIds, sPlayerbotAIConfig.ahBotListMultipliedItemIds);

    ai::AhBotItemPoolTable nextTable;

    ItemPrototypeMap const& protoMap = sObjectMgr.GetItemPrototypeMap();
    for (auto const& entry : protoMap)
    {
        ItemPrototype const& proto = entry.second;
        if (proto.ItemId == 0)
            continue;

        ai::AhBotItemFacts facts;
        facts.itemId = proto.ItemId;
        facts.itemClass = proto.Class;
        facts.itemSubClass = proto.SubClass;
        facts.name = proto.Name1;
        facts.quality = proto.Quality;
        facts.flags = proto.Flags;
        facts.extraFlags = proto.ExtraFlags;
        facts.buyPrice = proto.BuyPrice;
        facts.sellPrice = proto.SellPrice;
        facts.itemLevel = proto.ItemLevel;
        facts.requiredLevel = proto.RequiredLevel;
        facts.containerSlots = proto.ContainerSlots;
        facts.bonding = proto.Bonding;
        facts.duration = proto.Duration;
        facts.minMoneyLoot = proto.MinMoneyLoot;
        facts.isTransmogClone = sRandomItemMgr.IsTransmogClone(&proto);
        facts.isObtainable = obtainableItems.find(proto.ItemId) != obtainableItems.end();
        facts.isSoldByUnlimitedVendor = unlimitedVendorItems.find(proto.ItemId) != unlimitedVendorItems.end();
        facts.isProducedByRecipe = recipeProducedItems.find(proto.ItemId) != recipeProducedItems.end();

        if (filterCfg.itemLevelRestrictEnabled &&
            filterCfg.useCraftedItemForItemLevel &&
            proto.Class == ITEM_CLASS_RECIPE)
        {
            if (ItemPrototype const* produced = GetProducedItemFromRecipe(&proto))
            {
                facts.hasProducedItem = true;
                facts.producedItemLevel = produced->ItemLevel;
            }
        }

        if (ai::AhBotIsItemCandidate(facts, filterCfg))
            nextTable.AddCandidate(proto.ItemId, proto.Class, proto.Quality);
    }

    nextTable.BuildProportions(sPlayerbotAIConfig.ahBotListProportions, multipliedItemIds);

    size_t const candidateCount = nextTable.CandidateCount();
    uint64_t const totalWeight = nextTable.TotalProportionWeight();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_table = std::move(nextTable);
        m_loaded = true;
    }

    if (totalWeight == 0)
        sLog.outError("TortoiseBots: AhBot item pool loaded %zu candidates but total list proportion weight is 0", candidateCount);
    else
        TB_LOG_BASIC("TortoiseBots: AhBot item pool loaded %zu candidates (proportion weight %llu)",
            candidateCount, static_cast<unsigned long long>(totalWeight));
}

bool AhBotItemPool::IsLoaded() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_loaded;
}

uint32_t AhBotItemPool::PickItemForListing() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_loaded)
        return 0;
    return m_table.Pick([](uint64_t limit) -> uint64_t {
        if (limit <= 1)
            return 0;
        uint32_t const bounded = static_cast<uint32_t>(std::min<uint64_t>(limit - 1, UINT32_MAX));
        return static_cast<uint64_t>(urand(0, bounded));
    });
}

bool AhBotItemPool::IsCandidate(uint32_t itemId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_loaded)
        return false;
    return m_table.IsCandidate(itemId);
}

size_t AhBotItemPool::CandidateCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_loaded)
        return 0;
    return m_table.CandidateCount();
}

std::string AhBotItemPool::GetStatus() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_loaded)
        return "not loaded";
    return m_table.GetStatus();
}

} // namespace TortoiseBots
