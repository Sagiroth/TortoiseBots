#pragma once

#include <cstdint>

// Pure decision rules for the Heigan safety dance, derived from the
// vanilla core script (boss_heigan.cpp), NOT the donor constants:
// - Dance lasts 45s; first eruption 4s in, then every 3s.
// - Safe area walks 0,1,2,3,2,1,0... (eruptionPhase % 6 + mirror).
// - Safe-spot coords are the core sect spots (z ~273.6), not the
//   donor's WotLK waypoints.
// - Fight phase (90s, erupt 15s then every 10s) has no computable safe
//   area without a pull anchor — dance only.
// - Ranged hold the platform during the fight; everyone dances.

namespace ai
{
inline constexpr std::int32_t kHeiganDanceFirstEruptMs = 4000;
inline constexpr std::int32_t kHeiganDanceEruptMs = 3000;
inline constexpr std::int32_t kHeiganMoveLeadMs = 1000;

// Safe-area walk, index = eruption number % 6.
inline int HeiganSafeArea(int eruptionIndex)
{
    static const int seq[6] = { 0, 1, 2, 3, 2, 1 };
    return seq[eruptionIndex % 6];
}

// Next eruption strictly after now + lead (so the bot moves early).
inline int HeiganNextEruption(std::int32_t danceElapsedMs)
{
    const std::int32_t target = danceElapsedMs + kHeiganMoveLeadMs;
    if (target < kHeiganDanceFirstEruptMs)
        return 0;
    return (target - kHeiganDanceFirstEruptMs) / kHeiganDanceEruptMs + 1;
}

// Safe area to stand in right now (with move lead baked in).
inline int HeiganSafeAreaNow(std::int32_t danceElapsedMs)
{
    return HeiganSafeArea(HeiganNextEruption(danceElapsedMs));
}

// Dance is on while Heigan carries Plague Cloud (29350, self-cast at
// dance start, 45s) — directly observable per bot, no shared clock.
inline bool IsHeiganDanceUp(bool plagueCloudOnBoss)
{
    return plagueCloudOnBoss;
}
} // namespace ai
