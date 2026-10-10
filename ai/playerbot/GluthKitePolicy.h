#pragma once

#include <cstdint>

// Pure decision rules for the Gluth fight (mod-playerbots parity:
// NaxxActions_Gluth.cpp triage + mortal-wound swap, re-scoped for
// vanilla). Verified in tw_world + core boss_gluth.cpp: Gluth 15932,
// chow 16360, Mortal Wound 25646 (10s cadence), Decimate 28374/28375,
// Frenzy 28371. Omitted as unproven without live coords: kite ring,
// pre-decimate spot, hunter slowdown posts.

namespace ai
{
// Taunt swap: off-tank taunts when the current tank carries 5+ Mortal
// Wound stacks (donor GluthMainTankMortalWoundTrigger).
inline constexpr std::uint32_t kGluthWoundSwapStacks = 5;

inline bool ShouldGluthTauntSwap(bool botIsTank, bool targetIsGluth,
    bool victimIsOtherTank, std::uint32_t victimWoundStacks)
{
    return botIsTank && targetIsGluth && victimIsOtherTank &&
        victimWoundStacks >= kGluthWoundSwapStacks;
}

// Chow triage: Decimate leaves chow at ~5-10% HP; anything at or under
// 10% is an execute target before it reaches Gluth and heals him.
// Non-tanks take the close execute target, else the boss.
inline constexpr float kGluthChowExecutePct = 10.0f;

inline bool IsGluthChowExecute(float chowHpPct)
{
    return chowHpPct <= kGluthChowExecutePct;
}
} // namespace ai
