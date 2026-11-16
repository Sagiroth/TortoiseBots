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
#include "World.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

void RecordDistinctPhase(std::vector<uint32_t>& phases, uint32_t phase)
{
    if (std::find(phases.begin(), phases.end(), phase) == phases.end())
        phases.push_back(phase);
}

uint32_t LookupPhaseOrZero(std::unordered_map<uint32_t, uint32_t> const& map, uint32_t key)
{
    auto it = map.find(key);
    return (it != map.end()) ? it->second : 0;
}

void UpdateMinPhase(std::unordered_map<uint32_t, uint32_t>& map, uint32_t key, uint32_t phase)
{
    auto [it, inserted] = map.emplace(key, phase);
    if (!inserted && phase < it->second)
        it->second = phase;
}

struct CraftSpellInfo
{
    uint32_t spellId = 0;
    bool knownByTrainerOrDefault = false;
    std::vector<uint32_t> createdItems;
    std::vector<uint32_t> reagents;
};

void CollectObtainableAndPhases(
    std::unordered_set<uint32_t>& obtainableItems,
    std::unordered_set<uint32_t>& recipeProducedItems,
    std::unordered_set<uint32_t>& unlimitedVendorItems,
    std::unordered_map<uint32_t, uint32_t>& itemPhases)
{
    obtainableItems.clear();
    recipeProducedItems.clear();
    unlimitedVendorItems.clear();
    itemPhases.clear();

    // 1. Map phases from areatrigger_teleport.
    std::unordered_map<uint32_t, uint32_t> mapPhases;
    if (auto result = WorldDatabase.Query("SELECT target_map, required_phase FROM areatrigger_teleport"))
    {
        do
        {
            Field const* fields = result->Fetch();
            UpdateMinPhase(mapPhases, fields[0].GetUInt32(), fields[1].GetUInt32());
        } while (result->NextRow());
    }

    // 2. Creature spawn map phases -> loot/skinning/pickpocket loot_id phases.
    // Aggregated per loot_id across spawned templates so unspawned clone templates sharing
    // a raid loot_id do not drag raid drops down to phase 0.
    std::unordered_map<uint32_t, uint32_t> creatureMinPhase;
    if (auto result = WorldDatabase.Query("SELECT id, id2, id3, id4, map FROM creature"))
    {
        do
        {
            Field const* fields = result->Fetch();
            uint32_t const ph = LookupPhaseOrZero(mapPhases, fields[4].GetUInt32());
            for (int i = 0; i < 4; ++i)
            {
                uint32_t const cid = fields[i].GetUInt32();
                if (cid > 0)
                    UpdateMinPhase(creatureMinPhase, cid, ph);
            }
        } while (result->NextRow());
    }

    std::unordered_map<uint32_t, uint32_t> creatureLootPhase;
    std::unordered_map<uint32_t, uint32_t> pickpocketLootPhase;
    std::unordered_map<uint32_t, uint32_t> skinningLootPhase;
    if (auto result = WorldDatabase.Query(
            "SELECT entry, loot_id, pickpocket_loot_id, skinning_loot_id "
            "FROM creature_template WHERE loot_id > 0 OR pickpocket_loot_id > 0 OR skinning_loot_id > 0"))
    {
        do
        {
            Field const* fields = result->Fetch();
            uint32_t const entry = fields[0].GetUInt32();
            auto spawnIt = creatureMinPhase.find(entry);
            if (spawnIt == creatureMinPhase.end())
                continue;
            uint32_t const ph = spawnIt->second;
            if (uint32_t const lid = fields[1].GetUInt32(); lid > 0)
                UpdateMinPhase(creatureLootPhase, lid, ph);
            if (uint32_t const plid = fields[2].GetUInt32(); plid > 0)
                UpdateMinPhase(pickpocketLootPhase, plid, ph);
            if (uint32_t const slid = fields[3].GetUInt32(); slid > 0)
                UpdateMinPhase(skinningLootPhase, slid, ph);
        } while (result->NextRow());
    }

    // 3. GameObject spawn map phases -> gameobject_template data1 loot_id phases.
    std::unordered_map<uint32_t, uint32_t> goMinPhase;
    if (auto result = WorldDatabase.Query("SELECT id, map FROM gameobject"))
    {
        do
        {
            Field const* fields = result->Fetch();
            uint32_t const gid = fields[0].GetUInt32();
            if (gid > 0)
                UpdateMinPhase(goMinPhase, gid, LookupPhaseOrZero(mapPhases, fields[1].GetUInt32()));
        } while (result->NextRow());
    }

    std::unordered_map<uint32_t, uint32_t> goLootPhase;
    // Only GAMEOBJECT_TYPE_CHEST (3) and GAMEOBJECT_TYPE_FISHINGHOLE (25) use data1 as a loot_id.
    if (auto result = WorldDatabase.Query(
            "SELECT entry, data1 FROM gameobject_template WHERE type IN (3, 25) AND data1 > 0"))
    {
        do
        {
            Field const* fields = result->Fetch();
            uint32_t const entry = fields[0].GetUInt32();
            auto spawnIt = goMinPhase.find(entry);
            if (spawnIt == goMinPhase.end())
                continue;
            UpdateMinPhase(goLootPhase, fields[1].GetUInt32(), spawnIt->second);
        } while (result->NextRow());
    }

    // 4. Direct loot tables and reference_loot_template (1-level inheritance).
    std::unordered_map<uint32_t, std::vector<uint32_t>> itemSourcePhases;
    std::unordered_map<uint32_t, std::vector<uint32_t>> refSourcePhases;

    auto scanEntryLootTable = [&](char const* sql, std::unordered_map<uint32_t, uint32_t> const& entryPhaseMap)
    {
        if (auto result = WorldDatabase.Query(sql))
        {
            do
            {
                Field const* fields = result->Fetch();
                uint32_t const entry = fields[0].GetUInt32();
                uint32_t const item = fields[1].GetUInt32();
                int32_t const mincountOrRef = fields[2].GetInt32();
                uint32_t const ph = LookupPhaseOrZero(entryPhaseMap, entry);
                if (mincountOrRef > 0 && item > 0)
                {
                    obtainableItems.insert(item);
                    RecordDistinctPhase(itemSourcePhases[item], ph);
                }
                else if (mincountOrRef < 0)
                {
                    uint32_t const refId = static_cast<uint32_t>(-mincountOrRef);
                    RecordDistinctPhase(refSourcePhases[refId], ph);
                }
            } while (result->NextRow());
        }
    };

    auto scanPhaseZeroLootTable = [&](char const* sql)
    {
        if (auto result = WorldDatabase.Query(sql))
        {
            do
            {
                Field const* fields = result->Fetch();
                uint32_t const item = fields[0].GetUInt32();
                int32_t const mincountOrRef = fields[1].GetInt32();
                if (mincountOrRef > 0 && item > 0)
                {
                    obtainableItems.insert(item);
                    RecordDistinctPhase(itemSourcePhases[item], 0);
                }
                else if (mincountOrRef < 0)
                {
                    uint32_t const refId = static_cast<uint32_t>(-mincountOrRef);
                    RecordDistinctPhase(refSourcePhases[refId], 0);
                }
            } while (result->NextRow());
        }
    };

    scanEntryLootTable("SELECT entry, item, mincountOrRef FROM creature_loot_template", creatureLootPhase);
    scanEntryLootTable("SELECT entry, item, mincountOrRef FROM gameobject_loot_template", goLootPhase);
    scanEntryLootTable("SELECT entry, item, mincountOrRef FROM skinning_loot_template", skinningLootPhase);
    scanEntryLootTable("SELECT entry, item, mincountOrRef FROM pickpocketing_loot_template", pickpocketLootPhase);
    scanPhaseZeroLootTable("SELECT item, mincountOrRef FROM fishing_loot_template");
    scanPhaseZeroLootTable("SELECT item, mincountOrRef FROM disenchant_loot_template");
    scanPhaseZeroLootTable("SELECT item, mincountOrRef FROM item_loot_template");
    scanPhaseZeroLootTable("SELECT item, mincountOrRef FROM mail_loot_template");

    std::unordered_map<uint32_t, uint32_t> refPhases;
    refPhases.reserve(refSourcePhases.size());
    for (auto const& pair : refSourcePhases)
        refPhases[pair.first] = ai::AhBotCombineSourcePhases(pair.second);

    if (auto result = WorldDatabase.Query(
            "SELECT entry, item FROM reference_loot_template WHERE mincountOrRef > 0"))
    {
        do
        {
            Field const* fields = result->Fetch();
            uint32_t const entry = fields[0].GetUInt32();
            uint32_t const item = fields[1].GetUInt32();
            if (item > 0)
            {
                obtainableItems.insert(item);
                RecordDistinctPhase(itemSourcePhases[item], LookupPhaseOrZero(refPhases, entry));
            }
        } while (result->NextRow());
    }

    // 5. Vendors: unlimited stock (maxcount = 0) excluded later; limited stock (and vendor reagents/recipes) are phase 0.
    if (auto result = WorldDatabase.Query(
            "SELECT item, maxcount FROM npc_vendor "
            "UNION SELECT item, maxcount FROM npc_vendor_template"))
    {
        do
        {
            Field const* fields = result->Fetch();
            uint32_t const itemId = fields[0].GetUInt32();
            uint32_t const maxCount = fields[1].GetUInt32();
            if (itemId == 0)
                continue;
            if (maxCount == 0)
                unlimitedVendorItems.insert(itemId);
            RecordDistinctPhase(itemSourcePhases[itemId], 0);
        } while (result->NextRow());
    }

    // 6. Quest reward and choice reward items (event-gated quests inherit game_event.required_phase).
    std::unordered_map<uint32_t, uint32_t> eventPhases;
    if (auto result = WorldDatabase.Query("SELECT entry, required_phase FROM game_event WHERE required_phase > 0"))
    {
        do
        {
            Field const* fields = result->Fetch();
            eventPhases[fields[0].GetUInt32()] = fields[1].GetUInt32();
        } while (result->NextRow());
    }

    std::unordered_map<uint32_t, std::vector<uint32_t>> questSourcePhases;
    if (auto result = WorldDatabase.Query("SELECT quest, event FROM game_event_quest"))
    {
        do
        {
            Field const* fields = result->Fetch();
            uint32_t const questId = fields[0].GetUInt32();
            uint32_t const eventId = fields[1].GetUInt32();
            RecordDistinctPhase(questSourcePhases[questId], LookupPhaseOrZero(eventPhases, eventId));
        } while (result->NextRow());
    }

    std::unordered_map<uint32_t, uint32_t> questPhases;
    questPhases.reserve(questSourcePhases.size());
    for (auto const& pair : questSourcePhases)
        questPhases[pair.first] = ai::AhBotCombineSourcePhases(pair.second);

    for (auto const& pair : sObjectMgr.GetQuestTemplates())
    {
        Quest const* quest = pair.second.get();
        if (!quest)
            continue;
        uint32_t const qPhase = LookupPhaseOrZero(questPhases, pair.first);
        for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
        {
            if (quest->RewItemId[i] > 0)
            {
                obtainableItems.insert(quest->RewItemId[i]);
                RecordDistinctPhase(itemSourcePhases[quest->RewItemId[i]], qPhase);
            }
        }
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            if (quest->RewChoiceItemId[i] > 0)
            {
                obtainableItems.insert(quest->RewChoiceItemId[i]);
                RecordDistinctPhase(itemSourcePhases[quest->RewChoiceItemId[i]], qPhase);
            }
        }
    }

    std::unordered_map<uint32_t, uint32_t> nonCraftPhases;
    nonCraftPhases.reserve(itemSourcePhases.size());
    for (auto const& pair : itemSourcePhases)
        nonCraftPhases[pair.first] = ai::AhBotCombineSourcePhases(pair.second);

    // 7. Trainer spells, default-learned spells, and recipe items teaching craft spells.
    std::unordered_set<uint32_t> trainerSpells;
    if (auto result = WorldDatabase.Query(
            "SELECT DISTINCT spell FROM npc_trainer "
            "UNION SELECT DISTINCT spell FROM npc_trainer_template"))
    {
        do
        {
            uint32_t const spellId = result->Fetch()[0].GetUInt32();
            if (spellId > 0)
                trainerSpells.insert(spellId);
        } while (result->NextRow());
    }

    std::unordered_map<uint32_t, std::vector<uint32_t>> recipeTeaches;
    ItemPrototypeMap const& protoMap = sObjectMgr.GetItemPrototypeMap();
    for (auto const& entry : protoMap)
    {
        ItemPrototype const& proto = entry.second;
        if (proto.ItemId == 0)
            continue;
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            uint32 const spellId = proto.Spells[i].SpellId;
            if (!spellId)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            if (!spell)
                continue;
            for (uint8 eff = 0; eff < MAX_EFFECT_INDEX; ++eff)
            {
                if (spell->Effect[eff] == SPELL_EFFECT_LEARN_SPELL && spell->EffectTriggerSpell[eff] > 0)
                    recipeTeaches[spell->EffectTriggerSpell[eff]].push_back(proto.ItemId);
            }
        }
    }

    // 8. Craft spells (SPELL_EFFECT_CREATE_ITEM) and fixpoint propagation over reagents/recipes.
    std::vector<CraftSpellInfo> craftSpells;
    std::unordered_set<uint32_t> craftedItemIds;
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

        bool isProf = false;
        bool knownByDefault = false;
        SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
        for (SkillLineAbilityMap::const_iterator it = bounds.first; it != bounds.second; ++it)
        {
            if (!it->second)
                continue;
            if (IsProfessionSkillLine(it->second->skillId))
                isProf = true;
            if (it->second->learnOnGetSkill != 0)
                knownByDefault = true;
        }
        bool const isCraftAttr = (spell->Attributes & (SPELL_ATTR_TRADESPELL | SPELL_ATTR_NOT_SHAPESHIFT)) != 0;
        if (!isProf && !isCraftAttr)
            continue;

        CraftSpellInfo info;
        info.spellId = spellId;
        info.knownByTrainerOrDefault = knownByDefault || (trainerSpells.find(spellId) != trainerSpells.end());
        for (uint8 eff = 0; eff < MAX_EFFECT_INDEX; ++eff)
        {
            if (spell->Effect[eff] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[eff] > 0)
            {
                uint32 const itemId = spell->EffectItemType[eff];
                obtainableItems.insert(itemId);
                if (isProf)
                    recipeProducedItems.insert(itemId);
                info.createdItems.push_back(itemId);
                craftedItemIds.insert(itemId);
            }
        }
        for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
        {
            if (spell->Reagent[r] > 0)
                info.reagents.push_back(static_cast<uint32_t>(spell->Reagent[r]));
        }
        if (!info.createdItems.empty())
            craftSpells.push_back(std::move(info));
    }

    std::unordered_map<uint32_t, uint32_t> prevPhases = nonCraftPhases;
    constexpr int kMaxCraftPasses = 5;
    for (int pass = 0; pass < kMaxCraftPasses; ++pass)
    {
        std::unordered_map<uint32_t, std::vector<uint32_t>> passSources;
        for (auto const& pair : nonCraftPhases)
            RecordDistinctPhase(passSources[pair.first], pair.second);

        bool const finalPass = (pass == kMaxCraftPasses - 1);
        for (CraftSpellInfo const& cs : craftSpells)
        {
            uint32_t learnPhase = 0;
            bool waiting = false;
            if (!cs.knownByTrainerOrDefault)
            {
                auto recIt = recipeTeaches.find(cs.spellId);
                if (recIt != recipeTeaches.end() && !recIt->second.empty())
                {
                    std::vector<uint32_t> recPhases;
                    recPhases.reserve(recIt->second.size());
                    for (uint32_t recipeId : recIt->second)
                    {
                        auto phIt = prevPhases.find(recipeId);
                        if (phIt != prevPhases.end())
                            recPhases.push_back(phIt->second);
                        else if (!finalPass && craftedItemIds.find(recipeId) != craftedItemIds.end())
                        {
                            waiting = true;
                            break;
                        }
                        else
                            recPhases.push_back(0);
                    }
                    if (!waiting)
                        learnPhase = ai::AhBotCombineSourcePhases(recPhases);
                }
            }
            if (waiting)
                continue;

            std::vector<uint32_t> reagentPhases;
            reagentPhases.reserve(cs.reagents.size());
            for (uint32_t reagentId : cs.reagents)
            {
                auto phIt = prevPhases.find(reagentId);
                if (phIt != prevPhases.end())
                    reagentPhases.push_back(phIt->second);
                else if (!finalPass && craftedItemIds.find(reagentId) != craftedItemIds.end())
                {
                    waiting = true;
                    break;
                }
                else
                    reagentPhases.push_back(0);
            }
            if (waiting)
                continue;

            uint32_t const craftPh = ai::AhBotCombineCraftedPhase(learnPhase, reagentPhases);
            for (uint32_t outItem : cs.createdItems)
                RecordDistinctPhase(passSources[outItem], craftPh);
        }

        std::unordered_map<uint32_t, uint32_t> nextPhases;
        nextPhases.reserve(passSources.size());
        for (auto const& pair : passSources)
            nextPhases[pair.first] = ai::AhBotCombineSourcePhases(pair.second);

        if (nextPhases == prevPhases)
        {
            prevPhases = std::move(nextPhases);
            break;
        }
        prevPhases = std::move(nextPhases);
    }

    itemPhases = std::move(prevPhases);
}

