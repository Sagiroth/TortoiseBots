#pragma once

#include <cstdint>

// Pure policy for the organic AH seller lift (task A, live 2026-10-03).
//
// The seller teleports a bot holding AH-profitable stock to an auctioneer so
// it can post through the native handler. Two bounds keep that lift from
// hijacking ordinary play, mirroring the organic buyer (runtime/AhBuyerPolicy.h
// requires a same-map house for the same reason: a cross-map house is
// unroutable for the walk back):
//
// 1. Same-map only. Cross-map teleports yanked questing bots across
//    continents onto auctioneer spots (Critharadro Dun Morogh -> Darnassus AH
//    20:13:07; 112/194 true teleports land within 300 yd of an AH spawn).
// 2. No-hijack: a bot mid-trip on a quest/grind/gather errand keeps walking -
//    only an idle bot or one already heading to an AH takes the lift
//    (Tharobraeth Dun Morogh -> Stormwind AH shows same-map hops hijack too).
//
// Pure data-in/data-out, no server headers: the caller owns the AI/world reads
// (active travel target + purpose) and passes them in. Fail-closed throughout.

namespace TortoiseBots
{

inline bool AhSellerTeleportAllowed(uint32_t botMapId, uint32_t houseMapId,
    bool hasActiveTrip, bool tripIsAH)
{
    if (houseMapId != botMapId)
        return false;
    if (hasActiveTrip && !tripIsAH)
        return false;
    return true;
}

} // namespace TortoiseBots
