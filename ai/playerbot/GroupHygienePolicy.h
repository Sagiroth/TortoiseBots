#pragma once

#include <cstdint>

namespace ai
{
    // Group-hygiene gate (grouphygiene, SOC-G2).
    //
    // Donor mod-playerbots far-away leave for grouped bots: a bot that
    // shares no map with its group master, or stands more than twice the
    // RPG roam distance away, cannot contribute to the group and should
    // leave it.
    //
    // Pure decision rule, no core includes: the caller (LeaveFarAwayAction)
    // passes the already-known map sameness and distance, and the rule only
    // renders the verdict. Map sameness short-circuits first inside the
    // rule; the caller reads the master through the live-resolved group
    // master pointer (never raw group iteration).

    // SOC-G2: leave when the group cannot be contributed to.
    inline bool GroupFarAwayLeave(bool sameMap, float distance2d, float rpgDistance)
    {
        if (!sameMap)
            return true;
        return distance2d >= 2 * rpgDistance;
    }
}
