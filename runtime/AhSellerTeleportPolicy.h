#pragma once

#include <cstdint>

// Pure policy for the organic AH seller lift (task A, live 2026-10-03).
//
// The seller teleports a bot holding AH-profitable stock to an auctioneer so
// it can post through the native handler. Two bounds keep that lift local,
// mirroring the organic buyer (runtime/AhBuyerPolicy.h requires a same-map
// house for the same reason: a cross-map house is unroutable for the walk
// back):
//
// 1. Same-map, nearest house only. Random picks yanked questing bots across
//    continents onto auctioneer spots (Critharadro Dun Morogh -> Darnassus AH
//    20:13:07; 112/194 true teleports land within 300 yd of an AH spawn),
//    and a random same-map pick still lands an idle Dun Morogh gnome in Booty
//    Bay. The lift goes to the nearest non-hostile house on the bot's map.
// 2. No-hijack: only a bot already heading to an AH (tripIsAH) or already
//    standing near that house takes the lift (Tharobraeth Dun Morogh ->
//    Stormwind AH shows even near hops hijack a quest trip). Anything else
//    keeps walking; the post is skipped until the bot gets there itself.
//
// Pure data-in/data-out, no server headers: the caller owns the AI/world reads
// (active travel purpose, house distances, faction) and passes them in.
// Fail-closed throughout.

namespace TortoiseBots
{

// A bot standing this close to its nearest house is already there: the lift
// is a short hop onto the spawn, not a journey.
inline constexpr float kAhSellerLiftNearbyYd = 300.0f;

inline bool AhSellerTeleportAllowed(uint32_t botMapId, uint32_t houseMapId,
    bool tripIsAH, float distToHouseYd)
{
    if (houseMapId != botMapId)
        return false;
    if (!tripIsAH && distToHouseYd > kAhSellerLiftNearbyYd)
        return false;
    return true;
}

} // namespace TortoiseBots
