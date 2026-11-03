#pragma once

// Follow-spot ground safety shared by every formation (issue #217 and the
// raid ledge guard). A bot's follow spot is derived from the leader's
// position, which says nothing about the floor below it: on a crypt stair a
// missed floor lookup drags the bot into the terrain under the crypt, and at
// a ledge the spot can hang over a drop the leader already walked off.
//
// NearFormation carried three guards for this (stair headroom, a maximum
// ground drop, a line-of-sight check against terrain). The raid formation
// work needs them for every formation, so the decision lives here and the
// callers stay testable in tools/test_raid_follow_formation_policy.cpp.
//
// No core includes.

namespace ai
{
    // A follow spot whose ground is farther than this below the leader is a
    // ledge, a missed floor or a fall, never a step: refuse the spot and let
    // the follow fall back to the leader's own path. Four yards covers the
    // crypt stair's rise between two follower stations and refuses the 8+ yd
    // drops seen at the Jeklik stair and the Arlokk rock.
    constexpr float kFollowSpotMaxGroundDropYd = 4.0f;

    // Map::GetHeight casts its ray downward from z + 2. A spot behind a
    // leader walking downhill sits above the leader's own elevation, so the
    // search must start with headroom or it starts underneath the steps and
    // misses the floor structure.
    constexpr float kFollowSpotStairHeadroomYd = 4.0f;

    // Pure decision: is the ground below the spot a drop the leader is not
    // standing on? `leadZ` is the leader's (or formation center's) elevation,
    // `groundZ` the floor found below the spot. INVALID_HEIGHT and NaN never
    // count as a drop: a missed lookup is handled by the headroom and the
    // vertical clamp, not by refusing the spot.
    inline bool FollowSpotGroundRejected(float leadZ, float groundZ)
    {
        if (groundZ != groundZ || leadZ != leadZ)
            return false;
        if (groundZ <= -100000.0f)
            return false;
        return groundZ < leadZ - kFollowSpotMaxGroundDropYd;
    }
}
