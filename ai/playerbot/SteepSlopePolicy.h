#pragma once

// Which ground a bot walks: the same as a player. The navmesh is built with a
// 75-degree walkable slope, but a player can only climb about 52 degrees. The
// steeper polygons carry NAV_STEEP_SLOPES on top of NAV_GROUND (TileBuilder),
// so the core's player filter (include NAV_GROUND, exclude nothing) walks
// straight up cliffs. Bots plan through one shared helper (SteepSlopePolicy)
// that excludes the steep flag before the query runs; a bot knocked or fallen
// onto a steep polygon can still step off it (ShouldRetryPermissive).
//
// Tested in tools/test_steep_slope_policy.cpp.

#include <cstdint>

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

    // A strict query finds no start polygon when the bot stands on steep
    // ground, and the core answers exactly that case NOPATH. Anything else
    // never retries, not even a partial path: a longer way is still a way.
    inline bool ShouldRetryPermissive(std::uint32_t strictPathType)
    {
        return strictPathType == kSteepPathNoPath;
    }
}
