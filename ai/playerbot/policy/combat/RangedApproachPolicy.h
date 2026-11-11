#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    template <typename LosFn>
    inline RangedApproachChoice SelectRangedApproachPoint(
        std::vector<ApproachPoint> const& path, ApproachPoint const& target, float distance, LosFn hasLos)
    {
        RangedApproachChoice result;

        if (path.size() < 2 || distance <= 0.0f)
            return result;

        float const rangeSq = distance * distance;
        float const startSq = ApproachDistanceSq(path.front(), target);

        if (startSq <= rangeSq)
            return result;

        std::size_t i = path.size() - 1;
        while (i > 0 && !(ApproachDistanceSq(path[i], target) < startSq && hasLos(path[i])))
            --i;

        if (i == 0)
            return result; // nothing closer than the bot that still sees the target

        result.valid = true;
        result.point = path[i];

        if (ApproachDistanceSq(path[i], target) > rangeSq)
            return result; // closer and in sight, but short of range

        while (i > 0 && ApproachDistanceSq(path[i - 1], target) <= rangeSq && hasLos(path[i - 1]))
            --i;

        // i > 0 always: the front is out of range. path[i] is the earliest point
        // of the in-range, in-sight stretch ending at the anchor.
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

    // Only a hostile body is a hover reach target: "enemy out of spell range" judges with IsHostileTo, and
    // !IsFriendlyTo also takes neutral units. An attack reach (melee, pull) also needs a target the core lets
    // the bot attack; a spell reach may close on a hostile it cannot hit yet (a boss still unselectable in the
    // air), which is why the walk runs before the plain path's gates.
    inline bool HoverReachTargetEligible(bool hostile, bool attackReach, bool attackable)
    {
        return hostile && (!attackReach || attackable);
    }

    // AiPlayerbot.RangedReachHoverTarget: the walk under the boss is a ranged DPS position. The reach path is
    // shared with melee, CC and pull reaches, so a tank or a melee damage dealer that reached it could be
    // walked toward the boss and pulled off its post; healers keep today's path walk as well ("ranged damage
    // dealers only; tanks, melee and healers are unchanged"). The roles are read at the call site, only once
    // the walk would otherwise run.
    inline bool HoverReachTakerIsRangedDps(bool tank, bool healer, bool ranged)
    {
        return !tank && !healer && ranged;
    }

    // AiPlayerbot.RangedReachHoverTarget: a hostile at least this high above the floor under it is "hovering"
    // (Onyxia's P2 hover points stand 19-23 yd above the floor; a jumping or stair-stepping mob is never 8 yd up).
    constexpr float kHoverTargetMinHeightYd = 8.0f;

    inline bool HoverReachApplies(bool keyOn, bool inDungeon, bool hostile, float distance, float meleeReach,
                                  bool groundValid, float targetZ, float groundZ)
    {
        return keyOn && inDungeon && hostile && distance > meleeReach && groundValid &&
               targetZ - groundZ >= kHoverTargetMinHeightYd;
    }
    // AiPlayerbot.HoverCastHoldsSpread: a ranged damage dealer whose target hovers (HoverReachApplies) and stands in its
    // spell range by the trigger's measure takes no spacing step: a cast-time nuke is refused while the bot moves
    // (CastStartBlockedByMove), and walk 2d's casters on airborne Onyxia moved > 2 yd in 2 s in 48 % of their refused
    // frames. A live `spread` row (the boss's data asks for spacing) still steps. A member inside the spread radius
    // outranks the hold: a caster that stacked after a Deep Breath dodge steps out of the Fireball splash first.
    inline bool HoverCastHoldsSpread(bool keyOn, bool rangedDps, bool targetHovering, bool inSpellRange,
                                     bool spreadRowLive, float nearestAllyYd, float spreadRadius)
    {
        return keyOn && rangedDps && targetHovering && inSpellRange && !spreadRowLive && nearestAllyYd >= spreadRadius;
    }

    // The trigger ("enemy out of spell range") measures edge to edge with both combat reaches; the path walk measures
    // centre to centre. A hovering boss's reach (Onyxia 22 yd in the air) makes the centre distance the trigger allows
    // that much longer.
    inline float HoverReachDistance(float chaseDist, float targetReach, float botReach)
    {
        return chaseDist + targetReach + botReach;
    }

    // The point on the path to the floor under the target where the centre distance reaches HoverReachDistance, in
    // sight. `sightRequired` false is the second pass: geometry only (see the glue).
    template <typename LosFn>
    inline RangedApproachChoice SelectHoverApproachPoint(std::vector<ApproachPoint> const& pathToGround,
        ApproachPoint const& target, float chaseDist, float targetReach, float botReach, bool sightRequired, LosFn hasLos)
    {
        float const distance = HoverReachDistance(chaseDist, targetReach, botReach);
        if (sightRequired)
            return SelectRangedApproachPoint(pathToGround, target, distance, hasLos);
        return SelectRangedApproachPoint(pathToGround, target, distance, [](ApproachPoint const&) { return true; });
    }

    enum class HoverReachResult : std::uint8_t { NotHover, NoGround, NoPath, NoPoint, Hazard, MoveFailed, Moved, MovedNoLos };
    inline const char* HoverReachWord(HoverReachResult r)
    {
        switch (r)
        {
            case HoverReachResult::NotHover:   return "not_hover";
            case HoverReachResult::NoGround:   return "no_ground";
            case HoverReachResult::NoPath:     return "no_path";
            case HoverReachResult::NoPoint:    return "no_point";
            case HoverReachResult::Hazard:     return "hazard";
            case HoverReachResult::MoveFailed: return "move_failed";
            case HoverReachResult::Moved:      return "moved";
            case HoverReachResult::MovedNoLos: return "moved_nolos";
        }
        return "not_hover";
    }
}
