#pragma once

#include <cstdint>

namespace ai
{
// Interaction distance to flight master (yards).
constexpr float kPartyFlightReachYd = 5.0f;

// How long a bot may spend walking to and attempting to board the flight master before giving up.
constexpr uint32_t kPartyFlightBoardWindowMs = 60000;

// Maximum duration of a party flight episode (including flight duration).
constexpr uint32_t kPartyFlightEpisodeMaxMs = 15 * 60 * 1000;

// Distance from master on the same map considered "at master" (ends episode).
constexpr float kPartyFlightAtMasterYd = 30.0f;

// Whether a party bot is eligible to participate in party flight.
inline bool PartyFlightEligible(bool masterIsRealPlayer, bool botAlive, bool botInCombat,
    bool followStrategy)
{
    return masterIsRealPlayer && botAlive && !botInCombat && followStrategy;
}

// Whether the bot is back with the master on the same map.
inline bool PartyFlightAtMaster(bool crossMap, float masterDistanceYd)
{
    return !crossMap && masterDistanceYd <= kPartyFlightAtMasterYd;
}

// Whether the walk-to-board window has expired.
inline bool PartyFlightBoardExpired(uint32_t elapsedMs)
{
    return elapsedMs >= kPartyFlightBoardWindowMs;
}

// Whether the entire episode has exceeded maximum time.
inline bool PartyFlightEpisodeExpired(uint32_t elapsedMs)
{
    return elapsedMs >= kPartyFlightEpisodeMaxMs;
}

// Whether the bot has reached the flight master within interaction distance.
inline bool PartyFlightReached(float distanceYd)
{
    return distanceYd <= kPartyFlightReachYd;
}

} // namespace ai
