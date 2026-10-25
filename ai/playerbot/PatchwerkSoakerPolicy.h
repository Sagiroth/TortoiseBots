#pragma once
#include <cstdint>
#include <set>
#include <vector>

namespace ai
{
struct PatchwerkThreatPlayer
{
    uint64_t guid;
    bool eligible;
};
// In threat-list order, match the core's first four eligible players.
// Test the caller as if it stepped into melee, even while it is outside.
inline bool PatchwerkMeleeSlotsSafe(uint64_t main, std::vector<uint64_t> const& soakers,
    std::vector<PatchwerkThreatPlayer> const& players, uint64_t caller)
{
    if (!main || soakers.size() != 3 || !caller)
        return false;
    std::set<uint64_t> assigned{main};
    for (uint64_t soaker : soakers)
        if (!soaker || !assigned.insert(soaker).second)
            return false;
    if (assigned.count(caller))
        return false;
    std::set<uint64_t> selected;
    for (auto const& player : players)
    {
        if (!player.eligible && player.guid != caller)
            continue;
        if (!assigned.count(player.guid))
            return false;
        selected.insert(player.guid);
        if (selected.size() == 4)
            return selected == assigned;
    }
    return false;
}
}
