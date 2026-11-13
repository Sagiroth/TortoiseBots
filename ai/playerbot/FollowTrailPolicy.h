#pragma once

// Lead-trail catch-up (AiPlayerbot.RaidFollowWedge). A follower more than 20 yd
// behind the leader, out of combat, walks the leader's own recent path instead
// of the navmesh route to its formation slot: on a stair or around a corner the
// direct route cuts the inside of the turn and crosses whatever the route was
// avoiding. Each follower records the leader's positions it sees (no shared
// state) and picks a waypoint a short step further along that trail.
//
// The choice is pure so tools/test_raid_follow_formation_policy.cpp can prove a
// far follower heads along the trail instead of straight at the leader. No core
// includes.

#include <cmath>
#include <cstddef>
#include <vector>

namespace ai
{
    // Distance to the leader above which the trail replaces the formation
    // spot. Matches the M3 catch-up threshold.
    constexpr float kRaidFollowCatchUpDistanceYd = 20.0f;

    // The leader must move this far before another trail point is recorded:
    // denser points cost nothing but make the path no more accurate.
    constexpr float kFollowTrailSampleYd = 2.0f;

    // A trail longer than this (96 points, about 190 yd) is more than a catch-up
    // needs; dropping the oldest points bounds the memory per bot.
    constexpr std::size_t kFollowTrailMaxPoints = 96;

    // A jump this far from the last recorded point is a teleport (summon, a
    // revive return), not a walk: start the trail over instead of drawing a
    // straight line across the map.
    constexpr float kFollowTrailResetYd = 60.0f;

    // The follower walks toward the first trail point at least this far along
    // the recorded path from the point nearest to it: a short step keeps it on
    // the leader's line through a corner, where aiming at the leader would cut
    // the turn.
    constexpr float kFollowTrailStepYd = 6.0f;

    struct FollowTrailPoint
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    inline float FollowTrailDistance2d(FollowTrailPoint const& a, float x, float y)
    {
        float const dx = a.x - x;
        float const dy = a.y - y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Index of the trail point the follower should walk to, or -1 for an empty
    // trail. The nearest point (oldest on a tie) is the follower's place on the
    // path; from there the function walks forward along the stored segments
    // until it has covered `stepYd`, so the waypoint is always on the recorded
    // path and close enough that the straight walk to it cannot cut a corner.
    inline int FollowTrailWaypoint(std::vector<FollowTrailPoint> const& trail, float botX, float botY,
        float stepYd = kFollowTrailStepYd)
    {
        if (trail.empty())
            return -1;
        if (!(stepYd > 0.0f))
            stepYd = kFollowTrailStepYd;

        std::size_t nearest = 0;
        float best = FollowTrailDistance2d(trail[0], botX, botY);
        for (std::size_t i = 1; i < trail.size(); ++i)
        {
            float const distance = FollowTrailDistance2d(trail[i], botX, botY);
            if (distance < best)
            {
                best = distance;
                nearest = i;
            }
        }

        float along = 0.0f;
        for (std::size_t i = nearest + 1; i < trail.size(); ++i)
        {
            along += FollowTrailDistance2d(trail[i - 1], trail[i].x, trail[i].y);
            if (along >= stepYd)
                return static_cast<int>(i);
        }

        return static_cast<int>(trail.size() - 1);
    }

    // Back-off candidates: the trail point `backYd` behind the bot's place on
    // the path (the trail's start when the path is shorter), or -1 when nothing
    // lies behind. Points between the result and the bot's place are shorter
    // steps back along the same path.
    inline int FollowTrailBackOffIndex(std::vector<FollowTrailPoint> const& trail, float botX, float botY,
        float backYd, int* nearestOut = nullptr)
    {
        if (trail.empty())
            return -1;

        std::size_t nearest = 0;
        float best = FollowTrailDistance2d(trail[0], botX, botY);
        for (std::size_t i = 1; i < trail.size(); ++i)
        {
            float const distance = FollowTrailDistance2d(trail[i], botX, botY);
            if (distance <= best)
            {
                best = distance;
                nearest = i;
            }
        }
        if (nearestOut)
            *nearestOut = static_cast<int>(nearest);
        if (nearest == 0)
            return -1;

        float along = 0.0f;
        for (std::size_t i = nearest; i > 0; --i)
        {
            along += FollowTrailDistance2d(trail[i], trail[i - 1].x, trail[i - 1].y);
            if (along >= backYd)
                return static_cast<int>(i - 1);
        }
        return 0;
    }

    // Record the leader's current position. Returns true when the trail changed
    // (a point was added or the trail was reset). The caller owns the leader's
    // game state and passes its position.
    inline bool FollowTrailRecord(std::vector<FollowTrailPoint>& trail, float x, float y, float z)
    {
        FollowTrailPoint const point{ x, y, z };
        if (trail.empty())
        {
            trail.push_back(point);
            return true;
        }
        float const moved = FollowTrailDistance2d(trail.back(), x, y);
        if (moved > kFollowTrailResetYd)
        {
            trail.clear();
            trail.push_back(point);
            return true;
        }
        if (moved < kFollowTrailSampleYd)
            return false;
        trail.push_back(point);
        if (trail.size() > kFollowTrailMaxPoints)
            trail.erase(trail.begin(), trail.begin() + (trail.size() - kFollowTrailMaxPoints));
        return true;
    }
}
