#pragma once

// Pure decision rules for issue #470 (combat spread + flee memory).
// Ported donor behaviour (mod-playerbots @ b6696bdb):
//   MovementAction::CheckLastFlee (src/Ai/Base/Actions/MovementActions.cpp)
//   + CombatFormationMoveAction::Execute (nearest-group-member step-out)
//   + MoveFromGroupStrategy (move-from-group relevance).
// Adapted: the donor stores a "recently flee info" FleeInfo list value and an
// opt-in "disperse distance" knob; here the last two flee headings live on
// LastMovement (no extra value plumbing) and spread is trigger-gated.
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_combat_spread_policy.cpp.

#include <cstddef>
#include <cstdint>

namespace ai
{
    // Empty-slot marker for LastMovement::lastFleeAngles. 10 rad exceeds any
    // real heading (valid range -PI..PI), mirroring the donor's -1 default
    // for "disperse distance" (disabled).
    constexpr float kFleeAngleEmpty = 10.0f;

    // Donor veto half-width (CheckLastFlee: fabs(revAngle - curAngle) < PI/4):
    // a candidate heading within 45 degrees of a remembered flee destination
    // is skipped so repeated flees/spreads fan out.
    constexpr float kFleeAngleVetoHalfWidth = 0.7853981634f; // PI/4

    // How many flee headings are remembered. Two matches the donor's
    // effective window (list capped at 10 but pruned to ~5 s of entries).
    constexpr std::size_t kFleeAngleSlots = 2;

    // Circular heading distance in [0, PI]: wraps the raw difference into a
    // full turn first, then mirrors past PI.
    inline float FleeHeadingDistance(float a, float b)
    {
        float diff = a > b ? a - b : b - a;
        while (diff > 6.2831853072f)
            diff -= 6.2831853072f;
        if (diff > 3.1415926536f)
            diff = 6.2831853072f - diff;
        return diff;
    }

    // True when candidate heading is free to use: no remembered slot within
    // the veto half-width. Empty slots (kFleeAngleEmpty) never veto.
    inline bool IsFleeHeadingFree(float candidate, float const* past, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (past[i] > 3.1415926536f)
                continue;
            if (FleeHeadingDistance(candidate, past[i]) < kFleeAngleVetoHalfWidth)
                return false;
        }
        return true;
    }

    // Combat-spread gate for the "raid spread needed" trigger: spread applies
    // only while fighting, never for owned/hired bots answering to a real
    // player master (their player decides positioning), and never against an
    // explicit hold order (stay/follow/wait-for-attack/grind anchors).
    inline bool ShouldCombatSpread(bool inCombat, bool hasRealPlayerMaster,
        bool stayOrdered, bool followOrdered, bool waitOrdered, bool grindOrdered)
    {
        if (!inCombat)
            return false;
        if (hasRealPlayerMaster)
            return false;
        return !stayOrdered && !followOrdered && !waitOrdered && !grindOrdered;
    }
} // namespace ai
