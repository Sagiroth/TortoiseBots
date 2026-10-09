#pragma once

// Pure decision rules for issue #470 (combat spread + flee memory).
// Ported donor behaviour (mod-playerbots @ b6696bdb):
//   MovementAction::CheckLastFlee (src/Ai/Base/Actions/MovementActions.cpp)
//   + CombatFormationMoveAction::Execute (nearest-group-member step-out)
//   + MoveFromGroupStrategy (move-from-group relevance).
// Adapted: the donor stores a "recently flee info" FleeInfo list value and an
// opt-in "disperse distance" knob; here the last two flee headings live on
// LastMovement (no extra value plumbing) and spread is trigger-gated.
// Issue #486 replaces dispatch history with observed failures only.
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_combat_spread_policy.cpp.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ai
{
    // Empty-slot marker for failure memory. 10 rad exceeds any
    // real heading (valid range -PI..PI), mirroring the donor's -1 default
    // for "disperse distance" (disabled).
    constexpr float kFleeAngleEmpty = 10.0f;

    // Donor veto half-width (CheckLastFlee: fabs(revAngle - curAngle) < PI/4):
    // a candidate heading within 45 degrees of a failed destination is
    // deprioritized. A successful escape remains repeatable.
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

    // A dispatch is not an outcome. Observe separation from the same anchor
    // for up to 5 s; 2 yd gained clears the attempt, while no gain after 3 s
    // remembers its heading for 5 s. Flee and spread use separate instances.
    class FleeFailureMemory
    {
    public:
        static constexpr std::uint32_t kObservationMs = 3000;
        static constexpr std::uint32_t kMaxObservationMs = 5000;
        static constexpr std::uint32_t kFailureTtlMs = 5000;
        static constexpr float kSeparationGain = 2.0f;

        void Clear() { *this = FleeFailureMemory(); }
        std::uint64_t Anchor() const { return anchor; }
        bool IsPending() const { return pending; }

        void Observe(std::uint64_t newAnchor, std::uint32_t newMap,
            float distance, std::uint32_t nowMs, std::uint32_t currentSpline = 0)
        {
            if (anchor != newAnchor || map != newMap)
            {
                Clear();
                anchor = newAnchor;
                map = newMap;
            }
            if (!pending)
                return;
            if (spline != currentSpline)
            {
                pending = false; // Interrupted/replaced movement has no attributable outcome.
                return;
            }
            std::uint32_t const elapsed = nowMs - startedMs;
            if (!std::isfinite(distance) || elapsed > kMaxObservationMs)
            {
                pending = false; // No timely evidence: do not invent a failure.
                return;
            }
            if (distance >= startDistance + kSeparationGain)
            {
                // A fallback may have succeeded in a previously failed heading.
                for (auto& failure : failures)
                    if (FleeHeadingDistance(heading, failure.angle) < kFleeAngleVetoHalfWidth)
                        failure = Failure();
                pending = false;
            }
            else if (elapsed >= kObservationMs)
            {
                RecordFailure(heading, nowMs);
                pending = false;
            }
        }

        void BeginAttempt(std::uint64_t newAnchor, std::uint32_t newMap,
            float newHeading, float distance, std::uint32_t nowMs, std::uint32_t newSpline = 0)
        {
            Observe(newAnchor, newMap, distance, nowMs, spline);
            if (!newAnchor || !std::isfinite(newHeading) || !std::isfinite(distance))
                return;
            newHeading = std::remainder(newHeading, 6.2831853072f);
            // Frequent re-dispatches of the same vector must not restart the
            // observation clock and hide a bot that is making no progress.
            if (pending && FleeHeadingDistance(heading, newHeading) < kFleeAngleVetoHalfWidth)
            {
                spline = newSpline;
                return;
            }
            spline = newSpline;
            pending = true;
            heading = newHeading;
            startDistance = distance;
            startedMs = nowMs;
        }

    private:
        void RecordFailure(float failedHeading, std::uint32_t nowMs)
        {
            if (!anchor || !std::isfinite(failedHeading))
                return;
            failedHeading = std::remainder(failedHeading, 6.2831853072f);
            if (FleeHeadingDistance(failedHeading, failures[0].angle) >= kFleeAngleVetoHalfWidth)
                failures[1] = failures[0];
            failures[0] = { failedHeading, nowMs };
        }

    public:
        bool IsHeadingFree(float candidate, std::uint32_t nowMs) const
        {
            for (auto const& failure : failures)
                if (failure.angle != kFleeAngleEmpty &&
                    (nowMs - failure.atMs) < kFailureTtlMs &&
                    FleeHeadingDistance(candidate, failure.angle) < kFleeAngleVetoHalfWidth)
                    return false;
            return true;
        }

    private:
        struct Failure
        {
            float angle = kFleeAngleEmpty;
            std::uint32_t atMs = 0;
        };
        Failure failures[kFleeAngleSlots];
        std::uint64_t anchor = 0;
        std::uint32_t map = 0;
        bool pending = false;
        float heading = 0.0f;
        float startDistance = 0.0f;
        std::uint32_t startedMs = 0;
        std::uint32_t spline = 0;
    };

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

    // Opt-in spread gate ("spread" strategy): the player asked for spacing,
    // so owned/hired bots and melee are eligible too. Still combat-only and
    // never against an explicit hold order (stay/follow/guard/
    // wait-for-attack/grind anchors) — explicit orders beat automation
    // even when opted in. Note: follow/grind only exist on NON_COMBAT, so
    // those two params are near-dead checks kept for symmetry with the
    // legacy gate; stay/guard/wait-for-attack do the real work in combat.
    inline bool ShouldOptInSpread(bool inCombat,
        bool stayOrdered, bool followOrdered, bool guardOrdered, bool waitOrdered, bool grindOrdered)
    {
        if (!inCombat)
            return false;
        return !stayOrdered && !followOrdered && !guardOrdered && !waitOrdered && !grindOrdered;
    }

    // Donor "disperse distance" defaults (DisperseSetAction enable/reset):
    // 5yd for ranged, 2yd for melee. A set "spread distance" value (> 0)
    // overrides both; unset (-1) keeps the role default.
    constexpr float kSpreadDistanceRangedYd = 5.0f;
    constexpr float kSpreadDistanceMeleeYd = 2.0f;

    // Effective "too close" radius: the manual knob when set, else the role
    // default. Non-positive manual values mean unset (donor: dis <= 0 off).
    inline float SpreadRadius(float manualDistance, bool isRanged)
    {
        if (manualDistance > 0.0f)
            return manualDistance;
        return isRanged ? kSpreadDistanceRangedYd : kSpreadDistanceMeleeYd;
    }
} // namespace ai
