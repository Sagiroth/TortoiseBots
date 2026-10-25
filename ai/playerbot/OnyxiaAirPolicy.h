#pragma once
#include <cmath>

namespace ai
{
    inline bool OnyxiaAirPhase(bool alive, bool engaged, bool hover, bool flying)
    {
        return alive && engaged && (hover || flying);
    }

    inline bool OnyxiaWhelpRole(bool tank, bool ranged, bool healer)
    {
        return tank || (!ranged && !healer);
    }

    inline bool OnyxiaAirWhelpAllowed(bool ranged, bool healerVictim, bool ownGroup)
    {
        return !ranged || (healerVictim && ownGroup);
    }

    inline float OnyxiaWhelpScore(bool tank, bool needsPickup, bool current, float distance)
    {
        return (tank && needsPickup ? 1000.0f : 0.0f) + (current ? 10.0f : 0.0f) - distance;
    }

    inline bool OnyxiaAirApproachFloor(float x, float y, float z)
    {
        // Outer arena floor and central west lane; egg caves lie beyond this
        // apron. Actual paths must also clear eggs and return inward.
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
            x >= -80.0f && x <= 35.0f && y >= -252.0f && y <= -180.0f &&
            z >= -94.0f && z <= -80.0f;
    }

    // Conservative central floor, inset from the eight flight endpoints and
    // the whelp-spawn/egg-pit sides in Turtle's boss_onyxia.cpp. This bounds
    // candidates; navmesh, height and hazard checks are still required.
    inline bool OnyxiaSpreadFloor(float x, float y, float z)
    {
        float const dx = (x + 25.0f) / 30.0f;
        float const dy = (y + 215.0f) / 22.0f;
        return std::isfinite(z) && z >= -93.0f && z <= -80.0f && dx * dx + dy * dy <= 1.0f;
    }
}
