#pragma once

// Which hazard the escape action flees from. A bot standing inside a hazard must
// flee the hazard that CONTAINS it; a nearer hazard whose circle does not reach the
// bot (a small ground effect next to it) must not take the choice, or the action
// finds the bot "not inside" it and does nothing while the bot stands in the real one.
// Among the hazards that contain the bot the nearest centre wins (first one on a tie).
// No core includes: the action hands over each hazard's centre distance and radius.

#include <cstddef>
#include <vector>

namespace ai
{
    struct HazardEscapeCandidate
    {
        float distance; // bot to the hazard's centre
        float radius;   // the hazard's radius
    };

    constexpr std::size_t kNoEscapeHazard = static_cast<std::size_t>(-1);

    // Index into `hazards` of the hazard to flee, or kNoEscapeHazard when none contains the bot.
    inline std::size_t SelectEscapeHazard(std::vector<HazardEscapeCandidate> const& hazards)
    {
        std::size_t best = kNoEscapeHazard;
        for (std::size_t i = 0; i < hazards.size(); ++i)
        {
            if (hazards[i].distance > hazards[i].radius)
                continue;
            if (best == kNoEscapeHazard || hazards[i].distance < hazards[best].distance)
                best = i;
        }
        return best;
    }
}
