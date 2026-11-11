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

    // The two side points, the nearer side first; the far side is the other half of the facing axis.
    inline void DragonFlankPoints(float facing, float angleToBot,
        DragonFlankPoint (&points)[kDragonFlankPointCount])
    {
        if (angleToBot - facing > 0.0f)
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
