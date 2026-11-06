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

// How often a bot without an episode re-scans for a party portal while its
// master is away. The grid visit is the only expensive step of the follow;
// the episode itself only re-reads its stored portal.
constexpr uint32_t kPortalFollowScanIntervalMs = 2000;

// Same-map portal trips (Ironforge and Stormwind share map 0, Kalimdor
// cities map 1) leave master and bot on one map but a continent apart. Past
// this the master is treated as gone through a portal, mirroring the
// far-away leave's twice-the-roam reach (2 * RpgDistance, 200 yd default).
constexpr float kPortalFollowFarMasterYd = 400.0f;

// Near the master on the same map the trip is over, whatever opened it.
constexpr float kPortalFollowAtMasterYd = 30.0f;

// A bot only steps through a portal of its own accord when all of this holds:
// real-player master gone (another map, or far on the same map), alive and
// out of combat itself, follow (not stay/guard) strategy, not a pool bot.
inline bool PortalFollowEligible(bool masterIsRealPlayer, bool masterGone,
    bool botAlive, bool botInCombat, bool followStrategy, bool poolBot)
{
    return masterIsRealPlayer && masterGone && botAlive && !botInCombat &&
        followStrategy && !poolBot;
}

// Gone means another map, or far on the same map (a same-map portal trip).
inline bool PortalFollowMasterGone(bool crossMap, float masterDistanceYd)
{
    return crossMap || masterDistanceYd > kPortalFollowFarMasterYd;
}

// Back with the master: same map and near. Ends the episode outright.
inline bool PortalFollowAtMaster(bool crossMap, float masterDistanceYd)
{
    return !crossMap && masterDistanceYd <= kPortalFollowAtMasterYd;
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
