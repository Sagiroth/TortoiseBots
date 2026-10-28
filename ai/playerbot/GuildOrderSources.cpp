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
#include <mutex>
#include <unordered_map>

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

struct ZoneVote
{
    uint32_t spawns = 0;
    int32_t areaLevel = 0;
    bool allyHome = false;
    bool hordeHome = false;
};

uint32_t ZoneOf(uint16_t areaFlag, uint32_t mapId, uint32_t& areaId, bool& allyHome, bool& hordeHome)
{
    areaId = 0;
    allyHome = false;
    hordeHome = false;
    AreaTableEntry const* area = AreaEntry::GetByAreaFlagAndMap(areaFlag, mapId);
    if (!area)
        return 0;
    areaId = area->Id;
    uint32_t areaTeam = area->Team;
    uint32_t zoneId = area->ZoneId ? area->ZoneId : area->Id;
    if (areaTeam == AREATEAM_NONE && area->ZoneId)
        if (AreaEntry const* zone = AreaEntry::GetById(area->ZoneId))
            areaTeam = zone->Team;
    allyHome = areaTeam == AREATEAM_ALLY;
    hordeHome = areaTeam == AREATEAM_HORDE;
    return zoneId;
}

struct OrderSourceIndex
{
    std::unordered_map<uint32_t, std::vector<GuildOrderEntrySource>> byItem;
    std::unordered_map<int32_t, GuildOrderSpawnSummary> spawnByEntry;
    // Per-entry per-zone spawn counts and enemy-home flags. The nearest
    // distance per zone is bot-relative and stays a live computation over
    // the cached spawn positions at plan time.
    std::unordered_map<int32_t, std::map<uint32_t, uint32_t>> spawnsByZone;
    std::unordered_map<int32_t, std::map<uint32_t, uint8_t>> teamByZone;
    std::unordered_map<uint32_t, int32_t> levelByZone;
    bool built = false;
    std::mutex lock;
};

// Enemy-home flags per zone, as stored in teamByZone.
uint8_t const kZoneAllyHome = 1 << 0;
uint8_t const kZoneHordeHome = 1 << 1;

OrderSourceIndex& Index()
{
    static OrderSourceIndex index;
    return index;
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

void BuildOrderSourceIndex()
{
    OrderSourceIndex& index = Index();
    std::lock_guard<std::mutex> guard(index.lock);
    if (index.built)
        return;

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

    EntryGuidps const& guidps = GAI_VALUE_REF(EntryGuidps, "entry guidps");
    for (auto& [entry, points] : guidps)
    {
        std::map<uint32_t, ZoneVote> votes;
        for (AsyncGuidPosition const& point : points)
        {
            uint32_t areaId = 0;
            bool allyHome = false, hordeHome = false;
            uint32_t const zoneId = ZoneOf(point.getAreaFlag(), point.getMapId(), areaId, allyHome, hordeHome);
            if (!zoneId)
                continue;
            ZoneVote& vote = votes[zoneId];
            ++vote.spawns;
            vote.allyHome = vote.allyHome || allyHome;
            vote.hordeHome = vote.hordeHome || hordeHome;
            if (areaId && !index.levelByZone.count(zoneId))
            {
                int32_t areaLevel = 0;
                if (sTravelMgr.TryGetValidatedAreaLevel(areaId, areaLevel) && areaLevel > 0)
                    index.levelByZone[zoneId] = areaLevel;
                else if (sTravelMgr.TryGetValidatedAreaLevel(zoneId, areaLevel) && areaLevel > 0)
                    index.levelByZone[zoneId] = areaLevel;
            }
            index.spawnsByZone[entry][zoneId]++;
            uint8_t& team = index.teamByZone[entry][zoneId];
            if (allyHome)
                team |= kZoneAllyHome;
            if (hordeHome)
                team |= kZoneHordeHome;
        }
        if (votes.empty())
            continue;
        auto best = votes.begin();
        for (auto it = votes.begin(); it != votes.end(); ++it)
            if (it->second.spawns > best->second.spawns)
                best = it;
        GuildOrderSpawnSummary summary;
        for (auto& [zoneId, vote] : votes)
        {
            summary.spawns += vote.spawns;
            summary.allyHome = summary.allyHome || vote.allyHome;
            summary.hordeHome = summary.hordeHome || vote.hordeHome;
        }
        summary.zoneId = best->first;
        auto level = index.levelByZone.find(best->first);
        summary.areaLevel = level == index.levelByZone.end() ? 0 : level->second;
        index.spawnByEntry[entry] = summary;
    }

    index.built = true;
}

void EnsureOrderSourceIndex()
{
    OrderSourceIndex& index = Index();
    {
        std::lock_guard<std::mutex> guard(index.lock);
        if (index.built)
            return;
    }
    BuildOrderSourceIndex();
}

struct ZoneAccum
{
    uint32_t spawns = 0;
    float chance = 0.0f;
    float nearest = 0.0f;
    bool hasNearest = false;
};

} // namespace

std::vector<GuildOrderEntrySource> GuildOrderSourcesFor(uint32_t itemId)
{
    EnsureOrderSourceIndex();
    OrderSourceIndex& index = Index();
    std::lock_guard<std::mutex> guard(index.lock);
    auto found = index.byItem.find(itemId);
    return found == index.byItem.end() ? std::vector<GuildOrderEntrySource>() : found->second;
}

