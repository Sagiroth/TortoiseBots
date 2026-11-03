#pragma once

#include "GuildOrderContract.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Pure rules for guild-order source finding: which sources a bot may use for
// an item and how its farm zones rank. Header-only and free of core types so
// the rules stay unit tested on their own
// (tools/test_guild_order_sources_policy.cpp); ai/playerbot/GuildOrderSources.h
// feeds them plain values from the loot templates and the spawn cache.
namespace TortoiseBots
{
namespace GuildOrders
{

// Sources above the bot's level stop counting. A +3 mob is already red-ish
// for a fresh bot; anything higher is a corpse run, not a farm.
inline constexpr uint32_t kSourceLevelMargin = 3;

// Herb and ore trips start at level 10, like the bots' own gathering trips:
// below that every errand is a walk through mobs the bot cannot fight.
inline constexpr uint32_t kGatherTripMinLevel = 10;

// A solo bot never plans elite, rare-elite or boss creatures as a farm
// source (creature_template rank 1-3). Rare (non-elite) creatures (rank 4)
// fight like normal mobs, so they stay allowed.
inline bool CreatureRankExcludedFromSoloFarm(uint32_t rank)
{
    return rank == 1 || rank == 2 || rank == 3;
}
// A solo farm leg never walks into an instance: the travel search only
// serves points on the bot's own map at a survivable area level, so a
// planned dungeon zone only buys empty searches. Instance maps are read
// from the map entry at the call site; the rule itself stays unit tested.
// Map ids 0/1/530/571 are the open-world continents; everything else
// (dungeons, raids, battlegrounds) is not a farm zone.
inline bool FarmZoneMapSoloWalkable(uint32_t mapId)
{
    return mapId == 0 || mapId == 1 || mapId == 530 || mapId == 571;
}

// Drops below this chance never look farmable: rare world drops would
// otherwise win zones by spawn count while the bot grinds hundreds of kills.
inline constexpr float kMinFarmDropChancePct = 1.0f;

// Skinning skill needed for a mob level. Mirrors the core rule the loot code
// already copies (LootObjectStack.cpp), so the plan never promises a skin
// the skin-after-loot chain would refuse for lack of skill.
inline uint32_t SkinningSkillForMobLevel(uint32_t mobLevel)
{
    if (mobLevel <= 10)
        return 1;
    if (mobLevel < 20)
        return (mobLevel - 10) * 10;
    return mobLevel * 5;
}

// A source counts when its level fits the bot's window. Source level for a
// mob is its template max level; for a node its zone's
// (nodes keep no mob band).
inline bool SourceLevelFits(uint32_t sourceLevel, uint32_t botLevel)
{
    return sourceLevel > 0 && sourceLevel <= botLevel + kSourceLevelMargin;
}

// Fallback window for the travel leg's second tier: one level past the plan
// (+4), the standing cap the bot's own grind orders obey. The planner keeps
// the stricter +3 for its verdicts and zones; the leg only reaches here when
// the planned entries yield no spot in the planned zone, so a red mob one
// level up beats cycling zones past a farmable source.
inline constexpr uint32_t kFallbackLevelMargin = 4;

inline bool FallbackSourceLevelFits(uint32_t sourceLevel, uint32_t botLevel)
{
    return sourceLevel > 0 && sourceLevel <= botLevel + kFallbackLevelMargin;
}

// A creature the bot cannot attack is no farm source: the grind hostility
// gate (GrindTravelDestination::IsActive) would refuse it as inactive, so the
// plan must not name it. Hostile means the faction-template reaction between
// the bot's own template and the creature's template is hostile - the same
// verdict the travel gate reads, minus the live unit (reputation at-war,
// contested-PvP guards) it cannot see at plan time. Plain values in, so the
// core lookups stay at the call site and the rule stays unit tested.
struct FactionTemplateMasks
{
    uint32_t faction = 0;
    uint32_t ourMask = 0;
    uint32_t hostileMask = 0;
    uint32_t enemyFaction[4] = { 0, 0, 0, 0 };
};

// Mirror of the core reaction read (PlayerbotAI::GetFactionReaction, copied
// from the reputation code): hostile when the mask pairs or either side's
// enemy list says so. A neutral (non-hostile) creature is no farm source.
inline bool FactionTemplatesHostile(FactionTemplateMasks const& bot, FactionTemplateMasks const& prey)
{
    if (prey.ourMask & bot.hostileMask)
        return true;
    for (uint32_t enemy : bot.enemyFaction)
        if (enemy && enemy == prey.faction)
            return true;
    if (bot.ourMask & prey.hostileMask)
        return true;
    for (uint32_t enemy : prey.enemyFaction)
        if (enemy && enemy == bot.faction)
            return true;
    return false;
}

// Zone score: spawns x chance. A zone with twice the mobs at half the chance
// pays the same per walk, so both rank equal and the tie-break decides.
inline double ZoneScore(uint32_t spawns, float chancePct)
{
    return double(spawns) * double(chancePct);
}

// A failed farm-leg travel attempt is remembered per target point, not per
// zone: one mesh hole where a zone's spawn sample sat says nothing about the
// zone's other farm spots, and the travel pick tries those next. A zone stays
// out of the plan only once this many different target points in it failed
// inside the window. Stamps are server uptime ms, 0 = never failed; uint32
// wrap keeps the subtraction meaningful.
inline constexpr uint32_t kFarmZoneUnreachableMs = 30 * 60 * 1000;
inline constexpr size_t kFarmZoneFailedTargets = 2;

// Target points within this many yards count as the same target: the travel
// pick's approach point for one spawn moves a few yards between picks.
inline constexpr float kFarmZoneTargetCellYd = 10.0f;

inline bool FarmZoneFailureFresh(uint32_t failedAtMs, uint32_t nowMs)
{
    return failedAtMs != 0 && nowMs - failedAtMs < kFarmZoneUnreachableMs;
}

struct FarmZoneTargetPoint
{
    uint32_t mapId = 0;
    int32_t cellX = 0, cellY = 0, cellZ = 0;

