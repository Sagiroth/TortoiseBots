#pragma once

#include <cstddef>
#include <cstdint>

// Gathering-tool allowlist (E02, ported from mod-playerbots
// `Mgr/Item/LootObjectStack.cpp:337-343`).
//
// Every id below was verified to exist in `tw_world.item_template`. Donor
// ids with no 1.12 rows are excluded: 40772/40892/40893 (WotLK picks and
// the shared pick/knife entries). A bot carrying any listed tool may work
// the node; previously only Mining Pick 2901 / Skinning Knife 7005 passed.

namespace TortoiseBots
{

inline constexpr uint32_t kMiningPickIds[] = {
    756,    // Tunnel Pick
    778,    // Kobold Excavation Pick
    1819,   // Gouging Pick
    1893,   // Miner's Revenge
    1959,   // Cold Iron Pick
    2901,   // Mining Pick
    9465,   // Digmaster 5000
    20723,  // Brann's Trusty Pick
};

inline constexpr uint32_t kSkinningKnifeIds[] = {
    7005,   // Skinning Knife
    12709,  // Finkle's Skinner
    19901,  // Zulian Slicer
};

template <typename HasItem, std::size_t N>
inline bool HasAnyTool(HasItem hasItem, uint32_t const (&ids)[N])
{
    for (std::size_t i = 0; i < N; ++i)
        if (hasItem(ids[i]))
            return true;
    return false;
}

} // namespace TortoiseBots
