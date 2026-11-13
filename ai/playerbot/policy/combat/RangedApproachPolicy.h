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
        ApproachPoint point;
    };

    inline float ApproachDistanceSq(ApproachPoint const& a, ApproachPoint const& b)
    {
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        float const dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // Where a ranged reach stops: a few yards inside the casting distance.
    // The walk aims at where the target stood when it started, and a pulled
    // mob runs toward the group meanwhile, so every extra yard walked is a
    // yard closer to the fight and to the packs behind it.
    inline float RangedApproachStopDistance(float distance)
    {
        if (distance <= 0.0f)
            return 0.0f;
        return distance > 12.0f ? distance - 3.0f : distance * 0.75f;
    }

    // A mob that is already running at someone in the bot's group comes into
    // range by itself; walking to meet it ends next to the tank.
    inline bool RangedReachWaitsForTarget(bool targetMoving, bool targetOnOurGroup)
    {
        return targetMoving && targetOnOurGroup;
    }

    // Whether the bot already stands where the reach wants it: inside the
    // stop band and in line of sight. The caller passes the live range and
    // sight check of the target; nothing here touches the world.
    inline bool RangedApproachInPosition(float distToTarget, float distance, bool inLos)
    {
        if (!inLos)
            return false;
        return distToTarget <= RangedApproachStopDistance(distance);
    }

    // An unpulled hostile the reach walk must respect: centre and aggro
    // radius (yards). The caller builds the list from its cached target
    // list; this header only owns the geometry.
    struct ApproachIdlePack
    {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
    };

    // True when the walk point (x, y) stays clear of the pack: outside its
    // aggro radius, or no closer to it than the bot already stands (a bot
    // inside a radius may still step away from that pack). The same rule
    // as the flee's landing check (PoolFleeLandingWakesMob).
    inline bool ApproachPointClearOfPack(float x, float y, float botX, float botY, ApproachIdlePack const& pack)
    {
        if (pack.radius <= 0.0f)
            return true;
        float const dx = x - pack.x;
        float const dy = y - pack.y;
        float const distSq = dx * dx + dy * dy;
        float const wakeSq = pack.radius * pack.radius;
        if (distSq >= wakeSq)
            return true;
        float const bx = botX - pack.x;
        float const by = botY - pack.y;
        return distSq >= bx * bx + by * by;
    }

    // True when the point is a destination the reach may walk to: within
    // casting distance of the target, in sight of it, and clear of every
    // idle pack. When no listed point qualifies, the caller keeps the
    // current behaviour (the bot must still be able to fight).
    template <typename LosFn>
    inline bool RangedApproachPointSafe(ApproachPoint const& point, ApproachPoint const& target,
        ApproachPoint const& bot, float distance, LosFn hasLos,
        ApproachIdlePack const* packs, std::size_t packCount)
    {
        if (ApproachDistanceSq(point, target) > distance * distance)
            return false;
        if (!hasLos(point))
            return false;
        for (std::size_t i = 0; i < packCount; ++i)
        {
            if (!ApproachPointClearOfPack(point.x, point.y, bot.x, bot.y, packs[i]))
                return false;
        }
        return true;
    }

    // When the planned destination lies in an idle pack's aggro range, step
    // back along the walked path to the first point (nearest the bot) that
    // is in casting range, in sight and clear of every pack. Only points
    // closer to the bot than the planned destination count, so the walk
    // never goes further than planned. Invalid when none qualifies: the
    // caller keeps the planned destination (the bot must still fight).
    template <typename LosFn>
    inline RangedApproachChoice StepBackToPackSafePoint(
        std::vector<ApproachPoint> const& points, ApproachPoint const& choice,
        ApproachPoint const& target, float distance, LosFn hasLos,
        ApproachIdlePack const* packs, std::size_t packCount)
    {
        RangedApproachChoice result;
        if (points.empty() || packCount == 0)
            return result;
        ApproachPoint const& bot = points.front();
        float const choiceSq = ApproachDistanceSq(choice, bot);
        for (auto const& point : points)
        {
            if (ApproachDistanceSq(point, bot) >= choiceSq)
                continue;
            if (!RangedApproachPointSafe(point, target, bot, distance, hasLos, packs, packCount))
                continue;
            result.valid = true;
            result.point = point;
            return result;
        }
        return result;
    }

    // The path walk replaces the core chase only where the chase fails: inside
    // a dungeon (raids included, the core's Map::IsDungeon) and for a distance
    // beyond melee reach (a melee-reach destination passes the core's dungeon
    // check, so melee keeps the chase and its target tracking). Both are cheap,
    // so they come first; the caller then also requires the target in sight
    // (out of sight the reach already walks a path to the target), which costs
    // a vmap ray and is not paid in the open world.
    inline bool UseRangedApproach(bool inDungeon, float distance, float meleeReach)
    {
        return inDungeon && distance > meleeReach;
    }

    // `path` runs from the bot (front) to the target or, for a partial path,
    // to the closest point the navmesh reaches (back). `hasLos(point)` says
    // whether the target is visible from a point. Every point chosen sees the
    // target: the bot only gets here while it sees the target, and a point out
    // of sight would leave it standing hidden (the next tick's out-of-sight
    // path to the target ends where it already stands).
    //
    // - Fewer than two points, a non-positive distance, or a bot that already
    //   stands within `distance`: no choice (nothing for the path to add).
    // - The anchor is the last point of the path, scanning back from the end,
    //   that sees the target and is closer to it than the bot. For a complete
    //   path that is the end itself (the target's own spot); for a partial path
    //   whose end is hidden behind a ledge or wall it is an earlier point. No
    //   such point: no choice, and the caller falls back to the chase.
    // - An anchor out of range is the choice: walking there closes the gap
    //   without losing sight, the next tick takes it from there.
    // - An anchor in range: walk back from it while the previous point is
    //   within range and sees the target. Where the walk leaves range, the point
    //   on that segment exactly `distance` from the target is used if it sees
    //   the target; where it loses sight, the last point that saw it is used.
    // - `startInSight` false (the bot does not see the target): the bot's own
    //   distance does not bound the anchor, a bot in range still gets a point,
    //   and the segment where sight is lost is stepped back from the seeing
    //   end, so a two-point path does not end at the target's feet.
    template <typename LosFn>
    inline RangedApproachChoice SelectRangedApproachPoint(
        std::vector<ApproachPoint> const& path, ApproachPoint const& target, float distance, LosFn hasLos,
        bool startInSight = true)
    {
        RangedApproachChoice result;

        if (path.size() < 2 || distance <= 0.0f)
            return result;

        // `distance` is the stop band the caller wants the bot at (see
        // RangedApproachStopDistance): the walk ends there, not at the
        // range rim, so the bot casts instead of stepping again.
        float const rangeSq = distance * distance;
        float const startSq = ApproachDistanceSq(path.front(), target);

        if (startInSight && startSq <= rangeSq)
            return result;

        std::size_t i = path.size() - 1;
        while (i > 0 && !((!startInSight || ApproachDistanceSq(path[i], target) < startSq) && hasLos(path[i])))
            --i;

        if (i == 0)
            return result; // nothing closer than the bot that still sees the target

        result.valid = true;
        result.point = path[i];

        if (ApproachDistanceSq(path[i], target) > rangeSq)
            return result; // closer and in sight, but short of range

        while (i > 1 && ApproachDistanceSq(path[i - 1], target) <= rangeSq && hasLos(path[i - 1]))
            --i;

        // path[i] is the earliest point past the front of the in-range,
        // in-sight stretch ending at the anchor.
        result.point = path[i];

        ApproachPoint const& outside = path[i - 1];
        if (ApproachDistanceSq(outside, target) <= rangeSq)
        {
            // Lost sight, not range: stop at the last point in sight.
            if (startInSight)
                return result;
            ApproachPoint const& seeing = path[i];
            float const dx = outside.x - seeing.x;
            float const dy = outside.y - seeing.y;
            float const dz = outside.z - seeing.z;
            float const length = std::sqrt(dx * dx + dy * dy + dz * dz);
            for (float walked = 2.0f; walked < length; walked += 2.0f)
            {
                ApproachPoint step;
                step.x = seeing.x + dx * walked / length;
                step.y = seeing.y + dy * walked / length;
                step.z = seeing.z + dz * walked / length;
                if (ApproachDistanceSq(step, target) > rangeSq || !hasLos(step))
                    break;
                result.point = step;
            }
            return result;
        }

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
        float const rawDiscriminant = b * b - 4.0f * a * c;
        if (a <= 0.0f)
            return result;
        // The crossing segment touches the range sphere at a tangent: float
        // error can push the discriminant a hair below zero. Clamp, not bail.
        float const discriminant = rawDiscriminant < 0.0f ? 0.0f : rawDiscriminant;

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
