#pragma once

// Enchant-order meeting and replacement rules: which capital the giver must
// stand in, when `.bot order <bot> meet` comes now or waits, and who may
// replace someone else's running enchant order. Header-only and free of
// core/database types on purpose: exercised standalone in
// tools/test_guild_order_meet_policy.cpp. The command feeds this header
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
enum class MeetVerdict { NoOrder, Cooldown, NotCapital, Later, Gathering, NothingToBring, Coming };

// A meeting that ended without the enchant blocks the next Meet this long,
// so the bot cannot be pulled over and over for nothing.
inline constexpr uint32_t kMeetFailCooldownMs = 5u * 60u * 1000u;

inline uint32_t MeetCooldownLeftMs(uint32_t nowMs, uint32_t failedMs)
{
    if (!failedMs)
        return 0;
    uint32_t const elapsed = nowMs - failedMs;
    return elapsed >= kMeetFailCooldownMs ? 0 : kMeetFailCooldownMs - elapsed;
}

struct MeetFacts
{
    bool liveEnchantOrder = false; // unfinished enchant order on this bot from this giver
    bool giverInCapital = false;   // giver stands in the bot's capital
    bool botTiedUp = false;        // bot in a dungeon/raid/battleground or grouped with a real player
    bool gathering = false;        // order still gathering: working, or waiting for another reason than needitems
    bool nothingToBring = false;   // waiting needitems, and the giver carries none of the missing items
    uint32_t cooldownLeftMs = 0;   // left of the failed-meeting cooldown
};

inline MeetVerdict DecideMeet(MeetFacts const& facts)
{
    if (!facts.liveEnchantOrder)
        return MeetVerdict::NoOrder;
    if (facts.cooldownLeftMs)
        return MeetVerdict::Cooldown;
    if (!facts.giverInCapital)
        return MeetVerdict::NotCapital;
    if (facts.botTiedUp)
        return MeetVerdict::Later;
    if (facts.gathering)
        return MeetVerdict::Gathering;
    if (facts.nothingToBring)
        return MeetVerdict::NothingToBring;
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
