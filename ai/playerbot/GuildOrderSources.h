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

// One zone slice of an entry's spawns: flat per-entry list in the index.
struct GuildOrderZoneSlice
{
    uint32_t zoneId = 0;
    uint32_t spawns = 0;
    uint8_t team = 0;       // bit 0 ally home, bit 1 horde home
    int32_t zoneLevel = 0;  // <= 0 = unknown
    uint32_t mapId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;  // sample spawn position
};

// The order source index: item -> entries (corpse loot with resolved
// reference rows, skinning loot, game-object loot) plus the flat per-entry
// zone slices. Built once at startup after the travel data is ready; read
// only at runtime, so planning is a small lookup, never a spawn walk.
void BuildGuildOrderSourceIndex();
std::vector<GuildOrderEntrySource> GuildOrderSourcesFor(uint32_t itemId);
std::vector<GuildOrderZoneSlice> GuildOrderZonesFor(int32_t entry);

// True when the creature entry can skin the bot's Working farm order item:
// the narrow gate for emptying order-mob corpses (loot queue and store).
bool GuildOrderSkinMobFor(Player* bot, uint32_t creatureEntry);
