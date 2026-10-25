#pragma once
#include <cmath>
namespace ai
{
inline bool MaexxnaRescueEligible(bool enabled, bool encounter, bool tank, bool healer, bool ordered)
{
    return enabled && encounter && !tank && !healer && !ordered;
}
inline bool MaexxnaWrapAccessible(bool ranged, bool meleeReach, bool lineOfSight, float distance, float range)
{
    return lineOfSight && std::isfinite(distance) && std::isfinite(range) && distance >= 0 &&
        ((ranged && range > 0 && distance <= range) || meleeReach);
}
// Finish the current live wrap before switching to another; otherwise prefer nearby wraps.
inline float MaexxnaWrapScore(bool current, float distance)
{
    return (current ? 1000.0f : 0.0f) - distance;
}
}
