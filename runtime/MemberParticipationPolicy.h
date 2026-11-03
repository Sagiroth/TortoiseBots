#pragma once

// The run leader's participation clock (commands/Commander.cpp): a raid member that made actual combat progress
// this recently fights where it stands (DungeonRun::MemberParticipates), whatever the floor under it. The clock is
// glue state keyed by bot guid; its forgetting and its progress rule are pure here, so both can be tested without
// a Player.
//
// A bot's clock is forgotten by when it was last SEEN, never by when it last progressed: a bot that has not
// progressed yet (a fresh member, or one whose progress the read did not catch) was erased once 64 bots were
// tracked. An auto-repeat spell (Auto Shot, a wand) being on is NO progress: it stays on while the bot cannot shoot,
// which is the wedged case. A shot is: the glue reads it from the authoritative shot count (RangedShotLedger.h),
// never from a snapshot of the ranged attack timer, which a once-a-second read cannot sample reliably. A self-cast is no
// progress (a buff or a heal on the bot itself while it stands wedged), and neither is a
// selected target.

#include <cstddef>
#include <cstdint>

namespace TortoiseBots
{
namespace DungeonRun
{

// Every bot's clock: when it last made progress, and when the glue last read it (both WorldTimer ms).
struct ParticipationClock
{
    std::uint32_t actMs = 0;
    std::uint32_t seenMs = 0;
};

constexpr std::uint32_t kParticipationForgetMs = 60000; // unseen this long: forgotten once the map reaches its bound
constexpr std::size_t kParticipationClocksMax = 256;

// The spell id a progress clock reads: a self-targeted cast is no progress (0), any other cast is its own id.
inline std::uint32_t ProgressSpell(std::uint32_t spellId, bool targetSelf)
{
    return targetSelf ? 0 : spellId;
}

// One progress reading: a cast or channel running (or a changed cast), or a swing that landed since the last read
// (a ranged one is a shot that fired since the last read: the shot
// count moved). The auto-repeat spell being on is deliberately not a reading:
// it stays on with no shot, which is a wedged bot with Auto Shot or a wand. `inFight` gates all of it; `observed` is
// false for the first read of a bot.
inline bool ParticipationProgress(bool inFight, std::uint32_t spell, bool observed, std::uint32_t lastSpell,
    bool baseSwing, bool rangedSwing)
{
    return inFight && (spell != 0 || (observed && (spell != lastSpell || baseSwing || rangedSwing)));
}

// Forgets the clocks of bots unseen for `forgetMs` and, if the map is still at its bound, the least recently seen
// one: never more than `max` entries, and never a bot that was seen this minute whatever its progress. Ticks are
// wrap-safe (unsigned subtraction of the timer reading).
template <typename Map>
inline void PruneParticipationClocks(Map& clocks, std::uint32_t now, std::size_t max = kParticipationClocksMax,
    std::uint32_t forgetMs = kParticipationForgetMs)
{
    for (auto it = clocks.begin(); it != clocks.end();)
        if (static_cast<std::uint32_t>(now - it->second.seenMs) > forgetMs)
            it = clocks.erase(it);
        else
            ++it;
    while (clocks.size() > max && !clocks.empty())
    {
        auto oldest = clocks.begin();
        std::uint32_t oldestMs = static_cast<std::uint32_t>(now - oldest->second.seenMs);
        for (auto it = clocks.begin(); it != clocks.end(); ++it)
        {
            std::uint32_t const since = static_cast<std::uint32_t>(now - it->second.seenMs);
            if (since > oldestMs)
            {
                oldest = it;
                oldestMs = since;
            }
        }
        clocks.erase(oldest);
    }
}

} // namespace DungeonRun
} // namespace TortoiseBots
