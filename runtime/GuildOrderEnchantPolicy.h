#pragma once

// Enchant-order meeting and replacement rules: which capital the giver must
// stand in, when `.bot order <bot> meet` comes now or waits, and who may
// replace someone else's running enchant order. Header-only and free of
// core/database types on purpose: exercised standalone in
// tools/test_guild_order_enchant_policy.cpp. The command feeds this header
// plain facts (zone ids, team, stored row state); the store keeps the meet
// request itself.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// The bot's main capital, by faction: the giver must stand here to meet.
inline constexpr uint32_t kStormwindCityZone = 1519;
inline constexpr uint32_t kOrgrimmarZone = 1637;

// The capital an enchant order of a bot of this faction meets in.
inline uint32_t EnchantCapitalZone(bool botAlliance)
{
    return botAlliance ? kStormwindCityZone : kOrgrimmarZone;
}

// Whether the giver's zone is the bot's capital. Zone ids only (no area or
// distance check): the sibling capital helper swaps this later.
inline bool GiverInBotCapital(uint32_t giverZoneId, bool botAlliance)
{
    return giverZoneId == EnchantCapitalZone(botAlliance);
}

// Outcome of `.bot order <bot> meet`, in check order.
enum class MeetVerdict { NoOrder, NotCapital, Later, Gathering, Coming };

struct MeetFacts
{
    bool liveEnchantOrder = false; // unfinished enchant order on this bot from this giver
    bool giverInCapital = false;   // giver stands in the bot's capital
    bool botTiedUp = false;        // bot in a dungeon/raid/battleground or grouped with a real player
    bool gathering = false;        // order still gathering: working, or waiting for another reason than needitems
};

inline MeetVerdict DecideMeet(MeetFacts const& facts)
{
    if (!facts.liveEnchantOrder)
        return MeetVerdict::NoOrder;
    if (!facts.giverInCapital)
        return MeetVerdict::NotCapital;
    if (facts.botTiedUp)
        return MeetVerdict::Later;
    if (facts.gathering)
        return MeetVerdict::Gathering;
    return MeetVerdict::Coming;
}

// Whether a new order may replace the bot's current row.
enum class EnchantReplaceVerdict { Allow, Busy };

inline EnchantReplaceVerdict DecideEnchantReplace(bool existingEnchantBlocks, bool sameGiver,
    bool giverMayOverride)
{
    // Only an unfinished enchant order of another giver blocks, and only for
    // a giver who is neither the guild master nor an officer.
    if (!existingEnchantBlocks)
        return EnchantReplaceVerdict::Allow;
    if (sameGiver || giverMayOverride)
        return EnchantReplaceVerdict::Allow;
    return EnchantReplaceVerdict::Busy;
}

} // namespace GuildOrders
} // namespace TortoiseBots
