#pragma once

#include <cstdint>
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
        // The winning row's boss: the caller resolves the unit from it, so the boss and its victim never depend on
        // another kind's read (the seek clock's row).
        std::uint32_t bossEntry = 0;
        // The release clock the read used (the seek_aura cycle's): known only with that key and a live cycle.
        bool clockKnown = false;
        std::uint32_t dueInMs = 0;
    };

    // One live `heal_reserve` row as the pick sees it: its reserve percent, its release lead and its boss.
    struct HealReserveRow
    {
        float reservePct = 0.0f;
        float windowSec = 0.0f;
        std::uint32_t bossEntry = 0;
    };

    // The almost-full top-up tier: the only health band the reserve governs. A member below `mediumPct` is another
    // tier and a refusal for it is the ordinary mana rule, not the reserve's.
    inline bool HealReserveTopUpTier(float healthPct, float mediumPct, float almostFullPct)
    {
        return healthPct >= mediumPct && healthPct < almostFullPct;
    }

    // D5: the reserve is released while the cycle clock is known and the next siphon is at most `releaseLeadSec` away.
    // No clock or a lead of 0 never releases it.
    inline bool HealReserveReleased(bool clockKnown, std::uint32_t dueInMs, float releaseLeadSec)
    {
        if (!clockKnown || !(releaseLeadSec > 0.0f))
            return false;
        return static_cast<float>(dueInMs) <= releaseLeadSec * 1000.0f;
    }

    // D6: several live rows keep the highest `param`; its `window_s` and boss go with it. A tie keeps the first row.
    inline HealReserveRead HealReservePick(const std::vector<HealReserveRow>& rows, bool clockKnown,
                                           std::uint32_t dueInMs)
    {
        HealReserveRead out;
        out.clockKnown = clockKnown;
        out.dueInMs = clockKnown ? dueInMs : 0;
        for (const HealReserveRow& row : rows)
        {
            if (out.live && row.reservePct <= out.reservePct)
                continue;
            out.live = true;
            out.reservePct = row.reservePct;
            out.released = HealReserveReleased(clockKnown, dueInMs, row.windowSec);
            out.bossEntry = row.bossEntry;
        }
        return out;
    }

    // The reserve's effective line: the row's percent, never below today's MediumMana rule.
    inline float HealReserveLine(const HealReserveRead& read, std::uint8_t mediumManaPct)
    {
        return read.reservePct > static_cast<float>(mediumManaPct) ? read.reservePct :
                                                                     static_cast<float>(mediumManaPct);
    }

    // D2/D4: the reserve refuses a top-up only while it is live, outside its release window, for a target that is not
    // exempt, with the healer under its line. Everything else is today's rule. The pick reads this to skip a refused
    // top-up candidate; the gate reads it to answer the trigger.
    inline bool HealReserveRefuses(bool flagOn, const HealReserveRead& read, std::uint8_t manaPct,
                                   std::uint8_t mediumManaPct, bool targetExempt)
    {
        if (!flagOn || !read.live || targetExempt || read.released)
            return false;
        return static_cast<float>(manaPct) < HealReserveLine(read, mediumManaPct);
    }

    // D4: today's rule (`mana >= mediumMana`) whenever the key is off, no row is live, the target is exempt or the
    // window is released; else the reserve, never below the medium line.
    inline bool HealTopUpAllowed(bool flagOn, const HealReserveRead& read, std::uint8_t manaPct,
                                 std::uint8_t mediumManaPct, bool targetExempt)
    {
        if (HealReserveRefuses(flagOn, read, manaPct, mediumManaPct, targetExempt))
            return false;
        return manaPct >= mediumManaPct;
    }
}
