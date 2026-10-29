#pragma once
// When the bound-group sampler feeds a group's combat book instead of the run
// tick, and which engaged creature is a fight's boss; plain data and the
// standard library only.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots {
namespace BoundFight {

constexpr uint32_t kSampleMs = 1000;       // the sampler's cadence (the run tick's own 1 Hz)
constexpr uint32_t kRunFedWindowMs = 1500; // a binding the run fed this recently is the run's

// True when the bound-group sampler feeds this binding now: bound, and the run
// did not feed it within the window. The subtraction is the core's getMSTime
// rule (WorldTimer::getMSTimeDiff, src/shared/Timer.h:34): unsigned, so a tick
// wrap reads as the small elapsed time it is.
inline bool SamplerFeeds(bool bound, bool runFedEver, uint32_t runFedAtMs, uint32_t nowMs)
{
    return bound && (!runFedEver || nowMs - runFedAtMs >= kRunFedWindowMs);
}

struct BossCandidate
{
    uint32_t entry = 0;
    uint64_t guid = 0;
    std::string name;
    bool boss = false;
    uint32_t maxHealth = 0;
};

// The fight's boss: the boss-flagged candidate with the most maximum health
// (ties: the lowest entry); -1 when none.
inline int PickBoss(std::vector<BossCandidate> const& c)
{
    int best = -1;
    for (size_t i = 0; i < c.size(); ++i)
    {
        if (!c[i].boss)
            continue;
        if (best < 0 || c[i].maxHealth > c[best].maxHealth ||
            (c[i].maxHealth == c[best].maxHealth && c[i].entry < c[best].entry))
            best = static_cast<int>(i);
    }
    return best;
}

} // namespace BoundFight
} // namespace TortoiseBots
