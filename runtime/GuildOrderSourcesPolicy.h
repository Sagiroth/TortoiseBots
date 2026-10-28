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
// use the drop if it were in range, so the skill is not the blocker.
inline OrderReason OrderVerdict(bool lowGatherLevel, uint8_t skillFail, bool sawInRange, bool sawAny,
    bool unskilledRangeFail = false)
{
    if (!sawAny)
        return OrderReason::NoSource;
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

// Whether an order-mode travel info skips the leisure IsPossible gates for
// this destination: only order sources of the carried item skip them.
inline bool OrderModeSkipsGates(bool orderMode, bool isOrderSource)
{
    return orderMode && isOrderSource;
}

} // namespace GuildOrders
} // namespace TortoiseBots
