#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

// Pure policy for a ranged reach inside a dungeon.
//
// A healer or caster that sees its target but stands out of range used to
// hand the approach to the core chase. Inside a dungeon the core's chase
// generator refuses any destination that is not melee-reachable from the
// target (TargetedMovementGenerator, "Enforce stricter checking inside
// dungeons") and stops while the target is in sight, so the bot stood still
// out of range for the whole fight. The explicit ranged pull hit the same rule
// and got its own point move (issue #389, PullFiringPolicy.h).
//
// The reach now walks the navmesh path to the target instead (mod-playerbots
// ReachCombatTo + PathGenerator::ShortenPathUntilDist): from the target end of
// the path back towards the bot, as long as the path stays within range and in
// line of sight of the target, and moves to that point. The point selection
// is pure geometry plus a line-of-sight callback, so it is decided here and
// tested on its own.

namespace ai
{
    struct ApproachPoint
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct RangedApproachChoice
    {
        bool valid = false;
        // The point is within the requested distance of the target. False only
        // for a partial path that ends short of range: walking to its end still
        // closes the gap, the next tick takes it from there.
        bool inRange = false;
        ApproachPoint point;
    };

    inline float ApproachDistanceSq(ApproachPoint const& a, ApproachPoint const& b)
    {
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        float const dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // The path walk replaces the core chase only where the chase fails: inside
    // a dungeon (raids included, the core's Map::IsDungeon), with the target in
    // sight (out of sight the reach already walks a path to the target), and
    // for a distance beyond melee reach (a melee-reach destination passes the
    // core's dungeon check, so melee keeps the chase and its target tracking).
    inline bool UseRangedApproach(bool inDungeon, bool inLos, float distance, float meleeReach)
    {
        return inDungeon && inLos && distance > meleeReach;
    }

    // `path` runs from the bot (front) to the target or, for a partial path,
    // to the closest point the navmesh reaches (back). `hasLos(point)` says
    // whether the target is visible from a point.
    //
    // - Fewer than two points, a non-positive distance, or a bot that already
    //   stands within `distance`: no choice (nothing for the path to add).
    // - A path that ends out of range: its end, when that end is closer to the
    //   target than the bot is; otherwise no choice.
    // - Otherwise: walk back from the end while the previous point is within
    //   range and sees the target. Where the walk leaves range, the point on
    //   that segment exactly `distance` from the target is used if it sees the
    //   target; where it loses sight, the last point that saw it is used.
    template <typename LosFn>
    inline RangedApproachChoice SelectRangedApproachPoint(
        std::vector<ApproachPoint> const& path, ApproachPoint const& target, float distance, LosFn hasLos)
    {
        RangedApproachChoice result;

        if (path.size() < 2 || distance <= 0.0f)
            return result;

        float const rangeSq = distance * distance;
        ApproachPoint const& start = path.front();
        ApproachPoint const& end = path.back();
        float const startSq = ApproachDistanceSq(start, target);

        if (startSq <= rangeSq)
            return result;

        float const endSq = ApproachDistanceSq(end, target);
        if (endSq > rangeSq)
        {
            if (endSq < startSq)
            {
                result.valid = true;
                result.point = end;
            }
            return result;
        }

        result.valid = true;
        result.inRange = true;

        std::size_t i = path.size() - 1;
        while (i > 0 && ApproachDistanceSq(path[i - 1], target) <= rangeSq && hasLos(path[i - 1]))
            --i;

        // i > 0 always: the front is out of range. path[i] is the earliest point
        // of the in-range, in-sight stretch at the end of the path.
        result.point = path[i];

        ApproachPoint const& outside = path[i - 1];
        if (ApproachDistanceSq(outside, target) <= rangeSq)
            return result; // lost sight, not range: stop at the last point in sight

        // The point on outside -> path[i] at exactly `distance` from the target:
        // |outside + t * (inside - outside) - target|^2 = distance^2, the smaller
        // root in (0, 1] (outside is beyond range, inside is within it).
        ApproachPoint const& inside = path[i];
        float const sx = inside.x - outside.x;
        float const sy = inside.y - outside.y;
        float const sz = inside.z - outside.z;
        float const ox = outside.x - target.x;
        float const oy = outside.y - target.y;
        float const oz = outside.z - target.z;
        float const a = sx * sx + sy * sy + sz * sz;
        float const b = 2.0f * (ox * sx + oy * sy + oz * sz);
        float const c = ox * ox + oy * oy + oz * oz - rangeSq;
        float const discriminant = b * b - 4.0f * a * c;
        if (a <= 0.0f || discriminant < 0.0f)
            return result;

        float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;

        ApproachPoint edge;
        edge.x = outside.x + t * sx;
        edge.y = outside.y + t * sy;
        edge.z = outside.z + t * sz;
        if (hasLos(edge))
            result.point = edge;

        return result;
    }
}
