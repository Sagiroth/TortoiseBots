#pragma once
#include <cmath>

namespace TortoiseBots {

constexpr float kMaxSummonGroundDrop = 4.0f;

// A summon point next to the anchor is used only when the core found a floor under it (Map::GetHeight above
// INVALID_HEIGHT), that floor is within kMaxSummonGroundDrop of the anchor's z, and the anchor sees it. No floor
// found within the core's height search (10 yd) means a drop: the old code kept the anchor's z there and the bot
// fell (Wailing Caverns, the Serpentis foot, 47 yd).
//
// Plain data, no core types, so tools/test_summon_point_policy.cpp can
// exercise it standalone.
inline bool SummonPointIsSafe(bool groundFound, float originZ, float groundZ, bool inLos,
                              float maxDrop = kMaxSummonGroundDrop)
{
    return groundFound && std::fabs(groundZ - originZ) <= maxDrop && inLos;
}

} // namespace TortoiseBots
