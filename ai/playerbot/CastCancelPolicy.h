#pragma once

// Pure rule behind the CastCancel diagnostic row (AiPlayerbot.RaidBossDiagnostics).
//
// Healer and caster casts START and never land in Hakkar fights (609 episodes in
// the 1113 hunt), and no data set logged a cancel. The writer in
// CastCancelLog.cpp is called at every module-side cancel site and reports how
// far the cast had got; this header owns the percentage so it can be tested
// without the engine.
//
// No core includes: the caller passes the core's own numbers
// (Spell::GetCastTime() and Spell::GetCastedTime()).

#include <cstdint>

namespace ai
{
    // Progress of a cast in percent: 0 at the start, 100 at the end. `remainingMs` is the core's countdown
    // (Spell::GetCastedTime, m_timer), which starts at the total and counts to 0. -1 when the total is unknown
    // (an instant spell, nothing on a bar to lose); a remaining above the total reads as a fresh cast.
    inline int CastCancelPct(std::int32_t totalMs, std::uint32_t remainingMs)
    {
        if (totalMs <= 0)
            return -1;
        const std::int32_t remaining = remainingMs > static_cast<std::uint32_t>(totalMs)
            ? totalMs : static_cast<std::int32_t>(remainingMs);
        const int pct = static_cast<int>(100 * (totalMs - remaining) / totalMs);
        return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    }
} // namespace ai
