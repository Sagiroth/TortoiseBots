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
constexpr uint32_t kLeadGoneGraceMs = 30000; // an open fight survives its lead being away this long

// True when the bound-group sampler feeds this binding now: bound, and the run
// did not feed it within the window. The subtraction is the core's getMSTime
// rule (WorldTimer::getMSTimeDiff, src/shared/Timer.h:34): unsigned, so a tick
// wrap reads as the small elapsed time it is.
inline bool SamplerFeeds(bool bound, bool runFedEver, uint32_t runFedAtMs, uint32_t nowMs)
{
    return bound && (!runFedEver || nowMs - runFedAtMs >= kRunFedWindowMs);
}

// A lead is usable for its binding's sampler tick while it is in world in the
// group the binding was made with. A lead that left and rejoined elsewhere
// resolves to another group id; that, and a missing group, is away, so the
// grace closes the fight it left open instead of feeding a stranger's.
inline bool LeadUsable(bool resolved, bool inWorld, uint32_t leadGroupId, uint32_t bindingGroupId)
{
    return resolved && inWorld && leadGroupId != 0 && leadGroupId == bindingGroupId;
}

// One binding's clock of its lead being unusable (gone from the world through a
// map transition, no group, a character that cannot be resolved, a lead that
// rejoined elsewhere): until the lead is back the sampler has nothing to feed
// the open fight with, so after the grace the fight is closed as unfinished
// instead of kept alive forever.
struct LeadAway
{
    bool sinceSet = false;
    uint32_t sinceMs = 0;

    // Call each sampler tick; true once the lead has been unusable for the
    // grace. A usable lead (the transition finished) clears the clock.
    bool Overdue(bool leadUsable, uint32_t nowMs)
    {
        if (leadUsable)
        {
            sinceSet = false;
            return false;
        }
        if (!sinceSet)
        {
            sinceSet = true;
            sinceMs = nowMs;
            return false;
        }
        return nowMs - sinceMs >= kLeadGoneGraceMs;
    }
};

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
