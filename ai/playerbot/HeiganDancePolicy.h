#pragma once

#include <cstdint>

// Pure decision rules for the Heigan safety dance, derived from the
// vanilla core script (boss_heigan.cpp), NOT the donor constants:
// - Dance lasts 45s; first eruption 4s in, then every 3s (eruption k at
//   4+3k s, k = 0..13, last at 43s).
// - Eruption k burns every section EXCEPT seq[k%6], seq = 0,1,2,3,2,1.
// - So during [t_k, t_{k+1}) the safe section is seq[k%6]: stand where
//   it is safe NOW (moving 300ms early, donor margin), never the next
//   window's section.
// - Safe-spot coords are the core sect spots (z ~273.6), not the
//   donor's WotLK waypoints.
// - Ranged hold the platform during the fight; everyone dances.

namespace ai
{
inline constexpr std::int32_t kHeiganDanceFirstEruptMs = 4000;
inline constexpr std::int32_t kHeiganDanceEruptMs = 3000;
inline constexpr std::int32_t kHeiganDanceEruptCount = 14;
inline constexpr std::int32_t kHeiganMoveMarginMs = 300;

// Safe-area walk, index = eruption number % 6.
inline int HeiganSafeArea(int eruptionIndex)
{
    static const int seq[6] = { 0, 1, 2, 3, 2, 1 };
    return seq[eruptionIndex % 6];
}

// Eruption index whose window covers now (moving marginMs early).
// Clamped past the last eruption (43s) to the final window.
inline int HeiganSafeIndex(std::int32_t danceElapsedMs)
{
    const std::int32_t ahead = danceElapsedMs + kHeiganMoveMarginMs;
    if (ahead < kHeiganDanceFirstEruptMs)
        return 0;
    int k = (ahead - kHeiganDanceFirstEruptMs) / kHeiganDanceEruptMs;
    if (k >= kHeiganDanceEruptCount)
        k = kHeiganDanceEruptCount - 1;
    return k;
}

// Safe area to stand in right now.
inline int HeiganSafeAreaNow(std::int32_t danceElapsedMs)
{
    return HeiganSafeArea(HeiganSafeIndex(danceElapsedMs));
}

// Dance clock from the Plague Cloud aura (45s channel): elapsed =
// max duration - remaining. Falls back to the passed total when the
// aura reports no max duration.
inline std::int32_t HeiganDanceElapsed(std::int32_t auraMaxMs,
    std::int32_t auraRemainingMs, std::int32_t fallbackTotalMs = 45000)
{
    const std::int32_t total = auraMaxMs > 0 ? auraMaxMs : fallbackTotalMs;
    const std::int32_t elapsed = total - auraRemainingMs;
    return elapsed < 0 ? 0 : elapsed;
}
} // namespace ai
