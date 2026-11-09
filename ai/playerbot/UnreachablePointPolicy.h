#pragma once

#include <cstdint>

#include "DeathClusterPolicy.h"

// A point nobody can walk to stays out of the pick for a while.
//
// Fishing points sit on the water, so the pick cannot ask the navmesh whether
// they are reachable (TravelMeshSieveApplies). At Iceflow Lake every Dun Morogh
// fishing point lies on the lake: bots walked out onto an ice floe with a boat
// on it, found no path off it and piled up there, 143 bots in a day, each one
// picking the same lake again after its rescue. Algaz Station in Loch Modan
// does the same with its upper terrace and the hand-in NPCs standing on it
// (generic rpg errands): 141 bots in a day.
//
// The first failed walk to a point only notes it; the second, from any bot,
// refuses the point for half an hour and everything within the radius around it,
// so one bot wedged in an unrelated pocket cannot close a lake by itself.
//
// Pure rules over the death-spot slots (DeathClusterPolicy.h): TravelMgr owns
// the two shared lists, the move-failure branch notes, the pick only asks.
namespace ai
{
    std::uint32_t const kUnreachablePointSlots = 32;

    std::uint32_t const kUnreachableNotedMs = 10u * 60u * 1000u;

    std::uint32_t const kUnreachableRefusedMs = 30u * 60u * 1000u;

    float const kUnreachableRadiusYd = 15.0f;

    // Whether a travel purpose takes this memory: the purposes with many
    // interchangeable points, where another point is always a fair pick.
    inline bool PurposeRemembersUnreachablePoints(bool fishing, bool genericRpg)
    {
        return fishing || genericRpg;
    }

    // Records one failed walk. True when the point is refused from now on.
    inline bool NoteUnreachablePoint(DeathAvoidSpot* noted, DeathAvoidSpot* refused, std::uint32_t mapId,
        float x, float y, std::uint32_t nowMs)
    {
        if (IsDeathPositionAvoided(mapId, x, y, nowMs, refused, kUnreachablePointSlots))
            return true;
        if (IsDeathPositionAvoided(mapId, x, y, nowMs, noted, kUnreachablePointSlots))
        {
            AddDeathAvoidSpot(refused, kUnreachablePointSlots, mapId, x, y, nowMs, kUnreachableRefusedMs, kUnreachableRadiusYd);
            return true;
        }
        AddDeathAvoidSpot(noted, kUnreachablePointSlots, mapId, x, y, nowMs, kUnreachableNotedMs, kUnreachableRadiusYd);
        return false;
    }
}
