#include "playerbot/GuildOrderSources.h"

#include "playerbot/playerbot.h"
#include "playerbot/strategy/values/LootValues.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/TravelMgr.h"
#include "runtime/GuildOrderSourcesPolicy.h"
#include "runtime/GuildOrderStore.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>

using namespace TortoiseBots::GuildOrders;

namespace ai
{
namespace
{

float EqualChance(LootLootGroupAccess const& group)
{
    return EqualShareChance(uint32_t(group.EqualChanced.size()));
}

// Every (item, chance) pair one loot table can yield: ungrouped entries, one
// reference level (the world resolves a single indirection), then groups.
// Quest-only rows never count: a farm plan cannot promise them.
void CollectLootChances(LootTemplateAccess const* access, std::vector<std::pair<uint32_t, float>>& out)
{
    if (!access)
        return;

    auto consider = [&](LootStoreItem const& row, float outerChance, float equalShare)
    {
        if (row.mincountOrRef < 0)
        {
            LootTemplate const* ref = LootTemplates_Reference.GetLootFor(uint32_t(-row.mincountOrRef));
            if (!ref)
                return;
            float const gate = ReferenceGate(row.chance, equalShare);
            if (gate <= 0.0f)
                return;
            LootTemplateAccess const* refAccess = reinterpret_cast<LootTemplateAccess const*>(ref);
            for (LootStoreItem const& refRow : refAccess->Entries)
            {
                if (refRow.itemid == 0 || refRow.needs_quest || refRow.mincountOrRef < 0)
                    continue;
                out.emplace_back(refRow.itemid, refRow.chance * gate * outerChance);
            }
            for (LootLootGroupAccess const& group : refAccess->Groups)
            {
                for (LootStoreItem const& refRow : group.ExplicitlyChanced)
                {
                    if (refRow.itemid == 0 || refRow.needs_quest || refRow.mincountOrRef < 0)
                        continue;
                    out.emplace_back(refRow.itemid, refRow.chance * gate * outerChance);
                }
                float const refEqual = EqualChance(group);
                for (LootStoreItem const& refRow : group.EqualChanced)
                {
                    if (refRow.itemid == 0 || refRow.needs_quest || refRow.mincountOrRef < 0)
                        continue;
                    out.emplace_back(refRow.itemid, MemberChance(refRow.chance, refEqual) * gate * outerChance);
                }
            }
            return;
        }
        if (row.itemid == 0 || row.needs_quest)
            return;
        out.emplace_back(row.itemid, MemberChance(row.chance, equalShare) * outerChance);
    };

    for (LootStoreItem const& row : access->Entries)
        consider(row, 1.0f, 0.0f);
    for (LootLootGroupAccess const& group : access->Groups)
    {
        for (LootStoreItem const& row : group.ExplicitlyChanced)
            consider(row, 1.0f, 0.0f);
        float const equal = EqualChance(group);
        for (LootStoreItem const& row : group.EqualChanced)
            consider(row, 1.0f, equal);
    }
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

struct OrderSourceIndex
{
    std::unordered_map<uint32_t, std::vector<GuildOrderEntrySource>> byItem;
    std::unordered_map<int32_t, std::vector<GuildOrderZoneSlice>> zonesByEntry;
};

OrderSourceIndex& Index()
{
    static OrderSourceIndex index;
    return index;
}

bool& IndexBuilt()
{
    static bool built = false;
    return built;
}

void AddEntrySource(OrderSourceIndex& index, uint32_t itemId, GuildOrderEntrySource source)
{
    if (!DropChanceFarmable(source.chance, false))
        return;
    auto& list = index.byItem[itemId];
    for (GuildOrderEntrySource& known : list)
    {
        if (known.entry != source.entry || known.flag != source.flag)
            continue;
        known.chance = std::max(known.chance, source.chance);
        return;
    }
    list.push_back(source);
}

} // namespace

// Built once at startup, after the travel data is ready: "entry guidps"
// may not exist yet when quest travel is off, so force it here.
void BuildGuildOrderSourceIndex()
{
    if (IndexBuilt())
        return;
    OrderSourceIndex& index = Index();
    std::vector<std::pair<uint32_t, float>> chances;
    for (uint32_t entry = 0; entry < sCreatureStorage.GetMaxEntry(); ++entry)
    {
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);
        if (!cInfo)
            continue;
        ObjectGuid guid(HIGHGUID_UNIT, entry, uint32_t(1));
        chances.clear();
        CollectLootChances(DropMapValue::GetLootTemplate(guid, LOOT_CORPSE), chances);
        for (auto& [itemId, chance] : chances)
        {
            GuildOrderEntrySource source;
            source.entry = int32_t(entry);
            source.flag = SourceDrop;
            source.chance = chance;
            source.level = cInfo->level_max;
            AddEntrySource(index, itemId, source);
        }
        chances.clear();
        CollectLootChances(DropMapValue::GetLootTemplate(guid, LOOT_SKINNING), chances);
        for (auto& [itemId, chance] : chances)
        {
            GuildOrderEntrySource source;
            source.entry = int32_t(entry);
            source.flag = SourceSkin;
            source.chance = chance;
            source.level = cInfo->level_max;
            source.skillId = SKILL_SKINNING;
            source.reqSkill = SkinningSkillForMobLevel(cInfo->level_max);
            AddEntrySource(index, itemId, source);
        }
    }

