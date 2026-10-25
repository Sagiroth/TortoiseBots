#pragma once
#include <cstdint>

namespace ai
{
    // Three stacks is the opt-in handoff policy, not a boss-script threshold.
    inline bool KurinnaxxNeedsSwap(unsigned victimStacks, unsigned candidateStacks)
    {
        return victimStacks >= 3 && candidateStacks == 0;
    }

    inline bool KurinnaxxNeedsHold(unsigned selfStacks, bool otherParticipatingTank)
    {
        return selfStacks != 0 && otherParticipatingTank;
    }

    inline bool KurinnaxxBlockAction(bool taunt, bool offensive, bool aoe,
        bool targetsBoss, bool hold, bool otherParticipatingTank)
    {
        // Dedicated coordinated taunts are plain Actions, not generic taunt actions.
        return (taunt && (targetsBoss || aoe) && otherParticipatingTank) ||
            (hold && offensive && (targetsBoss || aoe));
    }
}
