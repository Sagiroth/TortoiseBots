#pragma once

// The dragon flank step's candidate points. A non-tank in a raid dragon's breath or tail cone steps off the facing
// axis to a side; the tail and the front are the cones themselves, so neither is ever a candidate, whatever the line
// of sight or the path check says about it. No core includes: the action owns the LOS, path and move checks, this
// header owns the geometry and the order the points are tried.

#include <cstddef>

namespace ai
{
    // 90 degrees off the dragon's facing axis.
    constexpr float kDragonFlankSideAngle = 1.5707963268f;

    constexpr std::size_t kDragonFlankPointCount = 2;

    struct DragonFlankPoint
    {
        float angle;   // absolute heading from the dragon to the point
        bool nearSide; // the side the bot already stands on
    };

    // The side the bot stands on, from the signed difference in [-PI, PI]. The core keeps orientations in [0, 2PI)
    // (MapManager::NormalizeOrientation) and WorldPosition::getAngleTo returns the same range, so a bare subtraction
    // reads a bot just clockwise of north (e.g. 5.28 rad) as the positive side of a north-facing dragon. No core
    // include: the fold is the same [0, 2PI) parallel, then the arc the core's HasInArc uses.
    inline float DragonFlankSignedDelta(float facing, float angleToBot)
    {
        float delta = angleToBot - facing;
        while (delta > 3.1415926536f)
            delta -= 6.2831853072f;
        while (delta < -3.1415926536f)
            delta += 6.2831853072f;
        return delta;
    }

    // The two side points, the nearer side first; the far side is the other half of the facing axis.
    inline void DragonFlankPoints(float facing, float angleToBot,
        DragonFlankPoint (&points)[kDragonFlankPointCount])
    {
        if (DragonFlankSignedDelta(facing, angleToBot) > 0.0f)
        {
            points[0] = { facing + kDragonFlankSideAngle, true };
            points[1] = { facing - kDragonFlankSideAngle, false };
        }
        else
        {
            points[0] = { facing - kDragonFlankSideAngle, true };
            points[1] = { facing + kDragonFlankSideAngle, false };
        }
    }

    // The first point `accepted` takes, near side first; -1 when neither is, so the step is abandoned instead of
    // walking the bot into the breath or the tail sweep. `accepted` is consulted only for a point actually tried.
    template <typename Accepted>
    inline int PickDragonFlankPoint(const DragonFlankPoint (&points)[kDragonFlankPointCount], Accepted accepted)
    {
        for (std::size_t i = 0; i < kDragonFlankPointCount; ++i)
        {
            if (accepted(points[i]))
                return static_cast<int>(i);
        }
        return -1;
    }
} // namespace ai
