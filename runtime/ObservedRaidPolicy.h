#pragma once

// The observed-raid sampler's rules that hold without a game header (spec dash-raids-tab): which members a group's combat
// book is fed from, and what the flag's off turn does to the books it fed. Standard library only, so
// tools/test_observed_raid_policy.cpp compiles it without a game header.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace TortoiseBots {
namespace ObservedRaid {

// Whether a sampled unit stands on the group's selected map copy, the map and instance the collection named for the group.
// A member in another instance copy, on another map or outdoors is not in the raid: its combat never opens or extends the
// group's fight, its being alive never keeps the fight from closing, and it is never the fight's boss anchor. The selected
// instance is never 0 (a raid map copy always has one), so a group without a copy samples nothing.
inline bool OnSelectedCopy(uint32_t selectedMap, uint32_t selectedInstance, uint32_t unitMap, uint32_t unitInstance)
{
    return selectedInstance != 0 && unitMap == selectedMap && unitInstance == selectedInstance;
}

// The sampler's flag as the last tick read it. TurnedOff is true once, on the enabled -> disabled turn (`.reload config`
// turned AiPlayerbot.ObservabilityRaids off), so that tick can close what the flag fed instead of leaving an open window
// behind.
struct FlagTurn
{
    bool on = false;

    bool TurnedOff(bool enabled)
    {
        bool const turned = on && !enabled;
        on = enabled;
        return turned;
    }
};

// Every group the sampler holds a book for, each once: the set the last collection found, then the groups on a gone-grace
// clock. A bound group is left out: its book belongs to the bound path, which closes it on its own turn.
template <typename IsBound>
inline std::vector<uint32_t> HeldGroups(std::vector<uint32_t> const& collected, std::vector<uint32_t> const& away,
                                        IsBound const& isBound)
{
    std::vector<uint32_t> out;
    out.reserve(collected.size() + away.size());
    for (uint32_t const groupId : collected)
        if (!isBound(groupId))
            out.push_back(groupId);
    for (uint32_t const groupId : away)
    {
        bool const seen = !collected.empty() &&
            std::find(collected.begin(), collected.end(), groupId) != collected.end();
        if (!seen && !isBound(groupId))
            out.push_back(groupId);
    }
    return out;
}

// The off turn: close each held group's open fight at now, then forget its book. Closing first means a fight still open at
// the turn is logged as unfinished instead of disappearing with the book.
template <typename Close, typename Forget>
inline void ReleaseBooks(std::vector<uint32_t> const& groups, uint32_t nowMs, Close const& close, Forget const& forget)
{
    for (uint32_t const groupId : groups)
    {
        close(groupId, nowMs);
        forget(groupId);
    }
}

} // namespace ObservedRaid
} // namespace TortoiseBots
