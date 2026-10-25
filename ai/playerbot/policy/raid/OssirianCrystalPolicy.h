#pragma once

#include <cstdint>

// Pure decision rules for the Ossirian crystal tactic (mod-playerbots
// parity: Aq20Triggers.cpp / Aq20Actions.cpp / Aq20Utils.cpp). No core
// includes: callers translate game state into plain inputs, so the rules
// stay testable in tools/test_ossirian_crystal_policy.cpp without the
// server. Spell/GO IDs verified against tw_world: buff 25176, weakness
// debuffs 25177/78/80/81/83, crystal GO 180619, boss entry 15339.

namespace ai
{
// Donor constants: head for the crystal when the weakness debuff has less
// than 5s left, or when remaining time minus the 5s crystal-arm lead is
// under the run time to the crystal (only inside the 30s window).
inline constexpr std::int32_t kOssirianRunNowMs = 5000;
inline constexpr std::int32_t kOssirianPlanWindowMs = 30000;
inline constexpr std::int32_t kOssirianCrystalArmMs = 5000;

// Boss must be within 25yd of the crystal for the use to strip the buff
// (donor guard; core spawns trigger pylons at the crystal position).
inline constexpr float kOssirianCrystalBossRange = 25.0f;

inline bool ShouldRunToOssirianCrystal(bool bossInCombat, bool buffActive,
    std::int32_t debuffMsRemaining, std::int32_t runTimeMs)
{
    if (!bossInCombat)
        return false;
    if (buffActive)
        return true;
    if (debuffMsRemaining < kOssirianRunNowMs)
        return true;
    if (debuffMsRemaining < kOssirianPlanWindowMs)
        return debuffMsRemaining - kOssirianCrystalArmMs < runTimeMs;
    return false;
}

inline bool ShouldUseOssirianCrystal(float bossDistToCrystal, bool crystalInUse,
    bool buffActive, std::int32_t debuffMsRemaining)
{
    if (bossDistToCrystal > kOssirianCrystalBossRange || crystalInUse)
        return false;
    if (buffActive)
        return true;
    return debuffMsRemaining <= kOssirianRunNowMs;
}
} // namespace ai

#include <cmath>
#include <cstdint>
#include <map>
namespace ai
{
inline bool OssirianWeaknessDue(bool strength, bool hasWeakness, int32_t remainingMs)
{
    return strength || !hasWeakness || (remainingMs >= 0 && remainingMs <= 6000);
}
inline bool OssirianCrystalInRange(float centerDistance)
{
    // Actual weakness radius30yd; retain5yd margin during the5sec delayed cast.
    return std::isfinite(centerDistance) && centerDistance >= 0 && centerDistance <= 25;
}
struct OssirianClickWindow
{
    std::map<uint64_t,unsigned> attempts;
    uint32_t lastAttempt = 0;
    bool started = false;
    bool Ready(uint64_t crystal, uint32_t now) const
    {
        auto it = attempts.find(crystal);
        return crystal && (it == attempts.end() || it->second < 2) &&
            (!started || now-lastAttempt >= 8000);
    }
    bool Claim(uint64_t crystal,uint64_t caller,uint64_t chosen,uint32_t now)
    {
        if (!caller || caller != chosen || !Ready(crystal,now)) return false;
        ++attempts[crystal]; lastAttempt=now; started=true; return true;
    }
};
}
