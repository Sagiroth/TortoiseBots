#pragma once

// Pure decision rules for the Kel'Thuzad fight (mod-playerbots parity:
// NaxxActions_Kelthuzad.cpp role-split priorities + ring positioning,
// re-derived for vanilla). Vanilla specifics: phase 1 = NOT_SELECTABLE
// (core uses IMMUNE+NOT_SELECTABLE, not the donor's NON_ATTACKABLE);
// Detonate Mana 27819 (vanilla-only) handled by the universal bomb
// runout; MC'd allies already skipped via EnemyPlayerValue. IDs verified
// in tw_world: KT 15990, soldier 16427, abom 16428, weaver 16429,
// guardian 16441, fissure 16129, Frost Blast 27808, Fissure 27810,
// Detonate 27819, Chains 28408. Center 3716.38/-5106.78 verified vs
// core pullPortal.

namespace ai
{
// Add priority order: index by (ranged, tank). Ranged: soldier, weaver,
// abom, KT. Tanks: abom, guardian, KT. Melee: abom, KT. Entries in
// priority order; KT (15990) last.
inline constexpr unsigned kKtSoldier = 16427;
inline constexpr unsigned kKtAbom = 16428;
inline constexpr unsigned kKtWeaver = 16429;
inline constexpr unsigned kKtGuardian = 16441;
inline constexpr unsigned kKtBoss = 15990;

// Ranged ring radius around the phase-2 center (donor inner ring).
inline constexpr float kKtRingRadius = 20.0f;
// Fissure flee distance (donor 10yd).
inline constexpr float kKtFissureFlee = 10.0f;

// Pick the first live entry in the priority list. Returns 0 if none.
inline unsigned KtPickTarget(bool ranged, bool tank, bool soldierUp,
    bool weaverUp, bool abomUp, bool guardianUp, bool ktUp)
{
    if (ranged)
    {
        if (soldierUp) return kKtSoldier;
        if (weaverUp) return kKtWeaver;
        if (abomUp) return kKtAbom;
        if (ktUp) return kKtBoss;
        return 0;
    }
    if (tank)
    {
        if (abomUp) return kKtAbom;
        if (guardianUp) return kKtGuardian;
        if (ktUp) return kKtBoss;
        return 0;
    }
    if (abomUp) return kKtAbom;
    if (ktUp) return kKtBoss;
    return 0;
}
} // namespace ai
