#pragma once

// Follow-first rules for bots with a real-player master (owner report
// 2026-11-01): when the master moves far enough that the bot would start
// following, the move overrides everything non-combat - buffing, drinking,
// eating, other downtime. Downtime only happens while the master stands
// still (or the follow distance is not exceeded). Combat unchanged.
//
// No core includes: callers translate game state into plain inputs, so the
// rules stay testable in tools/test_follow_urgency_policy.cpp without the
// server.

#include <cmath>

namespace ai
{
    // Distance past which a bot must be moving to its master instead of
    // doing downtime. Mirrors the follow trigger's own dead zone: the
    // "update follow" trigger fires past 5 yd from the master, and past the
    // follow leash (free-move range) the "out of free move range" row runs
    // at ACTION_NORMAL. Below both, the bot is in place and may buff/drink.
    inline bool FollowOverridesDowntime(float botToMasterYd, float followRangeYd,
        float stationDeadZoneYd = 5.0f)
    {
        if (!std::isfinite(botToMasterYd) || !std::isfinite(followRangeYd) ||
            !std::isfinite(stationDeadZoneYd))
            return false;
        if (botToMasterYd <= 0.0f)
            return false;
        float const range = followRangeYd > 0.0f ? followRangeYd : 0.0f;
        float const dead = stationDeadZoneYd > 0.0f ? stationDeadZoneYd : 0.0f;
        float const limit = range > dead ? range : dead;
        return botToMasterYd > limit;
    }

    // A buff cast-time spell must not stop a bot that should be following:
    // instant buffs may be cast on the run (the cast starts while moving and
    // the follow movement is not broken for them). Cast-time buffs wait
    // until the bot is back in place.
    inline bool BuffCastHoldsFollow(bool spellNeedsStandStill)
    {
        return spellNeedsStandStill;
    }
}
