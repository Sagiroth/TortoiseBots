#pragma once

#include <cstdint>

namespace ai
{
    // Group-hygiene gates (grouphygiene, SOC-G2/SOC-G4).
    //
    // Two small donor mod-playerbots behaviors for grouped bots:
    // - SOC-G2 far-away leave: a bot that shares no map with its group
    //   master, or stands more than twice the RPG roam distance away, cannot
    //   contribute to the group and should leave it.
    // - SOC-G4 dungeon leadership: a bot leading a group whose real-player
    //   master stands in the same dungeon should yield leadership - the bot
    //   does not know the dungeon, the master does.
    //
    // Pure decision rules, no core includes: callers (LeaveFarAwayAction,
    // GiveLeaderInDungeonAction) pass already-known map ids, distances, and
    // flags, and the rules only render the verdict.

    // SOC-G2: leave when the group cannot be contributed to. Cheap first:
    // the caller compares map ids before reading distance.
    inline bool GroupFarAwayLeave(bool sameMap, float distance2d, float rpgDistance)
    {
        if (!sameMap)
            return true;
        return distance2d >= 2 * rpgDistance;
    }

    // SOC-G4: yield leadership when the bot leads, the master is a live
    // real player in the world, the master stands in a dungeon on the bot's
    // map. Every clause must hold; anything else keeps leadership.
    inline bool DungeonLeadershipYield(bool botIsLeader, bool masterIsRealPlayer,
        bool masterInWorld, bool masterMapIsDungeon, bool sameMap)
    {
        return botIsLeader && masterIsRealPlayer && masterInWorld &&
            masterMapIsDungeon && sameMap;
    }
}
