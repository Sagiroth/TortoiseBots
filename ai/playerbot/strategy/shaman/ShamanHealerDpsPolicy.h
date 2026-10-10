#pragma once
#include <string>

// Pure policy for resto shaman healer-dps (mod-playerbots parity SHM-1,
// priest offdps pattern): while `healer should attack` holds (nobody needs
// healing, mana comfortable), the resto bot spends idle ticks on damage at
// the lowest relevance so every heal outbids it. Flame shock first for the
// DoT, lightning bolt filler, chain lightning only into a ranged pack.

namespace ai
{
    // Relative relevance ladder inside the `healer should attack` node.
    // Offsets above ACTION_DEFAULT mirror the priest offdps ladder shape
    // (DoT top, filler middle, pack AoE in its own node).
    inline float RestoHealerDpsPriority(std::string const& action)
    {
        if (action == "flame shock")
            return 0.2f;
        if (action == "chain lightning")
            return 0.3f;
        if (action == "lightning bolt")
            return 0.0f;
        return -1000.0f;
    }

    inline bool IsRestoHealerDpsAction(std::string const& action)
    {
        return action == "flame shock" || action == "lightning bolt" || action == "chain lightning";
    }
}