ai::AhBotItemFilterConfig BuildFilterConfigFromPlayerbotConfig(uint32_t contentPhase)
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

    cfg.respectContentPhase = sPlayerbotAIConfig.ahBotRespectContentPhase;
    cfg.contentPhase = contentPhase;

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
    auto const startTime = std::chrono::steady_clock::now();

    std::unordered_set<uint32_t> obtainableItems;
    std::unordered_set<uint32_t> recipeProducedItems;
    std::unordered_set<uint32_t> unlimitedVendorItems;
    std::unordered_map<uint32_t, uint32_t> itemPhases;
    CollectObtainableAndPhases(obtainableItems, recipeProducedItems, unlimitedVendorItems, itemPhases);

    uint32_t const contentPhase = sWorld.GetContentPhase();
    ai::AhBotItemFilterConfig const filterCfg = BuildFilterConfigFromPlayerbotConfig(contentPhase);
    ai::AhBotItemFilterConfig unphasedCfg = filterCfg;
    unphasedCfg.respectContentPhase = false;

    std::unordered_map<uint32_t, uint32_t> multipliedItemIds;
    ai::AhBotParseItemValuePairs(multipliedItemIds, sPlayerbotAIConfig.ahBotListMultipliedItemIds);

    ai::AhBotItemPoolTable nextTable;
    size_t lockedByPhase = 0;

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

        auto phaseIt = itemPhases.find(proto.ItemId);
        facts.contentPhase = (phaseIt != itemPhases.end()) ? phaseIt->second : 0;

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
        else if (filterCfg.respectContentPhase && ai::AhBotIsItemCandidate(facts, unphasedCfg))
            ++lockedByPhase;
    }

    nextTable.SetPhaseStats(contentPhase + 1, lockedByPhase);
    nextTable.BuildProportions(sPlayerbotAIConfig.ahBotListProportions, multipliedItemIds);

    size_t const candidateCount = nextTable.CandidateCount();
    uint64_t const totalWeight = nextTable.TotalProportionWeight();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_table = std::move(nextTable);
        m_loaded = true;
    }

    auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();

    if (totalWeight == 0)
        sLog.outError("TortoiseBots: AhBot item pool loaded %zu candidates (%zu locked by phase %u) in %lld ms, but total list proportion weight is 0",
            candidateCount, lockedByPhase, contentPhase + 1, static_cast<long long>(elapsedMs));
    else
        TB_LOG_BASIC("TortoiseBots: AhBot item pool loaded %zu candidates (%zu locked by phase %u, proportion weight %llu) in %lld ms",
            candidateCount, lockedByPhase, contentPhase + 1, static_cast<unsigned long long>(totalWeight),
            static_cast<long long>(elapsedMs));
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
