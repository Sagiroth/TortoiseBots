#pragma once

// Raid/dungeon follow wedge (AiPlayerbot.RaidFollowWedge). The configured
// formation spreads the roster all round the leader, including in front:
// GetFollowAngle's ring runs from 22.5 to 337.5 degrees of the facing, and the
// two ends (the front-left and front-right spots) hold the tanks. In a raid or
// dungeon that puts bots in front of the tank and turns every move row that
// passes a pack into a body pull.
//
// With the wedge on, followers stand in a 150 degree arc directly behind the
// leader, in two rings once there are more than ten of them. The angle formula
// and the ring choice live here so tools/
// test_raid_follow_formation_policy.cpp exercises the same numbers the
// formation uses. No core includes.

#include <cmath>

namespace ai
{
    constexpr float kRaidFollowWedgePi = 3.14159265358979323846f;

    // 150 degrees: the arc from 105 to 255 degrees of the leader's facing, so
    // no slot is ever ahead of the leader (or square beside it).
    constexpr float kRaidFollowWedgeSpreadRad = (5.0f / 6.0f) * kRaidFollowWedgePi;

    // A second ring this far behind the first when the group is large: twenty
    // bots on one 4 yd arc would stand shoulder to shoulder.
    constexpr float kRaidFollowSecondRingOffsetYd = 3.0f;

    // Above this many followers the wedge uses the second ring.
    constexpr int kRaidFollowSecondRingAbove = 10;

    // Angle of one wedge slot. `orientation` is the leader's facing, `index`
    // runs 0..count-1 from the left end of the arc to the right end, `count`
    // is the number of followers (not counting the leader). One follower
    // stands straight behind; a single follower must not divide by zero.
    inline float RaidFollowWedgeAngle(float orientation, int index, int count)
    {
        if (count <= 1)
            return orientation + kRaidFollowWedgePi;
        if (index < 0)
            index = 0;
        if (index > count - 1)
            index = count - 1;
        float const t = static_cast<float>(index) / static_cast<float>(count - 1) - 0.5f;
        return orientation + kRaidFollowWedgePi + t * kRaidFollowWedgeSpreadRad;
    }

    // The second ring holds the non-tank, non-healer followers: tanks and
    // healers stay on the inner ring (the ends and the centre), where they
    // can see and reach the leader.
    inline float RaidFollowWedgeRingOffset(int followerCount, bool tank, bool healer)
    {
        if (followerCount <= kRaidFollowSecondRingAbove || tank || healer)
            return 0.0f;
        return kRaidFollowSecondRingOffsetYd;
    }
}
