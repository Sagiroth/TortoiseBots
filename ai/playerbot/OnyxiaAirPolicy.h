#pragma once
#include <cmath>

namespace ai
{
    inline bool OnyxiaAirPhase(bool alive, bool engaged, bool hover, bool flying)
    {
        return alive && engaged && (hover || flying);
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
