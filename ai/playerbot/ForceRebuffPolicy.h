#pragma once

#include <algorithm>
#include <cstdint>

// Pure decision rules for the force-rebuff top-off pass (mod-playerbots
// parity, BUFF-1). Donor: mod-playerbots @ 79bd4281,
// src/Bot/ForceRebuff.cpp (2-min OOC window, margin rule, buff-first heal
// veto). No core includes: callers translate game state into plain inputs
// so the rules stay testable in tools/test_force_rebuff_policy.cpp.
//
// Donor rule: while the window is pending and out of combat, a buff counts
// as needing a recast when remaining + margin < max, where margin grows
// with the window age (margin = max(configMargin, ageMs + 5s), donor
// ForceRebuffMarginSecs default 60). Outside the window the normal
// refresh rule applies (our GroupBuffPolicy::BuffNeedsRefresh for the
// non-pending path).

namespace ai
{
    // Donor REBUFF_WINDOW_TIMEOUT_MS: 2 min.
    inline std::uint32_t ForceRebuffWindowMs() { return 2 * 60 * 1000; }
    // Donor ForceRebuffMarginSecs default: 60 s floor for the margin.
    inline std::uint32_t ForceRebuffMarginFloorMs() { return 60 * 1000; }

    inline bool ForceRebuffPending(std::uint32_t beginMs, std::uint32_t nowMs,
        std::uint32_t windowMs = ForceRebuffWindowMs())
    {
        return nowMs - beginMs < windowMs;
    }

    inline std::uint32_t ForceRebuffMarginMs(std::uint32_t beginMs, std::uint32_t nowMs,
        std::uint32_t floorMs = ForceRebuffMarginFloorMs())
    {
        return std::max(floorMs, (nowMs - beginMs) + 5000u);
    }

    // Top-off verdict inside the window: a missing aura always rebuffs; a
    // present aura rebuffs when remaining + margin < max. Permanent auras
    // (remaining/max <= 0) never count.
    inline bool ForceRebuffBelowTarget(bool hasAura, std::int32_t remainingMs,
        std::int32_t maxDurationMs, std::uint32_t beginMs, std::uint32_t nowMs)
    {
        if (!hasAura)
            return true;
        if (remainingMs <= 0 || maxDurationMs <= 0)
            return false;
        return std::uint32_t(remainingMs) + ForceRebuffMarginMs(beginMs, nowMs) <
            std::uint32_t(maxDurationMs);
    }

    // Buff-first heal veto inside the window (donor ForceRebuffBuffFirst
    // multiplier): heals yield while buff work is proposed this cycle or a
    // buff cast's GCD is still running.
    inline bool ForceRebuffSuppressHeal(bool pending, bool inCombat,
        bool buffProposedThisCycle, bool onGlobalCooldown)
    {
        if (!pending || inCombat)
            return false;
        return buffProposedThisCycle || onGlobalCooldown;
    }
}
