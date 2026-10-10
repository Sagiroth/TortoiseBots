#pragma once

// Pure decision rules for POS-4 (move away from a debuffed groupmate).
// Ported donor behaviour (mod-playerbots @ 79bd4281):
//   MoveAwayFromPlayerWithDebuffAction::Execute
//     (src/Ai/Base/Actions/MovementActions.cpp)
//   + TooCloseToPlayerWithDebuffTrigger::TooCloseToPlayerWithDebuff
//     (src/Ai/Base/Trigger/RangeTriggers.cpp)
// Donor shape: 8 compass directions x 3yd steps out to range+5yd; a
// candidate is safe when every carrier is at least `range` away, and the
// safest (largest minimum separation) wins.
// Adapted: the donor reads the group and moves the bot inline; here the
// trigger/action own the game state (group scan, LoS, path, aggro checks)
// and this header owns only the plain 2D geometry, so the rules stay
// testable in tools/test_debuff_spread_policy.cpp.
// Deliberate deviation: the bot itself is never an anchor (callers skip
// self). The carrier's own escape is RaidBombRunoutAction; counting self
// would force every candidate to also clear the bot's own start position
// and fight the runout for the move.
// No core includes: callers translate game state into plain inputs.

#include <cmath>
#include <cstddef>

namespace ai
{
    // A live groupmate carrying the blast debuff, projected to 2D.
    struct DebuffAnchor
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Search bounds shared by the action and the test: 3yd steps out to
    // 5yd past the blast radius (donor increment/range+5 loop).
    constexpr float kDebuffSpreadStepYd = 3.0f;
    constexpr float kDebuffSpreadOvershootYd = 5.0f;

    // Trigger gate: someone else's bomb is inside the blast radius.
    inline bool NeedsDebuffSpread(float nearestCarrierDist2d, float range)
    {
        return nearestCarrierDist2d < range;
    }

    // Worst-case (minimum) 2D separation from a candidate point to any
    // carrier: the escape score to maximise. -1 when there is nothing to
    // run from (callers return early, never move on it).
    inline float DebuffEscapeClearance(float candX, float candY,
        const DebuffAnchor* anchors, std::size_t count)
    {
        if (!anchors || count == 0)
            return -1.0f;
        float minDist = -1.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            float dx = candX - anchors[i].x;
            float dy = candY - anchors[i].y;
            float d = sqrtf(dx * dx + dy * dy);
            if (minDist < 0.0f || d < minDist)
                minDist = d;
        }
        return minDist;
    }

    // A candidate clears the blast only when EVERY carrier stays outside
    // the radius (donor: dist < range rejects the point).
    inline bool IsDebuffSafePoint(float clearance, float range)
    {
        return clearance >= 0.0f && clearance >= range;
    }
} // namespace ai
