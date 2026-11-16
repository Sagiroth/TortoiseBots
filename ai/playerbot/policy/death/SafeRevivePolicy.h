#pragma once

// Whether an autonomous ghost revives at its body now. A body among hostile
// mobs is not revived on: when a quieter point exists in reclaim range the
// ghost walks there (the corpse run picks it) and revives there; when none
// exists it takes the spirit healer right away. SAFE_REVIVE_MAX_WAIT_MS caps a
// ghost that keeps moving without ever reaching a safe point.
//
// Pure and core-free, so tools/test_safe_revive_policy.cpp includes it directly.

#include <cstdint>

namespace TortoiseBots
{

inline constexpr std::uint32_t SAFE_REVIVE_MAX_WAIT_MS = 2 * 60 * 1000;

enum class SafeReviveDecision
{
    Revive,
    MoveToSafePoint,
    SpiritHealer,
};

inline SafeReviveDecision DecideSafeRevive(std::uint32_t aggroUnits, bool safePointInReach)
{
    if (aggroUnits == 0)
        return SafeReviveDecision::Revive;
    return safePointInReach ? SafeReviveDecision::MoveToSafePoint : SafeReviveDecision::SpiritHealer;
}

// unsafeSinceMs: when the ghost first found its revive point unsafe (0 = not yet).
inline bool SafeReviveWaitExpired(std::uint32_t unsafeSinceMs, std::uint32_t nowMs)
{
    if (unsafeSinceMs == 0)
        return false;
    return nowMs - unsafeSinceMs >= SAFE_REVIVE_MAX_WAIT_MS;
}

} // namespace TortoiseBots
