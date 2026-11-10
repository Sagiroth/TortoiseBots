#pragma once

// Which ground a bot walks: the same as a player. The navmesh is built with a
// 75-degree walkable slope, but a player can only climb about 52 degrees. The
// steeper polygons carry NAV_STEEP_SLOPES on top of NAV_GROUND (TileBuilder),
// so the core's player filter (include NAV_GROUND, exclude nothing) walks
// straight up cliffs. Bots plan strict first (steep out), then permissive once
// when strict does not reach, and walk the permissive path only when a player
// could climb it (PermissivePathWalkable below). The flag over-marks stairs
// and ramps a player walks (Stormwind harbor, Orgrimmar leatherworkers), so a
// strict partial path alone would strand bots where a player reaches.
//
// Tested in tools/test_steep_slope_policy.cpp.

#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{
    // MoveMapSharedDefines.h NavTerrain flags, named here so the rule reads
    // without a core include; pinned by static_asserts in WorldPosition.cpp.
    constexpr std::uint16_t kNavGround = 0x01;
    constexpr std::uint16_t kNavMagma = 0x02;
    constexpr std::uint16_t kNavSlime = 0x04;
    constexpr std::uint16_t kNavWater = 0x08;
    constexpr std::uint16_t kNavSteepSlopes = 0x10;

    // The core's PathType bits (PathFinder.h), mirrored for the same reason.
    constexpr std::uint32_t kSteepPathNoPath = 0x0008;
    constexpr std::uint32_t kSteepPathIncomplete = 0x0004;

    // The retry runs only when strict did not reach: no path at all, or a
    // partial one. A complete strict path is used as is.
    inline bool ShouldRetryPermissive(std::uint32_t strictPathType)
    {
        return strictPathType == kSteepPathNoPath || (strictPathType & kSteepPathIncomplete) != 0;
    }

    // The steepest a player climbs (~52 deg); the grader refuses past ~50 deg
    // (tan 50 ~ 1.19), a margin under the limit.
    constexpr float kSteepGradeRisePerRun = 1.19f;
    // Legs are graded in windows covering this much ground, so one tiny stair
    // riser does not trip the rule.
    constexpr float kSteepGradeWindowRunYd = 3.0f;
    // A smaller total rise is a step, not a climb, whatever its grade.
    constexpr float kSteepGradeRiseMinYd = 1.5f;

    struct SteepGradePoint
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // Whether a player could walk `line` (the path's own points, first to last):
    // no uphill window climbs steeper than the player limit. Only the rise
    // counts (walking or dropping down a steep face is fine), summed over each
    // window of consecutive legs covering kSteepGradeWindowRunYd. Stairs at
    // 30-40 deg pass, a 60-75 deg cliff face fails.
    inline bool PermissivePathWalkable(std::vector<SteepGradePoint> const& line)
    {
        for (std::size_t i = 1; i < line.size(); ++i)
        {
            float rise = 0.0f, run = 0.0f;
            for (std::size_t j = i; j < line.size(); ++j)
            {
                float const dz = line[j].z - line[j - 1].z;
                if (dz > 0.0f)
                    rise += dz;
                float const dx = line[j].x - line[j - 1].x;
                float const dy = line[j].y - line[j - 1].y;
                run += std::sqrt(dx * dx + dy * dy);
                if (run >= kSteepGradeWindowRunYd)
                    break;
            }
            if (rise > kSteepGradeRiseMinYd && rise > kSteepGradeRisePerRun * run)
                return false;
        }
        return true;
    }
}
