#pragma once

// Whether an autonomous ghost revives at its body now. A body among hostile
// mobs is not revived on: the ghost waits (the corpse run keeps looking for a
// quieter point in reclaim range) and, if it stays unsafe for
// SAFE_REVIVE_MAX_WAIT_MS, takes the spirit healer instead.
//
// Pure and core-free, so tools/test_safe_revive_policy.cpp includes it directly.

#include <cstdint>

namespace TortoiseBots
{

inline constexpr std::uint32_t SAFE_REVIVE_MAX_WAIT_MS = 2 * 60 * 1000;

enum class SafeReviveDecision
{
    Revive,
    Wait,
};

inline SafeReviveDecision DecideSafeRevive(std::uint32_t aggroUnits)
{
    return aggroUnits == 0 ? SafeReviveDecision::Revive : SafeReviveDecision::Wait;
}

// unsafeSinceMs: when the ghost first found its revive point unsafe (0 = not yet).
inline bool SafeReviveWaitExpired(std::uint32_t unsafeSinceMs, std::uint32_t nowMs)
{
    if (unsafeSinceMs == 0)
        return false;
    return nowMs - unsafeSinceMs >= SAFE_REVIVE_MAX_WAIT_MS;
}

} // namespace TortoiseBots
