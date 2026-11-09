#pragma once
#include <cmath>
#include <cstdint>

// Pure policy: which core PathFinder answers a land walk may launch.
// getPathStepFrom already refuses NOPATH, shortcuts and not-using-path on
// land for the module's own routes (SwimPathPolicy.h); the bare MovePoint and
// raw spline sites below used to hand the same answers to the core, which
// walks them as a straight line through walls. The helper asks the same
// question: a walk launches only on a real navmesh path to the point.
// INCOMPLETE walks only when the partial path still closes most of the gap:
// the end is a real step toward the target, not a sidestep to a wall.
// INCOMPLETE already applies the steep filter, so a partial path that ends
// early on a cliff edge is refused by the same rule, not re-measured here.
namespace ai
{
    // The core's PathType bits (src/game/Maps/PathFinder.h), mirrored; the
    // SwimPathPolicy.h values pin the same bits.
    constexpr std::uint32_t kLandPathNormal = 0x0001;
    constexpr std::uint32_t kLandPathShortcut = 0x0002;
    constexpr std::uint32_t kLandPathIncomplete = 0x0004;
    constexpr std::uint32_t kLandPathNoPath = 0x0008;
    constexpr std::uint32_t kLandPathNotUsingPath = 0x0010;

    // An INCOMPLETE path counts as progress toward (tx, ty, tz) from (fx, fy, fz)
    // when the actual end it reached is this much closer in 3D than the start
    // was: the walk ends meaningfully nearer the target. A partial path that
    // ends at the same distance (along a wall) or behind the start is not one.
    constexpr float kLandIncompleteProgressYd = 2.0f;

    inline bool LandPathProgresses(float fromX, float fromY, float fromZ,
        float actualEndX, float actualEndY, float actualEndZ, float toX, float toY, float toZ)
    {
        float const fromDx = toX - fromX;
        float const fromDy = toY - fromY;
        float const fromDz = toZ - fromZ;
        float const endDx = toX - actualEndX;
        float const endDy = toY - actualEndY;
        float const endDz = toZ - actualEndZ;
        float const fromDist = std::sqrt(fromDx * fromDx + fromDy * fromDy + fromDz * fromDz);
        float const endDist = std::sqrt(endDx * endDx + endDy * endDy + endDz * endDz);
        return fromDist - endDist >= kLandIncompleteProgressYd;
    }

    enum class LandWalkVerdict
    {
        Walk,   // a real navmesh path (NORMAL, or INCOMPLETE with progress)
        Refuse, // a shortcut, NOPATH / not-using-path, or an INCOMPLETE going nowhere
    };

    // pathType: the core PathFinder's getPathType for bot -> point, asked with
    // ExcludeSteepSlopes. The INCOMPLETE end is the path's actual end position.
    inline LandWalkVerdict ClassifyLandPath(std::uint32_t pathType,
        float fromX, float fromY, float fromZ,
        float actualEndX, float actualEndY, float actualEndZ,
        float toX, float toY, float toZ)
    {
        if (pathType & (kLandPathNoPath | kLandPathNotUsingPath | kLandPathShortcut))
            return LandWalkVerdict::Refuse;
        if (pathType & kLandPathIncomplete)
            return LandPathProgresses(fromX, fromY, fromZ, actualEndX, actualEndY, actualEndZ, toX, toY, toZ)
                ? LandWalkVerdict::Walk
                : LandWalkVerdict::Refuse;
        return pathType & kLandPathNormal ? LandWalkVerdict::Walk : LandWalkVerdict::Refuse;
    }
    // A hazard rewrite (GeneratePathAvoidingHazards) moves a route point
    // sideways or inserts a new one; the leg into it must be a real path, or
    // the rewrite is dropped and the original route is kept. The leg query
    // answered a path: usable when its end is on the moved point's map within
    // kHazardLegReachYd on the ground plane (the walk below starts at the leg
    // start, so a small end gap still walks the right leg).
    constexpr float kHazardLegReachYd = 2.0f;

    inline bool HazardLegReaches(bool sameMap, float endSqDist2d, float endDz)
    {
        return sameMap && endSqDist2d < kHazardLegReachYd * kHazardLegReachYd &&
            (endDz < 2.0f && endDz > -2.0f);
    }
}
