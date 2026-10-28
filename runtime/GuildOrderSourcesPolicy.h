#pragma once

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
    if (mobLevel < 10)
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

} // namespace GuildOrders
} // namespace TortoiseBots
