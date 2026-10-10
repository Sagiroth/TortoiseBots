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
inline int ThaddiusPolaritySide(bool hasNegative, bool hasPositive)
{
    if (hasNegative)
        return 0;
    if (hasPositive)
        return 1;
    return 2;
}
} // namespace ai
