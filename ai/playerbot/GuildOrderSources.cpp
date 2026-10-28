#include "playerbot/GuildOrderSources.h"

#include "playerbot/playerbot.h"
#include "playerbot/strategy/values/LootValues.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/TravelMgr.h"
#include "runtime/GuildOrderSourcesPolicy.h"

#include <algorithm>
#include <cmath>

using namespace TortoiseBots::GuildOrders;

namespace ai
{
namespace
{

struct EntrySource
{
    int32_t entry = 0;      // drop-map sign: creature +, game object -
    uint8_t flag = 0;       // SourceFlag
    float chance = 0.0f;    // best usable chance for the item
    uint32_t level = 0;     // mob max level, or node lock skill
    uint32_t skillId = 0;   // SKILL_NONE when no skill needed
    uint32_t reqSkill = 0;
};

float EqualChance(LootLootGroupAccess const& group)
{
    return group.EqualChanced.empty() ? 0.0f : 100.0f / float(group.EqualChanced.size());
}

// Best usable chance of the item in one loot table: ungrouped entries, one
// reference level (the world resolves a single indirection), then groups.
// Quest-only rows never count: a farm plan cannot promise them.
float LootChanceFor(LootTemplateAccess const* access, uint32_t itemId)
{
    if (!access)
        return 0.0f;

    float best = 0.0f;

    auto consider = [&](LootStoreItem const& row, float outerChance)
    {
        if (row.mincountOrRef < 0)
        {
            LootTemplate const* ref = LootTemplates_Reference.GetLootFor(uint32_t(-row.mincountOrRef));
            if (!ref)
                return;
            float const gate = row.chance >= 100.0f ? 1.0f : row.chance / 100.0f;
            LootTemplateAccess const* refAccess = reinterpret_cast<LootTemplateAccess const*>(ref);
            for (LootStoreItem const& refRow : refAccess->Entries)
            {
                if (refRow.itemid != itemId || refRow.needs_quest)
                    continue;
                best = std::max(best, refRow.chance * gate * outerChance);
            }
            for (LootLootGroupAccess const& group : refAccess->Groups)
            {
                for (LootStoreItem const& refRow : group.ExplicitlyChanced)
                {
                    if (refRow.itemid != itemId || refRow.needs_quest)
                        continue;
                    best = std::max(best, refRow.chance * gate * outerChance);
                }
                float const equal = EqualChance(group);
                for (LootStoreItem const& refRow : group.EqualChanced)
                {
                    if (refRow.itemid != itemId || refRow.needs_quest)
                        continue;
                    float const chance = refRow.chance > 0.0f ? refRow.chance : equal;
                    best = std::max(best, chance * gate * outerChance);
                }
            }
            return;
        }
        if (row.itemid != itemId || row.needs_quest)
            return;
        best = std::max(best, row.chance * outerChance);
    };

    for (LootStoreItem const& row : access->Entries)
        consider(row, 1.0f);
    for (LootLootGroupAccess const& group : access->Groups)
    {
        for (LootStoreItem const& row : group.ExplicitlyChanced)
            consider(row, 1.0f);
        float const equal = EqualChance(group);
        for (LootStoreItem const& row : group.EqualChanced)
        {
            if (row.mincountOrRef < 0)
            {
                consider(row, 1.0f);
                continue;
            }
            if (row.itemid != itemId || row.needs_quest)
                continue;
            float const chance = row.chance > 0.0f ? row.chance : equal;
            best = std::max(best, chance);
        }
    }

    return best;
}

uint32_t NodeLockSkill(GameObjectInfo const* goInfo, uint32_t& reqSkill)
{
    reqSkill = 0;
    if (!goInfo)
        return SKILL_NONE;
    LockEntry const* lockInfo = sLockStore.LookupEntry(goInfo->GetLockId());
    if (!lockInfo)
        return SKILL_NONE;
    for (int i = 0; i < 8; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL)
            continue;
        if (uint32_t skillId = SkillByLockType(LockType(lockInfo->Index[i])))
        {
            reqSkill = std::max(uint32_t(1), lockInfo->Skill[i]);
            return skillId;
        }
    }
    return SKILL_NONE;
}

uint32_t ZoneOf(AsyncGuidPosition const& point)
{
    AreaTableEntry const* area = AreaEntry::GetByAreaFlagAndMap(point.getAreaFlag(), point.getMapId());
    if (!area)
        return 0;
    return area->ZoneId ? area->ZoneId : area->Id;
}

struct ZoneAccum
{
    uint32_t spawns = 0;
    float chance = 0.0f;
    float nearest = 0.0f;
    bool hasNearest = false;
};

} // namespace

