#pragma once

#include <string>
#include <cstdint>

// Pure decision rules for the Golemagg fight (mod-playerbots parity,
// raid1 batch item 9, implemented LAST per the task order).
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/MC/MCActions.cpp:115-301
// (tank positioning, Magma Splash back-off, healer spot, rager pickup) +
// MCTriggers.cpp:46-88 (splash/healer/main/assist triggers) +
// MCMultipliers.cpp:105-144 (single-tank, assist-tank, AoE-off, melee
// fallback, back-off lock, burn phase) + MCStrategy.cpp:108-128 (DPS
// exclusion of Core Ragers while Golemagg lives).
// NOTE: branched from origin/feat/playerbots-parity WITHOUT the
// parity/ld-8-main-tank value (not merged at implementation time). Tank
// roles below use the same fallback the Razorgore PR used: main = first
// living tank by member-slot order, assists = next two. When ld-8 merges,
// the role reads should move to the shared value — flagged in provenance.
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_golemagg_policy.cpp.

namespace ai
{
    // Golemagg 11988, Core Rager 11672, Magma Splash 13880, Trust 20553.
    constexpr std::uint32_t kGolemaggEntry = 11988;
    constexpr std::uint32_t kCoreRagerEntry = 11672;
    constexpr std::uint32_t kMagmaSplashSpellId = 13880;
    constexpr std::uint32_t kGolemaggTrustSpellId = 20553;

    // Donor constants: 20-stack back-off, 12y out, 8y healer tolerance,
    // 30y Trust separation, 10% burn phase.
    constexpr int kMagmaSplashBackOffStacks = 20;
    constexpr float kMagmaSplashBackOffDistance = 12.0f;
    constexpr float kGolemaggHealerTolerance = 8.0f;
    constexpr float kGolemaggTrustDistance = 30.0f;
    constexpr float kGolemaggBurnPct = 10.0f;

    // Fixed camp coords (donor map data; Turtle validation wants eyes).
    constexpr float kGolemaggTankX = 795.7308f;
    constexpr float kGolemaggTankY = -994.8848f;
    constexpr float kGolemaggTankZ = -207.18661f;
    constexpr float kRagerTankX = 846.6453f;
    constexpr float kRagerTankY = -1019.0639f;
    constexpr float kRagerTankZ = -198.9819f;
    constexpr float kHealerX = 821.2f;
    constexpr float kHealerY = -1007.0f;
    constexpr float kHealerZ = -203.0f;

    // Splash back-off: non-tanks at 20+ stacks step 12y out (unless burn).
    inline bool ShouldBackOffSplash(bool botIsTank, int splashStacks, float bossHealthPct)
    {
        if (botIsTank)
            return false;
        if (bossHealthPct <= kGolemaggBurnPct)
            return false;
        return splashStacks >= kMagmaSplashBackOffStacks;
    }

    // Back-off lock: backed-off non-tanks don't re-engage until the stack
    // expires (donor: whole stack gone, 30s after last application).
    inline bool ShouldHoldBackOff(bool botIsTank, bool hasAnySplash, float bossHealthPct)
    {
        if (botIsTank)
            return false;
        if (bossHealthPct <= kGolemaggBurnPct)
            return false;
        return hasAnySplash;
    }

    // DPS exclusion: Core Ragers are unkillable while Golemagg lives (full
    // heal at 50%) — DPS bots leave them to the tanks.
    inline bool ShouldExcludeRager(bool golemaggAlive)
    {
        return golemaggAlive;
    }

    // AoE name set shared with the Garr veto (duplicated here because this
    // branch predates McGarrShazzrahPolicy.h; merge both to one home when
    // the branches land): threat flags under-mark our real AoE and
    // over-mark heals + single-target dots.
    inline bool IsGolemaggSuppressedAoeAction(const std::string& actionName)
    {
        return actionName == "dps aoe" ||
               actionName == "consecration" ||
               actionName == "whirlwind" ||
               actionName == "magma totem" ||
               actionName == "explosive trap" ||
               actionName == "hurricane" ||
               actionName == "flamestrike" ||
               actionName == "arcane explosion" ||
               actionName == "multi-shot" ||
               actionName == "volley" ||
               actionName == "rain of fire" ||
               actionName == "hellfire";
    }

    // Single living tank picks up everything (donor skips the role dance).
    inline bool IsSingleLivingTank(unsigned livingTankCount)
    {
        return livingTankCount == 1;
    }
}