    for (uint32_t entry = 0; entry < sGOStorage.GetMaxEntry(); ++entry)
    {
        GameObjectInfo const* goInfo = sObjectMgr.GetGameObjectInfo(entry);
        if (!goInfo)
            continue;
        uint32_t reqSkill = 0;
        uint32_t const skillId = NodeLockSkill(goInfo, reqSkill);
        bool const locked = skillId == SKILL_HERBALISM || skillId == SKILL_MINING;
        chances.clear();
        CollectLootChances(DropMapValue::GetLootTemplate(
            ObjectGuid(HIGHGUID_GAMEOBJECT, entry, uint32_t(1)), LOOT_CORPSE), chances);
        for (auto& [itemId, chance] : chances)
        {
            GuildOrderEntrySource source;
            source.entry = -int32_t(entry);
            source.flag = skillId == SKILL_HERBALISM ? SourceHerb :
                (skillId == SKILL_MINING ? SourceOre : SourceDrop);
            source.chance = chance;
            source.skillId = locked ? skillId : SKILL_NONE;
            source.reqSkill = locked ? reqSkill : 0;
            AddEntrySource(index, itemId, source);
        }
    }
    // Zone slices only for entries that drop something orderable: walk the
    // cached spawn positions once, grouped by zone, with a sample position
    // per zone for the nearer-wins-ties rule.
    std::unordered_set<int32_t> wanted;
    for (auto& [itemId, sources] : index.byItem)
        for (GuildOrderEntrySource const& source : sources)
            wanted.insert(source.entry);

    EntryGuidps const& guidps = GAI_VALUE_REF(EntryGuidps, "entry guidps");
    for (auto& [entry, points] : guidps)
    {
        if (!wanted.count(entry))
            continue;
        std::map<uint32_t, GuildOrderZoneSlice> slices;
        for (AsyncGuidPosition const& point : points)
        {
            uint32_t areaId = 0;
            bool allyHome = false, hordeHome = false;
            uint32_t const zoneId = ZoneOf(point.getAreaFlag(), point.getMapId(), areaId, allyHome, hordeHome);
            if (!zoneId)
                continue;
            GuildOrderZoneSlice& slice = slices[zoneId];
            slice.zoneId = zoneId;
            ++slice.spawns;
            if (allyHome)
                slice.team |= 1 << 0;
            if (hordeHome)
                slice.team |= 1 << 1;
            if (slice.spawns == 1)
            {
                slice.mapId = point.getMapId();
                slice.x = point.getX();
                slice.y = point.getY();
                slice.z = point.getZ();
            }
            if (slice.zoneLevel <= 0 && areaId)
            {
                int32_t areaLevel = 0;
                if (sTravelMgr.TryGetValidatedAreaLevel(areaId, areaLevel) && areaLevel > 0)
                    slice.zoneLevel = areaLevel;
                else if (sTravelMgr.TryGetValidatedAreaLevel(zoneId, areaLevel) && areaLevel > 0)
                    slice.zoneLevel = areaLevel;
            }
        }
        if (slices.empty())
            continue;
        auto& out = index.zonesByEntry[entry];
        for (auto& [zoneId, slice] : slices)
            out.push_back(slice);
    }

