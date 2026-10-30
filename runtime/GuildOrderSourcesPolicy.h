#pragma once

#include "GuildOrderContract.h"

#include <cstdint>

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

} // namespace GuildOrders
} // namespace TortoiseBots