GuildOrderSpawnSummary GuildOrderSpawnSummaryFor(int32_t entry)
{
    EnsureOrderSourceIndex();
    OrderSourceIndex& index = Index();
    std::lock_guard<std::mutex> guard(index.lock);
    auto found = index.spawnByEntry.find(entry);
    return found == index.spawnByEntry.end() ? GuildOrderSpawnSummary() : found->second;
}

bool GuildOrderSkinMobFor(Player* bot, uint32_t creatureEntry)
{
    if (!bot || !creatureEntry)
        return false;
    std::optional<OrderRecord> record = Find(bot->GetGUIDLow());
    if (!record || record->type != OrderType::Farm || record->state != OrderState::Working || !record->itemId)
        return false;
    for (GuildOrderEntrySource const& source : GuildOrderSourcesFor(record->itemId))
    {
        if (source.entry == int32_t(creatureEntry) && (source.flag & SourceSkin))
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

    EnsureOrderSourceIndex();
    OrderSourceIndex& index = Index();

    uint32_t const botLevel = bot->GetLevel();
    Team const team = bot->GetTeam();
    WorldPosition const botPos(bot);
    bool const lowGatherLevel = botLevel < kGatherTripMinLevel;

    std::vector<GuildOrderEntrySource> candidates;
    {
        std::lock_guard<std::mutex> guard(index.lock);
        auto found = index.byItem.find(itemId);
        if (found == index.byItem.end())
        {
            plan.verdict = OrderReason::NoSource;
            return plan;
        }
        candidates = found->second;
    }
    bool sawAny = false;
    bool sawHerbOre = false;
    uint8_t skillFail = 0;
    bool sawInRange = false;

    auto noteNeed = [&](GuildOrderEntrySource const& source, uint32_t sourceLevel)
    {
        plan.sources |= source.flag;
        if (sourceLevel > 0 && (!plan.minLevel || sourceLevel < plan.minLevel))
            plan.minLevel = sourceLevel;
        if (source.skillId != SKILL_NONE && (plan.skillId == 0 || source.reqSkill > plan.skillValue))
        {
            plan.skillId = source.skillId;
            plan.skillValue = source.reqSkill;
        }
    };

    std::map<uint32_t, ZoneAccum> zones;
    // Entries that survive every gate: the travel branch asks the index for
    // the same per-item list and re-applies these gates, so plan and travel
    // never disagree.
    {
        std::lock_guard<std::mutex> guard(index.lock);
        for (GuildOrderEntrySource const& source : candidates)
        {
        sawAny = true;
        bool const creature = source.entry > 0;
        bool const herbOrOre = source.flag == SourceHerb || source.flag == SourceOre;
        if (herbOrOre)
            sawHerbOre = true;

        auto spawns = index.spawnsByZone.find(source.entry);
        auto teams = index.teamByZone.find(source.entry);
        if (spawns == index.spawnsByZone.end())
        {
            // Lootable but never spawned: still tells the verdict what the
            // bot would need, but grows no zone.
            if (creature)
            {
                noteNeed(source, source.level);
                if (SourceLevelFits(source.level, botLevel))
                    sawInRange = true;
            }
            continue;
        }

        for (auto& [zoneId, zoneSpawns] : spawns->second)
        {
            bool enemyHome = false;
            if (teams != index.teamByZone.end())
            {
                auto zoneTeam = teams->second.find(zoneId);
                if (zoneTeam != teams->second.end())
                    enemyHome = team == ALLIANCE ? (zoneTeam->second & kZoneHordeHome) :
                        (zoneTeam->second & kZoneAllyHome);
            }
            if (enemyHome)
                continue;

            uint32_t sourceLevel = source.level;
            if (!creature)
            {
                auto level = index.levelByZone.find(zoneId);
                if (level == index.levelByZone.end() || level->second <= 0)
                    continue;
                sourceLevel = uint32_t(level->second);
            }
            noteNeed(source, sourceLevel);
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

            ZoneAccum& accum = zones[zoneId];
            accum.spawns += zoneSpawns;
            accum.chance = std::max(accum.chance, source.chance);
        }
    }
    }

    if (zones.empty())
    {
        plan.verdict = OrderVerdict(lowGatherLevel && sawHerbOre, skillFail, sawInRange, sawAny);
        return plan;
    }

    // Nearer wins ties: walk the cached spawn positions of the surviving
    // entries once (positions only; zones resolve from the cached flags).
    EntryGuidps const& guidps = GAI_VALUE_REF(EntryGuidps, "entry guidps");
    for (GuildOrderEntrySource const& source : candidates)
    {
        auto spawns = guidps.find(source.entry);
        if (spawns == guidps.end())
            continue;
        for (AsyncGuidPosition const& point : spawns->second)
        {
            if (point.getMapId() != botPos.getMapId())
                continue;
            uint32_t areaId = 0;
            bool allyHome = false, hordeHome = false;
            uint32_t const zoneId = ZoneOf(point.getAreaFlag(), point.getMapId(), areaId, allyHome, hordeHome);
            auto accum = zones.find(zoneId);
            if (zoneId == 0 || accum == zones.end())
                continue;
            float const dist = botPos.sqDistance(point);
            if (!accum->second.hasNearest || dist < accum->second.nearest)
            {
                accum->second.nearest = dist;
                accum->second.hasNearest = true;
            }
        }
    }

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
