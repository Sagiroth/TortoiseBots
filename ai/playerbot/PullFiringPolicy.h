#pragma once
#include <algorithm>
#include <cmath>

// Pure policy for the approach of an explicit ranged pull (issue #389).
//
// A ranged pull has to walk the puller to a firing position at shooting range
// before the shot. The mature movement for that was the core chase, but inside
// a dungeon the core's chase generator refuses any destination that is not
// melee-reachable from the target (TargetedMovementGenerator) and drops the
// move while the action still reports success - the tank stood at the pull
// spot, "temporarily unreachable". A ranged pull now uses a point move to the
// firing position instead; the position itself is pure geometry, so it is
// decided here and tested on its own.

namespace ai
{
    struct PullFiringPosition
    {
        bool valid = false;
        float x = 0.0f;
        float y = 0.0f;
    };

    // The point `firingDistance` yards from the target, on the side the puller
    // already stands: the walk is straight at the mob and never crosses it. The
    // distance is clamped to the puller's own distance, so the point can never
    // land behind the puller and walk the tank backwards away from the mob.
    // Degenerate inputs - the puller on top of the target, or a non-positive
    // distance - have no firing position.
    inline PullFiringPosition ComputePullFiringPosition(
        float pullerX, float pullerY, float targetX, float targetY, float firingDistance)
    {
        PullFiringPosition result;

        const float dx = pullerX - targetX;
        const float dy = pullerY - targetY;
        const float distance = std::sqrt(dx * dx + dy * dy);

        if (firingDistance <= 0.0f || distance <= 0.0f)
            return result;

        const float effectiveDistance = std::min(firingDistance, distance);
        const float scale = effectiveDistance / distance;
        result.x = targetX + dx * scale;
        result.y = targetY + dy * scale;
        result.valid = true;
        return result;
    }
}