    bool operator==(FarmZoneTargetPoint const& other) const
    {
        return mapId == other.mapId && cellX == other.cellX && cellY == other.cellY &&
            cellZ == other.cellZ;
    }
};

inline FarmZoneTargetPoint FarmZoneTargetPointAt(uint32_t mapId, float x, float y, float z)
{
    auto cell = [](float value) { return int32_t(std::floor(value / kFarmZoneTargetCellYd)); };
    FarmZoneTargetPoint point;
    point.mapId = mapId;
    point.cellX = cell(x);
    point.cellY = cell(y);
    point.cellZ = cell(z);
    return point;
}

struct FarmZoneTargetFailure
{
    FarmZoneTargetPoint point;
    uint32_t failedAtMs = 0;
};

// Distinct target points of one zone whose last failure is still inside the
// window: repeats at the same point are one target, not two.
inline size_t FarmZoneFailedTargets(std::vector<FarmZoneTargetFailure> const& failures, uint32_t nowMs)
{
    size_t distinct = 0;
    for (size_t i = 0; i < failures.size(); ++i)
    {
        if (!FarmZoneFailureFresh(failures[i].failedAtMs, nowMs))
            continue;
        bool repeat = false;
        for (size_t j = 0; j < i; ++j)
            repeat = repeat || (failures[j].point == failures[i].point &&
                FarmZoneFailureFresh(failures[j].failedAtMs, nowMs));
        if (!repeat)
            ++distinct;
    }
    return distinct;
}

// The zone is left out of the plan: at least two different targets in it
// could not be walked to inside the window.
inline bool FarmZoneSkipped(std::vector<FarmZoneTargetFailure> const& failures, uint32_t nowMs)
{
    return FarmZoneFailedTargets(failures, nowMs) >= kFarmZoneFailedTargets;
}

// Record one failed target point: the point keeps its latest stamp, repeats
// refresh it, and stamps older than the window drop out on the way.
inline void RecordFarmZoneTargetFailure(std::vector<FarmZoneTargetFailure>& failures,
    FarmZoneTargetPoint const& point, uint32_t nowMs)
{
    if (!nowMs)
        return;
    for (size_t i = 0; i < failures.size();)
    {
        if (!FarmZoneFailureFresh(failures[i].failedAtMs, nowMs))
            failures.erase(failures.begin() + i);
        else
            ++i;
    }
    for (FarmZoneTargetFailure& failure : failures)
    {
        if (failure.point == point)
        {
            failure.failedAtMs = nowMs;
            return;
        }
    }
    failures.push_back(FarmZoneTargetFailure{ point, nowMs });
}

// The travel layer's navmesh verdict names a zone the leg cannot serve: a
// mesh hole (nopath) or a partial path (incomplete). A complete path whose
// dispatch still failed is a movement problem, and crossmap was never probed,
// so neither says anything about the zone.
inline bool TravelPathUnreachable(std::string const& pathTag)
{
    return pathTag == "incomplete" || pathTag == "nopath";
}

// Farm-zone plan order: zones on the bot's map first, then the ones the
// travel layer's route gate accepts, then the nearest farm point (the
// planner's own sample spawn), then the richer zone. A zone with no spawn on
// the bot's map has no distance and ranks after every same-map zone, by score
// among the rest. Distance is straight-line: the only node-graph cost the
// travel layer offers is a full A* per pair (TravelNodeMap::getRoute), so the
// gate's verdict is the reachability signal and the walk itself is measured
// in a straight line from where the bot stands.
inline bool FarmZoneBefore(FarmZone const& a, FarmZone const& b)
{
    if (a.onBotMap != b.onBotMap)
        return a.onBotMap;
    if (a.onBotMap && a.reachable != b.reachable)
        return a.reachable;
    if (a.onBotMap && a.distanceSq != b.distanceSq)
        return a.distanceSq < b.distanceSq;
    if (a.score != b.score)
        return a.score > b.score;
    return a.zoneId < b.zoneId;
}

// Drop the zones whose last travel attempt failed on the mesh; `unreachable`
// is index-parallel to `zones`. When every zone is flagged the plan keeps
// them: an empty plan reads as nosource and would end the order, while the
// travel layer's own retry and teleport may still get there. No zone is
// added, so the search stays inside the planner's existing limits.
inline void DropUnreachableFarmZones(std::vector<FarmZone>& zones,
    std::vector<uint8_t> const& unreachable)
{
    if (zones.size() != unreachable.size())
        return;
    bool anyReachable = false;
    for (uint8_t flagged : unreachable)
        anyReachable = anyReachable || !flagged;
    if (!anyReachable)
        return;
    size_t kept = 0;
    for (size_t i = 0; i < zones.size(); ++i)
        if (!unreachable[i])
            zones[kept++] = zones[i];
    zones.resize(kept);
}

// A drop row is usable for the farm plan when it is not a quest row and its
// chance clears the minimum. chancePct is the row's own percent (reference
// rows are resolved to their members before this check).
inline bool DropChanceFarmable(float chancePct, bool needsQuest)
{
    return !needsQuest && chancePct >= kMinFarmDropChancePct;
}

// Share of one member of an equal-chanced loot group: the rows carry chance
// 0 in the database, so each member rolls an even split of the group.
inline float EqualShareChance(uint32_t memberCount)
{
    return memberCount == 0 ? 0.0f : 100.0f / float(memberCount);
}

// A reference row gates its members by its own chance; an equal-chanced
// reference row carries chance 0, so its members gate by the equal share.
inline float ReferenceGate(float rowChancePct, float equalSharePct)
{
    float gate = rowChancePct > 0.0f ? rowChancePct : equalSharePct;
    return gate >= 100.0f ? 1.0f : gate / 100.0f;
}

// Chance of one member row: an explicit chance wins, otherwise the equal
// share of its group.
inline float MemberChance(float rowChancePct, float equalSharePct)
{
    return rowChancePct > 0.0f ? rowChancePct : equalSharePct;
}

// Verdict precedence when nothing is usable. A bot that is simply too young
// for herb/ore hears that first; then the skill failure (missing profession
// beats too-low skill); then level range; a sub-chance source never upgrades
// NoSource to NoRange. When the skill failure comes only from a fallback
// source while an unskilled source failed range, range wins: the bot could
// use the drop if it were in range, so the skill is not the blocker. Elite
// sources are invisible to the planner; when every source is elite the bot
// hears that instead of nosource.
inline OrderReason OrderVerdict(bool lowGatherLevel, uint8_t skillFail, bool sawInRange, bool sawAny,
    bool unskilledRangeFail = false, bool sawElite = false)
{
    if (!sawAny)
        return sawElite ? OrderReason::Elite : OrderReason::NoSource;
    if (lowGatherLevel)
        return OrderReason::LowLevel;
    if (!sawInRange && unskilledRangeFail)
        return OrderReason::NoRange;
    if (skillFail == 2)
        return OrderReason::NoSkill;
    if (skillFail == 1)
        return OrderReason::LowSkill;
    if (sawInRange)
        return OrderReason::NoSource;
    return OrderReason::NoRange;
}

// Craft recipe gate: the planner only promises a craft the bot itself can
// cast. `knowsRecipe` is the shared spell scan (FindCraftSpellFor) read as
// a bool, so the verdict never disagrees with the craft action.
inline OrderReason CraftRecipeVerdict(bool knowsRecipe)
{
    return knowsRecipe ? OrderReason::None : OrderReason::NoRecipe;
}

// Expected kills or gathers for an amount at the best source's chance.
// Skinning chances count per kill, drop chances per kill, node chances per
// gather. A zero chance never yields the item, so it estimates unbounded.
inline double ExpectedKills(uint32_t amount, float chancePct)
{
    if (chancePct <= 0.0f)
        return 1e9;
    return double(amount) / (double(chancePct) / 100.0);
}

// Easy is an evening's farm (about 30 kills), medium several sessions, hard
// a long grind. At a typical 40 % skinning chance Light Leather x10 needs 25
// kills (easy) while Golden Pearl x5 at 0.22 % needs over two thousand (hard);
// Copper Ore x20 at 100 % per vein needs 20 gathers (easy) but x200 crosses
// into medium.
inline constexpr double kDifficultyEasyMaxKills = 30.0;
inline constexpr double kDifficultyMediumMaxKills = 200.0;

inline OrderDifficulty EstimateDifficulty(double expectedKills)
{
    if (expectedKills <= kDifficultyEasyMaxKills)
        return OrderDifficulty::Easy;
    if (expectedKills <= kDifficultyMediumMaxKills)
        return OrderDifficulty::Medium;
    return OrderDifficulty::Hard;
}

// Whether an order-mode travel info skips the leisure IsPossible gates for
// this destination: only order sources of the carried item skip them.
inline bool OrderModeSkipsGates(bool orderMode, bool isOrderSource)
{
    return orderMode && isOrderSource;
}

// Whether the outgrown-zone leisure floor applies to a travel point: order
// mode plans its zone by the level + 3 rule, which may sit below the floor.
inline bool OutgrownFloorApplies(bool orderMode)
{
    return !orderMode;
}

// Whether the pick's 10 % far-partition skip applies: an order leg farms a
// planned zone, so it takes the nearest servable point instead of wandering
// to a farther band for variety.
inline bool FarPartitionSkipApplies(bool orderMode)
{
    return !orderMode;
}

} // namespace GuildOrders
} // namespace TortoiseBots
