#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace ai
{
inline float FankrissHealingSearchRange(float ordinary, bool active)
{
    // Search the bounded raid roster beyond the usual two-heal-range envelope
    // after a web. This is candidate discovery, never a spell range override.
    return active ? std::max(ordinary, 200.0f) : ordinary;
}
struct FankrissRearPoint { float x, y; };
inline FankrissRearPoint FankrissRear(float x, float y, float facing, float meleeReach)
{
    float const radius = meleeReach * 0.7f;
    return {x - std::cos(facing) * radius, y - std::sin(facing) * radius};
}
inline bool FankrissAtRear(float x, float y, FankrissRearPoint goal, bool melee, bool behind)
{
    return melee && behind && std::hypot(x - goal.x, y - goal.y) <= 1.5f;
}
inline bool FankrissOrdinaryMovement(std::string const& name)
{
    return name == "follow" || name == "follow master" || name == "formation station" ||
        name == "move to position" || name == "reach melee" || name == "reach spell" ||
        name == "reach target" || name == "set behind" || name == "move out of contact" ||
        name == "move out of enemy contact" || name == "flee" || name == "flee to master";
}
inline bool FankrissMovementBlocked(std::string const& name, bool stack, bool healing, bool healHere)
{
    if (stack && (FankrissOrdinaryMovement(name) || name == "dps aoe")) return true;
    if (!healing) return false;
    if (name == "reach party member to heal") return healHere;
    if (name == "flee" || name == "flee to master") return healHere;
    return FankrissOrdinaryMovement(name);
}
}
