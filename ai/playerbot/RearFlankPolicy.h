#pragma once

// Pure decision rules for POS-1 (generic rear-flank for melee).
// Ported donor behaviour (mod-playerbots @ 79bd4281):
//   RearFlankAction::isUseful
//     (src/Ai/Base/Actions/MovementActions.cpp)
// A melee bot standing in the mob's frontal arc or its tail cone should
// sidestep to a flank instead of walking straight through the cleave to
// the exact rear point. Donor shape: front = inside 2x90 degrees
// (mirrored HasInArc), rear = NOT inside (2PI - 120 degrees); either true
// means flank. Offsets: +-frand(90, 120) degrees at meleeRange x 0.5, the
// nearer side wins (callers roll the random angle; this header owns only
// the deterministic geometry).
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_rear_flank_policy.cpp.

#include <cmath>

namespace ai
{
    // Donor cone constants: 90-degree minimum keeps clear of frontal
    // cleaves/breaths (and avoids parry-hasting); 120-degree maximum leaves
    // a 120-degree symmetrical safe cone at the tail.
    constexpr float kFlankMinAngle = 1.5707963268f; // PI/2
    constexpr float kFlankMaxAngle = 2.0943951024f; // 2PI/6

    // Circular difference of two headings in [0, PI].
    inline float FlankHeadingDistance(float a, float b)
    {
        float diff = a > b ? a - b : b - a;
        while (diff > 6.2831853072f)
            diff -= 6.2831853072f;
        if (diff > 3.1415926536f)
            diff = 6.2831853072f - diff;
        return diff;
    }

    // Whether the bot (at botAngle around the target, measured from the
    // target's facing) needs a flank step: inside the frontal 90-degree
    // half-cone (donor: HasInArc(2 x 90)) or outside the rear safe cone
    // (donor: !HasInArc(2PI - 120)). diffFront = |targetFacing - angleToBot|
    // folded to [0, PI]; diffRear = angular distance from the tail axis.
    inline bool NeedsRearFlank(float diffFromFacing, float diffFromTail)
    {
        return diffFromFacing < kFlankMinAngle || diffFromTail > kFlankMaxAngle;
    }

    // Flank destination for one side: polar offset off the target's facing
    // at flankAngle radians, radius baseDist. Returns the 2D point.
    inline void FlankOffset(float targetX, float targetY, float targetFacing,
        float flankAngle, float baseDist, float& outX, float& outY)
    {
        outX = targetX + baseDist * cosf(targetFacing + flankAngle);
        outY = targetY + baseDist * sinf(targetFacing + flankAngle);
    }

    // Squared 2D distance, for the nearer-side pick without a sqrt.
    inline float Dist2Sq(float ax, float ay, float bx, float by)
    {
        float dx = ax - bx;
        float dy = ay - by;
        return dx * dx + dy * dy;
    }
} // namespace ai