SourcePlan PlanGuildOrderSources(Player* bot, uint32_t itemId, OrderType type)
{
    SourcePlan plan;
    if (!bot || !itemId)
    {
        plan.verdict = OrderReason::NoSource;
        return plan;
    }

    if (type == OrderType::Buy)
    {
        plan.sources = SourceAh;
        return plan;
    }

    if (type == OrderType::Craft)
    {
        for (auto& [spellId, spellState] : bot->GetSpellMap())
        {
            if (spellState.state == PLAYERSPELL_REMOVED || spellState.disabled || IsPassiveSpell(spellId))
                continue;
            SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
            if (!spell)
                continue;
            for (int i = 0; i < 3; ++i)
            {
                if (spell->Effect[i] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[i] == itemId)
                {
                    plan.sources = SourceCraft;
                    plan.skillId = spell->EffectSkill[i] ? spell->EffectSkill[i] : spell->EffectSkill[0];
                    return plan;
                }
            }
        }
        plan.verdict = OrderReason::NoRecipe;
        return plan;
    }

    EntryGuidps const& guidps = GAI_VALUE_REF(EntryGuidps, "entry guidps");
    uint32_t const botLevel = bot->GetLevel();
    Team const team = bot->GetTeam();
    WorldPosition const botPos(bot);
    bool const lowGatherLevel = botLevel < kGatherTripMinLevel;

    OrderReason firstFail = OrderReason::None;
    bool sawSkillFail = false;
    bool sawLevelFail = false;

    // Gather levels come from the zone, but the spawn cache is keyed by
    // entry: collect entry sources first, then grade them per spawn.
    std::vector<EntrySource> entries;
    DropMap* dropMap = GAI_VALUE(DropMap*, "drop map");
    std::vector<uint32_t> creatureEntries;
    if (dropMap)
    {
        auto range = dropMap->equal_range(itemId);
        for (auto it = range.first; it != range.second; ++it)
        {
            int32_t const entry = it->second;
            if (entry > 0)
                creatureEntries.push_back(uint32_t(entry));
        }
        std::sort(creatureEntries.begin(), creatureEntries.end());
        creatureEntries.erase(std::unique(creatureEntries.begin(), creatureEntries.end()), creatureEntries.end());
    }

    // Mob drops and skins: the drop map only reads corpse loot, so skinning
    // tables are walked per creature entry here.
    for (uint32_t entry : creatureEntries)
    {
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);
        if (!cInfo)
            continue;
        uint32_t const mobLevel = cInfo->level_max;
        bool const levelFits = SourceLevelFits(mobLevel, botLevel);

        float dropChance = LootChanceFor(DropMapValue::GetLootTemplate(ObjectGuid(HIGHGUID_UNIT, entry, uint32_t(1)), LOOT_CORPSE), itemId);
        if (dropChance > 0.0f)
        {
            if (DropChanceFarmable(dropChance, false))
            {
                EntrySource source;
                source.entry = int32_t(entry);
                source.flag = SourceDrop;
                source.chance = dropChance;
                source.level = mobLevel;
                entries.push_back(source);
            }
            if (!levelFits)
                sawLevelFail = true;
        }

        LootTemplateAccess const* skin = DropMapValue::GetLootTemplate(ObjectGuid(HIGHGUID_UNIT, entry, uint32_t(1)), LOOT_SKINNING);
        float skinChance = LootChanceFor(skin, itemId);
        if (skinChance > 0.0f && DropChanceFarmable(skinChance, false))
        {
            uint32_t const reqSkill = SkinningSkillForMobLevel(mobLevel);
            EntrySource source;
            source.entry = int32_t(entry);
            source.flag = SourceSkin;
            source.chance = skinChance;
            source.level = mobLevel;
            source.skillId = SKILL_SKINNING;
            source.reqSkill = reqSkill;
            entries.push_back(source);
            if (bot->GetSkillValue(SKILL_SKINNING) < reqSkill)
                sawSkillFail = true;
            if (!levelFits)
                sawLevelFail = true;
        }
        else if (skinChance > 0.0f)
        {
            if (!levelFits)
                sawLevelFail = true;
        }
    }

    // Herb and ore nodes: the drop map already lists them (negative entries).
    // Game-object loot carries no level of its own, so every node entry is
    // collected with its lock skill and graded per spawn zone below.
    if (dropMap)
    {
        auto range = dropMap->equal_range(itemId);
        std::vector<uint32_t> nodeEntries;
        for (auto it = range.first; it != range.second; ++it)
            if (it->second < 0)
                nodeEntries.push_back(uint32_t(-it->second));
        std::sort(nodeEntries.begin(), nodeEntries.end());
        nodeEntries.erase(std::unique(nodeEntries.begin(), nodeEntries.end()), nodeEntries.end());

        for (uint32_t entry : nodeEntries)
        {
            GameObjectInfo const* goInfo = sObjectMgr.GetGameObjectInfo(entry);
            if (!goInfo)
                continue;
            uint32_t reqSkill = 0;
            uint32_t const skillId = NodeLockSkill(goInfo, reqSkill);
            bool const locked = skillId == SKILL_HERBALISM || skillId == SKILL_MINING;
            float chance = LootChanceFor(DropMapValue::GetLootTemplate(ObjectGuid(HIGHGUID_GAMEOBJECT, entry, uint32_t(1)), LOOT_CORPSE), itemId);
            if (chance <= 0.0f || !DropChanceFarmable(chance, false))
                continue;
            EntrySource source;
            source.entry = -int32_t(entry);
            source.flag = skillId == SKILL_HERBALISM ? SourceHerb : (skillId == SKILL_MINING ? SourceOre : SourceDrop);
            source.chance = chance;
            source.level = 0;
            source.skillId = locked ? skillId : SKILL_NONE;
            source.reqSkill = locked ? reqSkill : 0;
            if (locked && bot->GetSkillValue(skillId) < reqSkill)
                sawSkillFail = true;
            entries.push_back(source);
        }
    }

    std::map<uint32_t, ZoneAccum> zones;
    auto noteUsable = [&](EntrySource const& source, AsyncGuidPosition const& point, uint32_t sourceLevel)
    {
        if (point.isEnemyHomeZoneFor(team))
            return;
        if (!SourceLevelFits(sourceLevel, botLevel))
            return;
        if (source.skillId != SKILL_NONE && bot->GetSkillValue(source.skillId) < source.reqSkill)
            return;
        uint32_t const zoneId = ZoneOf(point);
        if (!zoneId)
            return;

        plan.sources |= source.flag;
        if (!plan.minLevel || sourceLevel < plan.minLevel)
            plan.minLevel = sourceLevel;
        if (source.skillId != SKILL_NONE && (plan.skillId == 0 || source.reqSkill > plan.skillValue))
        {
            plan.skillId = source.skillId;
            plan.skillValue = source.reqSkill;
        }

        ZoneAccum& accum = zones[zoneId];
        ++accum.spawns;
        accum.chance = std::max(accum.chance, source.chance);
        if (point.getMapId() == botPos.getMapId())
        {
            float const dist = botPos.sqDistance(point);
            if (!accum.hasNearest || dist < accum.nearest)
            {
                accum.nearest = dist;
                accum.hasNearest = true;
            }
        }
    };

    for (EntrySource const& source : entries)
    {
        auto spawns = guidps.find(source.entry);
        if (spawns == guidps.end())
            continue;
        bool const herbOrOre = source.flag == SourceHerb || source.flag == SourceOre;
        if (herbOrOre && lowGatherLevel)
        {
            firstFail = OrderReason::LowLevel;
            continue;
        }
        for (AsyncGuidPosition const& point : spawns->second)
        {
            uint32_t sourceLevel = source.level;
            if (herbOrOre)
            {
                AreaTableEntry const* area = AreaEntry::GetByAreaFlagAndMap(point.getAreaFlag(), point.getMapId());
                int32_t areaLevel = sTravelMgr.GetAreaLevel(area ? area->Id : 0);
                if (areaLevel <= 0)
                    continue;
                sourceLevel = uint32_t(areaLevel);
            }
            noteUsable(source, point, sourceLevel);
        }
    }

    if (!plan.sources)
    {
        if (firstFail != OrderReason::None)
            plan.verdict = firstFail;
        else if (sawSkillFail)
            plan.verdict = OrderReason::NoSkill;
        else if (sawLevelFail)
            plan.verdict = OrderReason::NoRange;
        else
            plan.verdict = OrderReason::NoSource;
        return plan;
    }

    for (auto& [zoneId, accum] : zones)
    {
        FarmZone zone;
        zone.zoneId = zoneId;
        zone.chance = uint8_t(std::min(float(100), std::floor(accum.chance)));
        zone.score = ZoneScore(accum.spawns, accum.chance);
        plan.zones.push_back(zone);
    }
    std::sort(plan.zones.begin(), plan.zones.end(), [&](FarmZone const& a, FarmZone const& b)
    {
        if (a.score != b.score)
            return a.score > b.score;
        ZoneAccum const& accA = zones[a.zoneId];
        ZoneAccum const& accB = zones[b.zoneId];
        if (accA.hasNearest != accB.hasNearest)
            return accA.hasNearest;
        if (accA.hasNearest && accB.hasNearest && accA.nearest != accB.nearest)
            return accA.nearest < accB.nearest;
        return a.zoneId < b.zoneId;
    });

    return plan;
}

} // namespace ai
