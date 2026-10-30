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

// What one world tick makes due for the position feed.
struct Due
{
    bool combat = false;
    bool idle = false;

    bool Any() const { return combat || idle; }
};

// The two accumulators of the position feed. Advance runs on every world tick and moves both of them, each on its own
// period, so a slow period can never hold the other back (and the idle period never loses the time spent waiting for
// the combat period). Each accumulator resets when it fires.
class Clock
{
public:
    void Set(Cadence c) { m_cadence = c; }

    Due Advance(uint32_t diff)
    {
        Due due;
        m_combatMs += diff;
        if (m_combatMs >= m_cadence.combatMs)
        {
            m_combatMs = 0;
            due.combat = true;
        }
        if (IdleSends(m_cadence))
        {
            m_idleMs += diff;
            if (m_idleMs >= m_cadence.idleMs)
            {
                m_idleMs = 0;
                due.idle = true;
            }
        }
        return due;
    }

private:
    Cadence m_cadence;
    uint32_t m_combatMs = 0;
    uint32_t m_idleMs = 0;
};

} // namespace RealtimeCadence
} // namespace TortoiseBots
