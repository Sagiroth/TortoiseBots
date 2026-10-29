#pragma once

// The live position feed's own rates (task S3): how often a bound group's positions are sent while it fights, and on a
// second, slower period while it does not. Both are milliseconds and clamp to 250..2000. An idle period of 2000 is the
// shipped default: at that rate the regular 2 s COMMANDERS report is the only out-of-combat source, so a server whose
// groups stand around pays nothing extra for this feed.
//
// Standard library only, so tools/test_realtime_cadence.cpp compiles it without a game header.

#include <algorithm>
#include <cstdint>

namespace TortoiseBots {
namespace RealtimeCadence {

struct Cadence
{
    uint32_t combatMs = 500;
    uint32_t idleMs = 2000;
};

// Each period clamped to 250..2000 ms.
inline Cadence Clamp(int combat, int idle)
{
    auto const clamp = [](int v) { return static_cast<uint32_t>(std::max(250, std::min(2000, v))); };
    return Cadence{clamp(combat), clamp(idle)};
}

// True when the idle period asks for its own position reports. At 2000 ms it does not: the 2 s COMMANDERS report already
// carries the positions of every standing group, and a second out-of-combat source would only repeat it.
inline bool IdleSends(Cadence c) { return c.idleMs < 2000; }

// The two accumulators of the position feed, one world tick at a time: each fires once its period is reached and resets
// then, so the feed never runs twice for the same period.
class Clock
{
public:
    void Set(Cadence c) { m_cadence = c; }

    bool CombatDue(uint32_t diff)
    {
        m_combatMs += diff;
        if (m_combatMs < m_cadence.combatMs)
            return false;
        m_combatMs = 0;
        return true;
    }

    bool IdleDue(uint32_t diff)
    {
        m_idleMs += diff;
        if (m_idleMs < m_cadence.idleMs)
            return false;
        m_idleMs = 0;
        return true;
    }

private:
    Cadence m_cadence;
    uint32_t m_combatMs = 0;
    uint32_t m_idleMs = 0;
};

} // namespace RealtimeCadence
} // namespace TortoiseBots
