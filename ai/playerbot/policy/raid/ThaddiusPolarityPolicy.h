#pragma once

#include <cstdint>

// Pure decision rules for the Thaddius fight (mod-playerbots parity:
// NaxxActions_Thaddius.cpp + ThaddiusGenericMultiplier + ThaddiusBossHelper
// phase guards). No core includes: testable in
// tools/test_thaddius_polarity_policy.cpp. IDs verified in tw_world:
// Stalagg 15929, Feugen 15930, Thaddius 15928; charges 28059/62 (+)
// 28084/85 (-) 29659/60 (stacks); Polarity Shift 28089.

namespace ai
{
// Pet phase: an add counts as active while alive AND selectable. The core
// fake-deaths adds at 0 HP with UNIT_FLAG_NOT_SELECTABLE (boss_thaddius.cpp
// DamageTaken), so IsAlive alone would latch the pet phase forever.
inline bool IsThaddiusPetActive(bool alive, bool notSelectable)
{
    return alive && !notSelectable;
}

inline bool IsThaddiusPhasePet(bool feugenActive, bool stalaggActive)
{
    return feugenActive || stalaggActive;
}

// Transition: pets down, Thaddius still non-attackable (core removes
// NOT_SELECTABLE ~14s after the adds die, phase 2 at ~15s).
inline bool IsThaddiusPhaseTransition(bool phasePet, bool thaddiusNonAttackable)
{
    return !phasePet && thaddiusNonAttackable;
}

// Even-HP DPS gate: below 40% on the current pet, stop damage while the
// other pet leads by 3%+ so both die together (donor multiplier rule).
inline bool ShouldStopPetDps(float targetHpPct, float otherHpPct)
{
    return targetHpPct <= 40.0f && otherHpPct >= targetHpPct + 3.0f;
}

// Polarity sides: negative charge left, positive charge right, no charge
// center (donor ThaddiusMovePolarityAction; same-map coords shared).
inline int UpstreamThaddiusPolaritySide(bool hasNegative, bool hasPositive)
{
    if (hasNegative)
        return 0;
    if (hasPositive)
        return 1;
    return 2;
}
} // namespace ai


#include <cmath>
#include <algorithm>

namespace ai
{
    enum class ThaddiusPolaritySide : int
    {
        None = 0,
        Positive = 1,
        Negative = -1
    };

    struct ThaddiusPolarityPoint
    {
        float x;
        float y;
    };

    // Charge damage 28062/28085: SpellRadius row 17 = 13 yards.
    constexpr float THADDIUS_POLARITY_DAMAGE_RADIUS = 13.0f;
    constexpr float THADDIUS_POLARITY_ARRIVAL_TOLERANCE = 2.5f;
    constexpr float THADDIUS_POLARITY_OPPOSITE_CLEARANCE = THADDIUS_POLARITY_DAMAGE_RADIUS + 1.0f;
    // Even two arrivals at their inward tolerance edges remain beyond clearance.
    constexpr float THADDIUS_POLARITY_SIDE_RADIUS =
        THADDIUS_POLARITY_OPPOSITE_CLEARANCE / 2.0f + THADDIUS_POLARITY_ARRIVAL_TOLERANCE + 0.5f;

    inline ThaddiusPolaritySide GetThaddiusPolaritySide(bool positive, bool negative)
    {
        if (positive == negative)
            return ThaddiusPolaritySide::None;
        return positive ? ThaddiusPolaritySide::Positive : ThaddiusPolaritySide::Negative;
    }

    // The pinned core positions the two Tesla coils on a diagonal. Use its
    // perpendicular as a repeatable side axis; live floor/path checks still
    // decide whether either point is usable.
    inline ThaddiusPolarityPoint GetThaddiusPolarityPoint(float bossX, float bossY,
                                                           ThaddiusPolaritySide side)
    {
        constexpr float coilAx = 3487.10f;
        constexpr float coilAy = -2911.50f;
        constexpr float coilBx = 3527.81f;
        constexpr float coilBy = -2952.38f;
        float const dx = coilBx - coilAx;
        float const dy = coilBy - coilAy;
        float const length = std::sqrt(dx * dx + dy * dy);
        float const direction = static_cast<int>(side);
        return { bossX - dy / length * THADDIUS_POLARITY_SIDE_RADIUS * direction,
                 bossY + dx / length * THADDIUS_POLARITY_SIDE_RADIUS * direction };
    }

    inline bool ThaddiusPolarityNeedsMove(float x, float y, ThaddiusPolarityPoint point)
    {
        float const dx = x - point.x;
        float const dy = y - point.y;
        return std::sqrt(dx * dx + dy * dy) > THADDIUS_POLARITY_ARRIVAL_TOLERANCE;
    }

    inline bool ShouldHoldThaddiusPosition(bool needsMove, bool hasClearance)
    {
        // Never lock ordinary movement while placement or a manual swap is needed.
        return !needsMove && hasClearance;
    }

    inline bool ThaddiusSegmentClearsOpposite(ThaddiusPolarityPoint start,
                                               ThaddiusPolarityPoint end,
                                               ThaddiusPolarityPoint opposite)
    {
        float const dx = end.x - start.x, dy = end.y - start.y;
        float const ox = start.x - opposite.x, oy = start.y - opposite.y;
        float const length2 = dx * dx + dy * dy;
        float const start2 = ox * ox + oy * oy;
        float const dot = ox * dx + oy * dy;
        float const clearance2 = THADDIUS_POLARITY_OPPOSITE_CLEARANCE *
                                 THADDIUS_POLARITY_OPPOSITE_CLEARANCE;
        if (!std::isfinite(length2) || !std::isfinite(start2) || !std::isfinite(dot))
            return false;
        // Starting inside the margin is permitted only for an outward segment.
        // The caller additionally requires a final point outside the margin.
        if (start2 < clearance2)
            return dot >= 0.0f; // A repeated path point is harmless; final clearance is checked by the caller.
        float const t = length2 > 0.0f ? std::clamp(-dot / length2, 0.0f, 1.0f) : 0.0f;
        float const closestX = ox + t * dx, closestY = oy + t * dy;
        return closestX * closestX + closestY * closestY >= clearance2;
    }

}
