#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>
#include <utility>
#include <vector>

class Player;

namespace ai
{

// The one source finder for guild orders: which sources this bot can use for
// the item, its verdict and the farm zones ranked best first. Search, give and
// travel all ask it, so they never disagree. World thread only.
TortoiseBots::GuildOrders::SourcePlan PlanGuildOrderSources(Player* bot, uint32_t itemId,
                                                           TortoiseBots::GuildOrders::OrderType type);

// Difficulty estimate for the plan: farm uses the amount over the best chance
// (one item for search rows, the full amount for order rows), craft the
// hardest farmable reagent still missing, buy none. World thread only.
std::string PlanOrderDifficulty(Player* bot, uint32_t itemId,
                                TortoiseBots::GuildOrders::OrderType type, uint32_t amount);

// One entry's usable sources for one item, read from the order source index.
struct GuildOrderEntrySource
{
    int32_t entry = 0;       // drop-map sign: creature +, game object -
    uint8_t flag = 0;        // SourceFlag bits usable from this entry
    float chance = 0.0f;     // best farmable chance for the item
    uint32_t level = 0;      // mob max level; 0 for game objects (graded per zone)
    uint32_t skillId = 0;    // SKILL_NONE when no skill needed
    uint32_t reqSkill = 0;
    uint32_t rank = 0;       // creature_template rank; 0 for game objects
};

// Fishing entries in the index use a bias so they never collide with node
// entries (negative game-object ids): a fishing zone area id or a pool
// game-object entry stored as -(id + bias).
inline constexpr int32_t kFishingZoneEntryBias = 1000000;
inline constexpr uint32_t kMaxFishingZoneId = 100000;

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
bool GuildOrderSourcesReady();
std::vector<GuildOrderEntrySource> GuildOrderSourcesFor(uint32_t itemId);
std::vector<GuildOrderZoneSlice> GuildOrderZonesFor(int32_t entry);

// True when the creature entry can skin the bot's Working farm order item:
// the narrow gate for emptying order-mob corpses (loot queue and store).
bool GuildOrderSkinMobFor(Player* bot, uint32_t creatureEntry);

// The pole a Working farm order's grant minted for the planned zone, 0 when
// the order does not fish there.
uint32_t GuildOrderFishingPoleFor(Player* bot);

// The item and planned zone the bot currently farms for its Working order
// (0 item = none): the farm order item, the buy fallback remainder, or the
// craft reagent the package-5 value names. One Find so order mode never
// forks per type and never looks the record up twice.
std::pair<uint32_t, uint32_t> GuildOrderFarmItemFor(Player* bot);

// Whether the bot's Working order has its supply item waiting in the mailbox
// (the supply leg's mail item). Snapshot by PlayerTravelInfo so the async
// mail search and the order leg read the same verdict without touching the
// store on the worker. World thread only (reads mail state).
bool GuildOrderMailWaiting(Player* bot);

// Order-mode destination gate over the index: the entry is an order source
// of the item and usable in the planned zone (level + 3, +4 for mobs in the
// travel leg's fallback window, skill, team).
// hasSkill/skillValue carry the bot's skill for the entry's gate; team is
// the bot's Team as a number (0 = no team filter).
bool GuildOrderEntryUsableFor(uint32_t itemId, int32_t entry, uint32_t botLevel,
    uint32_t team, uint32_t zoneId, uint32_t skillValue, bool hasSkill);

// Whether the creature entry is hostile to the bot's own faction template:
// the planner-side mirror of the grind hostility gate. The planner and the
// travel leg both refuse non-hostile creatures, so planned zones only hold
// mobs the leg can actually attack. World thread only.
bool GuildOrderPreyHostileToBot(Player* bot, int32_t entry);

} // namespace ai
