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

    // Spread re-step floor in milliseconds: one spread step-out per window
    // per bot. Without it stacked ranged bots ping-pong every tick (each
    // step toward open ground lands next to another friendly and re-fires).
    // 3 s mirrors the flee return-delay scale without touching it.
    constexpr std::uint32_t kSpreadStepCooldownMs = 3000;

    // "Good enough" spacing: below this the trigger may fire, at/above it a
    // fresh trigger check treats the bot as spread and holds position.
    // Matches the RaidSpreadNeeded/Action 10 yd stack radius.
    constexpr float kSpreadSettledDistance = 10.0f;

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

    // Owned-bot answer for Execute-time gates: module-owned (hired/owned,
    // IsOwnedBot) counts as owned even when the master is offline or on
    // another character, where HasRealPlayerMaster goes false. Pool bots
    // (IsRandomBot, no owner record) stay eligible.
    inline bool IsSpreadExemptOwned(bool hasRealPlayerMaster, bool isOwnedBot)
    {
        return hasRealPlayerMaster || isOwnedBot;
    }

    // Spread re-step throttle: a step-out within the cooldown of the last
    // recorded flee/spread dispatch holds position instead of stepping again.
    // Wrap-safe via unsigned subtraction; 0 (no dispatch yet) never throttles.
    inline bool IsSpreadOnCooldown(std::uint32_t nowMs, std::uint32_t lastStepMs,
        std::uint32_t cooldownMs = kSpreadStepCooldownMs)
    {
        if (lastStepMs == 0)
            return false;
        return (nowMs - lastStepMs) < cooldownMs;
    }
} // namespace ai