    IndexBuilt() = true;
}

std::vector<GuildOrderEntrySource> GuildOrderSourcesFor(uint32_t itemId)
{
    OrderSourceIndex& index = Index();
    auto found = index.byItem.find(itemId);
    return found == index.byItem.end() ? std::vector<GuildOrderEntrySource>() : found->second;
}

std::vector<GuildOrderZoneSlice> GuildOrderZonesFor(int32_t entry)
{
    OrderSourceIndex& index = Index();
    auto found = index.zonesByEntry.find(entry);
    return found == index.zonesByEntry.end() ? std::vector<GuildOrderZoneSlice>() : found->second;
}

bool GuildOrderSkinMobFor(Player* bot, uint32_t creatureEntry)
{
    if (!bot || !creatureEntry)
        return false;
    std::optional<OrderRecord> record = Find(bot->GetGUIDLow());
    if (!record || record->type != OrderType::Farm || record->state != OrderState::Working || !record->itemId)
        return false;
    if (!bot->HasSkill(SKILL_SKINNING))
        return false;
    for (GuildOrderEntrySource const& source : GuildOrderSourcesFor(record->itemId))
    {
        if (source.entry != int32_t(creatureEntry) || !(source.flag & SourceSkin))
            continue;
        if (bot->GetSkillValue(SKILL_SKINNING) >= source.reqSkill)
            return true;
    }
    return false;
}

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
                if (spell->Effect[i] != SPELL_EFFECT_CREATE_ITEM || spell->EffectItemType[i] != itemId)
                    continue;
                plan.sources = SourceCraft;
                SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
                for (SkillLineAbilityMap::const_iterator it = bounds.first; it != bounds.second; ++it)
                {
                    if (it->second && it->second->skillId)
                    {
                        plan.skillId = it->second->skillId;
                        plan.skillValue = it->second->min_value;
                        break;
                    }
                }
                return plan;
            }
        }
        plan.verdict = OrderReason::NoRecipe;
        return plan;
    }

    OrderSourceIndex& index = Index();

    uint32_t const botLevel = bot->GetLevel();
    Team const team = bot->GetTeam();
    WorldPosition const botPos(bot);
    bool const lowGatherLevel = botLevel < kGatherTripMinLevel;

    auto found = index.byItem.find(itemId);
    if (found == index.byItem.end())
    {
        plan.verdict = OrderReason::NoSource;
        return plan;
    }

    bool sawAny = false;
    bool sawHerbOre = false;
    uint8_t skillFail = 0;
    bool sawInRange = false;

    // Usable need: sources passing every gate (reported when zones exist).
    uint8_t usableSources = 0;
    uint32_t usableMinLevel = 0;
    uint32_t usableSkillId = 0;
    uint32_t usableSkillValue = 0;
    // Fallback need: spawned sources outside enemy home zones, with the
    // minimum skill that unlocks one (reported only on failure).
    uint8_t fallbackSources = 0;
    uint32_t fallbackMinLevel = 0;
    uint32_t fallbackSkillId = 0;
    uint32_t fallbackSkillValue = 0;

    std::map<uint32_t, ZoneAccum> zones;
    for (GuildOrderEntrySource const& source : found->second)
    {
        auto slices = index.zonesByEntry.find(source.entry);
        if (slices == index.zonesByEntry.end())
            continue;
        sawAny = true;
        bool const creature = source.entry > 0;
        bool const herbOrOre = source.flag == SourceHerb || source.flag == SourceOre;
        if (herbOrOre)
            sawHerbOre = true;

        for (GuildOrderZoneSlice const& slice : slices->second)
        {
            bool const enemyHome = team == ALLIANCE ? (slice.team & (1 << 1)) : (slice.team & (1 << 0));
            if (enemyHome)
                continue;
            uint32_t sourceLevel = creature ? source.level :
                (slice.zoneLevel > 0 ? uint32_t(slice.zoneLevel) : 0);
            if (sourceLevel == 0)
                continue;

            fallbackSources |= source.flag;
            if (!fallbackMinLevel || sourceLevel < fallbackMinLevel)
                fallbackMinLevel = sourceLevel;
            if (source.skillId != SKILL_NONE &&
                (fallbackSkillId == 0 || source.reqSkill < fallbackSkillValue))
            {
                fallbackSkillId = source.skillId;
                fallbackSkillValue = source.reqSkill;
            }

            if (!SourceLevelFits(sourceLevel, botLevel))
                continue;
            sawInRange = true;
            if (herbOrOre && lowGatherLevel)
                continue;
            if (source.skillId != SKILL_NONE)
            {
                if (!bot->HasSkill(source.skillId))
                {
                    skillFail = 2;
                    continue;
                }
                if (bot->GetSkillValue(source.skillId) < source.reqSkill)
                {
                    skillFail = std::max(skillFail, uint8_t(1));
                    continue;
                }
            }

            usableSources |= source.flag;
            if (!usableMinLevel || sourceLevel < usableMinLevel)
                usableMinLevel = sourceLevel;
            if (source.skillId != SKILL_NONE &&
                (usableSkillId == 0 || source.reqSkill > usableSkillValue))
            {
                usableSkillId = source.skillId;
                usableSkillValue = source.reqSkill;
            }

            ZoneAccum& accum = zones[slice.zoneId];
            accum.spawns += slice.spawns;
            accum.chance = std::max(accum.chance, source.chance);
            if (slice.mapId == botPos.getMapId())
            {
                float dx = slice.x - botPos.getX();
                float dy = slice.y - botPos.getY();
                float dz = slice.z - botPos.getZ();
                float const dist = dx * dx + dy * dy + dz * dz;
                if (!accum.hasNearest || dist < accum.nearest)
                {
                    accum.nearest = dist;
                    accum.hasNearest = true;
                }
            }
        }
    }

    if (zones.empty())
    {
        plan.sources = fallbackSources;
        plan.minLevel = fallbackMinLevel;
        plan.skillId = fallbackSkillId;
        plan.skillValue = fallbackSkillValue;
        plan.verdict = OrderVerdict(lowGatherLevel && sawHerbOre, skillFail, sawInRange, sawAny);
        return plan;
    }

    plan.sources = usableSources;
    plan.minLevel = usableMinLevel;
    plan.skillId = usableSkillId;
    plan.skillValue = usableSkillValue;
    plan.verdict = OrderReason::None;
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
