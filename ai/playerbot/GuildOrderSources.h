#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>
#include <vector>

class Player;

namespace ai
{

// The one source finder for guild orders: which sources this bot can use for
// the item, its verdict and the farm zones ranked best first. Search, give and
// travel all ask it, so they never disagree. World thread only.
TortoiseBots::GuildOrders::SourcePlan PlanGuildOrderSources(Player* bot, uint32_t itemId,
                                                           TortoiseBots::GuildOrders::OrderType type);

// One entry's usable sources for one item, read from the order source index.
struct GuildOrderEntrySource
{
    int32_t entry = 0;      // drop-map sign: creature +, game object -
    uint8_t flag = 0;       // SourceFlag bits usable from this entry
    float chance = 0.0f;    // best farmable chance for the item
    uint32_t level = 0;     // mob max level; 0 for game objects (graded per zone)
    uint32_t skillId = 0;   // SKILL_NONE when no skill needed
    uint32_t reqSkill = 0;
};

// One entry's spawn summary, built once from the cached spawn areas.
struct GuildOrderSpawnSummary
{
    uint32_t spawns = 0;
    uint32_t zoneId = 0;    // dominant zone, 0 = none resolved
    int32_t areaLevel = 0;  // dominant zone level, <= 0 = unknown
    bool allyHome = false;
    bool hordeHome = false;
};

// The order source index: item -> entries (corpse loot with resolved
// reference rows, skinning loot, game-object loot) plus the per-entry spawn
// summary. Built once, lazily, off the hot path; planner, search and travel
// all read it, so a plan is a small lookup, never a spawn walk.
std::vector<GuildOrderEntrySource> GuildOrderSourcesFor(uint32_t itemId);
GuildOrderSpawnSummary GuildOrderSpawnSummaryFor(int32_t entry);

// True when the creature entry can skin the bot's Working farm order item:
// the narrow gate for emptying order-mob corpses (loot queue and store).
bool GuildOrderSkinMobFor(Player* bot, uint32_t creatureEntry);

} // namespace ai
