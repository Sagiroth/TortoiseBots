#pragma once

#include <cstdint>
#include <utility>
#include <vector>

// Pure decisions for the healer top-up reserve (the `heal_reserve` boss-tactic row): outside the row's release window
// a healer tops up members in the almost-full tier only while its own mana is at or above the row's reserve percent.
// Tanks and the boss's current victim keep today's `MediumMana` rule at all times.
//
// No core includes: callers in strategy/ translate game state into these plain inputs, so the rules stay testable in
// tools/test_heal_reserve_policy.cpp without the server.

namespace ai
{
    struct HealReserveRead
    {
        bool live = false;
        float reservePct = 0.0f;
        bool released = false;
    };

    // D5: the reserve is released while the cycle clock is known and the next siphon is at most `releaseLeadSec` away.
    // No clock or a lead of 0 never releases it.
    inline bool HealReserveReleased(bool clockKnown, std::uint32_t dueInMs, float releaseLeadSec)
    {
        if (!clockKnown || !(releaseLeadSec > 0.0f))
            return false;
        return static_cast<float>(dueInMs) <= releaseLeadSec * 1000.0f;
    }

    // D6: several live rows keep the highest `param`; its `window_s` goes with it. A tie keeps the first row.
    inline HealReserveRead HealReservePick(const std::vector<std::pair<float, float>>& paramAndWindow, bool clockKnown,
                                           std::uint32_t dueInMs)
    {
        HealReserveRead out;
        for (const std::pair<float, float>& row : paramAndWindow)
        {
            if (out.live && row.first <= out.reservePct)
                continue;
            out.live = true;
            out.reservePct = row.first;
            out.released = HealReserveReleased(clockKnown, dueInMs, row.second);
        }
        return out;
    }

    // D4: today's rule (`mana >= mediumMana`) whenever the key is off, no row is live, the target is exempt or the
    // window is released; else the reserve, never below the medium line.
    inline bool HealTopUpAllowed(bool flagOn, const HealReserveRead& read, std::uint8_t manaPct,
                                 std::uint8_t mediumManaPct, bool targetExempt)
    {
        if (!flagOn || !read.live || targetExempt || read.released)
            return manaPct >= mediumManaPct;
        const float reserve = read.reservePct > static_cast<float>(mediumManaPct) ? read.reservePct :
                                                                                    static_cast<float>(mediumManaPct);
        return static_cast<float>(manaPct) >= reserve;
    }
}
