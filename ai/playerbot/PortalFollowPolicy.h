#pragma once

// Pure decision rules for party bots stepping through a party mage's city
// portal after their real-player master. The caller translates game state
// into plain inputs, so the rules stay testable in
// tools/test_portal_follow_policy.cpp without the server.

#include <cstdint>

namespace ai
{
// How far from the portal (yards) counts as having reached it.
constexpr float kPortalFollowReachYd = 5.0f;

// How long a portal-follow episode may run before the fallback summon fires.
constexpr uint32_t kPortalFollowWindowMs = 45000;

// How long the mage that cast the portal waits after the others went or
// went still before it steps through itself.
constexpr uint32_t kPortalFollowMageDelayMs = 5000;

// A bot only steps through a portal of its own accord when all of this holds:
// real-player master on another map, alive and out of combat itself, follow
// (not stay/guard) strategy, not a pool bot.
inline bool PortalFollowEligible(bool masterIsRealPlayer, bool crossMap,
    bool botAlive, bool botInCombat, bool followStrategy, bool poolBot)
{
    return masterIsRealPlayer && crossMap && botAlive && !botInCombat &&
        followStrategy && !poolBot;
}

// The caster goes last: it waits while any other eligible follower still has
// to go through, or the short delay after the last movement, whichever
// settles first.
inline bool PortalFollowMageWaits(bool othersPending, uint32_t elapsedMs)
{
    return othersPending || elapsedMs < kPortalFollowMageDelayMs;
}

// The window ran out (or the portal is gone): stop walking and let the
// caller bring the bot with the existing summon instead.
inline bool PortalFollowWindowExpired(uint32_t elapsedMs)
{
    return elapsedMs >= kPortalFollowWindowMs;
}

// Reached the portal when within interact reach of it.
inline bool PortalFollowReached(float distanceYd)
{
    return distanceYd <= kPortalFollowReachYd;
}
}
